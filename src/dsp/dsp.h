// MIASMORA — DSP building blocks (header-only, used by the audio and export paths)
//
// Every rule in here exists because breaking it caused a real bug in Enso:
//  * sinL()      linear-interpolated sine. A plain table lookup quantises the
//                phase and buzzes on any sustained, smooth tone.
//  * Reson       2-pole resonator scaled by sin(w). Dividing by (1-r) silences
//                low resonators hit by short bursts; no scaling at all makes
//                them explode. sin(w) gives a peak of ~1 for an impulse of 1.
//  * flushDn()   denormal guard for long decays (reverb, filters).
//  * Ditherer    TPDF dither + round-to-nearest. Truncation produced a
//                signal-correlated grit on quiet tails.
#pragma once
#include <stdint.h>
#include <math.h>
#include "../config.h"

// v0.32: the per-sample helpers below must be inlined. The Arduino builder
// compiles for size (-Os), and there every one of them was a call per voice
// and per sample (v0.31: the five new drones added 63 % DSP load on the device).
#define DSP_INLINE inline __attribute__((always_inline))

namespace dsp {

// ---------------------------------------------------------------------------
// Sine table (linear interpolation)
// ---------------------------------------------------------------------------
static constexpr int SINE_N = 1024;
extern float sineTab[SINE_N + 1];   // one guard point, so i+1 never needs masking
void init();

// ph in cycles, expected in [0, 1)
static DSP_INLINE float sinL(float ph) {
  float x = ph * SINE_N;
  int i = (int)x;
  float f = x - (float)i;
  i &= SINE_N - 1;
  return sineTab[i] + (sineTab[i + 1] - sineTab[i]) * f;
}
// Any real phase (may be negative or > 1). Same result as x - floorf(x) for
// |x| < 2^31, without the library call (v0.32).
static DSP_INLINE float wrap01(float x) {
  float i = (float)(int32_t)x;   // truncates toward zero
  if (i > x) i -= 1.f;
  return x - i;
}
static DSP_INLINE float sinW(float ph) { return sinL(wrap01(ph)); }

static DSP_INLINE float flushDn(float x) { return (fabsf(x) < 1e-15f) ? 0.f : x; }
static DSP_INLINE float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static DSP_INLINE float lerpf(float a, float b, float t) { return a + (b - a) * t; }
// Exponential map of t (0..1) onto lo..hi
static inline float expMap(float t, float lo, float hi) { return lo * powf(hi / lo, t); }
static DSP_INLINE float smooth01(float t) { t = clampf(t, 0.f, 1.f); return t * t * (3.f - 2.f * t); }

// Cheap tanh-like saturator (Pade), exact clamp outside +-3.
// v0.32: no division. The ESP32-S3 has no FP divide (a library call), so
// 1 / (27 + 9x^2), always within [27, 108], is a linear first guess refined by
// three Newton steps (relative error ~5e-6).
static DSP_INLINE float softSat(float x) {
  if (x > 3.f) return 1.f;
  if (x < -3.f) return -1.f;
  float x2 = x * x;
  float d = 27.f + 9.f * x2;
  float r = 0.0361340f - 2.67658e-4f * d;   // minimax linear 1/d on [27, 108] (rel. error 0.22)
  r = r * (2.f - d * r);
  r = r * (2.f - d * r);
  r = r * (2.f - d * r);
  return x * (27.f + x2) * r;
}

// polyBLEP residual for band-limited saw / pulse
static DSP_INLINE float polyBlep(float t, float dt) {
  if (dt <= 0.f) return 0.f;
  if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
  if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
  return 0.f;
}
static DSP_INLINE float sawBL(float t, float dt) { return 2.f * t - 1.f - polyBlep(t, dt); }

// ---------------------------------------------------------------------------
// Random numbers
// ---------------------------------------------------------------------------
static inline uint32_t splitmix32(uint32_t& x) {
  uint32_t z = (x += 0x9E3779B9u);
  z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
  z = (z ^ (z >> 13)) * 0xC2B2AE35u;
  return z ^ (z >> 16);
}
// A stream seed for (user seed, stream id). Used to give every layer, the
// drift engine, the dither etc. their own independent sequence.
static inline uint32_t deriveSeed(uint32_t seed, uint32_t stream) {
  uint32_t x = seed ^ (stream * 0x9E3779B9u) ^ 0x5BD1E995u;
  splitmix32(x);
  return splitmix32(x);
}

// xorshift32. Never seeded with a raw value: the seed goes through splitmix32
// first, and a zero state (which would lock xorshift up) is replaced.
struct Rng {
  uint32_t s = 0x6D2B79F5u;
  void seed(uint32_t v) {
    uint32_t x = v;
    s = splitmix32(x);
    if (s == 0) s = 0x6D2B79F5u;
  }
  DSP_INLINE uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  DSP_INLINE float uni() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
  DSP_INLINE float bi() { return uni() * 2.f - 1.f; }                              // [-1,1)
  DSP_INLINE float range(float a, float b) { return a + (b - a) * uni(); }
  inline int irange(int lo, int hi) { return hi <= lo ? lo : lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
  DSP_INLINE bool chance(float p) { return uni() < p; }
  // Exponentially distributed interval with the given mean (Poisson process)
  inline float expo(float mean) { return -mean * logf(1.0f - uni()); }
};

// ---------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------
// Topology-preserving SVF (Simper). Stable under fast cutoff modulation, which
// the Chamberlin form is not. Coefficients are updated at control rate only.
struct Svf {
  float ic1 = 0.f, ic2 = 0.f;
  float a1 = 1.f, a2 = 0.f, a3 = 0.f, k = 1.f;
  void set(float hz, float q) {
    if (hz < 10.f) hz = 10.f;
    const float mx = SR * 0.45f;
    if (hz > mx) hz = mx;
    if (q < 0.3f) q = 0.3f;
    float g = tanf(PI_F * hz * INV_SR);
    k = 1.f / q;
    a1 = 1.f / (1.f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  DSP_INLINE void tick(float v0, float& lp, float& bp) {
    float v3 = v0 - ic2;
    float v1 = a1 * ic1 + a2 * v3;
    float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.f * v1 - ic1;
    ic2 = 2.f * v2 - ic2;
    lp = v2; bp = v1;
  }
  DSP_INLINE float lp(float x) { float l, b; tick(x, l, b); return l; }
  DSP_INLINE float bpn(float x) { float l, b; tick(x, l, b); return b * k; }   // unity peak gain
  DSP_INLINE float hp(float x) { float l, b; tick(x, l, b); return x - k * b - l; }
  void clear() { ic1 = ic2 = 0.f; }
  void flush() { ic1 = flushDn(ic1); ic2 = flushDn(ic2); }
};

// 2-pole resonator, normalised by sin(w) (see the header comment)
struct Reson {
  float y1 = 0.f, y2 = 0.f, c = 0.f, r2 = 0.f, g = 0.f;
  void set(float hz, float decaySec, float gain = 1.f) {
    if (decaySec < 0.0005f) decaySec = 0.0005f;
    float r = expf(-1.0f / (SR * decaySec));
    if (r > 0.99995f) r = 0.99995f;
    float w = TWO_PI_F * hz * INV_SR;
    if (w > 3.0f) w = 3.0f;          // keep well below Nyquist
    if (w < 0.001f) w = 0.001f;
    c = 2.f * r * cosf(w);
    r2 = r * r;
    g = gain * sinf(w);
  }
  DSP_INLINE float tick(float x) {
    float y = g * x + c * y1 - r2 * y2;
    y2 = y1; y1 = y;
    return y;
  }
  void clear() { y1 = y2 = 0.f; }
  void flush() { y1 = flushDn(y1); y2 = flushDn(y2); }
};

static inline float onePoleCoef(float hz) {
  float c = 1.0f - expf(-TWO_PI_F * hz * INV_SR);
  return c > 0.999f ? 0.999f : c;
}
struct OnePole {
  float z = 0.f, a = 1.f;
  void set(float hz) { a = onePoleCoef(hz); }
  DSP_INLINE float lp(float x) { z += a * (x - z); return z; }
  DSP_INLINE float hp(float x) { z += a * (x - z); return x - z; }
  void clear() { z = 0.f; }
  void flush() { z = flushDn(z); }
};

struct DcBlock {
  float x1 = 0.f, y1 = 0.f;
  DSP_INLINE float tick(float x) {
    float y = x - x1 + 0.9985f * y1;   // ~5 Hz corner at 22.05 kHz
    x1 = x; y1 = y;
    return y;
  }
  void clear() { x1 = y1 = 0.f; }
  void flush() { y1 = flushDn(y1); }
};

// Pink (Kellet economy) and brown noise shapers
struct PinkNoise {
  float b0 = 0.f, b1 = 0.f, b2 = 0.f;
  DSP_INLINE float tick(float w) {
    b0 = 0.99765f * b0 + w * 0.0990460f;
    b1 = 0.96300f * b1 + w * 0.2965164f;
    b2 = 0.57000f * b2 + w * 1.0526913f;
    return (b0 + b1 + b2 + w * 0.1848f) * 0.2f;
  }
  void clear() { b0 = b1 = b2 = 0.f; }
  void flush() { b0 = flushDn(b0); b1 = flushDn(b1); b2 = flushDn(b2); }
};
struct BrownNoise {
  float b = 0.f;
  DSP_INLINE float tick(float w) { b = b * 0.996f + w * 0.06f; return b * 1.3f; }
  void clear() { b = 0.f; }
  void flush() { b = flushDn(b); }
};

// ---------------------------------------------------------------------------
// TPDF dither + round-to-nearest into 16 bits
// ---------------------------------------------------------------------------
struct Ditherer {
  Rng r;
  DSP_INLINE int16_t q(float x) {
    float v = x * 32767.f + (r.uni() - r.uni());   // triangular, +-1 LSB
    v = v >= 0.f ? v + 0.5f : v - 0.5f;
    if (v > 32767.f) v = 32767.f;
    if (v < -32768.f) v = -32768.f;
    return (int16_t)v;
  }
};

}  // namespace dsp
