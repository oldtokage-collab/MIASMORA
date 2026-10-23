#include "scene.h"
#include <SD.h>
#include "audio.h"
#include "storage.h"
#include "config.h"

using namespace dsp;

void sceneSetName(SceneData& s, const char* name) {
  strncpy(s.name, name ? name : "", sizeof(s.name) - 1);
  s.name[sizeof(s.name) - 1] = 0;
}

void sceneDefaults(SceneData& s, const char* name) {
  memset(&s, 0, sizeof(s));
  sceneSetName(s, name);
  s.dread = 30;
  for (int i = 0; i < NUM_LAYERS; i++) {
    const LayerDef& d = LAYER_DEFS[i];
    s.L[i].level = d.defLevel;
    s.L[i].on = false;
    for (int k = 0; k < MAX_P; k++) s.L[i].p[k] = k < d.nParams ? d.p[k].def : 0;
  }
}

// ===========================================================================
// Mood presets (spec 5). Levels / params are the pre-DREAD base values.
// Unlisted layers are off. Entries with level 0 are padding.
// ===========================================================================
struct PL { int8_t id; uint8_t lv; uint8_t p[MAX_P]; };
struct PresetDef { const char* name; uint8_t dread; PL L[9]; };

static const PresetDef PRESETS[] = {
  {"Ward", 30, {           // abandoned hospital
    {L_CLUSTER, 50, {IV_MIN2, 35, 65}},
    {L_AIR,     45, {55, 25, NZ_PINK}},          // air conditioning
    {L_CREAK,   45, {30, 50}},
    {L_STEPS,   40, {20, SF_CONCRETE, DIR_PASS, 40, 45}},   // distant
    {L_PULSE,   35, {15, 60}},
    {L_KNOCK,   35, {12, KN_TAP, 65}},            // somewhere down the corridor
    {L_REVERB,  45, {65, 50}}}},
  {"Fog Town", 25, {
    {L_SUB,     55, {40, 30, 50}},
    {L_AIR,     55, {35, 60, NZ_PINK}},          // wind
    {L_METAL,   25, {0, 30, 50}},
    {L_SWELL,   40, {25, 60}},
    {L_REVERB,  55, {75, 55}}}},
  {"Boiler Room", 35, {
    {L_SUB,     75, {30, 60, 30}},
    {L_METAL,   45, {1, 50, 40}},
    {L_AIR,     45, {15, 30, NZ_BROWN}},         // low hum
    {L_PULSE,   50, {35, 70}},
    {L_SCRAPE,  40, {25, 85, 50}},
    {L_MACHINE, 40, {MC_ENGINE, 35, 40}},
    {L_CLANG,   30, {15, CL_PIPE, 60, 60}},
    {L_REVERB,  35, {45, 40}}}},
  {"Abyss", 30, {
    {L_SUB,     70, {5, 20, 60}},
    {L_AIR,     55, {10, 50, NZ_BROWN}},
    {L_SWELL,   45, {25, 85}},
    {L_REVERB,  80, {100, 75}}}},
  {"Cellar", 25, {
    {L_AIR,     30, {25, 30, NZ_BROWN}},
    {L_DRIP,    55, {40, 45, DR_LEAK}},
    {L_DRAG,    45, {20, 55, 50}},
    {L_BUMP,    35, {20, 30, 40}},
    {L_REVERB,  55, {55, 50}}}},
  {"Rust", 40, {
    {L_CLUSTER, 50, {IV_TRITONE, 40, 55}},
    {L_METAL,   45, {2, 55, 55}},
    {L_SCRAPE,  45, {30, 90, 55}},
    {L_CREAK,   35, {25, 35}},
    {L_STEPS,   40, {20, SF_METAL, DIR_APPROACH, 50, 20}},
    {L_CLANG,   35, {15, CL_SHEET, 50, 40}},
    {L_REVERB,  50, {60, 35}}}},
  {"Otherworld", 75, {
    {L_SUB,     60, {30, 55, 60}},
    {L_CLUSTER, 50, {IV_CLUSTER, 50, 45}},
    {L_METAL,   55, {1, 65, 60}},
    {L_PULSE,   55, {40, 75}},
    {L_STATIC,  35, {ST_RADIO, 40, 50}},
    {L_REVERB,  55, {70, 45}}}},
  {"Hollow", 10, {         // the fear of nothing happening
    {L_SUB,     22, {25, 10, 40}},
    {L_AIR,     25, {30, 20, NZ_PINK}},
    {L_PULSE,   30, {4, 60}},
    {L_CREAK,   30, {4, 40}},
    {L_SWELL,   30, {5, 70}},
    {L_REVERB,  50, {70, 60}}}},
  {"Dead Channel", 30, {
    {L_STATIC,  70, {ST_TV, 60, 45}},
    {L_SUB,     25, {45, 20, 30}},
    {L_SWELL,   35, {20, 50}},
    {L_REVERB,  25, {40, 50}}}},
  {"Night Radio", 30, {
    {L_STATIC,  60, {ST_TUNING, 45, 40}},
    {L_AIR,     25, {40, 30, NZ_PINK}},
    {L_PULSE,   30, {12, 50}},
    {L_REVERB,  35, {50, 55}}}},
  {"Upstairs", 30, {
    {L_STEPS,   60, {35, SF_WOOD, DIR_PASS, 45, 80}},   // muffled, through the ceiling
    {L_CREAK,   40, {25, 45}},
    {L_DRAG,    40, {18, 60, 45}},
    {L_SUB,     25, {40, 20, 30}},
    {L_REVERB,  30, {40, 70}}}},
  {"Crawlspace", 40, {
    {L_SKITTER, 55, {40, 40, 30}},
    {L_DRAG,    40, {20, 40, 60}},
    {L_DRIP,    40, {30, 60, DR_RANDOM}},
    {L_INSECT,  25, {25, IN_WRONG, 30}},
    {L_AIR,     20, {20, 20, NZ_BROWN}},
    {L_REVERB,  35, {35, 60}}}},
  {"Infestation", 50, {
    {L_INSECT,  55, {50, IN_FLY, 60}},
    {L_SKITTER, 55, {55, 50, 75}},
    {L_CLUSTER, 40, {IV_CLUSTER, 40, 60}},
    {L_AIR,     35, {45, 40, NZ_PINK}},
    {L_REVERB,  30, {40, 50}}}},
  // --- v0.17 ----------------------------------------------------------------
  {"Night Shift", 30, {    // a factory working through the night
    {L_MACHINE, 50, {MC_MOTOR, 45, 35}},
    {L_DRILL,   35, {15, 50, 50}},
    {L_CLANG,   35, {20, CL_PIPE, 45, 55}},
    {L_AIR,     35, {30, 30, NZ_BROWN}},
    {L_REVERB,  40, {55, 50}}}},
  {"Visitor", 40, {        // someone at the door
    {L_KNOCK,   50, {25, KN_KNOCK, 45}},
    {L_STEPS,   45, {25, SF_WOOD, DIR_APPROACH, 40, 50}},
    {L_CREAK,   35, {20, 45}},
    {L_SUB,     25, {35, 20, 40}},
    {L_REVERB,  35, {45, 60}}}},
  {"Presence", 45, {       // the radio knows something is near
    {L_STATIC,  55, {ST_PROXIMITY, 45, 40}},
    {L_SUB,     45, {25, 30, 50}},
    {L_SWELL,   35, {20, 60}},
    {L_PULSE,   30, {12, 60}},
    {L_REVERB,  40, {60, 55}}}},
  {"Workshop", 35, {       // an abandoned workshop
    {L_DRILL,   45, {25, 55, 60}},
    {L_CLANG,   40, {25, CL_SHEET, 50, 45}},
    {L_DRAG,    40, {20, 50, 50, DG_PIPE}},
    {L_METAL,   20, {1, 35, 40}},
    {L_AIR,     30, {35, 25, NZ_PINK}},
    {L_REVERB,  40, {50, 45}}}},
  {"Pipes", 35, {          // inside the walls
    {L_BUMP,    45, {35, 35, 50}},
    {L_DRIP,    45, {35, 40, DR_LEAK}},
    {L_CLANG,   30, {12, CL_PIPE, 55, 65}},
    {L_AIR,     35, {20, 30, NZ_BROWN}},
    {L_REVERB,  50, {55, 55}}}},
  // --- v0.31 (spec v1.2 3.10) -------------------------------------------------
  {"Requiem", 35, {        // a choir for no one
    {L_VOX,     55, {VX_CHOIR, 35, 60, 30, 25}},
    {L_DISCORD, 40, {CH_MINMAJ, 30, 30, 60, 25}},
    {L_SUB,     40, {35, 25, 40}},
    {L_REVERB,  75, {95, 60}}}},
  {"Descent", 40, {        // it keeps going down
    {L_SHEPARD, 55, {SH_DOWN, 45, SP_OCTAVE, 45}},
    {L_SUB,     45, {30, 30, 50}},
    {L_AIR,     35, {30, 40, NZ_BROWN}},
    {L_SWELL,   35, {20, 70}},
    {L_REVERB,  55, {75, 55}}}},
  {"Out of Tune", 35, {    // the piano nobody has tuned for years
    {L_MICRO,   50, {TU_BEATING, 45, 40, 40, MW_SINE}},
    {L_DRIP,    40, {30, 45, DR_RANDOM}},
    {L_CREAK,   35, {20, 50}},
    {L_REVERB,  50, {60, 50}}}},
  {"Threnody", 45, {       // a lament
    {L_MICRO,   50, {TU_QUARTER, 50, 50, 75, MW_SAW}},
    {L_DISCORD, 40, {CH_CLUSTER, 40, 35, 55, 35}},
    {L_AIR,     35, {40, 40, NZ_PINK}},
    {L_REVERB,  55, {75, 50}}}},
  {"Ritual", 45, {         // something is being called
    {L_VOX,     50, {VX_HUM, 25, 40, 30, 35}},
    {L_HEART,   50, {35, 30, 50, 30, 50, 60}},
    {L_PULSE,   35, {25, 65}},
    {L_DRAG,    35, {15, 55, 50, DG_BODY}},
    {L_REVERB,  50, {60, 55}}}},
  {"Inside", 30, {         // from inside a body
    {L_HEART,   60, {35, 30, 50, 30, 50, 60}},   // v0.984: Ritual's heartbeat (was 25, 35, 75, 65, 50, 35)
    {L_SUB,     25, {25, 20, 40}},
    {L_AIR,     25, {10, 30, NZ_BROWN}},
    {L_REVERB,  20, {25, 70}}}},
};
const int NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);

