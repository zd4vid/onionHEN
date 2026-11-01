/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Shared /system_tmp namespace for OnionHEN runtime state.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#if defined(ONION_HOST_TEST)
#define ONION_SYSTEM_TMP_ROOT "/tmp/onionhen"
#else
#define ONION_SYSTEM_TMP_ROOT "/system_tmp/onionhen"
#endif

#define ONION_SYSTEM_TMP_IPC_ROOT ONION_SYSTEM_TMP_ROOT "/ipc"
#define ONION_SYSTEM_TMP_READY_ROOT ONION_SYSTEM_TMP_ROOT "/ready"
#define ONION_SYSTEM_TMP_PID_ROOT ONION_SYSTEM_TMP_ROOT "/pid"


#define ONION_SYSTEM_TMP_FPS_SAMPLE ONION_SYSTEM_TMP_ROOT "/fps_sample"
