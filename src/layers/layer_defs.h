// MIASMORA — layer catalogue (names, groups, parameters)
//
// Compatibility rules:
//  * Scene files store layers by NAME, so the in-memory order below may grow.
//  * New layers are only ever appended at the end of their group.
//  * Every layer has a fixed `stream` id that feeds its private random stream.
//    Stream ids are never reused or renumbered: changing one would change the
//    sound of every saved seed.
#pragma once
#include <stdint.h>

enum LayerId : uint8_t {
  // DRONE
  L_SUB = 0, L_CLUSTER, L_AIR, L_METAL, L_STATIC,
  L_MACHINE,                                 // v0.14
  // EVENTS
  L_PULSE, L_CREAK, L_SWELL, L_DRIP, L_SCRAPE, L_DRAG, L_STEPS, L_SKITTER, L_INSECT,
  L_KNOCK,                                   // v0.13
  L_DRILL, L_CLANG,                          // v0.14
  L_BUMP,                                    // v0.15
  // DRONE again (v0.31, spec v1.2 3): appended after the last event layer;
  // the EDIT screen shows them at the end of the DRONE group
  L_DISCORD, L_MICRO, L_VOX, L_SHEPARD, L_HEART,
  // SPACE
  L_REVERB,
  NUM_LAYERS
};

enum LayerGroup : uint8_t { G_DRONE = 0, G_EVENTS, G_SPACE, NUM_GROUPS };
extern const char* const GROUP_NAMES[NUM_GROUPS];

static constexpr int MAX_P = 6;   // parameters per layer, excluding Level (v0.37: 5 -> 6, HEART's Attack)

struct ParamDef {
  const char* name;
  uint8_t nOpts;                 // 0 = continuous 0..100, otherwise an option index
  const char* const* opts;
  uint8_t def;                   // default value (0..100 or option index)
};

struct LayerDef {
  const char* name;
  LayerGroup group;
  uint16_t stream;               // fixed random-stream id (see above)
  const char* levelName;         // "Level", or "Mix" for the reverb
  uint8_t defLevel;
  uint8_t nParams;
  ParamDef p[MAX_P];
};

extern const LayerDef LAYER_DEFS[NUM_LAYERS];

static inline bool layerIsEvent(int i) { return LAYER_DEFS[i].group == G_EVENTS; }
static inline bool layerIsDrone(int i) { return LAYER_DEFS[i].group == G_DRONE; }
// "Keep at least one drone" (Drift, random scenes): HEART is in the DRONE
// group but does not count -- a heartbeat alone is not a bed (spec v1.2 3.9)
static inline bool layerIsBed(int i) { return layerIsDrone(i) && i != L_HEART; }
int findLayerByName(const char* name);

// Option indices used by the DSP code
enum { IV_UNISON = 0, IV_MIN2, IV_TRITONE, IV_CLUSTER };
enum { NZ_PINK = 0, NZ_BROWN };
enum { ST_TV = 0, ST_RADIO, ST_TUNING, ST_PROXIMITY };
enum { DR_RANDOM = 0, DR_LEAK };
enum { SF_WOOD = 0, SF_CONCRETE, SF_METAL, SF_GRAVEL };
enum { DIR_APPROACH = 0, DIR_RECEDE, DIR_PASS };
enum { IN_FLY = 0, IN_MOSQUITO, IN_CICADA, IN_WRONG };
enum { DG_BODY = 0, DG_PIPE };
enum { MC_MOTOR = 0, MC_ENGINE };
enum { CL_PIPE = 0, CL_SHEET };        // v0.16: Chain removed
enum { KN_KNOCK = 0, KN_TAP };          // v0.16: Pound removed
enum { CH_MINMAJ = 0, CH_DIMB9, CH_POLY, CH_CLUSTER, CH_TRITONE };     // DISCORD Chord
enum { TU_QUARTER = 0, TU_SIXTH, TU_BP, TU_BEATING, TU_STRETCHED };    // MICRO Tuning
enum { MW_SINE = 0, MW_SAW };                                           // MICRO Wave
enum { VX_CHOIR = 0, VX_HUM, VX_MOAN };                                 // VOX Mode
enum { SH_DOWN = 0, SH_UP };                                            // SHEPARD Direction
enum { SP_OCTAVE = 0, SP_TRITONE };                                     // SHEPARD Spacing
static constexpr int NUM_METAL_RATIOS = 5;
extern const float METAL_RATIOS[NUM_METAL_RATIOS];

// Fixed stream ids for non-layer consumers
enum : uint32_t {
  STREAM_SCENE  = 0x5C3E,   // random scene generation
  STREAM_DRIFT  = 0xD71F,   // drift timing and mutations
  STREAM_DITHER = 0xD17E,   // export dither
};
