// Crossfade (spec 6.3)
//   pre-roll 4 s (discarded) -> synthesise L + X samples s[0 .. L+X).
//   The loop is written rotated by X so the head never has to be held in RAM:
//     file = s[X .. L)  followed by  s[L+n]*cos + s[n]*sin  for n in [0, X)
//   (equal-power curve). The last sample flows into s[X], which is where the
//   file starts, so the wrap is seamless. s[0 .. X) is parked in a temporary
//   float file on the SD card in the meantime (4 s = 352 KB, too much for SRAM).
//
// Strict (spec 6.4)
//   Every Lfo period is snapped to a divisor of L, every sustained oscillator
//   to a whole number of cycles per loop, and every layer reseeds its event
//   stream at each loop start (see layers/layer.h). Warm-up passes are rendered
//   first (one, or two for loops under 16 s) and only the pass after them is
//   written: it starts with the reverb tail and the events still ringing from
//   the passes before, which is exactly what its own end leads into.
#include "export.h"
#include <SD.h>
#include "audio.h"
#include "storage.h"
#include "config.h"
#include <new>
#include "oneshot_core.h"

using namespace dsp;

static ExportConfig g_cfg;
static volatile ExportState g_state = EXS_IDLE;
static volatile float g_progress = 0.f;
static volatile bool g_cancel = false;
static const char* volatile g_stage = "";
static String g_result;

static const char* TMP_PATH = DIR_REC "/~xfhead.tmp";
static constexpr float PREROLL_SEC = 4.f;

// --- WAV writer with a small staging buffer --------------------------------
struct WavOut {
  File f;
  uint32_t bytes = 0;
  int16_t buf[2048];
  int n = 0;
  bool ok = true;
  void flush() {
    if (!n || !ok) { n = 0; return; }
    SdLock lk;
    size_t w = f.write((const uint8_t*)buf, n * 2);
    if (w != (size_t)n * 2) ok = false;
    bytes += w;
    n = 0;
  }
  inline void put(int16_t s) { buf[n++] = s; if (n == 2048) flush(); }
};
static WavOut g_wav;

// Normalize (spec v1.1, 4.3): the export is rendered twice. Synthesis is fully
// deterministic, so pass 1 (measuring the peak of the final, crossfaded
// output) and pass 2 (writing it with one constant gain, dithered after the
// gain) produce exactly the same samples. Re-rendering was chosen over a
// float temp file of the whole loop (up to 5.6 MB of extra SD writes).
static constexpr float NORM_TARGET = 0.891251f;   // -1 dBFS

// Stems (v0.7, spec v1.1 8.1): one synthesis, every stem at once. The engine
// adds each layer into the bus of its group (DRONE: the drones and HEART;
// EVENTS: the event layers); the DC blocker (linear: the stems still add up)
// and the reverb are done here per stem:
//   Wet          DRONE and EVENTS each through a reverb of their own (the
//                engine's, and a second one allocated only for the export)
//   Dry+Reverb   DRONE and EVENTS dry; REVERB = the reverb of their sum,
//                wet only (the three files add up to the mix)
// No master limiter on stems (it is not linear: the stems would no longer add
// up). Instead they are always rendered twice: pass 1 finds the highest peak
// of any stem, pass 2 writes them all with one common gain -- the Normalize
// gain, or with Normalize off just enough to keep every stem from clipping.
static constexpr int MAXCH = 3;
static const char* const STEM_NAMES[MAXCH] = {"DRONE", "EVENTS", "REVERB"};

// working memory for a stem export, allocated only while one runs
struct StemWork {
  float bus[MAXCH][BUF];       // DRONE, EVENTS, REVERB
  float head[MAXCH * BUF];     // Crossfade: the parked head, all stems of a chunk
  WavOut out[MAXCH - 1];       // files 2 and 3 (file 1 is g_wav)
  DcBlock dc[2];
};

