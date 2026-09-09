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
