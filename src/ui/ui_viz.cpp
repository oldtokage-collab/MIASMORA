// MIASMORA — MAIN's visualiser (v0.42, spec v1.22 4)
//
// Off / Bars / Scope / Monitor / Layers / Ink / Spectrum, all in the area the
// Bars always had, all black with reds. The audio task only hands data over
// (gVizHist / gVizOnset every buffer, the gVizSnap mailbox on request); the
// work (zero-crossing search, auto gain, FFT, Ink's afterimage) runs here, on
// the UI core. They show our own output, never the microphone.
// Cursed Mode's "the visualiser freezes" holds every one of them.
#include "ui_internal.h"
#include <math.h>
#include <algorithm>
#include "../audio.h"
#include "../cursed.h"

const char* const VIZ_NAMES[NUM_VIZ] = {"Off", "Bars", "Scope", "Monitor", "Layers", "Ink", "Spectrum"};

// the area (the same as the Bars have always used)
static constexpr int VX = 4, VY = 53, VW = 232, VH = 46;
static constexpr int CELL = 2, GW = VW / CELL, GH = VH / CELL;   // 116 x 23 cells (Ink, Spectrum)

float gVizFftUs = 0.f;   // Debug > Performance

// Only one view runs at a time: their buffers share one block of memory
// (cleared whenever the view changes), ~7 KB instead of ~14 KB.
static constexpr int SCOPE_SPAN = 640;   // samples shown (~29 ms: a low drone's sway)
static constexpr int FFT_N = 512;
struct Drop { bool on; float x, y, vx, vy, life; };
static union VizMem {
  struct { int16_t s[SCOPE_SPAN]; } scope;
  struct { uint8_t y[VW], spike[VW]; } mon;
  struct { uint8_t cell[GH][GW]; Drop drops[12]; } ink;
  struct { uint8_t cell[GH][GW]; float re[FFT_N], im[FFT_N]; } spec;
} g_vm;

// dB of a linear level, mapped to 0..1 over -54..-4 dBFS (as the Bars)
static float level01(float v) {
  float db = v > 1e-6f ? 20.f * log10f(v) : -120.f;
  float f = (db + 54.f) / 50.f;
  return f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
}
static float dread01() { return constrain((int)gRt.live.dread + appDreadBoost(), 0, 100) * 0.01f; }

// a red for 0..1 (black -> dark red -> red -> near-white red)
static uint16_t heat(float v) {
  if (v <= 0.f) return C_BG;
  if (v > 1.f) v = 1.f;
  uint8_t r, g, b;
  if (v < 0.5f) { float t = v * 2.f; r = (uint8_t)(20 + 150 * t); g = (uint8_t)(4 + 10 * t); b = (uint8_t)(6 + 8 * t); }
  else if (v < 0.85f) { float t = (v - 0.5f) / 0.35f; r = (uint8_t)(170 + 75 * t); g = (uint8_t)(14 + 40 * t); b = (uint8_t)(14 + 26 * t); }
  else { float t = (v - 0.85f) / 0.15f; r = 245; g = (uint8_t)(54 + 150 * t); b = (uint8_t)(40 + 140 * t); }
  return rgb(r, g, b);
}

// --- new buffers since the last frame -------------------------------------------
// (levels and onsets of the output, read from the ring the audio task fills)
static int g_lastIdx = -1;
static int takeNew(float* lv, uint32_t* on, int maxN) {
  const int idx = gVizIdx;
  if (g_lastIdx < 0) { g_lastIdx = idx; return 0; }
  int n = (idx - g_lastIdx + VIZ_N) % VIZ_N;
  if (n > maxN) n = maxN;
  for (int k = 0; k < n; k++) {
    int i = (idx - n + k + VIZ_N) % VIZ_N;
    lv[k] = gVizHist[i];
    on[k] = gVizOnset[i];
  }
  g_lastIdx = idx;
  return n;
}

// --- Bars (the original view) -----------------------------------------------------
static void drawBars(bool frozen) {
  const int x0 = 4, cy = 76, maxH = 22;
  static float snap[VIZ_N];
  static int snapIdx = 0;
  if (!frozen) {
    for (int k = 0; k < VIZ_N; k++) snap[k] = gVizHist[k];
    snapIdx = gVizIdx;
  }
  float dread = gRt.live.dread * 0.01f;
  for (int k = 0; k < 58; k++) {
    int i = (snapIdx - 58 + k + VIZ_N * 2) % VIZ_N;
    float f = level01(snap[i]);
    int h = (int)(f * maxH);
    uint8_t r = (uint8_t)(50 + 150 * f + 40 * dread), g = (uint8_t)(14 + 20 * f), b = (uint8_t)(16 + 10 * f);
    int x = x0 + k * 4;
    if (h > 0) canvas.fillRect(x, cy - h, 3, h * 2 + 1, rgb(r, g, b));
    else canvas.drawFastHLine(x, cy, 3, C_LINE);
  }
}

