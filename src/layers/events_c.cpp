// MIASMORA — creature layers: SKITTER, INSECT
#include "../dsp/speed.h"   // first: speed-optimised (v0.32)
#include "event_layer.h"

using namespace dsp;

// ===========================================================================
// SKITTER — many legs: very short band-passed noise ticks (0.5..2 ms) at a
// fast, irregular rate (50..400 /s) that runs, stops and runs again.
// Swarm = how many of these crawl at once.
// ===========================================================================
class SkitterLayer : public EventLayer {
  struct G {
    bool on; int delay, t, len;
    float cd, rate, rateF, segCd, tick, tickDec, amp, invLen;
    Svf f; Rng rr;
  };
 public:
  void reset(uint32_t seed) override {
    for (auto& g : g_) g.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    size_ = c.p[1];
    swarm_ = c.p[2];
    setRate(densityToRate(c.p[0], 0.5f, 15.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) {
      int count = 1 + (int)(swarm_ * 5.f * srng_.uni() + 0.3f * swarm_);
      for (int k = 0; k < count; k++)
        start(off + (k ? (int)(srng_.range(0.f, 0.6f) * SR) : 0));
    });
    for (auto& g : g_) {
      if (!g.on) continue;
      for (int i = 0; i < n; i++) {
        if (g.delay > 0) { g.delay--; continue; }
        float u = (float)g.t * g.invLen;   // v0.32: no per-sample divide
        float env = smooth01(u * 10.f) * smooth01((1.f - u) * 6.6f);
        if ((g.segCd -= 1.f) <= 0.f) {   // run / stop / run
          g.rateF = g.rr.chance(0.2f) ? 0.f : g.rr.range(0.6f, 1.4f);
          g.segCd = g.rr.range(0.08f, 0.3f) * SR;
        }
        if (g.rateF > 0.f && (g.cd -= 1.f) <= 0.f) {
          g.tick = env * g.rr.range(0.4f, 1.f);
          g.cd = g.rr.expo(SR / (g.rate * g.rateF)) + 1.f;
        }
        float x = g.rr.bi() * g.tick;
        g.tick *= g.tickDec;
        if (g.tick < 1e-6f) g.tick = 0.f;   // a decaying multiplier would otherwise stick at the smallest denormal
        out[i] += g.f.bpn(x) * g.amp;
        if (++g.t >= g.len) { g.on = false; break; }
      }
      g.f.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(g_); }   // Summon (v0.43)
 private:
  void start(int off) {
    // Draw from the scheduler stream unconditionally, so a full swarm (which
    // can differ between Strict passes) never shifts the event sequence.
    uint32_t vs = srng_.next();
    G* g = nullptr;
    for (auto& c : g_) if (!c.on) { g = &c; break; }
    if (!g) return;   // the swarm is already full
    g->rr.seed(vs);
    Rng& r = g->rr;
    g->on = true; g->delay = off; g->t = 0;
    g->len = (int)(r.range(0.3f, 2.f) * SR);
    g->invLen = 1.f / (float)g->len;
    g->rate = r.range(50.f, 400.f);
    g->rateF = 1.f; g->segCd = r.range(0.08f, 0.3f) * SR;
    g->cd = 1.f; g->tick = 0.f;
    g->tickDec = expf(-1.f / (SR * (0.0005f + 0.0015f * size_) * r.range(0.8f, 1.2f)));
    g->amp = 0.6f * r.range(0.4f, 1.f);
    g->f.clear();
    g->f.set(expMap(size_, 6000.f, 1600.f) * r.range(0.85f, 1.15f), 1.2f + 0.3f * size_);
  }
  G g_[6] = {};
  float size_ = 0.4f, swarm_ = 0.3f;
};

// ===========================================================================
// INSECT — unpleasant insects (not Enso's crickets)
//   Fly          band-passed saw 150..250 Hz, wandering pitch and level
//   Mosquito     thin, edgy 550..750 Hz whine (narrow pulse, 2.5..3.5 kHz
//                emphasis, fast wing-beat jitter) that swoops in and away
//   Cicada       3..6 kHz band noise, 100..200 Hz AM, swells then stops dead
//   Wrong Chirp  a cricket chirp made far too slow, low and irregular
// ===========================================================================
class InsectLayer : public EventLayer {
  struct I {
    bool on; int delay, t, len, mode;
    uint32_t pass;           // loop pass the individual started in
    float f0, ph, amp;
    float w1p, w1f, w2p, w2f, w3p, w3f;
    float envA, fA;          // control-rate envelope / pitch factor (interpolated)
    float tc, tw;            // mosquito swoop centre / width (fraction of len)
    float amPh, amF;         // cicada AM
    int chCd, pulseLeft, pulseLen, pulseGap, pulseT; bool inPulse;
    float invPulse;
    Svf flt; Rng rr;
  };
 public:
  void reset(uint32_t seed) override {
    for (auto& v : v_) v.on = false;
    loopStart(seed);
  }
  // The Count cap only counts individuals that started in the current pass:
  // ones still singing from the previous Strict pass must not block this
  // pass's own (v0.17). Without loops there is only ever one pass.
  void loopStart(uint32_t seed) override {
    EventLayer::loopStart(seed);
    pass_++;
  }
  void control(const LayerCtl& c, bool) override {
    mode_ = (int)c.p[1] & 3;
    maxCount_ = 1 + (int)(c.p[2] * 3.f + 0.5f);
    setRate(densityToRate(c.p[0], 0.5f, 15.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) { start(off); });
    for (auto& v : v_) {
      if (!v.on) continue;
      for (int i = 0; i < n && v.on; i += CTL) {
        int nb = blk(n, i);
        int j = 0;
        while (j < nb && v.delay > 0) { v.delay--; j++; }
        if (j >= nb) continue;
        int len = nb - j;
        float envB, fB;
        blockTargets(v, len, envB, fB);
        float dE = (envB - v.envA) / len, dF = (fB - v.fA) / len;
        for (; j < nb; j++) {
          float s = sample(v);
          out[i + j] += s * v.envA * atkRamp(v.t, ms2s(10));
          v.envA += dE; v.fA += dF;
          if (++v.t >= v.len) { v.on = false; break; }
        }
        v.envA = envB; v.fA = fB;
      }
      v.flt.flush();
    }
  }