// "<dir>/<prefix>_NNN" with the first NNN whose DRONE file is free
static String nextStemBase(const String& dir, const String& prefix) {
  SdLock lk;
  char num[8];
  for (int i = 1; i <= 999; i++) {
    snprintf(num, sizeof(num), "_%03d", i);
    String b = dir + "/" + prefix + num;
    if (!SD.exists(b + "_DRONE.wav") && !SD.exists(b + ".wav")) return b;
  }
  return String();
}

static volatile int g_batchI = 0, g_batchN = 1;
static StemWork* g_sw = nullptr;   // a stem export's working memory (runExport allocates it)
static Reverb* g_rev2 = nullptr;

// One file (or one set of stems) from g_cfg, into `dir`. On success `out` is
// what the result screen shows for it.
static bool runOne(const String& dir, String& out) {
  ExportConfig& c = g_cfg;
  const bool strict = c.mode == EXM_STRICT;
  const uint32_t L = (uint32_t)c.lenSec * SR;
  const uint32_t X = strict ? 0 : (uint32_t)c.xfSec * SR;
  const uint32_t P = strict ? 0 : (uint32_t)(PREROLL_SEC * SR);
  // Strict warm-up passes before the written one. The longest events (surges,
  // drill runs, insects, drags: up to ~12 s) can outlast an 8 s loop and reach
  // two passes ahead, so short loops get two warm-up passes instead of one (v0.17).
  const int warm = strict ? (c.lenSec < 16 ? 2 : 1) : 0;
  const uint32_t perPass = strict ? (uint32_t)(warm + 1) * L : P + L + X;
  const int stems = c.stems;                         // 0 off, 1 Wet, 2 Dry + Reverb
  const int K = stems == 0 ? 1 : (stems == 1 ? 2 : 3);   // files
  const int passes = (c.normalize || stems) ? 2 : 1;
  const uint32_t total = perPass * passes;
  const bool record = !strict && c.driftRecord && c.rt.drift;
  uint32_t done = 0;
  g_wav.ok = true; g_wav.n = 0; g_wav.bytes = 0;

  g_stage = "Checking SD";
  if (!gSdReady) { g_result = "No SD card"; return false; }
  uint64_t need = ((uint64_t)L * 2 + 44 + (uint64_t)X * 4) * K + 256 * 1024;
  if (sdFreeBytes() < need) { g_result = "SD card is full"; return false; }

  char seedStr[8];
  snprintf(seedStr, sizeof(seedStr), "%06lu", (unsigned long)(c.seed % 1000000UL));
  String prefix = c.moodTag + (strict ? "_ST_" : "_XF_") + String(c.lenSec) + "s_" + seedStr;
  // Drift Record depends on Drift Speed, so its files say which (spec v1.1, 4.1.5)
  if (record) { static const char* const TAG[4] = {"_DRs", "_DRm", "_DRf", "_DRc"}; prefix += TAG[c.rt.driftSpeed < 4 ? c.rt.driftSpeed : 1]; }
  prefix += c.arcTag;   // Arc applied: which section it was in
  String paths[MAXCH];
  if (!stems) {
    paths[0] = nextNumberedPath(dir.c_str(), prefix);
    if (!paths[0].length()) { g_result = "No free file name"; return false; }
  } else {
    const String base = nextStemBase(dir, prefix);
    if (!base.length()) { g_result = "No free file name"; return false; }
    for (int k = 0; k < K; k++) paths[k] = base + "_" + STEM_NAMES[k] + ".wav";
  }

  // stems: the working memory and (Wet) the second reverb, only now
  // the stems' working memory and second reverb: allocated once per export by
  // runExport() (v0.8: a batch used to allocate them again for every file)
  StemWork* sw = stems ? g_sw : nullptr;
  Reverb* rev2 = stems == 1 ? g_rev2 : nullptr;
  if (stems) gEngine.setStemBuses(sw->bus[0], sw->bus[1]);
  WavOut* outs[MAXCH] = {&g_wav, sw ? &sw->out[0] : nullptr, sw ? &sw->out[1] : nullptr};

  static EngineParams p;
  static float buf[BUF];
  static float head1[BUF];
  float* head = sw ? sw->head : head1;
  float* ch[MAXCH] = {buf, nullptr, nullptr};
  if (sw) for (int k = 0; k < K; k++) ch[k] = sw->bus[k];
  File tmp;
  bool fail = false;
  bool open[MAXCH] = {false, false, false};
  float peak = 0.f, gain = 1.f;

  auto finish = [&]() {   // the engine back to the normal mix (the memory stays for the next file)
    gEngine.setStemBuses(nullptr, nullptr);
  };
  auto cleanup = [&]() {
    {
      SdLock lk;
      if (tmp) tmp.close();
      for (int k = 0; k < K; k++) if (open[k]) { outs[k]->f.close(); SD.remove(paths[k]); }
      SD.remove(TMP_PATH);
    }
    finish();
  };

  for (int pass = 0; pass < passes && !g_cancel && !fail; pass++) {
    const bool write = pass == passes - 1;
    if (write) {
      if (c.normalize) gain = peak > 1e-6f ? NORM_TARGET / peak : 1.f;
      else if (stems && peak > NORM_TARGET) gain = NORM_TARGET / peak;   // no limiter: just never clip
      bool bad = false;
      {
        SdLock lk;
        for (int k = 0; k < K && !bad; k++) {
          WavOut& w = *outs[k];
          w.f = SD.open(paths[k], FILE_WRITE);
          if (!w.f) { bad = true; break; }
          open[k] = true;
          wavWriteHeader(w.f, 0);
          w.bytes = 0; w.n = 0; w.ok = true;
        }
      }
      if (bad) { g_result = "Cannot create file"; cleanup(); return false; }
    }
    // Every pass starts from the same snapshot: same seed -> same samples
    SceneRuntime rt = c.rt;
    if (record) rt.driftBegin(c.seed);   // deterministic: same seed, same drift path
    else { rt.finishXf(); rt.drift = false; }
    // EXPORT > Arc: Apply -- Arc as it stood when the export started (never its
    // climax); Ignore -- the scene itself, so the same scene and seed always
    // give the same file. Listen and Cursed stay out of exports as before.
    static SceneData arcd;
    auto build = [&]() {
      const SceneData* s = &rt.live;
      if (c.arcDread) {
        arcd = rt.live;
        arcd.dread = (uint8_t)constrain((int)arcd.dread + c.arcDread, 0, 100);
        s = &arcd;
      }
      buildEngineParams(*s, EXPORT_GAIN, c.seed, 0, p);
      p.densMul *= c.arcDens;
    };
    build();
    gEngine.reset(c.seed, strict ? L : 0, p, true);
    Ditherer dith[MAXCH];
    for (int k = 0; k < K; k++) dith[k].r.seed(deriveSeed(c.seed, STREAM_DITHER + k));
    if (sw) { sw->dc[0].clear(); sw->dc[1].clear(); }
    if (rev2) rev2->clear();
    bool firstChunk = true;

    // The export task runs flat out on PRO_CPU. The Arduino core's task watchdog
    // reboots the device if core 0's idle task gets no time for 5 s, which is
    // what happened ~5 s into every export before v0.18. A 1 ms pause every 8
    // chunks (~0.19 s of audio) keeps it fed at a negligible cost.
    int yieldCnt = 0;
    auto renderChunk = [&](int n) {
      if (++yieldCnt >= 8) { yieldCnt = 0; vTaskDelay(1); }
      if (record) {
        rt.update((float)n / SR);
        build();
        gEngine.setParams(p);
      }
      gEngine.render(buf, n);
      if (sw) {   // the stems: DC blocker, reverb(s), the export level
        float* d = sw->bus[0];
        float* e = sw->bus[1];
        for (int s = 0; s < n; s++) { d[s] = sw->dc[0].tick(d[s]); e[s] = sw->dc[1].tick(e[s]); }
        sw->dc[0].flush(); sw->dc[1].flush();
        const LayerCtl& rc = p.L[L_REVERB];
        Reverb& r1 = gEngine.reverb();
        r1.setParams(rc.level, rc.p[0], rc.p[1], firstChunk);
        if (stems == 1) {
          r1.process(d, n);
          rev2->setParams(rc.level, rc.p[0], rc.p[1], firstChunk);
          rev2->process(e, n);
        } else {
          float* r = sw->bus[2];
          for (int s = 0; s < n; s++) r[s] = d[s] + e[s];
          r1.process(r, n);   // dry + wet ...
          for (int s = 0; s < n; s++) r[s] -= d[s] + e[s];   // ... -> wet only
        }
        for (int k = 0; k < K; k++) for (int s = 0; s < n; s++) sw->bus[k][s] *= p.master;
      }
      firstChunk = false;
      done += n;
      g_progress = (float)done / (float)total;
    };
    // pass 1 (two-pass exports) only measures; the last pass writes
    auto emit = [&](int k, float y) {
      if (write) outs[k]->put(dith[k].q(y * gain));
      else { float a = fabsf(y); if (a > peak) peak = a; }
    };
    auto writeOk = [&]() { for (int k = 0; k < K; k++) if (!outs[k]->ok) return false; return true; };

    if (strict) {
      // warm-up passes: settle reverb / filters and bring in the ring-over of
      // events from earlier passes, exactly as the written pass will hear it
      g_stage = write ? "Warm-up" : "Analyzing (1/2)";
      for (int w = 0; w < warm && !g_cancel; w++) {
        for (uint32_t i = 0; i < L && !g_cancel; ) {
          int n = (L - i < (uint32_t)BUF) ? (int)(L - i) : BUF;   // chunks end exactly on the loop boundary
          renderChunk(n);
          i += n;
        }
      }
      if (write) g_stage = "Writing";
      for (uint32_t i = 0; i < L && !g_cancel && writeOk(); ) {
        int n = (L - i < (uint32_t)BUF) ? (int)(L - i) : BUF;
        renderChunk(n);
        for (int k = 0; k < K; k++) for (int s = 0; s < n; s++) emit(k, ch[k][s]);
        i += n;
      }
    } else {
      g_stage = write ? "Pre-roll" : "Analyzing (1/2)";
      for (uint32_t i = 0; i < P && !g_cancel; ) {
        int n = (P - i < (uint32_t)BUF) ? (int)(P - i) : BUF;
        renderChunk(n);
        i += n;
      }
      // head s[0 .. X) -> temp file (float; all stems of a chunk one after
      // another). Pass 2 renders it again (the engine has to run through it)
      // but the file from pass 1 is identical.
      if (write) g_stage = "Rendering";
      const bool writeHead = pass == 0;
      if (writeHead) {
        SdLock lk;
        SD.remove(TMP_PATH);
        tmp = SD.open(TMP_PATH, FILE_WRITE);
        if (!tmp) { g_result = "Cannot create temp file"; }
      }
      if (writeHead && !tmp) { cleanup(); return false; }
      for (uint32_t i = 0; i < X && !g_cancel && !fail; ) {
        int n = (X - i < (uint32_t)BUF) ? (int)(X - i) : BUF;
        renderChunk(n);
        if (writeHead) {
          SdLock lk;
          for (int k = 0; k < K; k++)
            if (tmp.write((const uint8_t*)ch[k], n * 4) != (size_t)n * 4) fail = true;
        }
        i += n;
      }
      {
        SdLock lk;
        if (tmp) tmp.close();
        tmp = SD.open(TMP_PATH, FILE_READ);
      }
      if (!tmp) fail = true;
      // body s[X .. L)
      for (uint32_t i = X; i < L && !g_cancel && writeOk() && !fail; ) {
        int n = (L - i < (uint32_t)BUF) ? (int)(L - i) : BUF;
        renderChunk(n);
        for (int k = 0; k < K; k++) for (int s = 0; s < n; s++) emit(k, ch[k][s]);
        i += n;
      }
      // tail s[L .. L+X) crossfaded onto the head (equal power), every stem alike
      if (write) g_stage = "Crossfading";
      for (uint32_t i = 0; i < X && !g_cancel && writeOk() && !fail; ) {
        int n = (X - i < (uint32_t)BUF) ? (int)(X - i) : BUF;
        renderChunk(n);
        {
          SdLock lk;
          if (tmp.read((uint8_t*)head, n * 4 * K) != n * 4 * K) fail = true;
        }
        for (int s = 0; s < n; s++) {
          float t = ((float)(i + s) + 0.5f) / (float)X;          // 0..1 across the overlap
          float fin = sinW(0.25f * t), fout = sinW(0.25f * t + 0.25f);   // sin / cos of t*pi/2
          for (int k = 0; k < K; k++) emit(k, ch[k][s] * fout + head[k * n + s] * fin);
        }
        i += n;
      }
    }
  }
  for (int k = 0; k < K; k++) if (open[k]) outs[k]->flush();

  if (g_cancel) { cleanup(); return false; }
  bool ok = !fail;
  for (int k = 0; k < K; k++) ok = ok && outs[k]->ok;
  if (!ok) { g_result = "SD write error"; cleanup(); return false; }

  {
    SdLock lk;
    for (int k = 0; k < K; k++) { wavPatchSizes(outs[k]->f, outs[k]->bytes); outs[k]->f.close(); }
    if (tmp) tmp.close();
    SD.remove(TMP_PATH);
  }
  finish();
  if (stems) out = paths[0].substring(0, paths[0].length() - 10) + "_*.wav (" + String(K) + " stems)";   // minus "_DRONE.wav"
  else out = paths[0];
  return true;
}

