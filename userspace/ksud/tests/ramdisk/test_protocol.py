import pathlib
import struct
import subprocess
import sys
import tempfile

SERVER = sys.argv[1]
U64 = lambda value: struct.pack("<Q", value)
U32 = lambda value: struct.pack("<I", value)

def string(value):
    data = value.encode()
    return U32(len(data)) + data

def empty_cpio():
    name = b"TRAILER!!!\0"
    fields = [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(name), 0]
    data = b"070701" + b"".join(f"{field:08x}".encode() for field in fields) + name
    return data + b"\0" * (-len(data) % 4)

class Session:
    def __init__(self, path):
        self.process = subprocess.Popen([SERVER, str(path)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.sequence = 0

    def request(self, opcode, payload=b"", status=0):
        self.sequence += 1
        self.process.stdin.write(struct.pack("<4sHHIQ", b"YRCP", 3, opcode, self.sequence, len(payload)) + payload)
        self.process.stdin.flush()
        header = self.process.stdout.read(20)
        assert len(header) == 20, (opcode, header, self.process.poll())
        magic, version, op, sequence, size = struct.unpack("<4sHHIQ", header)
        assert (magic, version, op, sequence) == (b"YRCP", 3, opcode | 0x8000, self.sequence)
        result = self.process.stdout.read(size)
        assert len(result) == size
        actual, = struct.unpack("<I", result[:4])
        assert actual == status, (opcode, actual, status)
        return result[4:]

    def info(self, node):
        return struct.unpack("<QQ", self.request(18, U64(node)))

    def read(self, node, revision, size):
        return self.request(19, U64(node) + U64(revision) + U64(0) + U64(size))

    def close(self):
        self.request(15)
        self.process.stdin.close()
        assert self.process.wait(timeout=5) == 0

def original(offset, size):
    return b"\0" + U64(offset) + U64(size)

def inserted(data):
    return b"\1" + U64(len(data)) + data

def plan(node, revision, old_size, new_size, pieces):
    return U64(node) + U64(revision) + U64(old_size) + U64(new_size) + U32(len(pieces)) + b"".join(pieces)

with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / "archive.cpio"
    path.write_bytes(empty_cpio())
    session = Session(path)
    hello = session.request(1)
    root = struct.unpack("<Q", hello[4:12])[0]
    capabilities = struct.unpack("<I", hello[28:32])[0]
    assert capabilities & (3 << 16) == (3 << 16)
    initial = b"0123456789"
    node, = struct.unpack("<Q", session.request(6, U64(root) + U32(0o640) + U32(123) + U32(456) + string("data") + initial))
    alias, = struct.unpack("<Q", session.request(9, U64(root) + U64(node) + string("alias")))
    revision, size = session.info(node)
    assert size == len(initial)
    session.request(18, U64(root), status=1)
    session.request(19, U64(root) + U64(revision) + U64(0) + U64(1), status=1)
    expected = b"01ABC789"
    result = session.request(20, plan(node, revision, size, len(expected),
        [original(0, 2), inserted(b"ABC"), original(7, 3)]))
    revision2, size2 = struct.unpack("<QQ", result)
    assert revision2 > revision and size2 == len(expected)
    assert session.read(node, revision2, size2) == expected
    assert session.read(alias, revision2, size2) == expected
    session.request(19, U64(node) + U64(revision) + U64(0) + U64(1), status=7)
    session.request(20, plan(node, revision, size, 1, [inserted(b"x")]), status=7)
    # A rejected payload larger than a pipe buffer must be fully drained.
    session.request(20, plan(node, revision, size, 262144, [inserted(b"x" * 262144)]), status=7)
    assert session.info(node) == (revision2, size2)
    session.request(20, U64(node) + U64(revision2) + U64(size2) + U64(0) + U32(262145), status=5)
    invalid_plans = [
        plan(node, revision2, size2, 1, [original(size2, 1)]),
        plan(node, revision2, size2, 1, [inserted(b"xy")]),
        plan(node, revision2, size2, 2, [inserted(b"x")]),
        plan(node, revision2, size2, 0, []) + b"trailing",
        plan(node, revision2, size2, 1, [b"\2" + U64(1) + b"x"]),
        plan(node, revision2, size2, 0, [inserted(b"")]),
        plan(node, revision2, size2, 1, [original(2**64-1, 1)]),
    ]
    for payload in invalid_plans:
        session.request(20, payload, status=1)
        assert session.info(node) == (revision2, size2)
        assert session.read(alias, revision2, size2) == expected
    session.request(20, plan(node, revision2, size2 + 1, 0, []), status=7)
    session.request(20, plan(node, revision2, size2, 512 * 1024 * 1024 + 1, []), status=5)
    session.request(13, U64(node) + U32(1) + U32(0o600))
    revision3, _ = session.info(node)
    assert revision3 > revision2
    session.request(19, U64(alias) + U64(revision2) + U64(0) + U64(1), status=7)
    session.request(5, U64(alias) + b"legacy")
    revision4, size4 = session.info(node)
    assert revision4 > revision3 and session.read(node, revision4, size4) == b"legacy"
    revision5, zero = struct.unpack("<QQ", session.request(20, plan(node, revision4, size4, 0, [])))
    assert zero == 0 and session.read(alias, revision5, 0) == b""
    final = b"\x00\xffsaved\x00"
    session.request(20, plan(node, revision5, 0, len(final), [inserted(final)]))
    session.request(14)
    session.close()

    session = Session(path)
    hello = session.request(1)
    root = struct.unpack("<Q", hello[4:12])[0]
    listing = session.request(3, U64(root))
    count, = struct.unpack("<I", listing[:4])
    assert count == 2
    data = memoryview(listing)[4:]
    nodes = {}
    for _ in range(count):
        node_id, = struct.unpack("<Q", data[:8])
        mode, uid, gid = struct.unpack("<III", data[28:40])
        data = data[66:]
        names = []
        for _ in range(3):
            length, = struct.unpack("<I", data[:4])
            data = data[4:]
            if length == 0xffffffff:
                names.append(None)
            else:
                names.append(bytes(data[:length]).decode())
                data = data[length:]
        nodes[names[0]] = node_id
        if names[0] == "data":
            assert mode & 0o777 == 0o600 and uid == 123 and gid == 456
    for node in nodes.values():
        revision, size = session.info(node)
        assert session.read(node, revision, size) == final
    session.close()

    saved = path.read_bytes()
    session = Session(path)
    node = nodes["data"]
    revision, size = session.info(node)
    payload = plan(node, revision, size, 6, [inserted(b"123456")])
    session.process.stdin.write(struct.pack("<4sHHIQ", b"YRCP", 3, 20, 99, len(payload)) + payload[:-2])
    session.process.stdin.close()
    assert session.process.wait(timeout=5) != 0
    assert path.read_bytes() == saved
print("YRCP atomic editing, revisions, hard links, malformed frames and dump round-trip passed")
