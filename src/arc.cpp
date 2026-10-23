// MIASMORA — Arc (phase 4, spec v1.1 6 / v1.2 5.1). See arc.h.
//
// Progress runs only while playing and not exporting (spec 6.5: frozen at the
// export's start, resumed afterwards). Its pace is 1 / Arc Length, or -- Once,
// Arc Follows Sleep, the sleep timer running -- whatever makes the rest of it
// end when the timer does; the timer switched on or off midway re-fits it from
// where it is (spec chat, phase 4 note 2).
//
// The climax (spec 6.1, v1.2 5.1) comes once per pass, timed so that the
// silence after it ends where Release begins:
//   rise   6 s   DREAD and the event rate climb, the scene's own event layers
//                are summoned one after another (only voices that are free --
//                nothing is cut off), HEART races and hits harder, SHEPARD
//                slides faster
//   burst  1 s   at the top; the scene's STATIC (if it plays) bursts out
//   end          with STATIC: an instant cut with a click; without: a 0.3 s fade
//   silence 2.5 s
//   back   3 s   the sound comes back into Release
// A Listen Hush wins over all of it (no summons while hushed).
#include "arc.h"
#include <algorithm>
#include "arc_curve.h"
#include "app.h"
#include "export.h"
#include "listen.h"
#include "power.h"
#include "layers/layer_defs.h"
#include "party.h"
#include "planchette.h"
#include "poltergeist.h"

const uint8_t ARC_LEN_MIN[NUM_ARC_LENS] = {10, 20, 30};
const char* const ARC_SECTION_NAMES[4] = {"Calm", "Build", "Peak", "Release"};

static constexpr float MIX_SEC = 8.f;         // Arc fades in / out over this (switched on / off)
static constexpr float CL_RISE_SEC = 6.f, CL_BURST_SEC = 1.f, CL_FADE_SEC = 0.3f;
static constexpr float CL_SILENT_SEC = 2.5f, CL_BACK_SEC = 3.f;
static constexpr float CL_TO_RELEASE = CL_RISE_SEC + CL_BURST_SEC + CL_SILENT_SEC;
static constexpr float CL_DREAD = 20.f;       // on top of Peak's +30 (the sum is capped at 100)
static constexpr float CL_DENS = 0.4f;        // x(1 + 0.4) at the top: 2.0x (arc::DENS_CAP)
static constexpr float TENSION_ARC = 0.4f;    // HEART's React: Arc's share of the tension

enum ClPhase : uint8_t { CL_NONE = 0, CL_RISE, CL_BURST, CL_SILENT, CL_BACK };

static float g_p = 0.f;            // progress 0..1
static float g_mix = 0.f;          // 0 off .. 1 on (faded)
static bool  g_done = false;       // Once has finished: it stays Calm
static bool  g_wasFollowing = false;
static ClPhase g_cl = CL_NONE;
static bool  g_clDone = false;     // this pass has had its climax
static float g_clT = 0.f, g_clC = 0.f, g_mul = 1.f, g_nextSummon = 0.f;
static bool  g_burst = false;      // STATIC bursts out (it plays in the scene)
static uint32_t g_cutSeq = 0;
static dsp::Rng g_r;
static bool g_seeded = false;

static float level() { return arc::intensity(g_done ? 0.f : g_p); }

void arcRestart() {
  g_p = 0.f;
  g_done = false;
  g_clDone = false;
  g_cl = CL_NONE; g_clC = 0.f; g_mul = 1.f; g_burst = false;
}

static bool followEligible() {
  return gSet.arcFollowSleep && gSet.arcCycle == ARC_ONCE && appSleepRemainingSec() >= 0 && !appSleepFading();
}

// progress per second
static float pace() {
  if (followEligible()) {
    const float rem = (float)appSleepRemainingSec();
    return (1.f - g_p) / std::max(rem, 1.f);
  }
  return 1.f / (ARC_LEN_MIN[gSet.arcLenIdx % NUM_ARC_LENS] * 60.f);
}

// snap: at once (stopped, exporting); else the sound comes back over CL_BACK_SEC
static void endClimax(bool snap) {
  if (g_cl == CL_NONE) return;
  g_clC = 0.f;
  g_burst = false;
  if (snap || g_mul >= 1.f) { g_cl = CL_NONE; g_mul = 1.f; }
  else { g_cl = CL_BACK; g_clT = g_mul * CL_BACK_SEC; }   // back up from where it is
  g_clDone = true;
}

static void startClimax() {
  g_cl = CL_RISE; g_clT = 0.f; g_clC = 0.f; g_mul = 1.f;
  g_nextSummon = g_r.range(0.3f, 0.8f);
  g_burst = false;
  g_clDone = true;
}

// one event of the scene: SWELL and PULSE first, then any event layer or
// HEART that sounds (level 15+) and has a voice free
static void summonOne() {
  int ids[NUM_LAYERS], w[NUM_LAYERS], n = 0, total = 0;
  for (int i = 0; i < NUM_LAYERS; i++) {
    const LayerState& L = gRt.live.L[i];
    if (!(layerIsEvent(i) || i == L_HEART) || !L.on || L.level < 15) continue;
    if (!gEngine.layerHasFreeVoice(i)) continue;
    ids[n] = i; w[n] = (i == L_SWELL || i == L_PULSE) ? 3 : 1; total += w[n]; n++;
  }
  if (!n) return;
  int r = g_r.irange(0, total - 1), k = 0;
  for (; k < n - 1; k++) { if (r < w[k]) break; r -= w[k]; }
  appRequestSummon(ids[k], g_r.next());
}

