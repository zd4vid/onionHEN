/* Copyright (C) 2025 OnionHEN / LightningMods — P0 split. */


#include <cstring>
#include <time.h>
#include <algorithm>
#include "hooked_funcs.hpp"
#include "ipc.hpp"
#include "external_symbols.hpp"
#include "overlay_text_metrics.hpp"
#include <onion/fps_sample.h>
#include <onion/net.h>
#include <onion/settings.hpp>
#include "plugin_progress.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

extern MonoObject* rootWidget;
extern MonoObject* font;
extern OverlayLayout g_overlay_layout;
extern onion::Settings g_settings;
#include "shellui_state.hpp"
void RemoveGameWidget(RemoveWidget widget);
bool CreateGameWidget(CreateWidget widget);
MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* fontObj, int horzAlign, int vertAlign, float r, float g, float b, float a);
void Widget_Append_Child(MonoObject* widget, MonoObject* child);
MonoObject* CreateUIFont(int size, int style, int weight);

struct OrbisKernelTimespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct Proc_Stats
{
    int32_t lo_data;								//0x00
    uint32_t td_tid;						//0x04
    OrbisKernelTimespec user_cpu_usage_time;	//0x08
    OrbisKernelTimespec system_cpu_usage_time;  //0x18
}; //0x28

extern "C" {
    int sceKernelGetSocSensorTemperature(int sensorId, int* soctime);
    int get_page_table_stats(int vm, int type, int* total, int* free);
    int sceKernelGetCpuUsage(struct Proc_Stats* out, int32_t* size);
    int sceKernelGetThreadName(uint32_t id, char* out);
	int sceKernelGetCpuTemperature(int* cputemp);
    int sceKernelClockGettime(int clockId, OrbisKernelTimespec* tp);
}

struct Memory
{
    int Used;
    int Free;
    int Total;
    float Percentage;
};

struct thread_usages
{
    OrbisKernelTimespec current_time;	//0x00
    int Thread_Count;					//0x10
    char padding0[0x4];					//0x14
    Proc_Stats Threads[3072];			//0x18
};

int Thread_Count = 0;
float Usage[8] = { 0 };
float Average_Usage;
Memory RAM;
Memory VRAM;

Proc_Stats Stat_Data[3072];
thread_usages gThread_Data[2];


extern "C" int sceLncUtilKillAppWithReason(int appId, int reason);

void calc_usage(unsigned int idle_tid[8], thread_usages* cur, thread_usages* prev, float usage_out[8])
{
    if (cur->Thread_Count <= 0 || prev->Thread_Count <= 0) //Make sure our banks have threads
        return;

    //Calculate the Current time difference from the last bank to the current bank.
    float Current_Time_Total = ((prev->current_time.tv_sec + (prev->current_time.tv_nsec / 1000000000.0f)) - (cur->current_time.tv_sec + (cur->current_time.tv_nsec / 1000000000.0f)));

    //Here this could use to be improved but essetially what its doing is finding the thread information for the idle threads using their thread Index stored from before.
    struct Data_s
    {
        Proc_Stats* Cur;
        Proc_Stats* Prev;
    }Data[8];

    for (int i = 0; i < cur->Thread_Count; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (idle_tid[j] == cur->Threads[i].td_tid)
                Data[j].Cur = &cur->Threads[i];
        }
    }

    for (int i = 0; i < prev->Thread_Count; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            if (idle_tid[j] == prev->Threads[i].td_tid)
                Data[j].Prev = &prev->Threads[i];
        }
    }

    //Here we loop through each core to calculate the total usage time as its split into user/sustem
    for (int i = 0; i < 8; i++)
    {
        float Prev_Usage_Time = (Data[i].Prev->system_cpu_usage_time.tv_sec + (Data[i].Prev->system_cpu_usage_time.tv_nsec / 1000000.0f));
        Prev_Usage_Time += (Data[i].Prev->user_cpu_usage_time.tv_sec + (Data[i].Prev->user_cpu_usage_time.tv_nsec / 1000000.0f));

        float Cur_Usage_Time = (Data[i].Cur->system_cpu_usage_time.tv_sec + (Data[i].Cur->system_cpu_usage_time.tv_nsec / 1000000.0f));
        Cur_Usage_Time += (Data[i].Cur->user_cpu_usage_time.tv_sec + (Data[i].Cur->user_cpu_usage_time.tv_nsec / 1000000.0f));

        //We calculate the usage using usage time difference between the two samples divided by the current time difference.
        float Idle_Usage = ((Prev_Usage_Time - Cur_Usage_Time) / Current_Time_Total);

        if (Idle_Usage > 1.0f)
            Idle_Usage = 1.0f;

        if (Idle_Usage < 0.0f)
            Idle_Usage = 0.0f;

        //Get inverse of idle percentage and express in percent.
        usage_out[i] = (1.0f - Idle_Usage) * 100.0f;
    }
}

