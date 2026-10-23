// MIASMORA — application state and actions shared by the UI screens
#pragma once
#include <Arduino.h>
#include <vector>
#include "scene.h"

// Shake actions (spec v1.22 1.1): only ever appended (settings files)
enum ShakeAction : uint8_t { SHAKE_OFF = 0, SHAKE_RANDOM, SHAKE_SLEEP, SHAKE_DISPLAY,
                             SHAKE_NEWSEED, SHAKE_NEXTMOOD, SHAKE_HUSH, SHAKE_SUMMON, SHAKE_DRIFTNOW, SHAKE_QUICKSAVE,
                             NUM_SHAKE_ACTIONS };
extern const char* const SHAKE_ACTION_NAMES[NUM_SHAKE_ACTIONS];

// SETTINGS choices (v0.4, spec v1.2 4.2)
static constexpr int NUM_FADE_IN = 5;
extern const uint8_t FADE_IN_SEC[NUM_FADE_IN];            // 0 (off), 2, 5, 10, 30
static constexpr int NUM_DIM_AFTER = 6;
extern const uint16_t DIM_AFTER_SEC[NUM_DIM_AFTER];       // 0 (off), 30 s, 1, 2, 5, 10 min
static constexpr int NUM_AUTO_OFF = 6;
extern const uint16_t AUTO_OFF_SEC[NUM_AUTO_OFF];         // 0 (off), 1, 2, 5, 10, 30 min
static constexpr int NUM_SHAKE_SENS = 3;
extern const uint8_t SHAKE_SENS_VALUES[NUM_SHAKE_SENS];   // Low / Mid / High = 25 / 50 / 75
enum StartupMode : uint8_t { START_STOP = 0, START_RESUME };
enum SleepAfter : uint8_t { AFTER_DEEP_SLEEP = 0, AFTER_DISPLAY_OFF };
enum HushDepth : uint8_t { HUSH_LIGHT = 0, HUSH_MID, HUSH_DEEP, NUM_HUSH_DEPTHS };
// MAIN's visualiser (v0.42, spec v1.22 4): only ever appended (settings files)
enum VizKind : uint8_t { VIZ_OFF = 0, VIZ_BARS, VIZ_SCOPE, VIZ_MONITOR, VIZ_LAYERS, VIZ_INK, VIZ_SPECTRUM, NUM_VIZ };
extern const char* const VIZ_NAMES[NUM_VIZ];
static constexpr int NUM_LOOP_LENS = 4;
extern const uint8_t LOOP_LENS[NUM_LOOP_LENS];            // 8, 16, 32, 64 s
static constexpr int NUM_BATCH = 5;
extern const uint8_t BATCH_COUNTS[NUM_BATCH];             // 0 (off), 5, 10, 20, 50
static constexpr int NUM_ONESHOT_COUNTS = 5;
extern const uint8_t ONESHOT_COUNTS[NUM_ONESHOT_COUNTS];  // 1, 5, 10, 20, 50
static constexpr int NUM_XF_LENS = 3;
extern const uint8_t XF_LENS[NUM_XF_LENS];                // 1, 2, 4 s

