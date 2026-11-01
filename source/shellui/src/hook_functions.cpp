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

#include "hooked_funcs.hpp"
#include "homeui_top_nav_patch.hpp"
#include "settings_bundle_patch.hpp"
#include "toolbox_i18n.hpp"
#include <onion/platform.h>
#include "detour.h"
#include <climits>
#include <msg.hpp>
#include <pthread.h>
#include <sys/_pthreadtypes.h>
#include <sys/signal.h>
#include <fstream>
#include <unistd.h>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>
extern "C"{
#include <ps5/kernel.h>
}

int (*__sys_regmgr_call)(long, long, int*, int*, long) = nullptr;

// UI runtime state: g_ui (shellui_state.hpp / shellui_globals.cpp)

// widgets → shellui_overlay_widgets.cpp
extern "C"{
}

/** Create MonoString on the *calling* domain (UI thread may not be Root_Domain). */

// threads → hook_background.cpp


void CheckRunningOnMainThread() {
	//notify("Main thread check called!");
}
void Patch_Main_thread_Check(MonoImage * image_core) {

    uint64_t real_addr = Get_Address_of_Method(image_core, "Sce.PlayStation.Core.Runtime", "Diagnostics", "CheckRunningOnMainThread", 0);
    if (!real_addr) {
#if SHELL_DEBUG==1
        LOG_ERROR("Failed to get method address");
#endif
        return;
    }
#if SHELL_DEBUG==1
    LOG_DEBUG("changing permissions on (0x%llx).", (unsigned long long)real_addr);
#endif
    
    if (!DetourFunction(real_addr, (void*)&CheckRunningOnMainThread)) {
        LOG_ERROR("Main thread check detour failed");
        return;
    }
#if SHELL_DEBUG==1
    LOG_DEBUG("Main thread check patched");
#endif

}
// Common logic function

// launch → hook_launch.cpp
