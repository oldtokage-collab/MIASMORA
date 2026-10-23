// MIASMORA — sustained layers: SUB, CLUSTER, AIR, METAL
//
// All slow movement here is driven by Lfo objects (period overridable for
// Strict loops). Pitched oscillators run on Phasor; pitch drift is applied as
// phase modulation so the waveform stays periodic in Strict mode.
#include "../dsp/speed.h"   // first: speed-optimised (v0.32)
#include "layer.h"

using namespace dsp;

static inline float smoothTo(float cur, float tgt, bool snap, float k = 0.2f) {
  return snap ? tgt : cur + (tgt - cur) * k;
}
// Natural LFO period in seconds after the Strict override (used by PM integrals)
static inline float resolvedSec(const Lfo& l) { return gClk.resolvePeriodSamples(l.period * SR) * INV_SR; }

// ===========================================================================
// SUB — 30..70 Hz sine, tanh drive, cents-scale pitch drift, breathing level
// ===========================================================================
class SubLayer : public Layer {
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0x51B0u);
    d1_.init(init.uni(), 17.f);
    d2_.init(init.uni(), 41.f);
    b1_.init(init.uni(), 23.f);
    b2_.init(init.uni(), 11.3f);
    ph_.reset();
    first_ = true;
    loopStart(seed);
  }
  void loopStart(uint32_t) override {}
  void control(const LayerCtl& c, bool snap) override {
    pitch_ = smoothTo(pitch_, expMap(c.p[0], 30.f, 70.f), snap, 0.15f);
    drive_ = smoothTo(drive_, c.p[1], snap);
    drift_ = smoothTo(drift_, c.p[2], snap);
  }
  void render(float* out, int n) override {
    const float gain = 1.f + 5.f * drive_;
    const float bias = 0.3f * drive_;
    const float sb = softSat(bias);
    const float norm = 1.f / (softSat(gain + bias) - sb);
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      d1_.advance(i, nb); d2_.advance(i, nb); b1_.advance(i, nb); b2_.advance(i, nb);
      ph_.setHz(pitch_);
      // Frequency deviation C*sin -> phase offset -C*T/(2pi)*cos (in cycles)
      float cents = 14.f * drift_ * gClk.lfoMul;
      float k = pitch_ * 0.000577623f * cents;          // Hz per cent * cents
      float off1 = -k * (0.6f * resolvedSec(d1_) * d1_.cos() + 0.4f * resolvedSec(d2_) * d2_.cos()) * (1.f / TWO_PI_F);
      float depth = (0.15f + 0.3f * drift_) * gClk.lfoMul;
      if (depth > 0.7f) depth = 0.7f;
      float amp1 = 1.f - depth * (0.65f * b1_.uni() + 0.35f * b2_.uni());
      if (first_) { off_ = off1; amp_ = amp1; first_ = false; }
      float dOff = (off1 - off_) / nb, dAmp = (amp1 - amp_) / nb;
      for (int j = 0; j < nb; j++) {
        float s = sinW(ph_.tick() + off_);
        s = softSat(s * gain + bias) - sb;
        out[i + j] = s * norm * amp_ * 0.55f;
        off_ += dOff; amp_ += dAmp;
      }
      off_ = off1; amp_ = amp1;
    }
  }

 private:
  Lfo d1_, d2_, b1_, b2_;
  Phasor ph_;
  float pitch_ = 45.f, drive_ = 0.3f, drift_ = 0.4f;
  float off_ = 0.f, amp_ = 1.f;
  bool first_ = true;
};

// ===========================================================================
// CLUSTER — 4 detuned band-limited saw / pulse voices, dark SVF low-pass
// ===========================================================================
static const int8_t IV_TABLE[4][4] = {
  {0, 0, 12, 0},     // Unison: beating only
  {0, 1, 12, 13},    // Minor 2nd
  {0, 6, 12, 18},    // Tritone
  {0, 1, 2, 3},      // Cluster
};
static const float DET_SHAPE[4] = {-1.0f, 0.7f, -0.45f, 1.0f};

class ClusterLayer : public Layer {
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0xC105u);
    c1_.init(init.uni(), 23.f);
    c2_.init(init.uni(), 37.f);
    a1_.init(init.uni(), 19.f);
    for (int v = 0; v < 4; v++) ph_[v].reset();
    f_.clear();
    loopStart(seed);
  }
  void loopStart(uint32_t) override {}
  void control(const LayerCtl& c, bool snap) override {
    iv_ = (int)c.p[0] & 3;
    det_ = smoothTo(det_, c.p[1], snap);
    dark_ = smoothTo(dark_, c.p[2], snap);
  }
  void render(float* out, int n) override {
    const float root = 61.74f;   // B1
    float hz[4];
    for (int v = 0; v < 4; v++) {
      float cents = IV_TABLE[iv_][v] * 100.f + DET_SHAPE[v] * (2.f + 28.f * det_);
      hz[v] = root * powf(2.f, cents / 1200.f);
    }
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      c1_.advance(i, nb); c2_.advance(i, nb); a1_.advance(i, nb);
      for (int v = 0; v < 4; v++) ph_[v].setHz(hz[v]);
      float oct = (0.45f * c1_.sin() + 0.25f * c2_.sin()) * gClk.lfoMul;
      float cut = 120.f * powf(2.f, (1.f - dark_) * 4.5f + oct);
      f_.set(cut, 1.0f);
      float amp = 1.f - 0.22f * gClk.lfoMul * a1_.uni();
      if (amp < 0.3f) amp = 0.3f;
      for (int j = 0; j < nb; j++) {
        float s = 0.f;
        for (int v = 0; v < 4; v++) {
          float dt = ph_[v].dt();
          float t = ph_[v].tick();
          if (v & 1) {   // pulse, width 0.35
            float t2 = t + 0.35f; if (t2 >= 1.f) t2 -= 1.f;
            s += 0.5f * (sawBL(t, dt) - sawBL(t2, dt));
          } else {
            s += sawBL(t, dt);
          }
        }
        out[i + j] = f_.lp(s * 0.2f) * amp * 0.9f;
      }
    }
    f_.flush();
  }

 private:
  Lfo c1_, c2_, a1_;
  Phasor ph_[4];
  Svf f_;
  int iv_ = 1;
  float det_ = 0.35f, dark_ = 0.6f;
};

