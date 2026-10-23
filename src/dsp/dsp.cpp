#include "speed.h"   // first: speed-optimised (v0.32)
#include "dsp.h"

namespace dsp {

float sineTab[SINE_N + 1];

void init() {
  for (int i = 0; i <= SINE_N; i++) sineTab[i] = sinf(TWO_PI_F * (float)i / SINE_N);
}

}  // namespace dsp
