"""Compile explicit source rejections into the existing NPC drop-table asset.

The v1 snapshot is an authoring input, not an accepted runtime format. Rows
not rejected here remain legacy/unverified; this migration does not certify
their probabilities. The exact-roll migration is still an open loot task.
"""
from __future__ import annotations

import json
from pathlib import Path
import struct
from export_normalization import parse_items, parse_npcs

ROOT = Path(__file__).resolve().parents[1]
REJECTIONS = ROOT / "content/loot/rejections.json"
REPAIRS = ROOT / "content/loot/runtime_repairs.json"
REASONS = {"unpublished_rate": 1, "outdated_drop_section": 2}


def repair_tables(rows: dict[int, bytes], repairs: dict) -> None:
    if repairs.get("format") != "runec-loot-runtime-repairs-v1":
        raise ValueError("unsupported loot repair manifest")
    for table in repairs["tables"]:
        if not table["source"].startswith("https://oldschool.runescape.wiki/?oldid="):
            raise ValueError("loot repair needs pinned evidence")
        source = rows[table["source_table"]]
        groups, offset = [], 0
        replacements = {int(k): v for k, v in table["item_ids"].items()}
        for kind, fmt in enumerate(("<IHH", "<IHHI", "<IHHI")):
            count = source[offset]
            offset += 1
            size = struct.calcsize(fmt)
            entries = [list(struct.unpack_from(fmt, source, offset + i * size)) for i in range(count)]
            offset += count * size
            for removal in table["remove"][kind]:
                if removal not in entries:
                    raise ValueError(f"{table['name']}: repair no longer matches source row {removal}")
            group = []
            for entry in entries:
                if entry in table["remove"][kind]:
                    continue
                entry[0] = replacements.get(entry[0], entry[0])
                if entry not in group:
                    group.append(entry)
            if kind == 2:
                group.extend(table["add_tertiary"])
            if len(group) > 255:
                raise ValueError("loot repair exceeds group capacity")
            groups.append(struct.pack("<B", len(group)) + b"".join(struct.pack(fmt, *r) for r in group))
        result = b"".join(groups) + source[offset:]
        for npc_id in table["npc_ids"]:
            if type(npc_id) is not int or not 0 <= npc_id < 20000:
                raise ValueError("invalid repaired NPC ID")
            rows[npc_id] = result


def compile_rejections(payload: bytes, inventory: dict, repairs: dict | None = None) -> bytes:
    if inventory.get("format") != "runec-loot-rejections-v1":
        raise ValueError("unsupported loot rejection manifest")
    if len(payload) < 12 or struct.unpack_from("<II", payload) != (0x504F5244, 1):
        raise ValueError("loot rejection input must be the owned DROP v1 snapshot")
    rejected = {}
    for table in inventory["tables"]:
        flags = 0
        for reason in table["reasons"]:
            if reason not in REASONS:
                raise ValueError(f"unknown loot rejection reason: {reason}")
            flags |= REASONS[reason]
        if not flags or not table["npc_ids"] or not table["source"].startswith("https://oldschool.runescape.wiki/?oldid="):
            raise ValueError("loot rejection needs NPC IDs, reason and pinned evidence")
        for npc in table["npc_ids"]:
            if type(npc) is not int or not 0 <= npc < 20000:
                raise ValueError("invalid rejected NPC ID")
            rejected[npc] = rejected.get(npc, 0) | flags
    rows = {}
    count, = struct.unpack_from("<I", payload, 8)
    offset = 12
    for _ in range(count):
        if offset + 4 > len(payload):
            raise ValueError("truncated snapshot NPC ID")
        npc, = struct.unpack_from("<I", payload, offset)
        offset += 4
        if npc in rows or npc >= 20000:
            raise ValueError("duplicate or invalid snapshot NPC ID")
        start = offset
        for kind in range(3):
            if offset >= len(payload):
                raise ValueError("truncated snapshot drop group")
            n = payload[offset]
            offset += 1 + n * (8 if kind == 0 else 12)
        offset += 4
        if offset > len(payload):
            raise ValueError("truncated snapshot drop entries")
        rows[npc] = payload[start:offset]
    if offset != len(payload):
        raise ValueError("trailing snapshot drop data")
    if repairs is not None:
        repair_tables(rows, repairs)
    # A rejected definition absent from the legacy snapshot still needs a
    # diagnostic table, not an indistinguishable successful empty drop.
    ids = sorted(rows.keys() | rejected.keys())
    output = bytearray(struct.pack("<III", 0x504F5244, 2, len(ids)))
    for npc in ids:
        flags = rejected.get(npc, 0)
        output.extend(struct.pack("<II", npc, flags))
        output.extend(bytes(7) if flags else rows[npc])
    return bytes(output)


def install_rejections(source: Path, destination: Path) -> None:
    inventory = json.loads(REJECTIONS.read_text())
    repairs = json.loads(REPAIRS.read_text())
    npcs, items = parse_npcs(), parse_items()
    for table in repairs["tables"]:
        for npc_id in table["npc_ids"]:
            if npcs.get(npc_id, {}).get("name") != table["name"]:
                raise ValueError(f"loot repair NPC identity mismatch: {npc_id}, {table['name']}")
        for old, new in table["item_ids"].items():
            before, after = items.get(int(old)), items.get(new)
            if not before or not after or before["name"] != after["name"]:
                raise ValueError(f"loot repair item identity mismatch: {old} -> {new}")
            if after["noted"] and before["linked_noted"] != new:
                raise ValueError(f"loot repair note link mismatch: {old} -> {new}")
    result = compile_rejections(source.read_bytes(), inventory, repairs)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_bytes(result)
    temporary.replace(destination)
