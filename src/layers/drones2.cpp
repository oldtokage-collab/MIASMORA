// MIASMORA — v0.31 drones (spec v1.2 3): DISCORD, MICRO, VOX, SHEPARD, HEART
//
// Same rules as drones.cpp: sustained pitches run on Phasor (whole cycles per
// Strict loop), every slow movement is an Lfo (period overridable) or is
// applied as phase modulation whose integral is known, so a Strict export
// stays sample-exactly periodic. Event-like parts (DISCORD's slides, HEART's
// beats) use a scheduler stream that loopStart() reseeds, like the event
// layers.
#include "../dsp/speed.h"   // first: speed-optimised (v0.32)
#include "event_layer.h"

using namespace dsp;

static inline float smoothTo(float cur, float tgt, bool snap, float k = 0.2f) {
  return snap ? tgt : cur + (tgt - cur) * k;
}
// Natural LFO period in seconds after the Strict override (used by PM integrals)
static inline float resolvedSec(const Lfo& l) { return gClk.resolvePeriodSamples(l.period * SR) * INV_SR; }

// Spec 3.2: low content gets a gentle waveshaper (like SUB) so that its
// harmonics carry it on the small speaker. Normalised to unity at x = 1.
struct Warm {
  float g = 1.f, norm = 1.f;
  void set(float drive) { g = 1.f + drive; norm = 1.f / softSat(g); }
  DSP_INLINE float tick(float x) const { return softSat(x * g) * norm; }
};

// ===========================================================================
// DISCORD — 4..6 band-limited saws in a dissonant chord, one low-pass on the
// sum, slow attack; now and then one voice slides down and comes back
// ===========================================================================
static const int8_t CHORDS[5][6] = {
  {0, 3, 7, 11, 0, 0},     // MinMaj
  {0, 3, 6, 9, 13, 0},     // Dim b9
  {0, 4, 7, 6, 10, 13},    // Poly: C major + F# major
  {0, 1, 2, 3, 4, 0},      // Cluster: stacked semitones
  {0, 6, 12, 18, 0, 0},    // Tritone
};
static const int CHORD_N[5] = {4, 5, 6, 5, 4};
static const float DSHAPE[6] = {-1.0f, 0.7f, -0.45f, 1.0f, -0.8f, 0.3f};

