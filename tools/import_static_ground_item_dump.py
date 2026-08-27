#!/usr/bin/env python3
"""Import a name-keyed OSRS static ground-item dump into RuneC content.

The source dump is keyed by item display/wiki names, while RuneC runtime data
uses b237 item ids. Resolution is done against b237 cache item configs, with a
small explicit alias table for source keys whose cache display names are
generic.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import struct
import sys
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from cache_item_defs import load_cache_item_defs  # noqa: E402

SOURCE = ROOT / "content/world/sources/osrs_item_spawn_dump.tyluur.2023-02-26.json"
OUT = ROOT / "content/world/static_ground_items.tsv"
REPORT = ROOT / "tools/reports/static_ground_item_import.txt"
DEFAULT_CACHE = ROOT / "data" / "source" / "b237-openrs2-2528" / "cache"
DEFAULT_ITEMS_BIN = ROOT / "data/defs/items.bin"

SOURCE_ID = "tyluur_gist_4cb93ba0_2f232f49"
IDEF_MAGIC = 0x49444546

RAW_ID_ALIASES = {
    "Diary_(Witch's_House)": 2408,
    "Poison_(item)": 273,
    "Burnt_Fish_(herring)": 357,
    "Letter_(The_Golem)": 4615,
}


@dataclass(frozen=True)
class Resolution:
    item_id: int
    method: str
    lookup_key: str
    candidates: tuple[int, ...]


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def lookup_key(value: str) -> str:
    value = unicodedata.normalize("NFKC", value or "")
    value = value.replace("_", " ")
    value = value.replace("\u2018", "'").replace("\u2019", "'")
    value = re.sub(r"[^0-9a-zA-Z]+", " ", value.lower())
    return re.sub(r"\s+", " ", value).strip()


def key_variants(raw_name: str) -> list[str]:
    base = raw_name.replace("_", " ")
    variants = [base]
    if base.lower().endswith(" log"):
        variants.append(base + "s")
    if base.lower().endswith(" logs"):
        variants.append(base[:-1])
    return list(dict.fromkeys(lookup_key(value) for value in variants))


def load_runtime_item_ids(path: Path) -> set[int]:
    if not path.is_file():
        return set()
    data = path.read_bytes()
    if len(data) < 12:
        raise ValueError(f"{path}: too short for IDEF header")
    magic, version, count = struct.unpack_from("<III", data, 0)
    if magic != IDEF_MAGIC or version not in (1, 2, 3):
        raise ValueError(f"{path}: unsupported IDEF header")
    pos = 12
    ids: set[int] = set()
    for _ in range(count):
        if pos + 4 > len(data):
            raise ValueError(f"{path}: truncated IDEF record length")
        (size,) = struct.unpack_from("<I", data, pos)
        pos += 4
        if pos + size > len(data):
            raise ValueError(f"{path}: truncated IDEF record")
        (item_id,) = struct.unpack_from("<I", data, pos)
        ids.add(int(item_id))
        pos += size
    return ids


def load_source(path: Path) -> dict[str, list[dict[str, Any]]]:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(data, dict):
        raise ValueError(f"{path}: expected top-level JSON object")
    for name, rows in data.items():
        if not isinstance(name, str) or not isinstance(rows, list):
            raise ValueError(f"{path}: malformed item entry {name!r}")
        for row in rows:
            if not isinstance(row, dict) or set(row) != {"x", "y", "z", "quantity"}:
                raise ValueError(f"{path}: malformed spawn row for {name!r}")
    return data


def build_name_index(cache_items: dict[int, dict[str, Any]]) -> dict[str, list[int]]:
    by_name: dict[str, list[int]] = {}
    for item_id, rec in cache_items.items():
        name = str(rec.get("name") or "")
        if not name or name == "null":
            continue
        key = lookup_key(name)
        if key:
            by_name.setdefault(key, []).append(int(item_id))
    return by_name


def is_viable(cache_items: dict[int, dict[str, Any]], item_id: int) -> bool:
    rec = cache_items[item_id]
    return (
        rec.get("name") != "null"
        and rec.get("note_template_id") is None
        and rec.get("placeholder_template_id") is None
    )


def score_item(rec: dict[str, Any], item_id: int) -> tuple[int, int, int, int, int, int]:
    ground_ops = {str(value or "").strip().lower() for value in rec.get("ground_ops") or []}
    inventory_ops = {
        str(value or "").strip().lower() for value in rec.get("interface_ops") or []
    }
    return (
        1 if "take" in ground_ops else 0,
        1 if "drop" in inventory_ops else 0,
        1 if rec.get("tradeable") else 0,
        1 if not rec.get("members") else 0,
        1 if rec.get("note_id") is not None else 0,
        -item_id,
    )


def resolve_name(
    raw_name: str,
    cache_items: dict[int, dict[str, Any]],
    by_name: dict[str, list[int]],
) -> Resolution:
    if raw_name in RAW_ID_ALIASES:
        item_id = RAW_ID_ALIASES[raw_name]
        if item_id not in cache_items:
            raise ValueError(f"{raw_name}: alias item id {item_id} not in cache")
        return Resolution(item_id, "source_alias", lookup_key(raw_name), (item_id,))

    candidates: list[int] = []
    used_key = ""
    for key in key_variants(raw_name):
        candidates = by_name.get(key, [])
        if candidates:
            used_key = key
            break
    viable = [item_id for item_id in candidates if is_viable(cache_items, item_id)]
    if not viable:
        raise ValueError(f"{raw_name}: unresolved item name")
    if len(viable) == 1:
        return Resolution(viable[0], "exact", used_key, tuple(viable))

    ranked = sorted(
        viable,
        key=lambda item_id: (score_item(cache_items[item_id], item_id), -item_id),
        reverse=True,
    )
    return Resolution(ranked[0], "scored_ambiguity", used_key, tuple(ranked))


def import_rows(
    source: dict[str, list[dict[str, Any]]],
    cache_items: dict[int, dict[str, Any]],
    runtime_ids: set[int],
) -> tuple[list[dict[str, str | int]], dict[str, Resolution]]:
    by_name = build_name_index(cache_items)
    resolutions: dict[str, Resolution] = {}
    rows: list[dict[str, str | int]] = []
    for raw_name in sorted(source):
        resolution = resolve_name(raw_name, cache_items, by_name)
        if runtime_ids and resolution.item_id not in runtime_ids:
            raise ValueError(
                f"{raw_name}: resolved item id {resolution.item_id} missing from items.bin"
            )
        resolutions[raw_name] = resolution
        for spawn in source[raw_name]:
            rows.append({
                "item_id": resolution.item_id,
                "quantity": int(spawn["quantity"]),
                "x": int(spawn["x"]),
                "y": int(spawn["y"]),
                "plane": int(spawn["z"]),
                "source": SOURCE_ID,
                "note": f"raw={raw_name};method={resolution.method}",
            })
    rows.sort(key=lambda row: (
        int(row["plane"]),
        int(row["x"]),
        int(row["y"]),
        int(row["item_id"]),
        int(row["quantity"]),
        str(row["note"]),
    ))
    return rows, resolutions


def write_tsv(path: Path, rows: list[dict[str, str | int]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        fieldnames = ["item_id", "quantity", "x", "y", "plane", "source", "note"]
        writer = csv.DictWriter(
            f, fieldnames=fieldnames, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(rows)


def write_report(
    path: Path,
    source_path: Path,
    cache_path: Path,
    items_bin: Path,
    rows: list[dict[str, str | int]],
    resolutions: dict[str, Resolution],
    cache_items: dict[int, dict[str, Any]],
) -> None:
    exact = [name for name, res in resolutions.items() if res.method == "exact"]
    aliases = [name for name, res in resolutions.items() if res.method == "source_alias"]
    scored = [name for name, res in resolutions.items() if res.method == "scored_ambiguity"]
    lines = [
        "Static ground item dump import",
        "",
        f"source_json: {source_path.relative_to(ROOT)}",
        f"source_json_sha256: {file_sha256(source_path)}",
        f"cache: {cache_path}",
        f"items_bin_validation: {items_bin.relative_to(ROOT)}",
        f"output_tsv: {OUT.relative_to(ROOT)}",
        f"item_names: {len(resolutions)}",
        f"spawn_rows: {len(rows)}",
        f"exact_name_resolutions: {len(exact)}",
        f"source_alias_resolutions: {len(aliases)}",
        f"scored_ambiguity_resolutions: {len(scored)}",
        "",
        "resolution_policy:",
        "  - source aliases are used only for raw wiki-title keys whose cache display name is generic",
        "  - noted/template/placeholder item forms are excluded",
        "  - ambiguous names prefer takeable/drop inventory forms, tradeable common forms, non-member forms, noteable base forms, then lowest b237 id",
        "",
        "source aliases:",
    ]
    for name in sorted(aliases):
        res = resolutions[name]
        lines.append(f"  {name} -> {res.item_id} ({cache_items[res.item_id]['name']})")
    lines.extend(["", "scored ambiguity sample:"])
    for name in sorted(scored)[:80]:
        res = resolutions[name]
        sample = ", ".join(str(item_id) for item_id in res.candidates[:8])
        lines.append(
            f"  {name} -> {res.item_id} ({cache_items[res.item_id]['name']}); candidates={sample}"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--output", type=Path, default=OUT)
    parser.add_argument("--report", type=Path, default=REPORT)
    parser.add_argument("--items-bin", type=Path, default=DEFAULT_ITEMS_BIN)
    parser.add_argument(
        "--cache",
        type=Path,
        default=Path(os.environ.get("RUNEC_B237_CACHE", DEFAULT_CACHE)),
        help="b237 OpenRS2 flat-file cache directory",
    )
    args = parser.parse_args()

    if not args.cache.is_dir():
        raise SystemExit(
            f"b237 cache not found: {args.cache}; pass --cache or set RUNEC_B237_CACHE"
        )

    source = load_source(args.source)
    cache_items = load_cache_item_defs(args.cache)
    runtime_ids = load_runtime_item_ids(args.items_bin)
    rows, resolutions = import_rows(source, cache_items, runtime_ids)
    write_tsv(args.output, rows)
    write_report(args.report, args.source, args.cache, args.items_bin, rows,
                 resolutions, cache_items)
    print(f"wrote {args.output} rows={len(rows)} item_names={len(resolutions)}")
    print(f"wrote {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
