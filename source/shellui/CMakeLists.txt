cmake_minimum_required (VERSION 3.20)

set(basename "shellui")
project(${basename} C CXX ASM)

onion_module_prologue(C_STANDARD 11 CXX_STANDARD 20)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()
set(D_SRC ${D_CWD}/src)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.elf")

# Finalize main target sources
target_compile_options(${PROJECT_NAME} PUBLIC
	$<$<COMPILE_LANGUAGE:C>:${C_DEFS} ${C_FLAGS}>
	$<$<COMPILE_LANGUAGE:CXX>:${CXX_DEFS} ${CXX_FLAGS}>
	$<$<COMPILE_LANGUAGE:ASM>:${ASM_FLAGS}>
)
# --icf=safe needs .llvm_addrsig in every input object; the SDK's crt1.o has
# none, so lld warns ("--icf=safe conservatively ignores SHT_LLVM_ADDRSIG").
# The standalone overlay build therefore links without ICF (set below); the
# full build keeps --icf=safe.
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--gc-sections")

# A/B: disable HomeUI top-nav Hermes patch without editing sources.
#   cmake -DONIONHEN_SHELLUI_HOMEUI_TOP_NAV_PATCH=OFF ...
option(ONIONHEN_SHELLUI_HOMEUI_TOP_NAV_PATCH
	"Patch HomeUI top-nav Fps slot into OnionHEN entry" ON)
if(NOT ONIONHEN_SHELLUI_HOMEUI_TOP_NAV_PATCH)
	target_compile_definitions(${PROJECT_NAME} PRIVATE SHELLUI_HOMEUI_TOP_NAV_PATCH=0)
	message(STATUS "shellui: HomeUI top-nav patch DISABLED")
endif()

# Standalone FPS overlay build (see standalone_fps/): the module only installs
# the per-frame overlay hook and skips toolbox/settings/plugin startup.
option(ONIONHEN_FPS_OVERLAY_ONLY "Build shellui as the standalone FPS overlay" OFF)
if(ONIONHEN_FPS_OVERLAY_ONLY)
	target_compile_definitions(${PROJECT_NAME} PRIVATE ONIONHEN_FPS_OVERLAY_ONLY=1)
	target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--icf=none")
	message(STATUS "shellui: FPS overlay-only mode")
else()
	target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--icf=safe")
endif()

set(SrcFiles
	${D_SRC}/prx.cpp
	${D_SRC}/prx_overlay.cpp
	${D_SRC}/shellui_overlay_widgets.cpp
	${D_SRC}/mono_runtime.cpp
	${D_SRC}/mono_utils.cpp
	${D_SRC}/settings_ui.cpp
	${D_SRC}/shellui_globals.cpp
	${D_SRC}/hook_lifecycle.cpp
	${D_SRC}/external_symbols.cpp
	${D_SRC}/hook_functions.cpp
	${D_SRC}/shellui_notify.cpp
)

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-nested-anon-types -Wno-gnu-anonymous-struct -Wno-zero-length-array -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -pedantic -pedantic-errors")
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -DNDEBUG")

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles})
add_dependencies(${PROJECT_NAME} onion_version onion_detour onion_proc onion_platform onion_ready)
onion_target_debug_define(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE
	"${D_CWD}/include"
	"${ONIONHEN_THIRD_PARTY_DIR}/cjson"
	"${ONIONHEN_THIRD_PARTY_DIR}/sha1"
	"${PROJECT_ROOT}/libonion_ipc/include"
	"${PROJECT_ROOT}/libonion_payload/include"
	"${PROJECT_ROOT}/libonion_settings/include"
	"${PROJECT_ROOT}/libonion_detour/include"
	"${PROJECT_ROOT}/libonion_proc/include"
	"${PROJECT_ROOT}/libonion_platform/include"
	"${PROJECT_ROOT}/libonion_fps/include"
	"${PROJECT_ROOT}/libonion_plugin_ui/include"
	"${PROJECT_ROOT}/libonion_plugin_bridge/include"
	"${PROJECT_ROOT}/include"
)
target_link_directories	(${PROJECT_NAME} PUBLIC "${ONIONHEN_PS5_STUB_DIR}")
target_link_libraries	(${PROJECT_NAME} PUBLIC onion_detour onion_proc onion_platform onion_ready SceRegMgr SceSystemService SceUserService SceNetCtl)

# Add post-build command
onion_strip_debug(${PROJECT_NAME})
