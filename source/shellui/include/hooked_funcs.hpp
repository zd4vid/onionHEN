/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * ShellUI API surface: types + Mono hooks.
 * Prefer including shellui_types.hpp alone when hooks are not needed.
 * Do NOT include this from ipc.hpp (breaks compile seam).
 */
#pragma once
#include "shellui_types.hpp"
#include "hook_lifecycle.hpp"
#include <cstdint>

enum GamePadButtons
	{
        None = 0,
		// Token: 0x040000D0 RID: 208
		Left = 1U,
		// Token: 0x040000D1 RID: 209
		Up = 2U,
		// Token: 0x040000D2 RID: 210
		Right = 4U,
		// Token: 0x040000D3 RID: 211
		Down = 8U,
		// Token: 0x040000D4 RID: 212
		Square = 16U,
		// Token: 0x040000D5 RID: 213
		Triangle = 32U,
		// Token: 0x040000D6 RID: 214
		Circle = 64U,
		// Token: 0x040000D7 RID: 215
		Cross = 128U,
		// Token: 0x040000D8 RID: 216
		Start = 256U,
		// Token: 0x040000D9 RID: 217
		Select = 512U,
		// Token: 0x040000DA RID: 218
		Option = 256U,
		// Token: 0x040000DB RID: 219
		L1 = 1024U,
		// Token: 0x040000DC RID: 220
		R1 = 2048U,
		// Token: 0x040000DD RID: 221
		L2 = 4096U,
		// Token: 0x040000DE RID: 222
		R2 = 8192U,
		// Token: 0x040000DF RID: 223
		L3 = 16384U,
		// Token: 0x040000E0 RID: 224
		R3 = 32768U,
		// Token: 0x040000E1 RID: 225
		Enter = 65536U,
		// Token: 0x040000E2 RID: 226
		Back = 131072U,
		// Token: 0x040000E3 RID: 227
		TouchPad = 262144U,
		// Token: 0x040000E4 RID: 228
		Move = 524288U,
		// Token: 0x040000E5 RID: 229
		Intercepted = 2147483648U
    };

struct ioctl_C0105203_args
{
  void* buffer;
  int size;
  int error;
};

void __syscall();

/* Minimal DIR surface used by payload scanners (not full BSD dirent). */
typedef struct _dirdesc {
    int dd_fd;
    long dd_loc;
    long dd_size;
    char *dd_buf;
    int dd_len;
    long dd_seek;
    long dd_rewind;
    int dd_flags;
    struct pthread_mutex *dd_lock;
    struct _telldir *dd_td;
} DIR;

extern "C" DIR * opendir(const char*);
extern "C" struct dirent* readdir(DIR*);
extern "C" int closedir(DIR*);

void notify(const char* text, ...);

extern void (*OnRender_orig)(MonoObject* instance);
extern void (*Orig_ReloadApp)(MonoString* str);
extern MonoImage * react_common_img;

/* =============================== mono utils =============================================================================*/
std::string Mono_to_String(MonoString* str);
std::string base64_decode(const std::string &encoded_string);
void ReloadRNPSApp(const char* title_id);

void Patch_Main_thread_Check(MonoImage * image_core);
uint64_t Get_Address_of_Method(MonoImage* Assembly_Image, const char* Name_Space, const char* Class_Name, const char* Method_Name, int Param_Count);
uint64_t Get_Address_of_Method(MonoImage* Assembly_Image, MonoClass* klass, const char* Method_Name, int Param_Count);
/** Recompute g_overlay_layout from overlay edge and align. */
void apply_overlay_layout();
/** Apply the overlay layout using the live ShellUI logical canvas dimensions. */
void apply_overlay_layout(float screen_w, float screen_h);
/** Read both logical canvas dimensions from a ShellUI RootWidget. */
bool resolve_root_dimensions(MonoObject *root, float *screen_w, float *screen_h);

/** Drain the one-shot Home reload; call only from UI thread after hooks ready. */
void shellui_poll_home_reload(void);

bool SetVersionString(const char* str);

template <typename result>
result Get_Property(MonoClass* Klass, MonoObject* Instance, const char* Property_Name)
{
    if (Klass == 0)
    {
        return (result)0;
    }

    MonoProperty* Prop = mono_class_get_property_from_name(Klass, Property_Name);

    if (Prop == 0)
    {
        return (result)0;
    }

    MonoMethod* Get_Method = mono_property_get_get_method(Prop);

    if (Get_Method == 0)
    {
        return (result)0;
    }

    uint64_t Get_Method_Thunk = (uint64_t)mono_compile_method(Get_Method);

    if (Get_Method_Thunk == 0)
    {
        return (result)0;
    }

    if (Instance != 0)
    {
        result(*Method)(MonoObject* Instance) = decltype(Method)(Get_Method_Thunk);
        return Method(Instance);
    }
    else
    {
        result(*Method)() = decltype(Method)(Get_Method_Thunk);
        return Method();
    }
}

