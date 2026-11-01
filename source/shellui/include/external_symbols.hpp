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

#define SCE_OK 0


typedef struct
{
    unsigned int size;
    uint32_t userId;
} SceShellUIUtilLaunchByUriParam;

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
#define SCE_NET_CTL_STATE_DISCONNECTED		0
#define SCE_NET_CTL_STATE_CONNECTING		1
#define SCE_NET_CTL_STATE_IPOBTAINING		2
#define SCE_NET_CTL_STATE_IPOBTAINED		3

/* event type */
#define SCE_NET_CTL_EVENT_TYPE_DISCONNECTED				1
#define SCE_NET_CTL_EVENT_TYPE_DISCONNECT_REQ_FINISHED	2
#define SCE_NET_CTL_EVENT_TYPE_IPOBTAINED				3

/* info code */
#define SCE_NET_CTL_INFO_DEVICE				1
#define SCE_NET_CTL_INFO_ETHER_ADDR			2
#define SCE_NET_CTL_INFO_MTU				3
#define SCE_NET_CTL_INFO_LINK				4
#define SCE_NET_CTL_INFO_BSSID				5
#define SCE_NET_CTL_INFO_SSID				6
#define SCE_NET_CTL_INFO_WIFI_SECURITY		7
#define SCE_NET_CTL_INFO_RSSI_DBM			8
#define SCE_NET_CTL_INFO_RSSI_PERCENTAGE	9
#define SCE_NET_CTL_INFO_CHANNEL			10
#define SCE_NET_CTL_INFO_IP_CONFIG			11
#define SCE_NET_CTL_INFO_DHCP_HOSTNAME		12
#define SCE_NET_CTL_INFO_PPPOE_AUTH_NAME	13
#define SCE_NET_CTL_INFO_IP_ADDRESS			14
#define SCE_NET_CTL_INFO_NETMASK			15
#define SCE_NET_CTL_INFO_DEFAULT_ROUTE		16
#define SCE_NET_CTL_INFO_PRIMARY_DNS		17
#define SCE_NET_CTL_INFO_SECONDARY_DNS		18
#define SCE_NET_CTL_INFO_HTTP_PROXY_CONFIG	19
#define SCE_NET_CTL_INFO_HTTP_PROXY_SERVER	20
#define SCE_NET_CTL_INFO_HTTP_PROXY_PORT	21
#define SCE_NET_CTL_INFO_RESERVED1			22
#define SCE_NET_CTL_INFO_RESERVED2			23

/* device */
#define SCE_NET_CTL_DEVICE_WIRED		0
#define SCE_NET_CTL_DEVICE_WIRELESS	 	1

/* link */
#define SCE_NET_CTL_LINK_DISCONNECTED	0
#define SCE_NET_CTL_LINK_CONNECTED		1

/* wifi_security */
#define SCE_NET_CTL_WIFI_SECURITY_NOAUTH			0
#define SCE_NET_CTL_WIFI_SECURITY_WEP				1
#define SCE_NET_CTL_WIFI_SECURITY_WPAPSK_WPA2PSK	2
#define SCE_NET_CTL_WIFI_SECURITY_WPAPSK_TKIP		3
#define SCE_NET_CTL_WIFI_SECURITY_WPAPSK_AES		4
#define SCE_NET_CTL_WIFI_SECURITY_WPA2PSK_TKIP		5
#define SCE_NET_CTL_WIFI_SECURITY_WPA2PSK_AES		6
#define SCE_NET_CTL_WIFI_SECURITY_UNSUPPORTED		7

/* ip_config */
#define SCE_NET_CTL_IP_DHCP	 	0
#define SCE_NET_CTL_IP_STATIC	1
#define SCE_NET_CTL_IP_PPPOE	2

/* http_proxy_config */
#define SCE_NET_CTL_HTTP_PROXY_OFF	0
#define SCE_NET_CTL_HTTP_PROXY_ON	1

/*
 * SceNetCtlInfo / SceNetEtherAddr / the SCE_NET_CTL_*_LEN constants and the
 * sceNetCtlGetInfo prototype now live in the shared ABI header. To read the
 * console address use onion_net_get_ip_address() from <onion/net.h>.
 */
#include <ps5/net_ctl.h>

// Token: 0x040002CA RID: 714
#define SCE_NET_SHOW_BUFFER_SIZE 8192

// Token: 0x040002CB RID: 715
#define SCE_NET_SHOW_NETSTAT_BUFFER_SIZE 65536

// Token: 0x040002CC RID: 716
#define SCE_NET_ADDR_STR_SIZE 18

// Token: 0x02000064 RID: 100

typedef struct
{
    uint64_t pad0;
    char version_str[0x1C];
    uint32_t version;
    uint64_t pad1;
} OrbisKernelSwVersion;


typedef struct app_info {
  uint32_t app_id;
  uint64_t unknown1;
  uint32_t app_type;
  char     title_id[10];
  char     unknown2[0x3c];
} app_info_t;

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