  bool hasFreeVoice() const override { return insectFree(); }   // Summon (v0.43)
  bool insectFree() const {
    int active = 0;
    for (auto& v : v_) if (v.on && v.pass == pass_) active++;
    return active < maxCount_ && anyFree(v_);
  }
 private:
  void start(int off) {
    uint32_t vs = srng_.next();   // unconditional: see SkitterLayer::start()
    int active = 0;
    for (auto& v : v_) if (v.on && v.pass == pass_) active++;
    if (active >= maxCount_) return;
    I* p = nullptr;
    for (auto& v : v_) if (!v.on) { p = &v; break; }
    if (!p) return;
    I& v = *p;
    v.rr.seed(vs);
    Rng& r = v.rr;
    v.on = true; v.delay = off; v.t = 0; v.mode = mode_; v.pass = pass_;
    v.ph = 0.f;
    v.w1p = r.uni(); v.w1f = r.range(0.3f, 1.7f) * INV_SR;
    v.w2p = r.uni(); v.w2f = r.range(2.f, 5.f) * INV_SR;
    v.w3p = r.uni(); v.w3f = r.range(0.2f, 0.9f) * INV_SR;
    v.flt.clear();
    switch (v.mode) {
      case IN_FLY:
        v.len = (int)(r.range(2.f, 8.f) * SR);
        v.f0 = r.range(150.f, 250.f);
        v.flt.set(r.range(350.f, 600.f), 0.9f);
        v.amp = 0.45f;
        break;
      case IN_MOSQUITO:
        v.len = (int)(r.range(1.5f, 5.f) * SR);
        v.f0 = r.range(550.f, 750.f);
        v.tc = r.range(0.3f, 0.7f); v.tw = r.range(0.12f, 0.25f);
        v.flt.set(r.range(2500.f, 3500.f), 1.5f);   // where the ear is most sensitive
        v.amp = 0.45f;
        break;
      case IN_CICADA:
        v.len = (int)(r.range(3.f, 10.f) * SR);
        v.flt.set(r.range(3000.f, 6000.f), 5.f);
        v.amPh = 0.f; v.amF = r.range(100.f, 200.f) * INV_SR;
        v.amp = 0.9f;   // a narrow band of noise carries little energy
        break;
      default:  // wrong chirp
        v.len = (int)(r.range(4.f, 12.f) * SR);
        v.f0 = r.range(700.f, 1400.f);
        v.chCd = (int)(r.range(0.1f, 0.8f) * SR);
        v.inPulse = false; v.pulseLeft = 0; v.pulseT = 0;
        v.amp = 0.28f;
        break;
    }
    float e, f;
    blockTargets(v, 0, e, f);
    v.envA = e; v.fA = f;
  }

