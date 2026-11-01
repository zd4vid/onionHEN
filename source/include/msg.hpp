#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string>
#include <sys/param.h>
#include <onion/system_tmp.h>

#define INVAIL -1
#define CRIT_IPC_SOC ONION_SYSTEM_TMP_CRIT_SOCKET
#define UTIL_IPC_SOC ONION_SYSTEM_TMP_UTIL_SOCKET
#define DAEMON_BUFF_MAX 0x1000


/* Underlying type is unsigned: several stable opcodes use the high bit set. */

/** TCP control port on crit daemon (LAN). Same wire as scripts/shutdown_onion.py. */
#define ONION_CTRL_TCP_PORT 9048
/** TCP control frame magic: ASCII 'ONIO' as BE u32. */
#define ONION_CTRL_TCP_MAGIC 0x4F4E494Fu
/** TCP cmd: full stack shutdown (util + ShellUI + daemon). */
#define ONION_CTRL_TCP_CMD_SHUTDOWN 1u
