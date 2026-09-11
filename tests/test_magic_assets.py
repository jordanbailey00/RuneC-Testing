import csv
import struct
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from export_spells import combat_effects, update_combat_effects
import export_spells
import export_runtime_snapshots
from frontend_validation.validate_combat_visuals import (
    read_anim_sequence_ids, read_model_ids, read_spotanims,
)


class MagicAssets(unittest.TestCase):
    def test_export_rejects_bad_costs_and_preserves_spell_identity(self):
        row = dict(name="Test spell", book=0, type=1, level=1, slv=0,
                   xp_q1=55, members=0, max_hit=2, effect_flags=1, runes=[(556, 1)])
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "spells.bin"
            export_spells.write_bin(path, export_spells.SPEL_MAGIC, [row])
            original = path.read_bytes()
            for change in (dict(name="x" * 64), dict(book=5), dict(type=8),
                           dict(level=0), dict(effect_flags=64), dict(runes=[(556, 0)]),
                           dict(runes=[(556, 1)] * 2), dict(runes=[(i, 1) for i in range(1, 10)])):
                with self.assertRaises(ValueError):
                    export_spells.write_bin(path, export_spells.SPEL_MAGIC, [dict(row, **change)])
                self.assertEqual(path.read_bytes(), original)
            with self.assertRaises(ValueError):
                export_spells.write_bin(path, export_spells.SPEL_MAGIC, [row, row])
            second = dict(row, name="A new spell")
            export_spells.write_bin(path, export_spells.SPEL_MAGIC, [second, row])
            rebuilt = path.read_bytes()
            self.assertEqual(rebuilt[12:len(original)], original[12:])
            export_spells.write_bin(path, export_spells.SPEL_MAGIC, [row, second])
            self.assertEqual(path.read_bytes(), rebuilt)
            for malformed in (b"", original[:12], original[:15], original[:-1], original + b"extra"):
                path.write_bytes(malformed)
                with self.assertRaises(ValueError):
                    export_spells.write_bin(path, export_spells.SPEL_MAGIC, [row])
                self.assertEqual(path.read_bytes(), malformed)
            for magic, rows in ((0, [row]), (export_spells.SPEL_MAGIC, []),
                                (export_spells.SPEL_MAGIC, [row] * 513)):
                with self.assertRaises(ValueError):
                    export_spells.write_bin(path, magic, rows)
            path.write_bytes(rebuilt)
            with self.assertRaisesRegex(ValueError, "missing installed"):
                export_spells.write_bin(path, export_spells.SPEL_MAGIC, [second])
            self.assertEqual(path.read_bytes(), rebuilt)
        for cost in ("<sup>1</sup>[[File:Unknown rune.png", "<sup>0</sup>[[File:Air rune.png",
                     "<sup>256</sup>[[File:Air rune.png", "not a recognized cost"):
            with self.assertRaises(ValueError):
                export_spells.parse_runes(cost, {"air rune": 556})
        self.assertEqual(export_spells.parse_runes("<sup>2</sup>[[File:Air rune.png", {"air rune": 556}), [(556, 2)])

    def test_ancient_casts_use_their_own_animation_families(self):
        with (ROOT / "content/combat_visuals/visuals.tsv").open() as stream:
            spells = {r["key"]: r for r in csv.DictReader(stream, delimiter="|")
                      if r["kind"] == "spell"}
        for element in ("Smoke", "Shadow", "Blood", "Ice"):
            for shape, sequence in (("Rush", 1978), ("Blitz", 1978),
                                    ("Burst", 1979), ("Barrage", 1979)):
                self.assertEqual(int(spells[f"{element} {shape}"]["attack_anim"]), sequence)
        self.assertEqual(int(spells["Fire Strike"]["attack_anim"]), 1162)

    def test_spell_producer_fails_openly_and_repair_cli_works(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "spells.bin"
            original = (ROOT / "data/defs/spells.bin").read_bytes()
            path.write_bytes(original)
            with patch.object(sys, "argv", ["export_spells.py", "--update-combat-effects", str(path)]):
                export_spells.main()
            self.assertEqual(path.read_bytes(), original)
            with patch.object(sys, "argv", ["export_spells.py"]), \
                 patch.object(export_spells, "load_bucket", return_value=[]):
                with self.assertRaisesRegex(ValueError, "no cached spell definitions"):
                    export_spells.main()
            table = Path(tmp) / "effects.tsv"
            for rows in ("Fire Blast|16|1\nFire Blast|16|1\n", "Fire Blast|16|0\n"):
                table.write_text("name|max_hit|effect_flags\n" + rows)
                with patch.object(export_spells, "content_read_path", return_value=table):
                    with self.assertRaisesRegex(ValueError, "invalid or duplicate combat spell"):
                        combat_effects()

    def test_production_snapshot_rebuild_applies_combat_values(self):
        source = export_runtime_snapshots.SNAPSHOT_ROOT / "data/defs/spells.bin"
        original = source.read_bytes()
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            with patch.object(export_runtime_snapshots, "ROOT", root), \
                 patch.object(export_runtime_snapshots, "REPORTS", root / "reports"), \
                 patch.object(sys, "argv", ["export_runtime_snapshots.py", "--paths",
                                            "data/defs/spells.bin", "data/defs/teleports.bin"]):
                self.assertEqual(export_runtime_snapshots.main(), 0)
            installed = root / "data/defs/spells.bin"
            self.assertEqual(installed.read_bytes(), (ROOT / "data/defs/spells.bin").read_bytes())
            self.assertNotEqual(installed.read_bytes(), original)
            report = (root / "reports/spells.txt").read_text()
            self.assertIn(export_runtime_snapshots.sha256_file(installed), report)
            self.assertIn("content/combat/spell_effects.tsv", report)
        self.assertEqual(source.read_bytes(), original)

    def test_repair_preserves_indices_requirements_and_unrelated_spells(self):
        original = (ROOT / "data/defs/spells.bin").read_bytes()
        damaged = bytearray(original)
        offset = 12
        duplicate = None
        for _ in range(struct.unpack_from("<I", original, 8)[0]):
            start = offset
            size = original[offset]
            name = original[offset + 1:offset + 1 + size].decode("latin-1")
            offset += 1 + size
            if name == "Fire Blast":
                struct.pack_into("<HH", damaged, offset + 7, 0, 0)
            offset += 12 + original[offset + 11] * 5
            if name == "Fire Blast":
                duplicate = original[start:offset]
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "spells.bin"
            path.write_bytes(damaged)
            self.assertEqual(update_combat_effects(path), 57)
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(update_combat_effects(path), 57)
            self.assertEqual(path.read_bytes(), original)
            duplicate_payload = struct.pack("<III", 0x4C455053, 2, 2) + duplicate * 2
            for payload in (b"", b"bad header!!!", original[:12], original[:12] + b"\xff",
                            original[:-1], original + b"extra", duplicate_payload,
                            struct.pack("<III", 0x4C455053, 2, 0)):
                path.write_bytes(payload)
                with self.assertRaises(ValueError):
                    update_combat_effects(path)
                self.assertEqual(path.read_bytes(), payload)
        self.assertEqual(combat_effects()["fire_blast"], (16, 1))

    def test_all_declared_player_effects_have_models_and_sequences(self):
        sequences = read_anim_sequence_ids(ROOT / "data/anims/all.anims")
        models = read_model_ids(ROOT / "data/models/projectiles.models")
        spots = read_spotanims(ROOT / "data/defs/spotanims.bin")
        with (ROOT / "content/combat_visuals/visuals.tsv").open() as stream:
            rows = list(csv.DictReader(stream, delimiter="|"))
        for row in rows:
            if row["kind"] not in ("item", "spell", "special"):
                continue
            with self.subTest(key=row["key"], kind=row["kind"], stance=row["stance_idx"]):
                for field in ("attack_anim", "projectile_anim", "aux_projectile_anim"):
                    if row[field] != "-":
                        self.assertTrue(int(row[field]) in sequences,
                                        f"missing {field} sequence {row[field]}")
                for field in ("launch_spotanim", "travel_spotanim", "impact_spotanim",
                              "double_launch_spotanim", "aux_travel_spotanim", "aux_impact_spotanim"):
                    if row[field] == "-":
                        continue
                    spot_id = int(row[field])
                    self.assertIn(spot_id, spots, field)
                    spot = spots[spot_id]
                    self.assertIn(spot.model_id, models, field)
                    if spot.animation_id >= 0:
                        self.assertIn(spot.animation_id, sequences, field)
        for subset in ("player", "npcs"):
            self.assertFalse(read_anim_sequence_ids(ROOT / f"data/anims/{subset}.anims") - sequences)


if __name__ == "__main__":
    unittest.main()
