#include "dsp/speed.h"   // first: speed-optimised (v0.32)
#include "audio.h"
#include <M5Cardputer.h>
#include <atomic>
#include <string.h>
#include "audio_io.h"
#include "rec.h"

using namespace dsp;

Engine gEngine;
volatile float gVizHist[VIZ_N];
volatile uint32_t gVizOnset[VIZ_N];
volatile int   gVizIdx = 0;
int16_t gVizSnap[VIZ_SNAP_N];
volatile bool gVizSnapReq = false, gVizSnapReady = false;
volatile float gAudioLoad = 0.f;
volatile float gAudioLoadPeak = 0.f;
volatile float gAudioLoadMean = 0.f;
volatile bool  gAudioStatsReset = false;
volatile float gAudioBusyMean = 0.f;
volatile uint32_t gLoadHist[LOAD_BINS];

float audioLoadP99() {
  uint64_t total = 0;
  for (int k = 0; k < LOAD_BINS; k++) total += gLoadHist[k];
  if (!total) return 0.f;
  uint64_t above = 0, lim = total / 100;   // the top 1 %
  for (int k = LOAD_BINS - 1; k >= 0; k--) {
    above += gLoadHist[k];
    if (above > lim) return (float)k * 0.01f;
  }
  return 0.f;
}

static constexpr int LAYER_FADE_IN = (int)(0.010f * SR);   // 10 ms onset ramp after a layer wakes

// Listen (v0.33): a layer's onset = its peak in this buffer 9 dB over the
// average of its recent peaks (in dB, ~0.3 s), and loud enough to be heard at
// all. Noise and drones wobble by a few dB from buffer to buffer and a swell
// rises slowly: neither counts. Every hit, beat and burst does.
static constexpr float ONSET_DB = 9.f;
static constexpr float ONSET_MIN_PEAK = 0.004f;   // -48 dBFS before the master gain
static constexpr float ONSET_AVG_COEF = 0.08f;    // per buffer (23 ms): ~0.3 s
static constexpr float ONSET_JUMP_DB = 3.f;      // after an onset: the average is set this far under it

// ===========================================================================
// Engine
// ===========================================================================
void Engine::begin() {
  for (int i = 0; i < NUM_LAYERS; i++) layers_[i] = createLayer(i);
  rev_.clear();
}

void Engine::reset(uint32_t seed, uint32_t loopLen, const EngineParams& p, bool snap) {
  seed_ = seed;
  if (loopLen) gClk.setLoop(loopLen); else gClk.setFree();
  gClk.pos = 0;
  p_ = p;
  for (int i = 0; i < NUM_LAYERS; i++) {
    asleep_[i] = true;      // every layer is (re)initialised from the seed when it wakes
    gain_[i] = 0.f;
    fade_[i] = 0;
    act_[i] = 0.f;
    onBase_[i] = -100.f;
  }
  onsets_ = 0;
  rev_.clear();
  dc_.clear();
  limEnv_ = 0.f;
  master_ = snap ? p.master : 0.f;
  snap_ = snap;
  lastCut_ = p.cutSeq;
  lastSummon_ = p.summonSeq;
  summonPending_ = false;
  pop_ = 0.f;
  sceneG_ = p.sceneMul;
  partyOn_ = false;
}

// Cursed Mode's "cut": a short crackling click on top of whatever plays
void Engine::addPop(float* out, int n) {
  if (pop_ <= 0.f) return;
  for (int s = 0; s < n && pop_ > 0.f; s++) {
    out[s] += popRng_.bi() * pop_;
    pop_ *= 0.995f;   // ~4.5 ms
    if (pop_ < 0.0005f) pop_ = 0.f;
  }
}

