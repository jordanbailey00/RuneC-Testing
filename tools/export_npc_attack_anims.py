#!/usr/bin/env python3
"""Validate and export RuneC-owned NPC stance-to-attack animations."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from content_paths import content_read_path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = content_read_path("npc_attack_anims/animations.tsv")
DEFAULT_OUTPUT = ROOT / "data/defs/npc_attack_anims.tsv"
DEFAULT_REPORT = ROOT / "tools/reports/npc_attack_anims.txt"
HEADER = "stand_anim|attack_anim|note"


def validate(path: Path) -> tuple[list[tuple[int, int, str]], list[str]]:
    failures: list[str] = []
    lines = path.read_text().splitlines()
    if not lines or lines[0] != HEADER:
        return [], ["header does not match NPC attack animation schema"]

    rows: list[tuple[int, int, str]] = []
    previous = -1
    for lineno, line in enumerate(lines[1:], 2):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        columns = line.split("|")
        if len(columns) != 3:
            failures.append(
                f"line {lineno}: expected 3 columns, got {len(columns)}"
            )
            continue
        try:
            stand_anim = int(columns[0])
            attack_anim = int(columns[1])
        except ValueError:
            failures.append(f"line {lineno}: animation IDs must be integers")
            continue
        if stand_anim < 0 or attack_anim <= 0 or attack_anim > 0xFFFF:
            failures.append(f"line {lineno}: invalid animation ID")
        if stand_anim <= previous:
            failures.append(
                f"line {lineno}: stand animations must be sorted and unique"
            )
        if not columns[2].startswith("curated:b237:"):
            failures.append(
                f"line {lineno}: note must identify curated B237 evidence"
            )
        previous = stand_anim
        rows.append((stand_anim, attack_anim, columns[2]))
    if not rows:
        failures.append("NPC attack animation table is empty")
    return rows, failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    args = parser.parse_args()

    rows, failures = validate(args.input)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        "NPC attack animation export\n\n"
        f"source: {args.input.relative_to(ROOT).as_posix()}\n"
        "authority: RuneC-owned reviewed B237 stance mapping\n"
        f"rows: {len(rows)}\n"
        f"validation failures: {len(failures)}\n"
        + "".join(f"  {failure}\n" for failure in failures)
    )
    if failures:
        print(f"NPC attack animation validation failed: {len(failures)}")
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.input, args.output)
    print(f"wrote {args.output} from {args.input} ({len(rows)} rows)")
    print(f"wrote {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
