# 模块热安装

`KSU_HAS_HOT_INSTALL=true` 表示安装器支持热安装。

默认情况下，新安装或更新的模块会被放在 `/data/adb/modules_update` 中，重启后才会生效。如果你的模块不依赖只在开机时才会进行的操作，可以在 `customize.sh` 中请求 YukiSU 立即应用：

```sh
# customize.sh
export MODULE_HOT_INSTALL_REQUEST=true
# 可选：新版本生效后要执行的模块内脚本
export MODULE_HOT_RUN_SCRIPT=hotinstall.sh
```

这个请求在安装时判断，因此由模块自己决定是否需要重启，例如只在新版本没有改动 `system` 下的文件时才请求热安装。

安装脚本（包括 `customize.sh` 和元模块的 `metainstall.sh`）成功结束后，YukiSU 会：

1. 用新版本替换 `/data/adb/modules/<id>`。旧目录会被移走，下次开机时才删除，因此仍在使用旧版本的进程和挂载在此之前可以继续工作。
2. 加载模块的 `sepolicy.rule`。规则只会增加，新版本删掉的规则在重启前仍然有效。
3. 执行 `MODULE_HOT_RUN_SCRIPT`（相对于模块目录的路径），方式与启动脚本相同：运行在全局 mount 命名空间中，工作目录为脚本所在目录。脚本输出会显示在安装界面中，stdin 为 `/dev/null`，并忽略 `SIGPIPE`，因此安装程序退出后后台进程不会被杀掉。安装程序最多等待 30 秒，长期运行的服务请放到后台启动。脚本不存在时会跳过。
4. 不留下 `update` 标记，管理器不会提示重启。

即使 `sepolicy.rule` 加载失败、脚本执行失败或超时，模块仍视为热安装成功，请留意安装输出。

和普通安装一样，即使旧版本处于禁用状态，安装后模块也会被启用。

立即生效：模块文件本身、WebUI、`action.sh` 以及 `module.prop` 中的信息。

下次开机才生效：`system` 及其他分区目录下的文件（由元模块挂载）、`system.prop`、`initrc`、`preinit`，以及 `post-fs-data.sh`、`post-mount.sh`、`service.sh`、`boot-completed.sh` 这几个阶段。热安装时**不会**执行它们，但之后每次开机仍会照常执行。如果需要它们立即生效，请在 `MODULE_HOT_RUN_SCRIPT` 中处理，例如启动你的服务或调用 `resetprop`。

YukiSU 不会替你停止旧版本，更新时也不会执行 `uninstall.sh`。如果你的模块运行了守护进程，`MODULE_HOT_RUN_SCRIPT` 应先停止旧实例再启动新实例。

以下情况 YukiSU 会回退为重启后生效，并打印原因：

- 正在安装的是元模块。
- 设备处于安全模式。
- `MODULE_HOT_RUN_SCRIPT` 不是指向模块内文件的相对路径（指向模块外的符号链接会被拒绝）。
- 无法替换模块目录。

元模块：YukiSU 在 `metainstall.sh` 完成后处理热安装请求。如果元模块自行实现了热安装，应在 `KSU_HAS_HOT_INSTALL` 为 `true` 时跳过自己的实现。
