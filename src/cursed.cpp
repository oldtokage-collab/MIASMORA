#include "cursed.h"
#include <Arduino.h>
#include "app.h"
#include "audio.h"
#include "export.h"
#include "rec.h"
#include "power.h"
#include "listen.h"
#include "party.h"
#include "planchette.h"
#include "poltergeist.h"

using namespace dsp;

// Party Mode (v0.66, spec v1.23 3.2): when an effect is chosen, this rarely it
// is a party instead -- about once in 250 effects, i.e. a few hours apart on
// Often -- if everything allows it (party.cpp)
static constexpr float PARTY_CHANCE = 1.f / 250.f;

const char* const CURSED_LEVEL_NAMES[NUM_CURSED_LEVELS] = {"Off", "Rare", "Normal", "Often"};

// UI hooks (ui.cpp)
void uiCursedBrightness(int percent);   // -1 = back to the user's brightness
void uiCursedInvert(bool on);
bool uiDisplayIsOn();

enum Fx : uint8_t {
  FX_NONE = 0,
  FX_MOOD_SCROLL,   // moods switch by themselves, as if a key were held
  FX_DREAD,         // DREAD creeps up for a while, then snaps back
  FX_NAME,          // the mood name glitches
  FX_FLICKER,       // the backlight flickers
  FX_INVERT,        // the screen inverts for an instant
  FX_VIZ,           // the visualiser freezes
  FX_BLACKOUT,      // the screen goes dark for a few seconds
  FX_SILENCE,       // the sound drops out for a few seconds
  FX_GHOST,         // a layer you did not turn on sneaks in
  FX_VOLDIP,        // the volume sags and comes back
  FX_WHISPER,       // a cryptic message
  FX_BIG,           // a burst of static, a click, and everything goes dead
  FX_HEART,         // the heart stops for 2..3 beats, then races a little (v0.37, only with HEART on)
  FX_EYE,           // the Watcher's eye opens for an instant and shuts (v0.6; not with Watcher off)
  FX_PARTY,         // Party Mode (v0.66): never by weight, see PARTY_CHANCE
  NUM_FX
};
// Relative weights; the sound ones are skipped while playback is stopped
static const uint8_t WEIGHT[NUM_FX] = {0, 12, 10, 14, 12, 8, 8, 6, 8, 12, 8, 10, 5, 10, 6, 0};
static constexpr float EYE_SEC = 0.6f;   // open 0.15 s, held 0.3 s, shut 0.15 s
static bool isAudioFx(int f) { return f == FX_SILENCE || f == FX_GHOST || f == FX_VOLDIP || f == FX_BIG || f == FX_DREAD || f == FX_HEART; }
// HEART is part of the scene and actually sounding
static constexpr int HEART_MIN_LEVEL = 15;
static bool heartSounding() { const LayerState& L = gRt.live.L[L_HEART]; return L.on && L.level >= HEART_MIN_LEVEL; }
// Screen effects: never while the display is off (and never switch it on)
static bool isScreenFx(int f) { return f == FX_NAME || f == FX_FLICKER || f == FX_INVERT || f == FX_VIZ || f == FX_BLACKOUT || f == FX_WHISPER || f == FX_EYE; }
// the eye: only with Watcher on, and not while a Hush has it open already
static bool eyeOk() { return gSet.watcher && !listenHushing(); }

static const float MEAN_WAIT[NUM_CURSED_LEVELS] = {0.f, 360.f, 180.f, 60.f};   // seconds

static const char* const WHISPERS[] = {
  "...did you hear that?", "Who is there?", "Don't turn around.", "It's behind the door.",
  "Not alone.", "Listen.", "It knows you are here.", "Something moved.",
};
static const int NUM_WHISPERS = sizeof(WHISPERS) / sizeof(WHISPERS[0]);

struct Ghost { int id; float level; float p[MAX_P]; };