const char* presetName(int i) { return (i >= 0 && i < NUM_PRESETS) ? PRESETS[i].name : ""; }

void presetToScene(int idx, SceneData& s) {
  if (idx < 0 || idx >= NUM_PRESETS) idx = 0;
  const PresetDef& P = PRESETS[idx];
  sceneDefaults(s, P.name);
  s.dread = P.dread;
  for (const PL& e : P.L) {
    if (e.lv == 0 || e.id < 0 || e.id >= NUM_LAYERS) continue;
    LayerState& L = s.L[e.id];
    L.on = true;
    L.level = e.lv;
    for (int k = 0; k < LAYER_DEFS[e.id].nParams; k++) L.p[k] = e.p[k];
  }
}

// ===========================================================================
// Random scene (spec 5.1): 1-3 drones + 1-3 event layers, all from the seed
// ===========================================================================
static void randomizeLayer(LayerState& L, int id, Rng& r, int lvLo, int lvHi) {
  const LayerDef& d = LAYER_DEFS[id];
  L.on = true;
  L.level = (uint8_t)r.irange(lvLo, lvHi);
  for (int k = 0; k < d.nParams; k++) {
    const ParamDef& pd = d.p[k];
    if (pd.nOpts) L.p[k] = (uint8_t)r.irange(0, pd.nOpts - 1);
    // density of event layers stays moderate (v0.19: by group -- an id range
    // missed the event layers added after INSECT)
    else if (layerIsEvent(id) && k == 0) L.p[k] = (uint8_t)r.irange(15, 55);
    else L.p[k] = (uint8_t)r.irange(15, 85);
  }
}

