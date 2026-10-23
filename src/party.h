// MIASMORA — Party Mode (hidden; spec v1.23 3). Not in README or HELP.
//
// The opposite of everything else here: a dance track (party_synth.*) with a
// heartbeat for its kick, colourful LED and screen. Started with Fn+P on MAIN
// (and, from v0.66 on, as a very rare Cursed Mode event). The flow:
//   fade      the scene fades out (0.5 s)                      [Cursed only]
//   silent    1.5 s of nothing                                  [Cursed only]
//   heart     the kick alone, from a muffled heartbeat to a kick, quiet to full
//             (10 s; Fn+P: 4 s, while the scene fades)
//   party     everything comes in at once: LET'S PARTY!          (Fn+P: until Esc)
//   cut       Esc: an instant cut with a click, screen and LED dark
//   dark      2.5 s of silence and a black screen
//   back      3 s: the scene comes back from its seed, MAIN again
// While it lasts, Drift, Arc, Listen and Cursed hold still; Space and a shake
// put in a one-bar fill; REC works, exports do not; the screen does not dim.
#pragma once
#include <stdint.h>

struct EngineParams;

enum PartyStage : uint8_t { PS_OFF = 0, PS_FADE, PS_SILENT, PS_HEART, PS_PARTY, PS_CUT, PS_DARK, PS_BACK };

// the settings one can change at a party (SETTINGS has none: they live here)
enum PartyItem : uint8_t { PI_TEMPO = 0, PI_BEAT, PI_KICK, PI_PUNCH, PI_HAT, PI_BASS, PI_CHORD, PI_KEY, PI_SWING, NUM_PARTY_ITEMS };

void partyUpdate(float dt);              // every UI loop (after appUpdate's sleep timer)
void partyApply(EngineParams& p);        // the live parameters: the party's music, the scene's level
const char* partyStartManual();          // Fn+P; nullptr, or why not (a toast)
// Cursed Mode (v0.66, spec 3.2): why it cannot happen now (nullptr: it can).
// debug = from Debug > Cursed Trigger: the time since power-on, the once per
// power-on and Cursed being on do not count
// anyScreen: MAIN need not be on screen yet (Debug: the party is armed)
const char* partyCursedBlocked(bool debug, bool anyScreen = false);
void partyStartCursed();                 // the whole flow, with the defaults, ending in the reveal
// Debug > Cursed Trigger (v0.67): the trigger is pressed on the Debug page, so
// the party is armed and starts once MAIN is back on screen (within 30 s;
// the conditions are checked again then)
void partyArmDebug();
bool partyRevealActive();                // "You found the Secret Party!" -- until a key
void partyRevealClose();
// v0.99: Poltergeist's When Found > Party stays locked ("???") until a party
// has happened once (any way: Cursed, Fn+P, Debug). The flag lives in its own
// file, /MIASMORA/unlocks.txt, so Reset Settings leaves it alone.
void partyLoadUnlock();                 // at start-up, before the settings are read
bool partyUnlocked();
bool partyRevealUnlockLine();           // the reveal also says "Party unlocked" (its first time)
void partyEnd();                         // Esc: the cut
void partyFill();                        // Space / a shake

bool partyActive();                      // anything from the start to the scene being back
PartyStage partyStage();
float partyStageTime();                  // seconds in the stage
bool partyManual();
uint32_t partyBeats();                   // quarter notes (the engine's count)
float partyBeatPhase();

const char* partyItemName(int i);
void partyItemValue(int i, char* out, int n);
void partyItemChange(int i, int d);      // d = -1 / +1
