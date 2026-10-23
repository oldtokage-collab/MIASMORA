// MIASMORA — Listen mode (spec v1.1 5, judging by method A of spec v1.21 3)
//
// The microphone measures how loud the room is (levels only; nothing is
// recorded or stored). A room where nothing happens slowly raises Attention
// (more events, more DREAD); a sudden sound in the room (a cough, a clap, a
// knock, the start of a voice) makes everything Hush.
//
// Method A (v0.33). Until v0.32 Listen learned how much of our own sound
// reaches the mic and judged the level left over. On the Cardputer speaker
// and mic sit a few cm apart, the mic hears mostly us, and that share changed
// with the layers, the volume and the room: no setting held everywhere. Now
// only sudden changes count:
//   * Every ~23 ms window: rise = mic level - median of the ~0.3 s before
//     (full band and 1..4 kHz band, the larger counts).
//   * A rise of 12 / 9 / 6 dB (Low / Mid / High) is a sound in the room when
//     the mic is also 25 / 20 / 15 dB over the room floor, our own output did not rise
//     6 dB where that sound was written (the output reaches the mic a fixed
//     lag later, ~70 ms), no onset of ours (the engine reports every event,
//     beat and burst) was heard from 20 ms before to 250 ms after, and no key
//     was pressed and the device was not moved in the last second. A rise
//     15 dB bigger than what our output did there is still the room (v0.34).
//   * Just over the bar it has to hold for two windows; 3 dB over, one does
//     (6 dB over while the mic is less than 20 dB above the room floor).
//   * A Hush starts 120 ms after it is decided, and is called off if a key
//     press or a movement of the device is noted meanwhile (v0.36).
//   * Slow swells and a voice that goes on are not sudden: no Hush for them.
//   * While the sound comes back after a Hush another one needs 6 dB more;
//     after four Hushes within a minute (a TV, talking) Listen rests for 20 s.
//   * Attention rises while no sound in the room comes near the bar.
// Everything runs on the UI thread (appUpdate), which also owns the I2C bus.
#pragma once
#include <stdint.h>

struct EngineParams;

static constexpr int NUM_LISTEN_SENS = 3;
extern const char* const LISTEN_SENS_NAMES[NUM_LISTEN_SENS];   // Low / Mid / High

void listenUpdate(float dt);            // every UI loop, before the parameters are built
int  listenDreadAdd();                  // 0..20
float listenDensMul();                  // 1..2
void listenApply(EngineParams& p);      // Hush + the "noticed" event; after cursedApply

// Turning Listen on (after consent): switches the mic on. Returns false if
// the microphone is not available (Listen then stays off).
bool listenEnable(bool on);
bool listenAvailable();                 // false once the mic failed to start
void listenSetHelpTest(bool on);        // HELP's mic test (levels without Listen)
bool listenHelpTest();
void listenRecalibrate();
bool listenShakeHush();                 // Shake's Hush (v0.43); false = it is down already
// Watcher (v0.6): where the Hush is (Listen's or the shake's, the same states)
enum WatchStage : uint8_t { WS_NONE = 0, WS_DROP, WS_HOLD, WS_NOTICED, WS_RECOVER };
struct ListenWatch {
  WatchStage stage;
  float t, dur;           // time in the stage and its length (s)
  uint32_t hushes;        // bumped by every Hush (a new one re-opens the eye)
  uint32_t responses;     // bumped when the lone "noticed" event fires
};
void listenWatch(ListenWatch& w);               // SETTINGS > Listen > Recalibrate (room floor, measured lag)

// For MAIN / HELP
float listenAttention();                // 0..1
bool  listenHushing();
struct ListenDebug {
  float inDb, inBandDb;                 // mic, last window (full band / 1..4 kHz)
  float outDb, outBandDb;               // our own output written in that window
  float riseF, riseB;                   // mic rise over the ~0.3 s before
  float outRise;                        // our output's rise where that sound was written
  float floorDb;                        // room floor
  int   ownDist;                        // windows from the nearest onset of ours heard, 99 = none
  int   latFixed;                       // output -> mic lag used (windows)
  float latMeasured;                    // lag measured on our own onsets, -1 = not yet
  bool  gated;                          // not judging at all
  int   state;                          // 0 idle, 1 drop, 2 hold, 3 noticed, 4 recover, 5 cooldown
  int   hushes;
  int   recoveries;                     // mic recovery stages tried this session
};
void listenDebug(ListenDebug& d);
// A key is pressed (or G0): the mic hears the keyboard, so no Hush for ~1 s
void listenNoteKey();
// The IMU sees the device being moved (hands, table): same hold as for keys
void listenNoteMove();
bool listenResting();                   // a burst of Hushes: no new one for a while
bool listenMoving();                    // the device is being moved (no Hush)

// Diagnostic log /MIASMORA/listen_log.csv (v0.23, numbers only)
void listenMark(bool falseHush);        // Shift+L: false Hush, Shift+K: no reaction
void listenLogFlush();                  // write what is buffered (before deep sleep)
// The window that started the last Hush (HELP shows it): its rises, and the
// first layer with an onset of ours in the second before (-1 = none).
bool listenLastTrigger(uint32_t& ms, float& riseF, float& riseB, int& layer);