void generateRandomScene(uint32_t seed, SceneData& s, const char* name) {
  sceneDefaults(s, name);
  Rng r;
  r.seed(deriveSeed(seed, STREAM_SCENE));
  int drones[NUM_LAYERS], nd = 0, events[NUM_LAYERS], ne = 0;   // group sizes grow over versions
  for (int i = 0; i < NUM_LAYERS; i++) {
    if (layerIsDrone(i)) drones[nd++] = i;
    else if (layerIsEvent(i)) events[ne++] = i;
  }
  for (int i = nd - 1; i > 0; i--) { int j = r.irange(0, i); int t = drones[i]; drones[i] = drones[j]; drones[j] = t; }
  for (int i = ne - 1; i > 0; i--) { int j = r.irange(0, i); int t = events[i]; events[i] = events[j]; events[j] = t; }
  // the first (loudest) drone is a real bed, never the heartbeat alone (v0.31)
  if (!layerIsBed(drones[0])) { int t = drones[0]; drones[0] = drones[1]; drones[1] = t; }
  int pickD = r.irange(1, 3), pickE = r.irange(1, 3);
  for (int k = 0; k < pickD; k++) randomizeLayer(s.L[drones[k]], drones[k], r, k == 0 ? 40 : 20, k == 0 ? 70 : 50);
  for (int k = 0; k < pickE; k++) randomizeLayer(s.L[events[k]], events[k], r, 30, 65);
  LayerState& rv = s.L[L_REVERB];
  rv.on = true;
  rv.level = (uint8_t)r.irange(20, 60);
  rv.p[0] = (uint8_t)r.irange(30, 90);
  rv.p[1] = (uint8_t)r.irange(30, 80);
  s.dread = (uint8_t)r.irange(10, 60);
}