static Rng g_r;
static bool g_seeded = false;
static Fx g_fx = FX_NONE;
static float g_t = 0.f, g_dur = 0.f, g_next = 0.f;
static int g_phase = 0, g_count = 0, g_dir = 1;
static float g_step = 0.f, g_stepT = 0.f;
static float g_mul = 1.f;
static int g_boost = 0;
static bool g_name = false, g_viz = false;
static Ghost g_ghost = {-1, 0.f, {0}};
static uint32_t g_cutSeq = 0;
static uint32_t g_heartSeq = 0;
static bool g_forced = false;          // an effect started from Debug is running (Cursed may be off)
static int g_lastFx = FX_NONE;
static uint32_t g_lastMs = 0;
static bool g_lastDebug = false;
static const char* const FX_NAMES[NUM_FX] = {
  "", "Mood Scroll", "Dread Creep", "Name Glitch", "Flicker", "Invert", "Viz Freeze",
  "Blackout", "Silence", "Ghost Layer", "Volume Dip", "Whisper", "Static Burst", "Heart Stop", "Eye Open", "Party"};
static int g_heartBeats = 2;
static bool g_bright = false, g_invert = false;

int cursedDreadBoost() { return g_boost; }
bool cursedNameGlitch() { return g_name; }
bool cursedVizFrozen() { return g_viz; }
float cursedEyeOpen() {
  if (g_fx != FX_EYE) return 0.f;
  if (g_t < 0.15f) return g_t / 0.15f;
  if (g_t < EYE_SEC - 0.15f) return 1.f;
  return fmaxf(0.f, (EYE_SEC - g_t) / 0.15f);
}

static void rollNext() {
  float mean = MEAN_WAIT[gSet.cursed < NUM_CURSED_LEVELS ? gSet.cursed : 0];
  if (mean <= 0.f) mean = MEAN_WAIT[2];
  float s = g_r.expo(mean);
  g_next = s < 20.f ? 20.f : s;
}

void cursedStopAll() {
  g_fx = FX_NONE;
  g_forced = false;
  g_mul = 1.f;
  g_boost = 0;
  g_name = g_viz = false;
  g_ghost.id = -1;
  if (g_bright) { uiCursedBrightness(-1); g_bright = false; }
  if (g_invert) { uiCursedInvert(false); g_invert = false; }
}

static void setGhost(int id, float lv, float p0, float p1, float p2, float p3 = 0.f, float p4 = 0.f) {
  g_ghost.id = id;
  g_ghost.level = lv;
  g_ghost.p[0] = p0; g_ghost.p[1] = p1; g_ghost.p[2] = p2; g_ghost.p[3] = p3; g_ghost.p[4] = p4;
}

// A layer that is not already playing, so the visitor is noticeable
static bool pickGhost() {
  int ids[5] = {L_KNOCK, L_STEPS, L_BUMP, L_PULSE, L_CREAK};
  for (int tries = 0; tries < 8; tries++) {
    int id = ids[g_r.irange(0, 4)];
    const LayerState& L = gRt.live.L[id];
    if (L.on && L.level > 0) continue;
    switch (id) {
      case L_KNOCK: setGhost(id, 0.15f, 0.8f, (float)KN_KNOCK, 0.7f); break;
      case L_STEPS: setGhost(id, 0.18f, 0.9f, (float)SF_WOOD, (float)DIR_APPROACH, 0.4f, 0.6f); break;
      case L_BUMP:  setGhost(id, 0.15f, 0.8f, 0.3f, 0.6f); break;
      case L_PULSE: setGhost(id, 0.15f, 0.8f, 0.8f, 0.f); break;
      default:      setGhost(id, 0.15f, 0.9f, 0.4f, 0.f); break;
    }
    return true;
  }
  return false;
}

static int nextMood(int dir) {
  int n = (int)gScenes.size();
  if (n == 0) return -1;
  int cur = gCurScene < 0 ? 0 : gCurScene;
  return ((cur + dir) % n + n) % n;
}

