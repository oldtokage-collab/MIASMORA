// MIASMORA — PLANCHETTE (phase 7, spec v1.2 6): tilt the device to feel your
// way through a mood, save it where it feels right. Named after the pointer
// of a ouija board.
//
// The posture when it starts is the centre (C records it again). Pitch and
// roll, past a +-4 degree dead zone, reach the edge at +-Range degrees and are
// followed smoothly (Smoothing). Space holds the values (the device can be
// put down); released, they glide back to the tilt.
//   Axis (v0.8): forward / back  -> DREAD -30..+30 and the event rate about
//                x0.5..x2 (each event layer's Density moved, so a saved
//                scene sounds the same); left / right -> Shade: REVERB's Mix
//                and Damping (left dry and bright, right deep and dark)
// The values are written into the playing scene as it is played, so what is
// saved (Enter / Q: Tilt_NNN) or exported (Shift+X) is what was heard.
// While it lasts: no shake, Drift / Arc / Listen / Cursed / parties hold
// still, the screen does not dim; Shift+S (sleep timer) still works. Esc
// leaves it: the sound stays as it is (the scene is marked edited, not saved).
#pragma once
#include <stdint.h>

bool planchetteActive();
const char* planchetteEnter();     // MAIN P; nullptr, or why not (a toast)
void planchetteExit();             // Esc
void planchetteUpdate(float dt);   // every UI loop
void planchetteRecenter();         // C
void planchetteToggleHold();       // Space
bool planchetteHeld();
struct SceneData;
// Enter / Q: the toast text (name: what it was saved as; out: the scene saved)
const char* planchetteSave(char* name = nullptr, int n = 0, SceneData* out = nullptr);
// for the screen
float planchetteX();               // -1..1 (left .. right)
float planchetteY();               // -1..1 (back .. forward)
int   planchetteDreadAdd();        // what the tilt adds to DREAD now
float planchetteRateMul();         // the event rate multiplier now (about)
float planchetteShade();           // -1..1
// Vector (v0.81)
bool planchetteVector();
void planchetteWeights(float* w);  // the four corners' weights now
const char* planchetteCornerName(int k);
