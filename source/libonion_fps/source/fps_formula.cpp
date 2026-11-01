/* Copyright (C) 2026 OnionHEN / LightningMods */
#include <onion/fps_formula.hpp>
#include <onion/fps_sample.h>

#include <cstring>

namespace onion {
namespace fps {

double hz_from_delta(uint64_t dcount, double dt_sec) {
  if (dcount == 0 || dt_sec <= 0.0)
    return 0.0;
  const double hz = static_cast<double>(dcount) / dt_sec;
  /* Keep uncapped rates for PHU-style calibration. The final published FPS
   * still uses kFpsMax; rejecting them here loses the only multi-pass signal. */
  if (hz < static_cast<double>(kFpsMin) || hz > static_cast<double>(kRawFpsMax))
    return 0.0;
  return hz;
}

float rolling_mean(const float *samples, int n) {
  if (!samples || n <= 0)
    return 0.f;
  double sum = 0.0;
  for (int i = 0; i < n; ++i)
    sum += static_cast<double>(samples[i]);
  return static_cast<float>(sum / static_cast<double>(n));
}

} // namespace fps
} // namespace onion
