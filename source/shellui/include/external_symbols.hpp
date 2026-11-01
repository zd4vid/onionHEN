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

#pragma once
#include <cstdint>
#include <sys/types.h>
#include "monodef.h"



typedef struct
{
    int32_t type;			 // 0x00
    int32_t req_id;			 // 0x04
    int32_t priority;		 // 0x08
    int32_t msg_id;			 // 0x0C
    int32_t target_id;		 // 0x10
    int32_t user_id;		 // 0x14
    int32_t unk1;			 // 0x18
    int32_t unk2;			 // 0x1C
    int32_t app_id;			 // 0x20
    int32_t error_num;		 // 0x24
    int32_t unk3;			 // 0x28
    char use_icon_image_uri; // 0x2C
    char message[1024];		 // 0x2D
    char uri[1024];			 // 0x42D
    char unkstr[1024];		 // 0x82D
} OrbisNotificationRequest;	 // Size = 0xC30

/* event type */

/* info code */
#define SCE_NET_CTL_INFO_IP_ADDRESS			14

/* device */

/* link */

/* wifi_security */

/* ip_config */

/* http_proxy_config */

/*
 * SceNetCtlInfo / SceNetEtherAddr / the SCE_NET_CTL_*_LEN constants and the
 * sceNetCtlGetInfo prototype now live in the shared ABI header. To read the
 * console address use onion_net_get_ip_address() from <onion/net.h>.
 */
#include <ps5/net_ctl.h>

// Token: 0x040002CA RID: 714

// Token: 0x040002CB RID: 715

// Token: 0x040002CC RID: 716

// Token: 0x02000064 RID: 100


/* ====================================== Global vars ======================================================*/
extern MonoDomain* Root_Domain;

/* ====================================== Dynamic SystemService Symbols ===================================*/

/* ====================================== Dynamic libkernel_sys Symbols ===================================*/

extern int (*sceKernelMprotect)(void* addr, size_t len, int prot);


extern int (*sceKernelSendNotificationRequest)(int unk1, OrbisNotificationRequest* req, int size, int unk2);



extern int (*sceKernelGetCurrentFanDuty)(uint16_t* out_duty, uint64_t* out_chassis_info);

/* ====================================== Dynamic Remote Play Symbols ===================================*/


/* ====================================== Dynamic Mono Symbols ===================================*/
/** Real Mono API returns gchandle id (uint32). */
extern  void* (*mono_object_unbox)(MonoObject* obj);
extern uint64_t(*mono_compile_method)(MonoMethod* method);
extern MonoDomain* (*mono_get_root_domain)();
extern MonoClass* (*mono_class_from_name)(MonoImage* image, const char* name_space, const char* name);
extern MonoAssembly* (*mono_domain_assembly_open)(MonoDomain* domain, const char* name);
extern MonoImage* (*mono_assembly_get_image)(MonoAssembly* assembly);
extern MonoMethod* (*mono_property_get_get_method)(MonoProperty* prop);
extern MonoMethod* (*mono_property_get_set_method)(MonoProperty* prop);
extern MonoProperty* (*mono_class_get_property_from_name)(MonoClass* klass, const char* name); //
extern MonoObject* (*mono_runtime_invoke)(MonoMethod* method, void* obj, void** params, MonoObject** exc); //
extern MonoString* (*mono_string_new)(MonoDomain* domain, const char* str); //
extern MonoThread* (*mono_thread_attach)(MonoDomain* domain);
extern MonoMethod* (*mono_class_get_method_from_name)(MonoClass* klass, const char* name, int param_count);//
extern void (*mono_runtime_object_init)(MonoObject* obj);
// extern void (*setenv)(const char*, const char*, int);
extern MonoDomain* (*mono_domain_get)();
extern MonoObject* (*mono_object_new)(MonoDomain* domain, MonoClass* klass);