void Engine::render(float* out, int n) {
  gClk.lfoMul = p_.lfoMul;
  gClk.densMul = p_.densMul;
  gClk.tension = p_.tension;
  gClk.climax = p_.climax;
  gClk.hushSeq = p_.hushSeq;
  gClk.heartStopSeq = p_.heartStopSeq;
  gClk.heartStopBeats = p_.heartStopBeats;
  gClk.heartSummonOnly = p_.heartSummonOnly;
  onsets_ = 0;

  // Instant cut (Cursed Mode): the sound drops to nothing at once, with a click
  if (p_.cutSeq != lastCut_) {
    lastCut_ = p_.cutSeq;
    master_ = 0.f;
    pop_ = 0.7f;
    onsets_ |= ONSET_ENGINE;
  }

  // Summon / Listen's Response (v0.43): one event of a layer, now, on its own
  // random stream (Layer::beginSummon). Up to v0.42 the layer's next event
  // was simply made due, which shifted the rest of its events.
  if (p_.summonSeq != lastSummon_) {
    lastSummon_ = p_.summonSeq;
    summonPending_ = true;
    summonLayer_ = p_.summonLayer;
    summonSeed_ = p_.summonSeed;
  }

  // Strict loop: every pass starts its random streams afresh, so each pass
  // schedules exactly the same events. The exporter aligns render calls to
  // the loop boundary, so the boundary is always at the start of a call.
  if (gClk.loopLen && gClk.pos > 0 && (gClk.pos % gClk.loopLen) == 0) {
    for (int i = 0; i < NUM_LAYERS; i++)
      if (layers_[i] && !asleep_[i]) layers_[i]->loopStart(deriveSeed(seed_, LAYER_DEFS[i].stream));
  }

  // Stopped and faded out: no DSP at all
  if (p_.master <= 0.f && master_ < 0.0001f) {
    master_ = 0.f;
    memset(out, 0, sizeof(float) * n);
    addPop(out, n);
    for (int i = 0; i < NUM_LAYERS; i++) act_[i] *= 0.7f;
    gClk.pos += n;
    return;
  }

  memset(out, 0, sizeof(float) * n);
  if (stemD_) { memset(stemD_, 0, sizeof(float) * n); memset(stemE_, 0, sizeof(float) * n); }
  const bool snap = snap_;
  // Party Mode: the scene fades out, and while it is silent its layers are not
  // rendered at all (the party is lighter than a full scene). The UI restarts
  // the scene from its seed when the party is over.
  const float sNext = snap ? p_.sceneMul : sceneG_ + (p_.sceneMul - sceneG_) * 0.3f;
  const bool sceneSilent = sceneG_ < 0.0005f && p_.sceneMul <= 0.f;
  for (int i = 0; i < NUM_LAYERS && !sceneSilent; i++) {
    Layer* L = layers_[i];
    if (!L) continue;
    const LayerCtl& c = p_.L[i];
    float tgt = c.level;
    float g = gain_[i];
    float gNext = snap ? tgt : g + (tgt - g) * 0.3f;
    if (fabsf(gNext - tgt) < 0.0001f) gNext = tgt;
    if (g <= 0.f && gNext <= 0.f) {   // Level 0 or OFF: skip the layer entirely
      if (summonPending_ && i == summonLayer_) summonPending_ = false;
      asleep_[i] = true;
      gain_[i] = 0.f;
      act_[i] *= 0.7f;
      onBase_[i] = -100.f;
      continue;
    }
    if (asleep_[i]) {
      L->control(c, true);   // parameters first: reset() rolls the first event wait from them
      L->reset(deriveSeed(seed_, LAYER_DEFS[i].stream));
      asleep_[i] = false;
      fade_[i] = 0;
    } else {
      L->control(c, snap);
    }
    // a sleeping layer would re-roll on waking: only an awake one is summoned
    const bool summoned = summonPending_ && i == summonLayer_ && L->beginSummon(summonSeed_, n);
    if (summoned) summonPending_ = false;
    L->render(tmp_, n);
    if (summoned) L->endSummon(n);
    float gs = (gNext - g) / n, pk = 0.f;
    int f = fade_[i];
    for (int s = 0; s < n; s++) {
      float v = tmp_[s] * g;
      if (f < LAYER_FADE_IN) { v *= (float)f * (1.f / LAYER_FADE_IN); f++; }   // no divide (v0.32)
      float a = fabsf(v); if (a > pk) pk = a;
      out[s] += v;
      tmp_[s] = v;   // (for a stem bus)
      g += gs;
    }
    if (stemD_) {
      float* bus = layerIsEvent(i) ? stemE_ : stemD_;
      for (int s = 0; s < n; s++) bus[s] += tmp_[s];
    }
    fade_[i] = f;
    gain_[i] = gNext;
    float lv = pk * 2.5f; if (lv > 1.f) lv = 1.f;
    act_[i] += (lv - act_[i]) * (lv > act_[i] ? 0.5f : 0.15f);
    // Listen: a sudden rise of this layer's output, or one the layer reports
    // itself (HEART's second beat rises out of the first one's ring)
    const float db = pk > 1e-5f ? 20.f * log10f(pk) : -100.f;
    if (pk > ONSET_MIN_PEAK && (db > onBase_[i] + ONSET_DB || L->onsetFlag)) {
      onsets_ |= 1u << i;
      // one onset is one onset: the average jumps up to just under it, so the
      // same sound ringing on is not reported again in the next buffers
      if (onBase_[i] < db - ONSET_JUMP_DB) onBase_[i] = db - ONSET_JUMP_DB;
    }
    L->onsetFlag = false;
    onBase_[i] += (db - onBase_[i]) * ONSET_AVG_COEF;
  }

  // stem export: the exporter takes it from the buses
  if (stemD_) { snap_ = false; gClk.pos += n; return; }

  // DC blocker (sub + asymmetric drive produce DC)
  for (int s = 0; s < n; s++) out[s] = dc_.tick(out[s]);
  dc_.flush();
  if (sceneSilent) {
    sceneG_ = 0.f;
  } else if (sNext != 1.f || sceneG_ != 1.f) {
    float g = sceneG_, gs = (sNext - sceneG_) / n;
    for (int s = 0; s < n; s++) { out[s] *= g; g += gs; }
    sceneG_ = fabsf(sNext - p_.sceneMul) < 0.0005f ? p_.sceneMul : sNext;
  }
  // Party Mode's music, on top (it starts afresh each time)
  if (p_.party.on && !partyOn_) party_.reset(seed_ ^ 0x9A27u);
  partyOn_ = p_.party.on;
  party_.render(out, n, p_.party);

  // Reverb (REVERB "Level" is its mix; OFF = mix 0). At a party: short and thin.
  const LayerCtl& rc = p_.L[L_REVERB];
  if (p_.party.on) rev_.setParams(0.12f, 0.15f, 0.6f, snap);
  else rev_.setParams(rc.level, rc.p[0], rc.p[1], snap);
  rev_.process(out, n);
  act_[L_REVERB] = rev_.idle() ? 0.f : rc.level;

  // Master gain and soft limiter
  float mNext = snap ? p_.master : master_ + (p_.master - master_) * 0.25f;
  if (fabsf(mNext - p_.master) < 0.0001f) mNext = p_.master;
  float m = master_, ms = (mNext - master_) / n;
  const float thr = 0.72f;
  const float rel = 1.f - expf(-1.f / (0.25f * SR));
  float env = limEnv_;
  for (int s = 0; s < n; s++) {
    float x = out[s] * m;
    m += ms;
    float a = fabsf(x);
    env += (a - env) * (a > env ? 0.3f : rel);
    if (env > thr) x *= thr / env;
    if (x > 0.85f)       x = 0.85f + (x - 0.85f) / (1.f + (x - 0.85f) * 4.f);
    else if (x < -0.85f) x = -0.85f + (x + 0.85f) / (1.f - (x + 0.85f) * 4.f);
    if (x > 0.98f) x = 0.98f; else if (x < -0.98f) x = -0.98f;
    out[s] = x;
  }
  limEnv_ = flushDn(env);
  master_ = mNext;
  addPop(out, n);
  snap_ = false;
  gClk.pos += n;
}