class DiscordLayer : public EventLayer {
  // A slide: down (smooth) -> hold -> back up -> settle, on one voice. Each
  // slide keeps its own phase offset, steered to a whole number of cycles
  // during "settle" and then dropped, so the voice ends exactly where it
  // would have been without it. Slides sit in a pool and may overlap on one
  // voice: which slot one lands in never changes the sound, so a Strict loop
  // schedules the same slides on every pass (a "voice busy" rule would not).
  struct Slide { bool on; int v, stage; int32_t t, len[3]; float depth, off; };
  enum { SL_DOWN = 1, SL_HOLD, SL_UP, SL_SETTLE };
  static constexpr int NV = 6, NS = 8;

 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0xD15Cu);
    c1_.init(init.uni(), 27.f);
    c2_.init(init.uni(), 43.f);
    a1_.init(init.uni(), 31.f);
    // a fixed phase per voice: in a Strict loop every Phasor is back at 0 at
    // the loop start, and the saws' resets would all line up on the seam
    for (int v = 0; v < NV; v++) { ph_[v].reset(); off_[v] = 0.f; ph0_[v] = init.uni(); }
    for (auto& s : sl_) s.on = false;
    f_.clear();
    env_ = 0.f;
    EventLayer::loopStart(seed);
  }
  void control(const LayerCtl& c, bool snap) override {
    int ch = (int)c.p[0];
    chord_ = (ch < 0 || ch > CH_TRITONE) ? CH_MINMAJ : ch;
    pitch_ = smoothTo(pitch_, expMap(c.p[1], 38.f, 110.f), snap, 0.15f);
    det_ = smoothTo(det_, c.p[2], snap);
    dark_ = smoothTo(dark_, c.p[3], snap);
    setRate(densityToRate(c.p[4], 0.4f, 4.f));   // slides per minute (DREAD raises it)
  }
  void render(float* out, int n) override {
    const int nv = CHORD_N[chord_];
    float hz[NV];
    for (int v = 0; v < nv; v++) {
      float cents = CHORDS[chord_][v] * 100.f + DSHAPE[v] * (2.f + 25.f * det_);
      hz[v] = pitch_ * powf(2.f, cents / 1200.f);
    }
    // slides fall at buffer resolution (seconds-long gestures)
    runSched(n, [&](int) {
      uint32_t gs = srng_.next();
      Rng r; r.seed(gs);
      Slide* s = nullptr;
      for (auto& q : sl_) if (!q.on) { s = &q; break; }
      if (!s) return;   // 8 at once: practically never
      s->on = true;
      s->v = (int)(gs % (uint32_t)nv);
      s->stage = SL_DOWN; s->t = 0; s->off = 0.f;
      s->depth = r.chance(0.4f) ? 0.5f : 1.0f;   // a quarter tone or a semitone
      float l0 = r.range(4.f, 10.f), l1 = r.range(3.f, 8.f), l2 = r.range(4.f, 8.f);
      // Strict: the whole slide (+1.5 s settle) fits in 85 % of the loop, so
      // the slides alive at a loop start are the same on every pass (spec 3.8)
      if (gClk.loopLen) {
        float room = 0.85f * gClk.loopLen * INV_SR - 1.5f;
        float tot = l0 + l1 + l2;
        if (room < 1.f) room = 1.f;
        if (tot > room) { float k = room / tot; l0 *= k; l1 *= k; l2 *= k; }
      }
      s->len[0] = (int32_t)(l0 * SR); s->len[1] = (int32_t)(l1 * SR); s->len[2] = (int32_t)(l2 * SR);
    });
    const float gain = 0.2f * sqrtf(4.f / nv);
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      c1_.advance(i, nb); c2_.advance(i, nb); a1_.advance(i, nb);
      float m = gClk.lfoMul;
      float cut = 140.f * powf(2.f, (1.f - dark_) * 4.2f + 0.6f * m * (0.6f * c1_.sin() + 0.4f * c2_.sin()));
      f_.set(cut, 0.8f);
      float amp = 1.f - 0.15f * m * a1_.uni();
      float offEnd[NV], dOff[NV];
      for (int v = 0; v < nv; v++) { ph_[v].setHz(hz[v]); offEnd[v] = 0.f; }
      for (auto& s : sl_) {
        if (!s.on) continue;
        if (s.v >= nv) { s.on = false; continue; }   // the chord got smaller
        stepSlide(s, hz[s.v], nb);
        offEnd[s.v] += s.off;
      }
      for (int v = 0; v < nv; v++) dOff[v] = (offEnd[v] - off_[v]) / nb;
      for (int j = 0; j < nb; j++) {
        float s = 0.f;
        for (int v = 0; v < nv; v++) {
          float dt = ph_[v].dt();
          float t = ph_[v].tick() + off_[v] + ph0_[v];
          off_[v] += dOff[v];
          s += sawBL(wrap01(t), dt);
        }
        if (env_ < 1.f) { env_ += 1.f / (3.f * SR); if (env_ > 1.f) env_ = 1.f; }   // slow attack
        out[i + j] = f_.lp(s * gain) * amp * env_ * 0.95f;
      }
      for (int v = 0; v < nv; v++) off_[v] = offEnd[v];
      // a finished slide leaves a whole number of cycles: the same phase, so
      // it is simply taken out
      for (auto& s : sl_) {
        if (s.on && s.stage == 0) { off_[s.v] -= s.off; s.on = false; }
      }
    }
    f_.flush();
  }

 private:
  // Advance one slide by nb samples (its own phase offset, s.off). Stage 0
  // afterwards = finished, s.off is then a whole number of cycles.
  static void stepSlide(Slide& s, float hz, int nb) {
    s.t += nb;
    if (s.stage == SL_SETTLE) {
      // steer the offset to the nearest whole cycle (a frequency change of a
      // fraction of a hertz); render() then drops the whole cycles
      int32_t left = s.len[0] - s.t;
      float target = roundf(s.off);
      if (left <= 0) { s.stage = 0; s.off = target; return; }   // exactly integral
      s.off += (target - s.off) * ((float)nb / (float)(left + nb));
      return;
    }
    float semis;
    if (s.stage == SL_DOWN) semis = -s.depth * smooth01((float)s.t / s.len[0]);
    else if (s.stage == SL_HOLD) semis = -s.depth;
    else semis = -s.depth * (1.f - smooth01((float)s.t / s.len[2]));
    s.off += hz * (powf(2.f, semis / 12.f) - 1.f) * (float)nb * INV_SR;
    int idx = s.stage == SL_DOWN ? 0 : s.stage == SL_HOLD ? 1 : 2;
    if (s.t >= s.len[idx]) {
      s.t = 0;
      if (s.stage == SL_UP) { s.stage = SL_SETTLE; s.len[0] = (int32_t)(1.5f * SR); }
      else s.stage++;
    }
  }

  Lfo c1_, c2_, a1_;
  Phasor ph_[NV];
  float off_[NV], ph0_[NV];
  Slide sl_[NS];
  Svf f_;
  int chord_ = CH_MINMAJ;
  float pitch_ = 55.f, det_ = 0.3f, dark_ = 0.55f, env_ = 0.f;
};

// ===========================================================================
// MICRO — 5 voices on intervals outside the 12-tone grid; each glides slowly
// to the neighbouring microtone and back (phase modulation, periodic)
// ===========================================================================
static const float TUNE_CENTS[5][5] = {
  {0.f, 50.f, 100.f, 150.f, 200.f},              // Quarter
  {0.f, 33.33f, 66.67f, 100.f, 133.33f},         // Sixth
  {0.f, 438.9f, 877.7f, 1316.6f, 1463.0f},       // Bohlen-Pierce steps 0 3 6 9 10 (3^(k/13))
  {0.f, 398.f, 693.f, 987.f, 1215.f},            // Beating: 5/4 +12, 3/2 -9, 7/4 +18, 2/1 +15 cents
  {0.f, 1272.f, 2016.f, 2544.f, 2954.f},         // Stretched partials: n^1.06
};
static const float GLIDE_STEP[5] = {50.f, 33.3f, 146.3f, 40.f, 60.f};   // "the neighbouring microtone", cents

