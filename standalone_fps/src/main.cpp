/* Standalone FPS counter + on-screen overlay payload.
 *
 * Samples the scanout (flip) counter from /dev/dce (safe mode: no game or
 * kernel memory access), publishes the result to
 * /system_tmp/onionhen/fps_sample (seqlock mmap, see fps_sample.h) and injects
 * a small ShellUI module (the embedded fps_overlay ELF) into SceShellUI that
 * draws FPS / CPU / GPU as stacked rows on screen.
 *
 * Load on a jailbroken console with an elfldr (e.g. port 9021).
 */
#include <onion/fps_dce.hpp>
#include <onion/fps_formula.hpp>
#include <onion/fps_publish.hpp>
#include <onion/fps_sample.h>
#include <onion/log.h>
#include <onion/ready.h>
#include <onion/proc_query.h>
#include <onion/system_tmp.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <ctime>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "daemon_ops.hpp"
#include "globalconf.hpp"

extern "C" {
int sceKernelGetProcessName(int pid, char *name);
int sceKernelGetAppInfo(pid_t pid, void *info);
int sceSystemServiceGetAppIdOfRunningBigApp();
int sceSystemServiceGetAppTitleId(int app_id, char *title_id);

/* libNineS: ptrace-injects an ELF into the process `pid`. */
bool Inject_Toolbox(int pid, uint8_t *elf);

/* Embedded fps_overlay ELF (see embedded_overlay.c). */
extern uint8_t fps_overlay_elf_start[];

/* kstuff probe (see kstuff_check.c). */
bool fps_kstuff_active(void);
}

/* Keeps the overlay injected into SceShellUI. Re-injects after a ShellUI
 * restart (new pid, e.g. Rest Mode resume). */
static void *injector_thread(void *) {
  pid_t injected_pid = -1;
  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const pid_t pid = onion_find_pid("SceShellUI");
    if (pid > 0 && pid != injected_pid &&
        !onion_ready_matches_pid(ONION_READY_TOOLBOX, pid)) {
      LOG_INFO("injecting fps overlay into SceShellUI pid=%d",
               static_cast<int>(pid));
      if (Inject_Toolbox(static_cast<int>(pid), fps_overlay_elf_start) &&
          onion_ready_wait_pid(ONION_READY_TOOLBOX, pid, 45000, 250)) {
        injected_pid = pid;
        LOG_INFO("fps overlay ready (pid=%d)", static_cast<int>(pid));
      } else {
        LOG_ERROR("fps overlay injection failed (pid=%d), retrying",
                  static_cast<int>(pid));
        sleep(10);
      }
    } else if (pid > 0 && onion_ready_matches_pid(ONION_READY_TOOLBOX, pid)) {
      injected_pid = pid;
    }
    sleep(3);
  }
  return nullptr;
}

std::atomic_bool g_stack_shutting_down{false};
onion::SettingsStore g_settings;

bool Get_Running_App_TID(std::string &title_id, int &BigAppid) {
  char tid[255];
  BigAppid = sceSystemServiceGetAppIdOfRunningBigApp();
  if (BigAppid < 0)
    return false;
  std::memset(tid, 0, sizeof tid);
  if (sceSystemServiceGetAppTitleId(BigAppid, tid) != 0)
    return false;
  title_id = tid;
  return true;
}

/* ---- Safe mode only --------------------------------------------------------
 * The full render counter / PS4 BC hook path is intentionally removed. The
 * standalone FPS payload must stay in the scanout-only path so it cannot touch
 * game memory, kernel memory, or PS4 compatibility hooks. */
static constexpr unsigned kWarmupSec = 0;
static constexpr useconds_t kSafeSampleUs = 100000; /* 10 Hz */

static double mono_sec() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) +
         static_cast<double>(ts.tv_nsec) * 1e-9;
}


/* ---- Game display name ------------------------------------------------------
 * Read from the app's metadata (param.json for PS5, param.sfo for PS4) once per
 * title change and published with every sample for the overlay's title row. */
static bool read_small_file(const char *path, std::string &out) {
  FILE *f = std::fopen(path, "rb");
  if (!f)
    return false;
  out.clear();
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
    out.append(buf, n);
    if (out.size() > 512 * 1024)
      break;
  }
  std::fclose(f);
  return !out.empty();
}

