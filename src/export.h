// MIASMORA — offline WAV loop export (spec 6)
#pragma once
#include <Arduino.h>
#include "scene.h"

enum ExportMode : uint8_t { EXM_CROSSFADE = 0, EXM_STRICT = 1 };

struct ExportConfig {
  ExportMode   mode;
  uint16_t     lenSec;        // 8 / 16 / 32 / 64
  uint8_t      xfSec;         // 1 / 2 / 4 (Crossfade only)
  bool         driftRecord;   // Crossfade + Drift scene only
  bool         normalize;     // peak -> -1 dBFS (two passes)
  uint8_t      stems;         // v0.7: 0 one file, 1 Wet (DRONE, EVENTS), 2 Dry + Reverb (+ REVERB)
  // Batch (v0.71, spec v1.1 8.2): that many files, seed, seed + 1, ... (0: one file)
  uint8_t      batch;
  bool         batchFolder;   // into a folder of its own (/MIASMORA/rec/Batch_<mood>_<seed>_NNN)
  uint8_t      sceneKind;     // 0 the scene as it is, 1 Unknown, 2 Drift (both made from each seed)
  // One-shot (v0.72, spec v1.1 8.3): one event of one layer per file
  bool         oneshot;
  uint8_t      osLayer;       // LayerId: an event layer, or L_HEART
  LayerState   osState;       // its parameters (the scene's, or the defaults), the Mode chosen
  uint8_t      osUnit;        // 0 Single, 1 Gesture
  uint8_t      osCount;       // files
  bool         osWet;         // through a reverb (the scene's REVERB settings, or the defaults)
  float        osRevMix, osRevSize, osRevDamp;
  String       osName;        // "KNOCK_Tap" / "KNOCK_Tap_G" (file names)
  uint32_t     seed;
  SceneRuntime rt;            // snapshot of the scene at export start
  String       moodTag;       // file-name safe mood name
  int8_t       arcDread;      // EXPORT > Arc: Apply -- Arc at the export's start: DREAD added ...
  float        arcDens;       // ... and the event-rate multiplier (Ignore / Arc off: 0 and 1)
  String       arcTag;        // Apply: "_ARCcm" / "_ARCbd" / "_ARCpk" / "_ARCrl" in the file name
};

enum ExportState : uint8_t { EXS_IDLE = 0, EXS_RUNNING, EXS_DONE, EXS_FAILED, EXS_CANCELLED };

// Call only after audioPause() succeeded. Runs in its own task.
bool        exportBegin(const ExportConfig& cfg);
void        exportCancel();
// what the files will take (all of a batch), to compare with the free space first
uint64_t    exportEstimateBytes(const ExportConfig& cfg);
// Batch: which file (0..) of how many is being written
int         exportBatchIndex();
int         exportBatchCount();
ExportState exportState();
float       exportProgress();     // 0..1
const char* exportStage();
String      exportResult();       // saved path, or the error message
void        exportAcknowledge();  // back to idle (UI, after showing the result)
bool        exportBusy();         // running (UI must not touch the SD card)

static constexpr float EXPORT_GAIN = 0.72f;   // fixed master for files (independent of the volume knob)