uint64_t exportEstimateBytes(const ExportConfig& c) {
  if (c.oneshot) return (uint64_t)(c.osCount ? c.osCount : 1) * 20 * SR * 2 + 64 * 1024;   // at most ~20 s each
  const uint64_t L = (uint64_t)c.lenSec * SR, X = c.mode == EXM_STRICT ? 0 : (uint64_t)c.xfSec * SR;
  const int K = c.stems == 0 ? 1 : (c.stems == 1 ? 2 : 3);
  const int n = c.batch ? c.batch : 1;
  return (L * 2 + 44) * K * n + X * 4 * K + 256 * 1024;
}
int exportBatchIndex() { return g_batchI; }
int exportBatchCount() { return g_batchN; }

// One file, or a batch (spec v1.1 8.2): seed, seed + 1, ... Unknown and Drift
// make their scene from each seed; a preset or a custom scene stays the same
// and only the seed changes. Esc keeps the files already written (the one
// being written is removed); an empty batch folder goes too.
static bool runExport() {
  ExportConfig& c = g_cfg;
  const int count = c.batch ? c.batch : 1;
  g_batchI = 0; g_batchN = count;
  const uint32_t seed0 = c.seed % 1000000UL;
  const SceneRuntime rt0 = c.rt;
  char s6[8];
  snprintf(s6, sizeof(s6), "%06lu", (unsigned long)seed0);
  String dir = DIR_REC;
  bool madeDir = false;
  if (c.batch && c.batchFolder) {
    if (!gSdReady) { g_result = "No SD card"; return false; }
    SdLock lk;
    char num[8];
    for (int i = 1; i <= 999 && !madeDir; i++) {
      snprintf(num, sizeof(num), "_%03d", i);
      String d = String(DIR_REC) + "/Batch_" + c.moodTag + "_" + s6 + num;
      if (!SD.exists(d)) { if (!SD.mkdir(d)) break; dir = d; madeDir = true; }
    }
    if (!madeDir) { g_result = "Cannot create folder"; return false; }
  }
  // stems: the working memory and (Wet) the second reverb, once for the whole batch
  if (c.stems) {
    g_sw = new (std::nothrow) StemWork();
    if (c.stems == 1) g_rev2 = new (std::nothrow) Reverb();
    if (!g_sw || (c.stems == 1 && !g_rev2)) {
      delete g_sw; delete g_rev2; g_sw = nullptr; g_rev2 = nullptr;
      if (madeDir) { SdLock lk; SD.rmdir(dir); }
      g_result = "Not enough memory";
      return false;
    }
  }
  int written = 0;
  String one;
  bool ok = true;
  for (int b = 0; b < count && !g_cancel; b++) {
    g_batchI = b;
    c.seed = (seed0 + (uint32_t)b) % 1000000UL;
    c.rt = rt0;
    if (c.batch && c.sceneKind == 1) { generateRandomScene(c.seed, c.rt.live, RANDOM_SCENE_NAME); c.rt.xf = false; }
    else if (c.batch && c.sceneKind == 2) { generateRandomScene(c.seed, c.rt.live, DRIFT_NAME); c.rt.xf = false; c.rt.drift = true; }
    if (!runOne(dir, one)) { ok = false; break; }
    written++;
  }
  delete g_sw; delete g_rev2; g_sw = nullptr; g_rev2 = nullptr;
  if (madeDir && written == 0) { SdLock lk; SD.rmdir(dir); }
  if (!c.batch) { if (ok) g_result = one; return ok; }
  char tail[48];
  snprintf(tail, sizeof(tail), "%d file%s, seeds %06lu-%06lu", written, written == 1 ? "" : "s",
           (unsigned long)seed0, (unsigned long)((seed0 + (uint32_t)(written ? written - 1 : 0)) % 1000000UL));
  if (g_cancel) { g_result = written ? String(written) + " written, kept" : String(); return false; }
  if (!ok) { g_result += String(" (after ") + written + ")"; return false; }
  g_result = dir + "/" + tail;
  return true;
}

