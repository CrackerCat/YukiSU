# Third-party Libraries

ksud builds its third-party dependencies from **git submodules** in this directory
(`userspace/ksud/third_party/`). Initialize them with
`git submodule update --init --recursive` before configuring CMake.

## Submodules (this directory)

### MagiskbootAlone

- **Path**: `userspace/ksud/third_party/MagiskbootAlone`
- **Purpose**: Boot image unpack/repack/split-dtb; compiled **into ksud** as a multi-call binary.
- **Usage**: `ksu/bin/ksud` is the main binary; `ksu/bin/magiskboot` should be a **symlink to ksud**. When invoked as `magiskboot`, ksud dispatches by `argv[0]` to `magiskboot_main()`.
- **Init/update**: `git submodule update --init userspace/ksud/third_party/MagiskbootAlone`

### bootctlAlone

- **Path**: `userspace/ksud/third_party/bootctlAlone`
- **Purpose**: Compiled **into ksud** as multi-call (like magiskboot). `ksu/bin/bootctl` → symlink to ksud; argv0 dispatch to `bootctl_main()`.
- **Init/update**: `git submodule update --init userspace/ksud/third_party/bootctlAlone`

### resetpropAlone

- **Path**: `userspace/ksud/third_party/resetpropAlone`
- **Purpose**: Compiled **into ksud** as multi-call. `ksu/bin/resetprop` → symlink to ksud; argv0 dispatch to `resetprop_main()`.
- **Init/update**: `git submodule update --init userspace/ksud/third_party/resetpropAlone`

### ndk-busybox

- **Path**: `userspace/ksud/third_party/ndk-busybox`
- **Purpose**: Built independently by `userspace/busybox`, packaged as the Manager's `libbusybox.so`, and installed by the Manager at `ksu/bin/busybox` for module scripts. ksud neither builds nor embeds BusyBox.
- **Init/update**: `git submodule update --init userspace/ksud/third_party/ndk-busybox`

### readelf_toyboxAlone

- **Path**: `userspace/ksud/third_party/readelf_toyboxAlone`
- **Purpose**: Android 16 Toybox `readelf` compiled **into ksud** as a namespaced multi-call applet, plus a reentrant structured ELF-header API shared by ksud and the CLI. `ksud readelf` works without a ROM-provided executable.
- **License**: 0BSD
- **Init/update**: `git submodule update --init userspace/ksud/third_party/readelf_toyboxAlone`

### ZIP and XZ libraries

- **libzip 1.12**: `libzip/`, BSD-3-Clause, https://github.com/Rouyashiki/libzip
- **liblzma 5.8.4**: `xz/`, 0BSD, https://github.com/Rouyashiki/xz
- **libbzip2 1.0.8**: `bzip2/`, bzip2 BSD-style license, https://github.com/Rouyashiki/bzip2
- **Purpose**: Shared ZIP reader for modules, plugins and AK3; native XZ resource decoding.
- **Build**: Local submodule sources compiled by `../cmake/ArchiveLibraries.cmake`.
- **Init/update**: `git submodule update --init --recursive`
- **Audit**: Exact revisions and license inputs are recorded in `manager/licenses/native-inputs.json` at the repository root.

### Lua

- **License**: MIT (see `lua.LICENSE`)
- **Path**: `lua/`
- **Source**: https://github.com/Rouyashiki/lua
- **Version**: 5.5.1
- **Purpose**: Embedded runtime and native-module ABI for ksud plugins
- **Build**: Sources are copied to the build directory, where `luaconf.h` is adapted
  to preserve the C ABI while compiling Lua as C++. The submodule stays unchanged.

### Mbed TLS

- **License**: Apache-2.0 OR GPL-2.0-or-later
- **Path**: `mbedtls/`
- **Source**: https://github.com/Rouyashiki/mbedtls
- **Version**: 3.6.2
- **Purpose**: Cryptographic hashes, X.509 and RSA/ECDSA support
- **Nested dependency**: `mbedtls/framework` uses
  https://github.com/Rouyashiki/mbedtls-framework at the revision pinned by Mbed TLS.

### LZ4 fallback

LZ4 and xxHash use the BSD-2-Clause sources vendored in MagiskbootAlone.
CMake falls back to the local `lz4/lib` submodule (v1.10.0,
https://github.com/Rouyashiki/lz4) if that copy is absent.

### Management

Submodules are declared in the root `.gitmodules`, with Mbed TLS's nested
framework declared in its own `.gitmodules`. The ksud CMake build does not
download dependency sources. MagiskbootAlone's separate standalone build has
its own dependency configuration; ksud compiles its source files directly.

When updating a Git tag or submodule, also review the app's
[open-source inventory](../../../../manager/licenses/README.md), including
the component's dependencies, copyright notices and full license texts.
