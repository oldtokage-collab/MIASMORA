// MIASMORA — the start screen (v0.98, splash spec): the logo rises out of a
// miasma and pulses with four heartbeats, about 6.7 s in all.
//
//   0.0 .. 0.4 s   a dark red fog seeps in over the black
//   0.4 .. 1.8 s   pieces of the logo appear where the fog is densest and
//                  gather into the whole word (a threshold rising through the
//                  fog's own noise, with a soft edge)
//   1.8 .. 2.2 s   the fog thins
//   2.2 s          the first heartbeat: "lub" -- the logo glows, "dub" -- a
//                  smaller glow (HEART itself, summoned; Startup Sound off or
//                  Volume 0: the glow alone)
//   3.0 s          the second heartbeat, a little weaker
//   3.3 s          the subtitle and the version fade in
//   3.8, 4.6 s     the third and fourth heartbeats, weaker still (v0.982)
//   5.2 .. 6.7 s   after the last beat, everything darkens slowly (v0.983); MAIN
// Any key or G0 skips it (the key itself does nothing: it is ignored until it
// is released). With Cursed Mode on, now and then Watcher's eyes open behind
// the logo for an instant after the second beat.
//
// The logo and its glow are 4-bit alpha images made from the font by
// tools/make_logo.py (logo_miasmora.h); the colours are the UI's own.
#include "ui_internal.h"
#include <math.h>
#include <new>
#include "../logo_miasmora.h"
#include "../dsp/dsp.h"

void watcherSplash(float open);   // ui_watcher.cpp
void uiKeyGuard();                // ui.cpp: ignore the keyboard until it is released

namespace {

constexpr float T_FOG_IN = 0.4f;
constexpr float T_REVEAL0 = 0.4f, T_REVEAL1 = 1.8f;
constexpr float T_THIN0 = 1.8f, T_THIN1 = 2.2f;
constexpr float T_BEAT1 = 2.2f, T_BEAT2 = 3.0f;
// v0.982: four beats (was two), each glow a little weaker than the one before
constexpr int N_BEATS = 4;
constexpr float BEAT_T[N_BEATS] = {T_BEAT1, T_BEAT2, 3.8f, 4.6f};
constexpr float BEAT_GLOW[N_BEATS] = {1.f, 0.8f, 0.65f, 0.5f};
constexpr float T_SUB = 3.3f, SUB_FADE = 0.25f;
constexpr float T_OUT0 = 5.2f, T_OUT1 = 6.7f;    // v0.983: a slow 1.5 s fade after the last beat (v0.982: 4.95 / 5.35)
constexpr float SOUND_LEAD = 0.1f;    // the beat is asked for this early (the audio path's delay)
// HEART's second sound (drones2.cpp: gap2) with Ritual's heart: BPM 35 + React
// 50 at DREAD 45 -> 84 BPM -> 0.306 s (v0.984; was 0.32 at BPM 30)
constexpr float DUB_GAP = 0.306f;
constexpr float FOG_MAX = 0.16f;      // the fog: the background with this much of the logo colour
constexpr float FOG_THIN = 0.4f;      // ... thinned to this
constexpr float EDGE = 0.12f;         // the logo's appearing edge, in noise units
constexpr uint32_t FRAME_MS = 33;

// the fog's noise: a coarse grid (13 x 8, wrapping sideways so it can drift)
// and a finer one, each enlarged smoothly
constexpr int CGX = 13, CGY = 8, FGX = 31, FGY = 17;

float smooth01(float x) { x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x); return x * x * (3.f - 2.f * x); }
float clamp01(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }

// the logo colour and the background, 0..255
float g_lr, g_lg, g_lb, g_br, g_bg, g_bb;
void split565(uint16_t c, float& r, float& g, float& b) {
  r = (float)(((c >> 11) & 31) * 255 / 31);
  g = (float)(((c >> 5) & 63) * 255 / 63);
  b = (float)((c & 31) * 255 / 31);
}
// the canvas holds byte-swapped RGB565 (M5GFX's 16-bit sprites)
inline uint16_t pack(float r, float g, float b) {
  const int R = r < 0.f ? 0 : (r > 255.f ? 255 : (int)r);
  const int G = g < 0.f ? 0 : (g > 255.f ? 255 : (int)g);
  const int B = b < 0.f ? 0 : (b > 255.f ? 255 : (int)b);
  const uint16_t c = (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3));
  return (uint16_t)((c >> 8) | (c << 8));
}
inline void unpack(uint16_t s, float& r, float& g, float& b) {
  const uint16_t c = (uint16_t)((s >> 8) | (s << 8));
  r = (float)((c >> 8) & 0xF8); g = (float)((c >> 3) & 0xFC); b = (float)((c << 3) & 0xF8);
}
inline float alpha4(const uint8_t* a, int stride, int x, int y) {
  const uint8_t v = a[y * stride + (x >> 1)];
  return (float)((x & 1) ? (v & 15) : (v >> 4)) * (1.f / 15.f);
}

