# Kasumi UAPI 18

[简体中文](zh/kasumi-uapi.md)

The API 18 freeze candidate is defined by [`uapi/kasumi.h`](../uapi/kasumi.h).
Integrated Kasumi, Kagami C++, and Manager JNI include this header directly.
Kernel include paths resolve it through `kernel/include/uapi`, a symlink to the
same directory. There is no separate Kagami or kernel copy of the definitions.

## Transport and versioning

Use the existing KernelSU control fd with ioctl type `'S'`. The caller must be
an authorized manager or root. Compat syscalls return `EOPNOTSUPP`; this
integration supports native 64-bit clients. The fixed-width conflict records
do not change that transport restriction.

`KSM_PROTOCOL_VERSION` is 18. Kagami requires an exact protocol match for normal
operations. Manager obtains its expected version from JNI, which reads the
shared header. API 17 kernel/userspace combinations must be updated together.
The KernelSU supercall UAPI version and command definitions are unchanged.

Only the two conflict ioctls below bypass Kasumi's VFS initialization gate. They
remain available before activation and after initialization is refused, even
when the ordinary `GET_VERSION` ioctl fails. Reading them does not enable Kasumi.

## Conflict diagnostics

| Command | Number | Encoded ioctl | Payload | Direction |
| --- | --- | --- | --- | --- |
| `KSM_IOC_GET_CONFLICTS` | 46 | `0x8508532e` | `kasumi_conflicts_arg`, 1288 bytes | kernel to user |
| `KSM_IOC_ACK_CONFLICTS` | 47 | `0x4008532f` | `kasumi_conflict_ack_arg`, 8 bytes | user to kernel |

`kasumi_conflict` is 80 bytes, aligned to 8 bytes:

| Field | Offset | Size | Meaning |
| --- | --- | --- | --- |
| `sequence` | 0 | 8 | Nonzero monotonically increasing event sequence |
| `kind` | 8 | 4 | Conflict kind below |
| `reserved` | 12 | 4 | Zero |
| `target` | 16 | 64 | NUL-terminated module name or configuration assignment |

The snapshot contains `count` at offset 0, a zero reserved word at offset 4,
and 16 event slots at offset 8. Only the first `count` entries are valid;
unused slots are zero. Entries have no ordering guarantee. The ACK payload
contains only an 8-byte aligned sequence at offset 0.

| Kind | Value | Refused operation |
| --- | --- | --- |
| `KSM_CONFLICT_LOADED_MODULE` | 1 | Kasumi activation while a conflicting module is present |
| `KSM_CONFLICT_BUILTIN_CONFIG` | 2 | Kasumi activation after detecting a conflicting built-in configuration |
| `KSM_CONFLICT_MODULE_LOAD` | 3 | Conflicting module load while the Kasumi VFS guard is active |

The blacklist currently contains `kasumi_lkm`, `pathmask`, `procguard`, `nomount`,
and `susfs_guard_lkm`. Built-in checks recognize `CONFIG_KSU_SUSFS=y` and
`CONFIG_NOMOUNT=y` in the running kernel's IKCONFIG. If that configuration cannot
be read, the existing guard logs the failure and permits initialization; an
empty conflict snapshot is not proof that the running kernel contains no
conflicting functionality.

Events live in kernel memory for the lifetime of the hosting KernelSU module.
They survive Manager restarts, failed Kasumi activation, and ordinary feature
state changes. They do not persist across a reboot or host module replacement.
Repeated `(kind, target)` pairs occupy one slot and receive a new sequence for
each refused attempt. At capacity, a new pair replaces the oldest entry.

GET returns a coherent snapshot without clearing it. After the user confirms
the displayed warning, ACK its maximum sequence. ACK clears events whose
current sequence is at most that value. A repeated or new conflict occurring
after the snapshot therefore survives an older ACK. Zero is a successful no-op;
a future sequence fails with `EINVAL` without changing the queue. Invalid user
buffers fail with `EFAULT`. Acknowledgement is global to the host module and
does not enable Kasumi, disable another component, or retry a refused operation.

Manager keeps the displayed snapshot until explicit confirmation succeeds.
Back and outside clicks cannot dismiss it. An acknowledgement failure leaves
the warning available for retry. Unknown kinds receive a generic warning.

## Mount hiding and reserved slots

Mount hiding now has one behavior, the former normal mode. Its existing
`KSM_IOC_SET_MOUNT_HIDE` command 25 and payload are unchanged. The namespace-link
projection hook, mode selector, and aggressive capability have been removed.
Kagami accepts `mount-hide off|on|normal`; Manager exposes one switch.

Existing configuration files may contain `mount_hide_mode`. Loading ignores
that field while preserving `enable_mount_hide`. Configuration output omits it,
and the next successful configuration save removes it. New writes to the
removed key are rejected as unknown.

Command 38 and feature bit 12 remain reserved and must not be reassigned.
Command 45 remains reserved for the standalone instance-watch interface and
is not implemented by this integration. No retained command, feature bit, or
pre-existing structure has been renumbered or resized for API 18. Extending or
changing a frozen payload requires a new ioctl encoding and protocol review.
