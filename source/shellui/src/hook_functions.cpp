/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version. See the file COPYING. */

#include "hooked_funcs.hpp"
#include "detour.h"

/* Opt-in (/data/onion_fps_patch_main_check): the overlay's own calls all run on
 * the UI thread, so this is normally not needed. */
void CheckRunningOnMainThread() {}

void Patch_Main_thread_Check(MonoImage *image_core) {
  uint64_t real_addr = Get_Address_of_Method(
      image_core, "Sce.PlayStation.Core.Runtime", "Diagnostics",
      "CheckRunningOnMainThread", 0);
  if (!real_addr)
    return;
  if (!DetourFunction(real_addr, (void *)&CheckRunningOnMainThread))
    LOG_ERROR("Main thread check detour failed");
}
