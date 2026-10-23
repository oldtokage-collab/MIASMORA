// MIASMORA — PLANCHETTE (phase 7). See planchette.h.
#include "planchette.h"
#include <M5Unified.h>
#include <math.h>
#include "app.h"
#include "export.h"
#include "party.h"
#include "arc.h"

static constexpr float DEAD_DEG = 4.f;
static constexpr float SMOOTH_SEC[3] = {0.2f, 0.4f, 0.8f};   // Low / Mid / High
static constexpr float RELEASE_SEC = 1.2f;                    // after Hold: back to the tilt slowly
static constexpr float DREAD_MAX = 30.f;
// The event rate x0.5..x2 as a shift of each event layer's Density: the rate
// is exponential in Density over a range of about 30x (event_layer.h), so a
// doubling is ln 2 / ln 30 = 0.204 of the scale -> 20 points of 100
static constexpr float DENS_SHIFT = 20.f;
static constexpr float SHADE_MIX = 25.f, SHADE_DAMP = 35.f;
// which way is which (v0.81: both reversed after the device check)
static constexpr float SIGN_X = -1.f, SIGN_Y = -1.f;

static bool g_on = false;
static SceneData g_base;          // the scene as it was when PLANCHETTE started
static bool g_centred = false;
static float g_pitch0 = 0.f, g_roll0 = 0.f;
static float g_x = 0.f, g_y = 0.f;   // smoothed, -1..1
static bool g_hold = false;
static float g_release = 0.f;     // > 0: gliding back after a Hold
static bool g_vector = false;     // Vector (SETTINGS > Motion > Tilt Mode), fixed while it lasts

bool planchetteActive() { return g_on; }
bool planchetteHeld() { return g_hold; }
float planchetteX() { return g_x; }
float planchetteY() { return g_y; }
int planchetteDreadAdd() { return (int)lroundf(DREAD_MAX * g_y); }
float planchetteRateMul() { return powf(2.f, g_y); }
float planchetteShade() { return g_x; }

static bool readAngles(float& pitch, float& roll) {
  float ax = 0, ay = 0, az = 0;
  M5.Imu.update();
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return false;
  roll = atan2f(ax, sqrtf(ay * ay + az * az)) * 57.29578f;
  pitch = atan2f(ay, sqrtf(ax * ax + az * az)) * 57.29578f;
  return true;
}

// a tilt (degrees) -> -1..1, with the dead zone
static float axisOf(float deg) {
  const float range = (float)gSet.plRange;
  const float a = fabsf(deg);
  if (a <= DEAD_DEG) return 0.f;
  const float v = (a - DEAD_DEG) / (range - DEAD_DEG);
  return copysignf(v > 1.f ? 1.f : v, deg);
}

// the tilt written into the playing scene (from the base, every time)
static void applyAxis() {
  SceneData s = g_base;
  s.dread = (uint8_t)constrain((int)lroundf(s.dread + DREAD_MAX * g_y), 0, 100);
  const float dShift = DENS_SHIFT * g_y;
  for (int i = 0; i < NUM_LAYERS; i++) {
    if (!layerIsEvent(i)) continue;
    LayerState& L = s.L[i];
    if (L.p[0] == 0) continue;   // "never" stays never
    L.p[0] = (uint8_t)constrain((int)lroundf(L.p[0] + dShift), 1, 100);
  }
  // Shade: REVERB's Mix and Damping; off in the scene: only the right side adds it
  LayerState& R = s.L[L_REVERB];
  const float mix0 = R.on ? R.level : 0.f;
  const float mix = mix0 + SHADE_MIX * g_x;
  R.level = (uint8_t)constrain((int)lroundf(mix), 0, 100);
  if (!g_base.L[L_REVERB].on) R.on = R.level > 0;
  R.p[1] = (uint8_t)constrain((int)lroundf(R.p[1] + SHADE_DAMP * g_x), 0, 100);
  gRt.live = s;
}