struct AppSettings {
  // Sound
  uint8_t volume;        // 0..100
  bool    seedLock;
  uint8_t startup;       // StartupMode
  uint8_t fadeInIdx;     // index into FADE_IN_SEC
  bool    startupSound;  // the splash's heartbeat (v0.98)
  // Drift
  uint8_t driftSpeed;    // 0 Slow, 1 Mid, 2 Fast, 3 Custom
  uint8_t dWaitMin, dWaitMax;   // Custom: 3..120 s between steps
  uint8_t dXf;                  // Custom: transition 2..30 s
  uint8_t dChMin, dChMax;       // Custom: changes per step 1..3
  uint8_t dLayMin, dLayMax;     // sounding layers 1..8 (any speed)
  uint32_t dPool;               // bit i: Drift may touch layer i (Drift Pool)
  bool    dModeChange;          // Drift may change a layer's mode
  // Sleep
  bool    sleepOn;
  uint8_t sleepMin;      // 1..240
  bool    sleepFade;
  uint8_t sleepFadeSec;  // 10..120
  uint8_t sleepAfter;    // SleepAfter
  // Display
  uint8_t brightness;    // 10..100
  uint8_t dimIdx;        // index into DIM_AFTER_SEC
  uint8_t dimLevel;      // 10..50 % of Brightness
  uint8_t autoOffIdx;    // index into AUTO_OFF_SEC
  bool    wakeAct;       // false: the first key only wakes the display; true: it also acts
  uint8_t viz;           // VizKind (up to v0.41: 0 off, 1 on = Bars)
  bool    led;           // the RGB LED (Watcher, party)
  uint8_t ledBright;     // 10..100 %
  // Motion
  uint8_t shakeAction;   // ShakeAction
  uint8_t shakeSens;     // 25 / 50 / 75 (Low / Mid / High)
  uint8_t plRange;       // PLANCHETTE: the tilt that reaches the edge, 20..45 degrees
  uint8_t plSmooth;      // PLANCHETTE: Smoothing 0 Low, 1 Mid, 2 High
  uint8_t plMode;        // PLANCHETTE: 0 Axis, 1 Vector
  char    plCorner[4][24];   // PLANCHETTE Vector: the corner scenes (by name)
  // Listen
  bool    listen;        // Listen mode (turned on only after consent)
  uint8_t listenSens;    // 0 Low, 1 Mid, 2 High
  uint8_t hushDepth;     // HushDepth
  bool    response;      // the lone event before the sound comes back
  bool    watcher;       // the eye on a Hush (Listen's and the shake's)
  // Arc (phase 4)
  bool    arc;           // kept over power-off; the progress is not (it starts at Calm)
  uint8_t arcLenIdx;     // index into ARC_LEN_MIN (10 / 20 / 30 min)
  uint8_t arcCycle;      // ArcCycle: Once / Loop
  bool    arcFollowSleep;   // Once + the sleep timer: Arc ends when the timer does
  // Cursed
  uint8_t cursed;        // Cursed Mode: 0 off, else the frequency 1..3 (kept until turned off)
  uint8_t cursedFreq;    // 1 Rare, 2 Normal, 3 Often (remembered while it is off)
  // System > Debug
  bool    listenLog;     // /MIASMORA/listen_log.csv
  bool    markKeys;      // Shift+L / Shift+K
  // Party Mode (hidden): one's own party's settings, kept for the next one
  uint8_t pTempo, pBeat, pKick, pPunch, pHat, pBass, pChord, pKey, pSwing;
  // Poltergeist (phase 8): the event slots (pgLayerAt index, 0 = None) and their modes
  uint8_t pgEv[3], pgMode[3];
  uint8_t pgTiming;      // 0 Fixed, 1 Random
  uint8_t pgFixIdx, pgMinIdx, pgMaxIdx;   // into PG_INTERVALS
  bool    pgEscalate;
  uint8_t pgDelayIdx;    // into PG_DELAY_MIN
  uint8_t pgStopIdx;     // into PG_STOP_MIN (Auto Stop: never off)
  uint8_t pgVolume;      // 0..100, within Volume
  uint8_t pgRoom;        // 0 Dry, 1 Small, 2 Large
  bool    pgShy;
  uint8_t pgFound;       // 0 Party, 1 Reveal, 2 Silent
  char    pgLast[56];    // "Last Poltergeist: ..." for the next start (after Auto Stop)
  // EXPORT
  bool    xNormalize;    // normalise the file's peak to -1 dBFS
  // EXPORT screen choices (remembered)
  uint8_t xMode;         // 0 Crossfade, 1 Strict
  uint8_t xLenIdx;
  uint8_t xXfIdx;
  bool    xNewSeed;
  bool    xDriftRecord;
  bool    xArcApply;     // Arc: Ignore (the scene itself, reproducible) / Apply (Arc as it stands)
  uint8_t xStems;        // Stems: 0 Off, 1 Wet, 2 Dry + Reverb
  uint8_t xBatchIdx;     // Batch: 0 Off, else BATCH_COUNTS[i]
  bool    xBatchFolder;  // a folder per batch
  bool    xBatchSleep;   // Sleep After: then as SETTINGS > Sleep > After
  // EXPORT > One-shot (remembered)
  uint8_t oLayerIdx;     // into the one-shot layer list (the event layers, then HEART)
  uint8_t oMode;         // the layer's mode (its first option parameter)
  uint8_t oUnit;         // 0 Single, 1 Gesture
  uint8_t oCountIdx;     // into ONESHOT_COUNTS
  bool    oWet, oNorm;
};