// ===========================================================================
// Crossfade / Drift runtime
// ===========================================================================
void SceneRuntime::startXf(const SceneData& target, float durSec) {
  from = live;           // current (maybe mid-morph) values become the new start point
  to = target;
  xf = true;
  xfT = 0.f;
  xfDur = durSec > 0.05f ? durSec : 0.05f;
  sceneSetName(live, to.name);
  // A layer coming in from silence takes its option params (mode, surface...)
  // right away, so it never switches character halfway through its fade-in.
  for (int i = 0; i < NUM_LAYERS; i++) {
    bool fromSilent = !from.L[i].on || from.L[i].level == 0;
    if (!fromSilent) continue;
    const LayerDef& d = LAYER_DEFS[i];
    for (int k = 0; k < d.nParams; k++)
      if (d.p[k].nOpts) { from.L[i].p[k] = to.L[i].p[k]; live.L[i].p[k] = to.L[i].p[k]; }
  }
}

void SceneRuntime::finishXf() {
  if (!xf) return;
  live = to;
  xf = false;
  xfT = 1.f;
}

// ---------------------------------------------------------------------------
// Drift (spec v1.1, 4.1)
// Each "step" makes 1..3 changes (by Drift Speed) to a planned copy of the
// scene and morphs to it in one transition. The wait before the next step is
// counted only once the transition has finished.
// ---------------------------------------------------------------------------
struct DriftSpeedDef { float waitLo, waitHi; int nLo, nHi; float xfLo, xfHi; };
static const DriftSpeedDef DRIFT_SPEEDS[NUM_DRIFT_SPEEDS] = {
  {20.f, 45.f, 1, 1, 12.f, 18.f},   // Slow  (the v0.181 pace)
  {12.f, 25.f, 1, 2,  8.f, 12.f},   // Mid
  { 6.f, 15.f, 2, 3,  4.f,  8.f},   // Fast
};
const char* const DRIFT_SPEED_NAMES[NUM_DRIFT_SPEEDS] = {"Slow", "Mid", "Fast", "Custom"};

// Custom (v0.41): the user's values; the transition is one length
static DriftSpeedDef speedDef(const SceneRuntime& rt) {
  if (rt.driftSpeed == DRIFT_CUSTOM)
    return {(float)rt.waitMin, (float)std::max(rt.waitMin, rt.waitMax), rt.chMin, std::max(rt.chMin, rt.chMax),
            (float)rt.xfSec, (float)rt.xfSec};
  return DRIFT_SPEEDS[rt.driftSpeed < 3 ? rt.driftSpeed : 1];
}

void SceneRuntime::driftBegin(uint32_t seed) {
  driftRng.seed(deriveSeed(seed, STREAM_DRIFT));
  drift = true;
  const DriftSpeedDef sp = speedDef(*this);
  driftTimer = driftRng.range(sp.waitLo, sp.waitHi);
}

bool SceneRuntime::driftNow() {
  if (!drift || xf) return false;
  driftTimer = 0.f;
  return true;
}

static bool isSounding(const LayerState& L) { return L.on && L.level > 3; }

// Bring layer `id` in quietly. Events come in at Density >= 50 so that a
// layer that "arrives" is actually heard within the next few seconds.
static void driftAddLayer(SceneData& s, int id, Rng& r) {
  randomizeLayer(s.L[id], id, r, 15, 40);
  if (layerIsEvent(id)) s.L[id].p[0] = (uint8_t)r.irange(50, 75);
}

