/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Portable process lookup (sysctl allproc) shared by util / daemon / shellui /
 * bootstrapper. Complements the kernel allproc helpers in onion/proc.h.
 */

#pragma once

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef ONION_PROC_PROCESS_NAME_LEN
#define ONION_PROC_PROCESS_NAME_LEN 128
#endif

/* kinfo_proc.ki_comm usable chars (COMMLEN in freebsd-helper.h). */
#ifndef ONION_PROC_KI_COMM_LEN
#define ONION_PROC_KI_COMM_LEN 19
#endif

/* kinfo_proc.ki_tdname usable chars (TDNAMLEN in freebsd-helper.h). Shorter
 * than ki_comm, and onion_find_pid matches both, so a caller that probes a
 * long name must try this truncation too or it will silently miss threads. */
#ifndef ONION_PROC_KI_TDNAM_LEN
#define ONION_PROC_KI_TDNAM_LEN 16
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Exact match on kinfo_proc.ki_comm. Returns pid or -1. */
pid_t onion_find_pid(const char *name);

/** Substring match on ki_comm. Returns first match or -1. */
pid_t onion_find_pid_substr(const char *substr);

/** True if pid still exists (KERN_PROC_PID). */
bool onion_proc_is_alive(pid_t pid);

typedef int (*onion_get_process_name_fn)(int pid, char *name);
typedef int (*onion_get_app_info_fn)(pid_t pid, void *app_info /* app_info_t */);
typedef int (*onion_get_bigapp_id_fn)(void);

void onion_proc_set_sce_hooks(onion_get_process_name_fn get_name,
                              onion_get_app_info_fn get_app_info,
                              onion_get_bigapp_id_fn get_bigapp);

#ifdef __cplusplus
} // extern "C"

inline bool isProcessAlive(int pid) noexcept {
  return onion_proc_is_alive(static_cast<pid_t>(pid));
}
#else
static inline bool isProcessAlive(pid_t pid) { return onion_proc_is_alive(pid); }
#endif
