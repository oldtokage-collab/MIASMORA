// MIASMORA — Party Mode (hidden). See party.h.
#include "party.h"
#include <Arduino.h>
#include "app.h"
#include "audio.h"
#include "export.h"
#include "power.h"
#include "party_synth.h"
#include "rec.h"
#include "listen.h"
#include "arc.h"
#include "planchette.h"
#include "poltergeist.h"
#include "storage.h"
#include "config.h"
#include <SD.h>

// UI hooks (ui.cpp)
bool uiPartyScreenOk();    // MAIN on screen, no dialog, not in HELP
bool uiDisplayIsOn();
void uiWakeDisplay();      // full brightness again

static constexpr float FADE_SEC = 0.5f, SILENT_SEC = 1.5f;
static constexpr float HEART_SEC = 10.f, HEART_MANUAL_SEC = 4.f;
static constexpr float DARK_SEC = 2.5f, BACK_SEC = 3.f;

static PartyStage g_stage = PS_OFF;
static float g_t = 0.f;
static bool g_manual = false;
static float g_sceneMul = 1.f;     // the scene's own level
static float g_masterMul = 1.f;    // dark: 0, back: rising
static uint32_t g_fillSeq = 0, g_cutSeq = 0;
static PartyParams g_pp;            // the settings in use (Cursed: the defaults)
static float g_partyLen = 75.f;     // a Cursed party lasts 60..90 s
static bool g_reveal = false;       // the reveal window is up
static bool g_cursedDone = false;   // a Cursed party happens once per power-on
static constexpr uint32_t CURSED_AFTER_MS = 10UL * 60UL * 1000UL;
static constexpr uint32_t ARM_MS = 30000;   // Debug: how long an armed party waits for MAIN
static uint32_t g_armedAt = 0;              // 0: not armed
static bool g_debug = false;                // this party was started from Debug's Cursed Trigger

// --- the unlock (v0.99) ----------------------------------------------------------
static constexpr const char* UNLOCK_PATH = DIR_ROOT "/unlocks.txt";
static bool g_unlocked = false;
static bool g_unlockNew = false;   // this party unlocked it: say so once it is over
static bool g_revealLine = false;  // ... in the reveal window (a Cursed party)
static constexpr const char* UNLOCK_MSG = "Poltergeist: When Found > Party unlocked";

void partyLoadUnlock() {
  if (!gSdReady) return;
  SdLock lk;
  File f = SD.open(UNLOCK_PATH, FILE_READ);
  if (!f) return;
  const String s = f.readString();
  f.close();
  g_unlocked = s.indexOf("party") >= 0;
}
bool partyUnlocked() { return g_unlocked; }
bool partyRevealUnlockLine() { return g_revealLine; }

// a party has begun: the first one unlocks When Found > Party for good
static void unlockParty() {
  if (g_unlocked) return;
  g_unlocked = true;
  g_unlockNew = true;
  if (!gSdReady) return;   // no card: unlocked for this power-on only
  SdLock lk;
  File f = SD.open(UNLOCK_PATH, FILE_WRITE);
  if (f) { f.print("party\n"); f.close(); }
}
// the party is over: a Cursed one says it in its reveal, the others in a toast
static void partyOver() {
  if (!g_unlockNew) return;
  g_unlockNew = false;
  if (!g_manual && !g_debug) g_revealLine = true;
  else uiToast("Poltergeist: When Found\n> Party unlocked");
}

static const char* const BEAT_NAMES[NUM_PARTY_BEATS] = {"Four", "Half", "Break", "DnB"};
static const char* const HAT_NAMES[NUM_PARTY_HATS] = {"Off", "8th", "16th"};
static const char* const BASS_NAMES[NUM_PARTY_BASS] = {"Off", "Bounce", "Roll"};
static const char* const CHORD_NAMES[NUM_PARTY_CHORDS] = {"Off", "Major", "Minor", "Sus"};
static const char* const KEY_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* const ITEM_NAMES[NUM_PARTY_ITEMS] = {"Tempo", "Beat", "Kick", "Punch", "Hat", "Bass", "Chord", "Key", "Swing"};

static void loadSettings(bool defaults) {
  g_pp = {};
  if (defaults) {   // spec 3.6: a Cursed party is the same for everyone
    g_pp.tempo = 124; g_pp.beat = PB_FOUR; g_pp.kick = 50; g_pp.punch = 70;
    g_pp.hat = PH_8TH; g_pp.bass = PBS_BOUNCE; g_pp.chord = PC_MINOR; g_pp.key = 9; g_pp.swing = 0;
  } else {
    g_pp.tempo = gSet.pTempo; g_pp.beat = gSet.pBeat; g_pp.kick = gSet.pKick; g_pp.punch = gSet.pPunch;
    g_pp.hat = gSet.pHat; g_pp.bass = gSet.pBass; g_pp.chord = gSet.pChord; g_pp.key = gSet.pKey; g_pp.swing = gSet.pSwing;
  }
}