static void start(Fx f) {
  g_fx = f; g_t = 0.f; g_phase = 0;
  g_lastFx = f; g_lastMs = millis(); g_lastDebug = g_forced;
  switch (f) {
    case FX_MOOD_SCROLL:
      g_count = g_r.irange(2, 5); g_dir = g_r.chance(0.5f) ? 1 : -1;
      g_step = g_r.range(0.06f, 0.15f); g_stepT = 0.f; g_dur = 10.f;
      break;
    case FX_DREAD:    g_dur = 20.f; break;
    case FX_EYE:      g_dur = EYE_SEC; break;   // drawn by the Watcher (cursedEyeOpen)
    case FX_NAME:     g_dur = 1.5f; g_name = true; break;
    case FX_FLICKER:  g_dur = 0.8f; g_stepT = 0.f; break;
    case FX_INVERT:   g_dur = g_r.range(0.15f, 0.5f); uiCursedInvert(true); g_invert = true; break;
    case FX_VIZ:      g_dur = g_r.range(4.f, 8.f); g_viz = true; break;
    case FX_BLACKOUT: g_dur = g_r.range(3.f, 6.f); uiCursedBrightness(0); g_bright = true; break;
    case FX_SILENCE:  g_dur = g_r.range(2.f, 5.f); g_mul = 0.f; break;
    case FX_GHOST:
      if (!pickGhost()) { g_fx = FX_NONE; return; }
      g_dur = g_r.range(10.f, 25.f);
      break;
    case FX_VOLDIP:   g_dur = 6.f; break;
    case FX_WHISPER:  uiToast(WHISPERS[g_r.irange(0, NUM_WHISPERS - 1)]); g_fx = FX_NONE; return;
    case FX_HEART:
      // the engine does the rest (HEART skips the beats, then +10 BPM fading
      // over 10..20 s); nothing to undo here
      g_heartBeats = g_r.irange(2, 3);
      g_heartSeq++;
      g_fx = FX_NONE;
      return;
    case FX_BIG:
      // phase 0: a sudden burst of loud static
      setGhost(L_STATIC, 0.9f, (float)ST_TV, 0.75f, 0.9f);
      g_dur = g_r.range(0.4f, 1.2f);
      break;
    default: g_fx = FX_NONE; return;
  }
}

static void startRandom() {
  if (g_r.uni() < PARTY_CHANCE && !partyCursedBlocked(false)) {
    g_lastFx = FX_PARTY; g_lastMs = millis(); g_lastDebug = false;
    partyStartCursed();
    return;
  }
  int total = 0;
  for (int i = 1; i < NUM_FX; i++) total += WEIGHT[i];
  for (int tries = 0; tries < 6; tries++) {
    int r = g_r.irange(0, total - 1), f = 1;
    for (; f < NUM_FX; f++) { if (r < WEIGHT[f]) break; r -= WEIGHT[f]; }
    if (f >= NUM_FX) f = FX_WHISPER;
    if (isAudioFx(f) && !gPlaying) continue;
    if (f == FX_HEART && !heartSounding()) continue;
    if (f == FX_EYE && !eyeOk()) continue;
    if (isScreenFx(f) && !uiDisplayIsOn()) continue;
    start((Fx)f);
    return;
  }
}

static void step(float dt) {
  g_t += dt;
  switch (g_fx) {
    case FX_MOOD_SCROLL:
      g_stepT += dt;
      if (g_stepT >= g_step) {
        g_stepT = 0.f;
        int idx = nextMood(g_dir);
        if (idx >= 0) appSelectScene(idx);
        if (--g_count <= 0) g_fx = FX_NONE;
      }
      break;
    case FX_DREAD:
      g_boost = g_t < 18.f ? (int)(25.f * g_t / 18.f) : 25;
      break;
    case FX_FLICKER:
      g_stepT -= dt;
      if (g_stepT <= 0.f) {
        g_stepT = g_r.range(0.04f, 0.09f);
        uiCursedBrightness(g_r.chance(0.5f) ? g_r.irange(0, 25) : gSet.brightness);
        g_bright = true;
      }
      break;
    case FX_VOLDIP:
      g_mul = 1.f - 0.5f * sinf(PI_F * g_t / g_dur);
      break;
    case FX_BIG:
      if (g_phase == 0 && g_t >= g_dur) {
        // phase 1: the click, dead silence, dark screen
        g_phase = 1; g_t = 0.f;
        g_dur = g_r.range(3.f, 6.f);
        g_ghost.id = -1;
        g_cutSeq++;
        g_mul = 0.f;
        uiCursedBrightness(0); g_bright = true;
        return;
      }
      break;
    default: break;
  }
  if (g_fx != FX_NONE && g_t >= g_dur) cursedStopAll();
}

