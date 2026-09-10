import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/cache_pipeline"))
from export_prayer_icons import read_catalog
from export_sprites_modern import prayer_sprite_map
from validate_ui_assets import required_asset_names


class PrayerIconAssets(unittest.TestCase):
    def test_catalog_assets_and_export_mapping(self):
        rows = read_catalog()
        required = required_asset_names(ROOT / "rc-viewer/ui_assets.c")
        mapping = prayer_sprite_map()
        self.assertEqual(len(rows), 31)
        self.assertEqual(rows[18], (18, "Protect from Melee", 129, 149, "headicons_prayer_0"))
        self.assertEqual(rows[26], (26, "Deadeye", 1422, 1426, ""))
        names = {"prayer_glow"}
        for _, name, on, off, overhead in rows:
            with self.subTest(prayer=name):
                self.assertNotEqual(on, off)
                for sprite in (on, off):
                    self.assertIn(str(sprite), required)
                    self.assertIn(str(sprite), mapping[sprite])
                    names.add(str(sprite))
                if overhead:
                    self.assertIn(overhead, required)
                    names.add(overhead)
        for name in names:
            with self.subTest(sprite=name), Image.open(ROOT / f"data/sprites/ui/{name}.png") as image:
                self.assertEqual(image.format, "PNG")
                self.assertTrue(image.convert("RGBA").getchannel("A").getbbox(), "empty prayer sprite")

    def test_malformed_catalog_rejected(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "prayers.inc"
            for content, message in (("", "cover every"), ("PRAYER_ICON(bad)", "invalid")):
                path.write_text(content)
                with self.assertRaisesRegex(ValueError, message):
                    read_catalog(path)


if __name__ == "__main__":
    unittest.main()
