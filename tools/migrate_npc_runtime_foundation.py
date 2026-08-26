#!/usr/bin/env python3
"""Upgrade the reviewed NPC runtime snapshots without replacing their content."""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIPELINE = ROOT / "tools/cache_pipeline"
sys.path.insert(0, str(PIPELINE))
sys.path.insert(0, str(ROOT / "tools"))

from rc_cache import CONFIG_NPC, RcCacheStore, decode_npc_definition, read_config_group
from spawn_index import read_npc_spawns, write_npc_spawns

NDEF_MAGIC = 0x4E444546
NDEF_V4 = 4
NDEF_V5 = 5
HUNT_PLAYER = 1
HUNT_LINE_OF_SIGHT = 1
HUNT_OUTSIDE_WILDERNESS = 1
HUNT_CHECK_NOT_BUSY = 1 << 0
HUNT_KEEP_HUNTING = 1 << 1
DEFAULT_RESPAWN_TICKS = 25
DEFAULT_REGEN_TICKS = 100
DEFAULT_WANDER_RANGE = 5
DEFAULT_SPAWN_DIRECTION = 6
SPAWN_WANDER_USE_DEF = 255


@dataclass(frozen=True)
class NpcRow:
    npc_id: int
    prefix: bytes
    aggressive: bool
    aggro_range: int
    policy: tuple[int, ...] | None


def take(data: bytes, offset: int, size: int, path: Path) -> tuple[bytes, int]:
    end = offset + size
    if end > len(data):
        raise ValueError(f"{path}: truncated NDEF row")
    return data[offset:end], end


def read_rows(path: Path) -> list[NpcRow]:
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"{path}: truncated NDEF header")
    magic, version, count = struct.unpack_from("<III", data)
    if magic != NDEF_MAGIC or version not in (NDEF_V4, NDEF_V5):
        raise ValueError(f"{path}: expected NDEF v4 or v5")
    offset = 12
    rows: list[NpcRow] = []
    for _ in range(count):
        start = offset
        fixed, offset = take(data, offset, 4 + 1 + 2 + 2 + 12 + 20, path)
        npc_id = struct.unpack_from("<I", fixed)[0]
        name_len_raw, offset = take(data, offset, 1, path)
        _, offset = take(data, offset, name_len_raw[0], path)
        combat, offset = take(data, offset, 10, path)
        aggressive = combat[0] != 0
        aggro_range = combat[4]
        model_count_raw, offset = take(data, offset, 1, path)
        _, offset = take(data, offset, model_count_raw[0] * 4, path)
        for _ in range(5):
            option_len_raw, offset = take(data, offset, 1, path)
            _, offset = take(data, offset, option_len_raw[0], path)
        prefix_end = offset
        policy = None
        if version == NDEF_V5:
            policy_raw, offset = take(
                data, offset, struct.calcsize("<BHHBBBBBBiiH"), path
            )
            values = struct.unpack("<BHHBBBBBBiiH", policy_raw)
            transform_count = values[-1]
            _, offset = take(data, offset, transform_count * 4, path)
            policy = values[:-1]
        rows.append(NpcRow(npc_id, data[start:prefix_end], aggressive,
                           aggro_range, policy))
    if offset != len(data):
        raise ValueError(f"{path}: trailing NDEF bytes")
    return rows


def cache_transforms(cache: Path) -> dict[int, tuple[int, int, list[int]]]:
    store = RcCacheStore(cache)
    records = read_config_group(store, CONFIG_NPC)
    result: dict[int, tuple[int, int, list[int]]] = {}
    for npc_id, payload in records.items():
        definition = decode_npc_definition(npc_id, payload)
        if not definition.complete:
            raise ValueError(
                f"cache NPC {npc_id}: unknown opcode {definition.unknown_opcode}"
            )
        result[npc_id] = (
            int(definition.varbit),
            int(definition.varp),
            [int(value) for value in definition.transforms],
        )
    return result


def derived_policy(row: NpcRow, repair_missing_wander: bool) -> tuple[int, ...]:
    if row.policy is not None:
        policy = list(row.policy)
        if repair_missing_wander:
            policy[0] = DEFAULT_WANDER_RANGE
        return tuple(policy)
    if not row.aggressive:
        return (DEFAULT_WANDER_RANGE, DEFAULT_RESPAWN_TICKS,
                DEFAULT_REGEN_TICKS,
                0, 0, 0, 0, 0, 0, -1, -1)
    return (
        DEFAULT_WANDER_RANGE,
        DEFAULT_RESPAWN_TICKS,
        DEFAULT_REGEN_TICKS,
        HUNT_PLAYER,
        HUNT_LINE_OF_SIGHT,
        HUNT_OUTSIDE_WILDERNESS,
        HUNT_CHECK_NOT_BUSY | HUNT_KEEP_HUNTING,
        max(1, row.aggro_range),
        1,
        -1,
        -1,
    )


def write_ndef(source: Path, output: Path, cache: Path) -> bool:
    rows = read_rows(source)
    repair_missing_wander = bool(rows) and (
        all(row.policy is None for row in rows)
        or all(row.policy is not None and row.policy[0] == 0 for row in rows)
    )
    transforms_by_id = cache_transforms(cache)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as handle:
        handle.write(struct.pack("<III", NDEF_MAGIC, NDEF_V5, len(rows)))
        for row in rows:
            policy = list(derived_policy(row, repair_missing_wander))
            varbit, varp, transforms = transforms_by_id.get(
                row.npc_id, (policy[9], policy[10], [])
            )
            if len(transforms) > 0xFFFF:
                raise ValueError(
                    f"NPC {row.npc_id}: too many transforms ({len(transforms)})"
                )
            policy[9] = varbit
            policy[10] = varp
            handle.write(row.prefix)
            handle.write(struct.pack("<BHHBBBBBBiiH", *policy,
                                     len(transforms)))
            for transform in transforms:
                handle.write(struct.pack("<i", transform))
    return repair_missing_wander


def write_spawns(source: Path, output: Path,
                 repair_missing_wander: bool) -> None:
    rows = read_npc_spawns(source)
    normalized = [
        (npc_id, x, y, plane,
         DEFAULT_SPAWN_DIRECTION if repair_missing_wander else direction,
         SPAWN_WANDER_USE_DEF if repair_missing_wander else wander, flags)
        for npc_id, x, y, plane, direction, wander, flags in rows
    ]
    write_npc_spawns(output, normalized)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--npc-source", type=Path, required=True)
    parser.add_argument("--npc-output", type=Path, required=True)
    parser.add_argument("--spawn-source", type=Path, required=True)
    parser.add_argument("--spawn-output", type=Path, required=True)
    args = parser.parse_args()
    repair_missing_wander = write_ndef(
        args.npc_source, args.npc_output, args.cache
    )
    write_spawns(args.spawn_source, args.spawn_output, repair_missing_wander)
    print(f"wrote NDEF v5 to {args.npc_output}")
    print(f"wrote explicit spawn policy to {args.spawn_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
