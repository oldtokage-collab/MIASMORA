// MIASMORA — Party Mode's music (v0.65). See party_synth.h.
#include "dsp/speed.h"   // first: speed-optimised (v0.32)
#include "party_synth.h"
#include "config.h"

using namespace dsp;

// 16 steps per bar; x = hit, g = ghost (softer)
//                                   0123456789ABCDEF
static const char* const KICK[NUM_PARTY_BEATS]  = {"x...x...x...x...",   // Four
                                                   "x.........x.....",   // Half
                                                   "x......x..x.....",   // Break
                                                   "x.........x....."};  // DnB (twice as fast)
static const char* const CLAP[NUM_PARTY_BEATS]  = {"....x.......x...",
                                                   "........x.......",
                                                   "....x..g....x..g",
                                                   "....x.......x..."};
static const char* const STAB = "..x..x...x..x..x";
static const char* const BOUNCE = "..x...x...x...x.";

// chord progressions (4 bars each): root in semitones from the key, and the chord
struct ChordStep { int8_t root; uint8_t kind; };   // kind: 0 maj, 1 min, 2 sus4, 3 sus2
static const ChordStep PROG[NUM_PARTY_CHORDS][4] = {
  {{0, 0}, {0, 0}, {0, 0}, {0, 0}},                 // Off (unused)
  {{0, 0}, {7, 0}, {9, 1}, {5, 0}},                 // Major: I V vi IV
  {{0, 1}, {8, 0}, {3, 0}, {10, 0}},                // Minor: i VI III VII
  {{0, 2}, {5, 3}, {7, 2}, {5, 3}},                 // Sus: Isus4 IVsus2 Vsus4 IVsus2
};
static const int8_t VOICE[4][4] = {{0, 4, 7, 12}, {0, 3, 7, 12}, {0, 5, 7, 12}, {0, 2, 7, 12}};

static constexpr float INV_SR_F = 1.f / SR;
static constexpr int32_t CLAP_GAP = (int32_t)(0.008f * SR);

static float noteHz(float semisFromA220) { return 220.f * exp2f(semisFromA220 / 12.f); }

void PartySynth::reset(uint32_t seed) {
  r_.seed(seed ^ 0x9A27u);   // its own stream (not a layer's)
  samplesToStep_ = 0.f; step_ = 0; bar_ = 0; beats_ = 0; beatPh_ = 0.f;
  dSamples_ = 0.f; dStep_ = 0;
  fillBar_ = fillPending_ = false;
  kPh_ = kEnv_ = kPitch_ = kClick_ = kDub_ = 0.f; kDubIn_ = -1;
  kLp_.clear(); cBp_.clear(); hHp_.clear(); bLp_.clear(); chLp_.clear(); dc_.clear();
  cEnv_ = 0.f; cT_ = 0; cTailEnv_ = 0.f; hEnv_ = 0.f; bEnv_ = 0.f; bLfo_ = 0.f; chEnv_ = 0.f;
  cBp_.set(1100.f, 1.3f);
  hHp_.set(7000.f, 0.8f);
  chordIdx_ = 0;
}

void PartySynth::triggerDrums(int step, const PartyParams& p, bool fill) {
  const int beat = p.beat < NUM_PARTY_BEATS ? p.beat : 0;
  const char k = KICK[beat][step], c = CLAP[beat][step];
  // the kick (the heartbeat's "lub", and its "dub" while it is still a heart)
  bool kick = k == 'x' || (fill && (step == 14 || step == 15) && r_.chance(0.6f));
  if (kick) {
    kEnv_ = 1.f; kPitch_ = 1.f; kClick_ = 1.f;
    kDubIn_ = p.heart < 0.95f ? (int32_t)(0.27f * SR) : -1;
  }
  if (p.kickOnly) return;
  // clap: on its steps, rolls in a fill
  float ca = c == 'x' ? 1.f : (c == 'g' ? 0.35f : 0.f);
  if (fill && step >= 8) ca = fmaxf(ca, step >= 12 ? 0.5f + 0.12f * (step - 12) : (step & 1 ? 0.f : 0.4f));
  if (ca > 0.f) { cEnv_ = ca; cT_ = 0; cTailEnv_ = 0.45f; }
  // hats
  const bool off8 = (step & 3) == 2;
  bool hat = p.hat == PH_16TH || (p.hat == PH_8TH && off8) || (fill && p.hat != PH_OFF);
  if (hat) {
    const bool open = off8 && step == 14 && r_.chance(0.35f);
    hEnv_ = 1.f;
    hAmp_ = open ? 0.8f : (off8 ? 0.7f : 0.4f);
    hDec_ = expf(-1.f / ((open ? 0.18f : 0.028f) * SR));
  }
}