enum SceneKind : uint8_t { SK_PRESET = 0, SK_DRIFT, SK_CUSTOM };
struct SceneEntry { String name; SceneKind kind; int presetIdx; };

extern AppSettings gSet;
extern SceneRuntime gRt;                 // the playing scene (UI-owned)
extern std::vector<SceneEntry> gScenes;  // presets, Drift, custom scenes
extern int  gCurScene;                   // index into gScenes, -1 = a generated "Unknown" scene
extern bool gEdited;                     // edited since it was loaded (presets: unsaved)
extern bool gPlaying;
extern uint32_t gSeed;                   // 0..999999
extern bool gImuReady;
extern const char* gResetReason;          // why the device last restarted (HELP, v0.32)

void appInit();                          // after storageInit(): load settings + scenes
void appUpdate(float dt);                // every UI loop
void appTogglePlay();
void appResetSettings();                 // SETTINGS > System > Reset Settings (scenes and WAVs stay)
int  appShakeSensIdx();                  // 0..2 from gSet.shakeSens
void appSetSeed(uint32_t seed);          // typed seed: restart, regenerate Unknown / Drift
void appNewRandomSeed();                 // fresh seed (no restart)
void appRandomScene();                   // R
void appSelectScene(int idx);
void appSettleEdit();                    // call before editing gRt.live directly
void appSceneEdited();                   // after editing gRt.live
void appSettingsChanged();
void appSyncDrift();                     // SETTINGS > Drift -> gRt
bool appSaveSceneAs(const String& name);
bool appDeleteScene(int idx);
String appMoodTag();                     // file-name safe mood name
const char* appMoodName();
bool appIsDrift();
int  appDreadBoost();                    // Cursed + Listen + Arc DREAD on top of the scene

// sleep timer
void appSleepArm();
int  appSleepRemainingSec();             // -1 = not armed
bool appSleepHeld();                     // due, but waiting for export / recording
bool appSleepFading();
int  appSleepFadeSec();                  // the fade the timer ends with (Fade off: 1 s)
void appSleepNow();                      // end now as the timer does (fade, then After) -- a batch's Sleep After

// shake / Fn+S: run a ShakeAction (Sleep Timer also for Shift+S)
void appShakeRun(uint8_t action);
// Summon / Listen's Response: one event of layer `layer` now (its own stream `seed`)
void appRequestSummon(int layer, uint32_t seed);
// The playing scene saved as "<prefix>_NNN" (Shake_ / Tilt_); the current scene
// stays what it is. Returns the toast text; name (if given) gets the name.
const char* appSnapshot(const char* prefix, char* name = nullptr, int n = 0, const SceneData* scene = nullptr);
void appMarkEdited();                    // the playing scene differs from its file / preset (not saved)
// The splash (v0.98): HEART alone (only the beats asked for), at the Volume;
// silent with Startup Sound off. appSplashBeat(): one "lub-dub" now.
// appSplashEnd(): back to the scene (the engine restarts from the seed).
void appSplashBegin();
void appSplashBeat();
void appSplashEnd();
void appRestartScene();                  // the engine starts again from the seed (Party Mode, Poltergeist)
// A new seed with a dip in the volume (Shake's New Seed, typed seeds): -20 dB
// over ~1 s, the engine restarts on the new seed, back up over ~1.5 s
void appChangeSeedSmooth(uint32_t seed);


// display hooks implemented by the UI
void uiDisplayOff();
void uiToast(const char* msg);
