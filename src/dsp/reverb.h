// MIASMORA — Schroeder reverb (4 parallel damped combs + 2 allpasses)
//
// Based on Enso's Space reverb. Differences:
//  * Size scales every delay length (1x .. 3x) as well as the feedback, so the
//    tail can reach ruin / cave lengths (RT60 well past 8 s at the top end).
//  * Delay lengths are re-picked as distinct primes whenever Size changes, so
//    the echo patterns never line up into a ringing tone at any size.
//  * Changing a delay length while the reverb is running would click, so the
//    wet signal is ducked to zero for one buffer, the lengths are swapped, and
//    the wet ramps back up over the next one.
#pragma once
#include "dsp.h"

class Reverb {
 public:
  static constexpr int NCOMB = 4, NAP = 2;
  static constexpr int COMB_MAX = 2048, AP_MAX = 560;

  void clear();
  // mix, size, damp: 0..1. Call once per buffer before process().
  void setParams(float mix, float size, float damp, bool snap);
  // In place: x <- dry + wet
  void process(float* x, int n);
  bool idle() const { return skipping_; }

 private:
  void applySize(int sizeQ);

  float comb_[NCOMB][COMB_MAX];
  float ap_[NAP][AP_MAX];
  int   clen_[NCOMB] = {557, 593, 641, 677};
  int   alen_[NAP] = {113, 277};
  int   ci_[NCOMB] = {0}, ai_[NAP] = {0};
  float clp_[NCOMB] = {0};
  float fb_ = 0.8f, damp_ = 0.3f;
  float mix_ = 0.f, mixTarget_ = 0.f;
  int   sizeQ_ = -1, wantSizeQ_ = 50;
  float duck_ = 1.f;
  bool  ducking_ = false;
  bool  skipping_ = true;
};
