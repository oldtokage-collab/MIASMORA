// MIASMORA — Watcher: the eye on a Hush (v0.6, spec v1.23 2)
//
// When a Hush begins (Listen's or the shake's -- the same states in
// listen.cpp) an eye opens in place of MAIN's visualiser:
//   drop (0.3 s)     the lids fly open, the pupil narrows once it is open
//   hold, 1st half   it searches: the pupil darts about every 0.4..1.2 s
//   hold, 2nd half   it stares at you (the centre), blinking now and then
//   noticed          the lone event: the eye widens for an instant
//   recover          the lids close as the sound comes back; shut = gone
// A new Hush while it closes opens it again. The pupil leans against the
// device's tilt (from where it was when the eye opened), so it keeps looking
// at whoever holds it. The LED pulses dark red meanwhile (on any screen).
// Display off: no eye, no LED. Cursed Mode can open it for an instant.
//
// v0.61: drawn as an ink sketch in MIASMORA's reds -- a lid crease, a stippled
// lash line, curved upper lashes sweeping to the outer corner, sparse lower
// ones, a large dark iris with radial streaks and a black limbal ring, a few
// small catchlights, veins in a shadowed, bloodshot white. v0.62: two eyes,
// no skin around them; the ink is dark red (black vanishes on the black
// screen). All procedural; the texture is a fixed hash of the pixel, so it
// does not shimmer.
// v0.988: one eye, as an engraving -- the Eye of Providence printed in bone
// ink: a heavy upper lid and a fine lower one, two crease lines above (they
// stay when it shuts), curved lashes sweeping outwards, hatching for the
// white's shadow and its corners, a red iris of radial fibres round a dark
// pupil, a pale highlight; the ink takes unevenly, like a print.
#include "ui_internal.h"
#include <math.h>
#include <algorithm>
#include "../audio.h"
#include "../listen.h"
#include "../cursed.h"
#include "../led.h"
#include "../poltergeist.h"

bool uiDisplayIsOn();

// one eye (v0.988), in the visualiser's area between DREAD's bar (y ..47) and
// Arc's bar (y 100..): crease lines and lashes reach up to y 49
static constexpr int EX = 120, CY = 78;
static constexpr int HW = 70;          // half the eye's width
static constexpr int UH = 20;          // the upper lid's height above CY, fully open
static constexpr int LH = 13;          // the lower lid's depth below CY
static constexpr int AREA_T = 49, AREA_B = 99;   // what the eye may draw on
static constexpr float IRIS_R = 14.f, PUPIL_R = 5.3f;
static constexpr float TILT_GAIN = 30.f;   // px per g of tilt
static constexpr float TILT_MAX = 20.f;
// which way the pupil leans for a tilt (v0.61: both reversed after the device check)
static constexpr float TILT_X_SIGN = 1.f, TILT_Y_SIGN = -1.f;

static float g_open = 0.f;             // 0 shut .. 1 open
static float g_wide = 0.f;             // the response's widening, decaying
static float g_px = 0.f, g_py = 0.f;   // pupil offset (px)
static float g_tx = 0.f, g_ty = 0.f;   // where it darts to
static float g_nextDart = 0.f, g_blinkIn = 3.f, g_blink = 0.f;
static float g_narrow = 0.f;           // the pupil narrowing after it opens
static float g_a0x = 0.f, g_a0y = 0.f; // the tilt when it opened
static bool  g_tiltSet = false;
static uint32_t g_lastHush = 0, g_lastResp = 0;
static float g_pulse = 0.f;
static dsp::Rng g_r;
static bool g_seeded = false;

static float smooth01(float x) { x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x); return x * x * (3.f - 2.f * x); }
static inline uint32_t hash2(int x, int y) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
  return h;
}
static inline float noise01(int x, int y) { return (hash2(x, y) & 1023) / 1023.f; }

bool watcherVisible() { return gSet.watcher && g_open > 0.01f; }

// the start screen (v0.98, Cursed): the eyes open behind the logo for an
// instant, staring straight ahead; 0 shuts them again
void watcherSplash(float open) {
  if (!g_seeded) { g_r.seed(esp_random()); g_seeded = true; }
  g_open = open;
  g_narrow = 1.f; g_wide = 0.f; g_blink = 0.f;
  g_px = g_py = 0.f; g_tx = g_ty = 0.f;
  if (open <= 0.f) g_tiltSet = false;
}