class MicroLayer : public Layer {
  static constexpr int NV = 5;
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0x31C2u);
    for (int v = 0; v < NV; v++) { gl_[v].init(init.uni(), init.range(10.f, 30.f)); ph_[v].reset(); ph0_[v] = init.uni(); }
    a1_.init(init.uni(), 37.f);
    lp_.clear();
    lp_.set(1400.f);
    first_ = true;
    loopStart(seed);
  }
  void loopStart(uint32_t) override {}
  void control(const LayerCtl& c, bool snap) override {
    int t = (int)c.p[0];
    tun_ = (t < 0 || t > TU_STRETCHED) ? TU_QUARTER : t;
    root_ = smoothTo(root_, expMap(c.p[1], 70.f, 280.f), snap, 0.15f);
    spread_ = smoothTo(spread_, c.p[2], snap);
    glide_ = smoothTo(glide_, c.p[3], snap);
    saw_ = (int)c.p[4] == MW_SAW;
  }
  void render(float* out, int n) override {
    float hz[NV], w[NV];
    const float scale = tun_ <= TU_SIXTH ? 0.5f + spread_ : 1.f;   // clusters widen with Spread
    for (int v = 0; v < NV; v++) {
      hz[v] = root_ * powf(2.f, TUNE_CENTS[tun_][v] * scale / 1200.f);
      w[v] = v < 2 ? 1.f : clampf(spread_ * 2.f - (v - 2) * 0.45f, 0.15f, 1.f);   // the upper voices open up
    }
    const float glideCents = glide_ * GLIDE_STEP[tun_];
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      a1_.advance(i, nb);
      float off1[NV], dOff[NV];
      for (int v = 0; v < NV; v++) {
        gl_[v].advance(i, nb);
        ph_[v].setHz(hz[v]);
        // frequency deviation K*sin -> phase -K*T/(2pi)*cos (cycles)
        float K = hz[v] * (powf(2.f, glideCents / 1200.f) - 1.f);
        off1[v] = -K * resolvedSec(gl_[v]) * gl_[v].cos() * (1.f / TWO_PI_F);
      }
      if (first_) { for (int v = 0; v < NV; v++) off_[v] = off1[v]; first_ = false; }
      for (int v = 0; v < NV; v++) dOff[v] = (off1[v] - off_[v]) / nb;
      float amp = 1.f - 0.2f * gClk.lfoMul * a1_.uni();
      for (int j = 0; j < nb; j++) {
        float s = 0.f;
        for (int v = 0; v < NV; v++) {
          float dt = ph_[v].dt();
          float t = ph_[v].tick() + off_[v] + ph0_[v];
          off_[v] += dOff[v];
          s += w[v] * (saw_ ? sawBL(wrap01(t), dt) * 0.6f : sinW(t));
        }
        if (saw_) s = lp_.lp(s) * 1.6f;
        out[i + j] = s * 0.17f * amp;
      }
      for (int v = 0; v < NV; v++) off_[v] = off1[v];
    }
    lp_.flush();
  }

 private:
  Lfo gl_[NV], a1_;
  Phasor ph_[NV];
  OnePole lp_;
  float off_[NV] = {0}, ph0_[NV] = {0};
  int tun_ = TU_QUARTER;
  float root_ = 140.f, spread_ = 0.4f, glide_ = 0.4f;
  bool saw_ = false, first_ = true;
};

// ===========================================================================
// VOX — 3-4 saws with vibrato through one formant filter (F1-F3); the vowel
// drifts u -> o -> a -> e and back; breath noise goes through the same filter
// ===========================================================================
struct Vowel { float f[3], a[3]; };
static const Vowel VOWELS[4] = {
  {{325.f, 700.f, 2530.f}, {1.0f, 0.50f, 0.10f}},   // u
  {{450.f, 800.f, 2830.f}, {1.0f, 0.60f, 0.10f}},   // o
  {{750.f, 1150.f, 2800.f}, {1.0f, 0.70f, 0.20f}},  // a
  {{500.f, 1750.f, 2600.f}, {1.0f, 0.45f, 0.25f}},  // e
};
static const Vowel HUM = {{260.f, 950.f, 2400.f}, {1.0f, 0.15f, 0.04f}};   // closed mouth "mmm"

