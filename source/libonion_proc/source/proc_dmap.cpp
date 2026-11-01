/* Copyright (C) 2026 OnionHEN / LightningMods
 *
 * Read/write another process's user VA through the kernel DMAP window.
 * Algorithm matches util KdirectBackend (cheats/memory_backends.cpp) and
 * kstuff-lite virt2phys: CR3 from vmspace.pmap, then 4-level PT walk.
 */
#include <onion/proc_dmap.h>

#include <ps5/kernel.h>

#include <cstdint>

namespace {

} // namespace