// bass and chords: always on the bar's own sixteenths (v0.68: DnB ran them
// twice as fast with the drums, out of step with how it felt)
void PartySynth::triggerMelody(int step, const PartyParams& p) {
  if (p.kickOnly) return;
  // bass
  const ChordStep cs = PROG[p.chord < NUM_PARTY_CHORDS && p.chord ? p.chord : PC_MINOR][chordIdx_ & 3];
  const float root = (float)((int)p.key - 9 + cs.root);   // semitones from A
  if (p.bass == PBS_BOUNCE && BOUNCE[step] == 'x') { bEnv_ = 1.f; bInc_ = noteHz(root - 24.f) * INV_SR_F; }
  if (p.bass == PBS_ROLL) {
    bEnv_ = (step & 3) == 0 ? 1.f : 0.75f;
    bInc_ = noteHz(root - 24.f + ((step & 7) == 6 ? 12.f : 0.f)) * INV_SR_F;   // an octave jump now and then
  }
  // chord stabs
  if (p.chord != PC_OFF && STAB[step] == 'x') {
    chEnv_ = 1.f;
    const int8_t* v = VOICE[cs.kind & 3];
    float base = root;
    if (base < -9.f) base += 12.f;   // keep the voicing around A3
    if (base > 2.f) base -= 12.f;
    for (int i = 0; i < 4; i++) chInc_[i] = noteHz(base + v[i] + (i & 1 ? 0.06f : -0.06f)) * INV_SR_F;
  }
}

DSP_INLINE float PartySynth::kickTick(const PartyParams& p) {
  // the "dub" of a heartbeat, fading as it becomes a kick
  if (kDubIn_ >= 0 && --kDubIn_ < 0) { kEnv_ = fmaxf(kEnv_, 0.55f * (1.f - p.heart)); kPitch_ = 0.6f; }
  const float kp = p.kick * 0.01f, pu = p.punch * 0.01f * p.heart;
  const float fEnd = 70.f - 26.f * kp, fStart = fEnd + 40.f + 160.f * pu;
  const float f = fEnd + (fStart - fEnd) * kPitch_;
  kPh_ += f * INV_SR_F; if (kPh_ >= 1.f) kPh_ -= 1.f;
  float y = sinL(kPh_) * kEnv_;
  y += kClick_ * pu * 0.6f * r_.bi();                  // the attack's click
  return softSat(y * (1.2f + 0.8f * pu)) * 0.9f;
}

