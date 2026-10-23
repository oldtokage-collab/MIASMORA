// MIASMORA — machines and metal (v0.14): MACHINE (drone), DRILL, CLANG (events)
#include "../dsp/speed.h"   // first: speed-optimised (v0.32)
#include "event_layer.h"

using namespace dsp;

static inline float smoothTo(float cur, float tgt, bool snap, float k = 0.2f) {
  return snap ? tgt : cur + (tgt - cur) * k;
}

// ===========================================================================
// MACHINE — something mechanical running somewhere in the building
//   Motor   rotor noise in narrow bands at 8x / 16x the rotation (v0.15: was
//           pure sine partials, which read as a musical note), a thrum at
//           the rotation rate, bearing rumble, brush hiss, faint mains hum
//   Engine  a low idling engine: firing pulses (12..40 Hz) striking a body
//           resonance, with rough, sometimes misfiring combustion
// Speed = rotation / firing rate. Load = how laboured and rough it runs.
// Strict-safe: the rotation is a Phasor at a constant rate and the speed
// wobble is applied as phase modulation from two Lfos (as in SUB).
// ===========================================================================
class MachineLayer : public Layer {
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0x3AC4u);
    w1_.init(init.uni(), 7.3f);
    w2_.init(init.uni(), 2.9f);
    a1_.init(init.uni(), 11.f);
    humHz_ = init.chance(0.5f) ? 50.f : 60.f;
    b1Hz_ = init.range(90.f, 130.f);
    b2Hz_ = init.range(250.f, 400.f);
    rot_.reset(); hum_.reset();
    body1_.clear(); body2_.clear(); rumble_.clear(); brush_.clear(); out_.clear();
    wb1_.clear(); wb2_.clear(); bear_.clear();
    bear_.set(250.f);
    body1_.set(b1Hz_, 0.03f);
    body2_.set(b2Hz_, 0.01f);
    rumble_.set(120.f);
    brush_.set(2500.f);
    out_.set(5000.f);
    burst_ = 0.f;
    first_ = true;
    loopStart(seed);
  }
  void loopStart(uint32_t seed) override { nrng_.seed(seed); }
  void control(const LayerCtl& c, bool snap) override {
    mode_ = (int)c.p[0] == MC_ENGINE ? MC_ENGINE : MC_MOTOR;
    speed_ = smoothTo(speed_, c.p[1], snap, 0.1f);
    load_ = smoothTo(load_, c.p[2], snap);
  }
  void render(float* out, int n) override {
    const bool engine = mode_ == MC_ENGINE;
    const float bdec = expf(-1.f / (SR * 0.0015f));
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      w1_.advance(i, nb); w2_.advance(i, nb); a1_.advance(i, nb);
      float fr = engine ? 12.f + 28.f * speed_ : 20.f + 60.f * speed_;
      rot_.setHz(fr);
      hum_.setHz(humHz_);
      if (!engine) { wb1_.set(8.f * fr, 4.f); wb2_.set(16.f * fr, 4.f); }
      // speed wobble as phase modulation: dev*sin -> -dev*T/(2pi)*cos (cycles)
      float dev = (0.01f + 0.03f * load_) * gClk.lfoMul;
      float T1 = gClk.resolvePeriodSamples(w1_.period * SR) * INV_SR;
      float T2 = gClk.resolvePeriodSamples(w2_.period * SR) * INV_SR;
      float off1 = -fr * dev * (0.7f * T1 * w1_.cos() + 0.3f * T2 * w2_.cos()) * (1.f / TWO_PI_F);
      float amp1 = 1.f - 0.15f * gClk.lfoMul * a1_.uni();
      // prevP_ one cycle back: the very first sample fires, exactly like the
      // start of every later Strict pass (where the rotation phase wraps)
      if (first_) { off_ = off1; amp_ = amp1; prevP_ = off1 + 1.f; first_ = false; }
      float dOff = (off1 - off_) / nb, dAmp = (amp1 - amp_) / nb;
      for (int j = 0; j < nb; j++) {
        float p = rot_.tick() + off_;
        float y;
        if (engine) {
          // a firing whenever the (modulated) phase crosses a whole cycle
          if (floorf(p) != floorf(prevP_)) {
            if (!(nrng_.uni() < 0.06f * load_))   // occasional misfire under load
              burst_ = (0.7f + 0.3f * nrng_.uni()) * (1.f - 0.4f * load_ * nrng_.uni());
          }
          float x = nrng_.bi() * burst_;
          burst_ *= bdec;
          if (burst_ < 1e-6f) burst_ = 0.f;
          y = body1_.tick(x) * 1.0f + body2_.tick(x) * 0.5f + rumble_.lp(nrng_.bi()) * (0.08f + 0.1f * load_);
          y *= 0.15f;   // the body resonances add up over many firings
        } else {
          float nz = nrng_.bi();
          float whine = wb1_.bpn(nz) + 0.5f * wb2_.bpn(nz);
          float am = 1.f + (0.25f + 0.35f * load_) * sinW(p);   // thrum at the rotation rate
          float hp = hum_.tick();
          float hm = 0.5f * sinL(hp) + 0.3f * sinW(2.f * hp) + 0.2f * sinW(3.f * hp);
          float bearing = bear_.lp(nrng_.bi()) * (0.4f + 0.6f * load_) * (1.f + 0.3f * sinW(p));
          float br = brush_.hp(nrng_.bi()) * (0.05f + 0.25f * load_) * (0.6f + 0.4f * sinW(4.f * p));
          y = (hm * 0.175f + whine * am * MOTOR_WHINE + bearing * 2.f + br) * 0.85f;
        }
        prevP_ = p;
        out[i + j] = out_.lp(y) * amp_ * 0.8f;
        off_ += dOff; amp_ += dAmp;
      }
      off_ = off1; amp_ = amp1;
    }
    body1_.flush(); body2_.flush(); rumble_.flush(); brush_.flush(); out_.flush();
    wb1_.flush(); wb2_.flush(); bear_.flush();
  }

 private:
  Rng nrng_;
  Lfo w1_, w2_, a1_;
  Phasor rot_, hum_;
  Reson body1_, body2_;
  OnePole rumble_, brush_, out_, bear_;
  Svf wb1_, wb2_;
  static constexpr float MOTOR_WHINE = 0.9f;
  float humHz_ = 50.f, b1Hz_ = 110.f, b2Hz_ = 300.f;
  int mode_ = MC_MOTOR;
  float speed_ = 0.4f, load_ = 0.3f;
  float off_ = 0.f, amp_ = 1.f, prevP_ = 0.f, burst_ = 0.f;
  bool first_ = true;
};