// The working memory, only while the start screen runs (~36 KB from the
// heap, given back afterwards): the noise at every pixel of this frame (the
// logo's reveal reads it too) and the columns' grid cells
struct Work {
  uint8_t noise[SCREEN_H][SCREEN_W];
  int cx0[SCREEN_W], fx0[SCREEN_W];
  float cfx[SCREEN_W], ffx[SCREEN_W];
  float coarse[CGY][CGX], fine[FGY][FGX];   // the fog's grids
};
Work* g_w = nullptr;

void makeNoise(float t) {
  // per column: the cells and the (smoothed) fractions, drifting sideways
  int* cx0 = g_w->cx0; int* fx0 = g_w->fx0;
  float* cfx = g_w->cfx; float* ffx = g_w->ffx;
  const float cShift = t * 0.55f, fShift = -t * 1.1f;   // cells per second
  for (int x = 0; x < SCREEN_W; x++) {
    float u = x * (float)(CGX - 1) / SCREEN_W * 0.75f + cShift;   // 3/4 of the grid on screen
    int i = (int)floorf(u);
    cfx[x] = smooth01(u - i);
    cx0[x] = ((i % CGX) + CGX) % CGX;
    u = x * (float)(FGX - 1) / SCREEN_W + fShift;
    i = (int)floorf(u);
    ffx[x] = smooth01(u - i);
    fx0[x] = ((i % FGX) + FGX) % FGX;
  }
  float cr[CGX], fr[FGX];
  for (int y = 0; y < SCREEN_H; y++) {
    float v = y * (float)(CGY - 1) / (SCREEN_H - 1);
    int j = (int)v; if (j >= CGY - 1) j = CGY - 2;
    float fy = smooth01(v - j);
    for (int i = 0; i < CGX; i++) cr[i] = g_w->coarse[j][i] + (g_w->coarse[j + 1][i] - g_w->coarse[j][i]) * fy;
    v = y * (float)(FGY - 1) / (SCREEN_H - 1);
    j = (int)v; if (j >= FGY - 1) j = FGY - 2;
    fy = smooth01(v - j);
    for (int i = 0; i < FGX; i++) fr[i] = g_w->fine[j][i] + (g_w->fine[j + 1][i] - g_w->fine[j][i]) * fy;
    for (int x = 0; x < SCREEN_W; x++) {
      const int a = cx0[x], b = (a + 1) % CGX;
      const float c = cr[a] + (cr[b] - cr[a]) * cfx[x];
      const int fa = fx0[x], fb = (fa + 1) % FGX;
      const float f = fr[fa] + (fr[fb] - fr[fa]) * ffx[x];
      // more contrast: the fog gathers in clouds
      const float n = clamp01((0.78f * c + 0.22f * f - 0.18f) * 1.6f);
      g_w->noise[y][x] = (uint8_t)(n * 255.f);
    }
  }
}

// the glow: "lub" and "dub" of each beat, a quick rise and a ~0.4 s fall
float pulseAt(float t) {
  float p = 0.f;
  for (int i = 0; i < 2 * N_BEATS; i++) {   // lub, dub (0.45 of it), per beat
    const float d = t - (BEAT_T[i / 2] + (i & 1 ? DUB_GAP : 0.f));
    if (d < 0.f) continue;
    p += BEAT_GLOW[i / 2] * (i & 1 ? 0.45f : 1.f) * (d < 0.04f ? d / 0.04f : expf(-(d - 0.04f) / 0.13f));
  }
  return p > 1.f ? 1.f : p;
}

