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
#include "debug_settings_route_runtime.hpp"
#include "dynamic_ui_runtime.hpp"
#include "hooked_funcs.hpp"
#include "defs.h"
#include "external_symbols.hpp"
#include "homeui_top_nav_patch.hpp"
#include "ipc.hpp"
#include "plugin_ui_bridge_client.hpp"
#include "proc.h"
#include "ps5/kernel.h"
#include "toolbox_i18n.hpp"
#include "ucred.h"
#include "webserver.hpp"

#include <onion/notify.h>
#include <onion/proc_query.h>
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
std::string uilib;
std::string Sysinfo;
std::string display_info;
std::string uilib_dll;
std::string payloads_xml;
std::string debug_settings_xml;
std::string cheats_xml;

MonoImage* pui_img = nullptr;
MonoImage* AppSystem_img = nullptr;
MonoObject* Game = nullptr;
MonoImage* react_common_img = nullptr;

bool hooked = false;
bool has_hv_bypass = false;
bool is_6xx = false;
bool is_3xx = false;

extern "C" long ptr_syscall = 0;

void __syscall() {
  asm(".intel_syntax noprefix\n"
      "  mov rax, rdi\n"
      "  mov rdi, rsi\n"
      "  mov rsi, rdx\n"
      "  mov rdx, rcx\n"
      "  mov r10, r8\n"
      "  mov r8,  r9\n"
      "  mov r9,  qword ptr [rsp + 8]\n"
      "  call qword ptr [rip + ptr_syscall]\n"
      "  ret\n");
}

void (*OnRender_orig)(MonoObject* instance);
void (*oUserCustomElementReset)(MonoObject* instance, MonoObject* item) = nullptr;
void (*oSettingPageStackOnPopping)(MonoObject* instance, MonoObject* outgoing,
                                    MonoObject* incoming) = nullptr;
MonoObject* rootWidget = nullptr;
MonoObject* font = nullptr;
void (*Orig_ReloadApp)(MonoString* str) = nullptr;

// Defined in prx_install / prx_overlay
void ReloadApp(MonoString* str);
void OnRender_Hook(MonoObject* instance);
int KillAppWithReason_Hook(int appId, int reason);

// ---------------------------------------------------------------------------
// Init constants
// ---------------------------------------------------------------------------

namespace {

constexpr int kSyscallInstrOffset = 0x0A;
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
  KERNEL_DLSYM(libkernelsys, sceKernelDebugOutText);
  KERNEL_DLSYM(libkernelsys, sceKernelMkdir);
  KERNEL_DLSYM(libkernelsys, scePthreadCreate);
  KERNEL_DLSYM(libkernelsys, sceKernelMprotect);
  KERNEL_DLSYM(libkernelsys, sceKernelSendNotificationRequest);
  KERNEL_DLSYM(libkernelsys, sceKernelGetProsperoSystemSwVersion);
  KERNEL_DLSYM(libkernelsys, sceKernelGetAppInfo);
  KERNEL_DLSYM(libkernelsys, sceKernelGetProcessName);
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

  KERNEL_DLSYM(libSceKernelHandle, sceKernelJitCreateSharedMemory);
  KERNEL_DLSYM(libSceKernelHandle, sceKernelJitCreateAliasOfSharedMemory);
  KERNEL_DLSYM(libSceKernelHandle, sceKernelJitMapSharedMemory);
  KERNEL_DLSYM(libSceKernelHandle, ioctl);
  KERNEL_DLSYM(libSceKernelHandle, __sys_regmgr_call);

  // Point ptr_syscall at the syscall instruction inside getpid.
  // Variable name must be `getpid` for KERNEL_DLSYM's #sym string.
  {
    static __attribute__((used)) long getpid = 0;
    KERNEL_DLSYM(libSceKernelHandle, getpid);
    ptr_syscall = getpid + kSyscallInstrOffset;
  }

  int shellui_util = get_module_handle(pid, "libSceShellUIUtil.sprx");
  KERNEL_DLSYM(shellui_util, sceShellUIUtilLaunchByUri);
  KERNEL_DLSYM(shellui_util, sceShellUIUtilInitialize);

  int system_service = get_module_handle(pid, "libSceSystemService.sprx");
  KERNEL_DLSYM(system_service, sceSystemServiceGetAppIdOfRunningBigApp);
  KERNEL_DLSYM(system_service, sceSystemServiceParamGetInt);
  KERNEL_DLSYM(system_service, sceSystemServiceGetAppTitleId);
  {
    void* sceSystemServiceLaunchApp = nullptr;
    KERNEL_DLSYM(system_service, sceSystemServiceLaunchApp);
    if (!sceSystemServiceLaunchApp)
      LOG_ERROR("Failed to resolve sceSystemServiceLaunchApp");
  }

  int reg = get_module_handle(pid, "libSceRegMgr.sprx");
  KERNEL_DLSYM(reg, sceRegMgrGetInt);

  int remoteplay = get_module_handle(pid, "libSceRemoteplay.sprx");
  LOG_DEBUG("Remote Play module handle: %d", remoteplay);
  KERNEL_DLSYM(remoteplay, sceRemoteplayInitialize);
  KERNEL_DLSYM(remoteplay, sceRemoteplayGeneratePinCode);
  KERNEL_DLSYM(remoteplay, sceRemoteplayConfirmDeviceRegist);
  KERNEL_DLSYM(remoteplay, sceRemoteplayNotifyPinCodeError);
  LOG_DEBUG("Remote Play symbols: init=%p pin=%p confirm=%p invalidate=%p",
            reinterpret_cast<void *>(sceRemoteplayInitialize),
            reinterpret_cast<void *>(sceRemoteplayGeneratePinCode),
            reinterpret_cast<void *>(sceRemoteplayConfirmDeviceRegist),
            reinterpret_cast<void *>(sceRemoteplayNotifyPinCodeError));

