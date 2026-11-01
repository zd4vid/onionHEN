/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * External plugin start/stop progress page backed by a Legacy Settings user_custom Panel.
 * The worker thread handles IPC and waits for dynamic UI registration.
 * UI3 widget creation and updates stay on the ShellUI thread.
 */
#pragma once

#include "monodef.h"

#include <string>
#include <string_view>