template <typename result>
result Get_Property(MonoImage* Assembly_Image, const char* Namespace, const char* Class_Name, MonoObject* Instance, const char* Property_Name)
{
    return Get_Property<result>(mono_class_from_name(Assembly_Image, Namespace, Class_Name), Instance, Property_Name);
}

template <typename Param>
void Set_Property(MonoClass* Klass, MonoObject* Instance, const char* Property_Name, Param Value)
{
    if (Klass == nullptr)
    {
        return;
    }

    if (Instance == nullptr)
    {
        return;
    }

    MonoProperty* Prop = mono_class_get_property_from_name(Klass, Property_Name);

    if (Prop == nullptr)
    {
        return;
    }

    MonoMethod* Set_Method = mono_property_get_set_method(Prop);

    if (Set_Method == nullptr)
    {
        return;
    }

    uint64_t Set_Method_Thunk = (uint64_t)mono_compile_method(Set_Method);

    if (Set_Method_Thunk == 0)
    {
        return;
    }

    void(*Method)(MonoObject* Instance, Param Value) = decltype(Method)(Set_Method_Thunk);
    Method(Instance, Value);
}

template <typename Param>
void Set_Property_Invoke(MonoClass* Klass, MonoObject* Instance, const char* Property_Name, Param Value)
{
    if (Klass == nullptr)
    {
        return;
    }

    if (Instance == nullptr)
    {
        return;
    }

    MonoProperty* Prop = mono_class_get_property_from_name(Klass, Property_Name);

    if (Prop == nullptr)
    {
        return;
    }

    MonoMethod* Set_Method = mono_property_get_set_method(Prop);

    if (Set_Method == nullptr)
    {
        return;
    }

    mono_runtime_invoke(Set_Method, Instance, (void**)&Value, 0);
}

#define ARRAY_COUNT(arry) sizeof(arry) / sizeof(arry[0])

template <typename result, typename... Args>
result Invoke(MonoImage* Assembly_Image, MonoClass* klass, MonoObject* Instance, const char* Method_Name, Args... args)
{
    void* Argsv[] = { &args... };
    uint64_t ThunkAddress = Get_Address_of_Method(Assembly_Image, klass, Method_Name, ARRAY_COUNT(Argsv));

    if (!ThunkAddress)
    {
        return (result)0;
    }

    if (Instance)
    {
        result(*Method)(MonoObject* Instance, Args... args) = decltype(Method)(ThunkAddress);
        return Method(Instance, args...);
    }
    else //Static Call.
    {
        result(*Method)(Args... args) = decltype(Method)(ThunkAddress);
        return Method(args...);
    }
}


extern MonoString* (*CxmlUri)(MonoObject* obj,MonoString* uri);

extern MonoString *(*oGetString)(MonoObject *Instance, MonoString *str);

extern int (*__sys_regmgr_call)(long, long, int*, int*, long);

/* ================================= HOOKED MONO FUNCS ============================================= */
#include "shellui_state.hpp"

extern  std::string UI3_dec;
extern  std::string legacy_dec;
extern  std::string appsystem_dll;
extern  std::string uilib;
extern  std::string Sysinfo;
extern  std::string display_info;
extern  std::string uilib_dll;

extern MonoImage* pui_img;
extern MonoImage* AppSystem_img;
extern MonoObject* Game;



MonoObject* CreateUIColor(float r, float g, float b, float a);
MonoObject* CreateUIFont(int size, int style, int weight);
MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* font, int horzAlign, int vertAlign, float r, float g, float b, float a);
void Widget_Append_Child(MonoObject* widget, MonoObject* child);
MonoObject* New_Object(MonoClass* Klass);
MonoString *GetString_Hook(MonoObject *Instance, MonoString *str);
MonoImage * getDLLimage(const char* dll_file);
MonoString* CxmlUri_Hook(MonoObject* obj, MonoString* uri);
bool Get_Running_App_TID(std::string& title_id, int& BigAppid);
#include <onion/proc_query.h>
#include <onion/platform.h>
int ioctl_hook (int fd, unsigned long request, void *argp);
int LaunchApp(MonoString* titleId, uint64_t* args, int argsSize, LaunchAppParam *param);
/* ================================= HOOKED MONO FUNCS ============================================= */