// ===========================================================================
// AIR — pink / brown noise through a slowly wandering band-pass
// ===========================================================================
class AirLayer : public Layer {
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0xA112u);
    c1_.init(init.uni(), 31.f);
    c2_.init(init.uni(), 13.f);
    a1_.init(init.uni(), 19.f);
    a2_.init(init.uni(), 7.3f);
    f_.clear(); pink_.clear(); brown_.clear();
    loopStart(seed);
  }
  void loopStart(uint32_t seed) override { rng_.seed(seed); }
  void control(const LayerCtl& c, bool snap) override {
    color_ = smoothTo(color_, c.p[0], snap);
    motion_ = smoothTo(motion_, c.p[1], snap);
    brown_on_ = ((int)c.p[2]) == NZ_BROWN;
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      c1_.advance(i, nb); c2_.advance(i, nb); a1_.advance(i, nb); a2_.advance(i, nb);
      float m = motion_ * gClk.lfoMul;
      float center = 70.f * powf(2.f, color_ * 5.3f + m * (0.9f * c1_.sin() + 0.4f * c2_.sin()));
      f_.set(center, 1.4f);
      float amp = 1.f - m * 0.45f * (0.6f * a1_.uni() + 0.4f * a2_.uni());
      if (amp < 0.1f) amp = 0.1f;
      float g = amp * (brown_on_ ? 1.5f : 1.0f) * 0.9f;
      for (int j = 0; j < nb; j++) {
        float w = rng_.bi();
        float x = brown_on_ ? brown_.tick(w) : pink_.tick(w);
        float lp, bp;
        f_.tick(x, lp, bp);
        out[i + j] = (bp * f_.k * 0.9f + lp * 0.35f) * g;
      }
    }
    f_.flush(); pink_.flush(); brown_.flush();
  }

 private:
  Rng rng_;
  Lfo c1_, c2_, a1_, a2_;
  Svf f_;
  PinkNoise pink_;
  BrownNoise brown_;
  float color_ = 0.4f, motion_ = 0.4f;
  bool brown_on_ = false;
};

// ===========================================================================
// METAL — 2-operator FM at a non-integer ratio, slowly moving index
// ===========================================================================
class MetalLayer : public Layer {
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0x3E7Au);
    i1_.init(init.uni(), 29.f);
    i2_.init(init.uni(), 11.f);
    a1_.init(init.uni(), 43.f);
    pc1_.reset(); pc2_.reset(); pm_.reset();
    lp_.clear();
    lp_.set(2500.f);
    first_ = true;
    loopStart(seed);
  }
  void loopStart(uint32_t) override {}
  void control(const LayerCtl& c, bool snap) override {
    int r = (int)c.p[0];
    if (r < 0 || r >= NUM_METAL_RATIOS) r = 0;
    ratio_ = METAL_RATIOS[r];
    index_ = smoothTo(index_, c.p[1], snap);
    motion_ = smoothTo(motion_, c.p[2], snap);
  }
  void render(float* out, int n) override {
    const float fc = 87.31f;   // F2
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      i1_.advance(i, nb); i2_.advance(i, nb); a1_.advance(i, nb);
      pc1_.setHz(fc);
      pc2_.setHz(fc * 1.0031f);
      pm_.setHz(fc * ratio_);
      float m = motion_ * gClk.lfoMul;
      float I1 = (0.3f + 5.0f * index_) * (1.f + 0.5f * m * i1_.sin() + 0.25f * m * i2_.sin());
      if (I1 < 0.f) I1 = 0.f;
      I1 *= (1.f / TWO_PI_F);   // index in cycles
      float amp1 = 1.f - 0.3f * m * a1_.uni();
      if (first_) { I_ = I1; amp_ = amp1; first_ = false; }
      float dI = (I1 - I_) / nb, dA = (amp1 - amp_) / nb;
      for (int j = 0; j < nb; j++) {
        float mod = I_ * sinL(pm_.tick());
        float s = 0.5f * (sinW(pc1_.tick() + mod) + sinW(pc2_.tick() + mod * 0.8f));
        out[i + j] = lp_.lp(s) * amp_ * 0.4f;
        I_ += dI; amp_ += dA;
      }
      I_ = I1; amp_ = amp1;
    }
    lp_.flush();
  }

 private:
  Lfo i1_, i2_, a1_;
  Phasor pc1_, pc2_, pm_;
  OnePole lp_;
  float ratio_ = 1.41f, index_ = 0.4f, motion_ = 0.4f;
  float I_ = 0.f, amp_ = 1.f;
  bool first_ = true;
};

Layer* createSub()     { return new SubLayer(); }
Layer* createCluster() { return new ClusterLayer(); }
Layer* createAir()     { return new AirLayer(); }
Layer* createMetal()   { return new MetalLayer(); }
