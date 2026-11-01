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

#include <msg.hpp>
#include <onion/log.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Shared injectee-side IPC client (crit + util daemons).
//
// Design rules:
//   - One connection object per target (crit vs util). Never flip a flag on a
//     shared singleton — that races MainDaemonSocket / UtilDaemonSocket.
//   - IPCSendCommand is serialized with a per-instance mutex (shellui hooks +
//     background threads may share the same singleton).
//   - Implementation lives in the .cpp (not header-only).
//   - JSON string fields are built with cJSON (escape-safe).
// ---------------------------------------------------------------------------

// Logging: the shared LOG_* macros from <onion/log.h>, included above.

struct PluginInventoryItem {
  std::string plugin_id;
  std::string version;
  std::string name;
  bool running = false;
  bool auto_start = false;
};

struct SprxInventoryItem {
  std::string id;
  std::string path;
  bool enabled = true;
  bool auto_start = false;
  int priority = 0;
  bool matches_current_target = false;
  bool loaded_for_current_target = false;
};
