// MIASMORA — layer base class and the shared modulation / timing structures
//
// Loop-aware design (spec 3.2 / 6.4):
//  * Clock carries the engine position and, during a Strict export, the loop
//    length. Everything time-dependent goes through it.
//  * Lfo is a phase accumulator + period. In free mode it simply accumulates;
//    when a loop length is set, its period is overridden to L/k (k integer,
//    nearest to the natural period) and its phase is derived from the loop
//    position with integer maths, so it returns exactly to its start value at
//    every loop boundary.
//  * Phasor is an exact rational phase accumulator (num/den, integers) for
//    sustained pitched oscillators. In Strict mode den = loop length, so any
//    constant frequency is quantised to a whole number of cycles per loop and
//    the waveform is sample-exactly periodic. Pitch drift is applied as phase
//    modulation on top, never by changing the step, so it stays periodic too.
//  * Every layer owns its random streams (derived from the seed and its fixed
//    stream id). loopStart() reseeds them, so in Strict mode each loop pass
//    schedules exactly the same events. Event voices own a private Rng seeded
//    at trigger time, so a voice that rings across a loop boundary sounds the
//    same as its twin in the previous pass.
#pragma once
#include <stdint.h>
#include "../config.h"
#include "../dsp/dsp.h"
#include "layer_defs.h"

struct Clock {
  uint32_t pos = 0;            // samples since engine reset, at the start of the current render call
  uint32_t loopLen = 0;        // Strict loop length in samples; 0 = free running
  uint32_t den = SR * 512;     // Phasor denominator
  float    invDen = 1.0f / (SR * 512);
  float    denPerHz = 512.f;   // den / SR
  float    lfoMul = 1.f;       // DREAD: modulation depth multiplier
  float    densMul = 1.f;      // DREAD: event rate multiplier
  float    tension = 0.f;      // 0..1 from DREAD / Listen Attention (HEART's React); fixed while exporting
  float    climax = 0.f;       // 0..1: Arc's climax (v0.5); always 0 in exports
  uint32_t hushSeq = 0;        // bumped by every Listen Hush (HEART skips a beat)
  uint32_t heartStopSeq = 0;   // bumped by Cursed Mode: HEART stops for heartStopBeats beats (v0.37)
  int      heartStopBeats = 0;
  bool     heartSummonOnly = false;   // the splash: HEART's regular beats are off, only summoned ones (v0.98)

  void setFree() { loopLen = 0; den = SR * 512; denPerHz = 512.f; invDen = 1.0f / (float)den; }
  void setLoop(uint32_t lenSamples) {
    loopLen = lenSamples;
    den = lenSamples;
    denPerHz = (float)lenSamples / SR;
    invDen = 1.0f / (float)den;
  }
  // Period override hook: free -> natural, Strict -> nearest divisor of the loop
  float resolvePeriodSamples(float samples) const {
    if (!loopLen) return samples;
    float k = roundf((float)loopLen / samples);
    if (k < 1.f) k = 1.f;
    return (float)loopLen / k;
  }
};
extern Clock gClk;

// Per-buffer controls handed to a layer (already DREAD-adjusted)
struct LayerCtl {
  float level;          // linear gain target (0 = off)
  float p[MAX_P];       // continuous: 0..1, option: index as float
};

// ---------------------------------------------------------------------------
struct Lfo {
  float ph = 0.f, ph0 = 0.f, period = 10.f;   // period in seconds (natural)
  void init(float phase0, float periodSec) { ph0 = phase0; ph = phase0; period = periodSec; }
  // Bring the phase to engine position gClk.pos + off; n = samples since the previous call.
  inline void advance(uint32_t off, int n) {
    if (gClk.loopLen) {
      const uint32_t L = gClk.loopLen;
      uint32_t k = (uint32_t)lroundf((float)L / (period * SR));
      if (k < 1) k = 1;
      uint32_t p = (gClk.pos + off) % L;
      uint32_t q = (uint32_t)(((uint64_t)k * p) % L);
      ph = ph0 + (float)q / (float)L;
      if (ph >= 1.f) ph -= 1.f;
    } else {
      ph += (float)n / (period * SR);
      if (ph >= 1.f) ph -= (float)(int)ph;
    }
  }
  DSP_INLINE float sin() const { return dsp::sinL(ph); }
  DSP_INLINE float cos() const { return dsp::sinW(ph + 0.25f); }
  DSP_INLINE float uni() const { return 0.5f + 0.5f * dsp::sinL(ph); }
};

struct Phasor {
  uint32_t num = 0, step = 0;
  void reset() { num = 0; }
  inline void setHz(float hz) {
    float s = hz * gClk.denPerHz;
    if (s < 0.f) s = 0.f;
    step = (uint32_t)(s + 0.5f);
    if (step >= gClk.den) step %= gClk.den;
  }
  DSP_INLINE float tick() {
    float p = (float)num * gClk.invDen;
    num += step;
    if (num >= gClk.den) num -= gClk.den;
    return p;
  }
  DSP_INLINE float dt() const { return (float)step * gClk.invDen; }
};

// Attack-safe envelope helper: a linear ramp of `atk` samples from 0, for
// click-free onsets (spec 4.4). Returns 0..1.
static DSP_INLINE float atkRamp(int t, int atk) { return t >= atk ? 1.f : (float)t / (float)atk; }
static inline int ms2s(float ms) { return (int)(ms * 0.001f * SR); }

// ---------------------------------------------------------------------------
class Layer {
 public:
  virtual ~Layer() {}
  // Full state reset (voices, filters, LFO phases) then loopStart(seed).
  virtual void reset(uint32_t seed) = 0;
  // Reseed the random streams / event scheduler. Called by reset() and, in
  // Strict mode, at every loop boundary.
  virtual void loopStart(uint32_t seed) = 0;
  // Per-buffer parameter update. `snap` = jump to targets (no smoothing).
  virtual void control(const LayerCtl& c, bool snap) = 0;
  // Writes (not adds) n samples.
  virtual void render(float* out, int n) = 0;

  // Event scheduler shared by all event layers: `wait` counts samples from
  // the start of the current render call to the next event.
  int32_t wait = 0x3FFFFFFF;

  // Summon (v0.43, spec v1.22 1.6; also Listen's Response): one event now,
  // on its own random stream so the layer's scheduled events do not shift.
  // beginSummon() before this buffer's render(), endSummon() after it; false =
  // not in this buffer (an event of its own is due in it: try the next one).
  virtual bool hasFreeVoice() const { return false; }   // a voice that is not sounding (nothing is stolen)
  virtual bool beginSummon(uint32_t seed, int n) { (void)seed; (void)n; return false; }
  virtual void endSummon(int n) { (void)n; }

  // Listen (v0.33): set by a layer whose onset the engine's own detector (a
  // sudden rise of the layer's peak level) could miss; cleared by the engine.
  bool onsetFlag = false;
};

Layer* createLayer(int id);