// the lids' shape across the eye (u = -1 .. 1, left .. right corner): the
// upper lid arches high, the lower one is flatter
static float upperShape(float u) {
  if (u <= -1.f || u >= 1.f) return 0.f;
  return powf(1.f - u * u, 0.78f) * (1.f - 0.05f * u);
}
static float lowerShape(float u) {
  if (u <= -1.f || u >= 1.f) return 0.f;
  return powf(1.f - u * u, 1.15f);
}

void watcherUpdate(float dt) {
  if (!g_seeded) { g_r.seed(esp_random()); g_seeded = true; }
  ListenWatch w;
  listenWatch(w);
  if (!gSet.watcher) w.stage = WS_NONE;
  float target = 0.f;
  bool stare = false, search = false;
  switch (w.stage) {
    case WS_DROP: target = fminf(w.t / 0.3f, 1.f); break;
    case WS_HOLD:
      target = 1.f;
      if (w.t < w.dur * 0.5f) search = true; else stare = true;
      break;
    case WS_NOTICED: target = 1.f; stare = true; break;
    case WS_RECOVER: target = 1.f - smooth01(w.dur > 0.f ? w.t / w.dur : 1.f); stare = true; break;
    default: break;
  }
  // a new Hush (also while it closes): open from where it is
  if (w.hushes != g_lastHush) { g_lastHush = w.hushes; g_narrow = 0.f; if (g_open < 0.05f) g_tiltSet = false; }
  if (w.responses != g_lastResp) { g_lastResp = w.responses; if (gSet.watcher) g_wide = 1.f; }
  g_wide = fmaxf(0.f, g_wide - dt / 0.35f);
  // Poltergeist's reveal: wide open, staring (v0.9)
  if (poltergeistRevealing()) { target = 1.f; stare = true; search = false; }
  // Cursed: open for an instant (no Hush, no sound)
  const float cur = gSet.watcher ? cursedEyeOpen() : 0.f;
  if (cur > target) { target = cur; stare = true; if (g_open < 0.05f) g_tiltSet = false; }
  // opening is quick, closing follows the recovery exactly
  g_open = target > g_open ? fminf(target, g_open + dt / 0.12f) : target;
  if (g_open <= 0.01f) { g_open = 0.f; g_px = g_py = 0.f; g_tiltSet = false; }

  // the pupil narrows once the eye is open
  if (g_open > 0.9f) g_narrow = fminf(1.f, g_narrow + dt / 0.4f);
  // searching: dart, rest; staring: back to the centre, blinking now and then
  if (search) {
    g_nextDart -= dt;
    if (g_nextDart <= 0.f) {
      g_nextDart = g_r.range(0.4f, 1.2f);
      g_tx = g_r.range(-HW * 0.45f, HW * 0.45f);
      g_ty = g_r.range(-UH * 0.3f, LH * 0.4f);
    }
  } else if (stare) {
    g_tx = 0.f; g_ty = 0.f;
    g_blinkIn -= dt;
    if (g_blinkIn <= 0.f) { g_blink = 0.18f; g_blinkIn = g_r.range(2.5f, 6.f); }
  }
  g_blink = fmaxf(0.f, g_blink - dt);
  const float k = 1.f - expf(-dt / (search ? 0.035f : 0.12f));   // darts are fast
  g_px += (g_tx - g_px) * k;
  g_py += (g_ty - g_py) * k;

  // LED: a slow dark-red pulse, fading with the lids (never with the display off)
  g_pulse += dt;
  const float lv = (uiDisplayIsOn() && w.stage != WS_NONE) ? g_open * (0.55f + 0.45f * sinf(g_pulse * 6.2831853f / 1.8f)) : 0.f;
  static uint32_t lastLed = 0;   // ~30 updates a second are plenty for a slow pulse
  const uint32_t now = millis();
  if (now - lastLed >= 33 || lv <= 0.f) { lastLed = now; ledShow(200, 0, 0, lv * 0.6f); }
}

