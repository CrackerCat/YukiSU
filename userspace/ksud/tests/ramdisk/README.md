# Host YRCP protocol tests

This target compiles the production ramdisk protocol implementation against the
same MagiskbootAlone and readelf submodules as ksud. Run on a POSIX host with CMake,
a C++17 compiler, Python 3 and liblzma development files:

    git submodule update --init --recursive
    cmake -S userspace/ksud/tests/ramdisk -B build/ramdisk-host -DCMAKE_BUILD_TYPE=Debug
    cmake --build build/ramdisk-host --parallel
    ctest --test-dir build/ramdisk-host --output-on-failure

MagiskbootAlone tracks LZ4 as a nested submodule. For offline builds, point
FETCHCONTENT_SOURCE_DIR_MBEDTLS at an existing compatible Mbed TLS checkout.

The Python client checks real request/response frames, mixed original/literal
edits, empty results, metadata and hard-link sharing, revision conflicts, legacy
writes, malformed/oversized requests, payload draining beyond pipe capacity,
truncated connections, and DUMP/reopen byte equality. Test images live in an
isolated temporary directory. These tests do not install anything on Android.
