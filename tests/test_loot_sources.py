from fractions import Fraction
import json
from pathlib import Path
import sys
import struct
from copy import deepcopy
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_loot_sources import probability, quantity, review, fetch, rejection_inventory, rejection_markdown
from compile_loot_rejections import compile_rejections, install_rejections, repair_tables, REPAIRS
from export_normalization import parse_npcs, parse_items
from audit_npc_reconciliation import drop_table_ids
import export_runtime_snapshots


class LootSourceTests(unittest.TestCase):
    def test_exact_probabilities_are_not_rounded_to_inverse_rarity(self):
        self.assertEqual(probability("21/128"), Fraction(21, 128))
        self.assertEqual(probability("1/5,012.5"), Fraction(2, 10025))
        self.assertEqual(probability("Always"), Fraction(1))

    def test_unpublished_and_estimated_rates_fail_openly(self):
        for value in ("Unknown", "Common", "Varies", "1/0", "129/128", "1/128 (on task)"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                probability(value)
        with self.assertRaisesRegex(ValueError, "estimate"):
            probability("1/128", approximate=True)

    def test_notes_and_large_quantities_are_preserved(self):
        self.assertEqual(quantity("150 (noted)"), (150, 150, True))
        self.assertEqual(quantity("20,000-81,000"), (20000, 81000, False))
        self.assertEqual(quantity("1"), (1, 1, False))

    def test_unknown_or_compound_quantities_are_not_guessed(self):
        for value in ("Varies", "1; 1 (noted)", "0", "3-2", "2147483648", "1/2"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                quantity(value)

    def test_review_does_not_approve_missing_revisions_or_noncombat_rows(self):
        source = {
            "infobox_monster": [{"page_name": "Example", "id": [1]}],
            "infobox_item": [{"page_name": "Coins", "item_id": [995]}],
            "dropsline": [
                {"page_name": "Example", "item_name": "Coins", "drop_json": json.dumps({
                    "Drop type": "combat", "Rarity": "21/128", "Drop Quantity": "100000"})},
                {"page_name": "Example", "item_name": "Coins", "drop_json": json.dumps({
                    "Drop type": "pickpocket", "Rarity": "Always", "Drop Quantity": "1"})},
            ],
        }
        items = {995: {"noted": False, "placeholder": False, "linked_noted": -1}}
        with TemporaryDirectory() as tmp, patch("verify_loot_sources.parse_items", return_value=items), \
                patch("verify_loot_sources.parse_npcs", return_value={1: {}}), \
                patch("verify_loot_sources.old_table_ids", return_value={1, 2}):
            root = Path(tmp)
            for name, rows in source.items():
                (root / (name + ".json")).write_text(json.dumps(rows))
            result = review(root)
            self.assertEqual(result["status"], "NEEDS_REVIEW_NOT_RUNTIME_APPROVAL")
            self.assertEqual(result["counts"]["combat_rows"], 1)
            self.assertEqual(result["counts"]["non_combat_rows_excluded"], 1)
            self.assertEqual(result["counts"]["requires_32_bit_quantity_rows"], 1)
            self.assertEqual(result["counts"]["pages_missing_revision"], 1)
            self.assertIsNone(result["pages"]["Example"]["url"])
            self.assertEqual(result["legacy_npcs_without_live_combat_source"], [2])
            (root / "pages").mkdir()
            (root / "pages" / "0000.json").write_text(json.dumps({"query": {"pages": [{
                "title": "Example", "revisions": [{"revid": 123, "slots": {"main": {
                    "content": "{{Obsolete|changed rewards}}"}}}]}]}}))
            result = review(root)
            self.assertEqual(result["counts"]["pages_missing_revision"], 0)
            self.assertEqual(result["counts"]["pages_marked_obsolete"], 1)
            self.assertEqual(result["pages"]["Example"]["url"],
                             "https://oldschool.runescape.wiki/?oldid=123")
            self.assertEqual(len(result["revision_file_hashes"]["0000.json"]), 64)

    def test_fetch_rejects_existing_evidence_before_network_access(self):
        with TemporaryDirectory() as tmp, patch("verify_loot_sources.BucketClient") as client:
            root = Path(tmp)
            (root / "partial.json").write_text("[]")
            with self.assertRaisesRegex(ValueError, "new snapshot"):
                fetch(root)
            client.assert_not_called()

    def test_rejection_inventory_distinguishes_loot_from_mechanics_and_id_collisions(self):
        monsters = [{"page_name": name, "name": name, "id": [i]}
                    for i, name in enumerate(("Loot", "Mechanic", "Other", "Binding"), 1)]
        pages = [{"title": name, "revisions": [{"revid": i, "slots": {"main": {
            "content": f"=={heading}==\n{{{{Obsolete|update}}}}"}}}]}
            for i, name, heading in ((1, "Loot", "Drops"), (2, "Mechanic", "Mechanics"))]
        report = {"source_hashes": {}, "pages": {
            name: {"npc_ids": [i], "url": f"https://oldschool.runescape.wiki/?oldid={i}",
                   "issues": ([{"item": "Coins", "rarity": "Unknown", "quantity": "1"}]
                              if i >= 3 else [])}
            for i, name in enumerate(("Loot", "Mechanic", "Other", "Binding"), 1)}}
        npcs = {1: {"name": "Loot"}, 2: {"name": "Mechanic"},
                3: {"name": "Other"}, 4: {"name": "Different NPC"}}
        with TemporaryDirectory() as tmp, patch("verify_loot_sources.parse_npcs", return_value=npcs):
            root = Path(tmp)
            (root / "infobox_monster.json").write_text(json.dumps(monsters))
            (root / "pages").mkdir()
            (root / "pages/0000.json").write_text(json.dumps({"query": {"pages": pages}}))
            inventory = rejection_inventory(root, report)
            self.assertEqual([t["table"] for t in inventory["tables"]], ["Loot", "Other"])
            self.assertEqual(inventory["tables"][0]["reasons"], ["outdated_drop_section"])
            self.assertEqual(inventory["tables"][1]["reasons"], ["unpublished_rate"])
            self.assertEqual(inventory["binding_gaps"][0]["npc_ids"], [4])
            text = rejection_markdown(inventory)
            self.assertIn("Coins: rate `Unknown`", text)
            self.assertIn("NPC IDs: 3", text)
            report["pages"]["Other"]["url"] = None
            with self.assertRaisesRegex(ValueError, "revision evidence"):
                rejection_inventory(root, report)

    @staticmethod
    def rejection_fixture():
        return {"format": "runec-loot-rejections-v1", "tables": [{
            "npc_ids": [1, 2], "reasons": ["unpublished_rate"],
            "source": "https://oldschool.runescape.wiki/?oldid=123"}]}

    def test_compiler_removes_legacy_rewards_and_adds_missing_rejection_records(self):
        # Even a legacy guaranteed reward must not survive a table rejection.
        old = (struct.pack("<IIII", 0x504F5244, 1, 1, 1)
               + struct.pack("<BIHHBBI", 1, 995, 100, 100, 0, 0, 0))
        result = compile_rejections(old, self.rejection_fixture())
        self.assertEqual(result, struct.pack("<III", 0x504F5244, 2, 2)
            + struct.pack("<II", 1, 1) + bytes(7)
            + struct.pack("<II", 2, 1) + bytes(7))
        with self.assertRaisesRegex(ValueError, "owned DROP v1"):
            compile_rejections(result, self.rejection_fixture())
        for bad in (old[:-1], old + b"x"):
            with self.assertRaises(ValueError):
                compile_rejections(bad, self.rejection_fixture())
        fixture = self.rejection_fixture()
        fixture["tables"][0]["reasons"] = ["guess"]
        with self.assertRaisesRegex(ValueError, "unknown"):
            compile_rejections(old, fixture)

    def test_production_rebuild_installs_rejections_not_raw_legacy_snapshot(self):
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            with patch.object(export_runtime_snapshots, "ROOT", root), \
                    patch.object(export_runtime_snapshots, "REPORTS", root / "reports"), \
                    patch.object(sys, "argv", ["export_runtime_snapshots.py", "--paths", "data/defs/drops.bin"]):
                self.assertEqual(export_runtime_snapshots.main(), 0)
            payload = (root / "data/defs/drops.bin").read_bytes()
            self.assertEqual(struct.unpack_from("<II", payload), (0x504F5244, 2))
            self.assertIn("NOT_VERIFIED", (root / "reports/drops.txt").read_text())
            ids = drop_table_ids(root / "data/defs/drops.bin")
            self.assertNotIn(414, ids)
            self.assertNotIn(3106, ids)
            self.assertIn(7416, ids)
            self.assertIn(3010, ids)
            self.assertIn(3011, ids)
            self.assertIn(2642, ids)

    def test_repair_bindings_and_note_links_match_b237(self):
        repairs = json.loads(REPAIRS.read_text())
        npcs, items = parse_npcs(), parse_items()
        for table in repairs["tables"]:
            for npc in table["npc_ids"]:
                self.assertEqual(npcs[npc]["name"], table["name"])
            for old, new in table["item_ids"].items():
                self.assertEqual(items[int(old)]["name"], items[new]["name"])
                if items[new]["noted"]:
                    self.assertEqual(items[int(old)]["linked_noted"], new)
        self.assertNotIn(2266, repairs["tables"][1]["npc_ids"])

    def test_install_rejects_identity_and_note_link_mismatches(self):
        repairs = {"tables": [{"name": "Guard", "npc_ids": [3010], "item_ids": {"1515": 1516}}]}
        items = {1515: {"name": "Yew logs", "linked_noted": 1516},
                 1516: {"name": "Yew logs", "noted": True}}
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "repairs.json"
            path.write_text(json.dumps(repairs))
            for npcs, data, message in (({}, items, "NPC identity"),
                    ({3010: {"name": "Guard"}}, {}, "item identity"),
                    ({3010: {"name": "Guard"}}, items, "note link")):
                if message == "note link":
                    data = deepcopy(data)
                    data[1515]["linked_noted"] = 0
                with patch("compile_loot_rejections.REPAIRS", path), \
                        patch("compile_loot_rejections.parse_npcs", return_value=npcs), \
                        patch("compile_loot_rejections.parse_items", return_value=data), \
                        self.assertRaisesRegex(ValueError, message):
                    install_rejections(Path(tmp) / "source.bin", Path(tmp) / "target.bin")
                self.assertFalse((Path(tmp) / "target.bin").exists())

    def test_repairs_preserve_rejection_priority_and_fail_on_source_drift(self):
        source = (struct.pack("<B", 3) + struct.pack("<IHH", 617, 1, 1) * 2
                  + struct.pack("<IHH", 526, 1, 1) + bytes(6))
        table = {"name": "Example", "source_table": 1, "npc_ids": [2],
                 "source": "https://oldschool.runescape.wiki/?oldid=1",
                 "item_ids": {"617": 995}, "remove": [[[526, 1, 1]], [], []],
                 "add_tertiary": [[556, 1, 1, 128]]}
        repairs = {"format": "runec-loot-runtime-repairs-v1", "tables": [table]}
        rows = {1: source}
        repair_tables(rows, repairs)
        self.assertEqual(rows[1], source)
        self.assertEqual(rows[2], struct.pack("<BIHHBBIHHII", 1, 995, 1, 1,
            0, 1, 556, 1, 1, 128, 0))
        old = struct.pack("<IIII", 0x504F5244, 1, 1, 1) + source
        output = compile_rejections(old, self.rejection_fixture(), repairs)
        self.assertEqual(output, struct.pack("<III", 0x504F5244, 2, 2)
            + struct.pack("<II", 1, 1) + bytes(7)
            + struct.pack("<II", 2, 1) + bytes(7))
        with self.assertRaisesRegex(ValueError, "manifest"):
            repair_tables(rows, {"format": "wrong"})
        bad = deepcopy(repairs)
        bad["tables"][0]["source"] = "unversioned"
        with self.assertRaisesRegex(ValueError, "evidence"):
            repair_tables(rows, bad)
        bad = deepcopy(repairs)
        bad["tables"][0]["remove"][0] = [[123, 1, 1]]
        with self.assertRaisesRegex(ValueError, "no longer matches"):
            repair_tables(rows, bad)
        bad = deepcopy(repairs)
        bad["tables"][0]["npc_ids"] = [20000]
        with self.assertRaisesRegex(ValueError, "NPC ID"):
            repair_tables(rows, bad)
        bad = deepcopy(repairs)
        bad["tables"][0]["add_tertiary"] *= 256
        with self.assertRaisesRegex(ValueError, "capacity"):
            repair_tables(rows, bad)

    def test_compiler_rejects_bad_manifests_and_incomplete_or_duplicate_snapshots(self):
        header = struct.pack("<III", 0x504F5244, 1, 1)
        record = struct.pack("<I", 1) + bytes(7)
        for payload in (header, header + record[:4],
                        struct.pack("<III", 0x504F5244, 1, 2) + record * 2):
            with self.assertRaises(ValueError):
                compile_rejections(payload, self.rejection_fixture())
        fixture = self.rejection_fixture()
        for field, value in (("npc_ids", []), ("npc_ids", [20000]),
                             ("reasons", []), ("source", "unversioned")):
            invalid = deepcopy(fixture)
            invalid["tables"][0][field] = value
            with self.assertRaises(ValueError):
                compile_rejections(header + record, invalid)
        with self.assertRaisesRegex(ValueError, "manifest"):
            compile_rejections(header + record, {"format": "wrong"})
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, target = root / "old.bin", root / "installed.bin"
            source.write_bytes(header)
            target.write_bytes(b"existing install")
            with self.assertRaises(ValueError):
                install_rejections(source, target)
            self.assertEqual(target.read_bytes(), b"existing install")


if __name__ == "__main__":
    unittest.main()
