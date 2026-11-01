/* Copyright (C) 2025 OnionHEN / LightningMods */

#include <onion/notify.h>
#include <onion/notify_i18n.h>
#include <onion/log.h>


#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Layout must total 0xC30 (same as elfldr: 45-byte header + 3075 payload).
 * message starts at offset 0x2D (immediately after use_icon_image_uri).
 */
typedef struct {
  int32_t type;             /* 0x00 */
  int32_t req_id;           /* 0x04 */
  int32_t priority;         /* 0x08 */
  int32_t msg_id;           /* 0x0C */
  int32_t target_id;        /* 0x10 */
  int32_t user_id;          /* 0x14 */
  int32_t unk1;             /* 0x18 */
  int32_t unk2;             /* 0x1C */
  int32_t app_id;           /* 0x20 */
  int32_t error_num;        /* 0x24 */
  int32_t unk3;             /* 0x28 */
  char use_icon_image_uri;  /* 0x2C */
  char message[1024];       /* 0x2D */
  char uri[1024];           /* 0x42D */
  char unkstr[1024];        /* 0x82D */
  char _pad_to_c30[3];      /* 0xC2D → 0xC30 */
} OrbisNotificationRequest;

_Static_assert(sizeof(OrbisNotificationRequest) == 0xC30,
               "OrbisNotificationRequest must be 0xC30");

static onion_notify_send_fn g_send = NULL;

void onion_notify_set_send(onion_notify_send_fn fn) { g_send = fn; }

void onion_notify_format(char *out, size_t out_sz, int show_watermark,
                         const char *fmt, va_list ap) {
  char buff[3075];

  /* notify.* key → onion_notify_tr → printf-style body. */
  vsnprintf(buff, sizeof(buff), onion_notify_tr(fmt), ap);
  if (show_watermark) {
    snprintf(out, out_sz, "[OnionHEN] %s", buff);
  } else {
    snprintf(out, out_sz, "%s", buff);
  }
}

static void onion_notify_send_request(OrbisNotificationRequest *req,
                                      int with_system_icon) {
  if (!req) {
    return;
  }

  req->type = 0;
  req->unk3 = 0;
  req->target_id = -1;
  if (with_system_icon) {
    req->use_icon_image_uri = 1;
    strncpy(req->uri, "cxml://psnotification/tex_icon_system",
            sizeof(req->uri) - 1);
  } else {
    /* Debug-style toast (SDK notify_debug): text only, no icon URI. */
    req->use_icon_image_uri = 0;
    req->uri[0] = '\0';
  }

  LOG_DEBUG("Notify%s: %s", with_system_icon ? "" : "(debug)",
            req->message);

  if (!g_send) {
    /* Never fall back to a direct CALL of sceKernelSendNotificationRequest —
     * that symbol is a data pointer in shellui/fps injectees. */
    LOG_ERROR("Notify: send fn not registered (onion_notify_set_send)");
    return;
  }
  (void)g_send(0, req, sizeof(*req), 0);
}

void onion_notify_v(int show_watermark, const char *fmt, va_list ap) {
  OrbisNotificationRequest req;
  memset(&req, 0, sizeof(req));
  onion_notify_format(req.message, sizeof(req.message), show_watermark, fmt, ap);
  onion_notify_send_request(&req, /*with_system_icon=*/1);
}

void onion_notify(int show_watermark, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  onion_notify_v(show_watermark, fmt, args);
  va_end(args);
}
