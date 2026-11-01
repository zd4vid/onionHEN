/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Extracted from mono_utils.cpp for module locality.
 */

#include "hooked_funcs.hpp"
#include "external_symbols.hpp"
#include "defs.h"
#include "ipc.hpp" // shellui_log
#include <atomic>
#include <string>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace {

std::atomic<bool> g_home_reload_pending{false};

} // namespace

MonoImage * getDLLimage(const char* dll_file){
  
  std::string dll_path = "/system_ex/common_ex/lib/" + std::string(dll_file);
  MonoAssembly * Assembly = mono_domain_assembly_open(Root_Domain, dll_path.c_str());
  if (!Assembly) {
    LOG_ERROR("Failed to open assembly %s.", dll_path.c_str());
    return nullptr;
  }

  MonoImage * img = mono_assembly_get_image(Assembly);
  if (!img) {
    LOG_ERROR("Failed to get image %s.", dll_path.c_str());
    return nullptr;
  }
  return img;
}


MonoObject* New_Object(MonoClass* Klass)
{
    if (Klass == nullptr)
    {
        return nullptr;
    }

    return mono_object_new(Root_Domain, Klass);
}

MonoObject* CreateUIColor(float r, float g, float b, float a)
{
    MonoClass* uIColor = mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UIColor");

    // Allocates memory for our new instance of a class.
    MonoObject* uIColorInstance = New_Object(uIColor);
    MonoObject* realInstance = (MonoObject*)mono_object_unbox(uIColorInstance);

    Invoke<void>(pui_img, uIColor, realInstance, ".ctor", r, g, b, a);

    return realInstance;
}

MonoObject* CreateUIFont(int size, int style, int weight)
{
    MonoClass* uIFont = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "UIFont");

    // Allocates memory for our new instance of a class.
    MonoObject* uIFontInstance = New_Object(uIFont);
    MonoObject* realInstance = (MonoObject*)mono_object_unbox(uIFontInstance);

    Invoke<void>(pui_img, uIFont, realInstance, ".ctor", size, style, weight);

    return realInstance;
}

MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* font, int horzAlign, int vertAlign, float r, float g, float b, float a)
{
    MonoClass* labelClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");

    // Allocates memory for our new instance of a class.
    MonoObject* labelInstance = New_Object(labelClass);

    // Call Constructor.
    mono_runtime_object_init(labelInstance);

    Set_Property(labelClass, labelInstance, "Name", mono_string_new(Root_Domain, name));
    Set_Property(labelClass, labelInstance, "PositionType", 1);
    Set_Property(labelClass, labelInstance, "MarginLeft", x);
    Set_Property(labelClass, labelInstance, "MarginTop", y);
    Set_Property(labelClass, labelInstance, "Text", mono_string_new(Root_Domain, text));
    Set_Property_Invoke(labelClass, labelInstance, "Font", font);
    Set_Property(labelClass, labelInstance, "HorizontalAlignment", horzAlign);
    Set_Property(labelClass, labelInstance, "VerticalAlignment", vertAlign);
    Set_Property_Invoke(labelClass, labelInstance, "TextColor", CreateUIColor(r, g, b, a));

    Set_Property(labelClass, labelInstance, "FitWidthToText", false);
    Set_Property(labelClass, labelInstance, "FitHeightToText", true);
    Set_Property(labelClass, labelInstance, "NumberOfLines", 1);
    /* Readable HUD text on dark bar. */
    Set_Property(labelClass, labelInstance, "EnableThemedTextShadow", true);

    return labelInstance;
}

void Widget_Append_Child(MonoObject* widget, MonoObject* child)
{
    MonoClass* widgetClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
    MonoMethod* appendChild = mono_class_get_method_from_name(widgetClass, "AppendChild", 1);

    void* args[1];
    args[0] = child;

    mono_runtime_invoke(appendChild, widget, args, nullptr);
}

uint64_t Get_Address_of_Method(MonoImage *Assembly_Image, const char *Name_Space, const char *Class_Name, const char *Method_Name, int Param_Count)
{
  MonoClass *klass = mono_class_from_name(Assembly_Image, Name_Space, Class_Name);
  if (!klass)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Get_Address_of_Method: failed to open class \"%s\" in namespace \"%s\"", Class_Name, Name_Space);
#endif
    return 0;
  }

  MonoMethod *Method = mono_class_get_method_from_name(klass, Method_Name, Param_Count);
  if (!Method)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Get_Address_of_Method: failed to find method \"%s\" in class \"%s\"", Method_Name, Class_Name);
