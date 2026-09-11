"""Reviewed weapon updates preserve all unrelated installed item records."""
import struct
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import export_items


class WeaponMetadataTests(unittest.TestCase):
    def test_reviewed_family_ranges(self):
        vectors = [("Oak longbow", "bow", 10), ("Magic comp bow", "bow", 10),
                   ("Magic shortbow", "bow", 7), ("Dark bow", "bow", 10),
                   ("Rune knife", "thrown", 4), ("Black chinchompa", "chinchompas", 9),
                   ("Heavy ballista", "crossbow", 9), ("Trident of the seas", "powered_staff", 7),
                   ("Tumeken's shadow", "powered_staff", 8),
                   ("Crossbow", "crossbow", 10), ("Phoenix crossbow", "crossbow", 5),
                   ("Hunters' crossbow", "crossbow", 8),
                   ("Hunters' sunlight crossbow", "crossbow", 8),
                   ("Dragon hunter crossbow", "crossbow", 7),
                   ("Armadyl crossbow", "crossbow", 8), ("Dorgeshuun crossbow", "crossbow", 6),
                   ("Dragon halberd", "polearm", 2), ("Unreviewed bow", "bow", -1)]
        for name, kind, expected in vectors:
            with self.subTest(name=name):
                self.assertEqual(export_items.reviewed_attack_range(
                    name, export_items.WEAPON_TYPES[kind], -1), expected)

    def test_range_only_repair_and_mode_limits(self):
        pairs = [(20997, "Twisted bow", "bow", 10),
                 (806, "Bronze dart", "thrown", 3),
                 (10148, "Black salamander", "salamander", 1),
                 (1409, "Iban's staff", "staff", 1)]
        records = []
        for item, name, kind, expected in pairs:
            wp = {"weapon_type": kind, "attack_range": -1}
            record = bytearray(export_items.build_record({"id": item, "name": name, "weapon": wp}, {}))
            self.assertEqual(record[-1], expected)
            record[-1] = 255
            records.append(record)
        original = struct.pack("<III", export_items.IDEF_MAGIC, 3, len(records))
        original += b"".join(struct.pack("<I", len(r)) + r for r in records)
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "items.bin"
            path.write_bytes(original)
            self.assertEqual(export_items.update_installed_weapon_ranges(path), 4)
            result = path.read_bytes()
            self.assertEqual(sum(a != b for a, b in zip(original, result)), 4)
            self.assertEqual(export_items.update_installed_weapon_ranges(path), 0)
            malformed_records = []
            for size in (8, 80, len(records[0]) - 1):
                malformed_records.append(struct.pack("<IIII", export_items.IDEF_MAGIC, 3, 1, size)
                                         + records[0][:size])
            for malformed in (b"", original[:12], original[:-1], original + b"extra", *malformed_records):
                path.write_bytes(malformed)
                with self.assertRaises(ValueError):
                    export_items.update_installed_weapon_ranges(path)
                self.assertEqual(path.read_bytes(), malformed)

    def test_identity_is_checked(self):
        with self.assertRaisesRegex(ValueError, "identity"):
            export_items.build_record({"id": 28922, "name": "wrong item"}, {})

    def test_bounded_update_is_idempotent_and_preserves_other_records(self):
        cache = {i: {"id": i, "name": row["name"], "wearpos1": 3,
                     "interface_ops": [None, "Wield", "Check", "Charge", "Uncharge"]}
                 for i, row in export_items.WEAPON_METADATA.items()}
        unrelated = export_items.build_record({"id": 995, "name": "Coins"}, {})
        records = [unrelated] + [struct.pack("<I", i) + b"old data"
                                for i in cache]
        original = struct.pack("<III", export_items.IDEF_MAGIC, 3, len(records))
        original += b"".join(struct.pack("<I", len(r)) + r for r in records)
        with TemporaryDirectory() as tmp, \
                patch.object(export_items, "load_cache_item_defs", return_value=cache), \
                patch.object(export_items, "require_cache_dir", return_value=Path(tmp)):
            path = Path(tmp) / "items.bin"
            path.write_bytes(original)
            export_items.update_installed_weapon_metadata(path, Path(tmp))
            result = path.read_bytes()
            size, = struct.unpack_from("<I", result, 12)
            self.assertEqual(result[16:16 + size], unrelated)
            self.assertNotEqual(result, original)
            export_items.update_installed_weapon_metadata(path, Path(tmp))
            self.assertEqual(path.read_bytes(), result)
            duplicates = records + [records[-1]]
            duplicate_file = struct.pack("<III", export_items.IDEF_MAGIC, 3, len(duplicates))
            duplicate_file += b"".join(struct.pack("<I", len(r)) + r for r in duplicates)
            missing_file = struct.pack("<IIII", export_items.IDEF_MAGIC, 3, 1, len(unrelated))
            missing_file += unrelated
            bad_header = struct.pack("<III", 0, 3, len(records)) + original[12:]
            for malformed in (original[:-1], original + b"extra", duplicate_file,
                              missing_file, bad_header):
                path.write_bytes(malformed)
                with self.assertRaises(ValueError):
                    export_items.update_installed_weapon_metadata(path, Path(tmp))
                self.assertEqual(path.read_bytes(), malformed)


if __name__ == "__main__":
    unittest.main()
