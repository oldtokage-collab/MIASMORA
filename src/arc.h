// MIASMORA — Arc (phase 4, spec v1.1 6 / v1.2 5.1)
//
// Over 10 / 20 / 30 minutes the playing mood goes from calm through a build-up
// to a peak with one climax, then settles again. Arc lays DREAD, the event
// rate, HEART's tension and Drift's comings and goings over whatever plays;
// the scene itself is never changed.
#pragma once
#include <Arduino.h>
#include "audio.h"

static constexpr int NUM_ARC_LENS = 3;
extern const uint8_t ARC_LEN_MIN[NUM_ARC_LENS];   // 10, 20, 30
enum ArcCycle : uint8_t { ARC_ONCE = 0, ARC_LOOP };
extern const char* const ARC_SECTION_NAMES[4];    // Calm, Build, Peak, Release

void  arcUpdate(float dt);       // every UI loop, before the engine parameters are built
void  arcRestart();              // from Calm again (play starts, Arc switched on)

// what the live parameters get (all 0 / 1 while Arc is off; faded in and out)
int   arcDreadAdd();             // -20..+30 (+ the climax)
float arcDensMul();              // 0.5..1.8 (x the climax)
float arcDriftBias();            // -1 (Calm) .. +1 (Peak)
void  arcApply(EngineParams& p); // tension, climax, the silence after it; after listenApply()

// EXPORT (spec v1.1 6.5): the values at this moment, without the climax
int   arcExportDreadAdd();
float arcExportDensMul();

// MAIN / HELP
bool  arcShown();                // Arc is on
float arcProgress();             // 0..1
int   arcSection();              // arc::Section
bool  arcFinished();             // Once: done, it stays Calm
bool  arcClimaxRunning();
bool  arcFollowing();            // Arc Follows Sleep is fitting it to the timer now