// ===========================================================================
// One-shot export (v0.72, spec v1.1 8.3 / v1.2 5.3): one event per file, as
// sound-effect material, into /MIASMORA/oneshot/. How a one-shot is played --
// Single / Gesture per layer, where it ends -- is in oneshot_core.h (the PC
// test bench plays them the same way). Here: two passes per file (the first
// finds the end and the peak, the second writes with one gain: Normalize to
// -1 dBFS, or else only ever down so it never clips); Wet = through a reverb
// of its own afterwards (allocated for the export only), so a Single that was
// cut still gets its tail.
// ===========================================================================
struct OsWork { float cur[BUF], held[BUF]; EngineParams p; };   // allocated for the export only

static bool runOneshotFile(const ExportConfig& c, uint32_t fileSeed, const String& path, Reverb* rv, OsWork& w) {
  float peak = 0.f, gain = 1.f;
  int lastLoud = -1;
  bool wavOpen = false;
  for (int pass = 0; pass < 2 && !g_cancel; pass++) {
    const bool write = pass == 1;
    if (write) {
      gain = peak > 1e-6f ? (c.normalize ? NORM_TARGET / peak : (peak > NORM_TARGET ? NORM_TARGET / peak : 1.f)) : 1.f;
      SdLock lk;
      g_wav.f = SD.open(path, FILE_WRITE);
      if (!g_wav.f) { g_result = "Cannot create file"; return false; }
      wavOpen = true;
      wavWriteHeader(g_wav.f, 0);
      g_wav.bytes = 0; g_wav.n = 0; g_wav.ok = true;
    }
    if (rv) { rv->clear(); rv->setParams(c.osRevMix, c.osRevSize, c.osRevDamp, true); }
    Ditherer dith;
    dith.r.seed(deriveSeed(fileSeed, STREAM_DITHER));
    int bufs = 0;
    auto emit = [&](float* x, int n) {
      if ((++bufs & 7) == 0) vTaskDelay(1);   // the idle task's watchdog (see runOne)
      if (rv) { rv->setParams(c.osRevMix, c.osRevSize, c.osRevDamp, false); rv->process(x, n); }
      float pk = 0.f;
      for (int i = 0; i < n; i++) {
        const float a = fabsf(x[i]);
        if (a > pk) pk = a;
        if (write) g_wav.put(dith.q(x[i] * gain));
      }
      if (!write && pk > peak) peak = pk;
      g_progress = (pass + fminf(1.f, bufs * (float)BUF / (SR * 8.f))) * 0.5f;
      return pk;
    };
    const int loud = oneshot::play(c.osLayer, c.osUnit == 1, c.osState, fileSeed, c.osWet, write ? lastLoud : -1,
                                   w.p, w.cur, w.held, emit, nullptr, &g_cancel);
    if (!write) lastLoud = loud < 0 ? 0 : loud;
  }
  if (!wavOpen) return false;
  g_wav.flush();
  SdLock lk;
  if (g_cancel || !g_wav.ok) { g_wav.f.close(); SD.remove(path); if (!g_cancel) g_result = "SD write error"; return false; }
  wavPatchSizes(g_wav.f, g_wav.bytes);
  g_wav.f.close();
  return true;
}

