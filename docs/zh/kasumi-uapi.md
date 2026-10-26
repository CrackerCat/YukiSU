# Kasumi UAPI 18

[English](../kasumi-uapi.md)

API 18 冻结候选以 [`uapi/kasumi.h`](../../uapi/kasumi.h) 为唯一接口定义。
集成版 Kasumi、Kagami C++ 和 Manager JNI 直接包含该头文件。内核通过
`kernel/include/uapi` 软链接引用同一目录，不维护独立的协议副本或转发头。

## 调用与版本

通过现有 KernelSU 控制 fd 调用类型为 `'S'` 的 ioctl，仅授权管理器或 root
可调用。本集成仅支持原生 64 位客户端，compat 系统调用返回 `EOPNOTSUPP`；
冲突结构使用固定宽度字段不代表开放了 compat 支持。

`KSM_PROTOCOL_VERSION` 为 18。Kagami 的普通操作要求协议版本完全匹配；
Manager 从 JNI 获取由共享头定义的期望版本。API 17 的内核和用户空间需配套更新。
KernelSU supercall 的 UAPI 版本和命令定义保持原状。

只有下述两个冲突 ioctl 绕过 Kasumi 的 VFS 初始化检查。未激活或初始化被拒绝时，
它们仍然可用，即使普通 `GET_VERSION` 调用失败也可读取告警。查询不会激活 Kasumi。

## 冲突诊断

| 命令 | 编号 | ioctl 编码 | 结构大小 | 方向 |
| --- | --- | --- | --- | --- |
| `KSM_IOC_GET_CONFLICTS` | 46 | `0x8508532e` | `kasumi_conflicts_arg`，1288 字节 | 内核至用户 |
| `KSM_IOC_ACK_CONFLICTS` | 47 | `0x4008532f` | `kasumi_conflict_ack_arg`，8 字节 | 用户至内核 |

`kasumi_conflict` 大小为 80 字节，按 8 字节对齐：

| 字段 | 偏移 | 大小 | 含义 |
| --- | --- | --- | --- |
| `sequence` | 0 | 8 | 非零、单调递增的事件序号 |
| `kind` | 8 | 4 | 下表中的冲突类型 |
| `reserved` | 12 | 4 | 零 |
| `target` | 16 | 64 | 以 NUL 结尾的模块名或配置赋值 |

快照的 `count` 位于偏移 0，零值保留字段位于偏移 4，16 个事件槽位从偏移 8 开始。
仅前 `count` 项有效，其余槽位为零；事件不保证顺序。ACK 结构仅包含偏移 0 的
8 字节对齐序号。

| 类型 | 值 | 被拒绝的操作 |
| --- | --- | --- |
| `KSM_CONFLICT_LOADED_MODULE` | 1 | 存在冲突模块时激活 Kasumi |
| `KSM_CONFLICT_BUILTIN_CONFIG` | 2 | 检测到冲突内置配置后激活 Kasumi |
| `KSM_CONFLICT_MODULE_LOAD` | 3 | Kasumi VFS 防冲突保护生效时加载冲突模块 |

当前模块黑名单为 `kasumi_lkm`、`pathmask`、`procguard`、`nomount` 和
`susfs_guard_lkm`。内置检查读取运行内核的 IKCONFIG，识别 `CONFIG_KSU_SUSFS=y`
和 `CONFIG_NOMOUNT=y`。读取失败时沿用现有行为：记录日志并允许初始化。
因此，空告警快照不能证明运行内核不存在冲突功能。

事件保存在内核内存中，生命周期与承载它的 KernelSU 模块相同。管理器重启、
Kasumi 激活失败或普通功能状态变化不会清除它们；重启设备或替换宿主模块后不保留。
相同 `(kind, target)` 共用一个槽位，每次拒绝都会更新序号。队列满时，新的事件组合
替换最旧的记录。

GET 返回一致快照，不清除记录。用户手动确认已显示的告警后，使用快照中的最大序号
执行 ACK，仅清除当前序号不大于该值的记录。读取后新发生或再次发生的冲突会保留。
ACK 零成功且不做修改；尚未发出的未来序号返回 `EINVAL`，不修改队列；无效用户缓冲区
返回 `EFAULT`。确认作用于宿主模块的全局队列，不会启用 Kasumi、停用其他组件或重试
被拒绝的操作。

Manager 保留已显示的快照，直到用户确认且 ACK 成功。返回键和点击弹窗外部不能关闭
警告；确认失败保留警告并允许重试。未知类型显示通用冲突提示。

## 挂载隐藏与保留编号

挂载隐藏仅保留原普通模式。`KSM_IOC_SET_MOUNT_HIDE` 的命令编号 25 和结构保持不变；
命名空间链接伪装 hook、等级选择器和激进模式能力标志已删除。Kagami 接受
`mount-hide off|on|normal`，Manager 仅显示一个开关。

旧配置中的 `mount_hide_mode` 在读取时被忽略，`enable_mount_hide` 保持原值。
配置查询不再输出旧字段，下次成功保存配置时清除它。新写入该字段会被视为未知配置并拒绝。

命令 38 和能力位 12 保留，不得重新分配。命令 45 留给独立版实例监视接口，本集成不实现。
API 18 没有重新编号现存命令或能力位，也没有改变现存结构大小。扩展或改变已冻结的
结构必须采用新的 ioctl 编码并重新审查协议。
