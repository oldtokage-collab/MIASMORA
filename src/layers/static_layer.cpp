// MIASMORA — STATIC: a broken TV / a badly tuned radio
//
//  TV      broadband hiss + mains hum (50 or 60 Hz, picked by the seed) with
//          occasional level bursts and dropouts.
//  Radio   hiss squeezed into an AM-band window (~300..3000 Hz) plus crackle,
//          with the reception slowly fading in and out.
//  Tuning  Radio, plus heterodyne whistles sweeping up and down and, now and
//          then, a fragment of "something like a voice": noise through moving
//          vowel formants. Never words.
//  Proximity  the "something is near" radio: a steady, muffled, gritty hiss
//          with a shallow nervous flutter and crackle, a low rumble, and the
//          sounds of an unstable tuning dial (falling swoops, short whistles).
//          Instability adds waves, as if a presence were approaching and passing.
//
// The irregular parts are events on the layer's own scheduler stream; the
// slow fading is Lfo-driven, so the layer is Strict-loop safe.
#include "../dsp/speed.h"   // first: speed-optimised (v0.32)
#include "layer.h"

using namespace dsp;

struct Vowel { float f1, f2, f3; };
static const Vowel VOWELS[5] = {
  {730.f, 1090.f, 2440.f},   // a
  {530.f, 1840.f, 2480.f},   // e
  {270.f, 2290.f, 3010.f},   // i
  {570.f,  840.f, 2410.f},   // o
  {300.f,  870.f, 2240.f},   // u
};