namespace {

constexpr int kMaxProcThreads = 3072;
constexpr int kCpuCores = 8;
constexpr int kOverlayInitDelayFrames = 30;
constexpr uint64_t kInitBackoffMinNs = 1000000000ull;  /* 1 s */
constexpr uint64_t kInitBackoffMaxNs = 10000000000ull; /* 10 s */
/* Every value (FPS, temps, RAM/VRAM, fan, CPU usage, IP) refreshes together every
 * 1 s of wall time, independent of the frame rate. */
constexpr uint64_t kOverlayUpdateIntervalNs = 1000000000ull;
#ifndef ONIONHEN_FPS_OVERLAY_ONLY
constexpr int kFpsUpdateIntervalFrames = 2; /* full build: FPS refreshes faster */
#endif

constexpr int kClockIdRealtime = 4;
constexpr int kVmSystem = 1;
constexpr int kPageTableRam = 1;
constexpr int kPageTableVram = 2;

MonoObject *find_label(const char *widget_name) {
  MonoObject *root = Get_Property<MonoObject *>(
      pui_img, "Sce.PlayStation.PUI.UI2", "Scene", Game, "RootWidget");
  MonoClass *widget_cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
  return Invoke<MonoObject *>(pui_img, widget_cls, root, "FindWidgetByName",
                              mono_string_new(Root_Domain, widget_name));
}

void set_label_text(const char *widget_name, const char *text) {
  MonoClass *label_cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
  MonoObject *label = find_label(widget_name);
  if (label)
    Set_Property(label_cls, label, "Text", mono_string_new(Root_Domain, text));
}

void set_label_layout(const char *widget_name, float margin_left,
                      float margin_top, float width, float cell_y) {
  MonoClass *label_cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");
  MonoClass *panel_cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
  MonoObject *label = find_label(widget_name);
  if (!label)
    return;

  char cell_name[96];
  std::snprintf(cell_name, sizeof(cell_name), "%s_cell", widget_name);
  MonoObject *cell = find_label(cell_name);
  if (cell) {
    Set_Property(panel_cls, cell, "X", margin_left);
    Set_Property(panel_cls, cell, "Y", cell_y);
    Set_Property(panel_cls, cell, "Width", width);
    Set_Property(panel_cls, cell, "Height", g_overlay_layout.bar_h);
  }

  Set_Property(label_cls, label, "PositionType", 1);
  Set_Property(label_cls, label, "MarginLeft", 0.0f);
  Set_Property(label_cls, label, "MarginTop", margin_top);
  Set_Property(label_cls, label, "Width", width);
  Set_Property(label_cls, label, "HorizontalAlignment", 0);
  Set_Property(label_cls, label, "VerticalAlignment", 0);
  Set_Property(label_cls, label, "FitWidthToText", false);
  Set_Property(label_cls, label, "FitHeightToText", true);
  Set_Property(label_cls, label, "NumberOfLines", 1);
}

#ifndef ONIONHEN_FPS_OVERLAY_ONLY /* standalone build has no background panel */
/** Resize/move the translucent background panel to wrap the stacked block. */
void set_bg_panel_rect(float x, float y, float w, float h) {
  MonoObject *panel = find_label("id_onion_overlay_bg");
  if (!panel)
    return;
  MonoClass *panel_cls =
      mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Panel");
  Set_Property(panel_cls, panel, "X", x);
  Set_Property(panel_cls, panel, "Y", y);
  Set_Property(panel_cls, panel, "Width", w);
  Set_Property(panel_cls, panel, "Height", h);
}
#endif

#ifdef ONIONHEN_FPS_OVERLAY_ONLY
/* "Name / TITLEID" of the running game, empty on the home screen (no row). */
char g_overlay_title[96] = {};

/**
 * Standalone FPS overlay: plain text rows, no background panel.
 *
 *   FPS: 59.94
 *   CPU: 43°C / GPU: 35°C
 *   RAM: 18% 3102MB
 *   VRAM: 15% 2574MB
 *   FAN: 74%
 *   IP: 192.168.1.99
 *   Teardown / PPSA15246      (only while a game is running)
 *
 * Each row is a single label; widgets not used by a row are parked
 * off-screen. Rows whose value is not available yet are skipped.
 */
void layout_bar_labels(const char *fps_str, const char *cpu_temp,
                       const char *cpu_usage, const char *gpu_temp,
                       const char *gpu_usage, const char *ram_str,
                       const char *ip_str, const char *fan_str) {
  (void)cpu_usage;

  const float font_pt = static_cast<float>(onion::overlay::effective_font_pt(
      onion::overlay_font_pt(g_settings.overlay_font_size)));
  constexpr float kOff = -4096.0f;

  struct Row {
    const char *id;
    char text[96];
  };
  Row rows[8];
  int nrows = 0;

  if (g_settings.overlay_fps) {
    rows[nrows].id = "id_fps_label";
    std::snprintf(rows[nrows].text, sizeof(rows[nrows].text), "FPS: %s",
                  (fps_str && fps_str[0]) ? fps_str : "--");
    ++nrows;
  }

  const bool show_cpu = g_settings.overlay_cpu;
  const bool show_gpu = g_settings.overlay_gpu;
  if (show_cpu || show_gpu) {
    const char *c = (cpu_temp && cpu_temp[0]) ? cpu_temp : "--";
    const char *g = (gpu_temp && gpu_temp[0]) ? gpu_temp : "--";
    rows[nrows].id = "id_cpu_label";
    if (show_cpu && show_gpu)
      std::snprintf(rows[nrows].text, sizeof(rows[nrows].text),
                    "CPU: %s / GPU: %s", c, g);
    else if (show_cpu)
      std::snprintf(rows[nrows].text, sizeof(rows[nrows].text), "CPU: %s", c);
    else
      std::snprintf(rows[nrows].text, sizeof(rows[nrows].text), "GPU: %s", g);
    ++nrows;
  }

  auto add_row = [&](const char *id, const char *fmt, const char *value) {
    if (!value || !value[0] || nrows >= 8)
      return;
    rows[nrows].id = id;
    std::snprintf(rows[nrows].text, sizeof(rows[nrows].text), fmt, value);
    ++nrows;
  };
  if (g_settings.overlay_ram)
    add_row("id_ram_label", "RAM: %s", ram_str);
  if (g_settings.overlay_ram && g_settings.overlay_gpu)
    add_row("id_ram_value", "VRAM: %s", gpu_usage);
  if (g_settings.overlay_fan)
    add_row("id_fan_label", "FAN: %s", fan_str);
  if (g_settings.overlay_ip)
    add_row("id_ip_label", "IP: %s", ip_str);
  add_row("id_cpu_usage_value", "%s", g_overlay_title);
  if (nrows == 0)
    return;

  const float screen_w = g_overlay_layout.bar_w;
  if (screen_w <= 1.0f)
    return;

  const float line_h = g_overlay_layout.bar_h;
  const bool bottom = onion::overlay::bar_edge_from_pos(g_settings.overlay_pos) ==
                      onion::overlay::BarEdge::Bottom;
  /* Bottom edge: grow upward so the last row sits on the screen edge. */
  const float y_base = bottom ? g_overlay_layout.bar_y -
                                    static_cast<float>(nrows - 1) * line_h
                              : g_overlay_layout.bar_y +
                                    onion::overlay::kStandaloneTopInset;
  const float margin_top = g_overlay_layout.label_margin_top;

  static const char *kAll[] = {
      "id_fps_label",        "id_fps_value",       "id_fps_sep",
      "id_cpu_label",        "id_cpu_temp_value",  "id_cpu_usage_value",
      "id_cpu_sep",          "id_gpu_label",       "id_gpu_temp_value",
      "id_gpu_usage_value",  "id_gpu_sep",         "id_ram_label",
      "id_ram_value",        "id_ram_sep",         "id_ip_label",
      "id_ip_value",         "id_ip_sep",          "id_fan_label",
      "id_fan_value",        "id_fan_sep",
  };
  for (const char *id : kAll)
    set_label_layout(id, kOff, margin_top, 0.0f, y_base);

  for (int r = 0; r < nrows; ++r) {
    /* The estimator is tuned for 18pt bold; pad it so regular text never clips. */
    const float w =
        onion::overlay::estimate_text_width(rows[r].text, font_pt) * 1.3f +
        16.0f;
    const float x = onion::overlay::pack_origin(
        screen_w, w,
        onion::overlay::bar_align_from_value(g_settings.overlay_align));
    const float y = y_base + static_cast<float>(r) * line_h;
    set_label_layout(rows[r].id, x, margin_top, w, y);
    set_label_text(rows[r].id, rows[r].text);
  }
}
#else
/**
 * Stacked layout: one metric group per row, values lined up in columns.
 *
 *   FPS  60
 *   CPU  54C  12%
 *   GPU  61C  40%
 *
 * The block is placed left/center/right by overlay.align and top/bottom by
 * overlay.pos. Labels use PositionType + margins layout.
 */
void layout_bar_labels(const char *fps_str, const char *cpu_temp,
                       const char *cpu_usage, const char *gpu_temp,
                       const char *gpu_usage, const char *ram_str,
                       const char *ip_str, const char *fan_str) {
  const float font_pt = static_cast<float>(onion::overlay::effective_font_pt(
      onion::overlay_font_pt(g_settings.overlay_font_size)));
  const float scale = font_pt / onion::overlay::kRefFontSize;
  const float kPairGap = 10.0f * scale; /* label column -> value column */
  const float kValGap = 10.0f * scale;  /* value column -> second value */
  const float kPad = 8.0f * scale;      /* background padding */
  constexpr float kOff = -4096.0f;

  struct GroupSpec {
    bool on;
    const char *id_l;
    const char *lab;
    const char *id_v0;
    const char *v0;
    const char *id_v1;
    const char *v1;
  };
  GroupSpec specs[onion::kOverlayMetricCount]{};
  specs[onion::kOverlayMetricFps] = {g_settings.overlay_fps && fps_str,
                                     "id_fps_label", "FPS", "id_fps_value",
                                     fps_str, nullptr, nullptr};
  specs[onion::kOverlayMetricCpu] = {
      (g_settings.overlay_cpu || g_settings.all_cpu_usage) && cpu_temp,
      "id_cpu_label", "CPU", "id_cpu_temp_value", cpu_temp,
      "id_cpu_usage_value", cpu_usage};
  specs[onion::kOverlayMetricGpu] = {g_settings.overlay_gpu && gpu_temp,
                                     "id_gpu_label", "GPU", "id_gpu_temp_value",
                                     gpu_temp, "id_gpu_usage_value", gpu_usage};
  specs[onion::kOverlayMetricMemory] = {g_settings.overlay_ram && ram_str,
                                        "id_ram_label", "RAM", "id_ram_value",
                                        ram_str, nullptr, nullptr};
  specs[onion::kOverlayMetricIp] = {g_settings.overlay_ip && ip_str,
                                    "id_ip_label", "IP", "id_ip_value", ip_str,
                                    nullptr, nullptr};
  specs[onion::kOverlayMetricFan] = {g_settings.overlay_fan && fan_str,
                                     "id_fan_label", "FAN", "id_fan_value",
                                     fan_str, nullptr, nullptr};

  auto order = g_settings.overlay_order;
  onion::overlay_normalize_order(order);
  GroupSpec rows[onion::kOverlayMetricCount];
  int nrows = 0;
  for (int metric : order) {
    if (metric < 0 || metric >= onion::kOverlayMetricCount)
      continue;
    if (!specs[metric].on || !specs[metric].v0 || !specs[metric].v0[0])
      continue;
    rows[nrows++] = specs[metric];
  }
  if (nrows == 0)
    return;

  /* Column widths: widest label, widest first value, widest second value. */
  float label_w = 0.f, v0_w = 0.f, v1_w = 0.f;
  bool any_v1 = false;
  for (int r = 0; r < nrows; ++r) {
    label_w = std::max(label_w,
                       onion::overlay::estimate_text_width(rows[r].lab, font_pt));
    v0_w = std::max(v0_w,
                    onion::overlay::estimate_text_width(rows[r].v0, font_pt));
    if (rows[r].id_v1 && rows[r].v1 && rows[r].v1[0]) {
      any_v1 = true;
      v1_w = std::max(
          v1_w, onion::overlay::estimate_text_width(rows[r].v1, font_pt));
    }
  }
  const float block_w =
      label_w + kPairGap + v0_w + (any_v1 ? kValGap + v1_w : 0.f);

  const float screen_w = g_overlay_layout.bar_w;
  if (screen_w <= 1.0f)
    return;
  const float x0 = onion::overlay::pack_origin(
      screen_w, block_w,
      onion::overlay::bar_align_from_value(g_settings.overlay_align));

  const float line_h = g_overlay_layout.bar_h;
  const bool bottom = onion::overlay::bar_edge_from_pos(g_settings.overlay_pos) ==
                      onion::overlay::BarEdge::Bottom;
  /* Bottom edge: grow upward so the last row sits on the screen edge. */
  const float y_base = bottom ? g_overlay_layout.bar_y -
                                    static_cast<float>(nrows - 1) * line_h
                              : g_overlay_layout.bar_y;
  const float margin_top = g_overlay_layout.label_margin_top;

  static const char *kAll[] = {
      "id_fps_label",        "id_fps_value",       "id_fps_sep",
      "id_cpu_label",        "id_cpu_temp_value",  "id_cpu_usage_value",
      "id_cpu_sep",          "id_gpu_label",       "id_gpu_temp_value",
      "id_gpu_usage_value",  "id_gpu_sep",         "id_ram_label",
      "id_ram_value",        "id_ram_sep",         "id_ip_label",
      "id_ip_value",         "id_ip_sep",          "id_fan_label",
      "id_fan_value",        "id_fan_sep",
  };
  for (const char *id : kAll)
    set_label_layout(id, kOff, margin_top, 0.0f, y_base);

  set_bg_panel_rect(x0 - kPad, y_base, block_w + 2.0f * kPad,
                    static_cast<float>(nrows) * line_h);

  const float v0_x = x0 + label_w + kPairGap;
  const float v1_x = v0_x + v0_w + kValGap;
  for (int r = 0; r < nrows; ++r) {
    const GroupSpec &gs = rows[r];
    const float y = y_base + static_cast<float>(r) * line_h;
    set_label_layout(gs.id_l, x0, margin_top, label_w, y);
    set_label_text(gs.id_l, gs.lab);
    set_label_layout(gs.id_v0, v0_x, margin_top, v0_w, y);
    set_label_text(gs.id_v0, gs.v0);
    if (gs.id_v1 && gs.v1 && gs.v1[0]) {
      set_label_layout(gs.id_v1, v1_x, margin_top, v1_w, y);
      set_label_text(gs.id_v1, gs.v1);
    }
  }
}

#endif /* ONIONHEN_FPS_OVERLAY_ONLY */

void discover_idle_thread_ids(unsigned int idle_tid[kCpuCores]) {
  int count = kMaxProcThreads;
  if (sceKernelGetCpuUsage(reinterpret_cast<Proc_Stats*>(&Stat_Data), &count) ||
      count <= 0)
    return;

  char thread_name[0x40];
  int core = 0;
  for (int i = 0; i < count; i++) {
    if (!sceKernelGetThreadName(Stat_Data[i].td_tid, thread_name) &&
        sscanf(thread_name, "SceIdleCpu%d", &core) == 1 && core >= 0 &&
        core < kCpuCores) {
      idle_tid[core] = Stat_Data[i].td_tid;
    }
  }
}

bool init_overlay_once(unsigned int idle_tid[kCpuCores]) {
  rootWidget = Get_Property<MonoObject*>(pui_img, "Sce.PlayStation.PUI.UI2", "Scene",
                                         Game, "RootWidget");
  if (!rootWidget)
    return false;

  float screen_w = 0.0f;
  float screen_h = 0.0f;
  if (!resolve_root_dimensions(rootWidget, &screen_w, &screen_h))
    return false;

  apply_overlay_layout(screen_w, screen_h);
  discover_idle_thread_ids(idle_tid);
  if (!g_settings.overlay_enabled)
    return true;

  /* Regular font: style=0, weight=400 */
  font = CreateUIFont(onion::overlay::effective_font_pt(
                          onion::overlay_font_pt(g_settings.overlay_font_size)),
                      0, 400);
  if (!font)
    return false;

  RemoveGameWidget(REMOVE_ALL_OVERLAYS);
  bool widgets_ready = true;
  if (g_settings.overlay_fps) {
    if (!CreateGameWidget(CREATE_FPS_OVERLAY))
      widgets_ready = false;
  }
  if (g_settings.overlay_cpu || g_settings.all_cpu_usage) {
    if (!CreateGameWidget(CREATE_CPU_OVERLAY))
      widgets_ready = false;
  }
#ifndef ONIONHEN_FPS_OVERLAY_ONLY
  /* The standalone build prints GPU temp in the CPU row; no GPU widgets. */
  if (g_settings.overlay_gpu) {
    if (!CreateGameWidget(CREATE_GPU_OVERLAY))
      widgets_ready = false;
  }
#endif
  if (g_settings.overlay_ram) {
    if (!CreateGameWidget(CREATE_RAM_OVERLAY))
      widgets_ready = false;
  }
  if (g_settings.overlay_ip) {
    if (!CreateGameWidget(CREATE_IP_OVERLAY))
      widgets_ready = false;
  }
  if (g_settings.overlay_fan) {
    if (!CreateGameWidget(CREATE_FAN_OVERLAY))
      widgets_ready = false;
  }
  if (!widgets_ready) {
    RemoveGameWidget(REMOVE_ALL_OVERLAYS);
    return false;
  }

  /* First paint: center placeholders on the full-width bar. */
  layout_bar_labels(
      g_settings.overlay_fps ? "--" : nullptr,
      (g_settings.overlay_cpu || g_settings.all_cpu_usage) ? "--C" : nullptr,
      (g_settings.overlay_cpu || g_settings.all_cpu_usage) ? "--%" : nullptr,
      g_settings.overlay_gpu ? "--C" : nullptr,
      g_settings.overlay_gpu ? "--%" : nullptr,
      g_settings.overlay_ram ? "---- MB" : nullptr,
      g_settings.overlay_ip ? "---.---.---.---" : nullptr,
      g_settings.overlay_fan ? "--%" : nullptr);
  return true;
}

/** Sample CPU into Usage[]; formats CPU_USAGE. Returns false if sampling skipped/failed. */
bool sample_cpu_usage(unsigned int idle_tid[kCpuCores], int& current_bank,
                      char* cpu_usage, size_t cpu_usage_sz) {
  if (!g_settings.overlay_cpu && !g_settings.all_cpu_usage)
    return false;

  /*
   * Dual-bank sample: first successful GetCpuUsage seeds the other bank; the
   * next one can compute a delta. Bound retries so a stuck/failing CPU probe
   * cannot block RAM/GPU/IP updates on the same render tick.
   */
  constexpr int kMaxAttempts = 4;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    gThread_Data[current_bank].Thread_Count = kMaxProcThreads;
    if (sceKernelGetCpuUsage(
            reinterpret_cast<Proc_Stats*>(&gThread_Data[current_bank].Threads),
            &gThread_Data[current_bank].Thread_Count))
      continue;

    Thread_Count = gThread_Data[current_bank].Thread_Count;
    sceKernelClockGettime(kClockIdRealtime, &gThread_Data[current_bank].current_time);
    current_bank = !current_bank;

    if (gThread_Data[current_bank].Thread_Count <= 0)
      continue;

    calc_usage(idle_tid, &gThread_Data[!current_bank], &gThread_Data[current_bank],
               Usage);

    if (g_settings.all_cpu_usage) {
      snprintf(cpu_usage, cpu_usage_sz,
               "%2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%% %2.0f%%",
               Usage[0], Usage[1], Usage[2], Usage[3], Usage[4], Usage[5], Usage[6],
               Usage[7]);
    } else {
      float avg = 0.f;
      for (int i = 0; i < kCpuCores; i++)
        avg += Usage[i];
      avg /= static_cast<float>(kCpuCores);
      snprintf(cpu_usage, cpu_usage_sz, "%.0f%%", avg);
    }
    return true;
  }
  snprintf(cpu_usage, cpu_usage_sz, "--%%");
  return false;
}