class VoxLayer : public Layer {
  static constexpr int NV = 4;
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0x70C5u);
    for (int v = 0; v < NV; v++) { vib_[v].init(init.uni(), init.range(0.17f, 0.23f)); ph_[v].reset(); ph0_[v] = init.uni(); }
    vw_.init(init.uni(), 30.f);
    mo_.init(init.uni(), 20.f);
    a1_.init(init.uni(), 19.f);
    for (auto& f : fm_) f.clear();
    body_.clear();
    body_.set(500.f);
    first_ = true;
    loopStart(seed);
  }
  void loopStart(uint32_t seed) override { nrng_.seed(seed); }
  void control(const LayerCtl& c, bool snap) override {
    int m = (int)c.p[0];
    mode_ = (m < 0 || m > VX_MOAN) ? VX_CHOIR : m;
    root_ = smoothTo(root_, expMap(c.p[1], 85.f, 230.f), snap, 0.15f);
    vowel_ = smoothTo(vowel_, c.p[2], snap);
    motion_ = smoothTo(motion_, c.p[3], snap);
    breath_ = smoothTo(breath_, c.p[4], snap);
    // the vowel changes slowly so that it never sounds like words
    vw_.period = expMap(motion_, 45.f, 12.f);
    mo_.period = expMap(motion_, 30.f, 12.f);
  }
  void render(float* out, int n) override {
    const int nv = mode_ == VX_HUM ? 3 : NV;
    const float root = mode_ == VX_HUM ? root_ * 0.7f : root_;
    const float detC = (mode_ == VX_CHOIR ? 14.f : mode_ == VX_HUM ? 4.f : 8.f) * gClk.lfoMul;
    // Moan: the whole voice sags by `sag` semitones and comes back. Written as
    // a constant shift to the mean plus a cosine deviation (integrable).
    const float sag = mode_ == VX_MOAN ? 1.5f + 1.5f * motion_ : 0.f;
    const float r = powf(2.f, -sag / 12.f);
    const float meanF = (1.f + r) * 0.5f, devF = (1.f - r) * 0.5f;
    float hz[NV];
    for (int v = 0; v < nv; v++) {
      float semis = (mode_ == VX_CHOIR && v == 3) ? 12.f : 0.f;   // one voice an octave up in the choir
      hz[v] = root * meanF * powf(2.f, (semis * 100.f + DSHAPE[v] * detC) / 1200.f);
    }
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      vw_.advance(i, nb); mo_.advance(i, nb); a1_.advance(i, nb);
      // formants for this block
      const Vowel* V0; const Vowel* V1; float fr;
      if (mode_ == VX_HUM) { V0 = V1 = &HUM; fr = 0.f; }
      else {
        float pos = (0.25f + 0.75f * vowel_) * 3.f * vw_.uni();   // Vowel: how far along u-o-a-e it goes
        int k = (int)pos; if (k > 2) k = 2;
        V0 = &VOWELS[k]; V1 = &VOWELS[k + 1]; fr = pos - (float)k;
      }
      float fa[3];
      for (int q = 0; q < 3; q++) {
        fm_[q].set(lerpf(V0->f[q], V1->f[q], fr), 6.f + 2.f * q);
        fa[q] = lerpf(V0->a[q], V1->a[q], fr);
      }
      float off1[NV], dOff[NV];
      for (int v = 0; v < nv; v++) {
        vib_[v].advance(i, nb);
        ph_[v].setHz(hz[v]);
        float K = hz[v] * 0.004f;   // ~7 cents of vibrato
        float o = -K * resolvedSec(vib_[v]) * vib_[v].cos() * (1.f / TWO_PI_F);
        if (sag > 0.f) o += root * devF * resolvedSec(mo_) * mo_.sin() * (1.f / TWO_PI_F) * (hz[v] / (root * meanF));
        off1[v] = o;
      }
      if (first_) { for (int v = 0; v < nv; v++) off_[v] = off1[v]; first_ = false; }
      for (int v = 0; v < nv; v++) dOff[v] = (off1[v] - off_[v]) / nb;
      float amp = 1.f - 0.2f * gClk.lfoMul * a1_.uni();
      float br = breath_ * 0.5f;
      for (int j = 0; j < nb; j++) {
        float s = 0.f;
        for (int v = 0; v < nv; v++) {
          float dt = ph_[v].dt();
          float t = ph_[v].tick() + off_[v] + ph0_[v];
          off_[v] += dOff[v];
          s += sawBL(wrap01(t), dt);
        }
        float x = s * (0.3f / nv) + nrng_.bi() * br;
        float y = fa[0] * fm_[0].bpn(x) + fa[1] * fm_[1].bpn(x) + fa[2] * fm_[2].bpn(x);
        y += 0.35f * body_.lp(x);   // chest
        out[i + j] = y * 3.5f * amp;
      }
      for (int v = 0; v < nv; v++) off_[v] = off1[v];
    }
    for (auto& f : fm_) f.flush();
    body_.flush();
  }

 private:
  Lfo vib_[NV], vw_, mo_, a1_;
  Phasor ph_[NV];
  Svf fm_[3];
  OnePole body_;
  Rng nrng_;
  float off_[NV] = {0}, ph0_[NV] = {0};
  int mode_ = VX_CHOIR;
  float root_ = 130.f, vowel_ = 0.5f, motion_ = 0.35f, breath_ = 0.3f;
  bool first_ = true;
};

