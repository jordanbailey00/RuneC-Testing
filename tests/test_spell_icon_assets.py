import contextlib
import io
import sys
import unittest
from collections import Counter
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/cache_pipeline"))
from export_spell_icons import read_catalog
import export_spell_icons as catalog
import export_sprites_modern as exporter
from validate_ui_assets import required_asset_names


class SpellIconAssets(unittest.TestCase):
    def test_all_four_books_have_required_exported_nonempty_icons(self):
        rows = read_catalog()
        self.assertEqual(Counter(book for book, _, _ in rows), {0: 76, 1: 27, 2: 46, 3: 46})
        self.assertEqual(len({(book, name) for book, name, _ in rows}), len(rows))
        ids = {(book, name): sprite for book, name, sprite in rows}
        for key, sprite in {(0, "Wind Strike"): 0, (1, "Ice Barrage"): 1895,
                            (2, "Vengeance"): 1961, (3, "Dark Lure"): 2992}.items():
            self.assertEqual(ids[key], sprite)
        required = required_asset_names(ROOT / "rc-viewer/ui_assets.c")
        mapping = exporter.build_sprite_map()
        for book, name, sprite in rows:
            with self.subTest(book=book, name=name):
                self.assertIn(str(sprite), required)
                self.assertIn(str(sprite), mapping[sprite])
                with Image.open(ROOT / f"data/sprites/ui/{sprite}.png") as image:
                    self.assertEqual(image.format, "PNG")
                    self.assertTrue(image.convert("RGBA").getchannel("A").getbbox(), "empty spell sprite")

    def test_bad_catalog_and_partial_sprite_export_fail_openly(self):
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "icons.inc"
            for content, error in (("", "empty"), ("SPELL_ICON(7, bad, 1)", "invalid")):
                path.write_text(content)
                with self.assertRaisesRegex(ValueError, error):
                    read_catalog(path)
            output, errors = io.StringIO(), io.StringIO()
            with patch.object(exporter, "select_reader", return_value=object()), \
                 patch.object(exporter, "build_sprite_map", return_value={0: ["0"], 1: ["1"]}), \
                 patch.object(exporter, "export_sprite", side_effect=[1, ValueError("missing sprite 1")]), \
                 contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                self.assertEqual(exporter.main(["--cache", tmp, "--output", tmp]), 1)
            self.assertIn("missing sprite 1", errors.getvalue())
            self.assertIn("1 failed", output.getvalue())
            obj = SimpleNamespace(symbol="test", fields={"param": ["param_601,Test", "param_599,icon"]})
            enums = {1982: SimpleNamespace(fields={"val": ["0,test", "1,test"]})}
            with patch.object(catalog, "parse_symbol_file", return_value={1: "icon"}), \
                 patch.object(catalog, "parse_dump_file", side_effect=[{1: obj}, enums]):
                with self.assertRaisesRegex(ValueError, "duplicate spell"):
                    catalog.spell_icons(Path(tmp))


if __name__ == "__main__":
    unittest.main()
