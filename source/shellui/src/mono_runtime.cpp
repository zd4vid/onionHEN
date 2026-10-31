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

/* Keep a few recent streams rooted because Settings can consume returned
 * streams after rapid resource re-entry. Old slots are released on reuse. */
static std::mutex g_xml_stream_gchandle_lock;
static constexpr size_t kXmlStreamHandleSlots = 4;
static uint32_t g_xml_stream_gchandles[kXmlStreamHandleSlots] = {};
static size_t g_xml_stream_gchandle_cursor = 0;

static uint32_t root_xml_stream(MonoObject *stream)
{
  if (!stream || !mono_gchandle_new)
    return 0;

  std::lock_guard<std::mutex> lock(g_xml_stream_gchandle_lock);
  uint32_t old_handle = g_xml_stream_gchandles[g_xml_stream_gchandle_cursor];
  if (old_handle != 0 && mono_gchandle_free)
    mono_gchandle_free(old_handle);

  uint32_t handle = mono_gchandle_new(stream, /*pinned=*/1);
  g_xml_stream_gchandles[g_xml_stream_gchandle_cursor] = handle;
  g_xml_stream_gchandle_cursor =
      (g_xml_stream_gchandle_cursor + 1) % kXmlStreamHandleSlots;
  return handle;
}

MonoObject *New_Mono_XML_From_String(std::string xml_doc, MonoDomain *domain)
{
  if (!domain)
    domain = (mono_domain_get ? mono_domain_get() : nullptr);
  if (!domain)
    domain = Root_Domain;

  LOG_DEBUG("[GMRS] New_Mono_XML_From_String: xml_size=%zu domain=%p Root_Domain=%p MemoryStream_IO=%p",
              xml_doc.size(), (void *)domain, (void *)Root_Domain,
              (void *)MemoryStream_IO);

  if (xml_doc.empty()) {
    LOG_DEBUG("[GMRS] New_Mono_XML_From_String: empty xml_doc");
    return nullptr;
  }
  if (!domain) {
    LOG_DEBUG("[GMRS] New_Mono_XML_From_String: domain is null");
    return nullptr;
  }
  if (!MemoryStream_IO) {
    LOG_DEBUG("[GMRS] New_Mono_XML_From_String: MemoryStream_IO is null");
    return nullptr;
  }

  MonoArray *Array = mono_array_new(domain, mono_get_byte_class(), xml_doc.size());
  if (!Array)
  {
    LOG_ERROR("[GMRS] New_Mono_XML_From_String: Failed to create byte[] array (size=%zu)", xml_doc.size());
    return nullptr;
  }

  char *Array_addr = mono_array_addr_with_size(Array, sizeof(char), 0);
  if (!Array_addr) {
    LOG_DEBUG("[GMRS] New_Mono_XML_From_String: mono_array_addr_with_size returned null");
    return nullptr;
  }
  /* Do NOT mprotect mono heap pages. Array_addr is usually mid-page; mprotect
   * rounds to page bounds and can change permissions on neighboring GC
   * objects — intermittent SIGSEGV when opening DebugSettings quickly or from
   * a notification (Hermes/RN still busy). Mono byte[] is already writable. */
  memcpy(Array_addr, xml_doc.data(), xml_doc.size());

  MonoObject *stream = mono_object_new(domain, MemoryStream_IO);
  if (!stream)
  {
    MemoryStream_IO = nullptr;
    LOG_ERROR("[GMRS] New_Mono_XML_From_String: Failed to create MemoryStream_Instance");
    return nullptr;
  }
  void *args[] = {Array};
  InvokeByDesc(MemoryStream_IO, ":.ctor(byte[])", stream, args);

  uint32_t gchandle = root_xml_stream(stream);

  LOG_DEBUG("[GMRS] New_Mono_XML_From_String: ok instance=%p gchandle=%u",
              (void *)stream, gchandle);
  return stream;
}