static bool runOneshot() {
  ExportConfig& c = g_cfg;
  const int count = c.osCount < 1 ? 1 : c.osCount;
  g_batchI = 0; g_batchN = count;
  if (!gSdReady) { g_result = "No SD card"; return false; }
  {
    SdLock lk;
    if (!SD.exists(DIR_ONESHOT)) SD.mkdir(DIR_ONESHOT);
  }
  if (sdFreeBytes() < exportEstimateBytes(c)) { g_result = "SD card is full"; return false; }
  Reverb* rv = nullptr;
  OsWork* work = new (std::nothrow) OsWork();
  if (c.osWet) rv = new (std::nothrow) Reverb();
  if (!work || (c.osWet && !rv)) { delete work; delete rv; g_result = "Not enough memory"; return false; }
  char s6[8];
  snprintf(s6, sizeof(s6), "%06lu", (unsigned long)(c.seed % 1000000UL));
  const String prefix = c.osName + "_" + s6;
  int written = 0;
  bool ok = true;
  g_stage = "One-shot";
  for (int i = 0; i < count && !g_cancel; i++) {
    g_batchI = i;
    const String path = nextNumberedPath(DIR_ONESHOT, prefix);
    if (!path.length()) { g_result = "No free file name"; ok = false; break; }
    if (!runOneshotFile(c, deriveSeed(c.seed, 0x0500u + (uint32_t)i), path, rv, *work)) { ok = false; break; }
    written++;
    vTaskDelay(1);
  }
  delete rv;
  delete work;
  if (g_cancel) { g_result = written ? String(written) + " written, kept" : String(); return false; }
  if (!ok) { g_result += String(" (after ") + written + ")"; return false; }
  g_result = String(DIR_ONESHOT) + "/" + prefix + "_*.wav  " + written + (written == 1 ? " file" : " files");
  return true;
}

static void exportTask(void*) {
  bool ok = g_cfg.oneshot ? runOneshot() : runExport();
  if (g_cancel) g_state = EXS_CANCELLED;
  else g_state = ok ? EXS_DONE : EXS_FAILED;
  vTaskDelete(NULL);
}

bool exportBegin(const ExportConfig& cfg) {
  if (g_state == EXS_RUNNING) return false;
  g_cfg = cfg;
  g_cancel = false;
  g_progress = 0.f;
  g_result = "";
  g_stage = "Starting";
  g_state = EXS_RUNNING;
  if (xTaskCreatePinnedToCore(exportTask, "export", 10240, NULL, 2, NULL, PRO_CPU_NUM) != pdPASS) {
    g_state = EXS_FAILED;
    g_result = "Cannot start task";
    return false;
  }
  return true;
}

void exportCancel() { g_cancel = true; }
ExportState exportState() { return g_state; }
float exportProgress() { return g_progress; }
const char* exportStage() { return g_stage; }
String exportResult() { return g_result; }
void exportAcknowledge() { if (g_state != EXS_RUNNING) g_state = EXS_IDLE; }
bool exportBusy() { return g_state == EXS_RUNNING; }