// Pick an idle, untouched layer: DRONE or EVENTS with equal chance (falling
// back to the other group), or only DRONE when `droneOnly`.
static int pickIncoming(const SceneData& s, Rng& r, uint32_t touched, bool droneOnly, uint32_t pool) {
  int dr[NUM_LAYERS], nd = 0, ev[NUM_LAYERS], ne = 0;
  for (int i = 0; i < NUM_LAYERS; i++) {
    if (i == L_REVERB || isSounding(s.L[i]) || (touched & (1u << i)) || !(pool & (1u << i))) continue;
    // replacing the last drone needs a real bed, not HEART (v0.31)
    if (droneOnly ? layerIsBed(i) : layerIsDrone(i)) dr[nd++] = i;
    else if (layerIsEvent(i)) ev[ne++] = i;
  }
  bool wantDrone = droneOnly || r.chance(0.5f);
  if (wantDrone && nd) return dr[r.irange(0, nd - 1)];
  if (!droneOnly && ne) return ev[r.irange(0, ne - 1)];
  if (!droneOnly && nd) return dr[r.irange(0, nd - 1)];
  return -1;
}

// What a Drift step may do (SETTINGS > Drift, v0.41). Layers outside the Pool
// are never touched: they do not come in, go out or change.
// Arc (v0.5) only leans the odds of a layer coming in or leaving (bias); the
// Pool, Layers Min / Max and the Custom pace stay as they are.
struct DriftLimits { uint32_t pool; int lmin, lmax; bool modeChange; float bias; };