// --- Scope ------------------------------------------------------------------------
// MORPHASE v0.31-32's way: a stretch aligned on a rising zero crossing, an
// auto gain that follows a louder signal at once and a quieter one slowly,
// "- silent -" when nothing has played for 300 ms. Low drones and noise may
// have no clean crossing: then the stretch is shown as it comes.
#define g_scope (g_vm.scope.s)
static bool g_scopeValid = false;
static float g_scopeEnv = 0.f;
static uint32_t g_scopeLoudMs = 0;

static void updateScope() {
  if (!gVizSnapReady) { if (!gVizSnapReq) gVizSnapReq = true; return; }
  // a rising zero crossing in the first part, with a little hysteresis
  int start = -1;
  for (int i = 1; i < VIZ_SNAP_N - SCOPE_SPAN; i++)
    if (gVizSnap[i - 1] < -60 && gVizSnap[i] >= 0) { start = i; break; }
  if (start < 0) start = 0;   // none: as it comes
  int pk = 0;
  for (int i = 0; i < SCOPE_SPAN; i++) {
    g_scope[i] = gVizSnap[start + i];
    int a = abs(g_scope[i]);
    if (a > pk) pk = a;
  }
  gVizSnapReady = false;
  gVizSnapReq = true;
  g_scopeValid = true;
  const float p = pk / 32768.f;
  g_scopeEnv = p > g_scopeEnv ? p : g_scopeEnv * 0.93f;   // fast up, slow down
  if (p > 0.002f) g_scopeLoudMs = millis();
}

static void drawScope(bool frozen) {
  if (!frozen) updateScope();
  const int cy = VY + VH / 2;
  canvas.drawFastHLine(VX, cy, VW, C_LINE);
  if (!g_scopeValid || millis() - g_scopeLoudMs > 300) {
    drawText("- silent -", VX + VW / 2, cy - 4, C_FAINT, 1, textdatum_t::top_center);
    return;
  }
  const float gain = std::min(60.f, 0.9f / std::max(g_scopeEnv, 0.004f));   // floor and a maximum gain
  const float ys = (VH / 2 - 1) * gain / 32768.f;
  const uint16_t col = rgb(225, 60, 50), dim = rgb(110, 20, 18);
  int px = VX, py = cy;
  for (int x = 0; x < VW; x++) {
    const int i = x * SCOPE_SPAN / VW;
    int y = cy - (int)(g_scope[i] * ys);
    y = constrain(y, VY, VY + VH - 1);
    if (x) { canvas.drawLine(px, py + 1, VX + x, y + 1, dim); canvas.drawLine(px, py, VX + x, y, col); }
    px = VX + x; py = y;
  }
}

// --- Monitor -----------------------------------------------------------------------
// An ECG: the output level in dB written left to right, wrapping round and
// overwriting the old trace a little ahead of the pen. Our own onsets (events,
// HEART's beats: what the engine tells Listen) stand out as spikes.
static constexpr float MON_SEC = 8.f;
#define g_monY (g_vm.mon.y)          // 0..VH-1 from the bottom
#define g_monSpike (g_vm.mon.spike)  // 0, or the spike's height
static int g_monX = 0;
static float g_monAcc = 0.f, g_monPeak = 0.f;
static uint32_t g_monOn = 0;

static void updateMonitor() {
  float lv[VIZ_N]; uint32_t on[VIZ_N];
  const int n = takeNew(lv, on, VIZ_N);
  const float pxPerBuf = (float)VW / (MON_SEC * SR / BUF);
  for (int k = 0; k < n; k++) {
    g_monPeak = std::max(g_monPeak, lv[k]);
    g_monOn |= on[k];
    g_monAcc += pxPerBuf;
    while (g_monAcc >= 1.f) {
      g_monAcc -= 1.f;
      g_monX = (g_monX + 1) % VW;
      g_monY[g_monX] = (uint8_t)(level01(g_monPeak) * (VH - 6));
      g_monSpike[g_monX] = g_monOn ? (uint8_t)(10 + level01(g_monPeak) * 10) : 0;
      g_monPeak = 0.f;
      g_monOn = 0;
    }
  }
}

