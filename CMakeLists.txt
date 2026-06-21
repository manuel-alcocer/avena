cmake_minimum_required(VERSION 3.21)

project(avena
    VERSION 0.1.0
    DESCRIPTION "Visual node-based GStreamer transcoding pipeline editor"
    LANGUAGES CXX)

# --- Language standard -------------------------------------------------------
# Target C++23. Available on GCC 13+/Clang 16+ (Ubuntu 24.04, Debian 13, Arch).
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Qt6 ---------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)

qt_standard_project_setup()

# --- GStreamer ---------------------------------------------------------------
# Enables media probing (GstDiscoverer) now and the execution backend later.
# ON by default; turn OFF to build the editor with Qt only (media info falls
# back to file size).
option(AVENA_ENABLE_GSTREAMER "Build GStreamer features (media probe + backend)" ON)

if(AVENA_ENABLE_GSTREAMER)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GST REQUIRED IMPORTED_TARGET
        gstreamer-1.0
        gstreamer-pbutils-1.0)
endif()

# --- Sources -----------------------------------------------------------------
add_subdirectory(src)