#ifdef ONIONHEN_FPS_OVERLAY_ONLY
/* False once the payload has exited (clean shutdown marker) or stopped
 * publishing (killed). The overlay then removes all its widgets so nothing
 * stays on screen; it comes back by itself if the payload is started again. */
bool fps_payload_alive() {
  static OnionFpsSample *map;
  static bool last = true; /* answer to reuse if the seqlock read races */
  if (map == nullptr || map == reinterpret_cast<OnionFpsSample *>(MAP_FAILED)) {
    map = onion_fps_map_ro();
    if (map == reinterpret_cast<OnionFpsSample *>(MAP_FAILED)) {
      map = nullptr;
      return last = false;
    }
  }
  OnionFpsSample local;
  if (onion_fps_seqlock_load(map, &local) != 0)
    return last;
  if (local.magic != ONION_FPS_MAGIC || local.pid == ONION_FPS_PID_SHUTDOWN)
    return last = false;
  const uint64_t now = onion_fps_realtime_ns();
  if (local.unix_ns == 0 || now < local.unix_ns ||
      now - local.unix_ns > ONION_FPS_HEARTBEAT_TIMEOUT_NS)
    return last = false;
  return last = true;
}
#endif

#ifdef ONIONHEN_FPS_OVERLAY_ONLY
/* Frame rate of the shell itself, measured from how often the per-frame
 * PUI.Application.Update hook runs. Used on the home screen, where no game is
 * running and the payload has no scanout sample to publish. */
