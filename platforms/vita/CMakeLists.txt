#------------------------------------------------------------------------------
# k2boy — PlayStation Vita build
#------------------------------------------------------------------------------
#
#   cmake -S platforms/vita -B build-vita && cmake --build build-vita
#       → build-vita/k2boy.vpk (title K2BY00001), game data at ux0:data/k2boy/data
#
# Or "make vita" from the top. Needs VitaSDK with $VITASDK set (or at
# ~/vitasdk). Compiles ../../src directly; the Vita surface is
# src/platform/platform_vita.cpp and src/gfx/gfx_gxm.cpp. See
# docs/plan/07-vita.md.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{VITASDK} AND EXISTS "$ENV{HOME}/vitasdk")
    set(ENV{VITASDK} "$ENV{HOME}/vitasdk")
endif()
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "VITASDK is not set — install VitaSDK and export VITASDK")
    endif()
endif()

project(k2boy_vita CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SRC ${ROOT}/src)
set(DATA "${ROOT}/data" CACHE PATH "retail install, for the LiveArea art")

set(K2_SOURCES
    ${SRC}/main.cpp
    ${SRC}/platform/vfs.cpp
    ${SRC}/platform/platform_vita.cpp
    ${SRC}/gfx/gfx_gxm.cpp
    ${SRC}/gfx/ttf.cpp
    ${SRC}/assets/k2a.cpp
    ${SRC}/assets/image.cpp
    ${SRC}/assets/lvl.cpp
    ${SRC}/assets/master.cpp
    ${SRC}/assets/act.cpp
    ${SRC}/assets/skl.cpp
    ${SRC}/assets/sfx.cpp
    ${SRC}/assets/nif.cpp
    ${SRC}/media/xf_compat.cpp
    ${SRC}/media/inflate.cpp
    ${SRC}/media/mov.cpp
    ${SRC}/media/svq1.cpp
    ${SRC}/media/qdm2.cpp
    ${SRC}/media/movie.cpp
    ${SRC}/ui/ui.cpp
    ${SRC}/ui/frontend.cpp
    ${SRC}/engine/module.cpp
    ${SRC}/engine/nirtti.cpp
    ${SRC}/engine/actor.cpp
    ${SRC}/engine/node.cpp
    ${SRC}/engine/config.cpp
    ${SRC}/engine/module_table.cpp
    ${SRC}/engine/collision.cpp
    ${SRC}/engine/trap.cpp
    ${SRC}/engine/script.cpp
    ${SRC}/engine/mixer.cpp
    ${SRC}/engine/fight.cpp
    ${SRC}/engine/jobs.cpp
)

add_executable(k2boy ${K2_SOURCES})
target_include_directories(k2boy PRIVATE ${SRC})
# -fsigned-char: the formats were written by x86 code, where char is signed.
target_compile_options(k2boy PRIVATE
    -O2 -fsigned-char -Wall -Wno-unused-parameter -Wno-unused-const-variable
    -Wno-missing-field-initializers -Wno-sign-compare)
# printf without C99 length modifiers: see platform_vita.cpp.
target_link_options(k2boy PRIVATE
    -Wl,--wrap=printf,--wrap=fprintf,--wrap=vprintf,--wrap=vfprintf,--wrap=snprintf,--wrap=vsnprintf)
target_link_libraries(k2boy
    SceGxm_stub
    SceDisplay_stub
    SceCtrl_stub
    SceTouch_stub
    SceMotion_stub
    SceAudio_stub
    ScePower_stub
    SceAppMgr_stub
    SceSysmodule_stub
    SceShaccCg_stub_weak          # resolved only if libshacccg.suprx is loaded
    pthread
    m
)

# LiveArea art, made from the retail install (tools/vita_sce_sys.py).
set(SYS ${CMAKE_CURRENT_BINARY_DIR}/sce_sys)
add_custom_command(
    OUTPUT ${SYS}/icon0.png ${SYS}/livearea/contents/bg.png ${SYS}/livearea/contents/startup.png
    COMMAND python3 ${ROOT}/tools/vita_sce_sys.py ${DATA} ${SYS}
    DEPENDS ${ROOT}/tools/vita_sce_sys.py
    COMMENT "LiveArea art from ${DATA}")
add_custom_target(k2boy_sce_sys DEPENDS ${SYS}/icon0.png)

# Compiled shaders, once there are some (see gfx_gxm.cpp): packaged as
# app0:shaders/*.gxp. Without them the game compiles its own on the device.
file(GLOB GXP ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.gxp)
set(GXP_FILES)
foreach(g ${GXP})
    get_filename_component(n ${g} NAME)
    list(APPEND GXP_FILES FILE ${g} shaders/${n})
endforeach()

vita_create_self(eboot.bin k2boy UNSAFE)
vita_create_vpk(k2boy.vpk K2BY00001 eboot.bin
    VERSION 01.00
    NAME "Prince of Persia 3D"
    FILE ${SYS}/icon0.png sce_sys/icon0.png
         ${SYS}/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
         ${SYS}/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
         ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
         ${GXP_FILES}
)
add_dependencies(k2boy.vpk-vpk k2boy_sce_sys)