static void drawMonitor(bool frozen) {
  if (!frozen) updateMonitor();
  const int base = VY + VH - 3;
  const uint16_t col = rgb(210, 40, 36);
  for (int g = 0; g < VW; g += 29) canvas.drawFastVLine(VX + g, VY, VH, rgb(22, 10, 10));   // grid
  const int gap = 8;   // erased ahead of the pen
  int px = -1, py = 0;
  for (int k = 1; k <= VW - gap; k++) {
    const int x = (g_monX + gap + k) % VW;
    int y = base - g_monY[x];
    if (px >= 0 && x > px) canvas.drawLine(VX + px, py, VX + x, y, col);
    if (g_monSpike[x]) {   // up, a little down, back: an R wave
      const int top = std::max(VY, y - g_monSpike[x]);
      canvas.drawLine(VX + x - 1, y, VX + x, top, rgb(245, 90, 70));
      canvas.drawLine(VX + x, top, VX + x + 1, std::min(VY + VH - 1, y + 4), rgb(245, 90, 70));
    }
    px = x; py = y;
  }
  canvas.fillCircle(VX + g_monX, base - g_monY[g_monX], 2, rgb(255, 200, 180));   // the pen
}

// --- Layers ---------------------------------------------------------------------------
// The sounding layers and how loud each is now, the loudest first, in two
// columns. Drones and events in two shades; an event lights up when it
// fires; a layer coming in or going out (Drift, mood changes) blinks.
static float g_layFlash[NUM_LAYERS];
static float g_laySnap[NUM_LAYERS];

static void drawLayers(bool frozen) {
  float lv[VIZ_N]; uint32_t on[VIZ_N];
  if (!frozen) {
    const int n = takeNew(lv, on, VIZ_N);
    for (int i = 0; i < NUM_LAYERS; i++) {
      g_layFlash[i] *= 0.75f;
      for (int k = 0; k < n; k++) if (on[k] & (1u << i)) g_layFlash[i] = 1.f;
      g_laySnap[i] = gEngine.activity(i);
    }
  }
  int ids[NUM_LAYERS], m = 0;
  for (int i = 0; i < NUM_LAYERS; i++)
    if (i != L_REVERB && gRt.live.L[i].on && gRt.live.L[i].level > 0) ids[m++] = i;
  std::sort(ids, ids + m, [](int a, int b) { return g_laySnap[a] > g_laySnap[b]; });
  if (m > 12) m = 12;
  const bool blinkOn = (millis() / 300) % 2;
  for (int k = 0; k < m; k++) {
    const int i = ids[k];
    const int col = k / 6, row = k % 6;
    const int x = VX + col * 118, y = VY + row * 8;
    // in or out of the scene right now: the crossfade is taking it to / from silence
    const bool moving = gRt.xf && ((gRt.from.L[i].on && gRt.from.L[i].level) != (gRt.to.L[i].on && gRt.to.L[i].level));
    const bool ev = layerIsEvent(i);
    uint16_t nameCol = moving ? (blinkOn ? C_INK : C_FAINT) : C_DIM;
    drawText(LAYER_DEFS[i].name, x, y, nameCol);
    const float a = std::min(1.f, sqrtf(g_laySnap[i]));
    const float fl = g_layFlash[i];
    const int w = (int)(a * 62);
    uint16_t c = ev ? rgb((uint8_t)(120 + 125 * fl), (uint8_t)(24 + 80 * fl), (uint8_t)(22 + 70 * fl))
                    : rgb(190, 34, 30);
    canvas.drawRect(x + 52, y + 1, 64, 6, C_LINE);
    if (w > 0) canvas.fillRect(x + 53, y + 2, w, 4, c);
  }
  if (!m) drawText("- nothing plays -", VX + VW / 2, VY + VH / 2 - 4, C_FAINT, 1, textdatum_t::top_center);
}

// --- Ink ----------------------------------------------------------------------------
// A red stain seeping out from the centre on black (the counterpart of Enso's
// ensō): wider with the level, slowly shrinking and fading when it is quiet;
// every event flings a drop that stays a while. DREAD darkens the red and
// tears the edge. The shape is a circle whose radius is shaken by noise per
// angle; the old picture fades a little each frame (an afterimage).
#define g_ink (g_vm.ink.cell)
static float g_inkLevel = 0.f, g_inkT = 0.f;
static dsp::Rng g_inkRng;   // the UI's own: never the sound's
#define g_drops (g_vm.ink.drops)

static float inkNoise(float a, float t) {   // smooth noise around the circle
  return 0.5f * sinf(3.f * a + 1.3f * t) + 0.3f * sinf(5.f * a - 0.9f * t + 1.f) + 0.2f * sinf(9.f * a + 2.1f * t + 2.f);
}

