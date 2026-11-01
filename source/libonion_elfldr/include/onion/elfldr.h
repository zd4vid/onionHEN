/* Copyright (C) 2024 John Törnblom / OnionHEN
 *
 * Shared ELF load helpers (inject path, embedded spawn, privilege raise).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Load ELF image into target process; returns entry VA or 0 on failure. */
intptr_t elfldr_load(pid_t pid, uint8_t *elf);

/** Allocate payload_args_t-compatible block in target. Returns VA or 0. */
intptr_t elfldr_payload_args(pid_t pid);

#ifdef __cplusplus
}
#endif
