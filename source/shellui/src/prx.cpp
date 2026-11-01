/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "detour.h"
#include "hooked_funcs.hpp"
#include "defs.h"
#include "external_symbols.hpp"
#include "proc.h"
#include "ps5/kernel.h"
#include "ucred.h"

#include <onion/notify.h>
#include <onion/ready.h>

#include <cstring>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <vector>

// ---------------------------------------------------------------------------
// Globals (referenced by other shellui translation units)
// ---------------------------------------------------------------------------

std::string UI3_dec;
std::string legacy_dec;
std::string appsystem_dll;

MonoImage* pui_img = nullptr;
MonoImage* AppSystem_img = nullptr;
MonoObject* Game = nullptr;

bool hooked = false;
bool has_hv_bypass = false;

void (*OnRender_orig)(MonoObject* instance);
MonoObject* rootWidget = nullptr;
MonoObject* font = nullptr;

// Defined in prx_overlay
void OnRender_Hook(MonoObject* instance);

// ---------------------------------------------------------------------------
// Init constants
// ---------------------------------------------------------------------------

namespace {

constexpr size_t kMprotectProbeSize = 100;
constexpr int kProtRwx = 0x7; // PROT_READ | PROT_WRITE | PROT_EXEC
// Same magnitude as the previous sleep(0x100000) keep-alive.
constexpr unsigned kKeepAliveSleepSec = 0x100000;


// ---------------------------------------------------------------------------
// RAII: restore process authid on every exit path
// ---------------------------------------------------------------------------

struct AuthIdGuard {
  pid_t pid;
  uintptr_t old_authid;
  bool active = true;

  AuthIdGuard(pid_t p, uintptr_t auth) : pid(p), old_authid(auth) {}
  ~AuthIdGuard() {
    if (active)
      set_proc_authid(pid, old_authid);
  }
  void release() { active = false; }

