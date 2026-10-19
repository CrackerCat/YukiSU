# 模块 preinit

模块可以在 **SELinux 策略加载之前、init 解析 init.rc 之前**执行一些操作，比如加载内核模块、运行静态程序、写 procfs/sysfs。把这些操作放在 `preinit/` 目录里，由 `preinit/preinit.conf` 描述：

```txt
/data/adb/modules/<MODID>/
├── preinit/
│   ├── preinit.conf
│   ├── kmod/foo.ko
│   └── bin/fixup
└── ...
```

ksud 会把每个已启用模块的整个 `preinit/` 目录复制到 `/metadata` 分区。下次开机时，YukiSU 内核模块里内嵌的 runner 会在 first stage init 执行 `/system/bin/init selinux_setup` 的时候运行这些操作。此时 `/metadata`、`/system`、`/vendor` 已经挂载，`/proc`、`/sys`、`/dev` 可用，但还没有任何策略，property service 和 ueventd 也都没有运行。模块按模块 ID 的字母顺序处理。

## preinit.conf

每行一个动作。`#` 开头的行和空行会被忽略。参数之间用空白分隔；用双引号保留参数中的空白，用 `\"`、`\\` 表示引号和反斜杠本身。**不做**任何变量展开。相对路径以模块的 preinit 目录为基准。单行最长 4 KB，整个文件最大 64 KB。

| 动作 | 语法 | 语义 |
|---|---|---|
| `insmod` | `insmod <ko> [params...]` | 加载内核模块。会先从内核 kallsyms 查询未定义符号并写入符号表，再用 `init_module` 加载，所以模块可以使用未导出的符号；不需要绑定符号的模块直接用 `finit_module` 加载。多个参数用空格拼接 |
| `exec` | `exec [-t <ms>] <prog> [args...]` | 运行程序并等待其退出，退出码非 0 视为失败。默认 5000 ms 后被杀掉 |
| `write` | `write <path> <content>` | 向已存在的文件写入 `content`，不追加换行，用于 procfs/sysfs |

动作名加 `?` 后缀表示失败可忽略。其他动作失败时会中止**当前模块**剩余的动作，后面的模块照常执行。

```txt
# preinit/preinit.conf
insmod   kmod/foo.ko  debug=1 name="a b"
insmod?  kmod/optional.ko
exec -t 10000  bin/fixup --flag
write    /proc/sys/kernel/printk_devkmsg on
```

`exec` 启动的程序以 root 运行，拥有完整的 capabilities，不受 SELinux 约束。工作目录是模块的 preinit 目录，stdin 接 `/dev/null`，stdout/stderr 按行转发到内核日志，带 `ksu_preinit[<MODID>]:` 前缀。环境变量只有 `KSU=true`、`KSU_PREINIT=1`、`MODID`、`PREINIT_DIR` 和 `PATH=/system/bin:/vendor/bin`。

::: warning 限制
- 没有 shell：只能使用**静态链接**的程序，因为 `/apex` 还没有挂载。
- `/dev` 下只有 first stage init 创建的节点，ueventd 没有运行。
- 固件加载没有用户态回退，固件只能由内核直接从 `/vendor/firmware` 等路径加载。
- 不能读写属性。
- 只有 first stage 的 vendor 模块已经加载，依赖 vendor_dlkm（second stage 才加载）中符号的 `.ko` 会加载失败。
- 不允许常驻：preinit 结束时所有后代进程都会被杀掉，包括自行创建会话的程序。整个 preinit 阶段最多 30 秒。
- 改动在**下次开机**生效，热安装不会执行 preinit。
- 目录和 `preinit.conf`（以及用到的 `.ko`、程序文件）的属主必须是 root，且 group/other 不可写。ksud 复制时会自动处理。
- ksud 复制时保留文件原有的权限位（去掉 group/other 写权限）。安装器会把模块文件设为 `0644`，所以 `exec` 要运行的程序需要在 `customize.sh` 里 `set_perm $MODPATH/preinit/bin/xxx 0 0 0755`。
:::

## /metadata 空间

`/metadata` 很小（通常只有 10 MB 左右），里面还放着加密密钥和 OTA 状态。因此 ksud 至少保留 8 MB 空闲空间，preinit 最多占用 4 MB。模块按 ID 字母顺序依次复制，放不下的模块会被整个跳过，安装时也会给出提示。可以用下面的命令查看结果：

```sh
ksud module preinit status
```

## 防砖

runner 执行任何动作前会把启动计数加 1，开机完成后由 ksud 清零。连续 3 次开机没有完成时，preinit 会被跳过，并保持禁用，直到某个模块的 preinit 内容发生变化，或者手动执行：

```sh
ksud module preinit reset
```

关机充电模式下不会执行 preinit；安全模式会禁用所有模块，下次开机时它们的 preinit 内容也会被移除。如果需要手动救砖，可以在 recovery 里删除 `/metadata/vold/ksu/preinit`（或 `/metadata/watchdog/ksu/preinit`、`/metadata/ksu/preinit`，以实际存在的为准），或者 `fastboot boot` 原厂镜像后删除。

::: tip
- 在 **late-load 模式**下以及不使用两阶段 init 的设备（Android 10 之前以 system-as-root 出厂的设备）上，preinit **不可用**。
- 当前内核不执行 preinit 时（late-load 模式、`preinit=0`，或内核编译时未包含 preinit），ksud 不会向 `/metadata` 拷贝任何内容，`ksud module preinit status` 会把这些模块标为 `unsupported`。
- 可以通过 YukiSU 内核模块参数 `preinit=0` 彻底关闭 preinit，`ksud boot-patch` / `boot-patch-v2` 的 `--no-preinit` 选项会设置它。
- 能写入 `/metadata` 就等于能在这么早的阶段执行代码，请只安装可信的模块。
:::