// One change on the planned scene. Returns the DriftKind applied (or DK_NONE).
static int driftMutate(SceneData& s, Rng& r, uint32_t& touched, const DriftLimits& lim) {
  const uint32_t pool = lim.pool;
  auto inPool = [&](int i) { return (pool & (1u << i)) != 0; };
  // The sounding layers in the Pool are the ones Drift counts and moves; the
  // real drone beds are counted wherever they are (outside the Pool they stay)
  int act[NUM_LAYERS], nA = 0, nD = 0, poolSize = 0;
  bool poolHasBed = false;
  for (int i = 0; i < NUM_LAYERS; i++) {
    if (i == L_REVERB) continue;
    if (inPool(i)) { poolSize++; if (layerIsBed(i)) poolHasBed = true; }
    if (!isSounding(s.L[i])) continue;
    if (layerIsBed(i)) nD++;   // HEART does not count as the drone that must stay (v0.31)
    if (inPool(i)) act[nA++] = i;
  }
  // A Pool smaller than Layers Min: Min is the Pool's size (the setting stays).
  // The last drone stays only when the Pool has a drone bed to do it with.
  const int lmin = std::min(lim.lmin, poolSize), lmax = std::max(lim.lmax, lmin);
  const bool keepBed = poolHasBed;
  // candidates among the sounding layers not yet changed in this step
  int freeAct[NUM_LAYERS], nFA = 0, paramAct[NUM_LAYERS], nPA = 0, removable[NUM_LAYERS], nR = 0;
  int modeAct[NUM_LAYERS], nMA = 0;
  for (int k = 0; k < nA; k++) {
    int i = act[k];
    if (touched & (1u << i)) continue;
    freeAct[nFA++] = i;
    const LayerDef& d = LAYER_DEFS[i];
    for (int q = 0; q < d.nParams; q++) if (!d.p[q].nOpts) { paramAct[nPA++] = i; break; }
    for (int q = 0; q < d.nParams; q++) if (d.p[q].nOpts > 1) { modeAct[nMA++] = i; break; }
    if (!(keepBed && layerIsBed(i) && nD <= 1)) removable[nR++] = i;   // the last drone stays
  }
  bool idleAny = false, idleDrone = false;   // an untouched silent layer that could come in
  for (int i = 0; i < NUM_LAYERS; i++) {
    if (i == L_REVERB || isSounding(s.L[i]) || (touched & (1u << i)) || !inPool(i)) continue;
    idleAny = true;
    if (layerIsBed(i)) idleDrone = true;
  }

  // weights: param 25, level 20, DREAD 10, remove 15, add 15, swap 15, mode 10
  int w[DK_NUM] = {25, 20, 10, 15, 15, 15, 10};
  if (lim.bias != 0.f) {   // Arc: Calm 3 / 27, Peak 27 / 3 (add / remove)
    const float b = lim.bias < -1.f ? -1.f : (lim.bias > 1.f ? 1.f : lim.bias);
    w[DK_ADD] = (int)lroundf(15.f * (1.f + 0.8f * b));
    w[DK_REMOVE] = (int)lroundf(15.f * (1.f - 0.8f * b));
  }
  if (!nPA) w[DK_PARAM] = 0;
  if (!nFA) w[DK_LEVEL] = 0;
  if (touched & DRIFT_TOUCH_DREAD) w[DK_DREAD] = 0;
  if (nA <= lmin || !nR) w[DK_REMOVE] = 0;           // at the lower bound: no removal
  if (nA >= lmax || !idleAny) w[DK_ADD] = 0;         // at the upper bound: no addition
  bool swapOk = nFA > 0 && idleAny && (nR > 0 || idleDrone);
  if (!swapOk) w[DK_SWAP] = 0;
  if (!lim.modeChange || !nMA) w[DK_MODE] = 0;
  // out of range (e.g. right after a scene load): force the way back
  if (nA > lmax && nR) { for (int k = 0; k < DK_NUM; k++) w[k] = 0; w[DK_REMOVE] = 1; }
  if (nA < lmin && idleAny) { for (int k = 0; k < DK_NUM; k++) w[k] = 0; w[DK_ADD] = 1; }
  int total = 0;
  for (int k = 0; k < DK_NUM; k++) total += w[k];
  if (!total) return DK_NONE;
  int roll = r.irange(0, total - 1), kind = 0;
  for (; kind < DK_NUM; kind++) { if (roll < w[kind]) break; roll -= w[kind]; }

  switch (kind) {
    case DK_PARAM: {
      int i = paramAct[r.irange(0, nPA - 1)];
      const LayerDef& d = LAYER_DEFS[i];
      int cont[MAX_P], nc = 0;
      for (int q = 0; q < d.nParams; q++) if (!d.p[q].nOpts) cont[nc++] = q;
      int q = cont[r.irange(0, nc - 1)];
      int delta = r.irange(8, 22) * (r.chance(0.5f) ? 1 : -1);
      s.L[i].p[q] = (uint8_t)constrain((int)s.L[i].p[q] + delta, 5, 95);
      touched |= 1u << i;
      break;
    }
    case DK_LEVEL: {
      int i = freeAct[r.irange(0, nFA - 1)];
      int delta = r.irange(8, 20) * (r.chance(0.5f) ? 1 : -1);
      s.L[i].level = (uint8_t)constrain((int)s.L[i].level + delta, 8, 85);
      touched |= 1u << i;
      break;
    }
    case DK_DREAD: {
      int delta = r.irange(5, 15) * (r.chance(0.5f) ? 1 : -1);
      s.dread = (uint8_t)constrain((int)s.dread + delta, 0, 90);
      touched |= DRIFT_TOUCH_DREAD;
      break;
    }
    case DK_REMOVE: {
      int i = removable[r.irange(0, nR - 1)];
      s.L[i].on = false;
      touched |= 1u << i;
      break;
    }
    case DK_ADD: {
      int i = pickIncoming(s, r, touched, false, pool);
      driftAddLayer(s, i, r);
      touched |= 1u << i;
      break;
    }
    case DK_MODE: {   // another mode / chord / surface... (dipped through, see update())
      int i = modeAct[r.irange(0, nMA - 1)];
      const LayerDef& d = LAYER_DEFS[i];
      int opt[MAX_P], no = 0;
      for (int q = 0; q < d.nParams; q++) if (d.p[q].nOpts > 1) opt[no++] = q;
      int q = opt[r.irange(0, no - 1)];
      int v = r.irange(0, d.p[q].nOpts - 2);
      if (v >= s.L[i].p[q]) v++;   // never the same one
      s.L[i].p[q] = (uint8_t)v;
      touched |= 1u << i;
      break;
    }
    default: {   // DK_SWAP: one goes out, another comes in, in the same morph
      int out = freeAct[r.irange(0, nFA - 1)];
      bool lastDrone = layerIsBed(out) && nD <= 1;
      if (lastDrone && !idleDrone) out = removable[r.irange(0, nR - 1)], lastDrone = false;
      int in = pickIncoming(s, r, touched | (1u << out), lastDrone, pool);
      if (in < 0) return DK_NONE;
      s.L[out].on = false;
      driftAddLayer(s, in, r);
      touched |= (1u << out) | (1u << in);
      break;
    }
  }
  return kind;
}

