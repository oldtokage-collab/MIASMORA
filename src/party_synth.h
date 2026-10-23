// MIASMORA — Party Mode's music (v0.65, spec v1.23 3.5)
//
// No new sound sources: the parts are built from the same DSP pieces as the
// layers -- HEART's thump for the kick (with its Attack: a clicky transient
// and a pitch drop), a burst of band-passed noise for the clap (as in KNOCK /
// SCRAPE), AIR's noise through a high-pass for the hats, SUB's sine for the
// bass, and CLUSTER-like detuned saws through a low-pass for the chords --
// but plain major / minor / sus chords this time. One 16-step sequencer runs
// them at the tempo and in the Beat chosen. Its random stream is its own
// (never a layer's).
#pragma once
#include <stdint.h>
#include "dsp/dsp.h"

enum PartyBeat : uint8_t { PB_FOUR = 0, PB_HALF, PB_BREAK, PB_DNB, NUM_PARTY_BEATS };
enum PartyHat : uint8_t { PH_OFF = 0, PH_8TH, PH_16TH, NUM_PARTY_HATS };
enum PartyBass : uint8_t { PBS_OFF = 0, PBS_BOUNCE, PBS_ROLL, NUM_PARTY_BASS };
enum PartyChord : uint8_t { PC_OFF = 0, PC_MAJOR, PC_MINOR, PC_SUS, NUM_PARTY_CHORDS };

struct PartyParams {
  bool    on;          // the party plays (the scene is silent)
  float   level;       // 0..1: the party's own fade
  bool    kickOnly;    // the heartbeat before the explosion: the kick alone
  float   heart;       // 0..1: the kick from a muffled heartbeat (0) to a real kick (1)
  uint8_t tempo;       // 100..140 BPM
  uint8_t beat;        // PartyBeat
  uint8_t kick;        // 0..100: short and hard .. low and long
  uint8_t punch;       // 0..100: the kick's attack
  uint8_t hat;         // PartyHat
  uint8_t bass;        // PartyBass
  uint8_t chord;       // PartyChord
  uint8_t key;         // 0..11: C .. B
  uint8_t swing;       // 0..50 %
  uint32_t fillSeq;    // bump for a one-bar drum fill
};

class PartySynth {
 public:
  void reset(uint32_t seed);
  // adds nothing when !p.on; the output is dry (the engine's reverb is set short)
  void render(float* out, int n, const PartyParams& p);
  uint32_t beats() const { return beats_; }     // quarter notes so far (the screen and LED follow them)
  float beatPhase() const { return beatPh_; }   // 0..1 within the current quarter note

 private:
  struct Drum { float env = 0.f, dec = 0.999f; };
  void triggerDrums(int step, const PartyParams& p, bool fill);
  void triggerMelody(int step, const PartyParams& p);
  float kickTick(const PartyParams& p);
  dsp::Rng r_;
  // clocks: the bar (bass, chords, the beat the screen and LED follow) and the
  // drums -- the same, except in DnB, where the drums run twice as fast
  float samplesToStep_ = 0.f;
  int   step_ = 0;            // 0..15 in the bar
  uint32_t bar_ = 0, beats_ = 0;
  float dSamples_ = 0.f;
  int   dStep_ = 0;
  float beatPh_ = 0.f;
  uint32_t lastFill_ = 0;
  bool  fillBar_ = false, fillPending_ = false;
  // kick
  float kPh_ = 0.f, kEnv_ = 0.f, kPitch_ = 0.f, kClick_ = 0.f, kDub_ = 0.f;
  int32_t kDubIn_ = -1;
  dsp::OnePole kLp_;
  // clap, hats
  float cEnv_ = 0.f, cTailEnv_ = 0.f;
  int32_t cT_ = 0;
  dsp::Svf cBp_;
  float hEnv_ = 0.f, hDec_ = 0.99f, hAmp_ = 0.f;
  dsp::Svf hHp_;
  // bass
  float bPh_ = 0.f, bInc_ = 0.f, bEnv_ = 0.f, bLfo_ = 0.f;
  dsp::Svf bLp_;
  // chord: four detuned saws
  float chPh_[4] = {0}, chInc_[4] = {0}, chEnv_ = 0.f;
  dsp::Svf chLp_;
  int   chordIdx_ = 0;
  dsp::DcBlock dc_;
};