// ===========================================================================
// DRILL — a power drill / grinder at work, somewhere
// 1..4 runs: spin up ("gyiiin"), bite into the material (the motor bogs down
// and grinding noise rises), let go, spin up again, and finally wind down.
// v0.15: v0.14 glided the motor pitch up to 35 % over slow load changes, which
// sounded like a wah pedal. Load now jitters fast and only costs ~8 % speed,
// and the sound sits on gear / brush / grinding noise rather than the motor tone.
// ===========================================================================
class DrillLayer : public EventLayer {
  struct V {
    bool on; int delay, t, stage, runsLeft;
    float segLeft, speed, spT, load, loadT, lcd, ph, gph, F, amp;
    Svf grind, gear; OnePole lp, brush; Rng r;
  };
  enum { RUN = 0, GAP, END };
 public:
  void reset(uint32_t seed) override {
    for (auto& v : v_) v.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    pitch_ = c.p[1];
    grind_ = c.p[2];
    setRate(densityToRate(c.p[0], 0.3f, 6.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) {
      uint32_t s = srng_.next();   // drawn even when busy (Strict determinism)
      start(off, s);
    });
    for (auto& v : v_) if (v.on) renderVoice(v, out, n);
  }

  bool hasFreeVoice() const override { return anyFree(v_); }   // Summon (v0.43)
 private:
  void renderVoice(V& v, float* out, int n) {
    const float upK = 1.f - expf(-1.f / (0.06f * SR));
    const float downK = 1.f - expf(-1.f / (0.3f * SR));
    for (int i = 0; i < n; i++) {
      if (v.delay > 0) { v.delay--; continue; }
      // run / gap / end sequencing
      if ((v.segLeft -= 1.f) <= 0.f) {
        if (v.stage == RUN) {
          if (--v.runsLeft > 0) { v.stage = GAP; v.segLeft = v.r.range(0.2f, 0.8f) * SR; v.spT = 0.f; }
          else { v.stage = END; v.segLeft = v.r.range(0.4f, 0.8f) * SR; v.spT = 0.f; }
        } else if (v.stage == GAP) {
          v.stage = RUN; v.segLeft = v.r.range(0.4f, 2.f) * SR; v.spT = 1.f;
        } else if (v.speed < 0.005f) {
          v.on = false; break;
        }
      }
      // biting into the material now and then
      if ((v.lcd -= 1.f) <= 0.f) {
        v.lcd = v.r.range(0.03f, 0.15f) * SR;   // fast, ragged load changes
        v.loadT = v.r.chance(0.3f + 0.5f * grind_) ? v.r.range(0.3f, 1.f) * (0.3f + 0.7f * grind_) : v.r.range(0.f, 0.2f);
      }
      v.load += (v.loadT - v.load) * 0.01f;
      float target = v.spT * (1.f - 0.08f * v.load);
      v.speed += (target - v.speed) * (target > v.speed ? upK : downK);
      float f = v.F * v.speed;
      v.ph += f * INV_SR; if (v.ph >= 1.f) v.ph -= 1.f;
      v.gph += f * 0.25f * INV_SR; if (v.gph >= 1.f) v.gph -= 1.f;
      float motor = 0.6f * sinL(v.ph) + 0.35f * sinW(2.f * v.ph) + 0.25f * sinW(3.f * v.ph) + 0.15f * sinW(5.f * v.ph);
      float gb = 0.5f + 0.5f * sinL(v.gph);
      float gear = v.gear.bpn(v.r.bi()) * gb * gb * v.speed;              // gear chatter
      float br = v.brush.hp(v.r.bi()) * v.speed * 0.15f;                  // brush hiss
      float g = v.grind.bpn(v.r.bi()) * v.load * v.speed * 1.2f;
      float y = motor * sqrtf(v.speed) * 0.15f + gear * 0.6f + br + g;
      out[i] += v.lp.lp(y) * v.amp * atkRamp(v.t, ms2s(10));
      v.t++;
    }
    v.grind.flush(); v.gear.flush(); v.lp.flush(); v.brush.flush();
  }
  // Six slots (v0.17): with one, a run carried over a Strict loop boundary
  // blocked the next pass's own run. Runs can outlast an 8 s loop, so the
  // carried-over ones and the new ones must all fit.
  void start(int off, uint32_t s) {
    V* p = nullptr;
    for (auto& c : v_) if (!c.on) { p = &c; break; }
    if (!p) return;
    V& v = *p;
    v.r.seed(s);
    v.on = true; v.delay = off; v.t = 0;
    v.stage = RUN; v.runsLeft = v.r.irange(1, 4);
    v.segLeft = v.r.range(0.4f, 2.f) * SR;
    v.speed = 0.f; v.spT = 1.f;
    v.load = 0.f; v.loadT = 0.f; v.lcd = v.r.range(0.2f, 0.6f) * SR;
    v.ph = 0.f; v.gph = 0.f;
    v.F = expMap(pitch_, 350.f, 1100.f) * v.r.range(0.9f, 1.1f);
    v.amp = 0.8f * v.r.range(0.7f, 1.f);
    v.grind.clear(); v.grind.set(v.r.range(2500.f, 4500.f), 1.5f);
    v.lp.clear(); v.lp.set(7000.f);
    v.gear.clear(); v.gear.set(v.r.range(1200.f, 2000.f), 1.5f);
    v.brush.clear(); v.brush.set(3000.f);
  }
  V v_[6] = {};
  float pitch_ = 0.5f, grind_ = 0.5f;
};

