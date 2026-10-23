// MIASMORA — gesture layers: SCRAPE, DRAG, STEPS
//
// All of these follow the Enso lesson: a struck / rubbed surface is a
// broadband excitation (noise grains, impulses) driving resonators, never a
// pitched oscillator.
#include "../dsp/speed.h"   // first: speed-optimised (v0.32)
#include "event_layer.h"

using namespace dsp;

// ===========================================================================
// SCRAPE — 0.5..3 s gesture: an irregular impulse train (30..300 Hz) through
// 3 band-pass resonators. Material blends Wood (low, damped) -> Metal (1..4 kHz, ringing).
// ===========================================================================
class ScrapeLayer : public EventLayer {
  struct V {
    bool on; int delay, t, len, tail;
    float cd, rate, wp1, wf1, wp2, wf2, burst, bdec, amp, press, invLen;
    Reson r[3]; OnePole bl; Rng rr;
  };
 public:
  void reset(uint32_t seed) override {
    for (auto& v : v_) v.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    material_ = c.p[1];
    pressure_ = c.p[2];
    setRate(densityToRate(c.p[0], 0.5f, 12.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) { trigger(off); });
    for (auto& v : v_) {
      if (!v.on) continue;
      for (int i = 0; i < n && v.on; i += CTL) {
        int nb = blk(n, i);
        int j = 0;
        while (j < nb && v.delay > 0) { v.delay--; j++; }
        if (j >= nb) continue;
        // v0.32: no per-sample divides (x / 0.15 is a library call on the S3)
        float u0 = (float)v.t * v.invLen;
        float envB = u0 < 1.f ? smooth01(u0 * (1.f / 0.15f)) * smooth01((1.f - u0) * (1.f / 0.3f)) : 0.f;
        v.bl.set(1500.f + 6000.f * envB);   // brighter at the peak of the stroke
        for (; j < nb; j++) {
          float u = (float)v.t * v.invLen;
          float env = u < 1.f ? smooth01(u * (1.f / 0.15f)) * smooth01((1.f - u) * (1.f / 0.3f)) : 0.f;
          if (u < 1.f && (v.cd -= 1.f) <= 0.f) {
            v.burst = env * v.press * (0.6f + 0.4f * v.rr.uni());
            v.bdec = expf(-1.f / (SR * v.rr.range(0.0003f, 0.001f)));
            float rt = v.rate * (0.5f + 0.8f * env) * (1.f + 0.3f * sinL(v.wp1) + 0.15f * sinL(v.wp2));
            if (rt < 10.f) rt = 10.f;
            v.cd = (SR / rt) * (1.f + 0.2f * v.rr.bi());
          }
          v.wp1 += v.wf1; if (v.wp1 >= 1.f) v.wp1 -= 1.f;
          v.wp2 += v.wf2; if (v.wp2 >= 1.f) v.wp2 -= 1.f;
          float x = v.bl.lp(v.rr.bi() * v.burst) + v.rr.bi() * env * 0.04f * v.press;
          v.burst *= v.bdec;
          if (v.burst < 1e-6f) v.burst = 0.f;   // denormal guard
          float y = v.r[0].tick(x) + 0.7f * v.r[1].tick(x) + 0.5f * v.r[2].tick(x);
          out[i + j] += y * v.amp;
          if (++v.t >= v.len + v.tail) { v.on = false; break; }
        }
      }
      for (auto& r : v.r) r.flush();
      v.bl.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(v_); }   // Summon (v0.43)
 private:
  void trigger(int off) {
    static const float WOOD[3]  = {350.f, 620.f, 980.f};
    static const float METAL[3] = {1200.f, 2300.f, 3700.f};
    V& v = grabVoice(v_);
    v.on = true; v.delay = off; v.t = 0;
    v.len = (int)(srng_.range(0.5f, 3.f) * SR);
    v.invLen = 1.f / (float)v.len;
    v.tail = ms2s(150);
    v.rate = expMap(dsp::clampf(0.25f + 0.45f * pressure_ + 0.3f * srng_.uni(), 0.f, 1.f), 30.f, 300.f);
    v.cd = 1.f;
    v.wf1 = srng_.range(0.5f, 2.f) * INV_SR;  v.wp1 = srng_.uni();
    v.wf2 = srng_.range(3.f, 7.f) * INV_SR;   v.wp2 = srng_.uni();
    v.burst = 0.f; v.bdec = 0.9f;
    v.press = 0.4f + 0.6f * pressure_;
    float dec = 0.008f * powf(0.07f / 0.008f, material_);
    // overlapping rings add up: scale by how many impulses one ring spans
    v.amp = 0.12f / sqrtf(1.f + v.rate * dec) * srng_.range(0.7f, 1.f);
    for (int k = 0; k < 3; k++) {
      float f = WOOD[k] * powf(METAL[k] / WOOD[k], material_) * srng_.range(0.9f, 1.1f);
      v.r[k].clear();
      v.r[k].set(f, dec * (1.f - 0.15f * k));
    }
    v.bl.clear();
    v.rr.seed(srng_.next());
  }
  V v_[2] = {};
  float material_ = 0.5f, pressure_ = 0.5f;
};

// ===========================================================================
// DRAG — something dragged across the floor.
//   Body  2..8 s of band-passed noise with stick-slip grain (5..40 Hz), the
//         band drifting as the thing moves, sometimes ending in a low thud.
//   Pipe  (v0.14, was CREAK "Door" in v0.13) a metal pipe dragged over
//         concrete: a fast stick-slip train (70..300 Hz) whose rate glides, so
//         it squeals with a wavering pitch through bright metal resonances.
// ===========================================================================
class DragLayer : public EventLayer {
  struct V {
    bool on, thud; int delay, t, len, atk, rel, thudAt, end;
    float c0, dir, wph, wf, gcd, grain, grainT, slip, amp, thB, thDec;
    float invLen, invAtk, invRel;   // v0.32: no per-sample divides
    Svf f; Reson th1, th2; Rng rr;
    // Pipe
    bool pipe; float prate, pwf1, pwf2, pwp1, pwp2, pcd, pb, pbdec, pamp, pg1, pg2;
    Reson pr[3];
  };
 public:
  void reset(uint32_t seed) override {
    for (auto& v : v_) v.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    weight_ = c.p[1];
    rough_ = c.p[2];
    object_ = (int)c.p[3] == DG_PIPE ? DG_PIPE : DG_BODY;
    setRate(densityToRate(c.p[0], 0.4f, 8.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) { trigger(off); });
    for (auto& v : v_) {
      if (!v.on) continue;
      if (v.pipe) { renderPipe(v, out, n); continue; }
      for (int i = 0; i < n && v.on; i += CTL) {
        int nb = blk(n, i);
        int j = 0;
        while (j < nb && v.delay > 0) { v.delay--; j++; }
        if (j >= nb) continue;
        float u = (float)v.t * v.invLen; if (u > 1.f) u = 1.f;
        v.wph += v.wf * nb; if (v.wph >= 1.f) v.wph -= 1.f;
        float c = v.c0 * powf(2.f, 0.8f * (u - 0.5f) * v.dir + 0.15f * sinL(v.wph));
        v.f.set(c, 1.5f);
        for (; j < nb; j++) {
          float env = 0.f;
          if (v.t < v.len) {
            env = smooth01((float)v.t * v.invAtk) * smooth01((float)(v.len - v.t) * v.invRel);
            if ((v.gcd -= 1.f) <= 0.f) {      // stick-slip grain
              v.grainT = v.rr.range(0.25f, 1.f);
              v.gcd = (SR / v.slip) * (1.f + 0.3f * v.rr.bi());
            }
          }
          v.grain += (v.grainT - v.grain) * 0.01f;
          float am = (1.f - rough_) + rough_ * v.grain;
          float x = v.rr.bi();
          float lp, bp;
          v.f.tick(x, lp, bp);
          float y = (bp * v.f.k + 0.3f * lp) * env * am * 0.6f;
          if (v.thud && v.t == v.thudAt) v.thB = 0.15f;   // ~1/sqrt(N/2) for the 4 ms burst
          if (v.thB > 0.0005f) {
            float xb = v.rr.bi() * v.thB;
            v.thB *= v.thDec;
            y += v.th1.tick(xb) * 0.9f + v.th2.tick(xb) * 0.5f;
          } else if (v.thud && v.t > v.thudAt) {
            y += v.th1.tick(0.f) * 0.9f + v.th2.tick(0.f) * 0.5f;
          }
          out[i + j] += y * v.amp;
          if (++v.t >= v.end) { v.on = false; break; }
        }
      }
      v.f.flush(); v.th1.flush(); v.th2.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(v_); }   // Summon (v0.43)
 private:
  void trigger(int off) {
    V& v = grabVoice(v_);
    v.on = true; v.delay = off; v.t = 0;
    v.len = (int)(srng_.range(2.f, 8.f) * SR);
    v.atk = (int)(srng_.range(0.3f, 1.f) * SR);
    v.rel = (int)(srng_.range(0.3f, 1.f) * SR);
    v.c0 = 100.f * powf(6.f, (1.f - weight_) * 0.7f + 0.3f * srng_.uni());
    v.dir = srng_.chance(0.5f) ? 1.f : -1.f;
    v.wph = srng_.uni(); v.wf = srng_.range(0.1f, 0.4f) * INV_SR;
    v.slip = 5.f + 35.f * (0.3f * rough_ + 0.7f * srng_.uni());
    v.gcd = 1.f; v.grain = 1.f; v.grainT = 1.f;
    v.amp = 0.7f * srng_.range(0.7f, 1.f);
    v.thud = srng_.chance(0.3f + 0.4f * weight_);
    v.thudAt = v.len - v.rel / 2;
    v.thB = 0.f;
    v.thDec = expf(-1.f / (SR * 0.004f));
    v.th1.clear(); v.th2.clear();
    v.th1.set(srng_.range(55.f, 110.f), 0.18f);
    v.th2.set(srng_.range(160.f, 240.f), 0.08f);
    v.end = v.len + (v.thud ? ms2s(500) : 0);
    v.f.clear();
    v.pipe = object_ == DG_PIPE;
    if (v.pipe) {
      // A long squealing stroke; heavier = lower. (Same sound as v0.13's CREAK Door.)
      float pitch = 1.f - weight_;
      v.len = (int)(srng_.range(1.2f, 4.5f) * SR);
      v.end = v.len + ms2s(250);
      v.thud = false;
      v.prate = srng_.range(70.f, 160.f) * powf(2.f, pitch * 1.2f - 0.6f);
      v.pwf1 = srng_.range(0.2f, 0.6f) * INV_SR;
      v.pwf2 = srng_.range(1.f, 3.f) * INV_SR;
      v.pwp1 = srng_.uni(); v.pwp2 = srng_.uni();
      v.pcd = 1.f; v.pb = 0.f;
      v.pbdec = expf(-1.f / (SR * 0.00025f));
      v.pamp = 0.13f * srng_.range(0.7f, 1.f);
      float h = srng_.range(900.f, 1300.f);
      for (auto& r : v.pr) r.clear();
      v.pr[0].set(h, 0.012f);
      v.pr[1].set(h * 1.73f, 0.008f);                  v.pg1 = 0.6f;
      v.pr[2].set(srng_.range(250.f, 380.f), 0.03f);   v.pg2 = 0.5f;
    }
    v.invLen = 1.f / (float)v.len; v.invAtk = 1.f / (float)v.atk; v.invRel = 1.f / (float)v.rel;
    v.rr.seed(srng_.next());
  }
  void renderPipe(V& v, float* out, int n) {
    for (int i = 0; i < n; i++) {
      if (v.delay > 0) { v.delay--; continue; }
      float u = (float)v.t * v.invLen;
      float env = u < 1.f ? smooth01(u * 4.f) * smooth01((1.f - u) * 3.3f) : 0.f;
      if (u < 1.f && (v.pcd -= 1.f) <= 0.f) {   // one slip
        v.pb = env * (0.5f + 0.5f * v.rr.uni());
        float rt = v.prate * (1.f + 0.45f * sinL(v.pwp1) + 0.15f * sinL(v.pwp2)) * (0.75f + 0.5f * env);
        if (rt < 5.f) rt = 5.f;
        v.pcd = (SR / rt) * (1.f + 0.05f * v.rr.bi());
      }
      v.pwp1 += v.pwf1; if (v.pwp1 >= 1.f) v.pwp1 -= 1.f;
      v.pwp2 += v.pwf2; if (v.pwp2 >= 1.f) v.pwp2 -= 1.f;
      float x = v.rr.bi() * v.pb;
      v.pb *= v.pbdec;
      if (v.pb < 1e-6f) v.pb = 0.f;   // denormal guard
      float y = v.pr[0].tick(x) + v.pg1 * v.pr[1].tick(x) + v.pg2 * v.pr[2].tick(x);
      out[i] += (y + x * 0.08f) * v.pamp;
      if (++v.t >= v.end) { v.on = false; break; }
    }
    for (auto& r : v.pr) r.flush();
  }
  V v_[2] = {};
  float weight_ = 0.5f, rough_ = 0.5f;
  int object_ = DG_BODY;
};

// ===========================================================================
// STEPS — 4..12 footsteps per gesture, hard-soled shoes ("katsu, katsu").
//   heel  a very short, bright noise click through heavily damped 1.2..5 kHz
//         resonators (the "ka-tsu") over a brief low body thump
//   toe   a quieter, shorter second click 80..130 ms later, no body
//   floor Wood adds a dull board knock (and sometimes a creak), Concrete is the
//         driest and brightest, Metal rings, Gravel crunches.
// v0.11: the v0.1 model rang pitched resonators on both heel and toe, which
// read as wooden clogs ("karan-koron"). Every resonator is now short and
// re-pitched per step, so nothing reads as a tone.
// v0.12: the click resonators still rang at Q ~60 (a struck bar: kalimba /
// xylophone). Q ~ pi * f * decay, so they now decay in 0.25..0.4 ms (Q ~2..3,
// an unpitched "tsk") and the body in 5..6 ms (Q ~3). Metal keeps its ring on purpose.
// Distance = level + low-pass.
// ===========================================================================
class StepsLayer : public EventLayer {
  struct S {
    bool on; int delay, t, len, toeAt;
    float body, bodyDec, click, clickDec, toeBody, toeClick;
    float raw, gh, g1, g2, gain;
    Reson heel, s1, s2;
    OnePole hpk;              // keeps the raw click bright
    // gravel micro-clicks
    int gN; float gcd, gStep, grit, gritDec;
    // wood creak
    bool creak; int crStart, crEnd; float crCd, crRate, crB;
    Reson cr;
    OnePole lp1, lp2; Rng rr;
  };  // A walker. It owns its random stream, so a gesture that carries across a
  // Strict loop boundary does not disturb the scheduler stream of the next pass.
  struct Gesture { bool on; int steps, idx, pauseAt; bool endAfterPause; int32_t next; float interval; float dist0; Rng gr; };
 public:
  void reset(uint32_t seed) override {
    for (auto& s : s_) s.on = false;
    for (auto& g : g_) g.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    surface_ = (int)c.p[1] & 3;
    dir_ = (int)c.p[2]; if (dir_ < 0 || dir_ > 2) dir_ = DIR_PASS;
    pace_ = c.p[3];
    muffle_ = c.p[4];
    setRate(densityToRate(c.p[0], 0.3f, 6.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) {
      uint32_t gs = srng_.next();   // drawn even when both walkers are busy (Strict determinism)
      for (auto& g : g_) if (!g.on) { startGesture(g, off, gs); break; }
    });
    for (auto& g : g_) {
      while (g.on && g.next < n) {
        int off = g.next < 0 ? 0 : g.next;
        step(g, off);
        float wait = g.interval * g.gr.range(0.95f, 1.05f);
        if (g.idx == g.pauseAt) {   // the walker stops... and may not go on
          wait += g.gr.range(1.5f, 4.f);
          if (g.endAfterPause) g.on = false;
        }
        g.idx++;
        if (g.idx >= g.steps) g.on = false;
        g.next += (int32_t)(wait * SR);
      }
      if (g.on) g.next -= n;
    }
    for (auto& s : s_) {
      if (!s.on) continue;
      for (int i = 0; i < n; i++) {
        if (s.delay > 0) { s.delay--; continue; }
        if (s.t == s.toeAt) { s.body += s.toeBody; s.click += s.toeClick; }
        float xb = s.rr.bi() * s.body;
        float xk = s.rr.bi() * s.click;
        s.body *= s.bodyDec;   if (s.body < 1e-6f) s.body = 0.f;     // denormal guards
        s.click *= s.clickDec; if (s.click < 1e-6f) s.click = 0.f;
        if (s.gN > 0 && (s.gcd -= 1.f) <= 0.f) {
          s.grit = s.rr.range(0.2f, 1.f);
          s.gcd = s.gStep * s.rr.range(0.3f, 1.7f);
          s.gN--;
        }
        if (s.grit > 0.0005f) { xk += s.rr.bi() * s.grit * 0.5f; s.grit *= s.gritDec; }
        else s.grit = 0.f;
        float y = s.heel.tick(xb) * s.gh + s.s1.tick(xk) * s.g1 + s.s2.tick(xk) * s.g2
                + s.hpk.hp(xk) * s.raw;
        if (s.creak) {
          if (s.t >= s.crStart && s.t < s.crEnd && (s.crCd -= 1.f) <= 0.f) {
            s.crB = s.rr.range(0.15f, 0.35f);
            s.crCd = (SR / s.crRate) * (1.f + 0.25f * s.rr.bi());
          }
          y += s.cr.tick(s.rr.bi() * s.crB) * 0.5f;
          s.crB *= 0.94f; if (s.crB < 1e-6f) s.crB = 0.f;
        }
        y = s.lp2.lp(s.lp1.lp(y));
        out[i] += y * s.gain;
        if (++s.t >= s.len) { s.on = false; break; }
      }
      s.heel.flush(); s.s1.flush(); s.s2.flush(); s.cr.flush();
      s.hpk.flush(); s.lp1.flush(); s.lp2.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(g_); }   // Summon (v0.43)
 private:  void startGesture(Gesture& g, int off, uint32_t gs) {
    g.gr.seed(gs);
    g.on = true;
    g.steps = g.gr.irange(4, 12);
    g.idx = 0;
    g.next = off;
    g.interval = 0.9f - 0.4f * pace_;
    g.pauseAt = g.gr.chance(0.25f) ? g.gr.irange(1, g.steps - 2) : -1;
    g.endAfterPause = g.gr.chance(0.4f);
    g.dist0 = g.gr.range(0.f, 0.15f);   // not every walker comes equally close
  }
  float distance(const Gesture& g) const {
    float u = g.steps > 1 ? (float)g.idx / (float)(g.steps - 1) : 0.f;
    float d;
    switch (dir_) {
      case DIR_APPROACH: d = 1.f - 0.85f * u; break;
      case DIR_RECEDE:   d = 0.15f + 0.85f * u; break;
      default:           d = 0.15f + 0.85f * fabsf(2.f * u - 1.f); break;
    }
    return clampf(d + g.dist0, 0.f, 1.f);
  }
  void step(Gesture& g, int off) {
    S& s = grabVoice(s_);
    Rng& r = g.gr;
    float d = distance(g);
    s.on = true; s.delay = off; s.t = 0;
    s.len = (int)(0.3f * SR);
    s.toeAt = (int)(r.range(0.08f, 0.13f) * SR);
    s.gN = 0; s.grit = 0.f; s.gritDec = expf(-1.f / (SR * 0.00025f)); s.gcd = 0.f; s.gStep = 1.f;
    s.creak = false; s.crB = 0.f;
    s.heel.clear(); s.s1.clear(); s.s2.clear(); s.cr.clear(); s.hpk.clear();
    s.hpk.set(1500.f);
    float toeB = 0.3f, toeK = 0.35f;
    switch (surface_) {
      case SF_WOOD:      // a dull knock from the boards under the click
        s.heel.set(r.range(120.f, 180.f), 0.006f);   s.gh = 0.8f;
        s.s1.set(r.range(1200.f, 2000.f), 0.0004f);  s.g1 = 0.7f;
        s.s2.set(r.range(2600.f, 3800.f), 0.0003f);  s.g2 = 0.5f;
        s.raw = 0.5f;
        if (r.chance(0.12f)) {
          s.creak = true;
          s.crStart = s.toeAt;
          s.crEnd = s.crStart + (int)(r.range(0.25f, 0.4f) * SR);
          s.crRate = r.range(35.f, 70.f);
          s.crCd = 1.f;
          s.cr.set(r.range(450.f, 750.f), 0.03f);
          s.len = s.crEnd + ms2s(100);
        }
        break;
      case SF_CONCRETE:  // driest, brightest
        s.heel.set(r.range(140.f, 220.f), 0.005f);   s.gh = 0.5f;
        s.s1.set(r.range(1800.f, 2800.f), 0.0003f);  s.g1 = 0.8f;
        s.s2.set(r.range(3500.f, 5000.f), 0.00025f); s.g2 = 0.6f;
        s.raw = 0.8f; toeB = 0.25f; toeK = 0.3f;
        break;
      case SF_METAL:     // grating / stairs: the click sets the metal ringing
        s.heel.set(r.range(110.f, 150.f), 0.03f);   s.gh = 0.6f;
        s.s1.set(r.range(700.f, 1100.f), 0.25f);    s.g1 = 0.35f;
        s.s2.set(r.range(1700.f, 2500.f), 0.18f);   s.g2 = 0.25f;
        s.raw = 0.6f;
        s.len = (int)(0.6f * SR);
        break;
      default:           // gravel: a scatter of tiny grains under a soft step
        s.heel.set(r.range(90.f, 120.f), 0.006f);    s.gh = 0.5f;
        s.s1.set(r.range(1500.f, 2500.f), 0.0004f);  s.g1 = 0.25f;
        s.s2.set(r.range(3000.f, 4500.f), 0.0003f);  s.g2 = 0.2f;
        s.raw = 0.3f; toeB = 0.2f; toeK = 0.2f;
        s.gN = r.irange(15, 40);
        s.gStep = r.range(0.08f, 0.15f) * SR / s.gN;
        s.gcd = 1.f;
        break;
    }
    // Burst lengths: body 2 ms, click 0.4..0.8 ms. A noise burst of N samples
    // builds a sin(w)-normalised resonator up to ~sqrt(N/2), hence the scaling.
    const float bodySec = 0.002f, clickSec = r.range(0.0004f, 0.0008f);
    s.bodyDec = expf(-1.f / (SR * bodySec));
    s.clickDec = expf(-1.f / (SR * clickSec));
    float nb = 1.f / sqrtf(0.5f * bodySec * SR), nk = 1.f / sqrtf(0.5f * clickSec * SR);
    s.body = nb;              s.toeBody = nb * toeB;
    s.click = nk;             s.toeClick = nk * toeK;
    float cut = (400.f + 7000.f * (1.f - d)) * (1.f - 0.85f * muffle_);
    if (cut < 150.f) cut = 150.f;
    s.lp1.clear(); s.lp2.clear();
    s.lp1.set(cut); s.lp2.set(cut);
    s.gain = 1.3f / (1.f + 2.5f * d) * STEP_GAIN * r.range(0.85f, 1.f);
    s.rr.seed(r.next());
  }
  static constexpr float STEP_GAIN = 1.4f;   // short clicks carry little energy; matched to v0.1's level
  S s_[5] = {};
  Gesture g_[2] = {};
  int surface_ = SF_WOOD, dir_ = DIR_PASS;
  float pace_ = 0.5f, muffle_ = 0.3f;
};

Layer* createScrape() { return new ScrapeLayer(); }
Layer* createDrag()   { return new DragLayer(); }
Layer* createSteps()  { return new StepsLayer(); }
