cmake_minimum_required(VERSION 3.16)
project(k2boy CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# OpenGL is deprecated on macOS but functional. Silence the deprecation spam
# rather than the warnings we actually care about.
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

set(K2_SOURCES
    src/main.cpp
    src/platform/vfs.cpp
    src/gfx/gfx_gl.cpp
    src/assets/k2a.cpp
    src/assets/image.cpp
    src/assets/lvl.cpp
    src/assets/master.cpp
    src/assets/act.cpp
    src/assets/skl.cpp
    src/assets/sfx.cpp
    src/engine/module.cpp
    src/engine/nirtti.cpp
    src/engine/actor.cpp
    src/engine/node.cpp
    src/engine/config.cpp
    src/engine/module_table.cpp
    src/engine/collision.cpp
    src/engine/trap.cpp
    src/engine/script.cpp
    src/engine/mixer.cpp
    src/engine/fight.cpp
)

if(APPLE)
    list(APPEND K2_SOURCES src/platform/platform_macos.mm)
else()
    message(FATAL_ERROR "Only macOS is implemented so far. See docs/plan/04-platform-layer.md.")
endif()

add_executable(k2boy ${K2_SOURCES})
target_include_directories(k2boy PRIVATE src)

if(APPLE)
    set_source_files_properties(src/platform/platform_macos.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(k2boy
        "-framework Cocoa"
        "-framework GameController"
        "-framework OpenGL"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudio"
    )
endif()
