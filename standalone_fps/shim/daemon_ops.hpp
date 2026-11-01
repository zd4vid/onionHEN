/* Standalone-FPS shim for the daemon's daemon_ops.hpp: only what
 * daemon_fps.cpp uses. */
#pragma once

#include <atomic>
#include <string>

extern std::atomic_bool g_stack_shutting_down;

bool Get_Running_App_TID(std::string &title_id, int &BigAppid);
