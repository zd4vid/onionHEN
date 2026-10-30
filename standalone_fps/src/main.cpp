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
#include <memory>
#include <string>

#include <ctime>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/kernel.h>

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

static double mono_sec();

static bool flag_file(const char *path) { return access(path, F_OK) == 0; }
/* Diagnostics for isolating crashes: /data/onion_fps_noinject = sampler only,
 * /data/onion_fps_nodce = injection only (no /dev/dce reads). */
static constexpr int kMaxInjectAttempts = 3;

/* The full daemon never ELF-loads into SceShellUI until these modules are
 * mapped (same gate kstuff uses; see source/daemon/source/daemon_inject.cpp).
 * Cheap safety check; it passes immediately on a console that has been up. */
static bool shellui_sprx_loaded(pid_t pid, const char *name) {
  uint32_t handle = static_cast<uint32_t>(-1);
  return kernel_dynlib_handle(pid, name, &handle) == 0 &&
         handle != static_cast<uint32_t>(-1);
}

static bool shellui_ready_for_inject(pid_t pid) {
  return shellui_sprx_loaded(pid, "libSceNpTrophy.sprx") &&
         shellui_sprx_loaded(pid, "libSceNpTrophy2.sprx");
}

/* Waits for the overlay's ready marker, but gives up as soon as the target
 * SceShellUI is gone or replaced. When the system restarts a hung ShellUI the
 * old pid never becomes ready, and waiting out the full timeout delayed the
 * injection into the new ShellUI by ~45 s. */
static bool wait_overlay_ready(pid_t pid, int timeout_ms) {
  for (int waited = 0; waited <= timeout_ms; waited += 250) {
    if (onion_ready_matches_pid(ONION_READY_FPS_OVERLAY, pid))
      return true;
    if (g_stack_shutting_down.load(std::memory_order_acquire))
      return false;
    if (!isProcessAlive(pid) || onion_find_pid("SceShellUI") != pid) {
      LOG_WARN("SceShellUI pid=%d went away while waiting for the overlay",
               static_cast<int>(pid));
      return false;
    }
    usleep(250 * 1000);
  }
  return onion_ready_matches_pid(ONION_READY_FPS_OVERLAY, pid);
}

/* Seconds to wait before the first injection (default 5). OnionHEN spends
 * 20+ s starting its services before its daemon touches SceShellUI; injecting within a few
 * milliseconds of being loaded from the on-console payload manager (a web page
 * that is still busy handing the payload over) hung the browser until the
 * system raised an error. Override with a plain number in
 * /data/onion_fps_delay (0 disables the wait). */
static unsigned initial_inject_delay_sec() {
  unsigned delay = 5;
  FILE *f = std::fopen("/data/onion_fps_delay", "r");
  if (f) {
    unsigned v = 0;
    if (std::fscanf(f, "%u", &v) == 1)
      delay = v > 120 ? 120 : v;
    std::fclose(f);
  }
  return delay;
}

/* ---- Running next to OnionHEN -----------------------------------------------
 * Both this payload and OnionHEN's daemon inject a module into SceShellUI.
 * They are kept apart in two ways:
 *  - this overlay signals its own ready marker (ONION_READY_FPS_OVERLAY), never
 *    the toolbox one, so OnionHEN's daemon does not think its toolbox is already
 *    in SceShellUI and skips it;
 *  - while OnionHEN is running and its toolbox is not in the current SceShellUI
 *    yet, this payload waits instead of injecting, so the two ptrace injections
 *    never overlap. */
static bool onionhen_running() {
  return onion_find_pid_substr("onion_daemon.elf") > 0 ||
         onion_find_pid_substr("onion_util.elf") > 0;
}

/* Longest this payload waits for OnionHEN's toolbox before injecting anyway. */
static constexpr double kOnionHenWaitSec = 5.0;

/* Keeps the overlay injected into SceShellUI. Re-injects after a ShellUI
 * restart (new pid, e.g. Rest Mode resume). */
