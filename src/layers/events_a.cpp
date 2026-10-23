// MIASMORA — event layers: PULSE, CREAK, SWELL, DRIP
#include "../dsp/speed.h"   // first: speed-optimised (v0.32)
#include "event_layer.h"

using namespace dsp;

// ===========================================================================
// PULSE — falling 40..60 Hz sine thump + a short noise click, sometimes doubled
// ===========================================================================
class PulseLayer : public EventLayer {
  struct V {
    bool on; int delay, t, atk;
    float ph, f, fEnd, gl, amp, dec, drive, clk, clkDec;
    OnePole clp; Rng r;
  };
 public:
  void reset(uint32_t seed) override {
    for (auto& v : v_) v.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    weight_ = c.p[1];
    setRate(densityToRate(c.p[0], 0.5f, 20.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) {
      trigger(off, 1.f);
      if (srng_.chance(0.3f)) trigger(off + (int)(srng_.range(0.28f, 0.42f) * SR), 0.65f);   // heartbeat
    });
    for (auto& v : v_) {
      if (!v.on) continue;
      for (int i = 0; i < n; i++) {
        if (v.delay > 0) { v.delay--; continue; }
        float s = sinL(v.ph) * v.amp * atkRamp(v.t, v.atk);
        v.ph += v.f * INV_SR; if (v.ph >= 1.f) v.ph -= 1.f;
        v.f = v.fEnd + (v.f - v.fEnd) * v.gl;
        v.amp *= v.dec;
        float click = v.clp.lp(v.r.bi()) * v.clk;
        v.clk *= v.clkDec;
        if (v.clk < 1e-6f) v.clk = 0.f;   // denormal guard (see dsp.h)
        out[i] += softSat(s * v.drive) * 0.55f + click;
        v.t++;
        if (v.amp < 0.0002f && v.t > v.atk) { v.on = false; break; }
      }
      v.clp.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(v_); }   // Summon (v0.43)
 private:
  void trigger(int off, float ampMul) {
    V& v = grabVoice(v_);
    float f0 = (60.f - 20.f * weight_) * srng_.range(0.96f, 1.04f);
    v.on = true; v.delay = off; v.t = 0; v.atk = ms2s(10);
    v.ph = 0.f;                       // phase 0 start: no onset click
    v.f = f0 * 2.2f; v.fEnd = f0;
    v.gl = expf(-1.f / (0.045f * SR));
    v.amp = 0.9f * ampMul;
    v.dec = expf(-1.f / (SR * (0.22f + 0.8f * weight_) * srng_.range(0.8f, 1.2f)));
    v.drive = 1.f + 2.f * weight_;
    v.clk = 0.35f * ampMul;
    v.clkDec = expf(-1.f / (SR * 0.0025f));
    v.clp.clear(); v.clp.set(1500.f);
    v.r.seed(srng_.next());
  }
  V v_[4] = {};
  float weight_ = 0.5f;
};

// ===========================================================================
// CREAK — stick-slip pulse train (20..80 Hz) striking a wooden resonator bank:
// floorboards, an old chair. (v0.13's "Door" squeal moved to DRAG as Object = Pipe.)
// ===========================================================================
class CreakLayer : public EventLayer {
  struct V {
    bool on; int delay, t, len, tail; float cd, invLen;
    float rate, wf1, wf2, wp1, wp2, burst, bdec, amp;
    float modA, modB, jit, g1, g2;   // rate wobble depths, slip jitter, resonator mix
    Reson r[3]; Rng rr;
  };
 public:
  void reset(uint32_t seed) override {
    for (auto& v : v_) v.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    pitch_ = c.p[1];
    setRate(densityToRate(c.p[0], 0.5f, 12.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) { trigger(off); });
    for (auto& v : v_) {
      if (!v.on) continue;
      for (int i = 0; i < n; i++) {
        if (v.delay > 0) { v.delay--; continue; }
        float u = (float)v.t * v.invLen;   // v0.32: no per-sample divide
        float env = 0.f;
        if (u < 1.f) env = smooth01(u * 4.f) * smooth01((1.f - u) * 3.3f);
        if (u < 1.f && (v.cd -= 1.f) <= 0.f) {
          // a slip: one short noise grain into the bank
          v.burst = env * (0.5f + 0.5f * v.rr.uni());
          float rt = v.rate * (1.f + v.modA * sinL(v.wp1) + v.modB * sinL(v.wp2)) * (0.75f + 0.5f * env);
          if (rt < 5.f) rt = 5.f;
          v.cd = (SR / rt) * (1.f + v.jit * v.rr.bi());
        }
        v.wp1 += v.wf1; if (v.wp1 >= 1.f) v.wp1 -= 1.f;
        v.wp2 += v.wf2; if (v.wp2 >= 1.f) v.wp2 -= 1.f;
        float x = v.rr.bi() * v.burst;
        v.burst *= v.bdec;
        if (v.burst < 1e-6f) v.burst = 0.f;   // denormal guard
        float y = v.r[0].tick(x) + v.g1 * v.r[1].tick(x) + v.g2 * v.r[2].tick(x);
        out[i] += (y + x * 0.08f) * v.amp;
        if (++v.t >= v.len + v.tail) { v.on = false; break; }
      }
      for (auto& r : v.r) r.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(v_); }   // Summon (v0.43)
 private:
  void trigger(int off) {
    V& v = grabVoice(v_);
    v.on = true; v.delay = off; v.t = 0;
    v.len = (int)(srng_.range(0.6f, 2.5f) * SR);
    v.invLen = 1.f / (float)v.len;
    v.tail = ms2s(150);
    v.rate = srng_.range(20.f, 80.f);
    v.cd = 1.f;
    v.wf1 = srng_.range(0.4f, 1.5f) * INV_SR;
    v.wf2 = srng_.range(2.f, 4.f) * INV_SR;
    v.wp1 = srng_.uni(); v.wp2 = srng_.uni();
    v.burst = 0.f;
    v.bdec = expf(-1.f / (SR * 0.0004f));
    v.amp = 0.15f * srng_.range(0.7f, 1.f);   // 3 ringing resonators per slip add up
    float f1 = 300.f * powf(2.f, pitch_ * 1.1f + srng_.range(0.f, 0.3f));
    v.r[0].clear(); v.r[1].clear(); v.r[2].clear();
    v.r[0].set(f1, 0.030f);
    v.r[1].set(f1 * 1.47f, 0.022f);
    v.r[2].set(f1 * 2.31f, 0.015f);
    v.modA = 0.35f; v.modB = 0.15f; v.jit = 0.25f; v.g1 = 0.6f; v.g2 = 0.35f;
    v.rr.seed(srng_.next());
  }
  V v_[2] = {};
  float pitch_ = 0.5f;
};

// ===========================================================================
// SWELL — reverse-envelope swell (slow rise, sudden cut), noise or drone
// ===========================================================================
class SwellLayer : public EventLayer {
  struct V {
    bool on, tone; int delay, t, len, rel;
    float ph1, ph2, inc1, inc2, amp, invLen, invRel;
    Svf f; Rng r;
  };
 public:
  void reset(uint32_t seed) override {
    for (auto& v : v_) v.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    length_ = c.p[1];
    setRate(densityToRate(c.p[0], 0.3f, 8.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) { trigger(off); });
    for (auto& v : v_) {
      if (!v.on) continue;
      for (int i = 0; i < n; i += CTL) {
        int nb = blk(n, i);
        int j = 0;
        while (j < nb && v.delay > 0) { v.delay--; j++; }
        if (j >= nb) continue;
        float u = (float)v.t * v.invLen; if (u > 1.f) u = 1.f;
        v.f.set(200.f * powf(2.f, 3.6f * u), 1.2f);
        for (; j < nb; j++) {
          float uu = (float)v.t * v.invLen;   // v0.32: no per-sample divide
          float env;
          if (v.t < v.len) env = uu * uu * sqrtf(uu);   // u^2.5
          else env = 1.f - (float)(v.t - v.len) * v.invRel;   // sudden (but click-free) cut
          float x;
          if (v.tone) {
            float a = sawBL(v.ph1, v.inc1), b = sawBL(v.ph2, v.inc2);
            v.ph1 += v.inc1; if (v.ph1 >= 1.f) v.ph1 -= 1.f;
            v.ph2 += v.inc2; if (v.ph2 >= 1.f) v.ph2 -= 1.f;
            x = 0.45f * (a + b) + 0.2f * v.r.bi();
          } else {
            x = v.r.bi();
          }
          out[i + j] += v.f.lp(x) * env * v.amp;
          if (++v.t >= v.len + v.rel) { v.on = false; break; }
        }
        if (!v.on) break;
      }
      v.f.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(v_); }   // Summon (v0.43)
 private:
  void trigger(int off) {
    V& v = grabVoice(v_);
    v.on = true; v.delay = off; v.t = 0;
    v.len = (int)((2.f + 4.f * length_) * srng_.range(0.85f, 1.15f) * SR);
    v.rel = ms2s(25);
    v.invLen = 1.f / (float)v.len; v.invRel = 1.f / (float)v.rel;
    v.tone = srng_.chance(0.5f);
    float f = srng_.range(55.f, 110.f);
    v.ph1 = 0.f; v.ph2 = 0.5f;
    v.inc1 = f * INV_SR; v.inc2 = f * 1.006f * INV_SR;
    v.amp = (v.tone ? 0.55f : 0.7f) * srng_.range(0.75f, 1.f);
    v.f.clear();
    v.r.seed(srng_.next());
  }
  V v_[2] = {};
  float length_ = 0.5f;
};

// ===========================================================================
// DRIP — a sine whose pitch rises as it decays (bubble resonance)
// ===========================================================================
class DripLayer : public EventLayer {
  struct V { bool on; int delay, t, len, atk; float ph, inc, incMul, amp, dec; };
 public:
  void reset(uint32_t seed) override {
    for (auto& v : v_) v.on = false;
    loopStart(seed);
  }
  void loopStart(uint32_t seed) override {
    srng_.seed(seed);
    leakFrac_ = 0.f;
    if (mode_ == DR_LEAK) wait = (int32_t)(srng_.uni() * leakPeriod());
    else wait = nextInterval();
  }
  void control(const LayerCtl& c, bool) override {
    size_ = c.p[1];
    int m = (int)c.p[2] == DR_LEAK ? DR_LEAK : DR_RANDOM;
    dens_ = c.p[0];
    if (m == DR_LEAK) {
      rate_ = dens_ > 0.005f ? 1.f : 0.f;   // only used as an on/off flag in Leak mode
      if (m != mode_ || (float)wait > leakPeriod() * 1.5f) wait = (int32_t)(srng_.uni() * leakPeriod());
    } else {
      setRate(densityToRate(dens_, 2.f, 60.f));
      if (m != mode_) wait = nextInterval();
    }
    mode_ = m;
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    if (mode_ == DR_LEAK) {
      if (dens_ > 0.005f) {
        while (wait < n) {
          float P = leakPeriod();
          int off = wait < 0 ? 0 : wait;
          drop(off + (int)(srng_.uni() * 0.04f * P), 1.f, 1.f);   // jitter is not cumulative
          // Fractional period carried over, so k drips span exactly k*P samples
          // (in Strict mode P = L/k and the grid closes on the loop boundary).
          leakFrac_ += P;
          int32_t st = (int32_t)leakFrac_;
          leakFrac_ -= (float)st;
          wait += st;
        }
        wait -= n;
      }
    } else {
      runSched(n, [&](int off) { drop(off, 1.f, 1.f); });
    }
    for (auto& v : v_) {
      if (!v.on) continue;
      for (int i = 0; i < n; i++) {
        if (v.delay > 0) { v.delay--; continue; }
        out[i] += sinL(v.ph) * v.amp * atkRamp(v.t, v.atk);
        v.ph += v.inc; if (v.ph >= 1.f) v.ph -= 1.f;
        v.inc *= v.incMul;
        v.amp *= v.dec;
        if (++v.t >= v.len) { v.on = false; break; }
      }
    }
  }