static void go(PartyStage s) { g_stage = s; g_t = 0.f; }

const char* partyStartManual() {
  if (g_stage != PS_OFF) return "";
  if (exportBusy()) return "Not while exporting";
  if (powerSleeping() || planchetteActive()) return "";
  if (!gPlaying) appTogglePlay();   // a party needs the sound on
  g_manual = true;
  g_debug = false;
  loadSettings(false);
  unlockParty();
  go(PS_HEART);
  return nullptr;
}

const char* partyCursedBlocked(bool debug, bool anyScreen) {
  if (g_stage != PS_OFF || g_reveal) return "A party is on";
  if (!debug) {
    if (!gSet.cursed) return "Cursed is off";
    if (g_cursedDone) return "Once per power-on";
    if (millis() < CURSED_AFTER_MS) return "Too soon after power-on";
  }
  if (planchetteActive()) return "Not in PLANCHETTE";
  if (poltergeistStage() != PG_OFF) return "Not with Poltergeist";
  if (!gPlaying) return "Not playing";
  if (!uiDisplayIsOn()) return "Display is off";
  if (!anyScreen && !uiPartyScreenOk()) return "Only on MAIN";
  if (appSleepRemainingSec() >= 0) return "Not with the sleep timer on";   // nobody gets woken at night
  if (exportBusy()) return "Not while exporting";
  if (recActive()) return "Not while recording";
  if (appSleepFading() || powerSleeping()) return "Not while falling asleep";
  if (listenHushing()) return "Not during a Hush";
  if (arcClimaxRunning()) return "Not during Arc's climax";
  return nullptr;
}

void partyStartCursed() {
  g_cursedDone = true;
  g_manual = false;
  g_debug = false;   // checkArmed() sets it again for a Debug party
  unlockParty();
  loadSettings(true);   // spec 3.6: the defaults, the same party for everyone
  g_partyLen = 60.f + (esp_random() % 3001) * 0.01f;
  uiWakeDisplay();      // dimmed: back to full brightness
  go(PS_FADE);
}

void partyArmDebug() { g_armedAt = millis() | 1u; }

// an armed party: MAIN is back -> check everything again and start
static void checkArmed() {
  if (!g_armedAt) return;
  if (millis() - g_armedAt > ARM_MS) { g_armedAt = 0; uiToast("Party cancelled"); return; }
  if (!uiPartyScreenOk()) return;
  g_armedAt = 0;
  if (const char* why = partyCursedBlocked(true)) { uiToast(why); return; }
  partyStartCursed();
  g_debug = true;   // after partyStartCursed(), which is also the real Cursed path
}

bool partyRevealActive() { return g_reveal; }
void partyRevealClose() { g_reveal = false; g_revealLine = false; }

void partyEnd() {
  if (g_stage == PS_HEART || g_stage == PS_PARTY || g_stage == PS_FADE || g_stage == PS_SILENT) go(PS_CUT);
}

void partyFill() { if (g_stage == PS_PARTY) g_fillSeq++; }

// the scene comes back from its own seed
void appRestartScene();

void partyUpdate(float dt) {
  checkArmed();
  if (g_stage == PS_OFF) return;
  g_t += dt;
  // the sleep timer has ended playback (it faded the party with everything
  // else): no cut, just over
  if (!gPlaying) { go(PS_OFF); g_sceneMul = 1.f; g_masterMul = 1.f; appRestartScene(); partyOver(); return; }
  switch (g_stage) {
    case PS_FADE:
      g_sceneMul = fmaxf(0.f, 1.f - g_t / FADE_SEC);
      if (g_t >= FADE_SEC) { g_sceneMul = 0.f; go(PS_SILENT); }
      break;
    case PS_SILENT:
      if (g_t >= SILENT_SEC) go(PS_HEART);
      break;
    case PS_HEART: {
      // Fn+P: the scene fades while the heart starts
      if (g_manual) g_sceneMul = fmaxf(0.f, g_sceneMul - dt / FADE_SEC);
      const float len = g_manual ? HEART_MANUAL_SEC : HEART_SEC;
      if (g_t >= len) { g_sceneMul = 0.f; go(PS_PARTY); }
      break;
    }
    case PS_PARTY:
      g_sceneMul = 0.f;
      if (!g_manual && g_t >= g_partyLen) go(PS_CUT);   // a Cursed party ends by itself
      break;
    case PS_CUT:
      g_cutSeq++;          // the click; the engine drops to nothing at once
      g_masterMul = 0.f;
      go(PS_DARK);
      break;
    case PS_DARK:
      if (g_t >= DARK_SEC) { g_sceneMul = 1.f; appRestartScene(); go(PS_BACK); }
      break;
    case PS_BACK:
      g_masterMul = fminf(1.f, g_t / BACK_SEC);
      g_masterMul *= g_masterMul;
      if (g_t >= BACK_SEC) { g_masterMul = 1.f; if (!g_manual) g_reveal = true; go(PS_OFF); partyOver(); }
      break;
    default: break;
  }
}