static void stamp(float cx, float cy, float r, float v) {
  const int x0 = std::max(0, (int)(cx - r)), x1 = std::min(GW - 1, (int)(cx + r));
  const int y0 = std::max(0, (int)(cy - r)), y1 = std::min(GH - 1, (int)(cy + r));
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
      if (d > r) continue;
      int nv = (int)(v * (1.f - d / (r + 0.01f) * 0.5f));
      if (nv > g_ink[y][x]) g_ink[y][x] = (uint8_t)std::min(255, nv);
    }
}

static void updateInk(float dt) {
  float lv[VIZ_N]; uint32_t on[VIZ_N];
  const int n = takeNew(lv, on, VIZ_N);
  float peak = 0.f; uint32_t ons = 0;
  for (int k = 0; k < n; k++) { peak = std::max(peak, lv[k]); ons |= on[k]; }
  const float lvl = level01(peak);
  g_inkLevel += (lvl - g_inkLevel) * (lvl > g_inkLevel ? 0.4f : 0.05f);   // grows fast, shrinks slowly
  g_inkT += dt;
  const float d = dread01();
  // fade the afterimage
  for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) g_ink[y][x] = (uint8_t)(g_ink[y][x] * 0.88f);
  // the stain: an ellipse (the area is wide) whose edge is shaken by noise,
  // worked out for 64 directions once per frame
  const float cx = GW / 2.f, cy = GH / 2.f;
  const float R = 2.f + g_inkLevel * 10.f;
  const float jag = 0.08f + 0.3f * d;
  static constexpr int NA = 64;
  float edgeR[NA];
  for (int k = 0; k < NA; k++)
    edgeR[k] = R * (1.f + jag * inkNoise(k * (6.2831853f / NA), g_inkT) + 0.1f * jag * g_inkRng.bi());
  const int ry = (int)(R * 1.5f) + 1, rx = (int)(R * 1.5f * 2.6f) + 1;
  for (int y = std::max(0, (int)cy - ry); y < std::min(GH, (int)cy + ry + 1); y++)
    for (int x = std::max(0, (int)cx - rx); x < std::min(GW, (int)cx + rx + 1); x++) {
      const float dx = (x - cx) / 2.6f, dy = y - cy;
      const float r = sqrtf(dx * dx + dy * dy);
      int k = (int)((atan2f(dy, dx) + 3.1415927f) * (NA / 6.2831853f));
      const float edge = edgeR[k < 0 ? 0 : (k >= NA ? NA - 1 : k)];
      if (r < edge) {
        int v = (int)(255.f * (0.35f + 0.65f * g_inkLevel) * (1.f - 0.6f * r / (edge + 0.01f)));
        if (v > g_ink[y][x]) g_ink[y][x] = (uint8_t)v;
      }
    }
  // drops: an event flings one from the centre
  if (ons) {
    for (auto& dr : g_drops) if (!dr.on) {
      const float a = g_inkRng.range(0.f, 6.2832f), sp = g_inkRng.range(6.f, 16.f);
      dr = {true, cx, cy, cosf(a) * sp * 2.6f, sinf(a) * sp, 1.f};
      break;
    }
  }
  for (auto& dr : g_drops) {
    if (!dr.on) continue;
    dr.x += dr.vx * dt; dr.y += dr.vy * dt;
    dr.vx *= 0.85f; dr.vy *= 0.85f;
    dr.life -= dt * 0.25f;
    stamp(dr.x, dr.y, 1.2f, 255.f * std::max(dr.life, 0.f));
    if (dr.life <= 0.f || dr.x < 0 || dr.x >= GW || dr.y < 0 || dr.y >= GH) dr.on = false;
  }
}

static void drawInk(bool frozen, float dt) {
  if (!frozen) updateInk(dt);
  const float d = dread01();
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) {
      const uint8_t v = g_ink[y][x];
      if (v < 10) continue;
      const float f = v / 255.f;
      // DREAD: a deeper, darker red
      const uint8_t r = (uint8_t)(f * (235.f - 60.f * d)), g = (uint8_t)(f * (40.f - 30.f * d)), b = (uint8_t)(f * (34.f - 24.f * d));
      canvas.fillRect(VX + x * CELL, VY + y * CELL, CELL, CELL, rgb(r, g, b));
    }
}

// --- Spectrum ---------------------------------------------------------------------------
// A spectrogram: a 512-point FFT of the mailbox (on this core), the strength
// per frequency (log axis, ~50 Hz .. 6 kHz) as brightness, one column per
// frame flowing to the left. SHEPARD's endless fall, MICRO's glides and VOX's
// vowels draw patterns.
#define g_spec (g_vm.spec.cell)   // [row][column], column GW-1 is the newest
#define g_fre (g_vm.spec.re)
#define g_fim (g_vm.spec.im)
static float g_specTop = -30.f;   // dB that maps to full brightness (follows the loudest)