// ===========================================================================
// CLANG — junk metal being struck (v0.15 rewrite)
// v0.14 rang a few sparse modes for a long time (glockenspiel bar ratios,
// glassy chain clinks): an instrument. Struck scrap metal instead has many
// densely packed modes that beat against each other, a short ring, and a
// rough, noisy impact:
//   Pipe   16 modes in detuned pairs over ~4 octaves, 0.3..0.8 s, often hit again
//   Sheet  16 dense low modes plus an amplitude-dependent rattle ("biri-biri")
// (v0.14..v0.15 also had "Chain"; it never read as a chain and was removed in v0.16.)
// Each voice is soft-limited so repeated hits cannot pile up into clipping.
// Size = bigger / lower / longer. Distance = level + low-pass.
// ===========================================================================
class ClangLayer : public EventLayer {
  static constexpr int NM = 16;
  struct V {
    bool on; int delay, t, len, obj, nm, hitsLeft;
    int32_t nextHit; float hitInt, force;
    float burst, bdec, crash, crashDec, crash0, g[NM], gain, buzz;
    Reson m[NM];
    OnePole crashHp, buzzHp, lp1, lp2;
    Rng r;
  };
 public:
  void reset(uint32_t seed) override {
    for (auto& v : v_) v.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    obj_ = (int)c.p[1];
    if (obj_ < 0 || obj_ > CL_SHEET) obj_ = CL_PIPE;
    size_ = c.p[2];
    dist_ = c.p[3];
    setRate(densityToRate(c.p[0], 0.3f, 8.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) { start(off, srng_.next()); });
    for (auto& v : v_) {
      if (!v.on) continue;
      for (int i = 0; i < n; i++) {
        if (v.delay > 0) { v.delay--; continue; }
        if (v.hitsLeft > 0 && v.t >= v.nextHit) {   // struck again
          float k = v.r.range(0.6f, 1.f);
          v.burst = v.force * k / sqrtf(0.5f * 0.0008f * SR);
          v.crash = v.crash0 * k;
          v.hitsLeft--;
          v.nextHit = v.t + (int32_t)(v.hitInt * v.r.range(0.85f, 1.15f) * SR);
        }
        float x = v.r.bi() * v.burst;
        v.burst *= v.bdec;
        if (v.burst < 1e-6f) v.burst = 0.f;   // denormal guard
        float y = 0.f;
        for (int k = 0; k < v.nm; k++) y += v.m[k].tick(x) * v.g[k];
        if (v.buzz > 0.f) y += v.buzzHp.hp(v.r.bi()) * fabsf(y) * v.buzz;   // loose-contact rattle
        if (v.crash > 0.f) {                                                // noisy impact
          y += v.crashHp.hp(v.r.bi()) * v.crash;
          v.crash *= v.crashDec;
          if (v.crash < 1e-5f) v.crash = 0.f;
        }
        y = softSat(y * 0.7f) * (1.f / 0.7f);   // repeated hits cannot pile up
        y = v.lp2.lp(v.lp1.lp(y));
        out[i] += y * v.gain;
        if (++v.t >= v.len) { v.on = false; break; }
      }
      for (auto& m : v.m) m.flush();
      v.crashHp.flush(); v.buzzHp.flush(); v.lp1.flush(); v.lp2.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(v_); }   // Summon (v0.43)
 private:
  void start(int off, uint32_t s) {
    V& v = grabVoice(v_);
    v.r.seed(s);
    Rng& r = v.r;
    v.on = true; v.delay = off; v.t = 0;
    v.obj = obj_;
    v.force = r.range(0.6f, 1.f);
    v.hitsLeft = 0; v.nextHit = 0x3FFFFFFF; v.hitInt = 1.f;
    v.buzz = 0.f;
    for (auto& m : v.m) m.clear();
    v.crashHp.clear(); v.buzzHp.clear();
    v.buzzHp.set(1500.f);
    float burstSec = 0.0008f, crashSec = 0.03f, crashHz = 1500.f;
    float sz = size_;
    v.nm = 0;
    if (v.obj == CL_PIPE) {
      float f0 = expMap(1.f - sz, 250.f, 1200.f) * r.range(0.9f, 1.1f);
      float D = r.range(0.3f, 0.8f) * (0.6f + 0.8f * sz);
      for (int k = 0; k < NM / 2; k++) {
        float fk = f0 * expf(logf(14.f) * ((float)k + r.uni()) / (NM / 2));
        if (fk > 9000.f) break;
        float dec = D / (1.f + 0.5f * k), g = r.range(0.4f, 1.f) / (1.f + 0.25f * k);
        v.m[v.nm].set(fk, dec);                                  v.g[v.nm++] = g;
        v.m[v.nm].set(fk * (1.f + r.range(0.004f, 0.02f)), dec); v.g[v.nm++] = g * r.range(0.6f, 1.f);   // beating pair
      }
      v.crash0 = 0.5f;
      crashSec = r.range(0.02f, 0.05f);
      v.len = (int)(D * 5.f * SR);
      if (r.chance(0.5f)) {   // someone keeps hitting it
        v.hitsLeft = r.irange(1, 4);
        v.hitInt = r.range(0.35f, 0.9f);
        v.nextHit = (int32_t)(v.hitInt * SR);
        v.len += (int)(v.hitsLeft * v.hitInt * 1.15f * SR);
      }
    } else {   // sheet
      float f0 = expMap(1.f - sz, 90.f, 400.f);
      for (int k = 0; k < NM; k++) {
        v.m[v.nm].set(f0 * expf(logf(30.f) * r.uni()), r.range(0.2f, 0.6f) * (0.6f + 0.8f * sz));
        v.g[v.nm++] = r.range(0.2f, 0.7f);
      }
      v.buzz = 2.f * r.range(0.6f, 1.f);
      v.crash0 = 0.6f; crashSec = 0.06f; crashHz = 800.f;
      burstSec = 0.003f;
      v.len = (int)((0.6f + 0.8f * sz) * 3.f * SR);
      if (r.chance(0.3f)) { v.hitsLeft = 1; v.hitInt = r.range(0.6f, 1.5f); v.nextHit = (int32_t)(v.hitInt * SR); v.len += (int)(v.hitInt * SR); }
    }
    v.crash = v.crash0;
    v.crashDec = expf(-1.f / (SR * crashSec));
    v.crashHp.set(crashHz);
    v.bdec = expf(-1.f / (SR * burstSec));
    v.burst = v.force / sqrtf(0.5f * burstSec * SR);
    float cut = 8000.f * powf(600.f / 8000.f, dist_);
    v.lp1.clear(); v.lp2.clear();
    v.lp1.set(cut); v.lp2.set(cut);
    static const float OBJ_GAIN[2] = {1.1f, 0.8f};   // pipe / sheet
    v.gain = OBJ_GAIN[v.obj] / (1.f + 2.f * dist_);
  }
  V v_[3] = {};
  int obj_ = CL_PIPE;
  float size_ = 0.5f, dist_ = 0.4f;
};
Layer* createMachine() { return new MachineLayer(); }
Layer* createDrill()   { return new DrillLayer(); }
Layer* createClang()   { return new ClangLayer(); }
