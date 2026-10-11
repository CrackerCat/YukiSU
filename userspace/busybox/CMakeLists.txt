cmake_minimum_required(VERSION 3.20)
project(yukisu_busybox LANGUAGES C ASM)

set(BUSYBOX_ANDROID_ABI "${CMAKE_ANDROID_ARCH_ABI}")
set(BUSYBOX_ANDROID_API "${CMAKE_SYSTEM_VERSION}")
if(DEFINED ANDROID_ABI)
    set(BUSYBOX_ANDROID_ABI "${ANDROID_ABI}")
endif()
if(DEFINED ANDROID_PLATFORM_LEVEL)
    set(BUSYBOX_ANDROID_API "${ANDROID_PLATFORM_LEVEL}")
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android" OR
   NOT BUSYBOX_ANDROID_ABI STREQUAL "arm64-v8a" OR
   BUSYBOX_ANDROID_API LESS 31)
    message(FATAL_ERROR "YukiSU BusyBox requires Android arm64-v8a API 31+")
endif()

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(NDK_BUSYBOX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ksud/third_party/ndk-busybox")
set(BUSYBOX_EMBEDDED OFF CACHE BOOL "Build the standalone BusyBox entry point" FORCE)
add_compile_options(-Os -ffunction-sections -fdata-sections)
add_subdirectory("${NDK_BUSYBOX_DIR}" ndk-busybox)

add_executable(yukisu_busybox "${NDK_BUSYBOX_DIR}/applets/applets.c")
target_compile_options(yukisu_busybox PRIVATE -include "${NDK_BUSYBOX_DIR}/include/autoconf.h")
target_link_libraries(yukisu_busybox PRIVATE busybox)
target_link_options(yukisu_busybox PRIVATE -static -no-pie -Wl,--gc-sections -Wl,-s
    -Wl,--wrap=realpath -Wl,--wrap=rename -Wl,--wrap=renameat)
set_target_properties(yukisu_busybox PROPERTIES OUTPUT_NAME busybox)
