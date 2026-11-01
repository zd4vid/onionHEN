/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * ShellUI API surface: types + Mono hooks.
 * Prefer including shellui_types.hpp alone when hooks are not needed.
 */
#pragma once
#include "shellui_types.hpp"
#include "hook_lifecycle.hpp"
#include <cstdint>
#include <onion/log.h>



void notify(const char* text, ...);

extern void (*OnRender_orig)(MonoObject* instance);

/* =============================== mono utils =============================================================================*/
std::string base64_decode(const std::string &encoded_string);

void Patch_Main_thread_Check(MonoImage * image_core);
uint64_t Get_Address_of_Method(MonoImage* Assembly_Image, const char* Name_Space, const char* Class_Name, const char* Method_Name, int Param_Count);
uint64_t Get_Address_of_Method(MonoImage* Assembly_Image, MonoClass* klass, const char* Method_Name, int Param_Count);
/** Recompute g_overlay_layout from overlay edge and align. */
void apply_overlay_layout();
/** Apply the overlay layout using the live ShellUI logical canvas dimensions. */
void apply_overlay_layout(float screen_w, float screen_h);
/** Read both logical canvas dimensions from a ShellUI RootWidget. */
bool resolve_root_dimensions(MonoObject *root, float *screen_w, float *screen_h);

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



/* ================================= HOOKED MONO FUNCS ============================================= */

extern  std::string UI3_dec;
extern  std::string legacy_dec;
extern  std::string appsystem_dll;

extern MonoImage* pui_img;
extern MonoImage* AppSystem_img;
extern MonoObject* Game;



MonoObject* CreateUIColor(float r, float g, float b, float a);
MonoObject* CreateUIFont(int size, int style, int weight);
MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* font, int horzAlign, int vertAlign, float r, float g, float b, float a);
void Widget_Append_Child(MonoObject* widget, MonoObject* child);
MonoObject* New_Object(MonoClass* Klass);
MonoImage * getDLLimage(const char* dll_file);
#include <onion/platform.h>
/* ================================= HOOKED MONO FUNCS ============================================= */