class StaticLayer : public Layer {
 public:
  void reset(uint32_t seed) override {
    Rng init; init.seed(seed ^ 0x57A7u);
    w1_.init(init.uni(), 7.1f);
    w2_.init(init.uni(), 12.7f);
    w3_.init(init.uni(), 4.3f);
    humHz_ = init.chance(0.5f) ? 50.f : 60.f;
    for (auto& w : tones_) w.on = false;
    rumLp_.clear(); r37_.reset(); r45_.reset();
    hum_.reset();
    hp_.clear(); lp_.clear(); lp2_.clear();
    g_ = gT_ = 1.f; gHold_ = 0;
    crAmp_ = 0.f;
    for (auto& w : whs_) w.active = false;
    for (auto& v : vos_) v.active = false;
    for (auto& s : sus_) s.active = false;
    proxI_ = 0.f; gate_ = 1.f; gateOn_ = true; gateCd_ = 1; holdCnt_ = 0; held_ = 0.f;
    gateLo_ = 0.4f; grain_ = 1.f; grainT_ = 1.f; grainCd_ = 1;
    buzz_.reset();
    first_ = true;
    loopStart(seed);
  }
  void loopStart(uint32_t seed) override {
    nrng_.seed(seed);
    srng_.seed(seed ^ 0x0EE7u);
    waitGain_ = interval(rateGain());
    waitCr_ = interval(rateCrackle());
    waitWh_ = interval(rateWhistle());
    waitVo_ = interval(rateVoice());
    waitSu_ = interval(rateSurge());
    waitSw_ = interval(rateSwoop());
    // Proximity's texture counters restart with the noise stream, so every
    // Strict pass draws the same noise in the same order (v0.17)
    gateCd_ = 1; gateOn_ = true; grainCd_ = 1; holdCnt_ = 0;
    waitWh2_ = interval(rateWhistle2());
  }
  void control(const LayerCtl& c, bool snap) override {
    mode_ = (int)c.p[0];
    if (mode_ < 0 || mode_ > ST_PROXIMITY) mode_ = 0;
    tone_ = snap ? c.p[1] : tone_ + (c.p[1] - tone_) * 0.2f;
    inst_ = snap ? c.p[2] : inst_ + (c.p[2] - inst_) * 0.2f;
    // A long pending wait from a very low rate is re-rolled when the rate goes up
    clampWait(waitGain_, rateGain());
    clampWait(waitCr_, rateCrackle());
    clampWait(waitWh_, rateWhistle());
    clampWait(waitVo_, rateVoice());
    clampWait(waitSu_, rateSurge());
    clampWait(waitSw_, rateSwoop());
    clampWait(waitWh2_, rateWhistle2());
  }
  void render(float* out, int n) override {
    if (mode_ == ST_PROXIMITY) { renderProximity(out, n); return; }
    const bool tv = mode_ == ST_TV;
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      w1_.advance(i, nb); w2_.advance(i, nb); w3_.advance(i, nb);
      schedule(nb);
      hum_.setHz(humHz_);
      float lm = gClk.lfoMul;
      // Reception: filter window and level wander
      float wob = (0.6f * w1_.sin() + 0.4f * w2_.sin()) * lm;
      float tf = 0.6f + 1.0f * tone_;
      float recv;
      if (tv) {
        lp_.set((2500.f + 4500.f * tone_), 0.7f);   // capped at 7 kHz: the top octave only stings
        recv = 1.f;
      } else {
        float sh = powf(2.f, 0.35f * wob);
        hp_.set(300.f * tf * sh, 0.8f);
        lp_.set(3000.f * tf * sh, 0.9f);
        float fadeDepth = (0.25f + 0.5f * inst_) * lm;
        if (fadeDepth > 0.9f) fadeDepth = 0.9f;
        recv = 1.f - fadeDepth * (0.55f * w1_.uni() + 0.3f * w2_.uni() + 0.15f * w3_.uni());
      }
      if (first_) { recvS_ = recv; first_ = false; }
      float dRecv = (recv - recvS_) / nb;
      float humLvl = tv ? 0.05f : 0.018f;
      // Voice fragment formants follow a vowel glide (control rate)
      for (auto& v : vos_) if (v.active) updateVoiceFilters(v);
      for (auto& w : whs_) if (w.active) updateWhistle(w);
      for (int j = 0; j < nb; j++) {
        float x = nrng_.bi() * 0.6f;
        // crackle: noise-modulated decaying impulse
        // drawn every sample (v0.37), crackling or not: the first Strict pass has
        // no crackle carried in, and the noise stream must not depend on it
        const float cn = nrng_.bi();
        if (crAmp_ > 0.0005f) { x += cn * crAmp_ * 1.2f; crAmp_ *= crDec_; }
        else crAmp_ = 0.f;
        float extra = 0.f;
        for (auto& w : whs_) if (w.active) extra += whistleSample(w);
        for (auto& v : vos_) if (v.active) extra += voiceSample(v);
        float y;
        if (tv) {
          y = lp_.lp(x) + extra;
        } else {
          y = lp_.lp(hp_.hp(x + extra));
        }
        float hp = hum_.tick();
        float hm = 0.55f * sinL(hp) + 0.3f * sinW(2.f * hp) + 0.15f * sinW(3.f * hp);
        g_ += (gT_ - g_) * 0.012f;   // ~3 ms slew, burst / dropout edges never click
        out[i + j] = (y * recvS_ * g_ + hm * humLvl) * OUT_SCALE;
        recvS_ += dRecv;
      }
      recvS_ = recv;
    }
    hp_.flush(); lp_.flush();
  }

 private:
  // --- rates (events per second) -------------------------------------------
  float rateGain() const { return mode_ == ST_TV ? (2.f + 50.f * inst_) / 60.f : 0.f; }
  float rateCrackle() const {
    float base = 0.5f + 25.f * inst_ * sqrtf(inst_);
    // Proximity: ~5 /s as in the reference. The rate depends on the parameters
    // only -- a rate taken from the live intensity made each Strict pass roll
    // different intervals (v0.17); the intensity scales the crackle's level instead.
    if (mode_ == ST_PROXIMITY) return 3.f + 4.f * inst_;
    return mode_ == ST_TV ? base * 0.12f : base;
  }
  float rateSurge() const { return mode_ == ST_PROXIMITY ? (2.f + 8.f * inst_) / 60.f : 0.f; }
  float rateWhistle() const { return mode_ == ST_TUNING ? (3.f + 10.f * inst_) / 60.f : 0.f; }
  float rateVoice() const { return mode_ == ST_TUNING ? (1.5f + 6.f * inst_) / 60.f : 0.f; }
  int32_t interval(float rate) {
    if (rate <= 0.0001f) return 0x3FFFFFFF;
    float s = srng_.expo(1.f / rate);
    if (s > 3600.f) s = 3600.f;
    return (int32_t)(s * SR) + 1;
  }
  void clampWait(int32_t& w, float rate) {
    if (rate <= 0.0001f) return;
    if ((float)w > 4.f * SR / rate) w = interval(rate);
  }

  void schedule(int nb) {
    waitGain_ -= nb;
    if (waitGain_ <= 0) {
      if (mode_ == ST_TV) {
        if (srng_.chance(0.55f)) { gT_ = srng_.range(1.3f, 1.8f); gHold_ = (int)(srng_.range(0.08f, 0.4f) * SR); }
        else                     { gT_ = srng_.range(0.0f, 0.08f); gHold_ = (int)(srng_.range(0.03f, 0.25f) * SR); }
      }
      waitGain_ += interval(rateGain());
    }
    if (gHold_ > 0) { gHold_ -= nb; if (gHold_ <= 0) gT_ = 1.f; }
    waitCr_ -= nb;
    while (waitCr_ <= 0) {
      float a = srng_.range(0.2f, 1.f);
      if (a > crAmp_) crAmp_ = a;
      crDec_ = expf(-1.f / (SR * srng_.range(0.0002f, 0.0015f)));
      waitCr_ += interval(rateCrackle());
    }
    // The seed is drawn even if the previous whistle / voice is still going,
    // so a busy slot never shifts the scheduler stream (Strict determinism).
    waitWh_ -= nb;
    if (waitWh_ <= 0) {
      uint32_t s = srng_.next();
      for (auto& w : whs_) if (!w.active) { startWhistle(w, s); break; }
      waitWh_ += interval(rateWhistle());
    }
    waitVo_ -= nb;
    if (waitVo_ <= 0) {
      uint32_t s = srng_.next();
      for (auto& v : vos_) if (!v.active) { startVoice(v, s); break; }
      waitVo_ += interval(rateVoice());
    }
    waitSu_ -= nb;
    if (waitSu_ <= 0) {
      uint32_t s = srng_.next();
      startSurge(s);
      waitSu_ += interval(rateSurge());
    }
  }

  // --- Proximity ---------------------------------------------------------------
  // One "presence": rises to a peak and falls away over 3..12 s
  struct Surge { bool active; int t, len; float peak, rise; };
  // Up to six surges overlap and add up (v0.17): with a single slot, a surge
  // carried over a Strict loop boundary blocked the next pass's own surge.
  void startSurge(uint32_t seed) {
    Surge* p = nullptr;
    for (auto& s : sus_) if (!s.active) { p = &s; break; }
    if (!p) return;
    Surge& su = *p;
    Rng r; r.seed(seed);
    su.active = true;
    su.t = 0;
    su.len = (int)(r.range(3.f, 12.f) * SR);
    // Strict export (spec v1.21 4): at most 85 % of the loop, so a surge that
    // crosses the loop boundary is over within the next pass and the warm-up
    // passes settle it (v0.31: Presence 16 s was -42 dB from periodic).
    // Live playback and Crossfade keep the full length.
    if (gClk.loopLen) {
      const int cap = (int)(0.85f * (float)gClk.loopLen);
      if (su.len > cap) su.len = cap;
    }
    su.peak = r.range(0.5f, 1.f) * (0.55f + 0.45f * inst_);
    su.rise = r.range(0.3f, 0.75f);
  }
  float surgeLevel() const {
    float sum = 0.f;
    for (const auto& su : sus_) {
      if (!su.active) continue;
      float u = (float)su.t / (float)su.len;
      float sh = u < su.rise ? smooth01(u / su.rise) : smooth01((1.f - u) / (1.f - su.rise));
      sum += su.peak * sh;
    }
    return sum > 1.f ? 1.f : sum;
  }
  // v0.12, matched against a reference recording: a steady, muffled (800..3200 Hz)
  // grainy hiss with shallow irregular flutter and a few crackles per second.
  // The "something approaches" waves are layered on top by Instability: at low
  // values it simply keeps hissing, higher values add surges and deep troughs.
  void renderProximity(float* out, int n) {
    for (int i = 0; i < n; i += CTL) {
      int nb = (n - i < CTL) ? n - i : CTL;
      w1_.advance(i, nb); w2_.advance(i, nb); w3_.advance(i, nb);
      schedule(nb);
      for (auto& su : sus_) if (su.active) { su.t += nb; if (su.t >= su.len) su.active = false; }
      // presence: a floor that drops as Instability rises, plus the current surge
      float floorI = 0.75f - 0.5f * inst_;
      float tgt = floorI + (1.f - floorI) * surgeLevel() * (0.9f + 0.1f * w3_.sin() * gClk.lfoMul);
      tgt = clampf(tgt, 0.f, 1.f);
      if (first_) { proxI_ = tgt; first_ = false; }
      float dI = (tgt - proxI_) / nb;
      float tf = 0.7f + 0.6f * tone_;
      hp_.set(700.f * tf, 0.7f);
      lp_.set(3000.f * tf, 0.7f);
      lp2_.set(3400.f * tf, 0.6f);
      buzz_.setHz(90.f);
      crLp_.set(2500.f);
      rumLp_.set(60.f, 0.8f);
      r37_.setHz(37.f); r45_.setHz(45.f);
      // tuning sounds: seeds are always drawn, busy slots just skip (Strict determinism)
      waitSw_ -= nb;
      if (waitSw_ <= 0) { startSwoop(srng_.next()); waitSw_ += interval(rateSwoop()); }
      waitWh2_ -= nb;
      if (waitWh2_ <= 0) { startWhistle2(srng_.next()); waitWh2_ += interval(rateWhistle2()); }
      float rumA = 1.f - 0.3f * w2_.uni() * gClk.lfoMul;
      for (int j = 0; j < nb; j++) {
        float I = proxI_;
        // flutter: short irregular dips (a few ms), only 4..6 dB deep
        if (--gateCd_ <= 0) {
          gateOn_ = !gateOn_;
          gateCd_ = gateOn_ ? (int)(nrng_.range(0.003f, 0.025f) * SR)
                            : (int)(nrng_.range(0.002f, 0.012f) * SR);
          if (gateCd_ < 8) gateCd_ = 8;
          gateLo_ = nrng_.range(0.5f, 0.65f);
        }
        gate_ += ((gateOn_ ? 1.f : gateLo_) - gate_) * 0.08f;
        // slower wavering of the grain (a few Hz), also shallow
        if (--grainCd_ <= 0) {
          grainCd_ = (int)(nrng_.range(0.025f, 0.15f) * SR);
          grainT_ = nrng_.range(0.7f, 1.f);
        }
        grain_ += (grainT_ - grain_) * 0.002f;
        // crunch: overdriven, sample-and-held, coarsely quantised noise. The
        // value is drawn every sample (v0.37): how often it is held depends on
        // I, and the noise stream must not, or the first Strict pass (no
        // surges carried in) ends in a different place of it than the others.
        const float dn = nrng_.bi();
        if (--holdCnt_ <= 0) {
          holdCnt_ = 1 + (int)(2.f * I);
          float d = clampf(dn * (1.f + 6.f * I), -1.f, 1.f);
          float q = 4.f + 10.f * (1.f - I);
          held_ = roundf(d * q) / q;
        }
        float y = lp2_.lp(lp_.lp(hp_.hp(held_)));
        // crackle goes in after the band filters (they would round it off) so it
        // stands out of the hiss like the reference's clicks
        float cr = 0.f;
        const float cn = nrng_.bi();   // every sample (v0.37, see the radio path)
        if (crAmp_ > 0.0005f) { cr = crLp_.lp(cn * crAmp_) * CRACKLE_GAIN * (0.4f + 0.6f * I); crAmp_ *= crDec_; }
        else { crAmp_ = 0.f; crLp_.z = 0.f; }
        y += cr;
        float bz = 1.f - 0.12f * I * (0.5f + 0.5f * softSat(3.f * sinL(buzz_.tick())));   // faint grit only
        float tun = 0.f;
        for (auto& w : tones_) if (w.on) tun += toneSample(w);
        float rum = (rumLp_.lp(nrng_.bi()) * 3.f + 0.25f * sinL(r37_.tick()) + 0.15f * sinL(r45_.tick())) * rumA * RUMBLE_GAIN;
        out[i + j] = (y * gate_ * grain_ * bz * (0.2f + 0.8f * I) + tun * (0.5f + 0.5f * I) + rum) * OUT_SCALE * PROX_GAIN;
        proxI_ += dI;
      }
      proxI_ = tgt;
    }
    hp_.flush(); lp_.flush(); lp2_.flush(); rumLp_.flush();
  }
  // --- Proximity tuning sounds (v0.14) ------------------------------------------
  // Measured in a clean 13 s reference recording (v0.13's four fixed wavering
  // whistles came from a noisier one and were wrong):
  //   swoops   1.1..1.7 kHz tones falling ~150..350 cents within ~0.2 s
  //   whistles 0.2..0.5 s near-steady tones at scattered pitches
  //            (~900 Hz, ~2.5 kHz, 3.4..5.2 kHz; the high ones much fainter)
  //   rumble   a broad 40..70 Hz rumble plus faint lines at 37 and 45 Hz
  // (v0.14 also had the recording's dull pops; they read as room ambience
  // rather than radio, so v0.15 moved them to their own layer, BUMP.)
  // Every swoop / whistle is an event with its own Rng (phase from 0,
  // short fades), so Strict loops stay exact.
  struct Tone { bool on; int t, len, fin, fout; float ph, f, fMul, amp, invOut; };
  static constexpr int NUM_TONES = 6;
  // v0.15: swoops +6 dB, high whistles +8 dB; v0.16: both +4 dB more
  static constexpr float SWOOP_GAIN = 0.8f, WHISTLE_GAIN = 0.1f;
  static constexpr float RUMBLE_GAIN = 0.8f;
  float rateSwoop() const { return mode_ == ST_PROXIMITY ? 0.1f + 0.4f * inst_ : 0.f; }
  float rateWhistle2() const { return mode_ == ST_PROXIMITY ? 0.4f + 0.8f * inst_ : 0.f; }

  Tone* freeTone() { for (auto& x : tones_) if (!x.on) return &x; return nullptr; }
  void startSwoop(uint32_t seed) {
    Rng r; r.seed(seed);
    Tone* p = freeTone(); if (!p) return;
    Tone& w = *p;
    w.on = true; w.t = 0; w.ph = 0.f;
    w.f = r.range(1100.f, 1700.f);
    float dur = r.range(0.15f, 0.3f);
    w.len = (int)(dur * SR);
    float cents = r.chance(0.85f) ? -r.range(120.f, 350.f) : r.range(150.f, 300.f);
    w.fMul = powf(2.f, cents / 1200.f / (float)w.len);
    w.fin = ms2s(15); w.fout = ms2s(40); w.invOut = 1.f / (float)w.fout;
    w.amp = SWOOP_GAIN * r.range(0.7f, 1.f);
  }
  void startWhistle2(uint32_t seed) {
    Rng r; r.seed(seed);
    Tone* p = freeTone(); if (!p) return;
    Tone& w = *p;
    w.on = true; w.t = 0; w.ph = 0.f;
    float u = r.uni();
    w.f = u < 0.4f ? r.range(850.f, 1000.f) : (u < 0.65f ? r.range(2400.f, 2700.f) : r.range(3400.f, 5200.f));
    w.len = (int)(r.range(0.2f, 0.5f) * SR);
    float cents = r.range(-120.f, 120.f) * (0.5f + inst_);
    w.fMul = powf(2.f, cents / 1200.f / (float)w.len);
    w.fin = w.fout = ms2s(20); w.invOut = 1.f / (float)w.fout;
    w.amp = WHISTLE_GAIN * (w.f < 1500.f ? 1.f : 1.2f) * r.range(0.6f, 1.f);
  }
  inline float toneSample(Tone& w) {
    float env = atkRamp(w.t, w.fin);
    int left = w.len - w.t;
    if (left < w.fout) env *= (float)left * w.invOut;   // v0.32: no per-sample divide
    float o = sinL(w.ph) * env * w.amp;
    w.ph += w.f * INV_SR; if (w.ph >= 1.f) w.ph -= 1.f;
    w.f *= w.fMul;
    if (++w.t >= w.len) w.on = false;
    return o;
  }

  // --- heterodyne whistle ---------------------------------------------------
  struct Whistle { bool active; int t, len, atk, rel; float f1, f2, f, ph, amp, vib, vibPh, invRel; };
  void startWhistle(Whistle& w, uint32_t seed) {
    Rng r; r.seed(seed);
    w.active = true;
    w.t = 0;
    w.len = (int)(r.range(0.6f, 3.2f) * SR);
    w.atk = ms2s(40); w.rel = ms2s(120); w.invRel = 1.f / (float)w.rel;
    w.f1 = r.range(400.f, 3000.f);
    w.f2 = r.range(300.f, 3200.f);
    w.ph = 0.f;
    w.amp = r.range(0.08f, 0.2f);
    w.vib = r.range(0.002f, 0.01f);
    w.vibPh = 0.f;
  }
  // Sweep position is evaluated at control rate; the oscillator itself per sample
  void updateWhistle(Whistle& w) {
    float u = (float)w.t / (float)w.len;
    // up-and-over "hwee-oo" sweep: the ratio passes f2 and settles back a little
    float s = smooth01(u * 1.3f) - 0.25f * smooth01((u - 0.6f) * 2.5f);
    w.f = w.f1 * powf(w.f2 / w.f1, s);
  }
  inline float whistleSample(Whistle& w) {
    w.vibPh += 5.3f * INV_SR; if (w.vibPh >= 1.f) w.vibPh -= 1.f;
    float f = w.f * (1.f + w.vib * sinL(w.vibPh));
    float env = atkRamp(w.t, w.atk);
    int left = w.len - w.t;
    if (left < w.rel) env *= (float)left * w.invRel;
    float o = sinL(w.ph) * env * w.amp;
    w.ph += f * INV_SR; if (w.ph >= 1.f) w.ph -= 1.f;
    if (++w.t >= w.len) w.active = false;
    return o;
  }

  // --- voice-like fragment ----------------------------------------------------
  struct VoiceFrag { bool active; int t, len, atk, rel; int va, vb; float syl, sylPh, amp, invRel; Rng r; Svf f[3]; };
  void startVoice(VoiceFrag& v, uint32_t seed) {
    v.r.seed(seed);
    Rng& r = v.r;
    v.active = true;
    v.t = 0;
    v.len = (int)(r.range(0.15f, 0.7f) * SR);
    v.atk = ms2s(20); v.rel = ms2s(40); v.invRel = 1.f / (float)v.rel;
    v.va = r.irange(0, 4);
    v.vb = r.irange(0, 4);
    v.syl = r.range(4.f, 7.f);
    v.sylPh = r.uni();
    v.amp = r.range(0.25f, 0.45f);
    for (int k = 0; k < 3; k++) v.f[k].clear();
    updateVoiceFilters(v);
  }
  void updateVoiceFilters(VoiceFrag& v) {
    float u = smooth01((float)v.t / (float)v.len);
    const Vowel& a = VOWELS[v.va]; const Vowel& b = VOWELS[v.vb];
    v.f[0].set(lerpf(a.f1, b.f1, u), 8.f);
    v.f[1].set(lerpf(a.f2, b.f2, u), 10.f);
    v.f[2].set(lerpf(a.f3, b.f3, u), 12.f);
  }
  inline float voiceSample(VoiceFrag& v) {
    float x = v.r.bi();
    float y = v.f[0].bpn(x) + 0.7f * v.f[1].bpn(x) + 0.35f * v.f[2].bpn(x);
    v.sylPh += v.syl * INV_SR; if (v.sylPh >= 1.f) v.sylPh -= 1.f;
    float am = 0.55f + 0.45f * sinL(v.sylPh);
    float env = atkRamp(v.t, v.atk);
    int left = v.len - v.t;
    if (left < v.rel) env *= (float)left * v.invRel;
    if (++v.t >= v.len) v.active = false;
    return y * am * env * v.amp;
  }

  Rng nrng_, srng_;
  Lfo w1_, w2_, w3_;
  Phasor hum_;
  float humHz_ = 50.f;
  Svf hp_, lp_;
  int mode_ = ST_TV;
  float tone_ = 0.5f, inst_ = 0.4f;
  float g_ = 1.f, gT_ = 1.f; int gHold_ = 0;
  float crAmp_ = 0.f, crDec_ = 0.9f;
  float recvS_ = 1.f;
  bool first_ = true;
  static constexpr float OUT_SCALE = 0.24f;  // v0.13: -2 dB more (v0.11: 0.3, v0.1: 0.55); broadband hiss reads louder than its RMS
  int32_t waitGain_ = 0, waitCr_ = 0, waitWh_ = 0, waitVo_ = 0, waitSu_ = 0;
  Surge sus_[6] = {};
  Phasor buzz_;
  static constexpr float PROX_GAIN = 1.6f;
  static constexpr float CRACKLE_GAIN = 5.f;
  OnePole crLp_;
  Svf lp2_;
  float proxI_ = 0.f, gate_ = 1.f, held_ = 0.f, gateLo_ = 0.4f, grain_ = 1.f, grainT_ = 1.f;
  int grainCd_ = 1;
  Tone tones_[6] = {};   // v0.17: 6 (was 3) so a busy slot never skips a tone in Strict loops
  Svf rumLp_;
  Phasor r37_, r45_;
  int32_t waitSw_ = 0, waitWh2_ = 0;
  bool gateOn_ = false;
  int gateCd_ = 1, holdCnt_ = 0;
  Whistle whs_[2] = {};
  VoiceFrag vos_[2] = {};
};

Layer* createStatic() { return new StaticLayer(); }
