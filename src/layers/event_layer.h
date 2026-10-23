// MIASMORA — shared scheduling for event layers
//
// Events are a Poisson process on the layer's own scheduler stream (srng_).
// `wait` is the number of samples from the start of the current render call
// to the next event, so triggers are sample-accurate inside a buffer.
// loopStart() reseeds the stream and rolls the first wait, which is what
// makes a Strict loop schedule the same events on every pass.
#pragma once
#include "layer.h"

class EventLayer : public Layer {
 public:
  void loopStart(uint32_t seed) override {
    srng_.seed(seed);
    wait = nextInterval();
  }

  // Summon: the scheduler stream is swapped for the summon's own for one
  // buffer and the next event is due at once; afterwards both come back.
  bool beginSummon(uint32_t seed, int n) override {
    if (wait < n) return false;   // its own event is due in this buffer
    savedRng_ = srng_;
    savedWait_ = wait;
    srng_.seed(seed);
    wait = 0;
    return true;
  }
  void endSummon(int n) override {
    srng_ = savedRng_;
    wait = savedWait_ - n;
  }

 protected:
  dsp::Rng savedRng_;
  int32_t savedWait_ = 0;
  // Density (0..1) -> events per minute on an exponential scale; 0 = never.
  static float densityToRate(float d, float loPerMin, float hiPerMin) {
    if (d < 0.005f) return 0.f;
    return dsp::expMap(d, loPerMin, hiPerMin) * gClk.densMul / 60.f;
  }
  void setRate(float perSec) {
    rate_ = perSec;
    // A huge wait left over from Density 0 is re-rolled once events are wanted
    if (rate_ > 0.f && (float)wait > 4.f * SR / rate_) wait = nextInterval();
  }
  int32_t nextInterval() {
    if (rate_ <= 0.f) return 0x3FFFFFFF;
    float s = srng_.expo(1.f / rate_);
    if (s < 0.04f) s = 0.04f;
    if (s > 3600.f) s = 3600.f;
    return (int32_t)(s * SR);
  }
  // Fire trig(offset) for every event falling inside this render call
  template <class F> void runSched(int n, F trig) {
    while (wait < n) {
      int32_t off = wait < 0 ? 0 : wait;
      trig((int)off);
      wait += nextInterval();
    }
    wait -= n;
  }

  dsp::Rng srng_;
  float rate_ = 0.f;
};

// Pick an idle voice, otherwise the one that has been sounding longest
// Any voice of a pool that is not sounding
template <class V, int N> static bool anyFree(const V (&v)[N]) {
  for (int k = 0; k < N; k++) if (!v[k].on) return true;
  return false;
}

template <class V, int N> static V& grabVoice(V (&v)[N]) {
  V* best = &v[0];
  for (int k = 0; k < N; k++) {
    if (!v[k].on) return v[k];
    if (v[k].t > best->t) best = &v[k];
  }
  return *best;
}

static inline int blk(int n, int i) { return (n - i < CTL) ? n - i : CTL; }
