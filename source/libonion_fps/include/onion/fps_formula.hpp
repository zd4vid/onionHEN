/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Host-testable FPS composition (no kernel / ioctl).
 */
#pragma once

#include <cstdint>

namespace onion {
namespace fps {

inline constexpr int kWindow = 5;
/* PHU keeps uncapped render rates for calibration; display output is capped. */
inline constexpr float kRawFpsMax = 10000.0f;
inline constexpr float kFpsMin = 1.0f;
inline constexpr float kFpsMax = 240.0f;

/** fps = Δcount / Δt. 0 if the interval is unusable. */
double hz_from_delta(uint64_t dcount, double dt_sec);

/** Arithmetic mean of n samples. 0 if n<=0. */
float rolling_mean(const float *samples, int n);

} // namespace fps
} // namespace onion
