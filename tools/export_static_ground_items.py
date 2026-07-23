#!/usr/bin/env python3
"""Export static world ground-item spawns.

The source file is intentionally empty until an approved static-item corpus is
added. The exporter still writes a valid zero-row binary so runtime loading,
packing, and validation can exercise the path without hardcoded item spawns.
"""
from __future__ import annotations

import csv
from pathlib import Path

from spawn_index import write_ground_item_spawns

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "content/world/static_ground_items.tsv"
OUT = ROOT / "data/spawns/world.ground-items.indexed.bin"
REPORT = ROOT / "tools/reports/static_ground_items.txt"

def parse_rows() -> list[dict[str, int | str]]:
    rows: list[dict[str, int | str]] = []
    if not SOURCE.is_file():
        return rows
    with SOURCE.open(newline="", encoding="utf-8") as f:
        filtered = (line for line in f if line.strip() and not line.startswith("#"))
        reader = csv.DictReader(filtered, delimiter="\t")
        if not reader.fieldnames:
            return rows
        required = {"item_id", "quantity", "x", "y", "plane"}
        missing = required - set(reader.fieldnames)
        if missing:
            raise ValueError(f"{SOURCE}: missing columns: {sorted(missing)}")
        for line_no, row in enumerate(reader, start=2):
            item_id = int(row["item_id"])
            quantity = int(row["quantity"])
            x = int(row["x"])
            y = int(row["y"])
            plane = int(row["plane"])
            if item_id < 0 or quantity <= 0 or plane < 0 or plane > 3:
                raise ValueError(f"{SOURCE}:{line_no}: invalid static item row")
            rows.append({
                "item_id": item_id,
                "quantity": quantity,
                "x": x,
                "y": y,
                "plane": plane,
                "source": row.get("source", ""),
                "note": row.get("note", ""),
            })
    return rows


def write_binary(rows: list[dict[str, int | str]]) -> None:
    indexed_rows = [
        (
            int(row["item_id"]),
            int(row["quantity"]),
            int(row["x"]),
            int(row["y"]),
            int(row["plane"]),
            0,
        )
        for row in rows
    ]
    write_ground_item_spawns(OUT, indexed_rows)


def write_report(rows: list[dict[str, int | str]]) -> None:
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "Static Ground Items",
        "",
        f"source: {SOURCE.relative_to(ROOT)}",
        "authority: runec_owned_static_ground_item_source",
        f"rows: {len(rows)}",
        f"output: {OUT.relative_to(ROOT)}",
    ]
    if not rows:
        lines.extend([
            "",
            "status: EMPTY_SOURCE",
            "note: no approved static world ground-item corpus is installed yet",
        ])
    REPORT.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    rows = parse_rows()
    write_binary(rows)
    write_report(rows)
    print(f"wrote {OUT} rows={len(rows)}")
    print(f"wrote {REPORT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