static void stepClimax(float dt) {
  if (g_cl == CL_NONE) return;
  g_clT += dt;
  switch (g_cl) {
    case CL_RISE:
      g_clC = arc::smooth01(g_clT / CL_RISE_SEC);
      if (g_clT >= CL_RISE_SEC) {
        g_cl = CL_BURST; g_clT = 0.f; g_clC = 1.f;
        const LayerState& st = gRt.live.L[L_STATIC];
        g_burst = st.on && st.level >= 10;
      }
      break;
    case CL_BURST:
      if (g_clT >= CL_BURST_SEC) {
        g_cl = CL_SILENT; g_clT = 0.f; g_clC = 0.f;
        if (g_burst) { g_cutSeq++; g_mul = 0.f; }   // the click and dead silence
        g_burst = false;
      }
      break;
    case CL_SILENT:
      if (g_mul > 0.f) g_mul = std::max(0.f, 1.f - g_clT / CL_FADE_SEC);
      if (g_clT >= CL_SILENT_SEC) { g_cl = CL_BACK; g_clT = 0.f; g_mul = 0.f; }
      break;
    case CL_BACK:
      g_mul = arc::smooth01(g_clT / CL_BACK_SEC);
      if (g_clT >= CL_BACK_SEC) { g_cl = CL_NONE; g_mul = 1.f; }
      break;
    default: break;
  }
  // the scene's events, one after another, closer together towards the top
  if (g_cl == CL_RISE || g_cl == CL_BURST) {
    g_nextSummon -= dt;
    if (g_nextSummon <= 0.f) {
      if (!listenHushing()) summonOne();
      g_nextSummon = g_r.range(0.35f, 0.6f) + (1.f - g_clC) * g_r.range(0.4f, 0.8f);
    }
  }
}

void arcUpdate(float dt) {
  if (!g_seeded) { g_r.seed(esp_random()); g_seeded = true; }
  const bool on = gSet.arc && gPlaying;
  const float step = dt / MIX_SEC;
  g_mix = on ? std::min(1.f, g_mix + step) : std::max(0.f, g_mix - step);
  if (!on) { endClimax(true); g_wasFollowing = false; return; }
  // frozen while exporting (the climax is dropped: the audio is paused anyway)
  // and at a party (it carries on from there afterwards)
  if (exportBusy() || partyActive() || planchetteActive() || poltergeistOwnsAudio()) { endClimax(true); return; }
  if (appSleepFading() || powerSleeping()) endClimax(false);

  // Arc Follows Sleep: the timer switched on after a finished Once starts a new one
  const bool follow = followEligible();
  if (follow && !g_wasFollowing && g_done) arcRestart();
  g_wasFollowing = follow;

  if (g_done && gSet.arcCycle == ARC_LOOP) arcRestart();   // Cycle changed to Loop
  if (!g_done) {
    const float r = pace();
    g_p += dt * r;
    if (g_p >= 1.f) {
      if (gSet.arcCycle == ARC_LOOP) { g_p -= 1.f; g_clDone = false; }
      else { g_p = 1.f; g_done = true; }
    }
    // the climax: timed so that its silence ends where Release begins
    if (!g_clDone && g_cl == CL_NONE && g_p >= arc::PEAK_AT) {
      if (g_p >= arc::RELEASE_AT) g_clDone = true;   // too late for this pass
      else if ((arc::RELEASE_AT - g_p) <= CL_TO_RELEASE * r && !appSleepFading() && !powerSleeping()) startClimax();
    }
  }
  stepClimax(dt);
}

int arcDreadAdd() {
  if (g_mix <= 0.f) return 0;
  return (int)lroundf(g_mix * (arc::dreadAdd(level()) + CL_DREAD * g_clC));
}
float arcDensMul() {
  if (g_mix <= 0.f) return 1.f;
  return powf(arc::densMul(level()), g_mix) * (1.f + CL_DENS * g_clC);
}
float arcDriftBias() { return g_mix * (2.f * level() - 1.f); }

int arcExportDreadAdd() { return g_mix > 0.f ? (int)lroundf(g_mix * arc::dreadAdd(level())) : 0; }
float arcExportDensMul() { return g_mix > 0.f ? powf(arc::densMul(level()), g_mix) : 1.f; }

void arcApply(EngineParams& p) {
  p.cutSeq += g_cutSeq;   // Cursed set its own count; either one cuts
  if (g_mix <= 0.f && g_cl == CL_NONE) return;
  p.tension = 1.f - (1.f - p.tension) * (1.f - TENSION_ARC * g_mix * level());
  p.climax = g_clC;
  p.master *= g_mul;
  if (g_burst) {   // the scene's STATIC bursts out, harsher
    LayerCtl& c = p.L[L_STATIC];
    if (c.level < 0.9f) c.level = 0.9f;
    c.p[2] = std::max(c.p[2], 0.9f);
  }
}

bool  arcShown() { return gSet.arc; }
float arcProgress() { return g_p; }
int   arcSection() { return (int)arc::section(g_p); }
bool  arcFinished() { return g_done; }
bool  arcClimaxRunning() { return g_cl != CL_NONE && g_cl != CL_BACK; }
bool  arcFollowing() { return gSet.arc && followEligible() && !g_done; }
