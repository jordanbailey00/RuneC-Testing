#!/usr/bin/env python3
"""Emit data/defs/spells.bin and data/defs/teleports.bin.

Runtime schemas: `schema/defs/spells.schema.toml` and
`schema/defs/teleports.schema.toml`.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
import struct
import sys
from pathlib import Path

from content_paths import content_read_path

ROOT = Path(__file__).resolve().parents[1]

CACHE = ROOT / "tools/wiki_cache"
OUT_S = ROOT / "data/defs/spells.bin"
OUT_T = ROOT / "data/defs/teleports.bin"

SPEL_MAGIC = 0x4C455053
TELE_MAGIC = 0x454C4554
VERSION = 2

REPORT = ROOT / "tools/reports/spells.txt"

SPELLBOOK = {"normal": 0, "ancient": 1, "lunar": 2, "arceuus": 3, "all": 4}
SPELL_TYPE = {"combat": 1, "teleport": 2, "utility": 3,
              "skilling": 4, "curse": 5, "charging": 6,
              "summoning": 7, "enchantment": 3, "alchemy": 3}

EFFECT_DAMAGE = 1 << 0
EFFECT_FREEZE = 1 << 1
EFFECT_HEAL = 1 << 2
EFFECT_POISON = 1 << 3
EFFECT_DRAIN = 1 << 4
EFFECT_TELEPORT = 1 << 5

_RUNE_RE = re.compile(r"<sup>(\d+)</sup>\s*\[\[File:([^\]|]+?)\.png",
                      re.IGNORECASE)


def load_bucket(bucket: str):
    rows = []
    for p in sorted(CACHE.glob(f"{bucket}_*.json")):
        rows.extend(json.loads(p.read_text()).get("bucket", []))
    return rows


def item_map() -> dict[str, int]:
    out: dict[str, int] = {}
    for r in load_bucket("infobox_item"):
        name = (r.get("item_name") or r.get("page_name") or "").strip()
        if not name:
            continue
        ids = r.get("item_id") or []
        if not isinstance(ids, list):
            ids = [ids]
        for iid in ids:
            try:
                iid = int(iid)
            except (TypeError, ValueError):
                continue
            k = name.lower()
            if k not in out or iid < out[k]:
                out[k] = iid
    return out


def parse_runes(cost: str, items: dict[str, int]):
    out = []
    for m in _RUNE_RE.finditer(cost or ""):
        qty = int(m.group(1))
        rune = m.group(2).strip()
        iid = items.get(rune.lower())
        if iid is None or not 0 < qty <= 255:
            raise ValueError(f"unknown rune or invalid quantity: {rune} x{qty}")
        out.append((iid, qty))
    if cost and cost.strip() and not out:
        raise ValueError(f"unrecognized rune cost: {cost}")
    return out


def slugify(s: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")


def combat_effects() -> dict[str, tuple[int, int]]:
    path = content_read_path("combat/spell_effects.tsv")
    with path.open() as stream:
        rows = list(csv.DictReader(stream, delimiter="|"))
    effects = {}
    for row in rows:
        key = slugify(row["name"])
        value = int(row["max_hit"]), int(row["effect_flags"])
        if key in effects or not 0 <= value[0] <= 65535 or not 0 < value[1] <= 65535:
            raise ValueError(f"invalid or duplicate combat spell: {row['name']}")
        effects[key] = value
    return effects


def update_combat_effects(path: Path) -> int:
    """Preserve installed spell indices, requirements, and all noncombat records."""
    data = bytearray(path.read_bytes())
    if len(data) < 12 or struct.unpack_from("<II", data) != (SPEL_MAGIC, VERSION):
        raise ValueError("expected a complete SPEL v2 file")
    count, = struct.unpack_from("<I", data, 8)
    effects = combat_effects()
    found = set()
    offset = 12
    for _ in range(count):
        if offset >= len(data):
            raise ValueError("truncated spell name")
        size = data[offset]
        offset += 1
        if offset + size + 12 > len(data):
            raise ValueError("truncated spell record")
        key = slugify(data[offset:offset + size].decode("latin-1"))
        offset += size
        if key in effects:
            if key in found:
                raise ValueError(f"duplicate installed spell: {key}")
            struct.pack_into("<HH", data, offset + 7, *effects[key])
            found.add(key)
        offset += 12 + data[offset + 11] * 5
    if offset != len(data):
        raise ValueError("spell record lengths do not match the file")
    if found != effects.keys():
        raise ValueError(f"missing combat spells: {sorted(effects.keys() - found)}")
    path.write_bytes(data)
    return len(found)


def parse_level(s) -> int:
    if s is None:
        return 1
    m = re.search(r"\d+", str(s).strip())
    return int(m.group(0)) if m else 1


def parse_xp_q1(s) -> int:
    if s is None:
        return 0
    s = str(s).strip()
    if not s or re.search(r"[a-zA-Z*]", s):
        return 0
    try:
        return max(0, min(65535, round(float(s) * 10)))
    except ValueError:
        return 0


def pack_short(s: str) -> bytes:
    data = s.encode("latin-1")
    if not 0 < len(data) < 64 or b"\0" in data:
        raise ValueError(f"invalid spell name: {s!r}")
    return data


def write_bin(path: Path, magic: int, spells: list[dict]):
    if magic not in (SPEL_MAGIC, TELE_MAGIC) or not 0 < len(spells) <= 512:
        raise ValueError("invalid spell header or row count")
    records = {}
    for s in spells:
        key = (s["book"], s["name"])
        if key in records or not (0 <= s["book"] <= 4 and 0 <= s["type"] <= 7
                and 1 <= s["level"] <= 99 and 0 <= s["slv"] <= 99
                and 0 <= s["xp_q1"] <= 65535 and s["members"] in (0, 1)
                and 0 <= s["max_hit"] <= 65535 and 0 <= s["effect_flags"] <= 63):
            raise ValueError(f"invalid or duplicate spell: {key}")
        runes = s["runes"]
        if (len(runes) > 8 or len({iid for iid, _ in runes}) != len(runes)
                or any(not 0 < iid <= 2147483647 or not 0 < qty <= 255 for iid, qty in runes)):
            raise ValueError(f"invalid rune requirements: {key}")
        nb = pack_short(s["name"])
        records[key] = (bytes((len(nb),)) + nb + struct.pack("<BBBBHBHHB",
            s["book"], s["type"], s["level"], s["slv"], s["xp_q1"],
            s["members"], s["max_hit"], s["effect_flags"], len(runes))
            + b"".join(struct.pack("<IB", iid, qty) for iid, qty in runes))
    ordered = []
    if path.exists():
        old = path.read_bytes()
        if len(old) < 12 or struct.unpack_from("<II", old) != (magic, VERSION):
            raise ValueError(f"invalid installed spell table: {path}")
        offset = 12
        seen = set()
        for _ in range(struct.unpack_from("<I", old, 8)[0]):
            if offset >= len(old) or not 0 < old[offset] < 64:
                raise ValueError("invalid installed spell name")
            size = old[offset]
            offset += 1
            if offset + size + 12 > len(old):
                raise ValueError("truncated installed spell record")
            key = (old[offset + size], old[offset:offset + size].decode("latin-1"))
            offset += size
            rune_count = old[offset + 11]
            if rune_count > 8 or key in seen or key not in records:
                raise ValueError(f"invalid, duplicate or missing installed spell identity: {key}")
            seen.add(key)
            ordered.append(records.pop(key))
            offset += 12 + rune_count * 5
        if offset != len(old):
            raise ValueError("invalid installed spell record lengths")
    ordered.extend(records[key] for key in sorted(records))
    payload = struct.pack("<III", magic, VERSION, len(ordered)) + b"".join(ordered)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(payload)
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--update-combat-effects", type=Path)
    args = parser.parse_args()
    if args.update_combat_effects:
        count = update_combat_effects(args.update_combat_effects)
        print(f"Updated {count} combat spells in {args.update_combat_effects}")
        return
    items = item_map()
    effects = combat_effects()
    spells = []
    teleports = []

    for r in load_bucket("infobox_spell"):
        name = (r.get("page_name") or "").strip()
        if not name:
            continue
        blob = r.get("json")
        if not blob:
            continue
        try:
            j = json.loads(blob)
        except (json.JSONDecodeError, TypeError):
            continue

        book = SPELLBOOK.get((r.get("spellbook") or j.get("spellbook")
                              or "").strip().lower(), 0)
        stype = SPELL_TYPE.get((j.get("type") or "").strip().lower(), 0)
        level = min(99, max(1, parse_level(j.get("level"))))
        slv_raw = j.get("slayerlevel")
        slv = parse_level(slv_raw)
        if slv == 1 and not str(slv_raw or "").strip():
            slv = 0
        max_hit, effect_flags = effects.get(slugify(name), (0, 0))
        if stype == 2:
            effect_flags |= EFFECT_TELEPORT
        s = {
            "name": name, "book": book, "type": stype,
            "level": level, "slv": min(99, slv),
            "xp_q1": parse_xp_q1(j.get("exp")),
            "members": 1 if r.get("is_members_only") else 0,
            "max_hit": max(0, min(65535, max_hit)),
            "effect_flags": max(0, min(65535, effect_flags)),
            "runes": parse_runes(j.get("cost") or "", items),
        }
        spells.append(s)
        if stype == 2:
            teleports.append(s)

    if not spells:
        raise ValueError("no cached spell definitions; refusing to replace installed data")
    print(f"{len(spells)} spells, {len(teleports)} teleports",
          file=sys.stderr)
    OUT_S.parent.mkdir(parents=True, exist_ok=True)
    write_bin(OUT_S, SPEL_MAGIC, spells)
    write_bin(OUT_T, TELE_MAGIC, teleports)
    matched = sum(1 for s in spells if s["max_hit"] > 0)
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(
        "Spellbook export\n\n"
        "status: READY_WITH_ACCEPTED_SIMPLIFICATIONS\n"
        f"spells: {len(spells)}\n"
        f"teleports: {len(teleports)}\n"
        f"combat max-hit matches: {matched}\n"
        "source: OSRS Wiki infobox_spell cache\n"
        "notes:\n"
        "  Spell metadata and rune costs are wiki-derived. Combat max hits and\n"
        "  basic effects come from content/combat/spell_effects.tsv. Advanced\n"
        "  target restrictions, area effects and utility spells remain tracked.\n"
    )
    print(f"  → {OUT_S} ({OUT_S.stat().st_size} bytes)", file=sys.stderr)
    print(f"  → {OUT_T} ({OUT_T.stat().st_size} bytes)", file=sys.stderr)


if __name__ == "__main__":
    main()