static void *injector_thread(void *) {
  pid_t injected_pid = -1;
  const unsigned initial_delay = initial_inject_delay_sec();
  LOG_INFO("waiting %us before the first injection", initial_delay);
  for (unsigned i = 0;
       i < initial_delay && !g_stack_shutting_down.load(std::memory_order_acquire);
       ++i)
    sleep(1);
  pid_t waiting_pid = -1;
  pid_t attempts_pid = -1;
  int attempts = 0;
  double wait_started = 0.0;
  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const pid_t pid = onion_find_pid("SceShellUI");
    if (pid > 0 && pid != injected_pid &&
        !onion_ready_matches_pid(ONION_READY_FPS_OVERLAY, pid)) {
      if (waiting_pid != pid) {
        waiting_pid = pid;
        wait_started = mono_sec();
      }
      if (!shellui_ready_for_inject(pid)) {
        static double last_log = 0.0;
        const double now = mono_sec();
        if (now - last_log >= 10.0) {
          LOG_INFO("waiting for SceShellUI pid=%d NpTrophy modules (%.0fs)",
                   static_cast<int>(pid), now - wait_started);
          last_log = now;
        }
        sleep(1);
        continue;
      }
      if (onionhen_running() &&
          !onion_ready_matches_pid(ONION_READY_TOOLBOX, pid) &&
          mono_sec() - wait_started < kOnionHenWaitSec) {
        static double last_onion_log = 0.0;
        const double now = mono_sec();
        if (now - last_onion_log >= 10.0) {
          LOG_INFO("OnionHEN is running; waiting for its toolbox in SceShellUI "
                   "pid=%d before injecting (%.0fs)",
                   static_cast<int>(pid), now - wait_started);
          last_onion_log = now;
        }
        sleep(1);
        continue;
      }
      LOG_INFO("SceShellUI pid=%d ready for inject after %.1fs",
               static_cast<int>(pid), mono_sec() - wait_started);
      if (attempts_pid != pid) {
        attempts_pid = pid;
        attempts = 0;
      }
      if (attempts >= kMaxInjectAttempts) {
        sleep(3);
        continue; /* gave up on this SceShellUI; a new pid starts over */
      }
      ++attempts;
      LOG_INFO("injecting fps overlay into SceShellUI pid=%d (attempt %d/%d)",
               static_cast<int>(pid), attempts, kMaxInjectAttempts);
      if (Inject_Toolbox(static_cast<int>(pid), fps_overlay_elf_start)) {
        /* The module's thread is running now. Never inject a second copy into
         * this process, even if it is slow to report ready. */
        injected_pid = pid;
        attempts = kMaxInjectAttempts;
        if (wait_overlay_ready(pid, 45000))
          LOG_INFO("fps overlay ready (pid=%d)", static_cast<int>(pid));
        else
          LOG_WARN("fps overlay not ready after 45 s (pid=%d); not re-injecting",
                   static_cast<int>(pid));
      } else {
        LOG_ERROR("fps overlay injection failed (pid=%d), attempt %d/%d",
                  static_cast<int>(pid), attempts, kMaxInjectAttempts);
        sleep(10u * static_cast<unsigned>(attempts));
      }
    } else if (pid > 0 && onion_ready_matches_pid(ONION_READY_FPS_OVERLAY, pid)) {
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
static constexpr useconds_t kSafeSampleUs = 100000; /* 10 Hz, also the heartbeat */

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

/* Passes (one per kSafeSampleUs) the running-app lookup may fail in a row
 * before the title is treated as gone. A transient failure must not throw away
 * the last FPS value: on a static screen (paused game) no new frames arrive, so
 * a wiped value would stay "--" until the picture moves again. */
static constexpr int kAppLostPasses = 5;
/* Passes of scanout read failures in a row (5 s) before /dev/dce is reopened. */
static constexpr int kDceReopenPasses = 50;
static constexpr int kDceMaxReopensPerTitle = 6;
/* A scanout rate measured this recently may seed a title that has no value yet. */
static constexpr double kSeedFreshSec = 1.5;
/* The last value of a title is reused if the same title shows up again within
 * this long (paused/static screens produce no frames to re-measure). */
static constexpr double kHoldSameTitleSec = 7200.0;
static constexpr double kStallLogSec = 10.0;

static void *safe_sampler_thread(void *) {
  std::unique_ptr<onion::fps::DceSource> dce(new onion::fps::DceSource());
  std::string cached_tid;
  bool have = false;
  uint64_t last_count = 0;
  double last_t = 0.0;
  float window[onion::fps::kWindow]{};
  int wn = 0, wi = 0;

  /* Latest rate measured from the scanout counter, whatever is on screen. The
   * counter is read all the time (not only while a game runs) so that a game
   * start already has a baseline and a recent rate to show. */
  float recent_hz = 0.f;
  double recent_hz_t = 0.0;
  /* Last value shown for a title, reused if that title reappears. */
  std::string good_tid;
  float good_fps = 0.f;
  double good_t = 0.0;

  /* The name read from the app metadata can come back empty (files not there
   * yet): retry for a while and reuse the last name known for the same title. */
  std::string name_tid, name_keep;
  int name_tries = 0;
  double next_name_try = 0.0;

  int lost_passes = 0;
  int dce_fail_passes = 0;
  int reopens_this_title = 0;
  double last_advance_t = 0.0;
  bool stall_logged = false;
  double detected_t = 0.0;
  bool first_value_pending = false;

  const bool use_dce = !flag_file("/data/onion_fps_nodce");
  if (!use_dce)
    LOG_WARN("/data/onion_fps_nodce present: not reading /dev/dce");
  (void)onion::fps::publish_open();
  LOG_INFO("safe fps sampler started (scanout only)");

  while (!g_stack_shutting_down.load(std::memory_order_acquire)) {
    const double now = mono_sec();

    /* 1. Scanout counter. */
    uint64_t count = 0;
    if (use_dce && dce->sample(&count)) {
      dce_fail_passes = 0;
      if (have && count > last_count && now > last_t) {
        const double hz =
            onion::fps::hz_from_delta(count - last_count, now - last_t);
        last_advance_t = now;
        if (stall_logged) {
          LOG_INFO("safe fps: frames resumed");
          stall_logged = false;
        }
        if (hz > 0.0) {
          recent_hz = static_cast<float>(hz);
          recent_hz_t = now;
          window[wi] = recent_hz;
          wi = (wi + 1) % onion::fps::kWindow;
          if (wn < onion::fps::kWindow)
            ++wn;
          /* Samples only count towards a title while one is running. */
          if (cached_tid.empty())
            wn = wi = 0;
        }
      } else if (have && count < last_count) {
        LOG_WARN("safe fps: flip counter went backwards; new baseline");
      }
      last_count = count;
      last_t = now;
      have = true;
    } else if (!use_dce) {
      have = false;
    } else {
      have = false;
      ++dce_fail_passes;
      /* A stale /dev/dce (display reinitialised, long idle) never recovers by
       * itself; start over with a fresh handle while a title is running. */
      if (dce_fail_passes >= kDceReopenPasses && !cached_tid.empty() &&
          reopens_this_title < kDceMaxReopensPerTitle) {
        ++reopens_this_title;
        LOG_WARN("safe fps: scanout read keeps failing (errno %d); reopening "
                 "/dev/dce (%d/%d)",
                 dce->last_errno(), reopens_this_title, kDceMaxReopensPerTitle);
        dce.reset(new onion::fps::DceSource());
        dce_fail_passes = 0;
      }
    }

    /* 2. Running title, with a short debounce on lookup failures. */
    std::string tid;
    int app_id = 0;
    if (!Get_Running_App_TID(tid, app_id)) {
      if (!cached_tid.empty() && ++lost_passes < kAppLostPasses) {
        publish_sample(wn > 0, cached_tid.c_str(),
                       wn > 0 ? onion::fps::rolling_mean(window, wn) : 0.f);
        usleep(kSafeSampleUs);
        continue;
      }
      if (!cached_tid.empty())
        LOG_INFO("safe fps: game %s gone", cached_tid.c_str());
      cached_tid.clear();
      g_title_name.clear();
      wn = wi = 0;
      lost_passes = 0;
      publish_sample(false, nullptr, 0.f);
      usleep(kSafeSampleUs);
      continue;
    }
    lost_passes = 0;

    if (tid != cached_tid) {
      cached_tid = tid;
      g_title_name = resolve_title_name(tid);
      if (g_title_name.empty() && tid == name_tid)
        g_title_name = name_keep;
      name_tries = 0;
      next_name_try = now + 2.0;
      LOG_INFO("safe fps: game %s detected, title name '%s'", tid.c_str(),
               g_title_name.c_str());
      wn = wi = 0;
      detected_t = now;
      first_value_pending = true;
      stall_logged = false;
      reopens_this_title = 0;
      last_advance_t = now;

      /* Show something straight away instead of "--" until frames are seen. */
      float seed = 0.f;
      const char *why = nullptr;
      if (tid == good_tid && good_fps >= onion::fps::kFpsMin &&
          now - good_t < kHoldSameTitleSec) {
        seed = good_fps;
        why = "last value of this title";
      } else if (recent_hz >= onion::fps::kFpsMin &&
                 recent_hz <= onion::fps::kFpsMax &&
                 now - recent_hz_t < kSeedFreshSec) {
        seed = recent_hz;
        why = "current scanout rate";
      }
      if (seed > 0.f) {
        window[0] = seed;
        wi = 1 % onion::fps::kWindow;
        wn = 1;
        LOG_INFO("safe fps: seeded with %.1f fps (%s)", seed, why);
      }
    }

    if (g_title_name.empty() && name_tries < 20 && now >= next_name_try) {
      ++name_tries;
      next_name_try = now + 2.0;
      g_title_name = resolve_title_name(tid);
      if (!g_title_name.empty())
        LOG_INFO("safe fps: title name '%s' (try %d)", g_title_name.c_str(),
                 name_tries);
    }
    if (!g_title_name.empty()) {
      name_tid = tid;
      name_keep = g_title_name;
    }

    if (!stall_logged && now - last_advance_t >= kStallLogSec) {
      LOG_INFO("safe fps: no new frames for %.0f s (%s)", kStallLogSec,
               wn > 0 ? "holding the last value" : "nothing to show yet");
      stall_logged = true;
    }

    /* Every pass is a heartbeat; with no new frame data (paused game,
     * transient ioctl failure) the last known value is repeated. */
    const float shown = wn > 0 ? onion::fps::rolling_mean(window, wn) : 0.f;
    if (wn > 0) {
      good_tid = tid;
      good_fps = shown;
      good_t = now;
      if (first_value_pending) {
        LOG_INFO("safe fps: first value %.1f fps, %.0f ms after detection",
                 shown, (now - detected_t) * 1000.0);
        first_value_pending = false;
      }
    }
    publish_sample(wn > 0, tid.c_str(), shown);
    usleep(kSafeSampleUs);
  }
  publish_shutdown();
  return nullptr;
}

static void on_term(int) {
  g_stack_shutting_down.store(true, std::memory_order_release);
}

/* Loaders hand the payload their connection as stdin/stdout/stderr and may wait
 * for it to close (an on-console payload manager blocks its UI on that and the
 * system reports an error after ~10 s; a PC sender does not care). Point the
 * standard descriptors at /dev/null so the loader's end is released, and ignore
 * SIGPIPE/SIGHUP so a loader that goes away cannot kill the payload. */
static void detach_from_loader(void) {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  const int fd = open("/dev/null", O_RDWR);
  if (fd < 0)
    return;
  for (int target = 0; target <= 2; ++target)
    (void)dup2(fd, target);
  if (fd > 2)
    close(fd);
}

int main() {
  detach_from_loader();
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
   * overlay's detour code falls back to a kernel mprotect. If the console
   * refuses both, injection fails and is retried (see the log). */
  if (fps_kstuff_active())
    LOG_INFO("kstuff detected");
  else
    LOG_WARN("kstuff not detected; injecting anyway (kernel mprotect fallback)");

  /* Preflight 2: never inject twice. A live overlay is marked ready for the
   * current SceShellUI pid; the injector thread skips it, and a new ShellUI
   * pid (Rest Mode resume) is picked up and injected once. */
  {
    const pid_t shellui = onion_find_pid("SceShellUI");
    if (shellui > 0 && onion_ready_matches_pid(ONION_READY_FPS_OVERLAY, shellui))
      LOG_WARN("SceShellUI pid=%d already has the overlay; skipping injection",
               static_cast<int>(shellui));
  }

  pthread_t render_thr{}, inject_thr{};
  pthread_create(&render_thr, nullptr, safe_sampler_thread, nullptr);
  const bool inject = !flag_file("/data/onion_fps_noinject");
  if (inject)
    pthread_create(&inject_thr, nullptr, injector_thread, nullptr);
  else
    LOG_WARN("/data/onion_fps_noinject present: sampler only, no injection");

  pthread_join(render_thr, nullptr);
  if (inject)
    pthread_join(inject_thr, nullptr);
  return 0;
}