void PartySynth::render(float* out, int n, const PartyParams& p) {
  if (!p.on) return;
  const int tempo = p.tempo < 60 ? 124 : p.tempo;
  const bool dnb = p.beat == PB_DNB && !p.kickOnly;
  const float stepLen = SR * 60.f / (tempo * 4.f);   // a sixteenth
  const float sw = p.swing * 0.01f * 0.6f;
  const float beatLen = SR * 60.f / tempo;
  if (p.fillSeq != lastFill_) { lastFill_ = p.fillSeq; fillPending_ = true; }
  // per buffer: decays and the heart's muffling
  const float kp = p.kick * 0.01f;
  const float kDec = expf(-1.f / ((0.12f + 0.45f * kp) * SR));
  const float pDec = expf(-1.f / ((0.025f + 0.02f * kp) * SR));
  const float clickDec = expf(-1.f / (0.003f * SR));
  kLp_.set(150.f + p.heart * p.heart * 7000.f);   // a heart heard from inside .. an open kick
  const float bDec = expf(-1.f / ((p.bass == PBS_ROLL ? 0.09f : 0.14f) * SR));
  const float chDec = expf(-1.f / (0.2f * SR));
  // bass filter: Roll wobbles in eighths
  bLfo_ += n / (beatLen * 0.5f);
  if (bLfo_ >= 1.f) bLfo_ -= floorf(bLfo_);
  bLp_.set(p.bass == PBS_ROLL ? 220.f + 700.f * (0.5f + 0.5f * sinL(bLfo_)) : 600.f, 1.4f);
  chLp_.set(2400.f, 0.9f);
  const float lvl = p.level;

  for (int s = 0; s < n; s++) {
    // the bar: bass, chords, and the beat (the screen and LED follow it)
    if (samplesToStep_ <= 0.f) {
      if (!dnb) {   // the drums on the same sixteenths
        if (step_ == 0) { fillBar_ = fillPending_; fillPending_ = false; }
        triggerDrums(step_, p, fillBar_);
      }
      triggerMelody(step_, p);
      if ((step_ & 3) == 0 && (step_ || bar_)) beats_++;
      samplesToStep_ += stepLen * ((step_ & 1) ? 1.f - sw : 1.f + sw);
      if (++step_ >= 16) {
        step_ = 0; bar_++;
        if ((bar_ & 3) == 0) chordIdx_ = (chordIdx_ + 1) & 3;   // a new chord every 4 bars
      }
    }
    samplesToStep_ -= 1.f;
    // DnB: the drums twice as fast, in step with the bar (a drum pattern every half bar)
    if (dnb) {
      if (dSamples_ <= 0.f) {
        if (dStep_ == 0) { fillBar_ = fillPending_; fillPending_ = false; }
        triggerDrums(dStep_, p, fillBar_);
        dSamples_ += stepLen * 0.5f * ((dStep_ & 1) ? 1.f - sw : 1.f + sw);
        if (++dStep_ >= 16) dStep_ = 0;
      }
      dSamples_ -= 1.f;
    } else {
      dSamples_ = samplesToStep_; dStep_ = (step_ * 2) & 15;   // ready to switch to DnB in step
    }
    beatPh_ = ((step_ + 3) % 4 + 1.f - fmaxf(0.f, samplesToStep_) / stepLen) * 0.25f;   // within the beat
    if (beatPh_ >= 1.f) beatPh_ -= 1.f;

    float y = kLp_.lp(kickTick(p));
    kEnv_ *= kDec; kPitch_ *= pDec; kClick_ *= clickDec;
    if (!p.kickOnly) {
      const float w = r_.bi();
      // clap: three quick bursts, then a short tail
      float cl = 0.f;
      if (cEnv_ > 0.f) {
        // three 8 ms bursts (each a quick drop), then a ~0.1 s tail
        const int32_t t = cT_++;
        float e;
        if (t < 3 * CLAP_GAP) { const int32_t ph = t % CLAP_GAP; e = ph < CLAP_GAP / 2 ? 1.f - ph * (2.f / CLAP_GAP) : 0.f; }
        else { e = cTailEnv_; cTailEnv_ *= 0.9994f; if (cTailEnv_ < 0.002f) cEnv_ = 0.f; }
        cl = cBp_.bpn(w) * cEnv_ * e;
      }
      // hats
      float ht = 0.f;
      if (hEnv_ > 0.0005f) { ht = hHp_.hp(w) * hEnv_ * hAmp_; hEnv_ *= hDec_; }
      // bass: a sine with a little growl
      float bs = 0.f;
      if (bEnv_ > 0.0005f) {
        bPh_ += bInc_; if (bPh_ >= 1.f) bPh_ -= 1.f;
        const float sn = sinL(bPh_);
        bs = bLp_.lp(sn + 0.35f * softSat(sn * 3.f)) * bEnv_;
        bEnv_ *= bDec;
      }
      // chords: detuned saws
      float ch = 0.f;
      if (chEnv_ > 0.0005f) {
        for (int i = 0; i < 4; i++) {
          float ph = chPh_[i] + chInc_[i];
          if (ph >= 1.f) ph -= 1.f;
          chPh_[i] = ph;
          ch += 2.f * ph - 1.f;
        }
        ch = chLp_.lp(ch * 0.25f) * chEnv_;
        chEnv_ *= chDec;
      }
      y += 0.38f * cl + 0.16f * ht + 0.5f * bs + 0.24f * ch;
    }
    out[s] += dc_.tick(y) * lvl;
  }
  kLp_.flush(); cBp_.flush(); hHp_.flush(); bLp_.flush(); chLp_.flush(); dc_.flush();
  kEnv_ = flushDn(kEnv_); cEnv_ = flushDn(cEnv_); hEnv_ = flushDn(hEnv_); bEnv_ = flushDn(bEnv_); chEnv_ = flushDn(chEnv_);
}