// ===========================================================================
// SHEPARD — 6 sines spaced an octave (or a tritone) apart, all gliding the
// same way; a bell curve over the log-frequency range fades each one in at
// one end and out at the other, so the glide never seems to end
// ===========================================================================
class ShepardLayer : public Layer {
  static constexpr int NP = 6;   // v0.31: 6 (8 cost 55 us per buffer on the PC; the lowest were inaudible)
  static constexpr uint64_t ONE = 1ull << 32;   // one spacing in the 32.32 position
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0x5E9Au);
    pos_ = (uint64_t)(init.uni() * NP * (double)ONE) % (NP * ONE);
    for (int j = 0; j < NP; j++) { ph_[j] = 0.f; seg_[j] = -1; }
    a1_.init(init.uni(), 29.f);
    first_ = true;
    climaxS_ = 0.f;
    loopStart(seed);
  }
  void loopStart(uint32_t) override {}
  void control(const LayerCtl& c, bool snap) override {
    down_ = (int)c.p[0] == SH_DOWN;
    int sp = (int)c.p[2];
    tri_ = sp == SP_TRITONE;
    spd_ = smoothTo(spd_, c.p[1], snap);
    tone_ = smoothTo(tone_, c.p[3], snap);
  }
  void render(float* out, int n) override {
    // Y octaves are covered by NP partials (octave: 6, tritone: 3)
    const int Y = tri_ ? NP / 2 : NP;
    const float fmin = tri_ ? expMap(tone_, 60.f, 170.f) : expMap(tone_, 28.f, 80.f);
    const float tSp = expMap(spd_, 120.f, 10.f);   // seconds per spacing
    const int sgn = down_ ? -1 : 1;
    const bool strict = gClk.loopLen != 0;
    // Strict: a whole number k of spacings per loop (spec 3.8); the position
    // is then a function of the loop position alone
    uint32_t k = 0;
    if (strict) {
      k = (uint32_t)lroundf((float)gClk.loopLen / (tSp * SR));
      if (k < 1) k = 1;
      // a different speed or tone means a different phase function: rebuild
      if (k != kPrev_ || fmin != fminPrev_ || tri_ != triPrev_) { for (int j = 0; j < NP; j++) seg_[j] = -1; }
      kPrev_ = k; fminPrev_ = fmin; triPrev_ = tri_;
    }
    const float spacingSamples = strict ? (float)gClk.loopLen / (float)k : tSp * SR;
    // Arc's climax (v0.5): it slides up to 5x faster for a moment (live only)
    if (!strict) climaxS_ += (gClk.climax - climaxS_) * fminf((float)n / (1.5f * SR), 1.f);
    const float dsPerSample = (float)sgn / spacingSamples * (strict ? 1.f : 1.f + 4.f * climaxS_);   // spacings per sample
    const float dyPerSample = dsPerSample * (float)Y / (float)NP;    // octaves per sample
    // Strict phase function: phi(y) = A * 2^y, A = fmin * T_spacing / ln(spacing ratio)
    const double A = (double)fmin * (spacingSamples * INV_SR) / (tri_ ? 0.34657359 : 0.69314718);
    const Warm wm = [] { Warm w; w.set(0.6f); return w; }();

    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      a1_.advance(i, nb);
      if (strict) {
        uint32_t p = (gClk.pos + i) % gClk.loopLen;
        uint64_t s = (((uint64_t)k * p) << 32) / gClk.loopLen;   // spacings since the loop start, 32.32
        s %= NP * ONE;
        pos_ = down_ ? (NP * ONE - s) % (NP * ONE) : s;
      }
      float amp = 1.f - 0.15f * gClk.lfoMul * a1_.uni();
      float wv[NP];
      // per-partial state for this block
      float d0[NP], off[NP], base[NP], inc[NP];
      for (int j = 0; j < NP; j++) {
        uint64_t t = (pos_ + (uint64_t)j * ONE) % (NP * ONE);
        uint64_t xfix = t / NP;                       // 0.32 position on the log axis
        float x = (float)xfix * (1.f / 4294967296.f);
        wv[j] = 0.5f - 0.5f * sinW(x + 0.25f);        // 0 at both ends (per block: it moves very slowly)
        uint64_t yfix = xfix * (uint64_t)Y;           // octaves above fmin, 2^-32 units
        if (strict) {
          int32_t seg = (int32_t)(yfix >> 26);        // 1/64-octave segments
          if (seg != seg_[j]) {
            double b = A * pow(2.0, seg / 64.0);
            segOff_[j] = (float)(b - floor(b));
            segBase_[j] = (float)b;
            seg_[j] = seg;
          }
          d0[j] = (float)(yfix & ((1ull << 26) - 1)) * (1.f / 4294967296.f);
          off[j] = segOff_[j];
          base[j] = segBase_[j];
        } else {
          float y = (float)yfix * (1.f / 4294967296.f);
          inc[j] = fmin * exp2f(y) * INV_SR;
        }
      }
      for (int j = 0; j < nb; j++) {
        float s = 0.f;
        if (strict) {
          for (int q = 0; q < NP; q++) {
            float z = (d0[q] + dyPerSample * j) * 0.69314718f;
            float e = z * (1.f + z * (0.5f + z * (1.f / 6.f)));   // 2^d - 1, |d| < 1/32
            s += wv[q] * sinW(off[q] + base[q] * e);
          }
        } else {
          for (int q = 0; q < NP; q++) {
            float p = ph_[q] + inc[q];
            if (p >= 1.f) p -= 1.f;
            ph_[q] = p;
            s += wv[q] * sinL(p);
          }
        }
        out[i + j] = wm.tick(s * 0.3f) * 0.6f * amp;
      }
      if (!strict) {
        int64_t d = (int64_t)((double)dsPerSample * nb * (double)ONE);
        pos_ = (uint64_t)(((int64_t)pos_ + d) % (int64_t)(NP * ONE) + (int64_t)(NP * ONE)) % (NP * ONE);
      }
    }
  }

 private:
  Lfo a1_;
  uint64_t pos_ = 0;           // spacings, 32.32, modulo NP
  float ph_[NP];               // free-running phases
  int32_t seg_[NP];            // Strict: current 1/64-octave segment per partial
  float segOff_[NP] = {0}, segBase_[NP] = {0};
  uint32_t kPrev_ = 0;
  float fminPrev_ = 0.f;
  bool triPrev_ = false;
  bool down_ = true, tri_ = false, first_ = true;
  float spd_ = 0.5f, tone_ = 0.45f;
  float climaxS_ = 0.f;   // Arc's climax, smoothed
};

