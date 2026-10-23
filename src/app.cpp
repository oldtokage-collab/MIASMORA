#include "app.h"
#include <SD.h>
#include <algorithm>
#include "audio.h"
#include "export.h"
#include "rec.h"
#include "storage.h"
#include "config.h"
#include "cursed.h"
#include "power.h"
#include "listen.h"
#include "arc.h"
#include "party.h"
#include "planchette.h"
#include "poltergeist.h"
#include "arc_curve.h"

const char* const SHAKE_ACTION_NAMES[NUM_SHAKE_ACTIONS] = {"Off", "Random Scene", "Sleep Timer", "Display Off",
                                                          "New Seed", "Next Mood", "Hush", "Summon", "Drift Now", "Quick Save"};
const uint8_t FADE_IN_SEC[NUM_FADE_IN] = {0, 2, 5, 10, 30};
const uint16_t DIM_AFTER_SEC[NUM_DIM_AFTER] = {0, 30, 60, 120, 300, 600};
const uint16_t AUTO_OFF_SEC[NUM_AUTO_OFF] = {0, 60, 120, 300, 600, 1800};
const uint8_t SHAKE_SENS_VALUES[NUM_SHAKE_SENS] = {25, 50, 75};
// Up to v0.38 the sleep timer was one choice: index into {off, 15, 30, 60, 90,
// 1 (test)} minutes. Read from old settings files only.
static const uint8_t OLD_SLEEP_MINUTES[6] = {0, 15, 30, 60, 90, 1};
const uint8_t LOOP_LENS[NUM_LOOP_LENS] = {8, 16, 32, 64};
const uint8_t XF_LENS[NUM_XF_LENS] = {1, 2, 4};

AppSettings gSet;
SceneRuntime gRt;
std::vector<SceneEntry> gScenes;
int gCurScene = 0;
bool gEdited = false;
bool gPlaying = true;
uint32_t gSeed = 0;
bool gImuReady = false;
const char* gResetReason = "?";

static uint32_t g_resetSeq = 1;
static bool g_settingsDirty = false, g_sceneDirty = false;
static uint32_t g_dirtyAt = 0;


static uint32_t randomSeed6() { return esp_random() % 1000000UL; }

static void settingsDefaults() {
  gSet.volume = 60;
  gSet.seedLock = false;
  gSet.startup = START_STOP;
  gSet.fadeInIdx = 1;          // 2 s
  gSet.startupSound = true;
  gSet.driftSpeed = 1;
  gSet.dWaitMin = 12; gSet.dWaitMax = 25;
  gSet.dXf = 10;
  gSet.dChMin = 1; gSet.dChMax = 2;
  gSet.dLayMin = 2; gSet.dLayMax = 6;
  gSet.dPool = 0xFFFFFFFFu;
  gSet.dModeChange = false;
  gSet.sleepOn = false;
  gSet.sleepMin = 30;
  gSet.sleepFade = true;
  gSet.sleepFadeSec = 45;
  gSet.sleepAfter = AFTER_DEEP_SLEEP;
  gSet.brightness = 70;
  gSet.dimIdx = 2;             // 1 min
  gSet.dimLevel = 30;
  gSet.autoOffIdx = 3;         // 5 min
  gSet.wakeAct = false;
  gSet.viz = 1;
  gSet.led = true;
  gSet.ledBright = 50;
  gSet.shakeAction = SHAKE_OFF;
  gSet.shakeSens = 50;
  gSet.plRange = 35;
  gSet.plSmooth = 1;
  gSet.plMode = 0;
  static const char* const CORNERS[4] = {"Fog Town", "Cellar", "Infestation", "Otherworld"};
  for (int k = 0; k < 4; k++) { strncpy(gSet.plCorner[k], CORNERS[k], 23); gSet.plCorner[k][23] = 0; }
  gSet.listen = false;
  gSet.listenSens = 1;
  gSet.hushDepth = HUSH_DEEP;
  gSet.response = true;
  gSet.watcher = true;
  gSet.arc = false;
  gSet.arcLenIdx = 1;          // 20 min
  gSet.arcCycle = ARC_ONCE;
  gSet.arcFollowSleep = true;
  gSet.cursed = 0;
  gSet.cursedFreq = 2;         // Normal
  gSet.listenLog = false;
  gSet.markKeys = false;
  gSet.pTempo = 124; gSet.pBeat = 0; gSet.pKick = 50; gSet.pPunch = 70;
  gSet.pHat = 1; gSet.pBass = 1; gSet.pChord = 2; gSet.pKey = 9; gSet.pSwing = 0;
  // Poltergeist: KNOCK (Tap) in slot 1, Random 1..8 min, Escalate, 3 min to hide it, Auto Stop 1 h
  memset(gSet.pgEv, 0, sizeof(gSet.pgEv));
  memset(gSet.pgMode, 0, sizeof(gSet.pgMode));
  for (int i = 0; i < pgLayerCount(); i++) if (pgLayerAt(i) == L_KNOCK) gSet.pgEv[0] = (uint8_t)i;
  { const int mk = pgModeParam(L_KNOCK); if (mk >= 0) gSet.pgMode[0] = KN_TAP; }
  gSet.pgTiming = 1;
  gSet.pgFixIdx = 7;    // 2 min
  gSet.pgMinIdx = 5;    // 1 min
  gSet.pgMaxIdx = 12;   // 8 min
  gSet.pgEscalate = true;
  gSet.pgDelayIdx = 2;  // 3 min
  gSet.pgStopIdx = 1;   // 1 h
  gSet.pgVolume = 70;
  gSet.pgRoom = 1;
  gSet.pgShy = true;
  gSet.pgFound = 1;     // Reveal
  gSet.pgLast[0] = 0;
  gSet.xNormalize = false;
  gSet.xMode = 0;
  gSet.xLenIdx = 1;
  gSet.xXfIdx = 1;
  gSet.xNewSeed = false;
  gSet.xDriftRecord = false;
  gSet.xArcApply = false;
  gSet.xStems = 0;
  gSet.xBatchIdx = 0;
  gSet.xBatchFolder = true;
  gSet.xBatchSleep = false;
  gSet.oLayerIdx = 0; gSet.oMode = 0; gSet.oUnit = 0; gSet.oCountIdx = 1; gSet.oWet = false; gSet.oNorm = true;
}

