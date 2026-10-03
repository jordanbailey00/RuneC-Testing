#!/usr/bin/env python3
"""Audit loot source evidence without altering runtime rewards.

Use --fetch with a new --snapshot directory to obtain fresh, paced Wiki data.
Without --fetch, all source files must already exist; there is no network or
legacy-data fallback. A parseable rate is evidence, not approval of roll groups.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from datetime import datetime, timezone
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import struct
import mwparserfromhell

from export_normalization import parse_items, parse_npcs
from wiki_bucket import BucketClient

ROOT = Path(__file__).resolve().parents[1]
BUCKETS = {
    "dropsline": ["page_name", "item_name", "drop_json", "rare_drop_table"],
    "infobox_monster": ["page_name", "name", "id"],
    "infobox_item": ["page_name", "item_name", "item_id"],
}
UNPUBLISHED_LABELS = {"", "unknown", "common", "uncommon", "rare", "very rare"}


def probability(raw: str, approximate: bool = False) -> Fraction:
    text = raw.strip().replace(",", "")
    if approximate:
        raise ValueError("source rate is an estimate, not an exact probability")
    if text.lower() == "always":
        return Fraction(1)
    if not re.fullmatch(r"\d+(?:\.\d+)?/\d+(?:\.\d+)?", text):
        raise ValueError("source does not provide an exact probability")
    numerator, denominator = text.split("/")
    if Fraction(denominator) <= 0:
        raise ValueError("invalid source denominator")
    value = Fraction(numerator) / Fraction(denominator)
    if not 0 < value <= 1:
        raise ValueError("source probability is outside (0, 1]")
    return value


def quantity(raw: str) -> tuple[int, int, bool]:
    text = str(raw).strip().replace(",", "")
    match = re.fullmatch(r"(\d+)(?:\s*[-\u2013\u2014]\s*(\d+))?\s*(\(noted\))?", text)
    if not match:
        raise ValueError("quantity has unresolved alternatives or conditions")
    low, high = int(match[1]), int(match[2] or match[1])
    if not 0 < low <= high <= 2147483647:
        raise ValueError("quantity is outside the runtime's signed 32-bit bounds")
    return low, high, bool(match[3])


def wiki_ids(value) -> list[int]:
    values = value if isinstance(value, list) else [value]
    return [int(v) for v in values if str(v).isdigit()]


def old_table_ids(path: Path) -> set[int]:
    payload = path.read_bytes()
    magic, version, count = struct.unpack_from("<III", payload)
    if (magic, version) != (0x504F5244, 1):
        raise ValueError("verification input must be the pre-migration DROP v1 snapshot")
    offset, ids = 12, set()
    for _ in range(count):
        npc, = struct.unpack_from("<I", payload, offset)
        ids.add(npc)
        offset += 4
        for kind in range(3):
            size = payload[offset]
            offset += 1 + size * (12 if kind else 8)
        offset += 4
    if offset != len(payload) or len(ids) != count:
        raise ValueError("invalid or duplicate legacy drop-table records")
    return ids


def fetch(snapshot: Path) -> None:
    if snapshot.exists() and any(snapshot.iterdir()):
        raise ValueError("--fetch requires a new snapshot directory; do not label cached data fresh")
    client = BucketClient(cache_dir=snapshot / "http")
    for name, fields in BUCKETS.items():
        rows = list(client.fetch(name, fields))
        (snapshot / (name + ".json")).write_text(json.dumps(rows, ensure_ascii=True, indent=2))
    npcs = parse_npcs()
    monsters = json.loads((snapshot / "infobox_monster.json").read_text())
    drop_pages = {row["page_name"].split("#")[0]
        for row in json.loads((snapshot / "dropsline.json").read_text())}
    titles = sorted({row["page_name"].split("#")[0] for row in monsters
        if any(npc in npcs for npc in wiki_ids(row.get("id")))} & drop_pages)
    pages = snapshot / "pages"
    pages.mkdir(exist_ok=True)
    for offset in range(0, len(titles), 25):
        data = client._get({"action": "query", "prop": "revisions",
            "rvprop": "ids|timestamp|content", "rvslots": "main",
            "titles": "|".join(titles[offset:offset + 25]), "redirects": 1})
        (pages / f"{offset:04}.json").write_text(json.dumps(data, ensure_ascii=True))


def review(snapshot: Path) -> dict:
    source = {name: json.loads((snapshot / (name + ".json")).read_text()) for name in BUCKETS}
    items, npcs = parse_items(), parse_npcs()
    legacy = old_table_ids(ROOT / "content/runtime_snapshots/data/defs/drops.bin")
    pages, item_ids = defaultdict(set), defaultdict(set)
    for row in source["infobox_monster"]:
        pages[row["page_name"].split("#")[0]].update(i for i in wiki_ids(row.get("id")) if i in npcs)
    for row in source["infobox_item"]:
        item_ids[row["page_name"]].update(i for i in wiki_ids(row.get("item_id"))
            if i in items and not items[i]["noted"] and not items[i]["placeholder"])

    revisions, obsolete = {}, set()
    for file in sorted((snapshot / "pages").glob("*.json")):
        for page in json.loads(file.read_text())["query"]["pages"]:
            if "revisions" not in page:
                continue
            revision = page["revisions"][0]
            revisions[page["title"]] = revision["revid"]
            if "{{obsolete" in revision["slots"]["main"]["content"].lower():
                obsolete.add(page["title"])

    results = {}
    totals = Counter()
    for row in source["dropsline"]:
        title = row["page_name"].split("#")[0]
        if not pages[title]:
            continue
        fact = json.loads(row["drop_json"])
        if fact.get("Drop type") != "combat":
            totals["non_combat_rows_excluded"] += 1
            continue
        page = results.setdefault(title, {
            "url": "https://oldschool.runescape.wiki/?oldid=" + str(revisions[title])
                if title in revisions else None,
            "revision_missing": title not in revisions,
            "npc_ids": sorted(pages[title]), "obsolete_notice": title in obsolete,
            "rows": 0, "issues": [], "exact_rate_rows": 0,
        })
        page["rows"] += 1
        totals["combat_rows"] += 1
        reasons = []
        try:
            probability(str(fact.get("Rarity", "")), bool(fact.get("Approx")))
            page["exact_rate_rows"] += 1
        except ValueError as error:
            reasons.append(str(error))
            totals["unconfirmed_rate_rows"] += 1
        name = row["item_name"]
        candidates = sorted(item_ids[name])
        if name != "Nothing" and len(candidates) != 1:
            reasons.append("no unambiguous Wiki-to-B237 item identity")
            totals["unresolved_item_rows"] += 1
        if name != "Nothing":
            try:
                low, high, noted = quantity(fact.get("Drop Quantity", ""))
                if high > 65535:
                    totals["requires_32_bit_quantity_rows"] += 1
                if noted:
                    totals["noted_rows"] += 1
                    if len(candidates) == 1 and items[candidates[0]]["linked_noted"] not in items:
                        reasons.append("Wiki noted choice has no B237 note link")
            except ValueError as error:
                reasons.append(str(error))
                totals["unresolved_quantity_rows"] += 1
        if reasons:
            page["issues"].append({"item": name, "rarity": fact.get("Rarity"),
                "quantity": fact.get("Drop Quantity"), "reasons": reasons})
    covered = {npc for page in results.values() for npc in page["npc_ids"]}
    totals["pages_missing_revision"] = sum(p["revision_missing"] for p in results.values())
    totals["pages_marked_obsolete"] = sum(p["obsolete_notice"] for p in results.values())
    return {
        "status": "NEEDS_REVIEW_NOT_RUNTIME_APPROVAL",
        "reviewed_at": datetime.now(timezone.utc).isoformat(),
        "scope": "NPC combat loot; excludes thieving, hunter and chest/activity rewards",
        "source_hashes": {name: hashlib.sha256((snapshot / (name + ".json")).read_bytes()).hexdigest() for name in BUCKETS},
        "revision_file_hashes": {file.name: hashlib.sha256(file.read_bytes()).hexdigest()
            for file in sorted((snapshot / "pages").glob("*.json"))},
        "source_rows": {name: len(rows) for name, rows in source.items()},
        "counts": dict(totals),
        "legacy_npcs_without_live_combat_source": sorted(legacy - covered),
        "live_source_npcs_missing_legacy_table": sorted(covered - legacy),
        "remaining_review": ["mutually exclusive versus independent groups", "conditional eligibility and variants",
            "shared-table branches", "approximate/unpublished rates", "B237 identity and semantic reconciliation"],
        "pages": dict(sorted(results.items())),
    }


def rejection_inventory(snapshot: Path, report: dict) -> dict:
    """Keep definite source blockers separate from pending semantic review."""
    npcs = parse_npcs()
    identities = defaultdict(set)
    for row in json.loads((snapshot / "infobox_monster.json").read_text()):
        title = row["page_name"].split("#")[0]
        name = str(mwparserfromhell.parse(str(row.get("name", ""))).strip_code()).strip().casefold()
        identities[title].update(i for i in wiki_ids(row.get("id"))
            if i in npcs and npcs[i]["name"].strip().casefold() == name)
    obsolete = defaultdict(list)
    for file in sorted((snapshot / "pages").glob("*.json")):
        for page in json.loads(file.read_text())["query"]["pages"]:
            if "revisions" not in page:
                continue
            code = mwparserfromhell.parse(page["revisions"][0]["slots"]["main"]["content"])
            for section in code.get_sections(flat=True):
                headings = section.filter_headings()
                heading = str(headings[0].title).strip().casefold() if headings else ""
                for template in section.filter_templates():
                    if str(template.name).strip().casefold() != "obsolete":
                        continue
                    reason = str(template.get(1).value).strip() if template.has(1) else "unspecified"
                    if "drop" in heading or "loot" in heading or "reward" in heading:
                        obsolete[page["title"]].append(reason)
    tables, binding_gaps = [], []
    for title, page in report["pages"].items():
        drops = [issue for issue in page["issues"]
            if str(issue["rarity"] or "").strip().casefold() in UNPUBLISHED_LABELS]
        if not drops and not obsolete[title]:
            continue
        ids = sorted(identities[title])
        unmatched = sorted(set(page["npc_ids"]) - set(ids))
        if unmatched:
            binding_gaps.append({"table": title, "npc_ids": unmatched,
                "reason": "Wiki ID exists in B237 but its NPC name did not match; no guessed binding"})
        if not page["url"]:
            raise ValueError(f"{title}: cannot publish a rejection without revision evidence")
        if not ids:
            continue
        reasons = (["unpublished_rate"] if drops else []) + (["outdated_drop_section"] if obsolete[title] else [])
        tables.append({"table": title, "npc_ids": ids, "source": page["url"],
            "reasons": reasons, "outdated_notes": obsolete[title],
            "drops": [{"item": d["item"], "rarity": d["rarity"], "quantity": d["quantity"]}
                      for d in drops]})
    return {"format": "runec-loot-rejections-v1", "snapshot": snapshot.name,
        "policy": "Reject the entire affected NPC death table before RNG or ground grants; no old-table fallback.",
        "scope": "Explicit unpublished-rate labels and outdated DROP sections. Numeric estimates, conditional rates, item/quantity bindings and roll groups still require review; absence here is NOT approval.",
        "source_hashes": report["source_hashes"], "tables": tables,
        "binding_gaps": binding_gaps}


def rejection_markdown(inventory: dict) -> str:
    tables = inventory["tables"]
    lines = ["# Rejected Loot Tables", "", f"Evidence snapshot: {inventory['snapshot']}.", "",
        inventory["policy"], "", inventory["scope"], "",
        f"{len(tables)} source tables; {len({i for t in tables for i in t['npc_ids']})} B237 NPC definitions.", "",
        "Each entry is an open TODO. Restore it only after exact odds, quantities,",
        "eligibility and roll grouping are confirmed and covered by tests.", ""]
    for table in tables:
        lines.extend([f"## {table['table']}", "",
            "- [ ] Verify and restore this table.",
            "- NPC IDs: " + ", ".join(map(str, table["npc_ids"])),
            f"- [Pinned source]({table['source']})",
            "- Blockers: " + ", ".join(table["reasons"])])
        if table["outdated_notes"]:
            lines.append("- Source marks its drops section outdated; recheck the complete distribution.")
        for drop in table["drops"]:
            lines.append(f"- {drop['item']}: rate `{drop['rarity'] or 'unpublished'}`, quantity `{drop['quantity'] or 'unpublished'}`.")
        lines.append("")
    if inventory["binding_gaps"]:
        lines.extend(["## Identity Review (Not Automatically Bound)", ""])
        for gap in inventory["binding_gaps"]:
            lines.append(f"- [ ] {gap['table']}: NPC IDs {gap['npc_ids']}. {gap['reason']}.")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "generated/loot-verification/review.json")
    parser.add_argument("--rejections", type=Path,
                        help="write the explicit rejection source and adjacent readable .txt revisit list")
    args = parser.parse_args()
    if args.fetch:
        fetch(args.snapshot)
    report = review(args.snapshot)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=True, indent=2) + "\n")
    if args.rejections:
        inventory = rejection_inventory(args.snapshot, report)
        args.rejections.parent.mkdir(parents=True, exist_ok=True)
        args.rejections.write_text(json.dumps(inventory, ensure_ascii=True, indent=2) + "\n")
        args.rejections.with_suffix(".txt").write_text(rejection_markdown(inventory))
    print(report["status"], len(report["pages"]), "NPC source pages")
    print(json.dumps(report["counts"], sort_keys=True))
    raise SystemExit("Verification incomplete: review conditional groups and every reported uncertainty. "
                     "Runtime tables were not modified or approved.")


if __name__ == "__main__":
    main()