// --- drawing: the engraving (v0.988) ------------------------------------------------
struct Ink { float r, g, b; };
static constexpr Ink INK_BONE = {205.f, 200.f, 192.f};   // the screen's text colour: the printing ink
static constexpr Ink INK_GREY = {120.f, 116.f, 112.f};   // hatching
static constexpr Ink INK_RED = {225.f, 70.f, 55.f};      // the iris
static constexpr Ink INK_DRED = {165.f, 28.f, 28.f};
static float g_vis = 1.f;                            // the ink fades in as it opens

// one pixel of ink; it takes unevenly, like a print (a fixed hash: no shimmer)
static void ink(int x, int y, const Ink& c, float k = 1.f) {
  if (y < AREA_T || y > AREA_B || x < 2 || x > SCREEN_W - 3) return;
  k *= g_vis * (0.72f + 0.28f * noise01(x, y));
  auto ch = [](float v) { return (uint8_t)(v < 0.f ? 0.f : (v > 255.f ? 255.f : v)); };
  canvas.drawPixel(x, y, rgb(ch(c.r * k), ch(c.g * k), ch(c.b * k)));
}
static void inkLine(float x0, float y0, float x1, float y1, const Ink& c, float k = 1.f) {
  const int n = (int)fmaxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1;
  for (int i = 0; i <= n; i++) {
    const float t = (float)i / n;
    ink((int)lroundf(x0 + (x1 - x0) * t), (int)lroundf(y0 + (y1 - y0) * t), c, k);
  }
}
// a curve y = f(u) across part of the eye, as short straight pieces
template <class F> static void inkCurve(float u0, float u1, F f, const Ink& c, float k = 1.f, int thick = 1) {
  const int steps = (int)((u1 - u0) * HW) + 1;
  float px = EX + u0 * HW, py = f(u0);
  for (int i = 1; i <= steps; i++) {
    const float u = u0 + (u1 - u0) * i / steps, x = EX + u * HW, y = f(u);
    inkLine(px, py, x, y, c, k);
    if (thick > 1) inkLine(px, py - 1.f, x, y - 1.f, c, k * 0.8f);
    px = x; py = y;
  }
}

