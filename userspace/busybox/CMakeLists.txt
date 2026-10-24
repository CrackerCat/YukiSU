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
# Match ndk-box-kitchen's Application.mk; full LTO resolves BusyBox's global pointer declarations.
add_compile_options(
    -Wall -Oz -fomit-frame-pointer -flto
    -fno-unwind-tables -fno-asynchronous-unwind-tables
    -fno-stack-protector -U_FORTIFY_SOURCE
    -ffunction-sections -fdata-sections)
add_subdirectory("${NDK_BUSYBOX_DIR}" ndk-busybox)
target_compile_options(busybox PRIVATE -Wno-error=format-security)

add_executable(yukisu_busybox "${NDK_BUSYBOX_DIR}/applets/applets.c")
target_compile_options(yukisu_busybox PRIVATE -w -Wno-error=format-security
    -include "${NDK_BUSYBOX_DIR}/include/autoconf.h")
target_link_libraries(yukisu_busybox PRIVATE busybox)
target_link_options(yukisu_busybox PRIVATE -flto -Wl,--icf=all
    -static -no-pie -Wl,--gc-sections -Wl,-s -Wl,-z,max-page-size=16384
    -Wl,--wrap=realpath -Wl,--wrap=rename -Wl,--wrap=renameat)
set_target_properties(yukisu_busybox PROPERTIES OUTPUT_NAME busybox)