void appSettingsChanged() { g_settingsDirty = true; g_dirtyAt = millis(); }

// SETTINGS > Drift -> the runtime (and through it every export that records Drift)
void appSyncDrift() {
  gRt.driftSpeed = gSet.driftSpeed;
  gRt.waitMin = gSet.dWaitMin; gRt.waitMax = gSet.dWaitMax;
  gRt.xfSec = gSet.dXf;
  gRt.chMin = gSet.dChMin; gRt.chMax = gSet.dChMax;
  gRt.layersMin = gSet.dLayMin; gRt.layersMax = gSet.dLayMax;
  gRt.pool = gSet.dPool;
  gRt.modeChange = gSet.dModeChange;
}
void appSceneEdited() { g_sceneDirty = true; gEdited = true; g_dirtyAt = millis(); }
void appSettleEdit() { gRt.finishXf(); }

bool appIsDrift() { return gCurScene >= 0 && gCurScene < (int)gScenes.size() && gScenes[gCurScene].kind == SK_DRIFT; }
const char* appMoodName() { return gRt.live.name; }
String appMoodTag() { return sanitizeName(gRt.live.name); }

// ---------------------------------------------------------------------------
// Scene registry
// ---------------------------------------------------------------------------
static void buildSceneList() {
  gScenes.clear();
  for (int i = 0; i < NUM_PRESETS; i++) gScenes.push_back({String(presetName(i)), SK_PRESET, i});
  gScenes.push_back({String(DRIFT_NAME), SK_DRIFT, -1});
  std::vector<String> files;
  sceneListFiles(files);
  std::sort(files.begin(), files.end(), [](const String& a, const String& b) { return a.compareTo(b) < 0; });
  for (auto& n : files) gScenes.push_back({n, SK_CUSTOM, -1});
}

static int findEntry(const String& name, int kind) {
  for (size_t i = 0; i < gScenes.size(); i++)
    if (gScenes[i].name == name && (kind < 0 || gScenes[i].kind == kind)) return (int)i;
  return -1;
}