  // Envelope and pitch factor at the end of the coming block
  void blockTargets(I& v, int len, float& env, float& fac) {
    float u = (float)(v.t + len) / (float)v.len; if (u > 1.f) u = 1.f;
    v.w1p += v.w1f * len; if (v.w1p >= 1.f) v.w1p -= 1.f;
    v.w2p += v.w2f * len; if (v.w2p >= 1.f) v.w2p -= 1.f;
    v.w3p += v.w3f * len; if (v.w3p >= 1.f) v.w3p -= 1.f;
    float wob = 0.6f * sinL(v.w1p) + 0.4f * sinL(v.w2p);
    switch (v.mode) {
      case IN_FLY: {
        // comes in, circles (lumpy level), leaves; pitch shifts with the circling
        env = smooth01(u * 5.f) * smooth01((1.f - u) * 4.f) * (0.55f + 0.45f * sinL(v.w3p));
        fac = 1.f + 0.04f * wob + 0.02f * sinW(v.w3p + 0.25f);
        break;
      }
      case IN_MOSQUITO: {
        float z = (u - v.tc) / v.tw;
        env = expf(-z * z) * (0.8f + 0.2f * sinL(v.w2p));
        // v0.11: a slow +-2% wobble read as chorus; fast, small wing-beat
        // jitter (+-0.5% every control block) keeps it hard and nervous
        fac = 1.f + 0.005f * v.rr.bi() - 0.015f * z / (1.f + z * z);   // + slight doppler around the pass
        break;
      }
      case IN_CICADA: {
        env = u * sqrtf(u);
        int left = v.len - (v.t + len);
        if (left < ms2s(15)) env *= (left > 0 ? (float)left / ms2s(15) : 0.f);   // stops dead
        fac = 1.f;
        break;
      }
      default: {
        env = smooth01(u * 12.f) * smooth01((1.f - u) * 12.f);
        fac = 1.f + 0.01f * wob;
        break;
      }
    }
  }

  inline float sample(I& v) {
    switch (v.mode) {
      case IN_FLY: {
        float inc = v.f0 * v.fA * INV_SR;
        float s = sawBL(v.ph, inc);
        v.ph += inc; if (v.ph >= 1.f) v.ph -= 1.f;
        return (v.flt.bpn(s) * 0.8f + s * 0.15f) * v.amp;
      }
      case IN_MOSQUITO: {
        float inc = v.f0 * v.fA * INV_SR;
        float t2 = v.ph + 0.18f; if (t2 >= 1.f) t2 -= 1.f;
        float s = 0.5f * (sawBL(v.ph, inc) - sawBL(t2, inc));   // narrow band-limited pulse: rich, buzzy
        v.ph += inc; if (v.ph >= 1.f) v.ph -= 1.f;
        return (v.flt.bpn(s) + s * 0.35f) * v.amp;
      }
      case IN_CICADA: {
        float am = 0.5f + 0.5f * sinL(v.amPh);
        v.amPh += v.amF; if (v.amPh >= 1.f) v.amPh -= 1.f;
        return v.flt.bpn(v.rr.bi()) * am * am * v.amp;
      }
      default: {
        // chirp sequencer: irregular chirps of 3..5 slow pulses
        if (!v.inPulse) {
          if (v.pulseLeft > 0) {
            if (--v.pulseGap <= 0) { v.inPulse = true; v.pulseT = 0; v.ph = 0.f; }
          } else if (--v.chCd <= 0) {
            v.pulseLeft = v.rr.irange(3, 5);
            v.pulseLen = (int)(v.rr.range(0.04f, 0.08f) * SR);
            v.invPulse = 1.f / (float)v.pulseLen;
            v.inPulse = true; v.pulseT = 0; v.ph = 0.f;
            v.chCd = (int)(v.rr.range(0.6f, 2.2f) * SR);
          }
          if (!v.inPulse) return 0.f;
        }
        float pu = (float)v.pulseT * v.invPulse;   // v0.32: no per-sample divides
        float e = atkRamp(v.pulseT, ms2s(8));
        int left = v.pulseLen - v.pulseT;
        if (left < ms2s(8)) e *= (float)left * (1.f / (float)ms2s(8));
        float inc = v.f0 * v.fA * (1.f - 0.08f * pu) * INV_SR;   // sags within each pulse
        float s = sinL(v.ph) * e;
        v.ph += inc; if (v.ph >= 1.f) v.ph -= 1.f;
        if (++v.pulseT >= v.pulseLen) {
          v.inPulse = false;
          v.pulseLeft--;
          v.pulseGap = (int)(v.rr.range(0.03f, 0.06f) * SR);
        }
        return s * v.amp;
      }
    }
  }

  I v_[8] = {};   // up to 4 per pass, plus ones carried over from the previous pass
  int mode_ = IN_FLY, maxCount_ = 2;
  uint32_t pass_ = 0;
};

Layer* createSkitter() { return new SkitterLayer(); }
Layer* createInsect()  { return new InsectLayer(); }