void cursedUpdate(float dt) {
  if (!g_seeded) { g_r.seed(esp_random()); g_seeded = true; rollNext(); }
  if (gSet.cursed == 0 && !g_forced) { if (g_fx != FX_NONE || g_mul != 1.f) cursedStopAll(); return; }
  // Never while exporting or recording, during the sleep timer's fade-out or
  // on the way into deep sleep: end anything in progress and wait
  if (exportBusy() || recActive() || appSleepFading() || powerSleeping() || partyActive() || planchetteActive() || poltergeistOwnsAudio()) {
    if (g_fx != FX_NONE || g_mul != 1.f) cursedStopAll();
    return;
  }
  // The display went off in the middle of a screen effect: drop it quietly
  if (isScreenFx(g_fx) && !uiDisplayIsOn()) cursedStopAll();
  if (g_fx != FX_NONE) { step(dt); return; }
  if (g_forced) { g_forced = false; return; }   // the Debug effect has ended
  if (gSet.cursed == 0) return;
  g_next -= dt;
  if (g_next <= 0.f) { startRandom(); rollNext(); }
}

void cursedApply(EngineParams& p) {
  p.cutSeq = g_cutSeq;
  p.heartStopSeq = g_heartSeq;
  p.heartStopBeats = (int8_t)g_heartBeats;
  if (gSet.cursed == 0 && !g_forced) return;
  p.master *= g_mul;
  if (g_ghost.id >= 0) {
    LayerCtl& c = p.L[g_ghost.id];
    if (c.level < g_ghost.level) c.level = g_ghost.level;
    for (int k = 0; k < MAX_P; k++) c.p[k] = g_ghost.p[k];
  }
}

// --- Debug (spec v1.22 2.1) ----------------------------------------------------
int cursedFxCount() { return NUM_FX - 1; }
const char* cursedFxName(int i) { return (i >= 0 && i < NUM_FX - 1) ? FX_NAMES[i + 1] : ""; }

const char* cursedTrigger(int i) {
  if (i < 0 || i >= NUM_FX - 1) return "Unknown effect";
  const Fx f = (Fx)(i + 1);
  if (!g_seeded) { g_r.seed(esp_random()); g_seeded = true; rollNext(); }
  // the same ground rules as the natural ones
  if (exportBusy()) return "Not while exporting";
  if (recActive()) return "Not while recording";
  if (appSleepFading() || powerSleeping()) return "Not while falling asleep";
  if (isScreenFx(f) && !uiDisplayIsOn()) return "Display is off";
  if (isAudioFx(f) && !gPlaying) return "Not playing";
  if (f == FX_HEART && !heartSounding()) return "HEART is not playing";
  if (f == FX_PARTY) {   // the time since power-on, once per power-on, Cursed on: not here
    // pressed on the Debug page: armed now, it starts once MAIN is back (v0.67)
    if (const char* why = partyCursedBlocked(true, true)) return why;
    if (g_fx != FX_NONE) cursedStopAll();
    g_lastFx = FX_PARTY; g_lastMs = millis(); g_lastDebug = true;
    partyArmDebug();
    return "Party armed: back to MAIN";
  }
  if (f == FX_EYE && !gSet.watcher) return "Watcher is off";
  if (f == FX_EYE && listenHushing()) return "The eye is open already";
  if (g_fx != FX_NONE) cursedStopAll();   // one at a time
  g_forced = true;
  start(f);
  if (f == FX_GHOST && g_fx == FX_NONE) { g_forced = false; return "No free layer for a ghost"; }
  return nullptr;
}

bool cursedLast(const char*& name, uint32_t& ms, bool& fromDebug) {
  if (g_lastFx == FX_NONE) return false;
  name = FX_NAMES[g_lastFx]; ms = g_lastMs; fromDebug = g_lastDebug;
  return true;
}