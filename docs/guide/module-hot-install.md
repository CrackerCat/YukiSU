# Hot installing modules

`KSU_HAS_HOT_INSTALL=true` indicates support for hot installation.

By default, a newly installed or updated module is staged in `/data/adb/modules_update` and only takes effect after a reboot. If your module does not depend on anything that only happens at boot, `customize.sh` can ask YukiSU to apply it right away:

```sh
# customize.sh
export MODULE_HOT_INSTALL_REQUEST=true
# Optional: a script in your module to run once the new version is live
export MODULE_HOT_RUN_SCRIPT=hotinstall.sh
```

The request is evaluated at install time, so the module decides whether a reboot is needed, for example by only requesting a hot install when the new version does not change its `system` files.

Once the installer (including `customize.sh` and a metamodule's `metainstall.sh`) finishes successfully, YukiSU:

1. Replaces `/data/adb/modules/<id>` with the new version. The old directory is moved aside and only deleted on the next boot, so processes and mounts still using the old version keep working until then.
2. Loads the module's `sepolicy.rule`. Rules are only ever added, so rules dropped by the new version stay active until the next reboot.
3. Runs `MODULE_HOT_RUN_SCRIPT`, a path relative to the module directory, the same way as boot scripts: in the global mount namespace, with the script's directory as the working directory. Its output is shown in the installer, stdin is `/dev/null` and `SIGPIPE` is ignored, so background processes are not killed when the installer goes away. The installer waits up to 30 seconds for it; start long-running services in the background. If the script does not exist it is skipped.
4. Leaves no `update` flag behind, so the manager does not ask for a reboot.

The module stays hot installed even if loading `sepolicy.rule` fails or the script fails or times out; check the installer output.

Like any install, this enables the module even if the previous version was disabled.

Effective immediately: the module files themselves, WebUI, `action.sh` and the information in `module.prop`.

Only applied on the next boot: files under `system` and the other partition directories (mounted by the metamodule), `system.prop`, `initrc`, `preinit`, and the `post-fs-data.sh`, `post-mount.sh`, `service.sh` and `boot-completed.sh` stages. They are **not** run during a hot install, but keep running on every boot as usual. If you need their effect now, do it in `MODULE_HOT_RUN_SCRIPT`, e.g. by starting your service or calling `resetprop`.

The old version is not stopped for you, and `uninstall.sh` is not run on updates. If your module runs a daemon, `MODULE_HOT_RUN_SCRIPT` should stop the old instance before starting the new one.

YukiSU falls back to applying the module on the next reboot, and prints the reason, when:

- The module being installed is a metamodule.
- The device is in safe mode.
- `MODULE_HOT_RUN_SCRIPT` is not a relative path to a file inside the module (symlinks pointing outside the module are rejected).
- The module directory cannot be replaced.

Metamodules: YukiSU processes the request after `metainstall.sh` completes. If a metamodule has its own hot install implementation, skip it when `KSU_HAS_HOT_INSTALL` is `true`.