  AuthIdGuard(const AuthIdGuard&) = delete;
  AuthIdGuard& operator=(const AuthIdGuard&) = delete;
};

// ---------------------------------------------------------------------------
// Detour helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Mono images used during init
// ---------------------------------------------------------------------------

struct ShellImages {
  MonoImage* legacy = nullptr;
  MonoImage* mscorlib = nullptr;
  MonoImage* react_pui = nullptr;
  MonoImage* app_system = nullptr;
  MonoImage* core = nullptr;
  MonoImage* capture_menu = nullptr;
  MonoImage* lnc = nullptr; // optional
  MonoImage* react_common = nullptr;
  MonoImage* rn_shell = nullptr;
  MonoImage* pui = nullptr;
};

MonoImage* require_dll(const char* name) {
  MonoImage* img = getDLLimage(name);
  if (!img) {
    LOG_ERROR("Failed to load Mono assembly: %s", name);
    notify("notify.hook.load_assembly");
  }
  return img;
}

// ---------------------------------------------------------------------------
// Phase 1: native dynlib symbols
// ---------------------------------------------------------------------------

bool resolve_native_symbols(pid_t pid) {
  int libkernelsys = get_module_handle(pid, "libkernel_sys.sprx");
  KERNEL_DLSYM(libkernelsys, sceKernelMprotect);
  KERNEL_DLSYM(libkernelsys, sceKernelSendNotificationRequest);
  KERNEL_DLSYM(libkernelsys, sceKernelGetCurrentFanDuty);

  /* libonion_platform must not CALL sceKernelSendNotificationRequest by name —
   * here that symbol is a dlsym'd *function pointer*. Register a trampoline. */
  onion_notify_set_send(+[](int32_t device, void *req, size_t size,
                            int32_t blocking) -> int32_t {
    if (!sceKernelSendNotificationRequest)
      return -1;
    return sceKernelSendNotificationRequest(
        device, static_cast<OrbisNotificationRequest *>(req),
        static_cast<int>(size), blocking);
  });

  return true;
}

// ---------------------------------------------------------------------------
// Phase 2: mono runtime symbols
// ---------------------------------------------------------------------------

bool resolve_mono_symbols(pid_t pid) {
  int libmono = get_module_handle(pid, "libmonosgen-2.0.sprx");

  KERNEL_DLSYM(libmono, mono_get_root_domain);
  KERNEL_DLSYM(libmono, mono_property_get_get_method);
  KERNEL_DLSYM(libmono, mono_property_get_set_method);
  KERNEL_DLSYM(libmono, mono_class_get_property_from_name);
  KERNEL_DLSYM(libmono, mono_class_from_name);
  KERNEL_DLSYM(libmono, mono_runtime_invoke);
  KERNEL_DLSYM(libmono, mono_string_new);
  KERNEL_DLSYM(libmono, mono_object_new);
  KERNEL_DLSYM(libmono, mono_object_unbox);
  KERNEL_DLSYM(libmono, mono_compile_method);
  KERNEL_DLSYM(libmono, mono_assembly_get_image);
  KERNEL_DLSYM(libmono, mono_domain_assembly_open);
  KERNEL_DLSYM(libmono, mono_thread_attach);
  KERNEL_DLSYM(libmono, mono_class_get_method_from_name);
  KERNEL_DLSYM(libmono, mono_runtime_object_init);
  KERNEL_DLSYM(libmono, mono_domain_get);

  const bool ok =
      mono_get_root_domain && mono_property_get_get_method &&
      mono_property_get_set_method && mono_class_get_property_from_name &&
      mono_class_from_name && mono_runtime_invoke && mono_string_new &&
      mono_object_new && mono_object_unbox && mono_compile_method &&
      mono_assembly_get_image && mono_domain_assembly_open && mono_thread_attach &&
      mono_class_get_method_from_name &&
      mono_runtime_object_init && mono_domain_get;

  if (!ok)
    LOG_ERROR("Failed to resolve mono symbols");
  return ok;
}

// ---------------------------------------------------------------------------
// Phase 5: load Mono assemblies
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Phase 6: resolve Game container scene
// ---------------------------------------------------------------------------

bool resolve_game_container(MonoImage* app_system, const char* scene_path = "Game",
                            bool notify_on_fail = true) {
  MonoClass* layer_manager =
      mono_class_from_name(app_system, "Sce.Vsh.ShellUI.AppSystem", "LayerManager");
  if (!layer_manager) {
    if (notify_on_fail)
      notify("notify.hook.layer_manager");
    return false;
  }

  MonoMethod* find =
      mono_class_get_method_from_name(layer_manager, "FindContainerSceneByPath", 1);
  if (!find) {
    if (notify_on_fail)
      notify("notify.hook.find_scene");
    return false;
  }

  MonoString* path_arg = mono_string_new(mono_domain_get(), scene_path);
  void* args[1] = {path_arg};
  MonoObject* exception = nullptr;
  Game = mono_runtime_invoke(find, nullptr, args, &exception);

  if (exception) {
    if (notify_on_fail)
      notify("notify.hook.find_scene_exc");
    return false;
  }
  if (!Game) {
    if (notify_on_fail)
      notify("notify.hook.game_scene");
    return false;
  }

  LOG_DEBUG("ContainerScene '%s': %p", scene_path, static_cast<void *>(Game));
  return true;
}

// ---------------------------------------------------------------------------
// Phase 7: install hooks (table-driven mono methods + special cases)
// ---------------------------------------------------------------------------

/** Mono method hook descriptor; `orig` is address of the typed trampoline ptr. */
struct MonoHookSpec {
  const char* name;
  MonoImage* image;
  const char* name_space;
  const char* klass;
  const char* method;
  int argc;
  void* hook;
  void** orig; // &SomeOrigFn
  bool required;
};

bool install_mono_hook(const MonoHookSpec& h) {
  const uint64_t addr =
      Get_Address_of_Method(h.image, h.name_space, h.klass, h.method, h.argc);
  if (!addr) {
    LOG_ERROR("Hook target missing: %s", h.name);
    if (h.required)
      notify("notify.hook.target");
    return !h.required;
  }
  LOG_DEBUG("Installing mono hook: %s target=%#02lx hook=%p", h.name, addr,
            h.hook);
  if (!InstallDetour(addr, h.hook, h.orig)) {
    LOG_ERROR("Detour failed: %s", h.name);
    if (h.required)
      notify("notify.hook.install");
    return !h.required;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Phase 8: keep-alive
// ---------------------------------------------------------------------------

void run_keep_alive() {
  // Publish the initialized SceShellUI process instance.  The daemon keeps
  // this marker so a restart cannot inject a second Toolbox into the same PID.
  // The standalone overlay is not the toolbox: use its own marker so the
  // OnionHEN daemon does not mistake it for an injected toolbox.
  onion_ready_signal_pid(ONION_READY_FPS_OVERLAY, getpid());

  while (true) {
    LOG_DEBUG("sleeping ....");
    sleep(kKeepAliveSleepSec);
  }
}

} // namespace

/*
 * Standalone FPS overlay entry. Used by standalone_fps: no toolbox, no
 * settings file, no plugin bridge, no resource hooks. Only the per-frame
 * overlay hook (PUI.Application.Update -> OnRender_Hook) is installed.
 */
namespace {

/* Only the assemblies the overlay hook touches. Everything else load_shell_images()
 * requires (Legacy, ReactNative.*, CaptureMenu, ...) belongs to the toolbox and
 * must not be able to fail the overlay. */
bool load_overlay_images(ShellImages& out) {
  out.core = require_dll("Sce.PlayStation.Core.dll");
  out.pui = require_dll("Sce.PlayStation.PUI.dll");
  out.app_system = require_dll(appsystem_dll.c_str());
  if (!out.core || !out.pui || !out.app_system)
    return false;

  AppSystem_img = out.app_system;
  pui_img = out.pui;
  return true;
}

/* Only the names the overlay path can reach (AppSystem for the Game scene,
 * Legacy/UI3 for the progress pollers). Skips the manifest-resource names and
 * their debug dump, which only the toolbox needs. */
void init_overlay_resource_names() {
  legacy_dec = base64_decode("U2NlLlZzaC5TaGVsbFVJLkxlZ2FjeS5kbGw="); // Legacy.dll
  UI3_dec = base64_decode("U2NlLlZzaC5TaGVsbFVJLlNldHRpbmdzLkNvcmVVSTM="); // Settings.CoreUI3
  appsystem_dll = base64_decode("U2NlLlZzaC5TaGVsbFVJLkFwcFN5c3RlbS5kbGw=");
}

int fps_overlay_only_main() {
  /* This module runs inside SceShellUI, a separate process from the payload, so
   * it has its own log state. Append (O_APPEND) to the payload's log so scene
   * probing results show up next to the payload lines. */
  onion_log_configure("FPS-UI", "/data/onion_fps.log");
  onion_log_set_level(ONION_LOG_INFO);
  LOG_INFO("overlay module starting in SceShellUI");

  const pid_t pid = getpid();
  AuthIdGuard auth(pid, set_ucred_to_ptrace());

  if (!resolve_native_symbols(pid) || !resolve_mono_symbols(pid))
    return -1;

  init_overlay_resource_names();

  /* Fixed overlay config: text rows (FPS, CPU/GPU temps, RAM, VRAM, FAN, IP,
   * game title), top-left, no background panel. Font size is fixed in overlay::effective_font_pt(). */
  g_settings.overlay_enabled = true;
  g_settings.overlay_background = false;
  g_settings.overlay_fps = true;
  g_settings.overlay_cpu = true;
  g_settings.overlay_gpu = true;
  g_settings.overlay_ram = false; /* RAM/VRAM rows disabled for now */
  g_settings.overlay_ip = true;
  g_settings.overlay_fan = true;
  g_settings.all_cpu_usage = false;
  g_settings.overlay_pos = 0;   /* top edge */
  g_settings.overlay_align = 0; /* left */
  g_settings.overlay_font_size = onion::kOverlayFontLarge; /* unused: see above */

  if (!mono_get_root_domain)
    return -1;
  Root_Domain = mono_get_root_domain();
  if (!Root_Domain)
    return -1;
  mono_thread_attach(Root_Domain);

  ShellImages images;
  if (!load_overlay_images(images))
    return -1;

  /* "SystemOverlay" is drawn above both games and the home screen. */
  if (!resolve_game_container(images.app_system, "SystemOverlay"))
    return -1;

  char probe[kMprotectProbeSize];
  has_hv_bypass = (sceKernelMprotect(probe, sizeof(probe), kProtRwx) == 0);
  Patch_Main_thread_Check(images.core);

  shellui_hooks_begin_install();
  (void)install_mono_hook(
      {"PUI.Application.Update", images.pui, "Sce.PlayStation.PUI",
       "Application", "Update", 0, reinterpret_cast<void*>(&OnRender_Hook),
       reinterpret_cast<void**>(&OnRender_orig), false});
  if (!OnRender_orig) {
    LOG_ERROR("fps overlay: Application.Update hook failed");
    shellui_hooks_publish_failed();
    return -1;
  }

  set_proc_authid(pid, auth.old_authid);
  auth.release();
  shellui_hooks_publish_ready();
  hooked = true;
  run_keep_alive();
  return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// main — orchestration only
// ---------------------------------------------------------------------------

int main(int argc, char const* argv[]) {
  (void)argc;
  (void)argv;

  if (hooked)
    return 0;

  return fps_overlay_only_main();
}
