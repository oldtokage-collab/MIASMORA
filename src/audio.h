// MIASMORA — synthesis engine, mixer, master stage and the live audio task
#pragma once
#include <stdint.h>
#include "config.h"
#include "layers/layer.h"
#include "dsp/reverb.h"
#include "party_synth.h"

// Everything the audio side needs for one buffer. Built on the UI side from
// the scene (DREAD already applied) and handed over as a whole, never field
// by field (see paramsPublish()).
struct EngineParams {
  LayerCtl L[NUM_LAYERS];   // L_REVERB: level = mix, p[0] = size, p[1] = damping
  float    master;          // master gain incl. volume curve and sleep fade; 0 = stopped
  float    lfoMul;          // DREAD: modulation depth
  float    densMul;         // DREAD: event rate
  uint32_t seed;            // user seed (0..999999)
  uint32_t resetSeq;        // bump to restart the engine from `seed`
  uint32_t cutSeq;          // bump for an instant cut with a click (Cursed Mode); live only
  uint32_t summonSeq;       // bump to fire one event of layer summonLayer now (Summon, Listen's Response); live only
  int8_t   summonLayer;
  uint32_t summonSeed;      // the summoned event's own random stream (v0.43)
  float    tension;         // 0..1: DREAD (+ Listen Attention, Arc live) -> HEART's React
  float    climax;          // 0..1: Arc's climax (live only, v0.5): HEART races, SHEPARD slides faster
  uint32_t hushSeq;         // Listen: number of Hushes so far (live only)
  uint32_t heartStopSeq;    // Cursed Mode: bump to stop HEART for heartStopBeats beats (live only, v0.37)
  int8_t   heartStopBeats;
  bool     heartSummonOnly;  // the splash (v0.98): HEART beats only when summoned, no regular beats (live only)
  // Party Mode (v0.65, live only): the scene's own level (0 = silent: its
  // layers are not even rendered) and the party's music
  float    sceneMul;
  PartyParams party;
};

class Engine {
 public:
  void begin();
  // Restart everything from `seed`. loopLen > 0 selects Strict loop mode
  // (samples). snap = start at full level with no ramps (offline export).
  void reset(uint32_t seed, uint32_t loopLen, const EngineParams& p, bool snap);
  void setParams(const EngineParams& p) { p_ = p; }
  void render(float* out, int n);
  // Stopped and fully faded out
  bool silent() const { return p_.master <= 0.f && master_ < 0.0001f; }
  float activity(int i) const { return act_[i]; }
  // Listen (v0.33): bit i = layer i's own output rose suddenly in the last
  // render() (an event, a beat, a burst); ONSET_ENGINE = Cursed Mode's click.
  // Listen does not take a sudden rise at the mic shortly after one of these
  // for a sound in the room.
  static constexpr uint32_t ONSET_ENGINE = 1u << 31;
  uint32_t onsets() const { return onsets_; }
  // Summon (v0.43): could layer i play one more event now? (read from the UI side)
  bool layerHasFreeVoice(int i) const { return i >= 0 && i < NUM_LAYERS && layers_[i] && !asleep_[i] && layers_[i]->hasFreeVoice(); }
  // Stem export (v0.7, spec v1.1 8.1): with buses set, render() also adds every
  // layer into the bus of its group (events / everything else) and stops
  // before the DC blocker, reverb and master stage -- the exporter does those
  // per stem. nullptr, nullptr: the normal mix again.
  void setStemBuses(float* drone, float* events) { stemD_ = drone; stemE_ = events; }
  Reverb& reverb() { return rev_; }   // the first stem reverb (the mix's own is not used then)
  // Party Mode: quarter notes so far and where in the current one (the screen and LED follow them)
  uint32_t partyBeats() const { return party_.beats(); }
  float partyBeatPhase() const { return party_.beatPhase(); }

 private:
  Layer*   layers_[NUM_LAYERS] = {nullptr};
  float    gain_[NUM_LAYERS] = {0};
  bool     asleep_[NUM_LAYERS] = {false};
  int      fade_[NUM_LAYERS] = {0};
  float    act_[NUM_LAYERS] = {0};
  float    onBase_[NUM_LAYERS] = {0};   // per-layer average peak level (dB, ~0.3 s)
  uint32_t onsets_ = 0;
  EngineParams p_ = {};
  Reverb   rev_;
  dsp::DcBlock dc_;
  float    master_ = 0.f;
  float    limEnv_ = 0.f;
  uint32_t seed_ = 0;
  bool     snap_ = false;
  uint32_t lastCut_ = 0;
  uint32_t lastSummon_ = 0;
  bool     summonPending_ = false;
  int      summonLayer_ = -1;
  uint32_t summonSeed_ = 0;
  float    pop_ = 0.f;
  float    sceneG_ = 1.f;      // the scene's level, smoothed (Party Mode)
  float*   stemD_ = nullptr;   // stem export buses
  float*   stemE_ = nullptr;
  bool     partyOn_ = false;
  PartySynth party_;
  dsp::Rng popRng_;
  void addPop(float* out, int n);
  float    tmp_[BUF];
};
extern Engine gEngine;

// --- UI -> audio parameter hand-over (double buffer + one atomic index) ----
void paramsPublish(const EngineParams& p);

// --- live audio task ------------------------------------------------------
void audioStart();
// Pause the live task (fades out first). Returns true once it is idle and
// the engine may be used by someone else (the exporter).
bool audioPause(uint32_t timeoutMs);
// Let the live task run again; it restarts the engine from the current seed.
void audioResume();

// --- visualiser feed ------------------------------------------------------
static constexpr int VIZ_N = 64;
extern volatile float gVizHist[VIZ_N];   // per-buffer RMS of the live output
extern volatile uint32_t gVizOnset[VIZ_N];   // Engine::onsets() of the same buffers (v0.42)
extern volatile int   gVizIdx;
// v0.42 (spec v1.22 4): a one-slot mailbox of the live output for the Scope
// and Spectrum views. The UI sets gVizSnapReq; the audio task fills
// gVizSnap over the next buffers and sets gVizSnapReady; nothing more is
// taken until the UI has read it and asks again (no cost for the other views).
static constexpr int VIZ_SNAP_N = 1024;
extern int16_t gVizSnap[VIZ_SNAP_N];
extern volatile bool gVizSnapReq, gVizSnapReady;
extern volatile float gAudioLoad;        // render time / buffer time (0..1+), smoothed
extern volatile float gAudioLoadPeak;    // highest single buffer since the last reset
extern volatile float gAudioLoadMean;    // mean since the last reset
extern volatile bool  gAudioStatsReset;  // UI -> audio: restart peak / mean / dropout count
// v0.31: render load in 1 % bins since the reset (for the p99), and the mean
// busy time of the whole audio task (render + dither + REC + mic + events)
static constexpr int LOAD_BINS = 201;    // 0..200 %
extern volatile uint32_t gLoadHist[LOAD_BINS];
extern volatile float gAudioBusyMean;
float audioLoadP99();                    // 0..2
