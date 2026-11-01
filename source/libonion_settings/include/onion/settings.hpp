/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#pragma once

// Overlay settings used by the ShellUI overlay module.

#ifdef __cplusplus
extern "C++" {

#include <array>
#include <cstddef>
#include <cstdint>

namespace onion {

inline constexpr int kOverlayAlignCenter = 1;

inline constexpr int kOverlayMetricCount = 6;
inline constexpr int kOverlayMetricFps = 0;
inline constexpr int kOverlayMetricCpu = 1;
inline constexpr int kOverlayMetricSoc = 2;
inline constexpr int kOverlayMetricMemory = 3;
inline constexpr int kOverlayMetricIp = 4;
inline constexpr int kOverlayMetricFan = 5;

inline constexpr int kOverlayFontSmall = 0;
inline constexpr int kOverlayFontMedium = 1;
inline constexpr int kOverlayFontLarge = 2;

inline constexpr int kOverlayFontPtSmall = 14;
inline constexpr int kOverlayFontPtMedium = 18;
inline constexpr int kOverlayFontPtLarge = 24;

inline constexpr std::array<int, kOverlayMetricCount> kOverlayOrderDefault = {
    kOverlayMetricFps, kOverlayMetricCpu, kOverlayMetricSoc,
    kOverlayMetricMemory, kOverlayMetricIp, kOverlayMetricFan};

inline int overlay_font_pt(int font_size) {
  if (font_size == kOverlayFontSmall)
    return kOverlayFontPtSmall;
  if (font_size == kOverlayFontLarge)
    return kOverlayFontPtLarge;
  return kOverlayFontPtMedium;
}

struct Settings {
  /** Master visibility switch for the complete ShellUI game monitor bar. */
  bool overlay_enabled = true;
  /** Show the translucent background panel behind the game monitor bar. */
  bool overlay_background = true;
  bool overlay_ram = true;
  bool overlay_cpu = true;
  bool overlay_soc = true;
  bool overlay_fps = true;
  bool overlay_ip = false;
  /** Show current fan duty (percent) on the overlay bar. */
  bool overlay_fan = false;
  /** Per-core CPU usage mode on the overlay (id_all_cpu_usage). */
  bool all_cpu_usage = false;
  int overlay_pos = 0; // 0/1 top edge, 2/3 bottom edge
  /** 0 left, 1 center, 2 right. */
  int overlay_align = kOverlayAlignCenter;
  /** 0 small, 1 medium, 2 large. */
  int overlay_font_size = kOverlayFontMedium;
  /** Display order of the six metrics; values are kOverlayMetric*. */
  std::array<int, kOverlayMetricCount> overlay_order = kOverlayOrderDefault;
};

} // namespace onion

} // extern "C++"
#endif /* __cplusplus */
