# Module preinit

A module can run a few actions **before the SELinux policy is loaded and before init.rc is parsed**, for example to load a kernel module, run a static program or write procfs/sysfs. Put them in a `preinit/` directory described by `preinit/preinit.conf`:

```txt
/data/adb/modules/<MODID>/
├── preinit/
│   ├── preinit.conf
│   ├── kmod/foo.ko
│   └── bin/fixup
└── ...
```

ksud copies the whole `preinit/` directory of every enabled module to the `/metadata` partition. On the next boot a runner embedded in the YukiSU kernel module executes the actions when first stage init hands over to `/system/bin/init selinux_setup`. At this point `/metadata`, `/system` and `/vendor` are mounted and `/proc`, `/sys` and `/dev` are available, but there is no policy, no property service and no ueventd yet. Modules are processed in alphabetical order of module ID.

## preinit.conf

One action per line. Lines starting with `#` and empty lines are ignored. Arguments are separated by blanks; use double quotes to keep blanks in an argument and `\"` / `\\` for a literal quote or backslash. Variables are **not** expanded. Relative paths are relative to the module's preinit directory. Lines are at most 4 KB and the file at most 64 KB.

| Action | Syntax | Meaning |
|---|---|---|
| `insmod` | `insmod <ko> [params...]` | Load a kernel module. Undefined symbols are looked up in the kernel's kallsyms and bound before `init_module`, so the module may use non-exported symbols; a module that needs no such binding is loaded with `finit_module`. Parameters are joined with spaces |
| `exec` | `exec [-t <ms>] <prog> [args...]` | Run a program and wait for it. A non-zero exit status is a failure. It is killed after 5000 ms by default |
| `write` | `write <path> <content>` | Write `content` to an existing file, without a trailing newline. Meant for procfs/sysfs |

Append `?` to an action to ignore its failure. Any other failing action stops the rest of **this** module; the next module still runs.

```txt
# preinit/preinit.conf
insmod   kmod/foo.ko  debug=1 name="a b"
insmod?  kmod/optional.ko
exec -t 10000  bin/fixup --flag
write    /proc/sys/kernel/printk_devkmsg on
```

Programs started by `exec` run as root with full capabilities and no SELinux restriction. Their working directory is the module's preinit directory, stdin is `/dev/null` and stdout/stderr are forwarded to the kernel log line by line with a `ksu_preinit[<MODID>]:` prefix. The environment contains only `KSU=true`, `KSU_PREINIT=1`, `MODID`, `PREINIT_DIR` and `PATH=/system/bin:/vendor/bin`.

::: warning LIMITATIONS
- There is no shell: only **statically linked** programs work, `/apex` is not mounted yet.
- `/dev` only contains the nodes created by first stage init; ueventd is not running.
- The firmware loader has no userspace fallback, firmware must be loadable directly from `/vendor/firmware` and similar paths.
- Properties can neither be read nor set.
- Only the vendor modules of first stage are loaded. A `.ko` that depends on symbols from vendor_dlkm (loaded in second stage) fails to load.
- Nothing may keep running: all processes left over when preinit ends are killed. The whole preinit stage is limited to 30 seconds.
- Changes take effect on the **next boot**; a hot install does not run preinit.
- The directory and `preinit.conf` (and the `.ko` / program files used) must be owned by root and not writable by group or others. ksud takes care of this when copying.
- ksud keeps the permission bits of each file (minus group/other write). The installer sets module files to `0644`, so programs run by `exec` need `set_perm $MODPATH/preinit/bin/xxx 0 0 0755` in `customize.sh`.
:::

## Space on /metadata

`/metadata` is small (often around 10 MB) and also holds encryption keys and OTA state. ksud therefore keeps at least 8 MB free and uses at most 4 MB for preinit. Modules are copied in alphabetical order of module ID; a module that does not fit is skipped entirely, and the installer prints a warning. Check the result with:

```sh
ksud module preinit status
```

## Bootloop protection

Before running any action the runner increments a boot counter, which ksud clears when boot completes. After 3 boots in a row that did not complete, preinit is skipped and stays disabled until the preinit content of a module changes or you run:

```sh
ksud module preinit reset
```

preinit is also not run in charger mode, and safe mode disables all modules, which removes their preinit content for the next boot. To get out of a bootloop by hand, delete `/metadata/vold/ksu/preinit` (or `/metadata/watchdog/ksu/preinit`, `/metadata/ksu/preinit`, whichever exists) from recovery, or `fastboot boot` the stock image and delete it there.

::: tip
- preinit is **not available** in **late-load mode** and on devices that do not use a two stage init (devices launched with system-as-root before Android 10).
- When the running kernel does not run preinit (late-load mode, `preinit=0`, or a kernel built without it), ksud copies nothing to `/metadata` and `ksud module preinit status` reports the modules as `unsupported`.
- preinit can be disabled entirely with the `preinit=0` parameter of the YukiSU kernel module, which `ksud boot-patch` / `boot-patch-v2` set with `--no-preinit`.
- Anyone who can write to `/metadata` can run code this early. Only install modules you trust.
:::
