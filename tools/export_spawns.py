#!/usr/bin/env python3
"""Export NPC spawns from mejrs/data_osrs/NPCList_OSRS.json.

Primary source: `data_osrs/NPCList_OSRS.json` — 24,110 NPC spawn
instances, each row `{id, name, x, y, p, size, combatLevel, models,
actions, ...}`. Keyed by cache NPC ID; no name resolution needed.

Cross-check: OSRS Wiki `locline` bucket — keyed by `page_name`. We
join via simple name-equality against NPCList to spot coverage gaps.

Outputs:
  - data/spawns/world.npc-spawns.indexed.bin - mapsquare-indexed NPC spawns
  - tools/reports/spawn_coverage.txt - cross-check report
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

from legacy_external_source_paths import DATA_OSRS
from spawn_index import write_npc_spawns

NPC_SPAWN_FLAG_INSTANCE = 0x01

ROOT = Path(__file__).resolve().parents[1]
NPCLIST = DATA_OSRS / "NPCList_OSRS.json"

OUT = ROOT / "data"
REPORTS = ROOT / "tools/reports"

# NPCList_OSRS does not carry direction or wander_range — defaults below
# match what rc-core's NPC wander AI treats as "unspecified":
#   direction=2 (south), wander_range=0 → use def's wander_range (v2 NDEF).
DEFAULT_DIRECTION = 2
DEFAULT_WANDER = 0


def load_npclist() -> list[dict]:
    return json.loads(NPCLIST.read_text())


def write_indexed(path: Path, spawns: list[dict]) -> None:
    rows = [
        (
            int(spawn["id"]),
            int(spawn["x"]),
            int(spawn["y"]),
            int(spawn.get("p", 0)),
            DEFAULT_DIRECTION,
            DEFAULT_WANDER,
            int(spawn.get("flags", 0)) & 0xFF,
        )
        for spawn in spawns
    ]
    write_npc_spawns(path, rows)


def load_locline_rows() -> list[dict]:
    rows = []
    cache = ROOT / "tools/wiki_cache"
    for p in sorted(cache.glob("locline_*.json")):
        d = json.loads(p.read_text())
        rows.extend(d.get("bucket", []))
    return rows


def instance_only_names(locline: list[dict]) -> set[str]:
    """Names (lowercased) whose ALL locline entries have a non-zero
    mapid — meaning every listed spawn is inside an instance, not the
    main world. We use this to flag spawns for skipping on static
    world-spawn loading; runtime code spawns them on instance entry.

    Locline's mapid: null / -1 / 0 = main world; positive = instance.
    """
    by_name: dict[str, list] = defaultdict(list)
    for r in locline:
        name = (r.get("page_name") or "").strip().lower()
        if not name:
            continue
        mapid = r.get("mapid")
        if mapid is None:
            mapid = 0
        try:
            mapid = int(mapid)
        except (TypeError, ValueError):
            mapid = 0
        by_name[name].append(mapid)
    return {
        n for n, mapids in by_name.items()
        if mapids and all(m > 0 for m in mapids)
    }


def cross_check(spawns: list[dict], locline: list[dict]) -> str:
    """Diff wiki locline coverage against NPCList. Report only.

    locline `page_name` is the wiki page title (e.g. "Goblin"), not a
    cache ID — we match names case-insensitively. Reports:
      - distinct NPC names in NPCList
      - locline page_names that match (subset of NPCList names)
      - locline page_names with no name match (wiki-only entries —
        may be OSRS-only content or disambiguation pages)
      - top 20 wiki-only names by row count (for triage)
    """
    npc_names = {s["name"].lower() for s in spawns}
    locline_names = defaultdict(int)
    for r in locline:
        locline_names[r.get("page_name", "").lower()] += 1

    matched = {n for n in locline_names if n in npc_names}
    only_wiki = {n: c for n, c in locline_names.items()
                 if n and n not in npc_names}
    only_npclist = {n for n in npc_names if n not in locline_names}

    lines = []
    lines.append(f"NPCList spawns:           {len(spawns)}")
    lines.append(f"NPCList distinct names:   {len(npc_names)}")
    lines.append(f"locline rows:             {len(locline)}")
    lines.append(f"locline distinct names:   {len(locline_names)}")
    lines.append(f"name match both sources:  {len(matched)}")
    lines.append(f"wiki-only names:          {len(only_wiki)}")
    lines.append(f"NPCList-only names:       {len(only_npclist)}")
    lines.append("")
    lines.append("Top 20 wiki-only names (by row count — check whether "
                 "they're OSRS-only content missing from NPCList, or just "
                 "disambiguation pages / variants with different base names):")
    for n, c in sorted(only_wiki.items(), key=lambda kv: -kv[1])[:20]:
        lines.append(f"  {c:5}  {n}")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.parse_args()

    print(f"loading {NPCLIST}", file=sys.stderr)
    spawns = load_npclist()
    print(f"  {len(spawns)} spawns, "
          f"{len({s['name'] for s in spawns})} distinct NPC names",
          file=sys.stderr)

    # Cross-ref locline → compute instance-only flag per spawn.
    locline = load_locline_rows()
    inst_names = instance_only_names(locline) if locline else set()
    flagged = 0
    for s in spawns:
        if s["name"].lower() in inst_names:
            s["flags"] = NPC_SPAWN_FLAG_INSTANCE
            flagged += 1
    print(f"  flagged {flagged} spawns as instance-only (via locline)",
          file=sys.stderr)

    out_world = OUT / "spawns/world.npc-spawns.indexed.bin"
    write_indexed(out_world, spawns)
    print(f"  -> {out_world} ({out_world.stat().st_size} bytes)",
          file=sys.stderr)

    # Cross-check report
    if locline:
        report = cross_check(spawns, locline)
        REPORTS.mkdir(parents=True, exist_ok=True)
        (REPORTS / "spawn_coverage.txt").write_text(report)
        print(f"\n{report}", file=sys.stderr)


if __name__ == "__main__":
    main()
