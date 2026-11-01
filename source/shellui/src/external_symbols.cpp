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

#include "../include/external_symbols.hpp"


/* ====================================== Global vars ======================================================*/
MonoDomain* Root_Domain = nullptr;

/* ====================================== Dynamic SystemService Symbols ===================================*/

/* ====================================== Dynamic Appmsg Symbols ===================================*/

/* ====================================== Dynamic libkernel_sys Symbols ===================================*/
int (*sceKernelGetCurrentFanDuty)(uint16_t* out_duty, uint64_t* out_chassis_info) = nullptr;
int (*sceKernelMprotect)(void* addr, size_t len, int prot) = nullptr;
//  ssize_t (*write_alt)(int fd, const void* buf, size_t count) = nullptr;
int (*sceKernelSendNotificationRequest)(int unk1, OrbisNotificationRequest* req, int size, int unk2) = nullptr;



/* ====================================== Dynamic Mono Symbols ===================================*/
uint64_t(*mono_compile_method)(MonoMethod* method) = nullptr;
MonoDomain* (*mono_get_root_domain)() = nullptr;
MonoClass* (*mono_class_from_name)(MonoImage* image, const char* name_space, const char* name) = nullptr;
MonoAssembly* (*mono_domain_assembly_open)(MonoDomain* domain, const char* name) = nullptr;
MonoImage* (*mono_assembly_get_image)(MonoAssembly* assembly) = nullptr;
MonoMethod* (*mono_property_get_get_method)(MonoProperty* prop) = nullptr;
MonoMethod* (*mono_property_get_set_method)(MonoProperty* prop) = nullptr;
MonoProperty* (*mono_class_get_property_from_name)(MonoClass* klass, const char* name) = nullptr; //
MonoObject* (*mono_runtime_invoke)(MonoMethod* method, void* obj, void** params, MonoObject** exc) = nullptr; //
MonoString* (*mono_string_new)(MonoDomain* domain, const char* str) = nullptr; //
MonoThread* (*mono_thread_attach)(MonoDomain* domain) = nullptr;
MonoMethod* (*mono_class_get_method_from_name)(MonoClass* klass, const char* name, int param_count) = nullptr;//
void (*mono_runtime_object_init)(MonoObject* obj) = nullptr;
MonoDomain* (*mono_domain_get)() = nullptr;
MonoObject* (*mono_object_new)(MonoDomain* domain, MonoClass* klass) = nullptr;
void* (*mono_object_unbox)(MonoObject* obj) = nullptr;

