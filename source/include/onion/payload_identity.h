/* Stable per-file identity for user Payload runtime state. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* "p" followed by a 64-bit FNV-1a digest and a NUL terminator. */
#define ONION_PAYLOAD_IDENTITY_SIZE 18
#define ONION_PAYLOAD_PATH_SIZE 1024