float g_shell_fps = 0.f;

void tick_shell_fps() {
  static uint64_t window_start_ns = 0;
  static unsigned frames = 0;
  struct timespec ts {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const uint64_t now = static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
                       static_cast<uint64_t>(ts.tv_nsec);
  if (window_start_ns == 0) {
    window_start_ns = now;
    frames = 0;
    return;
  }
  ++frames;
  const uint64_t dt = now - window_start_ns;
  if (dt >= 500000000ull) { /* refresh twice a second */
    g_shell_fps = static_cast<float>(frames) * 1e9f / static_cast<float>(dt);
    window_start_ns = now;
    frames = 0;
  }
}
#endif

#ifdef ONIONHEN_FPS_OVERLAY_ONLY
/* True when the live payload is publishing but reports no running game. */
bool fps_raw_sample(OnionFpsSample *out) {
  static OnionFpsSample *map;
  if (map == nullptr || map == reinterpret_cast<OnionFpsSample *>(MAP_FAILED)) {
    map = onion_fps_map_ro();
    if (map == reinterpret_cast<OnionFpsSample *>(MAP_FAILED)) {
      map = nullptr;
      return false;
    }
  }
  if (onion_fps_seqlock_load(map, out) != 0)
    return false;
  return out->magic == ONION_FPS_MAGIC && out->pid != ONION_FPS_PID_SHUTDOWN;
}

bool fps_no_game_running() {
  OnionFpsSample local;
  return fps_raw_sample(&local) && local.title_id[0] == '\0';
}

/* "Name / TITLEID" while a game runs; empty (row hidden) on the home screen.
 * The row must not blink: a failed read of the shared sample keeps what is
 * shown, "no game" has to be seen on a few updates in a row before the row goes
 * away, and a name that was known for a title is reused if a later sample
 * arrives without it. */
void update_overlay_title() {
  static int empty_reads = 0;
  static char name_tid[17] = {};
  static char name_keep[65] = {};
  constexpr int kEmptyReadsToHide = 1;

  OnionFpsSample local;
  if (!fps_raw_sample(&local))
    return;
  if (local.title_id[0] == '\0') {
    if (++empty_reads >= kEmptyReadsToHide)
      g_overlay_title[0] = '\0';
    return;
  }
  empty_reads = 0;
  char tid[sizeof(local.title_id) + 1] = {};
  char name[sizeof(local.title_name) + 1] = {};
  std::memcpy(tid, local.title_id, sizeof(local.title_id));
  std::memcpy(name, local.title_name, sizeof(local.title_name));
  if (name[0]) {
    std::snprintf(name_tid, sizeof(name_tid), "%s", tid);
    std::snprintf(name_keep, sizeof(name_keep), "%s", name);
  } else if (std::strcmp(tid, name_tid) == 0) {
    std::snprintf(name, sizeof(name), "%s", name_keep);
  }
  if (name[0])
    std::snprintf(g_overlay_title, sizeof(g_overlay_title), "%s / %s", name, tid);
  else
    std::snprintf(g_overlay_title, sizeof(g_overlay_title), "%s", tid);
}
#endif

void format_fps_text(char *buf, size_t buf_sz) {
  if (!buf || buf_sz == 0)
    return;
  OnionFpsSample sample {};
  if (onion_fps_read(&sample) == 0 && sample.valid && sample.fps >= 1.f) {
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
    snprintf(buf, buf_sz, "%.2f", sample.fps);
#else
    if (sample.fps >= 100.f)
      snprintf(buf, buf_sz, "%.0f", sample.fps);
    else
      snprintf(buf, buf_sz, "%.1f", sample.fps);
#endif
  } else {
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
    /* onion_fps_read() fails for any invalid sample, so ask the raw sample
     * whether a game is running: no title id means home screen. */
    if (fps_no_game_running() && g_shell_fps >= 1.f) {
      snprintf(buf, buf_sz, "%.2f", g_shell_fps);
      return;
    }
#endif
    snprintf(buf, buf_sz, "--");
  }
}

#ifdef ONIONHEN_FPS_OVERLAY_ONLY
/* RAM/VRAM page-table stats. In games the system VM (kVmSystem) reports them,
 * but on the home screen it can report 0/0. Other VM ids are only probed once
 * the home screen has been stable for a few updates (never while a game is
 * starting or running, when VM state is changing under us), and the id that
 * worked is remembered so the others are not queried again. */
constexpr int kHomeStableUpdates = 2; /* 2 ticks = 1.5 s */
int g_home_stable_updates = 0;
int g_cached_vm[3] = {-1, -1, -1}; /* per page-table type; -1 = none yet */

/* A running game's GPU memory is not always accounted to the system VM (id 1):
 * VRAM then reads 0% or a few MB even in very demanding titles, while other
 * games read correctly. While a game runs, VRAM is therefore read from every
 * VM id in range and the one with the most pages in use is shown (the game's
 * own VM dwarfs the shell's). Scanning starts after the title has been up for
 * a few updates so a game that is still starting is left alone. */
constexpr int kGameScanUpdates = 2;  /* 2 ticks = 1.5 s */
constexpr int kVramScanMaxVm = 7;    /* ids 0..7 */
constexpr int kVmDebugMaxVm = 15;    /* debug dump covers 0..15 */
int g_game_updates = 0;

void note_home_state() {
  if (fps_no_game_running()) {
    if (g_home_stable_updates < kHomeStableUpdates)
      ++g_home_stable_updates;
    g_game_updates = 0;
  } else {
    g_home_stable_updates = 0;
    if (g_game_updates < kGameScanUpdates)
      ++g_game_updates;
  }
}

bool query_vm(int vm, int type, Memory &m) {
  int total = 0, free_pages = 0;
  if (get_page_table_stats(vm, type, &total, &free_pages) == -1 || total <= 0)
    return false;
  /* A running console always has pages in use. A VM that reports a total but
   * nothing used is one that is torn down or not populated yet (right after a
   * game quits), not a real reading, so it must not be shown as 0 MB. */
  if (free_pages < 0 || free_pages >= total)
    return false;
  m.Total = total;
  m.Free = free_pages;
  m.Used = total - free_pages;
  return true;
}

/* Reads `type` from every VM id in range and keeps the busiest one. */
bool query_busiest_vm(int type, Memory &m) {
  bool found = false;
  Memory best{};
  for (int vm = 0; vm <= kVramScanMaxVm; ++vm) {
    Memory cand{};
    if (!query_vm(vm, type, cand))
      continue;
    if (!found || cand.Used > best.Used) {
      best = cand;
      found = true;
    }
  }
  if (found)
    m = best;
  return found;
}

/* Opt-in diagnostics: when /data/onion_fps_vmdebug exists, every ~6 s while a
 * game runs the raw page-table numbers of VM ids 0..15 are appended to
 * /data/onion_fps_vmscan.log (capped), to see which id holds the game's VRAM. */
void dump_vm_debug() {
  static int ticks = 0;
  static int dumps = 0;
  if (dumps >= 200 || ++ticks < 8)
    return;
  ticks = 0;
  FILE *flag = std::fopen("/data/onion_fps_vmdebug", "r");
  if (!flag)
    return;
  std::fclose(flag);
  OnionFpsSample s{};
  char tid[sizeof(s.title_id) + 1] = {};
  if (fps_raw_sample(&s))
    std::memcpy(tid, s.title_id, sizeof(s.title_id));
  FILE *out = std::fopen("/data/onion_fps_vmscan.log", "a");
  if (!out)
    return;
  ++dumps;
  std::fprintf(out, "--- %s\n", tid[0] ? tid : "(no game)");
  for (int vm = 0; vm <= kVmDebugMaxVm; ++vm) {
    for (int type = kPageTableRam; type <= kPageTableVram; ++type) {
      int total = 0, free_pages = 0;
      const int rc = get_page_table_stats(vm, type, &total, &free_pages);
      std::fprintf(out, "vm%d type%d rc=%d total=%d free=%d used=%d\n", vm, type,
                   rc, total, free_pages, total - free_pages);
    }
  }
  std::fclose(out);
}

void get_mem_stats(int type, Memory &m) {
  const int slot = (type >= 0 && type < 3) ? type : 0;
  if (type == kPageTableVram && g_game_updates >= kGameScanUpdates &&
      !fps_no_game_running() && query_busiest_vm(type, m))
    return;
  if (query_vm(kVmSystem, type, m)) {
    if (g_home_stable_updates >= kHomeStableUpdates)
      g_cached_vm[slot] = kVmSystem;
    return;
  }
  /* The system VM reports 0/0 on the home screen, including the first seconds
   * after a game quits. Use the id that worked on the home screen before; it
   * is known good, so it is safe to query straight away. */
  if (g_cached_vm[slot] >= 0 && query_vm(g_cached_vm[slot], type, m))
    return;
  /* Other ids are only probed once the home screen has been stable for a few
   * updates, never while a game is starting or running. */
  if (g_home_stable_updates >= kHomeStableUpdates) {
    static const int kFallbackVms[] = {0, 2, 3};
    for (int vm : kFallbackVms) {
      if (query_vm(vm, type, m)) {
        g_cached_vm[slot] = vm;
        return;
      }
    }
    m.Used = m.Free = m.Total = 0;
    return;
  }
  /* Nothing usable yet (transition): keep showing the previous reading
   * instead of flashing 0 MB. */
}
#endif

void update_overlay_metrics(unsigned int idle_tid[kCpuCores], int& current_bank,
                            char *cpu_temp, size_t cpu_temp_sz, char *cpu_usage,
                            size_t cpu_usage_sz, char *gpu_temp, size_t gpu_temp_sz,
                            char *gpu_usage, size_t gpu_usage_sz, char *ram_str,
                            size_t ram_sz, char *ip_address, size_t ip_sz,
                            char *fan_str, size_t fan_sz) {
  if (!g_settings.overlay_enabled)
    return;

#ifdef ONIONHEN_FPS_OVERLAY_ONLY
  note_home_state();
  if (g_settings.overlay_ram && g_game_updates >= kGameScanUpdates)
    dump_vm_debug();
#endif
  sample_cpu_usage(idle_tid, current_bank, cpu_usage, cpu_usage_sz);

  if (g_settings.overlay_ram) {
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
    get_mem_stats(kPageTableRam, RAM);
    snprintf(ram_str, ram_sz, "%d%% %dMB",
             RAM.Total > 0 ? static_cast<int>((100LL * RAM.Used) / RAM.Total) : 0,
             static_cast<int>(RAM.Used));
#else
    Get_Page_Table_Stats(kVmSystem, kPageTableRam, &RAM.Used, &RAM.Free, &RAM.Total);
    snprintf(ram_str, ram_sz, "%u MB", RAM.Used);
#endif
  }

  if (g_settings.overlay_gpu) {
    int soc_temp = 0;
    sceKernelGetSocSensorTemperature(0, &soc_temp);
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
    snprintf(gpu_temp, gpu_temp_sz, "%d\xC2\xB0" "C", soc_temp); /* 35°C */
#else
    snprintf(gpu_temp, gpu_temp_sz, "%dC", soc_temp);
#endif
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
    if (g_settings.overlay_ram)
      get_mem_stats(kPageTableVram, VRAM);
#else
    Get_Page_Table_Stats(kVmSystem, kPageTableVram, &VRAM.Used, &VRAM.Free,
                         &VRAM.Total);
#endif
    VRAM.Percentage =
        (VRAM.Total > 0) ? ((float)VRAM.Used / (float)VRAM.Total) * 100.0f : 0.f;
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
    snprintf(gpu_usage, gpu_usage_sz, "%.0f%% %dMB", VRAM.Percentage,
             static_cast<int>(VRAM.Used));
#else
    snprintf(gpu_usage, gpu_usage_sz, "%.0f%%", VRAM.Percentage);
#endif
  }

  if (g_settings.overlay_ip) {
    /* Network-info query on the UI thread, once per second like the other
     * values. */
    static uint64_t next_ip_ns = 0;
    struct timespec ip_ts {};
    clock_gettime(CLOCK_MONOTONIC, &ip_ts);
    const uint64_t ip_now_ns = static_cast<uint64_t>(ip_ts.tv_sec) * 1000000000ull +
                               static_cast<uint64_t>(ip_ts.tv_nsec);
    if (ip_now_ns >= next_ip_ns) {
      (void)onion_net_get_ip_address(ip_address, ip_sz);
      next_ip_ns = ip_now_ns + 1000000000ull;
    }
  }

  if (g_settings.overlay_cpu || g_settings.all_cpu_usage) {
    int cpu_t = 0;
    sceKernelGetCpuTemperature(&cpu_t);
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
    snprintf(cpu_temp, cpu_temp_sz, "%d\xC2\xB0" "C", cpu_t); /* 43°C */
#else
    snprintf(cpu_temp, cpu_temp_sz, "%dC", cpu_t);
#endif
  }

  if (g_settings.overlay_fan) {
    uint16_t duty = 0;
    uint64_t chassis = 0;
    const int fan_rc =
        sceKernelGetCurrentFanDuty ? sceKernelGetCurrentFanDuty(&duty, &chassis)
                                   : -1;
    if (fan_rc == 0) {
      snprintf(fan_str, fan_sz, "%.0f%%",
               (double)duty * 100.0 / 1024.0);
    } else {
      snprintf(fan_str, fan_sz, "--%%");
    }
  }
}

} // namespace