  bool hasFreeVoice() const override { return anyFree(v_); }   // Summon (v0.43)
  bool beginSummon(uint32_t seed, int n) override {
    if (mode_ != DR_LEAK) return EventLayer::beginSummon(seed, n);
    Rng saved = srng_;   // Leak: one drop now, on the summon's own stream
    srng_.seed(seed);
    drop(0, 1.f, 1.f);
    srng_ = saved;
    direct_ = true;
    return true;
  }
  void endSummon(int n) override {
    if (direct_) { direct_ = false; return; }
    EventLayer::endSummon(n);
  }
 private:
  // Leak: near-constant period in samples, overridable to a loop divisor
  float leakPeriod() const {
    float sec = expMap(dens_, 5.f, 0.6f) / gClk.densMul;
    return gClk.resolvePeriodSamples(sec * SR);
  }
  void drop(int off, float ampMul, float pitchMul) {
    V& v = grabVoice(v_);
    float f0 = 1600.f * powf(0.25f, size_) * srng_.range(0.8f, 1.25f) * pitchMul;
    float lenSec = (0.03f + 0.09f * size_) * srng_.range(0.8f, 1.2f);
    v.on = true; v.delay = off; v.t = 0;
    v.len = (int)(lenSec * SR);
    v.atk = ms2s(3);   // drops are only 30..120 ms long; 3 ms keeps the "plink"
    v.ph = 0.f;
    v.inc = f0 * INV_SR;
    v.incMul = powf(srng_.range(1.5f, 2.6f), 1.f / v.len);
    v.dec = powf(0.001f, 1.f / v.len);
    v.amp = srng_.range(0.5f, 1.f) * ampMul * 0.45f;
    if (ampMul >= 1.f && srng_.chance(0.35f))   // small second drop
      drop(off + (int)(srng_.range(0.06f, 0.2f) * SR), 0.4f, pitchMul * srng_.range(1.2f, 1.5f));
  }
  V v_[6] = {};
  float size_ = 0.5f, dens_ = 0.3f, leakFrac_ = 0.f;
  int mode_ = DR_RANDOM;
  bool direct_ = false;
};

Layer* createPulse() { return new PulseLayer(); }
Layer* createCreak() { return new CreakLayer(); }
Layer* createSwell() { return new SwellLayer(); }
Layer* createDrip()  { return new DripLayer(); }
