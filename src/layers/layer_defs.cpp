#include "layer_defs.h"
#include <string.h>

const char* const GROUP_NAMES[NUM_GROUPS] = {"DRONE", "EVENTS", "SPACE"};

static const char* const O_INTERVAL[] = {"Unison", "Minor 2nd", "Tritone", "Cluster"};
static const char* const O_NOISE[]    = {"Pink", "Brown"};
static const char* const O_RATIO[]    = {"1:1.41", "1:2.76", "1:3.51", "1:1.73", "1:0.59"};
static const char* const O_STATIC[]   = {"TV", "Radio", "Tuning", "Proximity"};   // options are only ever appended
static const char* const O_DRIP[]     = {"Random", "Leak"};
static const char* const O_SURFACE[]  = {"Wood", "Concrete", "Metal", "Gravel"};
static const char* const O_DIR[]      = {"Approach", "Recede", "Pass"};
static const char* const O_INSECT[]   = {"Fly", "Mosquito", "Cicada", "Wrong Chirp"};
static const char* const O_DRAG[]     = {"Body", "Pipe"};
static const char* const O_MACHINE[]  = {"Motor", "Engine"};
static const char* const O_CLANG[]    = {"Pipe", "Sheet"};
static const char* const O_KNOCK[]    = {"Knock", "Tap"};
static const char* const O_CHORD[]    = {"MinMaj", "Dim b9", "Poly", "Cluster", "Tritone"};
static const char* const O_TUNING[]   = {"Quarter", "Sixth", "BP", "Beating", "Stretched"};
static const char* const O_WAVE[]     = {"Sine", "Saw"};
static const char* const O_VOX[]      = {"Choir", "Hum", "Moan"};
static const char* const O_DIRSH[]    = {"Down", "Up"};
static const char* const O_SPACING[]  = {"Octave", "Tritone"};

const float METAL_RATIOS[NUM_METAL_RATIOS] = {1.41f, 2.76f, 3.51f, 1.73f, 0.59f};

#define C(n, d)        {n, 0, nullptr, d}
#define E(n, o, d)     {n, (uint8_t)(sizeof(o) / sizeof(o[0])), o, d}

const LayerDef LAYER_DEFS[NUM_LAYERS] = {
  // name       group     stream  level    defLv n  params
  {"SUB",     G_DRONE,  0x0101, "Level", 50, 3, {C("Pitch", 40), C("Drive", 30), C("Drift", 40)}},
  {"CLUSTER", G_DRONE,  0x0102, "Level", 45, 3, {E("Interval", O_INTERVAL, IV_MIN2), C("Detune", 35), C("Darkness", 60)}},
  {"AIR",     G_DRONE,  0x0103, "Level", 45, 3, {C("Color", 40), C("Motion", 40), E("Noise", O_NOISE, NZ_PINK)}},
  {"METAL",   G_DRONE,  0x0104, "Level", 35, 3, {E("Ratio", O_RATIO, 0), C("Index", 40), C("Motion", 40)}},
  {"STATIC",  G_DRONE,  0x0105, "Level", 45, 3, {E("Mode", O_STATIC, ST_TV), C("Tone", 50), C("Instability", 40)}},
  {"MACHINE", G_DRONE,  0x0106, "Level", 40, 3, {E("Mode", O_MACHINE, MC_MOTOR), C("Speed", 40), C("Load", 30)}},

  {"PULSE",   G_EVENTS, 0x0201, "Level", 45, 2, {C("Density", 30), C("Weight", 50)}},
  {"CREAK",   G_EVENTS, 0x0202, "Level", 45, 2, {C("Density", 30), C("Pitch", 50)}},
  {"SWELL",   G_EVENTS, 0x0203, "Level", 45, 2, {C("Density", 25), C("Length", 50)}},
  {"DRIP",    G_EVENTS, 0x0204, "Level", 45, 3, {C("Density", 35), C("Size", 50), E("Mode", O_DRIP, DR_RANDOM)}},
  {"SCRAPE",  G_EVENTS, 0x0205, "Level", 45, 3, {C("Density", 25), C("Material", 50), C("Pressure", 50)}},
  {"DRAG",    G_EVENTS, 0x0206, "Level", 45, 4, {C("Density", 20), C("Weight", 50), C("Roughness", 50),
                                                 E("Object", O_DRAG, DG_BODY)}},
  {"STEPS",   G_EVENTS, 0x0207, "Level", 45, 5, {C("Density", 25), E("Surface", O_SURFACE, SF_WOOD),
                                                 E("Direction", O_DIR, DIR_PASS), C("Pace", 50), C("Muffle", 30)}},
  {"SKITTER", G_EVENTS, 0x0208, "Level", 45, 3, {C("Density", 30), C("Size", 40), C("Swarm", 30)}},
  {"INSECT",  G_EVENTS, 0x0209, "Level", 40, 3, {C("Density", 30), E("Mode", O_INSECT, IN_FLY), C("Count", 30)}},
  {"KNOCK",   G_EVENTS, 0x020A, "Level", 45, 3, {C("Density", 25), E("Style", O_KNOCK, KN_KNOCK), C("Distance", 40)}},
  {"DRILL",   G_EVENTS, 0x020B, "Level", 40, 3, {C("Density", 20), C("Pitch", 50), C("Grind", 50)}},
  {"CLANG",   G_EVENTS, 0x020C, "Level", 40, 4, {C("Density", 20), E("Object", O_CLANG, CL_PIPE), C("Size", 50), C("Distance", 40)}},
  {"BUMP",    G_EVENTS, 0x020D, "Level", 45, 3, {C("Density", 30), C("Pitch", 40), C("Cluster", 40)}},

  // v0.31 (spec v1.2 3): DRONE layers, appended after the events
  {"DISCORD", G_DRONE,  0x0107, "Level", 40, 5, {E("Chord", O_CHORD, CH_MINMAJ), C("Pitch", 35), C("Detune", 30),
                                                 C("Darkness", 55), C("Slide", 30)}},
  {"MICRO",   G_DRONE,  0x0108, "Level", 40, 5, {E("Tuning", O_TUNING, TU_QUARTER), C("Pitch", 40), C("Spread", 40),
                                                 C("Glide", 40), E("Wave", O_WAVE, MW_SINE)}},
  {"VOX",     G_DRONE,  0x0109, "Level", 40, 5, {E("Mode", O_VOX, VX_CHOIR), C("Pitch", 35), C("Vowel", 50),
                                                 C("Motion", 35), C("Breath", 30)}},
  {"SHEPARD", G_DRONE,  0x010A, "Level", 40, 4, {E("Direction", O_DIRSH, SH_DOWN), C("Speed", 50),
                                                 E("Spacing", O_SPACING, SP_OCTAVE), C("Tone", 45)}},
  {"HEART",   G_DRONE,  0x010B, "Level", 50, 6, {C("BPM", 30), C("Irregularity", 30), C("Muffle", 50),
                                                 C("Flow", 30), C("React", 50), C("Attack", 50)}},   // Attack: v0.37

  {"REVERB",  G_SPACE,  0x0301, "Mix",   40, 2, {C("Size", 60), C("Damping", 50)}},
};

int findLayerByName(const char* name) {
  for (int i = 0; i < NUM_LAYERS; i++) if (strcmp(LAYER_DEFS[i].name, name) == 0) return i;
  return -1;
}