static void fft(float* re, float* im, int n) {
  for (int i = 1, j = 0; i < n; i++) {   // bit reversal
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
  }
  for (int len = 2; len <= n; len <<= 1) {
    const float ang = -6.2831853f / len;
    const float wr = cosf(ang), wi = sinf(ang);
    for (int i = 0; i < n; i += len) {
      float cr = 1.f, ci = 0.f;
      for (int k = 0; k < len / 2; k++) {
        const int a = i + k, b = a + len / 2;
        const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti;
        re[a] += tr; im[a] += ti;
        const float ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr; cr = ncr;
      }
    }
  }
}

static void updateSpectrum() {
  if (!gVizSnapReady) { if (!gVizSnapReq) gVizSnapReq = true; return; }
  const int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < FFT_N; i++) {   // Hann window (from the sine table)
    const float w = 0.5f - 0.5f * dsp::sinW((float)i / (FFT_N - 1) + 0.25f);
    g_fre[i] = gVizSnap[i] / 32768.f * w;
    g_fim[i] = 0.f;
  }
  gVizSnapReady = false;
  gVizSnapReq = true;
  fft(g_fre, g_fim, FFT_N);
  // scroll left, new column on the right
  for (int r = 0; r < GH; r++) memmove(&g_spec[r][0], &g_spec[r][1], GW - 1);
  float top = -120.f;
  for (int r = 0; r < GH; r++) {
    // row r (0 = top) covers [f0, f1] on a log axis from 50 Hz to 6 kHz
    const float lo = 50.f * powf(120.f, (float)(GH - 1 - r) / GH), hi = 50.f * powf(120.f, (float)(GH - r) / GH);
    int b0 = (int)(lo * FFT_N / SR), b1 = (int)(hi * FFT_N / SR);
    if (b1 < b0) b1 = b0;
    float m = 0.f;
    for (int k = b0; k <= b1 && k < FFT_N / 2; k++) m = std::max(m, g_fre[k] * g_fre[k] + g_fim[k] * g_fim[k]);
    const float db = 10.f * log10f(m + 1e-12f);
    top = std::max(top, db);
    float v = (db - (g_specTop - 50.f)) / 50.f;   // a 50 dB window under the loudest
    g_spec[r][GW - 1] = (uint8_t)(constrain(v, 0.f, 1.f) * 255.f);
  }
  g_specTop += (top - g_specTop) * (top > g_specTop ? 0.5f : 0.02f);
  if (g_specTop < -60.f) g_specTop = -60.f;
  const float us = (float)(esp_timer_get_time() - t0);
  gVizFftUs = gVizFftUs * 0.9f + us * 0.1f;
}

static void drawSpectrum(bool frozen) {
  if (!frozen) updateSpectrum();
  for (int r = 0; r < GH; r++)
    for (int x = 0; x < GW; x++) {
      const uint8_t v = g_spec[r][x];
      if (v < 12) continue;
      canvas.fillRect(VX + x * CELL, VY + r * CELL, CELL, CELL, heat(v / 255.f));
    }
}

// --- dispatch ------------------------------------------------------------------------
void drawVisualizer() {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  const float dt = lastMs ? std::min(0.2f, (now - lastMs) * 0.001f) : 0.066f;
  lastMs = now;
  static uint8_t lastViz = 255;
  if (gSet.viz != lastViz) {   // a view starts afresh
    lastViz = gSet.viz;
    g_lastIdx = -1;
    gVizSnapReady = false;
    gVizSnapReq = false;
    g_scopeValid = false;
    memset(&g_vm, 0, sizeof(g_vm));
    g_monX = 0; g_monAcc = 0.f; g_monPeak = 0.f; g_monOn = 0;
    g_inkLevel = 0.f;
    g_specTop = -30.f;
  }
  const bool frozen = cursedVizFrozen();
  switch (gSet.viz) {
    case VIZ_BARS:     drawBars(frozen); break;
    case VIZ_SCOPE:    drawScope(frozen); break;
    case VIZ_MONITOR:  drawMonitor(frozen); break;
    case VIZ_LAYERS:   drawLayers(frozen); break;
    case VIZ_INK:      drawInk(frozen, dt); break;
    case VIZ_SPECTRUM: drawSpectrum(frozen); break;
    default: break;
  }
}

// Not on MAIN (or the display is off): the mailbox is left alone
void vizIdle() { gVizSnapReq = false; }
