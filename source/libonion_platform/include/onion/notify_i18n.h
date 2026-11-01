/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Process-local localization for user-facing notification text.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum onion_notify_language {
  ONION_NOTIFY_LANG_ZH_HANS = 0,
  ONION_NOTIFY_LANG_EN = 1,
  ONION_NOTIFY_LANG_AR = 2,
  ONION_NOTIFY_LANG_ZH_HANT = 3,
  ONION_NOTIFY_LANG_JA = 4,
  ONION_NOTIFY_LANG_FR = 5,
  ONION_NOTIFY_LANG_DE = 6,
  ONION_NOTIFY_LANG_KO = 7,
  ONION_NOTIFY_LANG_ES = 8,
  ONION_NOTIFY_LANG_PT_BR = 9,
  ONION_NOTIFY_LANG_IT = 10,
  ONION_NOTIFY_LANG_RU = 11,
  ONION_NOTIFY_LANG_PL = 12,
  ONION_NOTIFY_LANG_TH = 13,
} onion_notify_language_t;

/** Return the currently selected notification language. */
onion_notify_language_t onion_notify_get_language(void);

/** Translate a stable notification key; unknown keys are returned unchanged. */
const char *onion_notify_tr(const char *key);

#ifdef __cplusplus
}
#endif