// ===========================================================================
// Parameter hand-over
// ===========================================================================
// Enso v0.83 lesson: fields written one by one from the UI core while the
// audio core reads them can be seen half-updated (a torn combination of old
// and new values), which a held key (e.g. DREAD) makes very likely. So the
// UI fills the slot that is NOT current and then publishes it with a single
// atomic store of a generation counter. The reader copies the slot named by
// the generation and re-checks the counter afterwards; if the UI published in
// the meantime, the copy is simply retried. The audio side therefore always
// works from one complete, consistent parameter set.
static EngineParams g_slot[2];
static std::atomic<uint32_t> g_gen{0};

void paramsPublish(const EngineParams& p) {
  uint32_t g = g_gen.load(std::memory_order_relaxed);
  g_slot[(g + 1) & 1] = p;
  g_gen.store(g + 1, std::memory_order_release);
}

static void paramsAcquire(EngineParams& out) {
  for (int tries = 0; tries < 8; tries++) {
    uint32_t g1 = g_gen.load(std::memory_order_acquire);
    out = g_slot[g1 & 1];
    std::atomic_thread_fence(std::memory_order_acquire);
    uint32_t g2 = g_gen.load(std::memory_order_relaxed);
    if (g1 == g2) return;
  }
}

// ===========================================================================
// Live audio task (PRO_CPU)
// ===========================================================================
static volatile bool g_pauseReq = false;
static volatile bool g_paused = false;
static volatile bool g_resumeReset = false;

