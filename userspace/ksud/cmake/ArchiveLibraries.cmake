include_guard(GLOBAL)

function(ksud_archive_libraries)
    set(archive_source_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party")
    foreach(source IN ITEMS libzip/CMakeLists.txt xz/CMakeLists.txt bzip2/bzlib.c)
        if(NOT EXISTS "${archive_source_dir}/${source}")
            message(FATAL_ERROR
                "Missing archive submodule source: ${source}. "
                "Run git submodule update --init --recursive.")
        endif()
    endforeach()
    set(ksud_xz_SOURCE_DIR "${archive_source_dir}/xz")
    set(ksud_bzip2_SOURCE_DIR "${archive_source_dir}/bzip2")

    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(XZ_TOOL_XZ OFF)
    set(XZ_TOOL_XZDEC OFF)
    set(XZ_TOOL_LZMADEC OFF)
    set(XZ_TOOL_LZMAINFO OFF)
    set(XZ_NLS OFF)
    set(XZ_DOC OFF)
    add_subdirectory("${ksud_xz_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/xz")
    target_include_directories(liblzma PUBLIC
        $<BUILD_INTERFACE:${ksud_xz_SOURCE_DIR}/src/liblzma/api>)

    add_library(ksud_bzip2 STATIC
        ${ksud_bzip2_SOURCE_DIR}/blocksort.c
        ${ksud_bzip2_SOURCE_DIR}/bzlib.c
        ${ksud_bzip2_SOURCE_DIR}/compress.c
        ${ksud_bzip2_SOURCE_DIR}/crctable.c
        ${ksud_bzip2_SOURCE_DIR}/decompress.c
        ${ksud_bzip2_SOURCE_DIR}/huffman.c
        ${ksud_bzip2_SOURCE_DIR}/randtable.c
    )
    target_include_directories(ksud_bzip2 PUBLIC ${ksud_bzip2_SOURCE_DIR})

    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/archive")
    set(ENABLE_COMMONCRYPTO OFF)
    set(ENABLE_GNUTLS OFF)
    set(ENABLE_OPENSSL OFF)
    set(ENABLE_WINDOWS_CRYPTO OFF)
    set(ENABLE_BZIP2 ON)
    set(ENABLE_LZMA ON)
    set(ENABLE_ZSTD OFF)
    set(ENABLE_FDOPEN ON)
    set(LIMIT_LZMA_WINDOW 134217728)
    set(BUILD_TOOLS OFF)
    set(BUILD_REGRESS OFF)
    set(BUILD_OSSFUZZ OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_DOC OFF)
    set(LIBZIP_DO_INSTALL OFF)
    add_subdirectory("${archive_source_dir}/libzip" "${CMAKE_CURRENT_BINARY_DIR}/third_party/libzip")
endfunction()

ksud_archive_libraries()