static std::string json_title_name(const std::string &j) {
  size_t p = j.find("\"titleName\"");
  if (p == std::string::npos)
    return "";
  p = j.find(':', p);
  if (p == std::string::npos)
    return "";
  p = j.find('"', p);
  if (p == std::string::npos)
    return "";
  std::string r;
  for (++p; p < j.size() && j[p] != '"'; ++p) {
    if (j[p] == '\\' && p + 1 < j.size()) {
      ++p;
      if (j[p] == 'n' || j[p] == 't' || j[p] == 'r')
        r += ' ';
      else if (j[p] == 'u') /* keep it simple: skip \uXXXX */
        p += 4;
      else
        r += j[p];
    } else {
      r += j[p];
    }
  }
  return r;
}

static std::string sfo_title(const std::string &d) {
  const auto *b = reinterpret_cast<const uint8_t *>(d.data());
  const size_t n = d.size();
  if (n < 20 || std::memcmp(b, "\0PSF", 4) != 0)
    return "";
  auto u16 = [&](size_t o) -> uint32_t { return b[o] | (b[o + 1] << 8); };
  auto u32 = [&](size_t o) -> uint32_t {
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) |
           (static_cast<uint32_t>(b[o + 3]) << 24);
  };
  const size_t key_tbl = u32(8), data_tbl = u32(12), count = u32(16);
  for (size_t i = 0; i < count; ++i) {
    const size_t e = 20 + 16 * i;
    if (e + 16 > n)
      break;
    const size_t ko = key_tbl + u16(e);
    const size_t doff = data_tbl + u32(e + 12);
    const size_t len = u32(e + 4);
    if (ko >= n || doff >= n)
      continue;
    const char *key = reinterpret_cast<const char *>(b + ko);
    if (strnlen(key, n - ko) == 5 && std::memcmp(key, "TITLE", 5) == 0) {
      const size_t take = std::min(len, n - doff);
      return std::string(reinterpret_cast<const char *>(b + doff),
                         strnlen(reinterpret_cast<const char *>(b + doff),
                                 take));
    }
  }
  return "";
}

static std::string resolve_title_name(const std::string &tid) {
  static const char *kJson[] = {
      "/system_data/priv/appmeta/%s/param.json",
      "/system_data/priv/appmeta/external/%s/param.json",
      "/system_ex/app/%s/sce_sys/param.json",
      "/user/appmeta/%s/param.json",
  };
  static const char *kSfo[] = {
      "/system_data/priv/appmeta/%s/param.sfo",
      "/system_data/priv/appmeta/external/%s/param.sfo",
      "/user/appmeta/%s/param.sfo",
  };
  char path[256];
  std::string data;
  for (const char *fmt : kJson) {
    std::snprintf(path, sizeof path, fmt, tid.c_str());
    if (read_small_file(path, data)) {
      std::string name = json_title_name(data);
      if (!name.empty())
        return name;
    }
  }
  for (const char *fmt : kSfo) {
    std::snprintf(path, sizeof path, fmt, tid.c_str());
    if (read_small_file(path, data)) {
      std::string name = sfo_title(data);
      if (!name.empty())
        return name;
    }
  }
  return "";
}

static std::string g_title_name; /* name of the title currently published */

static void publish_sample(bool valid, const char *tid, float fps) {
  OnionFpsSample s{};
  s.pid = valid ? 1 : -1;
  s.valid = valid ? 1 : 0;
  s.source = ONION_FPS_SRC_SCANOUT;
  s.fps = valid ? fps : 0.f;
  s.unix_ns = onion_fps_realtime_ns();
  if (tid) {
    std::strncpy(s.title_id, tid, sizeof(s.title_id) - 1);
    std::strncpy(s.title_name, g_title_name.c_str(), sizeof(s.title_name) - 1);
  }
  onion::fps::publish(s);
}

/* Tells the overlay (inside SceShellUI) that this payload is going away so it
 * can remove its widgets immediately. A killed payload (SIGKILL) cannot do
 * this; the overlay then notices the heartbeat stopped (see
 * fps_payload_alive() in prx_overlay.cpp). */
static void publish_shutdown() {
  OnionFpsSample s{};
  s.pid = ONION_FPS_PID_SHUTDOWN;
  s.valid = 0;
  s.source = ONION_FPS_SRC_SCANOUT;
  s.unix_ns = onion_fps_realtime_ns();
  onion::fps::publish(s);
}