// ===========================================================================
// HEART — "lub-dub": a soft pulse into low resonators, the second beat
// shorter and lighter, all muffled as if heard from inside; blood flow noise
// swells after each beat. BPM rises with tension (React); a Listen Hush
// stops it for one beat and quickens it for a while.
// Attack (spec v1.21 5) makes the beat audible on the tiny speaker, which plays
// little below ~200 Hz. v0.38: overtones of the beat itself (the low thump is
// clipped, scaled back to its own envelope and band-passed to ~150..450 Hz:
// the ear fills in the missing low beat from them), plus a little unpitched,
// muffled noise thud. (v0.37 rang one ~200 Hz resonator instead, which
// sounded like tapping a big plastic bottle.) Both go through the same
// muffling low-pass. Cursed Mode can stop the heart for 2..3
// beats (spec v1.21 6); it starts again a little faster.
// ===========================================================================
class HeartLayer : public Layer {
  struct Beat { bool on; int32_t delay, t, len1, d2, len2; float a1, a2, inv1, inv2; };
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0x4EA7u);
    f1_ = init.range(42.f, 52.f);
    f2_ = init.range(85.f, 115.f);
    f3_ = init.range(62.f, 78.f);
    // Body resonances as SVF band-passes, Q = pi * f * decay. (A 2-pole Reson
    // in float, this low, never forgets its starting state -- rounding keeps
    // it alive -- so a Strict loop never became periodic. The SVF does.)
    r1_.clear(); r2_.clear(); r3_.clear();
    r1_.set(f1_, PI_F * f1_ * 0.14f); r2_.set(f2_, PI_F * f2_ * 0.05f); r3_.set(f3_, PI_F * f3_ * 0.07f);
    hb_.clear(); hb_.set(H_CENTER, 0.7f);
    tb_.clear(); tb_.set(T_CENTER, 0.8f);
    henv_ = 0.f; thud_ = 0.f;
    hrel_ = expf(-1.f / (H_RELEASE * SR));
    tdec_ = expf(-1.f / (T_DECAY * SR));
    muff_.clear(); muffA_.clear(); flowF_.clear();
    flowF_.set(180.f, 1.2f);
    for (auto& b : beats_) b.on = false;
    tens_ = gClk.tension;
    boost_ = 0.f;
    flowEnv_ = 0.f;
    lastHush_ = gClk.hushSeq;
    skipNext_ = false;
    lastStop_ = gClk.heartStopSeq;
    stopLeft_ = 0;
    boostC_ = 0.f;
    climaxS_ = 0.f;
    loopStart(seed);
  }
  void loopStart(uint32_t seed) override {
    srng_.seed(seed);
    nrng_.seed(seed ^ 0xF10Fu);
    slot_ = 0;
    skipSlot_ = -1;
    // Strict: the beats sit half a beat off the loop start (set in render(),
    // where the period is known), so the seam never falls on a beat
    startPending_ = gClk.loopLen != 0;
    nextSlot_ = gClk.loopLen ? 0x3FFFFFFF : (int32_t)(0.3f * SR);
  }
  // Summon (v0.43): one beat now, besides the regular ones
  bool hasFreeVoice() const override { return !beats_[0].on || !beats_[1].on; }
  bool beginSummon(uint32_t, int) override { summonBeat_ = true; return true; }
  void control(const LayerCtl& c, bool snap) override {
    bpm_ = smoothTo(bpm_, c.p[0], snap);
    irr_ = smoothTo(irr_, c.p[1], snap);
    muffle_ = smoothTo(muffle_, c.p[2], snap);
    flow_ = smoothTo(flow_, c.p[3], snap);
    react_ = smoothTo(react_, c.p[4], snap);
    attack_ = smoothTo(attack_, c.p[5], snap);
  }
  void render(float* out, int n) override {
    const bool strict = gClk.loopLen != 0;
    // Tension follows in ~6 s; a Hush skips a beat and adds +15 BPM that fades
    if (!strict) {
      tens_ += (gClk.tension - tens_) * ((float)n / (6.f * SR));
      if (gClk.hushSeq != lastHush_) { lastHush_ = gClk.hushSeq; skipNext_ = true; boost_ = 15.f; }
      // Cursed: the heart stops for a few beats (counted at the slots below)
      if (gClk.heartStopSeq != lastStop_) { lastStop_ = gClk.heartStopSeq; stopLeft_ = gClk.heartStopBeats; }
      const float k = expf(-(float)n / (5.f * SR));
      boost_ *= k;
      if (boost_ < 0.01f) boost_ = 0.f;
      boostC_ *= k;   // +10 BPM after a stop, mostly gone in 10..20 s
      if (boostC_ < 0.01f) boostC_ = 0.f;
    }
    // Arc's climax (v0.5): a few beats faster and stronger, eased in and out
    if (!strict) climaxS_ += (gClk.climax - climaxS_) * fminf((float)n / (1.5f * SR), 1.f);
    float bpm = 40.f + 100.f * bpm_ + react_ * 40.f * tens_ + boost_ + boostC_ + CLIMAX_BPM * climaxS_;
    float period;   // samples per beat
    if (strict) {
      // a whole number of beats per loop (spec 3.8), e.g. 32 s at 72 BPM -> 38 beats
      float beats = roundf((float)gClk.loopLen * bpm / (60.f * SR));
      if (beats < 1.f) beats = 1.f;
      period = (float)gClk.loopLen / beats;
      bpm = 60.f * SR / period;
    } else {
      period = 60.f * SR / bpm;
    }
    // the second beat comes sooner when the heart is fast
    const float gap2 = 0.35f - 0.1f * clampf((bpm - 40.f) / 100.f, 0.f, 1.f);
    if (startPending_) { nextSlot_ = (int32_t)lroundf(0.5f * period); startPending_ = false; }
    if (summonBeat_) { summonBeat_ = false; startBeat(0, 1.f, gap2); }
    // the splash (v0.98): only the summoned beats, at the moments it asks for
    if (gClk.heartSummonOnly && !strict) nextSlot_ = n + (int32_t)(0.3f * SR);

    while (nextSlot_ < n) {
      int32_t off = nextSlot_ < 0 ? 0 : nextSlot_;
      // four draws per slot, always (Strict determinism)
      float u1 = srng_.uni(), u2 = srng_.uni(), u3 = srng_.uni(), u4 = srng_.uni();
      const bool stopped = stopLeft_ > 0;
      bool skip = u2 < 0.07f * irr_ || skipNext_ || slot_ == skipSlot_ || stopped;
      skipNext_ = false;
      if (stopped && --stopLeft_ == 0) boostC_ = 10.f;   // it starts again, a little faster
      if (!skip) {
        int32_t jit = (int32_t)((u1 * 2.f - 1.f) * 0.03f * irr_ * period);
        startBeat(off + (jit > 0 ? jit : 0), (0.85f + 0.3f * u4) * (1.f + CLIMAX_AMP * climaxS_), gap2);
        // now and then an early beat between two slots, then a long pause
        if (u3 < 0.07f * irr_) {
          startBeat(off + (int32_t)(0.6f * period), 0.8f, gap2);
          skipSlot_ = slot_ + 1;
        }
      }
      slot_++;
      if (strict) nextSlot_ += (int32_t)(lroundf((slot_ + 0.5f) * period) - lroundf((slot_ - 0.5f) * period));
      else nextSlot_ += (int32_t)period;
    }
    nextSlot_ -= n;

    muff_.set(expMap(muffle_, 600.f, 150.f), 0.7f);
    // the Attack's own copy: the same Muffle, an octave higher. At the body's
    // cutoff (150..600 Hz) it would take away most of what the speaker plays.
    muffA_.set(2.f * expMap(muffle_, 600.f, 150.f), 0.7f);
    const float flowAmt = flow_ * 0.6f;
    const float hGain = H_GAIN * attack_, tGain = T_GAIN * attack_;
    Warm wm; wm.set(1.4f);
    for (int i = 0; i < n; i++) {
      float e1 = 0.f, e2 = 0.f;
      for (auto& b : beats_) {
        if (!b.on) continue;
        if (b.delay > 0) { b.delay--; continue; }
        if (b.t == 0) { flowEnv_ += 0.8f * b.a1; thud_ += b.a1; onsetFlag = true; }   // Listen: our own beat (v0.33)
        if (b.t < b.len1) e1 += b.a1 * sinL((float)b.t * b.inv1);
        int32_t t2 = b.t - b.d2;
        if (t2 == 0) { thud_ += b.a2; onsetFlag = true; }              // ... and the second one
        if (t2 >= 0 && t2 < b.len2) e2 += b.a2 * sinL((float)t2 * b.inv2);
        if (++b.t >= b.d2 + b.len2) b.on = false;
      }
      const float body = bpRaw(r1_, e1) * 1.0f + bpRaw(r2_, e1) * 0.45f + bpRaw(r3_, e2) * 0.8f;
      // Attack: the body's overtones, following its own envelope
      const float ab = fabsf(body);
      henv_ = ab > henv_ ? ab : henv_ * hrel_;
      const float harm = hb_.bpn(softSat(body * H_DRIVE) * henv_);
      // ... and the noise thud (drawn every sample: Strict determinism)
      const float tn = nrng_.bi();
      const float th = tb_.bpn(tn * thud_);
      thud_ *= tdec_;
      flowEnv_ *= 0.99982f;   // ~0.25 s
      float fl = flowF_.bpn(nrng_.bi()) * (0.25f + flowEnv_) * flowAmt;
      float y = muff_.lp(body * 2.6f + fl);
      // the Attack is added after the warm saturation, which would squash it,
      // through its own copy of the muffling low-pass
      // (a soft ceiling: the clipped overtones start with sharp peaks)
      const float a = A_CEIL * softSat(muffA_.lp(harm * hGain + th * tGain) * (1.f / A_CEIL));
      out[i] = (wm.tick(y) + a) * 0.5f;
    }
    if (flowEnv_ < 1e-6f) flowEnv_ = 0.f;
    if (thud_ < 1e-6f) thud_ = 0.f;
    if (henv_ < 1e-9f) henv_ = 0.f;
    r1_.flush(); r2_.flush(); r3_.flush(); hb_.flush(); tb_.flush(); muff_.flush(); muffA_.flush(); flowF_.flush();
  }

 private:
  // Attack (v0.38)
  static constexpr float H_DRIVE = 40.f;      // the body clipped into a near-square ...
  static constexpr float H_RELEASE = 0.02f;   // ... scaled to its envelope (release, s)
  static constexpr float H_CENTER = 300.f;    // overtones kept: ~170..520 Hz
  static constexpr float T_CENTER = 200.f;    // noise thud band
  static constexpr float T_DECAY = 0.015f;    // s
  // Gains at Attack 100. Measured (HEART alone, default parameters): above
  // 200 Hz, what the tiny speaker plays, Attack 0 / 50 / 100 = -46 / -36 /
  // -34 dB; the layer's whole level -26 / -24 / -23 dB; peak 0.25 -> 0.44.
  static constexpr float H_GAIN = 20.f;
  static constexpr float T_GAIN = 25.f;
  static constexpr float A_CEIL = 1.0f;
  // raw band-pass output: its peak gain is Q, like a ringing resonator
  static inline float bpRaw(Svf& f, float x) { float l, b; f.tick(x, l, b); return b; }
  void startBeat(int32_t delay, float a, float gap2) {
    Beat* b = &beats_[0];
    if (b->on) b = &beats_[1];
    if (b->on) return;   // both busy (very fast / early beat): let it pass
    b->on = true;
    b->delay = delay;
    b->t = 0;
    b->len1 = (int32_t)((0.02f + 0.02f * muffle_) * SR);   // 20..40 ms
    b->d2 = (int32_t)(gap2 * SR);
    b->len2 = (int32_t)(b->len1 * 0.7f);
    b->inv1 = 0.5f / (float)b->len1;   // half a sine over each beat, no per-sample divide (v0.32)
    b->inv2 = 0.5f / (float)b->len2;
    b->a1 = a;
    b->a2 = a * 0.55f;
  }

  Rng srng_, nrng_;
  Svf r1_, r2_, r3_, hb_, tb_;
  Svf muff_, muffA_, flowF_;
  Beat beats_[2];
  float f1_ = 47.f, f2_ = 100.f, f3_ = 70.f;
  float henv_ = 0.f, hrel_ = 0.999f, thud_ = 0.f, tdec_ = 0.997f;
  float bpm_ = 0.3f, irr_ = 0.3f, muffle_ = 0.5f, flow_ = 0.3f, react_ = 0.5f, attack_ = 0.5f;
  float tens_ = 0.f, boost_ = 0.f, flowEnv_ = 0.f;
  uint32_t lastHush_ = 0, lastStop_ = 0;
  int stopLeft_ = 0;
  float boostC_ = 0.f;
  float climaxS_ = 0.f;                       // Arc's climax, smoothed
  static constexpr float CLIMAX_BPM = 35.f;   // at the top of the climax
  static constexpr float CLIMAX_AMP = 0.5f;
  bool skipNext_ = false;
  int32_t nextSlot_ = 0, slot_ = 0, skipSlot_ = -1;
  bool startPending_ = false;
  bool summonBeat_ = false;
};

Layer* createDiscord() { return new DiscordLayer(); }
Layer* createMicro()   { return new MicroLayer(); }
Layer* createVox()     { return new VoxLayer(); }
Layer* createShepard() { return new ShepardLayer(); }
Layer* createHeart()   { return new HeartLayer(); }
