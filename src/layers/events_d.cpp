// MIASMORA — KNOCK (v0.13): someone, or something, at a wooden door
//
//   Knock  1..3 groups of 2..5 knuckle knocks ("kon-kon"), then silence
//   Tap    1..3 slow, light taps seconds apart -- the patient kind
//
// Each hit is a short noise burst striking six door-panel modes spread over
// 70..1600 Hz. (v0.13..v0.15 also had "Pound" -- fist blows -- which kept
// reading as a snare drum; it was removed in v0.16.)
// v0.14: v0.13 used two long-ringing modes (Q ~18..30), which sounded like an
// electronic tom. A door is a stiff, heavily damped panel: every mode now dies
// within a few periods (Q ~7..9) and is re-pitched per hit, so no note is
// heard. Distance = level + low-pass, as if from
// another room or behind the door.
#include "../dsp/speed.h"   // first: speed-optimised (v0.32)
#include "event_layer.h"

using namespace dsp;

class KnockLayer : public EventLayer {
  struct H {   // one hit
    bool on; int delay, t, len;
    float burst, bdec, g[6], gain;
    Reson m[6];
    OnePole hp, lp1, lp2;
    Rng rr;
  };
  // A knocking sequence. It owns its random stream, so a sequence that carries
  // across a Strict loop boundary leaves the scheduler stream untouched.
  struct G {
    bool on; int style, hitsLeft, groupsLeft, hitsPerGroup;
    int32_t next; float interval, gap, force, dist;
    Rng gr;
  };
 public:
  void reset(uint32_t seed) override {
    for (auto& h : h_) h.on = false;
    for (auto& g : g_) g.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    style_ = (int)c.p[1];
    if (style_ < 0 || style_ > KN_TAP) style_ = KN_KNOCK;
    dist_ = c.p[2];
    setRate(densityToRate(c.p[0], 0.3f, 8.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) {
      uint32_t gs = srng_.next();   // drawn even when both sequences are busy (Strict determinism)
      for (auto& g : g_) if (!g.on) { start(g, off, gs); break; }
    });
    for (auto& g : g_) {
      while (g.on && g.next < n) {
        int off = g.next < 0 ? 0 : g.next;
        hit(g, off);
        float wait = g.interval * g.gr.range(0.88f, 1.12f);
        if (--g.hitsLeft <= 0) {
          if (--g.groupsLeft <= 0) { g.on = false; break; }
          g.hitsLeft = g.hitsPerGroup + g.gr.irange(-1, 1);
          if (g.hitsLeft < 2) g.hitsLeft = 2;
          wait = g.gap * g.gr.range(0.8f, 1.25f);
        }
        g.next += (int32_t)(wait * SR);
      }
      if (g.on) g.next -= n;
    }
    for (auto& h : h_) {
      if (!h.on) continue;
      for (int i = 0; i < n; i++) {
        if (h.delay > 0) { h.delay--; continue; }
        float x = h.rr.bi() * h.burst;
        h.burst *= h.bdec;
        if (h.burst < 1e-6f) h.burst = 0.f;   // denormal guard
        float y = h.hp.hp(x) * 0.15f;   // knuckle contact
        for (int k = 0; k < 6; k++) y += h.m[k].tick(x) * h.g[k];
        y = h.lp2.lp(h.lp1.lp(y));
        out[i] += y * h.gain;
        if (++h.t >= h.len) { h.on = false; break; }
      }
      for (auto& m : h.m) m.flush();
      h.hp.flush(); h.lp1.flush(); h.lp2.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(g_); }   // Summon (v0.43)
 private:
  void start(G& g, int off, uint32_t gs) {
    g.gr.seed(gs);
    Rng& r = g.gr;
    g.on = true;
    g.style = style_;
    g.next = off;
    g.dist = clampf(dist_ + r.range(-0.1f, 0.1f), 0.f, 1.f);
    switch (g.style) {
      case KN_TAP:
        g.groupsLeft = 1;
        g.hitsPerGroup = r.irange(1, 3);
        g.interval = r.range(1.2f, 3.5f);
        g.gap = 0.f;
        g.force = r.range(0.4f, 0.7f);
        break;
      default:   // knock
        g.groupsLeft = r.irange(1, 3);
        g.hitsPerGroup = r.irange(2, 5);
        g.interval = r.range(0.13f, 0.22f);
        g.gap = r.range(0.8f, 2.2f);
        g.force = r.range(0.5f, 0.8f);
        break;
    }
    g.hitsLeft = g.hitsPerGroup;
  }

  void hit(G& g, int off) {
    H& h = grabVoice(h_);
    Rng& r = g.gr;
    h.on = true; h.delay = off; h.t = 0;
    float f = g.force * r.range(0.8f, 1.f);
    // six panel modes: centre ranges and decays (Q = pi * f * decay ~ 7..9)
    static const float LO[6]  = {70.f, 140.f, 230.f, 380.f, 600.f, 1000.f};
    static const float HI[6]  = {110.f, 200.f, 320.f, 520.f, 900.f, 1600.f};
    static const float DEC[6] = {0.025f, 0.015f, 0.008f, 0.006f, 0.004f, 0.002f};
    static const float G_KNOCK[6] = {0.35f, 0.5f, 0.8f, 0.8f, 0.7f, 0.6f};
    static const float G_TAP[6]   = {0.1f, 0.25f, 0.6f, 0.8f, 0.8f, 0.7f};
    const float* gainTab = g.style == KN_TAP ? G_TAP : G_KNOCK;
    float burstSec = g.style == KN_TAP ? 0.0006f : 0.001f;
    h.hp.clear();
    h.hp.set(2000.f);
    for (int k = 0; k < 6; k++) {
      h.m[k].clear();
      h.m[k].set(r.range(LO[k], HI[k]), DEC[k] * r.range(0.85f, 1.15f));
      h.g[k] = gainTab[k] * r.range(0.75f, 1.f);
    }
    h.bdec = expf(-1.f / (SR * burstSec));
    h.burst = f / sqrtf(0.5f * burstSec * SR);   // a burst of N samples builds a resonator ~sqrt(N/2)
    h.len = (int)(0.25f * SR);
    // distance: further = darker and quieter
    float cut = 6000.f * powf(500.f / 6000.f, g.dist);
    h.lp1.clear(); h.lp2.clear();
    h.lp1.set(cut); h.lp2.set(cut);
    h.gain = KNOCK_GAIN / (1.f + 2.f * g.dist);
    h.rr.seed(r.next());
  }

  static constexpr float KNOCK_GAIN = 1.0f;
  H h_[6] = {};
  G g_[2] = {};
  int style_ = KN_KNOCK;
  float dist_ = 0.4f;
};

Layer* createKnock() { return new KnockLayer(); }

// ===========================================================================
// BUMP (v0.15) — dull bumps somewhere in the walls or the pipes
// The "pops" measured in the radio reference (v0.14 had them inside STATIC /
// Proximity): 10..120 ms bursts of band-passed noise (around 250..1200 Hz,
// set by Pitch), often in pairs or threes 40..250 ms apart (Cluster).
// ===========================================================================
class BumpLayer : public EventLayer {
  struct P { bool on; int delay, t, len, atk; float amp, dec, env; Svf f; Rng r; };
 public:
  void reset(uint32_t seed) override {
    for (auto& p : p_) p.on = false;
    loopStart(seed);
  }
  void control(const LayerCtl& c, bool) override {
    pitch_ = c.p[1];
    cluster_ = c.p[2];
    setRate(densityToRate(c.p[0], 3.f, 90.f));
  }
  void render(float* out, int n) override {
    for (int i = 0; i < n; i++) out[i] = 0.f;
    runSched(n, [&](int off) { start(off, srng_.next()); });
    for (auto& p : p_) {
      if (!p.on) continue;
      for (int i = 0; i < n; i++) {
        if (p.delay > 0) { p.delay--; continue; }
        out[i] += p.f.bpn(p.r.bi()) * p.env * atkRamp(p.t, p.atk) * p.amp;
        p.env *= p.dec;
        if (++p.t >= p.len) { p.on = false; break; }
      }
      p.f.flush();
    }
  }

