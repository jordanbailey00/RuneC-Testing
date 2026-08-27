#!/usr/bin/env python3
"""Upgrade reviewed object behaviors with explicit B237 pair metadata."""
from __future__ import annotations

import struct
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ODEF = ROOT / "data/defs/object_defs.bin"
SNAPSHOT = ROOT / "content/runtime_snapshots/data/defs/object_behaviors.bin"
OUT = ROOT / "data/defs/object_behaviors.bin"
REPORT = ROOT / "tools/reports/object_behaviors.txt"

OBHV_MAGIC = 0x5648424F
OBHV_VERSION = 2
ROW_FMT = "<IIiiiiBBH"

B_DOOR = 1 << 0
B_LADDER = 1 << 1
B_STAIR = 1 << 2
B_BANK = 1 << 3
B_ALTAR = 1 << 4
B_RESOURCE = 1 << 5
B_TRANSPORT = 1 << 6
B_STORAGE = 1 << 7
B_PAIR_LEFT = 1 << 8
B_PAIR_RIGHT = 1 << 9
B_PAIR_WIDE = 1 << 10

FLAG_NAMES = (
    (B_DOOR, "door"),
    (B_LADDER, "ladder"),
    (B_STAIR, "stair"),
    (B_BANK, "bank"),
    (B_ALTAR, "altar"),
    (B_RESOURCE, "resource"),
    (B_TRANSPORT, "transport"),
    (B_STORAGE, "storage"),
    (B_PAIR_LEFT, "pair_left"),
    (B_PAIR_RIGHT, "pair_right"),
    (B_PAIR_WIDE, "pair_wide"),
)


def read_object_names() -> dict[int, str]:
    data = ODEF.read_bytes()
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != 0x4645444F or version not in (1, 2):
        raise SystemExit(f"{ODEF}: unsupported object definition format")
    pos = 12
    row_fmt = "<IHHBBBBBiiiII"
    row_size = struct.calcsize(row_fmt)
    extra_fmt = "<BBHiHH"
    extra_size = struct.calcsize(extra_fmt)
    names: dict[int, str] = {}
    for _ in range(count):
        if pos + row_size > len(data):
            raise SystemExit(f"{ODEF}: truncated definition row")
        rec = struct.unpack_from(row_fmt, data, pos)
        pos += row_size
        obj_id = rec[0]
        model_count = rec[5]
        transform_count = rec[6]
        param_count = 0
        if version >= 2:
            if pos + extra_size > len(data):
                raise SystemExit(f"{ODEF}: truncated definition extension")
            extra = struct.unpack_from(extra_fmt, data, pos)
            pos += extra_size
            param_count = extra[2]
        if pos + 2 > len(data):
            raise SystemExit(f"{ODEF}: truncated object name")
        name_len = struct.unpack_from("<H", data, pos)[0]
        pos += 2
        if pos + name_len > len(data):
            raise SystemExit(f"{ODEF}: truncated object name payload")
        names[obj_id] = data[pos:pos + name_len].decode(
            "latin-1", errors="replace")
        pos += name_len
        for _slot in range(5):
            if pos + 2 > len(data):
                raise SystemExit(f"{ODEF}: truncated object action")
            action_len = struct.unpack_from("<H", data, pos)[0]
            pos += 2 + action_len
            if pos > len(data):
                raise SystemExit(f"{ODEF}: truncated object action payload")
        pos += model_count * 4 + transform_count * 4 + param_count * 8
        if pos > len(data):
            raise SystemExit(f"{ODEF}: truncated object metadata")
    if pos != len(data):
        raise SystemExit(f"{ODEF}: unexpected trailing bytes")
    return names


def read_snapshot() -> list[tuple[int, ...]]:
    data = SNAPSHOT.read_bytes()
    magic, version, count = struct.unpack_from("<III", data, 0)
    row_size = struct.calcsize(ROW_FMT)
    if magic != OBHV_MAGIC or version != OBHV_VERSION:
        raise SystemExit(f"{SNAPSHOT}: expected reviewed OBHV v2 snapshot")
    if len(data) != 12 + count * row_size:
        raise SystemExit(f"{SNAPSHOT}: malformed row payload")
    return [
        struct.unpack_from(ROW_FMT, data, 12 + i * row_size)
        for i in range(count)
    ]


def main() -> int:
    names = read_object_names()
    source_rows = read_snapshot()
    rows: list[tuple[int, ...]] = []
    upgraded = 0
    counts: Counter[str] = Counter()
    for row in source_rows:
        obj_id, flags, *rest = row
        if flags & (B_PAIR_LEFT | B_PAIR_RIGHT):
            name = names.get(obj_id)
            if name is None:
                raise SystemExit(f"paired object {obj_id} lacks a B237 definition")
            if "gate" in name.lower():
                if not (flags & B_PAIR_WIDE):
                    upgraded += 1
                flags |= B_PAIR_WIDE
        updated = (obj_id, flags, *rest)
        rows.append(updated)
        for bit, label in FLAG_NAMES:
            if flags & bit:
                counts[label] += 1

    OUT.parent.mkdir(parents=True, exist_ok=True)
    tmp = OUT.with_name(OUT.name + ".tmp")
    with tmp.open("wb") as handle:
        handle.write(struct.pack("<III", OBHV_MAGIC, OBHV_VERSION, len(rows)))
        for row in rows:
            handle.write(struct.pack(ROW_FMT, *row))
    tmp.replace(OUT)

    lines = [
        "Object behavior rules",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: {OUT.relative_to(ROOT)} ({OUT.stat().st_size} bytes)",
        f"source reviewed behaviors: {SNAPSHOT.relative_to(ROOT)}",
        f"source B237 definitions: {ODEF.relative_to(ROOT)}",
        f"behavior rows: {len(rows)}",
        f"wide paired gate rows added: {upgraded}",
        "",
        "by behavior:",
    ]
    for _bit, label in FLAG_NAMES:
        lines.append(f"  {label:<12} {counts[label]}")
    lines.extend([
        "",
        "accepted simplifications:",
        "  - reviewed behavior classifications and stage pairs are preserved",
        "  - paired B237 objects whose definition name identifies a gate carry "
        "an explicit wide-pair flag",
        "  - exact per-object sounds remain deferred content metadata",
    ])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
