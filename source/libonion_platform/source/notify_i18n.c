/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/notify_i18n.h>

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

/* Generated from source/i18n locale catalogs at build time. */
#include "notify_i18n_catalog.inc"

static atomic_int gLanguage = ATOMIC_VAR_INIT(ONION_NOTIFY_LANG_EN);

static const char *locale_id_for_language(onion_notify_language_t language) {
  switch (language) {
  case ONION_NOTIFY_LANG_ZH_HANS:
    return "zh-Hans";
  case ONION_NOTIFY_LANG_AR:
    return "ar";
  case ONION_NOTIFY_LANG_ZH_HANT:
    return "zh-Hant";
  case ONION_NOTIFY_LANG_JA:
    return "ja";
  case ONION_NOTIFY_LANG_FR:
    return "fr";
  case ONION_NOTIFY_LANG_DE:
    return "de";
  case ONION_NOTIFY_LANG_KO:
    return "ko";
  case ONION_NOTIFY_LANG_ES:
    return "es";
  case ONION_NOTIFY_LANG_PT_BR:
    return "pt-BR";
  case ONION_NOTIFY_LANG_IT:
    return "it";
  case ONION_NOTIFY_LANG_RU:
    return "ru";
  case ONION_NOTIFY_LANG_PL:
    return "pl";
  case ONION_NOTIFY_LANG_TH:
    return "th";
  case ONION_NOTIFY_LANG_EN:
  default:
    return "en";
  }
}

static int locale_index_for_language(onion_notify_language_t language) {
  const char *id = locale_id_for_language(language);
  int i;

  for (i = 0; i < I18N_LOCALE_COUNT; ++i) {
    if (strcmp(kI18nLocaleIds[i], id) == 0)
      return i;
  }
  return kI18nLocaleFallback;
}

onion_notify_language_t onion_notify_get_language(void) {
  return (onion_notify_language_t)atomic_load_explicit(&gLanguage,
                                                        memory_order_relaxed);
}

const char *onion_notify_tr(const char *key) {
  const int idx = locale_index_for_language(onion_notify_get_language());
  size_t i;

  if (!key)
    return "";

  for (i = 0; i < sizeof(kTranslations) / sizeof(kTranslations[0]); ++i) {
    if (strcmp(kTranslations[i].key, key) == 0)
      return kTranslations[i].text[idx];
  }
  return key;
}