void drawFrame(float t, float eyeOpen) {
  uint16_t* buf = (uint16_t*)canvas.getBuffer();
  if (!buf) return;
  const float fade = t < T_OUT0 ? 1.f : 1.f - smooth01((t - T_OUT0) / (T_OUT1 - T_OUT0));
  float fogAmp = smooth01(t / T_FOG_IN);
  if (t > T_THIN0) fogAmp *= 1.f - (1.f - FOG_THIN) * smooth01((t - T_THIN0) / (T_THIN1 - T_THIN0));
  const float fogK = FOG_MAX * fogAmp / 255.f;

  makeNoise(t);
  // 1) the fog
  for (int y = 0; y < SCREEN_H; y++) {
    uint16_t* row = buf + y * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) {
      const float k = fogK * g_w->noise[y][x];
      row[x] = pack((g_br + (g_lr - g_br) * k) * fade, (g_bg + (g_lg - g_bg) * k) * fade, (g_bb + (g_lb - g_bb) * k) * fade);
    }
  }
  // 2) Cursed: the eyes, behind the logo
  if (eyeOpen > 0.f) { watcherSplash(eyeOpen); watcherDraw(); }

  // 3) the glow and the logo, over what is there
  const float pulse = pulseAt(t);
  const float th = t < T_REVEAL0 ? -1.f : (t >= T_REVEAL1 ? 2.f : -EDGE + (1.f + 2.f * EDGE) * smooth01((t - T_REVEAL0) / (T_REVEAL1 - T_REVEAL0)));
  if (pulse > 0.01f && t >= T_REVEAL1) {
    const int gs = (GLOW_W + 1) / 2;
    const float gk = 0.95f * pulse * fade;
    for (int y = 0; y < GLOW_H; y++) {
      const int sy = GLOW_Y + y;
      if (sy < 0 || sy >= SCREEN_H) continue;
      for (int x = 0; x < GLOW_W; x++) {
        const int sx = GLOW_X + x;
        if (sx < 0 || sx >= SCREEN_W) continue;
        const float a = alpha4(GLOW_A4, gs, x, y);
        if (a <= 0.f) continue;
        float r, g, b;
        unpack(buf[sy * SCREEN_W + sx], r, g, b);
        const float k = a * gk;
        buf[sy * SCREEN_W + sx] = pack(r + g_lr * k, g + g_lg * k, b + g_lb * k);
      }
    }
  }
  if (th > -EDGE) {
    const int ls = (LOGO_W + 1) / 2;
    // the body brightens a little with the beat
    const float b2 = 0.35f * pulse;
    const float cr = (g_lr + (255.f - g_lr) * b2) * fade, cg = (g_lg + (190.f - g_lg) * b2) * fade,
                cb = (g_lb + (170.f - g_lb) * b2) * fade;
    for (int y = 0; y < LOGO_H; y++) {
      const int sy = LOGO_Y + y;
      if (sy < 0 || sy >= SCREEN_H) continue;
      for (int x = 0; x < LOGO_W; x++) {
        const int sx = LOGO_X + x;
        if (sx < 0 || sx >= SCREEN_W) continue;
        float a = alpha4(LOGO_A4, ls, x, y);
        if (a <= 0.f) continue;
        // the densest fog first: a pixel shows once the threshold passes it
        const float dense = 1.f - g_w->noise[sy][sx] * (1.f / 255.f);
        a *= clamp01((th - dense) / EDGE);
        if (a <= 0.f) continue;
        float r, g, b;
        unpack(buf[sy * SCREEN_W + sx], r, g, b);
        buf[sy * SCREEN_W + sx] = pack(r + (cr - r) * a, g + (cg - g) * a, b + (cb - b) * a);
      }
    }
  }
  // 4) the subtitle and the version
  if (t >= T_SUB) {
    const float k = smooth01((t - T_SUB) / SUB_FADE) * fade;
    float r, g, b;
    split565(C_DIM, r, g, b);
    const uint16_t c = rgb((uint8_t)(g_br + (r - g_br) * k), (uint8_t)(g_bg + (g - g_bg) * k), (uint8_t)(g_bb + (b - g_bb) * k));
    char s[48];
    snprintf(s, sizeof(s), "DARK AMBIENT DRONE GENERATOR  %s", FW_VERSION);
    drawText(s, SCREEN_W / 2, 98, c, 1, textdatum_t::top_center);
  }
}

}  // namespace

// Runs the whole start screen (blocking); the audio task is already running.
void uiSplashRun() {
  g_w = new (std::nothrow) Work;
  if (!g_w) { appSplashEnd(); return; }   // no memory: straight to MAIN
  dsp::Rng r;
  r.seed(esp_random());
  for (auto& row : g_w->coarse) for (auto& v : row) v = r.uni();
  for (auto& row : g_w->fine) for (auto& v : row) v = r.uni();
  split565(C_ACC2, g_lr, g_lg, g_lb);
  split565(C_BG, g_br, g_bg, g_bb);
  // Cursed Mode: now and then the eyes (splash spec 3.6)
  const bool eye = gSet.cursed && gSet.watcher && r.irange(0, 19) == 0;

  appSplashBegin();
  int beats = 0;   // heartbeats asked for so far
  bool skipped = false;
  const uint32_t t0 = millis();
  uint32_t next = t0;
  for (;;) {
    M5Cardputer.update();
    if (M5Cardputer.Keyboard.isPressed() || M5Cardputer.BtnA.wasPressed()) { skipped = true; break; }
    const float t = (millis() - t0) * 0.001f;
    if (t >= T_OUT1) break;
    if (beats < N_BEATS && t >= BEAT_T[beats] - SOUND_LEAD) { appSplashBeat(); beats++; }

    // the eyes: right after the second beat, open and shut within 0.3 s
    float eyeOpen = 0.f;
    if (eye) {
      const float e = (t - (T_BEAT2 + 0.25f)) / 0.3f;
      if (e > 0.f && e < 1.f) eyeOpen = sinf(e * 3.14159f);
    }
    drawFrame(t, eyeOpen);
    canvas.pushSprite(0, 0);
    next += FRAME_MS;
    const uint32_t now = millis();
    if ((int32_t)(next - now) > 0) delay(next - now); else next = now;
  }
  if (eye) watcherSplash(0.f);
  appSplashEnd();
  if (skipped) uiKeyGuard();
  canvas.fillScreen(C_BG);
  canvas.pushSprite(0, 0);
  delete g_w;
  g_w = nullptr;
}
