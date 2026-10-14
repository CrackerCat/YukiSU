cmake_minimum_required(VERSION 3.20)
project(yrcp_protocol_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(KSUD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(MAGISKBOOT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${KSUD_ROOT}/third_party/MagiskbootAlone" magiskboot EXCLUDE_FROM_ALL)
add_executable(yrcp_test_server
    test_server.cpp
    "${KSUD_ROOT}/src/boot/ramdisk_editor.cpp"
    "${KSUD_ROOT}/third_party/readelf_toyboxAlone/readelf_api.c")
target_include_directories(yrcp_test_server PRIVATE
    "${KSUD_ROOT}/src"
    "${KSUD_ROOT}/third_party/readelf_toyboxAlone/include")
target_link_libraries(yrcp_test_server PRIVATE magiskboot_core)
target_compile_options(yrcp_test_server PRIVATE -Wall -Wextra -Werror)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()
add_test(NAME yrcp_protocol COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_protocol.py" "$<TARGET_FILE:yrcp_test_server>")
