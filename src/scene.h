// MIASMORA — scenes: data, mood presets, random generation, Drift, crossfades,
// the DREAD macro (scene -> engine parameters) and scene files.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "layers/layer_defs.h"
#include "dsp/dsp.h"

struct EngineParams;

struct LayerState {
  uint8_t level;          // 0..100
  bool    on;
  uint8_t p[MAX_P];       // continuous 0..100, or option index
};

struct SceneData {
  char       name[24];
  uint8_t    dread;       // 0..100 (base values below are pre-DREAD)
  LayerState L[NUM_LAYERS];
};

void sceneDefaults(SceneData& s, const char* name);   // every layer off, default params
void sceneSetName(SceneData& s, const char* name);

// --- mood presets ---------------------------------------------------------
extern const int NUM_PRESETS;
const char* presetName(int i);
void presetToScene(int i, SceneData& s);

// --- random scene from a seed (spec 5.1) ------------------------------------
static const char* const RANDOM_SCENE_NAME = "Unknown";
static const char* const DRIFT_NAME = "Drift";
void generateRandomScene(uint32_t seed, SceneData& s, const char* name);

// --- crossfade + Drift runtime ------------------------------------------------
// A plain value type with no hidden globals, so the exporter can step its own
// copy in sample time (Drift "Record") while the UI copy stays frozen.
// Drift Speed (SETTINGS) and the kinds of change a Drift step can make
static constexpr int NUM_DRIFT_SPEEDS = 4;               // Slow, Mid, Fast, Custom (v0.41)
static constexpr uint8_t DRIFT_CUSTOM = 3;
extern const char* const DRIFT_SPEED_NAMES[NUM_DRIFT_SPEEDS];
// DK_MODE (v0.41): a sounding layer's mode changes (SETTINGS > Drift > Mode Change)
enum DriftKind : int { DK_PARAM = 0, DK_LEVEL, DK_DREAD, DK_REMOVE, DK_ADD, DK_SWAP, DK_MODE, DK_NUM, DK_NONE = -1 };
static constexpr uint32_t DRIFT_TOUCH_DREAD = 1u << 31;  // "DREAD already changed in this step"

struct SceneRuntime {
  SceneData live;          // what is playing right now (possibly mid-morph)
  SceneData from, to;
  bool  xf = false;
  float xfT = 1.f, xfDur = 3.f;
  bool  drift = false;
  // Drift settings (SETTINGS > Drift; copied into exports, so Record follows them)
  uint8_t driftSpeed = 1;  // 0 Slow, 1 Mid, 2 Fast, 3 Custom
  uint8_t waitMin = 12, waitMax = 25;   // Custom: seconds between steps
  uint8_t xfSec = 10;                   // Custom: transition
  uint8_t chMin = 1, chMax = 2;         // Custom: changes per step
  uint8_t layersMin = 2, layersMax = 6; // sounding layers Drift keeps between (any speed)
  uint32_t pool = 0xFFFFFFFFu;          // bit i: Drift may touch layer i
  bool  modeChange = false;             // Drift may change a layer's mode
  bool  xfDip = false;                  // this transition dips the level of a layer whose mode changes
  float arcBias = 0.f;                  // Arc (v0.5): -1 Calm (layers rather leave) .. +1 Peak (rather come in)
  float driftTimer = 0.f;
  dsp::Rng driftRng;
  uint32_t driftSteps = 0, driftStats[DK_NUM] = {0};   // counters for the test bench
  bool  driftNow();                    // Shake's Drift Now: the next step at once (false: not now)

  void startXf(const SceneData& target, float durSec);
  void finishXf();                     // jump to the target now
  void driftBegin(uint32_t seed);      // (re)seed the Drift stream from the user seed
  void update(float dt);               // advance morph and Drift
  const SceneData& settled() const { return xf ? to : live; }
};

static constexpr float SCENE_XFADE_SEC = 3.0f;

// --- DREAD + level curves -> engine parameters ------------------------------
void buildEngineParams(const SceneData& s, float master, uint32_t seed, uint32_t resetSeq, EngineParams& p);

// --- scene files (/MIASMORA/scene/*.json) -------------------------------------
void sceneToJson(JsonObject o, const SceneData& s);
void sceneFromJson(JsonObjectConst o, SceneData& s);
String sceneFilePath(const String& name);
bool sceneSaveFile(const SceneData& s);
bool sceneLoadFile(const String& name, SceneData& s);
void sceneDeleteFile(const String& name);
void sceneListFiles(std::vector<String>& names);