void SceneRuntime::update(float dt) {
  if (xf) {
    xfT += dt / xfDur;
    bool done = xfT >= 1.f;
    float t = done ? 1.f : xfT;
    float e = t * t * (3.f - 2.f * t);
    for (int i = 0; i < NUM_LAYERS; i++) {
      const LayerState& F = from.L[i];
      const LayerState& T = to.L[i];
      LayerState& V = live.L[i];
      float lf = F.on ? F.level : 0.f, lt = T.on ? T.level : 0.f;
      V.level = (uint8_t)(lf + (lt - lf) * e + 0.5f);
      V.on = F.on || T.on;
      const LayerDef& d = LAYER_DEFS[i];
      bool modeSwitch = false;
      for (int k = 0; k < d.nParams; k++) {
        if (d.p[k].nOpts) { V.p[k] = e < 0.5f ? F.p[k] : T.p[k]; modeSwitch |= F.p[k] != T.p[k]; }
        else V.p[k] = (uint8_t)(F.p[k] + (T.p[k] - F.p[k]) * e + 0.5f);
      }
      // Drift's Mode Change (v0.41): a layer that sounds on both sides switches
      // its mode in the middle of the transition, through a dip in its level,
      // so its character never jumps
      if (xfDip && modeSwitch && F.on && T.on) {
        const float g = (2.f * e - 1.f) * (2.f * e - 1.f);
        V.level = (uint8_t)(V.level * g + 0.5f);
      }
    }
    live.dread = (uint8_t)(from.dread + (to.dread - from.dread) * e + 0.5f);
    if (done) { finishXf(); xfDip = false; }
  }
  if (drift && !xf) {
    driftTimer -= dt;   // the wait starts once the previous morph has arrived
    if (driftTimer <= 0.f) {
      const DriftSpeedDef sp = speedDef(*this);
      const DriftLimits lim = {pool, layersMin, layersMax, modeChange, arcBias};
      SceneData t = live;
      uint32_t touched = 0;
      int n = driftRng.irange(sp.nLo, sp.nHi);
      for (int k = 0; k < n; k++) {
        int kind = driftMutate(t, driftRng, touched, lim);
        if (kind != DK_NONE) driftStats[kind]++;
      }
      driftSteps++;
      startXf(t, driftRng.range(sp.xfLo, sp.xfHi));
      xfDip = true;
      driftTimer = driftRng.range(sp.waitLo, sp.waitHi);
    }
  }
}
// ===========================================================================
// DREAD macro (spec 3.1). The scene keeps base values; DREAD is applied here,
// on the way to the engine, as multipliers / offsets.
// ===========================================================================
void buildEngineParams(const SceneData& s, float master, uint32_t seed, uint32_t resetSeq, EngineParams& p) {
  const float d = s.dread * 0.01f;
  for (int i = 0; i < NUM_LAYERS; i++) {
    const LayerState& L = s.L[i];
    const LayerDef& D = LAYER_DEFS[i];
    LayerCtl& c = p.L[i];
    for (int k = 0; k < MAX_P; k++) {
      if (k < D.nParams && D.p[k].nOpts) c.p[k] = (float)L.p[k];
      else c.p[k] = L.p[k] * 0.01f;
    }
    float lv = L.on ? L.level * 0.01f : 0.f;
    switch (i) {
      case L_CLUSTER:   // wider detune: faster, rougher beating
        c.p[1] = clampf(c.p[1] * (1.f + 0.8f * d) + 0.25f * d, 0.f, 1.f);
        break;
      case L_AIR:       // more noise, deeper movement
        lv *= 1.f + 0.5f * d;
        c.p[1] = clampf(c.p[1] + 0.4f * d * (1.f - c.p[1]), 0.f, 1.f);
        break;
      case L_METAL:     // louder, higher index
        lv *= 1.f + 0.8f * d;
        c.p[1] = clampf(c.p[1] + 0.4f * d * (1.f - c.p[1]), 0.f, 1.f);
        break;
      case L_STATIC:    // more unstable
        c.p[2] = clampf(c.p[2] + 0.5f * d * (1.f - c.p[2]), 0.f, 1.f);
        break;
      // v0.31 (spec v1.2 3.3-3.7)
      case L_DISCORD:   // wider detune, more slides
        c.p[2] = clampf(c.p[2] + 0.5f * d * (1.f - c.p[2]), 0.f, 1.f);
        c.p[4] = clampf(c.p[4] + 0.6f * d * (1.f - c.p[4]), 0.f, 1.f);
        break;
      case L_MICRO:     // wider spread, deeper glide
        c.p[2] = clampf(c.p[2] + 0.4f * d * (1.f - c.p[2]), 0.f, 1.f);
        c.p[3] = clampf(c.p[3] + 0.5f * d * (1.f - c.p[3]), 0.f, 1.f);
        break;
      case L_VOX:       // faster vowels (the detune widens through lfoMul)
        c.p[3] = clampf(c.p[3] + 0.5f * d * (1.f - c.p[3]), 0.f, 1.f);
        break;
      case L_SHEPARD:   // slides faster
        c.p[1] = clampf(c.p[1] + 0.35f * d * (1.f - c.p[1]), 0.f, 1.f);
        break;
      // L_HEART: through the tension below (React)
      default: break;
    }
    // Perceptual level curve for sound layers; the reverb mix stays linear
    c.level = (i == L_REVERB) ? lv : lv * lv * 1.5f;
  }
  p.lfoMul = 1.f + 0.8f * d;     // all LFOs swing deeper
  p.densMul = 1.f + 1.5f * d;    // all events come more often
  p.master = master;
  p.seed = seed;
  p.resetSeq = resetSeq;
  p.cutSeq = 0;   // Cursed Mode sets this on the live path only
  p.summonSeq = 0;  // the live path sets these (Summon, Listen's Response)
  p.summonLayer = -1;
  p.summonSeed = 0;
  p.tension = d;  // the live path adds Listen's Attention and Arc
  p.climax = 0.f; // Arc's climax: live only
  p.hushSeq = 0; p.heartStopSeq = 0; p.heartStopBeats = 0;
  p.heartSummonOnly = false;   // the splash sets it
  p.sceneMul = 1.f;   // Party Mode: live only
  memset(&p.party, 0, sizeof(p.party));
}