static void audioTask(void*) {
  static int16_t obuf[3][BUF];
  static float fb[BUF];
  static Ditherer dith;
  dith.r.seed(0xA0D10u);
  EngineParams p;
  paramsAcquire(p);
  gEngine.reset(p.seed, 0, p, false);
  uint32_t curReset = p.resetSeq;
  bool fadedOut = false;
  bool countDrops = false;   // the first poll only clears what came before the task ran
  double loadSum = 0.0, busySum = 0.0, lastPostUs = 0.0;
  int64_t lastBlockUs = esp_timer_get_time();   // the last time the write waited (CPU0 was free)
  uint32_t loadN = 0;
  int b = 0;
  const float bufUs = (float)BUF * 1e6f / SR;

  for (;;) {
    if (g_pauseReq) {
      if (!g_paused) {
        // One last buffer, faded to silence, so pausing never clicks
        paramsAcquire(p);
        gEngine.setParams(p);
        gEngine.render(fb, BUF);
        for (int s = 0; s < BUF; s++) obuf[b][s] = dith.q(fb[s] * (1.f - (float)s / BUF));
        audioIoWrite(obuf[b], BUF);
        b = (b + 1) % 3;
        g_paused = true;   // from here on the DMA plays silence by itself
        countDrops = false;   // that silence is intended: not a dropout
      }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    g_paused = false;

    paramsAcquire(p);
    float rampTo = 1.f;
    bool resetPending = g_resumeReset || p.resetSeq != curReset;
    if (resetPending) {
      if (!fadedOut && !gEngine.silent() && !g_resumeReset) {
        rampTo = 0.f;          // fade the old sound out over this buffer, restart on the next
        fadedOut = true;
      } else {
        gEngine.reset(p.seed, 0, p, false);
        curReset = p.resetSeq;
        g_resumeReset = false;
        fadedOut = false;
      }
    }

    int64_t t0 = esp_timer_get_time();
    gEngine.setParams(p);
    gEngine.render(fb, BUF);
    if (rampTo < 1.f) for (int s = 0; s < BUF; s++) fb[s] *= 1.f - (float)s / BUF;
    const float load = (float)(esp_timer_get_time() - t0) / bufUs;
    gAudioLoad = gAudioLoad * 0.9f + 0.1f * load;
    // Load test (v0.3): peak and mean since the last reset (HELP, [R]);
    // v0.31: a 1 % histogram for the p99, and the whole task's busy time
    if (gAudioStatsReset) {
      gAudioStatsReset = false;
      gAudioLoadPeak = 0.f; loadSum = 0.0; loadN = 0; busySum = 0.0;
      for (int k = 0; k < LOAD_BINS; k++) gLoadHist[k] = 0;
      audioIoResetDropouts();
    }
    if (load > gAudioLoadPeak) gAudioLoadPeak = load;
    loadSum += load; loadN++;
    gAudioLoadMean = (float)(loadSum / (double)loadN);
    int bin = (int)(load * 100.f);
    gLoadHist[bin < 0 ? 0 : bin >= LOAD_BINS ? LOAD_BINS - 1 : bin]++;
    busySum += lastPostUs;   // the previous buffer's work after its write (mic, events)

    float ss = 0.f;
    if (gEngine.silent()) {
      memset(obuf[b], 0, sizeof(obuf[b]));    // true silence when stopped
    } else {
      for (int s = 0; s < BUF; s++) {
        ss += fb[s] * fb[s];
        obuf[b][s] = dith.q(fb[s]);
      }
    }
    gVizHist[gVizIdx] = sqrtf(ss / BUF);
    gVizOnset[gVizIdx] = gEngine.onsets();
    gVizIdx = (gVizIdx + 1) % VIZ_N;
    // the visualiser's mailbox (only while the UI asks for it)
    static int snapPos = 0;
    if (gVizSnapReq && !gVizSnapReady) {
      int m = VIZ_SNAP_N - snapPos < BUF ? VIZ_SNAP_N - snapPos : BUF;
      memcpy(gVizSnap + snapPos, obuf[b], sizeof(int16_t) * m);
      snapPos += m;
      if (snapPos >= VIZ_SNAP_N) { snapPos = 0; gVizSnapReq = false; gVizSnapReady = true; }
    } else {
      snapPos = 0;
    }

    recPush(obuf[b], BUF);
    audioIoNoteOwnOnsets(gEngine.onsets());   // Listen: what this buffer starts (v0.33)
    const int64_t tW = esp_timer_get_time();
    busySum += (double)(tW - t0);   // render + dither + REC hand-off
    audioIoWrite(obuf[b], BUF);   // blocks while the DMA queue is full
    int64_t tA = esp_timer_get_time();
    // v0.32: never let an overload reset the device. While synthesis keeps up,
    // the write above blocks and CPU0's idle task runs. When it cannot keep up
    // the queue never fills, the task would never block, and the task
    // watchdog would reboot the device (v0.31, all 25 layers on). Then give
    // CPU0 1 ms every 50 ms; the sound drops out (counted in `dr`) instead.
    if (tA - tW > 100) lastBlockUs = tA;   // it waited: the idle task got its turn
    else if (tA - lastBlockUs > 50000) { vTaskDelay(1); tA = lastBlockUs = esp_timer_get_time(); }
    audioIoMicProcess();          // never waits; does nothing while the mic is off
    audioIoPollEvents(countDrops);
    countDrops = true;
    lastPostUs = (double)(esp_timer_get_time() - tA);
    gAudioBusyMean = (float)(busySum / ((double)loadN * bufUs));
    b = (b + 1) % 3;
  }
}

void audioStart() {
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 3, NULL, PRO_CPU_NUM);
}

bool audioPause(uint32_t timeoutMs) {
  g_pauseReq = true;
  uint32_t t0 = millis();
  while (!g_paused) {
    if (millis() - t0 > timeoutMs) return false;
    delay(2);
  }
  delay(60);   // let the speaker queue drain the faded tail
  return true;
}

void audioResume() {
  g_resumeReset = true;
  g_pauseReq = false;   // the task clears g_paused itself once it is running again
}