// --- Vector (v0.81, spec v1.2 6.3) ------------------------------------------------
// Four scenes in the corners (SETTINGS > Motion > Corner A..D): A forward-left,
// B forward-right, C back-left, D back-right. The tilt gives each a bilinear
// weight; every number (levels, Density, Pitch, DREAD, REVERB ...) is mixed by
// weight, a layer missing from a corner counting as level 0 there. A choice
// (Mode, Surface, Chord ...) comes from one corner, with hysteresis: it moves
// to another once that one weighs over 0.6 and its own has fallen under 0.4,
// and the layer dips out and back in around the switch (0.3 s), so its
// character never jumps.
static SceneData g_corner[4];
static int8_t g_optSrc[NUM_LAYERS];   // the corner each layer's choices come from (-1: none yet)
static int8_t g_dipTo[NUM_LAYERS];
static float g_dipT[NUM_LAYERS];      // < 0: no dip; 0..1 through it
static constexpr float DIP_SEC = 0.3f;

static void loadCorner(int k) {
  const char* name = gSet.plCorner[k];
  for (auto& e : gScenes) {
    if (e.name != name) continue;
    if (e.kind == SK_PRESET) { presetToScene(e.presetIdx, g_corner[k]); return; }
    if (e.kind == SK_CUSTOM && sceneLoadFile(e.name, g_corner[k])) return;
  }
  g_corner[k] = g_base;   // gone (a deleted scene): the scene that was playing
}

static void vectorBegin() {
  for (int k = 0; k < 4; k++) loadCorner(k);
  for (int i = 0; i < NUM_LAYERS; i++) { g_optSrc[i] = -1; g_dipT[i] = -1.f; g_dipTo[i] = -1; }
}

void planchetteWeights(float* w) {
  const float u = (g_x + 1.f) * 0.5f, v = (g_y + 1.f) * 0.5f;   // 0..1: left -> right, back -> forward
  w[0] = (1.f - u) * v;   w[1] = u * v;                // A forward-left, B forward-right
  w[2] = (1.f - u) * (1.f - v); w[3] = u * (1.f - v);  // C back-left,    D back-right
}

static bool sameChoices(int i, int a, int b) {
  const LayerDef& d = LAYER_DEFS[i];
  for (int q = 0; q < d.nParams; q++) if (d.p[q].nOpts && g_corner[a].L[i].p[q] != g_corner[b].L[i].p[q]) return false;
  return true;
}

static void applyVector(float dt) {
  float w[4];
  planchetteWeights(w);
  SceneData s;
  sceneDefaults(s, g_base.name);
  float dread = 0.f;
  for (int k = 0; k < 4; k++) dread += w[k] * g_corner[k].dread;
  s.dread = (uint8_t)constrain((int)lroundf(dread), 0, 100);
  for (int i = 0; i < NUM_LAYERS; i++) {
    const LayerDef& d = LAYER_DEFS[i];
    float lv = 0.f, wOn = 0.f, pm[MAX_P] = {0};
    int best = -1;
    for (int k = 0; k < 4; k++) {
      const LayerState& L = g_corner[k].L[i];
      if (!L.on) continue;   // missing here: level 0, and no say in its sound
      lv += w[k] * L.level;
      wOn += w[k];
      for (int q = 0; q < d.nParams; q++) if (!d.p[q].nOpts) pm[q] += w[k] * L.p[q];
      if (best < 0 || w[k] > w[best]) best = k;
    }
    LayerState& o = s.L[i];
    o.level = (uint8_t)constrain((int)lroundf(lv), 0, 100);
    o.on = o.level > 0;
    if (best < 0) { g_optSrc[i] = -1; continue; }
    for (int q = 0; q < d.nParams; q++) if (!d.p[q].nOpts) o.p[q] = (uint8_t)constrain((int)lroundf(pm[q] / wOn), 0, 100);
    // the choices: one corner, with hysteresis and a dip around a switch
    int8_t& src = g_optSrc[i];
    if (src < 0 || !g_corner[src].L[i].on) src = (int8_t)best;   // nothing to hear yet: no dip needed
    else if (g_dipT[i] < 0.f && w[src] < 0.4f && w[best] > 0.6f && best != src) {
      if (sameChoices(i, src, best)) src = (int8_t)best;
      else { g_dipT[i] = 0.f; g_dipTo[i] = (int8_t)best; }
    }
    float g = 1.f;
    if (g_dipT[i] >= 0.f) {
      g_dipT[i] += dt / DIP_SEC;
      if (g_dipT[i] >= 0.5f && g_dipTo[i] >= 0) { src = g_dipTo[i]; g_dipTo[i] = -1; }   // switched at the bottom
      g = fabsf(1.f - 2.f * fminf(g_dipT[i], 1.f));
      if (g_dipT[i] >= 1.f) { g_dipT[i] = -1.f; g = 1.f; }
    }
    for (int q = 0; q < d.nParams; q++) if (d.p[q].nOpts) o.p[q] = g_corner[src].L[i].p[q];
    o.level = (uint8_t)lroundf(o.level * g * g);
  }
  gRt.live = s;
}