#endif
    return 0;
  }

  // return (uint64_t)mono_aot_get_method(Root_Domain, Method);
  return mono_compile_method(Method);
}

uint64_t Get_Address_of_Method(MonoImage* Assembly_Image, MonoClass* klass, const char* Method_Name, int Param_Count)
{
	if (!klass)
	{
		return 0;
	}

	MonoMethod* Method = mono_class_get_method_from_name(klass, Method_Name, Param_Count);

	if (!Method)
	{
		return 0;
	}

	//return (uint64_t)mono_aot_get_method(mono_get_root_domain(), Method);
  return mono_compile_method(Method);
}

MonoObject *Get_Instance(MonoClass *klass, const char *Instance)
{

  MonoProperty *inst_prop = mono_class_get_property_from_name(klass, Instance);
  if (!inst_prop)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Failed to find Instance property \"%s\" in Class \"%s\".", Instance, klass->name);
#endif
    return nullptr;
  }

  MonoMethod *inst_get_method = mono_property_get_get_method(inst_prop);
  if (!inst_get_method)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Failed to find get method for \"%s\" in Class \"%s\".", Instance, klass->name);
#endif
    return nullptr;
  }

  MonoObject *inst = mono_runtime_invoke(inst_get_method, 0, 0, 0);
  if (!inst)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("Failed to find get Instance \"%s\" in Class \"%s\".", Instance, klass->name);
#endif
    return nullptr;
  }

  return inst;
}

std::string Mono_to_String(MonoString *str)
{
  if (!str)
  {
    return "";
  }

  if (!mono_string_to_utf8)
    return "";

  const char *c_str = mono_string_to_utf8(str);
  if (!c_str)
    return "";

  std::string ret(c_str);
  if (mono_free)
    mono_free((void *)c_str);
  return ret;
}

bool SetVersionString(const char *str)
{
  MonoAssembly *Assembly = mono_domain_assembly_open(Root_Domain, uilib_dll.c_str());
  if (!Assembly)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("SetVersionString: Failed to open assembly.");
#endif
    return false;
  }
  MonoClass *SystemSoftwareVersionInfo = mono_class_from_name(mono_assembly_get_image(Assembly), uilib.c_str(), Sysinfo.c_str());
  if (!SystemSoftwareVersionInfo)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("SetVersionString: Failed to open class.");
#endif
    return false;
  }

  MonoObject *SystemSoftwareVersionInfo_Instance = Get_Instance(SystemSoftwareVersionInfo, "Instance");
  if (!SystemSoftwareVersionInfo_Instance)
  {
#if SHELL_DEBUG == 1
    LOG_ERROR("SetVersionString: Failed to open Instance.");
#endif
    return false;
  }

  MonoMethod *Set_Method = mono_class_get_method_from_name(SystemSoftwareVersionInfo, display_info.c_str(), 1);
  if (Set_Method == nullptr)
  {
#if SHELL_DEBUG == 1
    LOG_DEBUG("SetVersionString: Could not find set method.");
#endif
    return false;
  }

  MonoObject *exception = nullptr;
  void *args[] = {mono_string_new(Root_Domain, str)};
  //    MonoObject* result = mono_runtime_invoke(send_by_id_method, nullptr, args, &exception);
  mono_runtime_invoke(Set_Method, SystemSoftwareVersionInfo_Instance, args, &exception);
  if (exception)
  {
    MonoString *exc_string = mono_object_to_string(exception, nullptr);
    const char *exc_chars = mono_string_to_utf8(exc_string);
#if SHELL_DEBUG == 1
    LOG_DEBUG("Exception: %s", exc_chars);
#endif
    mono_free((void *)exc_chars);
    return false;
  }
  return true;
}

void ReloadRNPSApp(const char* title_id){
    void (*ReloadApp)(MonoString* tid) = (void(*)(MonoString*))Get_Address_of_Method(react_common_img, "ReactNative.Vsh.Common", "ReactApplicationSceneManager", "ReloadApp", 1);
    if (ReloadApp) {
        LOG_DEBUG("Reloading %s scenes", title_id);
        ReloadApp(mono_string_new(Root_Domain, title_id));
    } else {
        LOG_ERROR("Failed to find reload method, not reloading scene");
    }
}

void shellui_poll_home_reload(void) {
  if (!g_home_reload_pending.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  LOG_DEBUG("applying NPXS40002 home reload on UI thread");
  ReloadRNPSApp("NPXS40002");
}
