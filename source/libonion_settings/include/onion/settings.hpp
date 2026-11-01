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

// ---------------------------------------------------------------------------
// Single product config schema for daemon / util / shellui.
//
// Paths:
//   primary  /data/OnionHEN/config.ini          (elevated daemons)
//   shellui  /user/data/OnionHEN/config.ini     (SceShellUI sandbox view)
// Load tries both; save writes all writable targets so the files stay twins.
//
// Process-local store: use SettingsStore for multi-threaded daemons (snapshot
// under mutex). Multi-process copies of Settings are expected and OK.
//
// Force C++ linkage even when a parent includes us under extern "C"
// (e.g. util common_utils.h inside extern "C" blocks).
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C++" {

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <utility>

namespace onion {

// Semantic config schema. This schema starts at version 1.
inline constexpr int kSettingsSchemaVersion = 1;
inline constexpr int kUiLanguageSystem = 0;

inline constexpr int kStartupOpenNone = 0;

// Mirrors onion_log_level from <onion/log.h>. Duplicated rather than included
// so libonion_settings stays independent of libonion_platform; the processes
// that bridge the two (see onion_apply_log_settings) static_assert that the
// values still line up.
inline constexpr int kCheatsMirrorAuto = 0;

inline constexpr int kLogLevelOff = 0;
inline constexpr int kLogLevelError = 1;
inline constexpr int kLogLevelWarn = 2;
inline constexpr int kLogLevelInfo = 3;
inline constexpr int kLogLevelDebug = 4;
inline constexpr int kLogLevelTrace = 5;

inline constexpr int kOverlayAlignCenter = 1;

inline constexpr int kOverlayMetricCount = 6;
inline constexpr int kOverlayMetricFps = 0;
inline constexpr int kOverlayMetricCpu = 1;
inline constexpr int kOverlayMetricGpu = 2;
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
    kOverlayMetricFps, kOverlayMetricCpu, kOverlayMetricGpu,
    kOverlayMetricMemory, kOverlayMetricIp, kOverlayMetricFan};

inline int overlay_font_pt(int font_size) {
  if (font_size == kOverlayFontSmall)
    return kOverlayFontPtSmall;
  if (font_size == kOverlayFontLarge)
    return kOverlayFontPtLarge;
  return kOverlayFontPtMedium;
}

inline constexpr int kFanAutomaticThresholdCelsius = 77;
inline constexpr int kRestModeDelayDefaultSeconds = 10;

inline constexpr std::size_t kMaxAppJailbreakExactTitleIds = 20;
inline constexpr std::size_t kMaxAppJailbreakTitleIdPrefixes = 20;

struct AppJailbreakAllowlist {
  std::array<std::string, kMaxAppJailbreakExactTitleIds> exact_title_ids = {
      "ITEM00001", "NPXS39041", "PKGI13337", "PKGI12345", "TOOL00001"};
  std::size_t exact_title_id_count = 5;

  std::array<std::string, kMaxAppJailbreakTitleIdPrefixes>
      title_id_prefixes = {"LAPY"};
  std::size_t title_id_prefix_count = 1;
};

struct Settings {
  // [startup]
  // Page to open after OnionHEN finishes loading.
  int startup_open_after_load = kStartupOpenNone;

  // [rest_mode]
  // Extra seconds after Rest Mode trophy SPRX are ready, before Toolbox inject.
  uint64_t rest_mode_delay_seconds = kRestModeDelayDefaultSeconds;

  // [cheats], [app_jailbreak]
  int cheats_mirror = kCheatsMirrorAuto;
  bool app_jailbreak_enabled = true;
  bool debug_app_jb_msg = false;
  AppJailbreakAllowlist app_jailbreak_allowlist{};

  // [home_screen], [game_menu]
  bool display_tids = false;
  bool onionhen_game_opts = true;

  // [cooling]
  bool enable_fan_speed = false;
  int fan_threshold = kFanAutomaticThresholdCelsius;

  // [overlay]
  /** Master visibility switch for the complete ShellUI game monitor bar. */
  bool overlay_enabled = true;
  /** Show the translucent background panel behind the game monitor bar. */
  bool overlay_background = true;
  bool overlay_ram = true;
  bool overlay_cpu = true;
  bool overlay_gpu = true;
  bool overlay_fps = true;
  bool overlay_ip = false;
  /** Show current fan duty (percent) on the overlay bar. */
  bool overlay_fan = false;
  /** Per-core CPU usage mode on the overlay (id_all_cpu_usage). */
  bool all_cpu_usage = false;
  int overlay_pos = 0; // 0/1 top edge, 2/3 bottom edge
  /** 0 left, 1 center, 2 right. Independent of overlay.edge. */
  int overlay_align = kOverlayAlignCenter;
  /** 0 small, 1 medium, 2 large. Applies to the whole monitor bar. */
  int overlay_font_size = kOverlayFontMedium;
  /** Display order of the six metrics; values are kOverlayMetric*. */
  std::array<int, kOverlayMetricCount> overlay_order = kOverlayOrderDefault;

  // [shortcuts]
  int cheats_shortcut_opt = 0;
  int toolbox_shortcut_opt = 0;

  // [kstuff]
  // Load the embedded/override kstuff payload when OnionHEN starts.
  bool kstuff_autoload = true;

  // [toolbox]
  // 0 = system (default), 1 = zh-Hans, 2 = en, 3 = ar, 4 = zh-Hant,
  // 5 = ja, 6 = fr, 7 = de, 8 = ko, 9 = es, 10 = pt-BR, 11 = it,
  // 12 = ru, 13 = pl
  int ui_lang = kUiLanguageSystem;

  // [logging]
  // Runtime log threshold, matching onion_log_level (1=error .. 5=trace).
  // Lets a user raise verbosity for a bug report without a debug payload;
  // levels stripped at compile time cannot be re-enabled this way.
  int log_level = kLogLevelInfo;

  // Meta
  int schema_version = kSettingsSchemaVersion;
};

// Thread-safe process-local settings (daemon / util IPC + worker threads).
// ShellUI may keep a plain Settings if UI work is single-threaded; prefer this
// store when readers and writers can race.

} // namespace onion

} // extern "C++"
#endif /* __cplusplus */