const char* planchetteCornerName(int k) { return gSet.plCorner[k & 3]; }
bool planchetteVector() { return g_vector; }

const char* planchetteEnter() {
  if (g_on) return "";
  if (!gImuReady) return "PLANCHETTE needs the IMU";
  if (partyActive()) return "";
  if (exportBusy()) return "Not while exporting";
  appSettleEdit();          // a crossfade in progress: to its end
  g_base = gRt.live;
  g_centred = false;
  g_x = g_y = 0.f;
  g_hold = false;
  g_release = 0.f;
  g_vector = gSet.plMode == 1;
  if (g_vector) vectorBegin();
  g_on = true;
  return nullptr;
}

void planchetteExit() {
  if (!g_on) return;
  g_on = false;
  // the sound stays as it is: marked edited (MOOD N keeps it), not saved
  appMarkEdited();
}

void planchetteRecenter() { g_centred = false; }

void planchetteToggleHold() {
  g_hold = !g_hold;
  if (!g_hold) g_release = RELEASE_SEC;
}

void planchetteUpdate(float dt) {
  if (!g_on) return;
  if (exportBusy()) return;   // Shift+X: the scene stays as it was saved
  float pitch, roll;
  if (!readAngles(pitch, roll)) return;
  if (!g_centred) { g_pitch0 = pitch; g_roll0 = roll; g_centred = true; }
  if (!g_hold) {
    const float tx = SIGN_X * axisOf(roll - g_roll0), ty = SIGN_Y * axisOf(pitch - g_pitch0);
    float tc = SMOOTH_SEC[gSet.plSmooth % 3];
    if (g_release > 0.f) { g_release -= dt; tc = fmaxf(tc, RELEASE_SEC * 0.5f); }
    const float k = 1.f - expf(-dt / tc);
    g_x += (tx - g_x) * k;
    g_y += (ty - g_y) * k;
  }
  if (g_vector) applyVector(dt);
  else applyAxis();
}

// Saved: the scene as heard. Arc holds still in PLANCHETTE but what it adds is
// heard, so it goes in too (v0.9): DREAD, and its event-rate multiplier as a
// Density shift (the same mapping as the tilt). Shift+X exports this scene
// without Arc, so the file and the saved scene sound the same.
const char* planchetteSave(char* name, int n, SceneData* out) {
  SceneData s = gRt.live;
  if (arcShown()) {
    s.dread = (uint8_t)constrain((int)s.dread + arcExportDreadAdd(), 0, 100);
    const float shift = DENS_SHIFT * log2f(arcExportDensMul());
    for (int i = 0; i < NUM_LAYERS; i++) {
      if (!layerIsEvent(i) || s.L[i].p[0] == 0) continue;
      s.L[i].p[0] = (uint8_t)constrain((int)lroundf(s.L[i].p[0] + shift), 1, 100);
    }
  }
  if (out) *out = s;
  return appSnapshot("Tilt", name, n, &s);
}