void partyApply(EngineParams& p) {
  p.cutSeq += g_cutSeq;
  if (g_stage == PS_OFF) return;
  p.sceneMul = g_sceneMul;
  p.master *= g_masterMul;
  PartyParams& q = p.party;
  q = g_pp;
  q.fillSeq = g_fillSeq;
  switch (g_stage) {
    case PS_HEART: {
      const float len = g_manual ? HEART_MANUAL_SEC : HEART_SEC;
      const float x = fminf(1.f, g_t / len);
      q.on = true; q.kickOnly = true;
      q.heart = x * x;                     // muffled for a long while, then a kick
      q.level = 0.04f + 0.96f * x * x;     // from very quiet
      break;
    }
    case PS_PARTY: q.on = true; q.kickOnly = false; q.heart = 1.f; q.level = 1.f; break;
    default: q.on = false; break;
  }
}

bool partyActive() { return g_stage != PS_OFF; }
PartyStage partyStage() { return g_stage; }
float partyStageTime() { return g_t; }
bool partyManual() { return g_manual; }
uint32_t partyBeats() { return gEngine.partyBeats(); }
float partyBeatPhase() { return gEngine.partyBeatPhase(); }

const char* partyItemName(int i) { return (i >= 0 && i < NUM_PARTY_ITEMS) ? ITEM_NAMES[i] : ""; }

void partyItemValue(int i, char* out, int n) {
  switch (i) {
    case PI_TEMPO: snprintf(out, n, "%d BPM", g_pp.tempo); break;
    case PI_BEAT: snprintf(out, n, "%s", BEAT_NAMES[g_pp.beat % NUM_PARTY_BEATS]); break;
    case PI_KICK: snprintf(out, n, "%d", g_pp.kick); break;
    case PI_PUNCH: snprintf(out, n, "%d", g_pp.punch); break;
    case PI_HAT: snprintf(out, n, "%s", HAT_NAMES[g_pp.hat % NUM_PARTY_HATS]); break;
    case PI_BASS: snprintf(out, n, "%s", BASS_NAMES[g_pp.bass % NUM_PARTY_BASS]); break;
    case PI_CHORD: snprintf(out, n, "%s", CHORD_NAMES[g_pp.chord % NUM_PARTY_CHORDS]); break;
    case PI_KEY: snprintf(out, n, "%s", KEY_NAMES[g_pp.key % 12]); break;
    case PI_SWING: snprintf(out, n, "%d%%", g_pp.swing); break;
    default: out[0] = 0; break;
  }
}

void partyItemChange(int i, int d) {
  auto cyc = [&](uint8_t& v, int m) { v = (uint8_t)((v + d + m) % m); };
  switch (i) {
    case PI_TEMPO: g_pp.tempo = (uint8_t)constrain((int)g_pp.tempo + d * 2, 100, 140); break;
    case PI_BEAT: cyc(g_pp.beat, NUM_PARTY_BEATS); break;
    case PI_KICK: g_pp.kick = (uint8_t)constrain((int)g_pp.kick + d * 5, 0, 100); break;
    case PI_PUNCH: g_pp.punch = (uint8_t)constrain((int)g_pp.punch + d * 5, 0, 100); break;
    case PI_HAT: cyc(g_pp.hat, NUM_PARTY_HATS); break;
    case PI_BASS: cyc(g_pp.bass, NUM_PARTY_BASS); break;
    case PI_CHORD: cyc(g_pp.chord, NUM_PARTY_CHORDS); break;
    case PI_KEY: cyc(g_pp.key, 12); break;
    case PI_SWING: g_pp.swing = (uint8_t)constrain((int)g_pp.swing + d * 5, 0, 50); break;
    default: return;
  }
  // a party of one's own keeps its settings for the next one (spec 3.6)
  if (g_manual) {
    gSet.pTempo = g_pp.tempo; gSet.pBeat = g_pp.beat; gSet.pKick = g_pp.kick; gSet.pPunch = g_pp.punch;
    gSet.pHat = g_pp.hat; gSet.pBass = g_pp.bass; gSet.pChord = g_pp.chord; gSet.pKey = g_pp.key; gSet.pSwing = g_pp.swing;
    appSettingsChanged();
  }
}