  bool hasFreeVoice() const override { return anyFree(p_); }   // Summon (v0.43)
 private:
  void start(int off, uint32_t s) {
    Rng r; r.seed(s);
    float u = r.uni();
    float p2 = 0.15f + 0.6f * cluster_;   // chance of a second bump
    int count = u < 1.f - p2 ? 1 : (u < 1.f - p2 * 0.35f ? 2 : 3);
    int at = off;
    for (int k = 0; k < count; k++) {
      if (k) at += (int)(r.range(0.04f, 0.25f) * SR);
      P& q = grabVoice(p_);
      q.r.seed(r.next());
      q.on = true; q.delay = at; q.t = 0;
      q.atk = ms2s(1);
      float tau = q.r.chance(0.1f) ? 0.06f : q.r.range(0.008f, 0.04f);
      q.dec = expf(-1.f / (SR * tau));
      q.len = (int)(tau * 6.f * SR);
      q.env = 1.f;
      q.amp = BUMP_GAIN * q.r.range(0.5f, 1.f);
      q.f.clear();
      q.f.set(expMap(pitch_, 250.f, 1200.f) * q.r.range(0.8f, 1.25f), q.r.range(1.5f, 3.f));
    }
  }
  static constexpr float BUMP_GAIN = 2.4f;
  P p_[6] = {};
  float pitch_ = 0.4f, cluster_ = 0.4f;
};

Layer* createBump() { return new BumpLayer(); }