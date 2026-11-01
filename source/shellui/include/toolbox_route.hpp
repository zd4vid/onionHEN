/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Pure resource-name → toolbox page routing (host-testable, no Mono/PS5).
 * Used by GetManifestResourceStream_Hook.
 */
#pragma once

#include <string>
#include <string_view>

namespace toolbox {

/** Which dynamic (or special) page to serve for a Legacy settings resource. */
enum class Page : unsigned char {
  None = 0,           /**< unknown → original stream */
  DebugSettings,      /**< embedded toolbox XML */
  Payloads,
  PayloadConfig,      /**< per-Payload configuration page */
  OverlayMetrics,     /**< overlay metric list (overlay_metrics.xml) */
  OverlayMetricConfig, /**< per-overlay metric page (overlay_<id>.xml) */
  Sprx,
  SprxConfig,         /**< per-module SPRX catalog page (sprx_<id>.xml) */
  Plugins,            /**< built-in and externally discovered plugins */
  PluginConfig,       /**< per-plugin configuration page (kstuff.xml / plugin_<id>.xml) */
  Cheats,
  AutoPayloads,
  Account,
  CheatProgress,
  RemotePlay,
  DynamicPlugin,     /**< SDK-provided UI contribution page */
  PluginProgress,    /**< external plugin start/stop progress transition page */
  SuperuserPass,      /**< recognized; still use original stream */
  RedirectOgDebug,    /**< og_debug.xml → debug_settings resource */
};

/** Child routes whose page-stack pop should restore the owning parent route. */
constexpr bool restores_parent_on_pop(Page page) {
  return page == Page::CheatProgress || page == Page::RemotePlay ||
         page == Page::PayloadConfig || page == Page::OverlayMetricConfig ||
         page == Page::PluginConfig ||
         page == Page::SprxConfig ||
         page == Page::DynamicPlugin ||
         page == Page::PluginProgress;
}

} // namespace toolbox
