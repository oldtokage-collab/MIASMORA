#include "speed.h"   // first: speed-optimised (v0.32)
#include "reverb.h"
#include <string.h>

static const int COMB_BASE[Reverb::NCOMB] = {557, 593, 641, 677};   // samples @22050 Hz
static const int AP_BASE[Reverb::NAP]     = {113, 277};

static bool isPrime(int n) {
  if (n < 2) return false;
  if (n % 2 == 0) return n == 2;
  for (int d = 3; d * d <= n; d += 2) if (n % d == 0) return false;
  return true;
}
static int primeNear(int n, int maxv, const int* used, int nUsed) {
  if (n > maxv) n = maxv;
  for (int k = n; k > 2; k--) {
    if (!isPrime(k)) continue;
    bool dup = false;
    for (int u = 0; u < nUsed; u++) if (used[u] == k) dup = true;
    if (!dup) return k;
  }
  return n;
}

void Reverb::clear() {
  memset(comb_, 0, sizeof(comb_));
  memset(ap_, 0, sizeof(ap_));
  memset(ci_, 0, sizeof(ci_));
  memset(ai_, 0, sizeof(ai_));
  memset(clp_, 0, sizeof(clp_));
  sizeQ_ = -1;
  mix_ = 0.f;
  duck_ = 1.f;
  ducking_ = false;
  skipping_ = true;
}

void Reverb::applySize(int sizeQ) {
  float scale = 1.f + 2.f * (sizeQ / 100.f);
  int used[NCOMB + NAP]; int nu = 0;
  for (int i = 0; i < NCOMB; i++) {
    clen_[i] = primeNear((int)(COMB_BASE[i] * scale), COMB_MAX - 1, used, nu);
    used[nu++] = clen_[i];
    if (ci_[i] >= clen_[i]) ci_[i] = 0;
  }
  float aScale = 1.f + (sizeQ / 100.f);
  for (int i = 0; i < NAP; i++) {
    alen_[i] = primeNear((int)(AP_BASE[i] * aScale), AP_MAX - 1, used, nu);
    used[nu++] = alen_[i];
    if (ai_[i] >= alen_[i]) ai_[i] = 0;
  }
  sizeQ_ = sizeQ;
}

void Reverb::setParams(float mix, float size, float damp, bool snap) {
  mixTarget_ = dsp::clampf(mix, 0.f, 1.f);
  wantSizeQ_ = (int)(dsp::clampf(size, 0.f, 1.f) * 100.f + 0.5f);
  fb_ = 0.70f + 0.235f * (wantSizeQ_ / 100.f);
  damp_ = 0.10f + 0.75f * dsp::clampf(damp, 0.f, 1.f);
  if (snap) {
    mix_ = mixTarget_;
    applySize(wantSizeQ_);
    duck_ = 1.f;
    ducking_ = false;
  }
}

void Reverb::process(float* x, int n) {
  // Fully silent and wanted silent: skip the whole thing (and start clean later)
  if (mix_ < 0.0005f && mixTarget_ < 0.0005f) {
    if (!skipping_) {
      memset(comb_, 0, sizeof(comb_));
      memset(ap_, 0, sizeof(ap_));
      memset(clp_, 0, sizeof(clp_));
      skipping_ = true;
    }
    mix_ = 0.f;
    if (sizeQ_ != wantSizeQ_) applySize(wantSizeQ_);   // free to swap while silent
    return;
  }
  skipping_ = false;

  if (sizeQ_ < 0) applySize(wantSizeQ_);
  // Size change: duck out over this buffer, swap at the end, duck back in on the next
  float duckFrom = duck_, duckTo = duck_;
  bool swapAfter = false;
  if (sizeQ_ != wantSizeQ_ && !ducking_) { ducking_ = true; }
  if (ducking_) { duckTo = 0.f; swapAfter = true; }
  else if (duck_ < 1.f) duckTo = 1.f;
  float dStep = (duckTo - duckFrom) / n;

  float mNext = mix_ + (mixTarget_ - mix_) * 0.3f;
  if (fabsf(mNext - mixTarget_) < 0.0005f) mNext = mixTarget_;
  float m = mix_, mStep = (mNext - mix_) / n;
  float d = duckFrom;
  const float fb = fb_, damp = damp_, und = 1.f - damp_;

  for (int s = 0; s < n; s++) {
    float in = x[s] * 0.25f;
    float acc = 0.f;
    for (int i = 0; i < NCOMB; i++) {
      float* cb = comb_[i];
      float y = cb[ci_[i]];
      clp_[i] = y * und + clp_[i] * damp;
      cb[ci_[i]] = dsp::flushDn(in + clp_[i] * fb);
      if (++ci_[i] >= clen_[i]) ci_[i] = 0;
      acc += y;
    }
    float w = acc * 0.25f;
    for (int i = 0; i < NAP; i++) {
      float* ab = ap_[i];
      float b = ab[ai_[i]];
      float t = w + b * 0.5f;
      ab[ai_[i]] = dsp::flushDn(t);
      if (++ai_[i] >= alen_[i]) ai_[i] = 0;
      w = b - t * 0.5f;
    }
    x[s] = x[s] * (1.f - 0.3f * m) + w * m * 1.3f * d;
    m += mStep;
    d += dStep;
  }
  mix_ = mNext;
  duck_ = duckTo;
  for (int i = 0; i < NCOMB; i++) clp_[i] = dsp::flushDn(clp_[i]);
  if (swapAfter) { applySize(wantSizeQ_); ducking_ = false; }
}
