// MIASMORA — Cursed Mode: opt-in "paranormal" glitches
//
// When enabled (SETTINGS, after the user has read and accepted a warning),
// something strange happens every now and then: the mood changes by itself,
// the screen flickers or goes dark, the sound cuts out with a click, a knock
// or footsteps sneak in, a cryptic message appears...
//
// Ground rules:
//  * never during an export or a live recording
//  * never deletes or overwrites saved scenes / files, never shows fake errors
//  * everything except an occasional mood change reverts by itself
//  * the export path never sees any of it (it builds its own parameters)
#pragma once
#include <stdint.h>

struct EngineParams;

static constexpr int NUM_CURSED_LEVELS = 4;   // Off, Rare, Normal, Often (v0.4; were Low / Mid / High)
extern const char* const CURSED_LEVEL_NAMES[NUM_CURSED_LEVELS];

void cursedUpdate(float dt);           // UI loop
void cursedApply(EngineParams& p);     // live parameters, right before they are published
void cursedStopAll();                  // end whatever is happening, restore everything
int  cursedDreadBoost();               // DREAD creeping up (added on top of the scene's)
bool cursedNameGlitch();               // MAIN: corrupt the mood name
bool cursedVizFrozen();                // MAIN: freeze the visualiser
float cursedEyeOpen();                 // the Watcher's eye, opened for an instant (0..1; v0.6)

// SETTINGS > System > Debug > Cursed Trigger (spec v1.22 2.1): start one
// effect now, even with Cursed Mode off (it is not switched on). Returns
// nullptr, or why it cannot happen now (shown as a toast).
int cursedFxCount();
const char* cursedFxName(int i);
const char* cursedTrigger(int i);
// The last effect that happened (natural or from Debug), for HELP / Debug.
// Returns false if none has since power-on.
bool cursedLast(const char*& name, uint32_t& ms, bool& fromDebug);