static void drawEngraving(float open, float ix, float iy, float pr) {
  auto top = [&](float u) { return CY - UH * upperShape(u) * open; };
  auto bot = [&](float u) { return CY + LH * lowerShape(u) * open; };
  // inside the lids: the white's hatching, the iris
  for (int x = EX - HW; x <= EX + HW; x++) {
    const float u = (float)(x - EX) / HW;
    const float t = top(u), b = bot(u);
    for (int y = (int)ceilf(t + 0.5f); y < b - 0.5f; y++) {
      const float dx = x - ix, dy = y - iy, d = sqrtf(dx * dx + dy * dy);
      if (d <= IRIS_R) {
        if (d <= pr) continue;                                // the pupil: the paper's dark
        if (d <= pr + 0.9f) { ink(x, y, INK_RED); continue; }     // its rim
        if (d >= IRIS_R - 1.3f) { ink(x, y, INK_RED); continue; } // the limbal ring
        const float rr = d / IRIS_R;
        if (fabsf(rr - 0.62f) < 0.04f) { ink(x, y, INK_DRED, 0.8f); continue; }
        // radial fibres as streaks of shade (thin lines this small would only
        // step round like a cog), fading a little towards the rim
        if (rr > 0.4f) {
          const float a = atan2f(dy, dx);
          const float fib = 0.5f + 0.5f * cosf(a * 22.f + 2.5f * noise01((int)(a * 40.f) + 99, 3));
          const float k = (0.25f + 0.75f * fib) * (1.f - 0.3f * (rr - 0.4f) / 0.6f);
          if (k > 0.3f) ink(x, y, INK_RED, k);
        }
        continue;
      }
      // the white: short diagonal strokes in the shadow under the upper lid ...
      const float fromTop = y - t, shade = UH * 0.45f * open;
      if (fromTop < shade && (x + y) % 3 == 0) { ink(x, y, INK_GREY, 0.9f - 0.5f * fromTop / shade); continue; }
      // ... and cross-hatching in the corners
      if (fabsf(u) > 0.62f) {
        const int sx = u > 0.f ? 1 : -1;
        if (((x * 3 - sx * y) % 7 + 7) % 7 == 0) ink(x, y, INK_GREY, 0.75f);
      }
    }
  }
  // the creases above the upper lid: they stay put when it shuts
  for (int k = 0; k < 2; k++) {
    const float off = k ? 8.f : 4.5f, ext = k ? 0.7f : 0.86f;
    inkCurve(-ext, ext, [&](float u) {
      const float a = fmaxf(0.f, 1.f - (u / 1.05f) * (u / 1.05f));
      return CY - UH * powf(a, 0.75f) - off * sqrtf(a);
    }, INK_BONE, k ? 0.55f : 0.75f);
  }
  // the lids: the upper one heavy, the lower one fine; two strokes under the lower one
  inkCurve(-1.f, 1.f, top, INK_BONE, 1.f, 2);
  inkCurve(-1.f, 1.f, bot, INK_BONE, 0.85f);
  inkCurve(-0.55f, 0.55f, [&](float u) { return bot(u) + 3.5f; }, INK_GREY, 0.8f);
  inkCurve(-0.35f, 0.35f, [&](float u) { return bot(u) + 6.f; }, INK_GREY, 0.6f);
  // lashes: curved, longer towards the outer (right) corner; short ticks below
  const int nUp = 14;
  for (int i = 0; i < nUp; i++) {
    const float u = -0.82f + 1.7f * (i + 0.5f) / nUp;
    const float x0 = EX + u * HW, y0 = top(u);
    const float len = (4.f + 5.f * fmaxf(0.f, u + 0.3f)) * (0.4f + 0.6f * open);
    float px = x0, py = y0;
    for (int k = 1; k <= 8; k++) {
      const float s = k / 8.f;
      const float ang = -1.5708f + (0.35f + 0.9f * fmaxf(0.f, u)) * s * 1.6f + u * 0.4f;
      const float x = x0 + cosf(ang) * len * s + 0.48f * len * s * s * fmaxf(0.2f, u + 0.5f);
      const float y = y0 + sinf(ang) * len * s;
      inkLine(px, py, x, y, INK_BONE, 0.9f);
      px = x; py = y;
    }
  }
  for (int i = 0; i < 11; i++) {
    const float u = -0.7f + 1.4f * i / 10.f;
    const float x0 = EX + u * HW, y0 = bot(u), ang = 1.5708f + u * 0.8f;
    inkLine(x0, y0, x0 + cosf(ang) * 2.2f, y0 + sinf(ang) * 2.2f, INK_BONE, 0.7f);
  }
  // the highlight, upper left of the pupil
  if (open > 0.35f) {
    const int hx = (int)lroundf(ix - IRIS_R * 0.32f), hy = (int)lroundf(iy - IRIS_R * 0.36f);
    if (hy > top((hx - EX) / (float)HW) + 1.f) {
      const Ink HL = {235.f, 225.f, 210.f};
      ink(hx, hy, HL); ink(hx + 1, hy, HL); ink(hx, hy + 1, HL); ink(hx + 1, hy + 1, HL, 0.7f);
    }
  }
}

void watcherDraw() {
  // the tilt: the pupil leans against it, from where it was when the eye opened
  float ox = 0.f, oy = 0.f;
  if (gImuReady) {
    float ax = 0, ay = 0, az = 0;
    M5.Imu.update();
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
      if (!g_tiltSet) { g_a0x = ax; g_a0y = ay; g_tiltSet = true; }
      ox = fmaxf(-TILT_MAX, fminf(TILT_MAX, TILT_X_SIGN * (ax - g_a0x) * TILT_GAIN));
      oy = fmaxf(-TILT_MAX * 0.5f, fminf(TILT_MAX * 0.5f, TILT_Y_SIGN * (ay - g_a0y) * TILT_GAIN));
    }
  }
  const float open = fminf(1.2f, g_open * (1.f + 0.2f * g_wide)) * (g_blink > 0.f ? 0.08f : 1.f);
  g_vis = fminf(1.f, g_open * 3.f);   // the ink fades in over the first third
  const float pr = PUPIL_R * (1.f - 0.3f * g_narrow) * (1.f + 0.55f * g_wide);
  drawEngraving(open, EX + g_px + ox, CY + 1 + g_py + oy, pr);
}
