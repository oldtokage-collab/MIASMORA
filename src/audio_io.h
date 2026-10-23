// MIASMORA — own full-duplex I2S driver for the ES8311 codec (Cardputer ADV)
//
// M5Unified drives the speaker on I2S port 1 and the microphone on port 0,
// sharing BCK/WS, so the two cannot run at the same time. For Listen mode
// MIASMORA takes port 1 over after M5Unified has configured the codec and
// runs it as master TX + RX itself (approach proven in AEOLYRA).
//
// After audioIoBegin() nothing may call M5Cardputer.Speaker / M5Cardputer.Mic
// again: their tasks would fight this driver for the port.
#pragma once
#include <stdint.h>

// Bring the codec up through M5Unified, then replace its I2S driver with
// ours. Returns false if the driver could not be installed (no sound).
bool audioIoBegin();

// Write mono samples (both slots get the same value). Blocks while the DMA
// queue is full, which is what paces the audio task.
void audioIoWrite(const int16_t* mono, int frames);

// Stop the output cleanly (before deep sleep). Writes silence, then removes
// the driver.
void audioIoEnd();
// Poltergeist (v0.93): the codec's DAC and output stage off while nothing
// plays (the volume ramped down first, up again after: no click), and the
// I2S stopped altogether for light sleep (the audio task paused first)
void audioIoDacPower(bool on);
void audioIoSuspend(bool suspend);

// True once our driver is installed and running.
bool audioIoReady();

// Load test (v0.3): audio task, once per buffer -- drain the driver's event
// queue and count dropouts (the output ran dry). count = false only throws
// the events away (after a pause, whose silence is intended).
void audioIoPollEvents(bool count);
uint32_t audioIoDropouts();
void audioIoResetDropouts();

// --- microphone ----------------------------------------------------------------
// Switch the codec's ADC on / off. I2C register writes: call from the UI
// thread only (the keyboard and IMU share that bus). Returns false if the
// codec did not answer.
bool audioIoMicEnable(bool on);
bool audioIoMicOn();

// Audio task, once per buffer after audioIoWrite(): take whatever the ADC has
// delivered (never waits) and update the level statistics.
void audioIoMicProcess();
// Audio task, before audioIoWrite(): the engine's onsets in that buffer (Listen)
void audioIoNoteOwnOnsets(uint32_t mask);

// One measurement window: what the mic delivered during one pass of the
// audio task (normally one buffer, ~23 ms; v0.33, was ~100 ms) and what was
// written to the speaker in the same pass. "Band" is the same signal through a
// 1..4 kHz band-pass (claps and voices are strong there). The output written
// now reaches the mic only some windows later (DMA queue + codec + air).
struct MicWindow {
  float inDb, outDb;           // mic RMS (DC removed) / our own output RMS above ~200 Hz, dBFS
  float inBandDb, outBandDb;
  uint32_t own;                // Engine::onsets() of the buffers written in this window
  uint16_t n;                  // mic samples in the window
  bool  frozen;                // every input sample identical (dead ADC)
  uint8_t slot;                // I2S slot the mic was read from (0 left, 1 right)
  float otherDb;               // level in the other slot
};
// UI side: take the oldest window not read yet. Returns false if none.
bool audioIoMicPop(MicWindow& w);

struct MicStats {
  float    procUs;     // mic processing time per buffer (smoothed), µs
  uint32_t windows;    // windows measured since the mic was switched on
  uint32_t freezes;    // of which frozen
  uint32_t underruns;  // buffers for which no input was ready
  uint32_t lost;       // windows dropped because the UI did not collect them
  uint8_t  slot;       // I2S slot in use (0 left, 1 right); switches by itself
  uint32_t slotSwitches;
};
void audioIoMicStats(MicStats& out);

// Recovery for a mic that has gone silent or frozen (v0.26). UI thread only.
// stage 1: rewrite the ADC registers, 2: ADC off / on, 3: reinstall the I2S
// driver (a moment of silence in the output). Returns false if the stage
// could not be carried out.
bool audioIoMicRecover(int stage);
