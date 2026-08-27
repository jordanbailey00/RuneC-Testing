#!/usr/bin/env python3
"""Upgrade reviewed gathering nodes with exact placement lifecycle data."""
from __future__ import annotations

import struct
from collections import Counter
from pathlib import Path

from object_placement_index import iter_object_placements

ROOT = Path(__file__).resolve().parents[1]
OBHV = ROOT / "data/defs/object_behaviors.bin"
OPLI = ROOT / "data/regions/world.object-placements.indexed.bin"
SNAPSHOT = ROOT / "content/runtime_snapshots/data/defs/gathering_nodes.bin"
OUT = ROOT / "data/defs/gathering_nodes.bin"
REPORT = ROOT / "tools/reports/gathering_nodes.txt"

GNOD_MAGIC = 0x444F4E47  # GNOD
GNOD_VERSION = 2
RESOURCE_FLAG = 1 << 5
EXPECTED_UNPLACED_SNAPSHOT_NODES = 806

SKILL_NAMES = {
    1: "woodcutting",
    2: "mining",
    3: "fishing",
    4: "farming",
    5: "prayer",
}


def read_behaviors() -> dict[int, tuple[int, int, int, int]]:
    data = OBHV.read_bytes()
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != 0x5648424F or version not in (1, 2):
        raise SystemExit("bad object_behaviors.bin")
    pos = 12
    out: dict[int, tuple[int, int, int]] = {}
    for _ in range(count):
        if version == 1:
            obj_id, flags, action_mask, skill, _pad = struct.unpack_from(
                "<IIBBH", data, pos)
            pos += 12
            next_stage = -1
        else:
            obj_id, flags, next_stage, _open_sound, _close_sound, \
                _climb_anim, action_mask, skill, _pad = struct.unpack_from(
                    "<IIiiiiBBH", data, pos)
            pos += struct.calcsize("<IIiiiiBBH")
        if flags & RESOURCE_FLAG:
            out[obj_id] = (action_mask, skill, flags, next_stage)
    return out


def read_snapshot_nodes() -> list[tuple[int, ...]]:
    data = SNAPSHOT.read_bytes()
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != GNOD_MAGIC or version != 1:
        raise SystemExit(f"{SNAPSHOT}: expected reviewed GNOD v1 snapshot")
    row_fmt = "<IHHHBBBBBBH"
    row_size = struct.calcsize(row_fmt)
    if len(data) != 12 + count * row_size:
        raise SystemExit(f"{SNAPSHOT}: malformed row payload")
    return [
        struct.unpack_from(row_fmt, data, 12 + i * row_size)
        for i in range(count)
    ]


def read_placement_keys(
    requested: set[tuple[int, int, int, int, int, int]],
) -> tuple[dict[tuple[int, int, int, int, int, int], int],
           set[tuple[int, int, int, int, int, int]]]:
    keys: dict[tuple[int, int, int, int, int, int], int] = {}
    for (obj_id, key, x, y, mapsquare, plane, typ, rotation,
         _place_flags) in iter_object_placements(OPLI):
        identity = (obj_id, x, y, plane, typ, rotation)
        if identity not in requested:
            continue
        if identity in keys:
            raise SystemExit(f"ambiguous gathering placement: {identity}")
        keys[identity] = key
    missing = requested - keys.keys()
    if len(missing) != EXPECTED_UNPLACED_SNAPSHOT_NODES:
        raise SystemExit(
            f"expected {EXPECTED_UNPLACED_SNAPSHOT_NODES} reviewed nodes "
            f"without B237 placements, found {len(missing)}")
    return keys, missing


def build_rows() -> tuple[list[tuple[int, ...]], int]:
    resources = read_behaviors()
    snapshot_rows = read_snapshot_nodes()
    requested = {
        (obj_id, x, y, plane, typ, rotation)
        for obj_id, x, y, _mapsquare, plane, _skill, _action_mask, typ,
            rotation, _flags, _pad in snapshot_rows
    }
    placement_keys, unplaced = read_placement_keys(requested)
    rows = []
    for obj_id, x, y, mapsquare, plane, skill, action_mask, typ, rotation, \
            flags, _pad in snapshot_rows:
        behavior = resources.get(obj_id)
        if not behavior:
            raise SystemExit(f"reviewed gathering object {obj_id} lacks behavior")
        identity = (obj_id, x, y, plane, typ, rotation)
        if identity in unplaced:
            continue
        _behavior_mask, _behavior_skill, _behavior_flags, next_stage = behavior
        key = placement_keys[identity]
        respawn_ticks = 20 if skill == 4 else 4 if skill == 3 else 8
        rows.append((
            mapsquare, x, y, obj_id, key, plane, skill, action_mask,
            typ, rotation, flags,
            next_stage, respawn_ticks,
        ))
    return sorted(rows), len(unplaced)


def main() -> int:
    rows, excluded_count = build_rows()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", GNOD_MAGIC, GNOD_VERSION, len(rows)))
        for row in rows:
            mapsquare, x, y, obj_id, key, plane, skill, action_mask, typ, \
                rotation, flags, next_stage, respawn_ticks = row
            f.write(struct.pack(
                "<IQiHHHBBBBBBHH",
                obj_id, key, next_stage, x, y, mapsquare, plane, skill,
                action_mask, typ, rotation, flags, respawn_ticks, 0,
            ))

    by_skill = Counter(row[6] for row in rows)
    by_region = Counter(row[0] for row in rows)
    lines = [
        "Gathering node index",
        "",
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS",
        f"output binary: data/defs/gathering_nodes.bin ({OUT.stat().st_size} bytes)",
        f"source object behaviors: {OBHV.relative_to(ROOT)}",
        f"source object placements: {OPLI.relative_to(ROOT)}",
        f"source reviewed nodes: {SNAPSHOT.relative_to(ROOT)}",
        f"nodes: {len(rows)}",
        f"reviewed nodes excluded without exact B237 placement: {excluded_count}",
        "",
        "by skill:",
    ]
    for skill, count in sorted(by_skill.items()):
        lines.append(f"  {SKILL_NAMES.get(skill, str(skill)):<12} {count}")
    lines.extend(["", "top regions:"])
    for mapsquare, count in by_region.most_common(20):
        rx, ry = mapsquare >> 8, mapsquare & 0xFF
        lines.append(f"  {mapsquare:5} ({rx:02d},{ry:02d}) {count}")
    lines.extend([
        "",
        "accepted simplifications:",
        "  - nodes are object-backed resources only; hunter targets, stalls, "
        "and activity-only nodes stay with activity schemas",
        "  - level/tool requirements come from recipes and future per-skill "
        "action rules",
        "  - runtime now owns generic depletion/respawn; exact success rolls "
        "remain per-skill action work",
    ])
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")
    print(REPORT.read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