  return true;
}

// ---------------------------------------------------------------------------
// Phase 2: mono runtime symbols
// ---------------------------------------------------------------------------

bool resolve_mono_symbols(pid_t pid) {
  int libmono = get_module_handle(pid, "libmonosgen-2.0.sprx");

  KERNEL_DLSYM(libmono, mono_object_to_string);
  KERNEL_DLSYM(libmono, mono_get_root_domain);
  KERNEL_DLSYM(libmono, mono_property_get_get_method);
  KERNEL_DLSYM(libmono, mono_property_get_set_method);
  KERNEL_DLSYM(libmono, mono_class_get_property_from_name);
  KERNEL_DLSYM(libmono, mono_class_from_name);
  KERNEL_DLSYM(libmono, mono_raise_exception);
  KERNEL_DLSYM(libmono, mono_runtime_invoke);
  KERNEL_DLSYM(libmono, mono_array_new);
  KERNEL_DLSYM(libmono, mono_string_new);
  KERNEL_DLSYM(libmono, mono_jit_set_aot_only);
  KERNEL_DLSYM(libmono, mono_jit_init_version);
  KERNEL_DLSYM(libmono, mono_object_new);
  KERNEL_DLSYM(libmono, mono_object_unbox);
  KERNEL_DLSYM(libmono, mono_set_dirs);
  KERNEL_DLSYM(libmono, mono_compile_method);
  KERNEL_DLSYM(libmono, mono_assembly_get_image);
  KERNEL_DLSYM(libmono, mono_domain_assembly_open);
  KERNEL_DLSYM(libmono, mono_get_byte_class);
  KERNEL_DLSYM(libmono, mono_thread_attach);
  KERNEL_DLSYM(libmono, mono_object_get_class);
  KERNEL_DLSYM(libmono, mono_vtable_get_static_field_data);
  KERNEL_DLSYM(libmono, mono_class_get_method_from_name);
  KERNEL_DLSYM(libmono, mono_class_get_field_from_name);
  KERNEL_DLSYM(libmono, mono_field_get_value);
  KERNEL_DLSYM(libmono, mono_aot_get_method);
  KERNEL_DLSYM(libmono, mono_field_static_set_value);
  KERNEL_DLSYM(libmono, mono_assembly_setrootdir);
  KERNEL_DLSYM(libmono, mono_free);
  KERNEL_DLSYM(libmono, mono_gchandle_new);
  KERNEL_DLSYM(libmono, mono_gchandle_free); /* optional; null if missing */
  KERNEL_DLSYM(libmono, mono_image_open_from_data);
  KERNEL_DLSYM(libmono, mono_runtime_object_init);
  KERNEL_DLSYM(libmono, mono_domain_get);
  KERNEL_DLSYM(libmono, mono_assembly_load_from);
  KERNEL_DLSYM(libmono, mono_method_desc_new);
  KERNEL_DLSYM(libmono, mono_method_desc_search_in_class);
  KERNEL_DLSYM(libmono, mono_method_desc_free);
  KERNEL_DLSYM(libmono, mono_object_new_specific);
  KERNEL_DLSYM(libmono, mono_thread_detach);
  KERNEL_DLSYM(libmono, mono_array_addr_with_size);
  KERNEL_DLSYM(libmono, mono_thread_current);
  KERNEL_DLSYM(libmono, mono_class_vtable);
  KERNEL_DLSYM(libmono, mono_domain_unload);
  KERNEL_DLSYM(libmono, mono_string_to_utf8);

  const bool ok =
      mono_object_to_string && mono_get_root_domain && mono_property_get_get_method &&
      mono_property_get_set_method && mono_class_get_property_from_name &&
      mono_class_from_name && mono_runtime_invoke && mono_array_new && mono_string_new &&
      mono_jit_set_aot_only && mono_jit_init_version && mono_object_new &&
      mono_object_unbox && mono_set_dirs && mono_compile_method &&
      mono_assembly_get_image && mono_domain_assembly_open && mono_get_byte_class &&
      mono_thread_attach && mono_object_get_class && mono_vtable_get_static_field_data &&
      mono_class_get_method_from_name && mono_class_get_field_from_name &&
      mono_aot_get_method && mono_field_static_set_value && mono_assembly_setrootdir &&
      mono_free && mono_gchandle_new && mono_image_open_from_data &&
      mono_runtime_object_init && mono_domain_get && mono_assembly_load_from &&
      mono_method_desc_new && mono_method_desc_search_in_class && mono_method_desc_free &&
      mono_object_new_specific && mono_thread_detach && mono_array_addr_with_size &&
      mono_thread_current && mono_class_vtable && mono_domain_unload &&
      mono_string_to_utf8;

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
// Phase 8: process query hooks + keep-alive
// ---------------------------------------------------------------------------

void setup_proc_hooks() {
  onion_proc_set_sce_hooks(
      [](int pid, char* name) -> int {
        return sceKernelGetProcessName ? sceKernelGetProcessName(pid, name) : -1;
      },
      [](pid_t pid, void* info) -> int {
        return sceKernelGetAppInfo
                   ? sceKernelGetAppInfo(pid, static_cast<app_info_t*>(info))
                   : -1;
      },
      []() -> int {
        return sceSystemServiceGetAppIdOfRunningBigApp
                   ? sceSystemServiceGetAppIdOfRunningBigApp()
                   : -1;
      });
}

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
  setup_proc_hooks();
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
