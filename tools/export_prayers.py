#!/usr/bin/env python3
"""Emit data/defs/prayers.bin from RuneC-owned prayer metadata.

Runtime schema: `schema/defs/prayers.schema.toml`.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data/defs/prayers.bin"
REPORT = ROOT / "tools/reports/prayers.txt"

MAGIC = 0x59415250
VERSION = 2

MEMBERS = 1 << 0
OVERHEAD = 1 << 1
UNAVAILABLE = 1 << 2

G_DEF = 1 << 0
G_STR = 1 << 1
G_ATK = 1 << 2
G_RANGED = G_ATK | G_STR
G_MAGIC = G_ATK | G_STR
G_OVERHEAD = 1 << 3


def row(name: str, level: int, drain: int, varbit: int, groups: int = 0,
        flags: int = 0, atk: int = 0, str_: int = 0, defence: int = 0,
        ratk: int = 0, rstr: int = 0, matk: int = 0, mdef: int = 0,
        mdmg: int = 0, unlock: int = 0, unlock_value: int = 0,
        defence_level: int = 0, replaces: int = 255):
    return {
        "name": name, "level": level, "drain": drain, "varbit": varbit,
        "groups": groups, "flags": flags,
        "atk": atk, "str": str_, "def": defence,
        "ratk": ratk, "rstr": rstr, "matk": matk, "mdef": mdef,
        "mdmg": mdmg,
        "unlock": unlock, "unlock_value": unlock_value,
        "defence_level": defence_level, "replaces": replaces,
    }


PRAYERS = [
    row("Thick Skin", 1, 1, 4104, G_DEF, defence=5),
    row("Burst of Strength", 4, 1, 4105, G_STR, str_=5),
    row("Clarity of Thought", 7, 1, 4106, G_ATK, atk=5),
    row("Sharp Eye", 8, 1, 4122, G_RANGED, ratk=5, rstr=5),
    row("Mystic Will", 9, 1, 4123, G_MAGIC, matk=5, mdef=5),
    row("Rock Skin", 10, 6, 4107, G_DEF, defence=10),
    row("Superhuman Strength", 13, 6, 4108, G_STR, str_=10),
    row("Improved Reflexes", 16, 6, 4109, G_ATK, atk=10),
    row("Rapid Restore", 19, 1, 4110),
    row("Rapid Heal", 22, 2, 4111),
    row("Protect Item", 25, 2, 4112, flags=UNAVAILABLE),
    row("Hawk Eye", 26, 6, 4124, G_RANGED, ratk=10, rstr=10),
    row("Mystic Lore", 27, 6, 4125, G_MAGIC, matk=10, mdef=10, mdmg=1),
    row("Steel Skin", 28, 12, 4113, G_DEF, defence=15),
    row("Ultimate Strength", 31, 12, 4114, G_STR, str_=15),
    row("Incredible Reflexes", 34, 12, 4115, G_ATK, atk=15),
    row("Protect from Magic", 37, 12, 4116, G_OVERHEAD,
        flags=OVERHEAD),
    row("Protect from Missiles", 40, 12, 4117, G_OVERHEAD,
        flags=OVERHEAD),
    row("Protect from Melee", 43, 12, 4118, G_OVERHEAD,
        flags=OVERHEAD),
    row("Eagle Eye", 44, 12, 4126, G_RANGED, ratk=15, rstr=15),
    row("Mystic Might", 45, 12, 4127, G_MAGIC, matk=15, mdef=15, mdmg=2),
    row("Retribution", 46, 3, 4119, G_OVERHEAD, flags=MEMBERS | OVERHEAD | UNAVAILABLE),
    row("Redemption", 49, 6, 4120, G_OVERHEAD, flags=MEMBERS | OVERHEAD | UNAVAILABLE),
    row("Smite", 52, 18, 4121, G_OVERHEAD, flags=MEMBERS | OVERHEAD),
    row("Preserve", 55, 2, 5466, flags=MEMBERS, unlock=5453, unlock_value=1),
    row("Chivalry", 60, 24, 4128, G_ATK | G_STR | G_DEF,
        flags=MEMBERS, atk=15, str_=18, defence=20,
        unlock=3909, unlock_value=8, defence_level=65),
    row("Deadeye", 62, 12, 16090, G_RANGED,
        flags=MEMBERS, ratk=18, rstr=18, defence=5,
        unlock=16097, unlock_value=1, replaces=19),
    row("Mystic Vigour", 63, 12, 16091, G_MAGIC,
        flags=MEMBERS, matk=18, mdef=18, defence=5, mdmg=3,
        unlock=16098, unlock_value=1, replaces=20),
    row("Piety", 70, 24, 4129, G_ATK | G_STR | G_DEF,
        flags=MEMBERS, atk=20, str_=23, defence=25,
        unlock=3909, unlock_value=8, defence_level=70),
    row("Rigour", 74, 24, 5464, G_RANGED | G_DEF,
        flags=MEMBERS, ratk=20, rstr=23, defence=25,
        unlock=5451, unlock_value=1, defence_level=70),
    row("Augury", 77, 24, 5465, G_MAGIC | G_DEF,
        flags=MEMBERS, matk=25, mdef=25, defence=25, mdmg=4,
        unlock=5452, unlock_value=1, defence_level=70),
]


def main() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as f:
        f.write(struct.pack("<III", MAGIC, VERSION, len(PRAYERS)))
        for idx, p in enumerate(PRAYERS):
            name = p["name"].encode("utf-8")[:255]
            f.write(struct.pack(
                "<BBBBHHBBBbbbbbbbbB",
                idx, p["level"], p["drain"], p["flags"],
                p["unlock"], p["groups"], p["unlock_value"],
                p["defence_level"], p["replaces"],
                p["atk"], p["str"], p["def"],
                p["ratk"], p["rstr"], p["matk"], p["mdef"], p["mdmg"],
                len(name),
            ))
            f.write(name)

    lines = [
        "Prayer definition export",
        "",
        "status: FOUNDATION_WITH_EXPLICIT_UNAVAILABLE_EFFECTS",
        f"runtime: {OUT.relative_to(ROOT)}",
        f"count: {len(PRAYERS)}",
        "source: B237 prayer params/enums/varbits; rsmod B233; RuneLite B235; osrs-dps-calc",
        "scope: standard OSRS prayer book only; Ruinous Powers excluded",
        "notes:",
        "  Conflicts use the B237 attack/strength/defence/overhead sets.",
        "  Unlocks use existing world varbits; PRAY contains no presentation IDs.",
        "  Protect Item, Retribution and Redemption are explicitly unavailable",
        "  pending death-item ownership and verified death/low-HP effects.",
        "  Smite affects only eligible resources or explicit encounter hooks.",
    ]
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines) + "\n")
    print(f"{len(PRAYERS)} prayers -> {OUT}", file=sys.stderr)


if __name__ == "__main__":
    main()
