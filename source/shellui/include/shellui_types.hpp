/* Copyright (C) 2025 OnionHEN / LightningMods
 * ShellUI domain types / enums / settings externs (no Mono hooks).
 */
#pragma once

#include "external_symbols.hpp"
#include "overlay_layout.hpp"
#include <string>
#include <onion/settings.hpp>

static const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";

enum RemoveWidget {
    REMOVE_SOC_OVERLAY,
    REMOVE_CPU_OVERLAY,
    REMOVE_RAM_OVERLAY,
    REMOVE_IP_OVERLAY,
    REMOVE_FAN_OVERLAY,
    REMOVE_FPS_OVERLAY,
    REMOVE_ALL_OVERLAYS,
};

enum CreateWidget {
    CREATE_SOC_OVERLAY,
    CREATE_CPU_OVERLAY,
    CREATE_RAM_OVERLAY,
    CREATE_IP_OVERLAY,
    CREATE_FAN_OVERLAY,
    CREATE_FPS_OVERLAY,
    CREATE_ALL_OVERLAYS,
};

struct WidgetConfig {
    const char* id;
    float x, y;
    const char* text;
    int bold;
    float r, g, b, a;
};

void RemoveGameWidget(RemoveWidget widget);
bool CreateGameWidget(CreateWidget widget);

extern onion::Settings g_settings;

/**
 * Overlay layout: full-width edge strip + left-, center-, or right-packed metrics.
 * bar_x/bar_w are always 0 / screen width; bar_y is 0 or (H - bar_h).
 */
using OverlayLayout = onion::overlay::Layout;
extern OverlayLayout g_overlay_layout;