static void *safe_sampler_thread(void *) {
  onion::fps::DceSource dce;
  std::string cached_tid;
  double started = 0.0;
  bool have = false;
  uint64_t last_count = 0;
  double last_t = 0.0;
  float window[onion::fps::kWindow]{};
  int wn = 0, wi = 0;
  (void)onion::fps::publish_open();
  LOG_INFO("safe fps sampler started (scanout only, warmup %us)", kWarmupSec);

  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    std::string tid;
    int app_id = 0;
    if (!Get_Running_App_TID(tid, app_id)) {
      cached_tid.clear();
      g_title_name.clear();
      have = false;
      wn = wi = 0;
      publish_sample(false, nullptr, 0.f);
      sleep(1);
      continue;
    }
    if (tid != cached_tid) {
      cached_tid = tid;
      g_title_name = resolve_title_name(tid);
      LOG_INFO("safe fps: title name '%s'", g_title_name.c_str());
      started = mono_sec();
      have = false;
      wn = wi = 0;
      LOG_INFO("safe fps: game %s detected; sampling immediately", tid.c_str());
    }
    if (mono_sec() - started < kWarmupSec) {
      publish_sample(false, tid.c_str(), 0.f);
      sleep(1);
      continue;
    }

    uint64_t count = 0;
    if (dce.sample(&count)) {
      const double now = mono_sec();
      if (have && count > last_count && now > last_t) {
        const double hz =
            onion::fps::hz_from_delta(count - last_count, now - last_t);
        if (hz > 0.0) {
          window[wi] = static_cast<float>(hz);
          wi = (wi + 1) % onion::fps::kWindow;
          if (wn < onion::fps::kWindow)
            ++wn;
          publish_sample(true, tid.c_str(),
                         onion::fps::rolling_mean(window, wn));
        }
      }
      last_count = count;
      last_t = now;
      have = true;
    } else if (dce.unavailable()) {
      static bool logged = false;
      if (!logged) {
        LOG_ERROR("safe fps: /dev/dce unavailable");
        logged = true;
      }
      /* Keep the heartbeat alive; the overlay hides itself if it stops. */
      publish_sample(false, tid.c_str(), 0.f);
      sleep(1);
    }
    usleep(kSafeSampleUs);
  }
  publish_shutdown();
  return nullptr;
}

static void on_term(int) {
  g_stack_shutting_down.store(true, std::memory_order_release);
}

int main() {
  mkdir(ONION_SYSTEM_TMP_ROOT, 0777);
  onion_log_configure("FPS", "/data/onion_fps.log");
  onion_log_set_level(ONION_LOG_INFO);
  signal(SIGTERM, on_term);
  signal(SIGINT, on_term);

  /* Needed by onion_find_pid_ex(..., for_bigapp=true). */
  onion_proc_set_sce_hooks(
      [](int pid, char *name) -> int {
        return sceKernelGetProcessName(pid, name);
      },
      [](pid_t pid, void *info) -> int {
        return sceKernelGetAppInfo(pid, info);
      },
      []() -> int { return sceSystemServiceGetAppIdOfRunningBigApp(); });

  LOG_INFO("standalone fps counter started (safe scanout-only mode + overlay)");

  /* kstuff is optional. With it, userland mprotect(RWX) works; without it the
   * detour code falls back to a kernel mprotect. If the console's hypervisor
   * refuses both, injection fails and is retried (see the log). */
  if (fps_kstuff_active())
    LOG_INFO("kstuff detected");
  else
    LOG_WARN("kstuff not detected; trying to inject anyway");

  /* Preflight 2: never inject twice. A live overlay is marked ready for the
   * current SceShellUI pid; the injector thread skips it, and a new ShellUI
   * pid (Rest Mode resume) is picked up and injected once. */
  {
    const pid_t shellui = onion_find_pid("SceShellUI");
    if (shellui > 0 && onion_ready_matches_pid(ONION_READY_TOOLBOX, shellui))
      LOG_WARN("SceShellUI pid=%d already has the overlay; skipping injection",
               static_cast<int>(shellui));
  }

  pthread_t render_thr{}, inject_thr{};
  pthread_create(&render_thr, nullptr, safe_sampler_thread, nullptr);
  pthread_create(&inject_thr, nullptr, injector_thread, nullptr);

  pthread_join(render_thr, nullptr);
  pthread_join(inject_thr, nullptr);
  return 0;
}
