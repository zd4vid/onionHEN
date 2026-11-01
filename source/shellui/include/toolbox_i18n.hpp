/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Lightweight i18n for dynamic toolbox XML.
 */
#pragma once

#include <cstdarg>
#include <string>
#include <string_view>

namespace toolbox_i18n {

/**
 * Look up a UI string by stable key.
 * Missing key returns the key itself (visible failure).
 */
const char *tr(const char *key);

std::string format(const char *key, ...);

} // namespace toolbox_i18n
