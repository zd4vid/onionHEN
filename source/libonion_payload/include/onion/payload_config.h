#pragma once

#include <onion/payload_identity.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ONION_PAYLOAD_PRIORITY_MAX 999
#define ONION_PAYLOAD_DELAY_MAX 300

typedef struct OnionPayloadConfig {
  int priority;
  int delay_seconds;
} OnionPayloadConfig;

#ifdef __cplusplus
}
#endif