static void sceneForEntry(int idx, SceneData& s) {
  const SceneEntry& e = gScenes[idx];
  if (e.kind == SK_PRESET) presetToScene(e.presetIdx, s);
  else if (e.kind == SK_DRIFT) generateRandomScene(gSeed, s, DRIFT_NAME);
  else sceneLoadFile(e.name, s);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
static void saveSettings() {
  if (!gSdReady) return;
  JsonDocument doc;
  // flat keys (spec v1.2 4.3): the old ones as they were, new ones added
  doc["vol"] = gSet.volume;
  doc["bright"] = gSet.brightness;
  doc["shake"] = gSet.shakeAction;
  doc["plRange"] = gSet.plRange;
  doc["plSmooth"] = gSet.plSmooth;
  doc["plMode"] = gSet.plMode;
  { JsonArray ca = doc["plCorners"].to<JsonArray>(); for (int k = 0; k < 4; k++) ca.add(gSet.plCorner[k]); }
  doc["shakeSens"] = gSet.shakeSens;
  doc["seedLock"] = gSet.seedLock;
  doc["driftSpd"] = gSet.driftSpeed;
  doc["dWaitMin"] = gSet.dWaitMin;
  doc["dWaitMax"] = gSet.dWaitMax;
  doc["dXf"] = gSet.dXf;
  doc["dChMin"] = gSet.dChMin;
  doc["dChMax"] = gSet.dChMax;
  doc["dLayMin"] = gSet.dLayMin;
  doc["dLayMax"] = gSet.dLayMax;
  doc["dModeChg"] = gSet.dModeChange;
  {   // the Pool by layer NAME (the order of the layers can grow)
    JsonArray off = doc["dPoolOff"].to<JsonArray>();
    for (int i = 0; i < NUM_LAYERS; i++) if (!(gSet.dPool & (1u << i))) off.add(LAYER_DEFS[i].name);
  }
  doc["xNorm"] = gSet.xNormalize;
  {
    JsonArray ev = doc["pgEv"].to<JsonArray>(), md = doc["pgMode"].to<JsonArray>();
    for (int k = 0; k < 3; k++) {   // by name: the layer list may grow
      const int id = pgLayerAt(gSet.pgEv[k]);
      ev.add(id < 0 ? "None" : LAYER_DEFS[id].name);
      md.add(gSet.pgMode[k]);
    }
  }
  doc["pgTiming"] = gSet.pgTiming; doc["pgFix"] = gSet.pgFixIdx; doc["pgMin"] = gSet.pgMinIdx; doc["pgMax"] = gSet.pgMaxIdx;
  doc["pgEsc"] = gSet.pgEscalate; doc["pgDelay"] = gSet.pgDelayIdx; doc["pgStop"] = gSet.pgStopIdx;
  doc["pgVol"] = gSet.pgVolume; doc["pgRoom"] = gSet.pgRoom; doc["pgShy"] = gSet.pgShy; doc["pgFound"] = gSet.pgFound;
  doc["pgLast"] = gSet.pgLast;
  doc["pTempo"] = gSet.pTempo; doc["pBeat"] = gSet.pBeat; doc["pKick"] = gSet.pKick;
  doc["pPunch"] = gSet.pPunch; doc["pHat"] = gSet.pHat; doc["pBass"] = gSet.pBass;
  doc["pChord"] = gSet.pChord; doc["pKey"] = gSet.pKey; doc["pSwing"] = gSet.pSwing;
  doc["cursed"] = gSet.cursed;
  doc["cursedFreq"] = gSet.cursedFreq;
  doc["listen"] = gSet.listen;
  doc["listenSens"] = gSet.listenSens;
  doc["hushDepth"] = gSet.hushDepth;
  doc["response"] = gSet.response;
  doc["watcher"] = gSet.watcher;
  doc["led"] = gSet.led;
  doc["ledBr"] = gSet.ledBright;
  doc["arc"] = gSet.arc;
  doc["arcLen"] = gSet.arcLenIdx;
  doc["arcCycle"] = gSet.arcCycle;
  doc["arcSleep"] = gSet.arcFollowSleep;
  doc["startup"] = gSet.startup;
  doc["fadeIn"] = gSet.fadeInIdx;
  doc["startSnd"] = gSet.startupSound;
  doc["sleepOn"] = gSet.sleepOn;
  doc["sleepMin"] = gSet.sleepMin;
  doc["sleepFade"] = gSet.sleepFade;
  doc["sleepFadeSec"] = gSet.sleepFadeSec;
  doc["sleepAfter"] = gSet.sleepAfter;
  doc["dim"] = gSet.dimIdx;
  doc["dimLevel"] = gSet.dimLevel;
  doc["autoOff"] = gSet.autoOffIdx;
  doc["wakeAct"] = gSet.wakeAct;
  doc["viz"] = gSet.viz;
  doc["listenLog"] = gSet.listenLog;
  doc["markKeys"] = gSet.markKeys;
  doc["seed"] = gSeed;
  doc["playing"] = gPlaying;
  doc["xMode"] = gSet.xMode;
  doc["xLen"] = gSet.xLenIdx;
  doc["xXf"] = gSet.xXfIdx;
  doc["xNewSeed"] = gSet.xNewSeed;
  doc["xDrift"] = gSet.xDriftRecord;
  doc["xArc"] = gSet.xArcApply;
  doc["xStems"] = gSet.xStems;
  doc["xBatch"] = gSet.xBatchIdx;
  doc["xBatchDir"] = gSet.xBatchFolder;
  doc["xBatchSleep"] = gSet.xBatchSleep;
  doc["oLayer"] = gSet.oLayerIdx; doc["oMode"] = gSet.oMode; doc["oUnit"] = gSet.oUnit;
  doc["oCount"] = gSet.oCountIdx; doc["oWet"] = gSet.oWet; doc["oNorm"] = gSet.oNorm;
  bool loose = gCurScene < 0;
  if (!loose) {
    doc["scene"] = gScenes[gCurScene].name;
    doc["kind"] = (int)gScenes[gCurScene].kind;
  } else {
    doc["scene"] = "";
  }
  // A generated scene, or unsaved edits on top of a preset, survive a reboot here
  if (loose || (gEdited && gScenes[gCurScene].kind == SK_PRESET))
    sceneToJson(doc["live"].to<JsonObject>(), gRt.settled());
  SdLock lk;
  File f = SD.open(SETTINGS_PATH, FILE_WRITE);
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

static void loadSettings() {
  settingsDefaults();
  gSeed = randomSeed6();
  JsonDocument doc;
  bool ok = false;
  if (gSdReady) {
    SdLock lk;
    File f = SD.open(SETTINGS_PATH, FILE_READ);
    if (f) { ok = !deserializeJson(doc, f); f.close(); }
  }
  if (ok) {
    gSet.volume      = constrain((int)(doc["vol"] | 60), 0, 100);
    gSet.brightness  = constrain((int)(doc["bright"] | 70), 10, 100);
    gSet.shakeAction = constrain((int)(doc["shake"] | 0), 0, NUM_SHAKE_ACTIONS - 1);
    gSet.plRange = constrain((int)(doc["plRange"] | 35), 20, 45);
    gSet.plSmooth = constrain((int)(doc["plSmooth"] | 1), 0, 2);
    gSet.plMode = constrain((int)(doc["plMode"] | 0), 0, 1);
    if (doc["plCorners"].is<JsonArrayConst>()) {
      int k = 0;
      for (JsonVariantConst v : doc["plCorners"].as<JsonArrayConst>()) {
        if (k >= 4) break;
        const char* s = v.as<const char*>();
        if (s && *s) { strncpy(gSet.plCorner[k], s, 23); gSet.plCorner[k][23] = 0; }
        k++;
      }
    }
    // Shake Sensitivity was 0..100 up to v0.38: the nearest of Low / Mid / High
    int ss = constrain((int)(doc["shakeSens"] | 50), 0, 100);
    gSet.shakeSens   = ss < 38 ? 25 : (ss < 63 ? 50 : 75);
    gSet.seedLock    = doc["seedLock"] | false;
    gSet.driftSpeed  = constrain((int)(doc["driftSpd"] | 1), 0, NUM_DRIFT_SPEEDS - 1);
    gSet.dWaitMin    = constrain((int)(doc["dWaitMin"] | 12), 3, 120);
    gSet.dWaitMax    = constrain((int)(doc["dWaitMax"] | 25), gSet.dWaitMin, 120);
    gSet.dXf         = constrain((int)(doc["dXf"] | 10), 2, 30);
    gSet.dChMin      = constrain((int)(doc["dChMin"] | 1), 1, 3);
    gSet.dChMax      = constrain((int)(doc["dChMax"] | 2), gSet.dChMin, 3);
    gSet.dLayMin     = constrain((int)(doc["dLayMin"] | 2), 1, 8);
    gSet.dLayMax     = constrain((int)(doc["dLayMax"] | 6), gSet.dLayMin, 8);
    gSet.dModeChange = doc["dModeChg"] | false;
    gSet.dPool = 0xFFFFFFFFu;
    for (JsonVariantConst v : doc["dPoolOff"].as<JsonArrayConst>()) {
      int i = findLayerByName(v.as<const char*>());
      if (i >= 0) gSet.dPool &= ~(1u << i);
    }
    gSet.xNormalize  = doc["xNorm"] | false;
    if (doc["pgEv"].is<JsonArrayConst>()) {
      int k = 0;
      for (JsonVariantConst v : doc["pgEv"].as<JsonArrayConst>()) {
        if (k >= 3) break;
        const char* nm = v.as<const char*>();
        gSet.pgEv[k] = 0;
        if (nm) for (int i = 1; i < pgLayerCount(); i++) if (!strcmp(LAYER_DEFS[pgLayerAt(i)].name, nm)) gSet.pgEv[k] = (uint8_t)i;
        k++;
      }
      k = 0;
      for (JsonVariantConst v : doc["pgMode"].as<JsonArrayConst>()) { if (k < 3) gSet.pgMode[k++] = (uint8_t)constrain(v.as<int>(), 0, 15); }
    }
    gSet.pgTiming = constrain((int)(doc["pgTiming"] | 1), 0, 1);
    gSet.pgFixIdx = constrain((int)(doc["pgFix"] | 7), 0, PG_NUM_INTERVALS - 1);
    gSet.pgMinIdx = constrain((int)(doc["pgMin"] | 5), 0, PG_NUM_INTERVALS - 1);
    gSet.pgMaxIdx = constrain((int)(doc["pgMax"] | 12), 0, PG_NUM_INTERVALS - 1);
    gSet.pgEscalate = doc["pgEsc"] | true;
    gSet.pgDelayIdx = constrain((int)(doc["pgDelay"] | 2), 0, PG_NUM_DELAYS - 1);
    gSet.pgStopIdx = (uint8_t)(doc["pgStop"] | 1);
    if (gSet.pgStopIdx >= PG_NUM_STOPS) gSet.pgStopIdx = 1;   // the 1 min test (v0.92..v0.96) is gone: 1 h
    gSet.pgVolume = constrain((int)(doc["pgVol"] | 70), 0, 100);
    gSet.pgRoom = constrain((int)(doc["pgRoom"] | 1), 0, 2);
    gSet.pgShy = doc["pgShy"] | true;
    gSet.pgFound = constrain((int)(doc["pgFound"] | 1), 0, 2);
    if (gSet.pgFound == 0 && !partyUnlocked()) gSet.pgFound = 1;   // v0.99: Party still locked -> Reveal
    strncpy(gSet.pgLast, doc["pgLast"] | "", sizeof(gSet.pgLast) - 1);
    gSet.pgLast[sizeof(gSet.pgLast) - 1] = 0;
    gSet.pTempo = constrain((int)(doc["pTempo"] | 124), 100, 140);
    gSet.pBeat  = constrain((int)(doc["pBeat"] | 0), 0, 3);
    gSet.pKick  = constrain((int)(doc["pKick"] | 50), 0, 100);
    gSet.pPunch = constrain((int)(doc["pPunch"] | 70), 0, 100);
    gSet.pHat   = constrain((int)(doc["pHat"] | 1), 0, 2);
    gSet.pBass  = constrain((int)(doc["pBass"] | 1), 0, 2);
    gSet.pChord = constrain((int)(doc["pChord"] | 2), 0, 3);
    gSet.pKey   = constrain((int)(doc["pKey"] | 9), 0, 11);
    gSet.pSwing = constrain((int)(doc["pSwing"] | 0), 0, 50);
    // Cursed: the old levels Low / Mid / High are now Rare / Normal / Often
    gSet.cursed      = constrain((int)(doc["cursed"] | 0), 0, NUM_CURSED_LEVELS - 1);
    gSet.cursedFreq  = constrain((int)(doc["cursedFreq"] | (gSet.cursed ? (int)gSet.cursed : 2)), 1, NUM_CURSED_LEVELS - 1);
    if (gSet.cursed) gSet.cursed = gSet.cursedFreq;
    gSet.listen      = doc["listen"] | false;   // consent was given when it was turned on
    gSet.listenSens  = constrain((int)(doc["listenSens"] | 1), 0, NUM_LISTEN_SENS - 1);
    gSet.hushDepth   = constrain((int)(doc["hushDepth"] | (int)HUSH_DEEP), 0, NUM_HUSH_DEPTHS - 1);
    gSet.response    = doc["response"] | true;
    gSet.watcher     = doc["watcher"] | true;
    gSet.led         = doc["led"] | true;
    gSet.ledBright   = constrain((int)(doc["ledBr"] | 50), 10, 100);
    gSet.arc         = doc["arc"] | false;
    gSet.arcLenIdx   = constrain((int)(doc["arcLen"] | 1), 0, NUM_ARC_LENS - 1);
    gSet.arcCycle    = constrain((int)(doc["arcCycle"] | 0), 0, 1);
    gSet.arcFollowSleep = doc["arcSleep"] | true;
    gSet.startup     = constrain((int)(doc["startup"] | 0), 0, 1);
    gSet.fadeInIdx   = constrain((int)(doc["fadeIn"] | 1), 0, NUM_FADE_IN - 1);
    gSet.startupSound = doc["startSnd"] | true;
    if (doc["sleepOn"].isNull()) {
      // up to v0.38: one index (0 = off); the old 1 min test entry reads as Time 1 min
      int si = constrain((int)(doc["sleep"] | 0), 0, 5);
      gSet.sleepOn = si > 0;
      if (si > 0) gSet.sleepMin = OLD_SLEEP_MINUTES[si];
    } else {
      gSet.sleepMin = constrain((int)(doc["sleepMin"] | 30), 1, 240);
    }
    // v0.43: the timer itself starts Off after every power-on (Time, Fade and
    // After are kept); it is switched on for this session only
    gSet.sleepOn = false;
    gSet.sleepFade    = doc["sleepFade"] | true;
    gSet.sleepFadeSec = constrain((int)(doc["sleepFadeSec"] | 45), 10, 120);
    gSet.sleepAfter   = constrain((int)(doc["sleepAfter"] | 0), 0, 1);
    gSet.dimIdx       = constrain((int)(doc["dim"] | 2), 0, NUM_DIM_AFTER - 1);
    gSet.dimLevel     = constrain((int)(doc["dimLevel"] | 30), 10, 50);
    gSet.autoOffIdx   = constrain((int)(doc["autoOff"] | 3), 0, NUM_AUTO_OFF - 1);
    gSet.wakeAct      = doc["wakeAct"] | false;
    gSet.viz          = constrain((int)(doc["viz"] | 1), 0, NUM_VIZ - 1);   // the old On (1) is Bars
    gSet.listenLog    = doc["listenLog"] | false;
    gSet.markKeys     = doc["markKeys"] | false;
    // Startup (spec v1.2 4.2): Stop, or Resume = play the last scene
    gSet.xMode       = constrain((int)(doc["xMode"] | 0), 0, 1);
    gSet.xLenIdx     = constrain((int)(doc["xLen"] | 1), 0, NUM_LOOP_LENS - 1);
    gSet.xXfIdx      = constrain((int)(doc["xXf"] | 1), 0, NUM_XF_LENS - 1);
    gSet.xNewSeed    = doc["xNewSeed"] | false;
    gSet.xDriftRecord = doc["xDrift"] | false;
    gSet.xArcApply   = doc["xArc"] | false;
    gSet.xStems      = constrain((int)(doc["xStems"] | 0), 0, 2);
    gSet.xBatchIdx   = constrain((int)(doc["xBatch"] | 0), 0, NUM_BATCH - 1);
    gSet.xBatchFolder = doc["xBatchDir"] | true;
    gSet.xBatchSleep = doc["xBatchSleep"] | false;
    gSet.oLayerIdx = constrain((int)(doc["oLayer"] | 0), 0, 31);
    gSet.oMode = constrain((int)(doc["oMode"] | 0), 0, 15);
    gSet.oUnit = constrain((int)(doc["oUnit"] | 0), 0, 1);
    gSet.oCountIdx = constrain((int)(doc["oCount"] | 1), 0, NUM_ONESHOT_COUNTS - 1);
    gSet.oWet = doc["oWet"] | false;
    gSet.oNorm = doc["oNorm"] | true;
    // Boot counts as a play start: a locked seed is kept, otherwise a new one is rolled
    if (gSet.seedLock) gSeed = (uint32_t)(doc["seed"] | 0UL) % 1000000UL;
  }
  buildSceneList();
  appSyncDrift();

  gCurScene = 0;
  String name = ok ? String((const char*)(doc["scene"] | "")) : String();
  int kind = ok ? (int)(doc["kind"] | 0) : 0;
  if (ok && name.length() == 0 && !doc["live"].isNull()) {
    gCurScene = -1;   // generated scene
    sceneFromJson(doc["live"].as<JsonObjectConst>(), gRt.live);
    gRt.drift = false;
  } else {
    int idx = ok ? findEntry(name, kind) : -1;
    if (idx >= 0) gCurScene = idx;
    sceneForEntry(gCurScene, gRt.live);
    gRt.drift = false;
    if (gScenes[gCurScene].kind == SK_DRIFT) gRt.driftBegin(gSeed);
    if (ok && gScenes[gCurScene].kind == SK_PRESET && !doc["live"].isNull()) {
      sceneFromJson(doc["live"].as<JsonObjectConst>(), gRt.live);
      sceneSetName(gRt.live, gScenes[gCurScene].name.c_str());
      gEdited = true;
    }
  }
  gRt.xf = false;
  gPlaying = gSet.startup == START_RESUME;
}

static void flushDirty() {
  if (!(g_settingsDirty || g_sceneDirty)) return;
  if (millis() - g_dirtyAt < SAVE_DEBOUNCE_MS) return;
  if (g_sceneDirty && gCurScene >= 0 && gScenes[gCurScene].kind == SK_CUSTOM) {
    SceneData s = gRt.settled();
    sceneSetName(s, gScenes[gCurScene].name.c_str());
    sceneSaveFile(s);   // custom scene: edits go straight into its file
  }
  saveSettings();
  g_settingsDirty = g_sceneDirty = false;
}

static void flushSceneNow() {
  if (g_sceneDirty && gCurScene >= 0 && gScenes[gCurScene].kind == SK_CUSTOM) {
    SceneData s = gRt.settled();
    sceneSetName(s, gScenes[gCurScene].name.c_str());
    sceneSaveFile(s);
  }
  g_sceneDirty = false;
}

static float g_playFade = 0.f;   // Fade In (Sound): 0 -> 1 after each play start

void appInit() {
  partyLoadUnlock();   // v0.99: before the settings (When Found > Party)
  loadSettings();
  g_playFade = 0.f;
  appSleepArm();   // a sleep timer left on counts from power-on
}

void appResetSettings() {
  if (gSet.listen) listenEnable(false);
  cursedStopAll();
  const bool xn = gSet.xNormalize;   // EXPORT's own choices are not SETTINGS
  const uint8_t xm = gSet.xMode, xl = gSet.xLenIdx, xx = gSet.xXfIdx;
  const bool xs = gSet.xNewSeed, xd = gSet.xDriftRecord, xa = gSet.xArcApply;
  const uint8_t xst = gSet.xStems, xbi = gSet.xBatchIdx;
  const bool xbf = gSet.xBatchFolder, xbs = gSet.xBatchSleep;
  const uint8_t ol = gSet.oLayerIdx, om = gSet.oMode, ou = gSet.oUnit, oc = gSet.oCountIdx;
  const bool ow = gSet.oWet, on = gSet.oNorm;
  settingsDefaults();
  gSet.xNormalize = xn; gSet.xMode = xm; gSet.xLenIdx = xl; gSet.xXfIdx = xx; gSet.xNewSeed = xs; gSet.xDriftRecord = xd;
  gSet.xArcApply = xa; gSet.xStems = xst;
  gSet.xBatchIdx = xbi; gSet.xBatchFolder = xbf; gSet.xBatchSleep = xbs;
  gSet.oLayerIdx = ol; gSet.oMode = om; gSet.oUnit = ou; gSet.oCountIdx = oc; gSet.oWet = ow; gSet.oNorm = on;
  appSleepArm();
  appSettingsChanged();
}

int appShakeSensIdx() { return gSet.shakeSens < 38 ? 0 : (gSet.shakeSens < 63 ? 1 : 2); }

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void appTogglePlay() {
  if (gPlaying) {
    gPlaying = false;
  } else {
    if (!gSet.seedLock) gSeed = randomSeed6();
    g_resetSeq++;
    gPlaying = true;
    g_playFade = 0.f;
    appSleepArm();
    arcRestart();   // every play starts its Arc at Calm
  }
  appSettingsChanged();
}

// Party Mode: the scene starts again from its own seed (the engine restarts)
void appRestartScene() { g_resetSeq++; }
void appNewRandomSeed() {
  gSeed = randomSeed6();
  appSettingsChanged();
}

void appSetSeed(uint32_t seed) {
  gSeed = seed % 1000000UL;
  g_resetSeq++;
  // Seed-derived scenes follow the seed, so a typed seed reproduces them
  if (gCurScene < 0 && !strcmp(gRt.live.name, RANDOM_SCENE_NAME)) {
    SceneData s;
    generateRandomScene(gSeed, s, RANDOM_SCENE_NAME);
    gRt.startXf(s, SCENE_XFADE_SEC);
    gEdited = false;
  } else if (appIsDrift()) {
    SceneData s;
    generateRandomScene(gSeed, s, DRIFT_NAME);
    gRt.startXf(s, SCENE_XFADE_SEC);
    gRt.driftBegin(gSeed);
  }
  appSettingsChanged();
}

void appRandomScene() {
  flushSceneNow();
  if (!gSet.seedLock) gSeed = randomSeed6();
  g_resetSeq++;
  SceneData s;
  generateRandomScene(gSeed, s, RANDOM_SCENE_NAME);
  gRt.drift = false;
  gRt.startXf(s, SCENE_XFADE_SEC);
  gCurScene = -1;
  gEdited = false;
  appSettingsChanged();
}

void appSelectScene(int idx) {
  if (idx < 0 || idx >= (int)gScenes.size()) return;
  flushSceneNow();
  SceneData s;
  sceneForEntry(idx, s);
  gRt.drift = false;
  gRt.startXf(s, SCENE_XFADE_SEC);
  if (gScenes[idx].kind == SK_DRIFT) gRt.driftBegin(gSeed);
  gCurScene = idx;
  gEdited = false;
  appSettingsChanged();
}

bool appSaveSceneAs(const String& rawName) {
  String name = rawName;
  name.trim();
  if (!name.length() || !gSdReady) return false;
  if (name == DRIFT_NAME || name == RANDOM_SCENE_NAME) return false;
  for (int i = 0; i < NUM_PRESETS; i++) if (name == presetName(i)) return false;
  appSettleEdit();
  SceneData s = gRt.live;
  sceneSetName(s, name.c_str());
  if (!sceneSaveFile(s)) return false;
  buildSceneList();
  gCurScene = findEntry(name, SK_CUSTOM);
  sceneSetName(gRt.live, name.c_str());
  gRt.drift = false;
  gEdited = false;
  g_sceneDirty = false;
  appSettingsChanged();
  return gCurScene >= 0;
}

bool appDeleteScene(int idx) {
  if (idx < 0 || idx >= (int)gScenes.size() || gScenes[idx].kind != SK_CUSTOM) return false;
  String cur = gCurScene >= 0 ? gScenes[gCurScene].name : String();
  int curKind = gCurScene >= 0 ? (int)gScenes[gCurScene].kind : -1;
  bool deletingCurrent = idx == gCurScene;
  if (deletingCurrent) g_sceneDirty = false;
  sceneDeleteFile(gScenes[idx].name);
  buildSceneList();
  if (deletingCurrent) gCurScene = -1;   // keep playing it as a loose scene
  else if (gCurScene >= 0) gCurScene = findEntry(cur, curKind);
  appSettingsChanged();
  return true;
}

// ---------------------------------------------------------------------------
// Sleep timer (spec v1.2 4.2): after Time minutes, fade out over Fade Length
// (Fade off: 1 s, never a click), then either save and deep sleep (wake with
// G0) or stop playing and switch the display off (After). Held while an
// export or a recording is running.
// ---------------------------------------------------------------------------
static bool s_armed = false, s_fading = false, s_held = false;
static uint32_t s_start = 0;
static float s_fadePos = 0.f, s_mul = 1.f;

void appSleepArm() {
  s_armed = gSet.sleepOn && gPlaying;
  s_start = millis();
  s_fading = s_held = false;
  s_fadePos = 0.f;
  s_mul = 1.f;
}
int appSleepRemainingSec() {
  if (!s_armed) return -1;
  int total = gSet.sleepMin * 60;
  int el = (int)((millis() - s_start) / 1000);
  return total > el ? total - el : 0;
}
bool appSleepHeld() { return s_armed && s_held; }
bool appSleepFading() { return s_armed && s_fading; }
int appSleepFadeSec() { return gSet.sleepFade ? gSet.sleepFadeSec : 1; }
const uint8_t BATCH_COUNTS[NUM_BATCH] = {0, 5, 10, 20, 50};
const uint8_t ONESHOT_COUNTS[NUM_ONESHOT_COUNTS] = {1, 5, 10, 20, 50};

static void sleepFinish();
// A batch's Sleep After (spec v1.1 8.2 / v1.2 5.3): as if the timer ran out now
void appSleepNow() {
  if (gPlaying) { s_armed = true; s_held = false; s_fading = true; s_fadePos = 0.f; return; }   // the fade, then After
  sleepFinish();
}

static void updateSleep(float dt) {
  if (!s_armed) { s_mul = 1.f; return; }
  if (!gPlaying) { s_armed = false; s_mul = 1.f; return; }
  bool busy = exportBusy() || recActive();
  if (!s_fading) {
    if (appSleepRemainingSec() <= 0) {
      if (busy) s_held = true;
      else { s_held = false; s_fading = true; s_fadePos = 0.f; }
    }
    s_mul = 1.f;
    return;
  }
  if (busy) return;   // a recording started during the fade: hold where we are
  s_fadePos += dt / (float)appSleepFadeSec();
  float f = 1.f - s_fadePos;
  if (f <= 0.f) {
    s_mul = 0.f;
    sleepFinish();
    return;
  }
  s_mul = f * f;   // perceptually even fade
}

// the end of the timer, after its fade: SETTINGS > Sleep > After
static void sleepFinish() {
  if (gSet.sleepAfter == AFTER_DISPLAY_OFF) {
    // stop playing and switch the display off; the device stays awake
    gPlaying = false;
    s_armed = s_fading = false;
    s_mul = 1.f;
    appSettingsChanged();
    uiDisplayOff();
    return;
  }
  // Pending auto-saves are written now (then deep sleep; waking with G0 is
  // a restart, which follows Startup).
  flushSceneNow();
  saveSettings();
  g_settingsDirty = false;
  listenLogFlush();
  powerDeepSleep();   // does not return
}

// ---------------------------------------------------------------------------
// --- Summon (spec v1.22 1.6) ---------------------------------------------------
static uint32_t g_summonSeq = 0, g_summonSeed = 0;
static int g_summonLayer = -1;
static dsp::Rng g_summonRng;   // its own: never a layer's stream
static bool g_summonSeeded = false;

void appRequestSummon(int layer, uint32_t seed) {
  if (layer < 0 || layer >= NUM_LAYERS) return;
  g_summonLayer = layer;
  g_summonSeed = seed;
  g_summonSeq++;
}

// One event of the scene: an EVENTS layer (or HEART) that is on at level 15
// or more and has a voice free; nothing is stolen
static void summonOne() {
  if (!g_summonSeeded) { g_summonRng.seed(esp_random()); g_summonSeeded = true; }
  if (!gPlaying) { uiToast("Not playing"); return; }
  int cand[NUM_LAYERS], n = 0, nFree = 0, freeIds[NUM_LAYERS];
  for (int i = 0; i < NUM_LAYERS; i++) {
    const LayerState& L = gRt.live.L[i];
    if (!(layerIsEvent(i) || i == L_HEART) || !L.on || L.level < 15) continue;
    cand[n++] = i;
    if (gEngine.layerHasFreeVoice(i)) freeIds[nFree++] = i;
  }
  if (!n) { uiToast("Nothing answers"); return; }
  if (!nFree) { uiToast("No voice free"); return; }
  const int id = freeIds[g_summonRng.irange(0, nFree - 1)];
  appRequestSummon(id, g_summonRng.next());
  uiToast(LAYER_DEFS[id].name);
}

// --- Quick Save (spec v1.22 1.8) and PLANCHETTE's save (v0.8): a snapshot as
// <prefix>_NNN; the current scene stays what it is (a Drift keeps drifting)
const char* appSnapshot(const char* prefix, char* nameOut, int nOut, const SceneData* scene) {
  if (!gSdReady) return "No SD card";
  char name[16];
  int k = 1;
  for (; k <= 999; k++) {
    snprintf(name, sizeof(name), "%s_%03d", prefix, k);
    bool used = false;
    for (auto& e : gScenes) if (e.name == name) { used = true; break; }
    if (!used) break;
  }
  if (k > 999) return "No free name";
  SceneData s = scene ? *scene : gRt.settled();
  sceneSetName(s, name);
  if (!sceneSaveFile(s)) return "Could not save";
  const String cur = gCurScene >= 0 ? gScenes[gCurScene].name : String();
  const int curKind = gCurScene >= 0 ? (int)gScenes[gCurScene].kind : -1;
  buildSceneList();
  if (gCurScene >= 0) gCurScene = findEntry(cur, curKind);
  if (nameOut && nOut > 0) { strncpy(nameOut, name, nOut - 1); nameOut[nOut - 1] = 0; }
  static char b[32];
  snprintf(b, sizeof(b), "Saved %s", name);
  return b;
}
static void quickSave() { uiToast(appSnapshot("Shake")); }

void appMarkEdited() { gEdited = true; appSettingsChanged(); }

// --- a new seed through a dip (spec v1.22 1.3) -------------------------------------
static int g_seedPhase = 0;          // 0 none, 1 going down, 2 coming back
static float g_seedT = 0.f, g_seedMul = 1.f;
static uint32_t g_seedNext = 0;
static constexpr float SEED_DOWN_SEC = 1.f, SEED_UP_SEC = 1.5f, SEED_DIP = 0.1f;   // -20 dB

void appChangeSeedSmooth(uint32_t seed) {
  if (!gPlaying) { appSetSeed(seed); return; }   // nothing to hear: at once
  g_seedNext = seed % 1000000UL;
  if (g_seedPhase == 1) return;   // already going down: the newest seed is taken there
  // coming back up from an earlier one: go down again from where it is
  g_seedT = g_seedPhase == 2 ? (1.f - (g_seedMul - SEED_DIP) / (1.f - SEED_DIP)) * SEED_DOWN_SEC : 0.f;
  g_seedPhase = 1;
}

static void updateSeedChange(float dt) {
  if (g_seedPhase == 0) { g_seedMul = 1.f; return; }
  g_seedT += dt;
  if (g_seedPhase == 1) {
    const float x = std::min(g_seedT / SEED_DOWN_SEC, 1.f);
    g_seedMul = 1.f - (1.f - SEED_DIP) * x;
    if (x >= 1.f) { appSetSeed(g_seedNext); g_seedPhase = 2; g_seedT = 0.f; }   // the engine restarts down here
  } else {
    const float x = std::min(g_seedT / SEED_UP_SEC, 1.f);
    g_seedMul = SEED_DIP + (1.f - SEED_DIP) * x;
    if (x >= 1.f) { g_seedPhase = 0; g_seedMul = 1.f; }
  }
}

void appShakeRun(uint8_t action) {
  switch (action) {
    case SHAKE_RANDOM:
      appRandomScene();
      break;
    case SHAKE_SLEEP: {
      // with the Sleep settings (Time, Fade, After); again: off (v0.43)
      if (appSleepRemainingSec() >= 0) {
        gSet.sleepOn = false;
        appSleepArm();
        appSettingsChanged();
        uiToast("Sleep timer off");
        break;
      }
      if (!gPlaying) { uiToast("Not playing"); break; }
      gSet.sleepOn = true;
      appSleepArm();
      appSettingsChanged();
      char b[32];
      snprintf(b, sizeof(b), "Sleep timer %d min", gSet.sleepMin);
      uiToast(b);
      break;
    }
    case SHAKE_NEWSEED: {
      const uint32_t s = randomSeed6();
      appChangeSeedSmooth(s);
      char b[24];
      snprintf(b, sizeof(b), "SEED %06lu", (unsigned long)s);
      uiToast(b);
      break;
    }
    case SHAKE_NEXTMOOD: {   // as "/" on MAIN
      const int n = (int)gScenes.size();
      if (n == 0) break;
      const int idx = gCurScene < 0 ? 0 : (gCurScene + 1) % n;
      appSelectScene(idx);
      uiToast(gScenes[idx].name.c_str());
      break;
    }
    case SHAKE_HUSH:
      if (!gPlaying) { uiToast("Not playing"); break; }
      listenShakeHush();   // already down: nothing
      break;
    case SHAKE_SUMMON: summonOne(); break;
    case SHAKE_DRIFTNOW:
      if (!appIsDrift()) uiToast("Not a Drift scene");
      else if (gRt.xf) uiToast("Drifting...");
      else { gRt.driftNow(); uiToast("Drift"); }
      break;
    case SHAKE_QUICKSAVE: quickSave(); break;
    case SHAKE_DISPLAY:
      uiDisplayOff();
      break;
    default:
      uiToast("Set a Shake action in SETTINGS");
      break;
  }
}

// DREAD added on top of the scene's own value (Cursed Mode, Listen, Arc)
int appDreadBoost() { return cursedDreadBoost() + listenDreadAdd() + arcDreadAdd(); }

// ---------------------------------------------------------------------------
void appUpdate(float dt) {
  const bool busy = exportBusy();
  appSyncDrift();
  updateSleep(dt);
  partyUpdate(dt);
  poltergeistUpdate(dt);
  arcUpdate(dt);
  gRt.arcBias = arcDriftBias();   // Arc leans Drift's comings and goings (Pool and limits stay)
  // Drift is frozen while exporting (the exporter steps its own copy), at a
  // party and in PLANCHETTE (which writes the tilt into the scene itself)
  if (!busy && !partyActive() && !planchetteActive() && !poltergeistOwnsAudio()) gRt.update(dt);
  planchetteUpdate(dt);
  updateSeedChange(dt);
  listenUpdate(dt);

  static EngineParams p;
  // Fade In (Sound): silence -> scene after power-on and each play start
  if (g_playFade < 1.f) {
    const int fs = FADE_IN_SEC[gSet.fadeInIdx];
    g_playFade = fs ? g_playFade + dt / (float)fs : 1.f;
    if (g_playFade > 1.f) g_playFade = 1.f;
  }
  float v = gSet.volume * 0.01f;
  // v0.12: x1.5 (+3.5 dB vs v0.1). The limiter keeps loud scenes at the v0.1
  // ceiling, so only quiet scenes come up; the speaker never gets more than in v0.1.
  float master = gPlaying ? 1.5f * v * sqrtf(v) * s_mul * g_playFade * g_playFade * g_seedMul : 0.f;
  // Poltergeist: its own scene and volume; the scene, Arc, Listen, Cursed stay out
  if (poltergeistOwnsAudio()) {
    poltergeistParams(p, gSeed, g_resetSeq);
    p.summonSeq = g_summonSeq;
    p.summonLayer = (int8_t)g_summonLayer;
    p.summonSeed = g_summonSeed;
    paramsPublish(p);
    if (!busy) flushDirty();
    return;
  }
  // Effective DREAD = scene + Arc + Listen + Cursed (spec v1.1 6.4), on a copy --
  // the scene itself is untouched
  const SceneData* src = &gRt.live;
  static SceneData boosted;
  int boost = appDreadBoost();
  if (boost) {
    boosted = gRt.live;
    boosted.dread = (uint8_t)constrain((int)boosted.dread + boost, 0, 100);
    src = &boosted;
  }
  buildEngineParams(*src, master, gSeed, g_resetSeq, p);
  // spec v1.1 6.4: the event-rate multiplier = Arc x Listen, capped -- at 2.0
  // since v0.51 (2.5 overloaded the DSP with all layers on)
  p.densMul *= std::min(arcDensMul() * listenDensMul(), arc::DENS_CAP);
  cursedApply(p);
  listenApply(p);
  arcApply(p);   // after Listen: a Hush still wins (it scales the layers)
  partyApply(p); // Party Mode: its music, the scene silent
  p.summonSeq = g_summonSeq;   // Summon / Listen's Response
  p.summonLayer = (int8_t)g_summonLayer;
  p.summonSeed = g_summonSeed;
  paramsPublish(p);

  if (!busy) flushDirty();
}

// --- the splash's heartbeat (v0.98, splash spec 3.4) ---------------------------------
static void publishSplash() {
  SceneData s;
  sceneDefaults(s, "Splash");
  // v0.984: Ritual's heartbeat (its HEART parameters and its DREAD, which sets
  // the tension React answers to); the level stays higher, for the speaker
  s.dread = 45;
  LayerState& H = s.L[L_HEART];
  H.on = true;
  H.level = 75;
  H.p[0] = 35;   // BPM (with React and DREAD: the lub-dub's spacing)
  H.p[1] = 30;   // Irregularity (only the regular beats, which are off here)
  H.p[2] = 50;   // Muffle
  H.p[3] = 30;   // Flow
  H.p[4] = 50;   // React
  H.p[5] = 60;   // Attack: heard on the built-in speaker too
  const float v = gSet.volume * 0.01f;
  const float master = gSet.startupSound ? 1.5f * v * sqrtf(v) : 0.f;
  EngineParams p;   // every field is set below
  buildEngineParams(s, master, gSeed, g_resetSeq, p);
  p.heartSummonOnly = true;
  p.summonSeq = g_summonSeq;
  p.summonLayer = (int8_t)g_summonLayer;
  p.summonSeed = g_summonSeed;
  paramsPublish(p);
}
void appSplashBegin() { g_resetSeq++; publishSplash(); }
void appSplashBeat() { appRequestSummon(L_HEART, 0x5EA7u); publishSplash(); }
void appSplashEnd() { g_resetSeq++; appUpdate(0.f); }
