// MIASMORA — Poltergeist mode (phase 8, spec v1.24): hide the device and let
// it knock, drip, scrape... now and then, with the screen and LED dark. A
// prank, on someone who will laugh about it afterwards; Auto Stop is always on.
//
// The flow: confirm -> countdown (Start Delay; the screen goes dark for the
// last 10 s; moving the device is expected) -> waiting until it lies still
// (5 s) -> lurking: events at Fixed / Random times (Escalate: the gaps shrink
// to 40 % by the end; Random: now and then 2-3 in a row), each one summoned
// from its layer (a gesture layer plays its gesture; HEART 4..8 beats).
// Lifted (tilted or shaken for 0.5 s; checked every 0.2 s) = found: Party,
// Reveal (Watcher's eyes, "You found me.", the report) or Silent (the
// report). It also ends with Auto Stop, Esc held 2 s, G0, or the battery under
// 10 % -- the report is shown and a line goes to /MIASMORA/poltergeist_log.txt
// (with the battery's % and voltage at the start and the end). After Auto
// Stop / low battery: as the sleep timer ends (Sleep > After); the next start
// shows "Last Poltergeist: ..." as a toast.
// Shy (v0.93): with the mic, a sound in the room (a voice, steps, a cough; not
// its own sounds) keeps it quiet for 30 s .. 2 min. Power (v0.93): between
// events nothing is synthesised, the codec's output is off and the CPU runs at
// 80 MHz; with Shy off the audio stops altogether and the CPU light-sleeps.
#pragma once
#include <stdint.h>

struct EngineParams;

enum PgStage : uint8_t { PG_OFF = 0, PG_TEST, PG_COUNTDOWN, PG_SETTLE, PG_LURK, PG_REVEAL, PG_REPORT };
enum PgEnd : uint8_t { PGE_AUTO = 0, PGE_FOUND, PGE_MANUAL, PGE_BATTERY };

static constexpr int PG_NUM_INTERVALS = 19;
extern const uint16_t PG_INTERVALS[PG_NUM_INTERVALS];   // 10 s .. 30 min
static constexpr int PG_NUM_DELAYS = 5;
extern const uint8_t PG_DELAY_MIN[PG_NUM_DELAYS];       // 1, 2, 3, 5, 10
static constexpr int PG_NUM_STOPS = 5;
extern const uint16_t PG_STOP_MIN[PG_NUM_STOPS];        // 30, 60, 120, 180, 240 (v0.97: the 1 min test is gone)

PgStage poltergeistStage();
bool poltergeistActive();          // countdown, waiting, lurking: the device is "in the mode"
bool poltergeistOwnsAudio();       // test, countdown, waiting, lurking: its sounds, not the scene's
bool poltergeistLurking();         // keys do nothing (but Esc held 2 s), the display stays dark
float poltergeistStageTime();
int poltergeistCountdownLeft();    // seconds

const char* poltergeistTest();     // one of the chosen events now
const char* poltergeistCanStart(); // nullptr, or why not
void poltergeistStart();           // after the confirmation
void poltergeistCancel();          // Esc during the countdown
void poltergeistStop(PgEnd why);   // Esc held 2 s, G0
void poltergeistUpdate(float dt);
// the engine's parameters while it owns the audio
void poltergeistParams(EngineParams& p, uint32_t seed, uint32_t resetSeq);
void poltergeistCloseScreen();     // a key on the reveal / report: back to MAIN
bool poltergeistRevealing();       // the eyes are forced open (Watcher)
void poltergeistReport(char lines[5][40]);
const char* poltergeistBootToast(); // "Last Poltergeist: ..." once, or nullptr
// Shy (v0.93): Listen's judgement runs for it while it lurks; a sound in the
// room keeps it quiet for 30 s .. 2 min (longer if it goes on)
bool poltergeistShyListening();
// v0.986: the codec's output was just switched on or off (its own click):
// Shy does not judge for 1.2 s
bool poltergeistShyDeaf();
void poltergeistHeard();
// light sleep between events with Shy off (ui.cpp, at the end of a loop)
void poltergeistIdleSleep();

// the event slots' layer list (None, the event layers, HEART) and the mode
int  pgLayerCount();
int  pgLayerAt(int idx);           // -1 = None
int  pgModeParam(int layer);       // the layer's first option parameter, or -1