// ===========================================================================
// Scene files. Layers are keyed by NAME so the in-memory order can grow.
// ===========================================================================
void sceneToJson(JsonObject o, const SceneData& s) {
  o["name"] = s.name;
  o["dread"] = s.dread;
  JsonObject ls = o["layers"].to<JsonObject>();
  for (int i = 0; i < NUM_LAYERS; i++) {
    JsonObject l = ls[LAYER_DEFS[i].name].to<JsonObject>();
    l["lv"] = s.L[i].level;
    l["on"] = s.L[i].on;
    JsonArray pa = l["p"].to<JsonArray>();
    for (int k = 0; k < LAYER_DEFS[i].nParams; k++) pa.add(s.L[i].p[k]);
  }
}

void sceneFromJson(JsonObjectConst o, SceneData& s) {
  sceneDefaults(s, (const char*)(o["name"] | "Scene"));
  s.dread = (uint8_t)constrain((int)(o["dread"] | 30), 0, 100);
  JsonObjectConst ls = o["layers"];
  for (JsonPairConst kv : ls) {
    int i = findLayerByName(kv.key().c_str());
    if (i < 0) continue;   // a layer from a newer firmware: ignore
    JsonObjectConst l = kv.value();
    s.L[i].level = (uint8_t)constrain((int)(l["lv"] | 0), 0, 100);
    s.L[i].on = l["on"] | false;
    int k = 0;
    for (JsonVariantConst v : l["p"].as<JsonArrayConst>()) {
      if (k >= LAYER_DEFS[i].nParams) break;
      const ParamDef& pd = LAYER_DEFS[i].p[k];
      int mx = pd.nOpts ? pd.nOpts - 1 : 100;
      s.L[i].p[k] = (uint8_t)constrain(v.as<int>(), 0, mx);
      k++;
    }
  }
}

String sceneFilePath(const String& name) {
  String f = String(DIR_SCENE) + "/";
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_') f += c;
    else if (c == ' ') f += '_';
  }
  return f + ".json";
}

bool sceneSaveFile(const SceneData& s) {
  if (!gSdReady) return false;
  JsonDocument doc;
  sceneToJson(doc.to<JsonObject>(), s);
  SdLock lk;
  File f = SD.open(sceneFilePath(s.name), FILE_WRITE);
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

bool sceneLoadFile(const String& name, SceneData& s) {
  sceneDefaults(s, name.c_str());
  if (!gSdReady) return false;
  JsonDocument doc;
  {
    SdLock lk;
    File f = SD.open(sceneFilePath(name), FILE_READ);
    if (!f) return false;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;
  }
  sceneFromJson(doc.as<JsonObjectConst>(), s);
  sceneSetName(s, name.c_str());
  return true;
}

void sceneDeleteFile(const String& name) {
  if (!gSdReady) return;
  SdLock lk;
  SD.remove(sceneFilePath(name));
}

void sceneListFiles(std::vector<String>& names) {
  names.clear();
  if (!gSdReady) return;
  SdLock lk;
  File dir = SD.open(DIR_SCENE);
  if (!dir) return;
  File f;
  while ((f = dir.openNextFile())) {
    if (!f.isDirectory()) {
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        String n = (const char*)(doc["name"] | "");
        if (n.length()) names.push_back(n);
      }
    }
    f.close();
  }
  dir.close();
}