void OnRender_Hook(MonoObject* instance) {
  if (!shellui_hooks_are_ready()) {
    if (OnRender_orig)
      OnRender_orig(instance);
    return;
  }

  /* UI thread: one NPXS40002 reload covers display TIDs and top-nav patch. */

  static bool inited = false;
  static unsigned int idle_thread_id[kCpuCores] = {};
  static int current_bank = 0;
  static uint64_t next_update_ns = 0;
  static int frames_until_init = kOverlayInitDelayFrames;
  static char gpu_temp[32] = {};
  static char gpu_usage[32] = {};
  static char cpu_temp[32] = {};
  static char cpu_usage[120] = {};
  static char ram_str[32] = {};
  static char ip_address[64] = {};
  static char fan_str[32] = "--%";
  static char fps_str[16] = "--";
  static uint64_t next_init_try_ns = 0;
  static uint64_t init_backoff_ns = kInitBackoffMinNs;
  static char last_layout_sig[640] = {};
  static uint64_t next_forced_layout_ns = 0;

#ifdef ONIONHEN_FPS_OVERLAY_ONLY
  /* Payload gone (exited or killed): take the overlay down and stay idle. */
  if (!fps_payload_alive()) {
    if (inited) {
      RemoveGameWidget(REMOVE_ALL_OVERLAYS);
      inited = false;
      frames_until_init = kOverlayInitDelayFrames;
      next_update_ns = 0;
      next_init_try_ns = 0;
      init_backoff_ns = kInitBackoffMinNs;
      last_layout_sig[0] = '\0';
    }
    OnRender_orig(instance);
    return;
  }
  tick_shell_fps();
#endif

  if (!inited) {
    if (frames_until_init > 0) {
      --frames_until_init;
    } else {
      /* A failed init (no root widget yet, widgets refused) does a lot of work:
       * thread scan, font and widget creation and removal. Frames only come
       * when something on screen moves (e.g. the cursor over a browser page),
       * so retrying on every frame stalled the UI thread until the system
       * raised an error. Back off between attempts instead. */
      struct timespec its {};
      clock_gettime(CLOCK_MONOTONIC, &its);
      const uint64_t init_now_ns =
          static_cast<uint64_t>(its.tv_sec) * 1000000000ull +
          static_cast<uint64_t>(its.tv_nsec);
      if (init_now_ns >= next_init_try_ns) {
        if (init_overlay_once(idle_thread_id)) {
          inited = true;
          init_backoff_ns = kInitBackoffMinNs;
          last_layout_sig[0] = '\0';
        } else {
          next_init_try_ns = init_now_ns + init_backoff_ns;
          init_backoff_ns = std::min(init_backoff_ns * 2, kInitBackoffMaxNs);
        }
      }
    }
    if (!inited) {
      OnRender_orig(instance);
      return;
    }
  }

  struct timespec metrics_ts {};
  clock_gettime(CLOCK_MONOTONIC, &metrics_ts);
  const uint64_t metrics_now_ns =
      static_cast<uint64_t>(metrics_ts.tv_sec) * 1000000000ull +
      static_cast<uint64_t>(metrics_ts.tv_nsec);
  /* One tick every second drives every value, FPS included, so the rows change
   * together. */
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
  /* Every value refreshes once a second, except that opening or quitting a game
   * (or its name arriving) refreshes everything at once. The shared sample is
   * checked every 100 ms for that. */
  static uint64_t next_state_probe_ns = 0;
  static char last_game[sizeof(OnionFpsSample::title_id) +
                        sizeof(OnionFpsSample::title_name) + 1] = {};
  bool game_changed = false;
  if (metrics_now_ns >= next_state_probe_ns) {
    next_state_probe_ns = metrics_now_ns + 100000000ull;
    OnionFpsSample probe_sample{};
    if (fps_raw_sample(&probe_sample)) {
      char cur[sizeof(last_game)] = {};
      std::memcpy(cur, probe_sample.title_id, sizeof(probe_sample.title_id));
      std::memcpy(cur + sizeof(probe_sample.title_id), probe_sample.title_name,
                  sizeof(probe_sample.title_name));
      if (std::memcmp(cur, last_game, sizeof(cur)) != 0) {
        std::memcpy(last_game, cur, sizeof(cur));
        game_changed = true;
      }
    }
  }
  const bool tick = game_changed || metrics_now_ns >= next_update_ns;
#else
  const bool tick = metrics_now_ns >= next_update_ns;
#endif
  if (tick) {
    update_overlay_metrics(idle_thread_id, current_bank, cpu_temp,
                           sizeof(cpu_temp), cpu_usage, sizeof(cpu_usage),
                           gpu_temp, sizeof(gpu_temp), gpu_usage,
                           sizeof(gpu_usage), ram_str, sizeof(ram_str),
                           ip_address, sizeof(ip_address), fan_str,
                           sizeof(fan_str));
    next_update_ns = metrics_now_ns + kOverlayUpdateIntervalNs;
  }

#ifdef ONIONHEN_FPS_OVERLAY_ONLY
  /* Everything refreshes together on the tick, except that a "--" is replaced
   * as soon as a value exists (polled every 100 ms) so a game that just started
   * does not show "--" until the next 1 s tick. */
  static uint64_t next_fps_probe_ns = 0;
  bool fps_arrived = false;
  if (!tick && g_settings.overlay_fps && fps_str[0] == '-' &&
      metrics_now_ns >= next_fps_probe_ns) {
    next_fps_probe_ns = metrics_now_ns + 100000000ull;
    char probe[sizeof(fps_str)] = {};
    format_fps_text(probe, sizeof(probe));
    fps_arrived = probe[0] != '-';
  }
  const bool layout_tick = tick || fps_arrived;
#else
  static int frames_until_fps = 0;
  const bool layout_tick = frames_until_fps <= 0;
  frames_until_fps = layout_tick ? kFpsUpdateIntervalFrames : frames_until_fps - 1;
#endif
  if (layout_tick) {
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
    update_overlay_title();
#endif
    if (g_settings.overlay_fps)
      format_fps_text(fps_str, sizeof(fps_str));
    else
      fps_str[0] = '\0';
#ifdef ONIONHEN_FPS_OVERLAY_ONLY
    /* Re-laying out the widgets is the costly part of a tick; do it only when a
     * shown text changed (and at least every 5 s as a safety refresh). */
    char sig[sizeof(last_layout_sig)];
    std::snprintf(sig, sizeof(sig), "%s|%s|%s|%s|%s|%s|%s", fps_str, cpu_temp,
                  gpu_temp, ram_str, ip_address, fan_str, g_overlay_title);
    if (std::strcmp(sig, last_layout_sig) == 0 &&
        metrics_now_ns < next_forced_layout_ns) {
      OnRender_orig(instance);
      return;
    }
    std::memcpy(last_layout_sig, sig, sizeof(sig));
    next_forced_layout_ns = metrics_now_ns + 5000000000ull;
#endif
    layout_bar_labels(
        g_settings.overlay_fps ? fps_str : nullptr,
        (g_settings.overlay_cpu || g_settings.all_cpu_usage) ? cpu_temp
                                                             : nullptr,
        (g_settings.overlay_cpu || g_settings.all_cpu_usage) ? cpu_usage
                                                             : nullptr,
        g_settings.overlay_gpu ? gpu_temp : nullptr,
        g_settings.overlay_gpu ? gpu_usage : nullptr,
        g_settings.overlay_ram ? ram_str : nullptr,
        g_settings.overlay_ip ? ip_address : nullptr,
        g_settings.overlay_fan ? fan_str : nullptr);
  }

  OnRender_orig(instance);

  /*
   * Legacy's own UpdateProgressPanel lifecycle runs inside OnRender_orig and
   * can restore its default Progress=1.0. Apply our value after that update so
   * the final value committed for this frame is the daemon state.
   */
}
