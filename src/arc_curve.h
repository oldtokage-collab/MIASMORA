// MIASMORA — Arc's shape (spec v1.1 6.1 / 6.2), shared with the PC test bench
//
// Progress 0..1 runs through Calm (0..30 %), Build (30..70 %), Peak (70..85 %)
// and Release (85..100 %). intensity() is 0 in Calm and 1 in Peak; Build and
// Release are smoothsteps, so nothing jumps at a section boundary (the slope
// is zero at both ends of each).
#pragma once
#include <math.h>

namespace arc {

enum Section { CALM = 0, BUILD, PEAK, RELEASE, NUM_SECTIONS };

constexpr float BUILD_AT = 0.30f, PEAK_AT = 0.70f, RELEASE_AT = 0.85f;
constexpr float DREAD_CALM = -20.f, DREAD_PEAK = 30.f;   // added to DREAD
// Event-rate multiplier. v0.51: the spec's 0.5 / 1.8 put the climax at 2.5x,
// which with all 25 layers took the DSP load past the limit (p99 88). Now the
// climax tops out at 2.0x (PEAK x the climax's 1.4) and Calm is quieter, so
// the rise is wider than before (Calm -> Peak 4.1x, was 3.6x).
constexpr float DENS_CALM = 0.35f, DENS_PEAK = 2.0f / 1.4f;
constexpr float DENS_CAP = 2.0f;   // Arc x Listen (spec v1.1 6.4 said 2.5)

inline float smooth01(float x) {
  x = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
  return x * x * (3.f - 2.f * x);
}

inline float intensity(float p) {
  if (p < BUILD_AT) return 0.f;
  if (p < PEAK_AT) return smooth01((p - BUILD_AT) / (PEAK_AT - BUILD_AT));
  if (p < RELEASE_AT) return 1.f;
  return 1.f - smooth01((p - RELEASE_AT) / (1.f - RELEASE_AT));
}

inline Section section(float p) {
  return p < BUILD_AT ? CALM : p < PEAK_AT ? BUILD : p < RELEASE_AT ? PEAK : RELEASE;
}

inline float dreadAdd(float level) { return DREAD_CALM + (DREAD_PEAK - DREAD_CALM) * level; }
// geometric, so each step up sounds like the same step
inline float densMul(float level) { return DENS_CALM * powf(DENS_PEAK / DENS_CALM, level); }

}  // namespace arc
