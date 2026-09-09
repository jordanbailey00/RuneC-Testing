import copy
import sys
import unittest
from tempfile import TemporaryDirectory
from pathlib import Path
from unittest.mock import patch

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/cache_pipeline"))
import export_item_sprites_b237 as sprites


def triangle():
    return sprites.ModelData(model_id=1, vertex_count=3, face_count=1,
        vertices_x=[-20, 0, 20], vertices_y=[-20, 20, -20], vertices_z=[0, 0, 0],
        face_a=[0], face_b=[1], face_c=[2], face_colors=[5000])


class ItemSprites(unittest.TestCase):
    def test_metadata_axes_signed_offsets_and_templates(self):
        d = sprites.decode_item_sprite_def(1, bytes.fromhex(
            "01 000a 04 0800 05 0100 06 0200 07 fffb 08 0007 5f 0300 "
            "61 0011 62 0012 8b 0013 8c 0014 94 0015 95 0016 00"))
        self.assertEqual((d.xan_2d, d.yan_2d, d.zan_2d), (256, 512, 768))
        self.assertEqual((d.offset_x_2d, d.offset_y_2d), (-5, 7))
        self.assertEqual((d.note_id, d.note_template_id, d.bought_id,
                          d.bought_template_id, d.placeholder_id,
                          d.placeholder_template_id), (17, 18, 19, 20, 21, 22))

    def test_projection_uses_client_camera_not_bounding_box_fit(self):
        model = triangle()
        item = sprites.ItemSpriteDef(1, zoom_2d=1024)
        np.testing.assert_array_equal(sprites.project_vertices(model, item),
            [[6, 11, 1024], [16, 31, 1024], [26, 11, 1024]])
        near = sprites.project_vertices(model, item)
        item.zoom_2d = 2048
        far = sprites.project_vertices(model, item)
        self.assertLess(np.ptp(far[:, 0]), np.ptp(near[:, 0]))
        item.offset_x_2d = 16
        shifted = sprites.project_vertices(model, item)
        np.testing.assert_array_equal(shifted[:, 0], far[:, 0] + 4)
        item.zoom_2d = 0
        with self.assertRaisesRegex(ValueError, "crosses the camera"):
            sprites.project_vertices(model, item)

    def test_backfaces_and_painter_order(self):
        m = triangle()
        m.face_count = 3
        m.face_a, m.face_b, m.face_c = [0, 3, 0], [1, 4, 2], [2, 5, 1]
        p = np.array([[0, 0, 100], [0, 10, 100], [10, 0, 100],
                      [0, 0, 200], [0, 10, 200], [10, 0, 200]])
        self.assertEqual(sprites.ordered_faces(m, p), [1, 0])
        m.face_priorities = [0, 1, 0]
        self.assertEqual(sprites.ordered_faces(m, p), [0, 1])
        m.face_priorities = [10, 11, 0]
        self.assertEqual(sprites.ordered_faces(m, p), [0, 1])
        m.face_priorities = [12, 0, 0]
        with self.assertRaisesRegex(ValueError, "invalid face priority"):
            sprites.ordered_faces(m, p)
        m.face_a, m.face_b, m.face_c = [3, 3, 3, 0, 0], [4, 4, 4, 1, 1], [5, 5, 5, 2, 2]
        m.face_priorities = [1, 3, 6, 10, 11]
        self.assertEqual(sprites.ordered_faces(m, p), [0, 1, 2, 3, 4])

    def test_native_lit_pixels_outline_shadow_and_texture(self):
        m = triangle()
        item = sprites.ItemSpriteDef(1, zoom_2d=1024)
        image = sprites.render_icon(item, m, {})
        self.assertEqual(image.size, (36, 32))
        self.assertIsNotNone(image.getbbox())
        self.assertEqual(set(np.unique(np.array(image)[:, :, 3])), {0, 255})
        outlined = np.array(sprites.outline_icon(image))
        self.assertTrue(np.any(np.all(outlined == [0, 0, 1, 255], axis=-1)))
        self.assertTrue(np.any(np.all(outlined == [48, 32, 32, 255], axis=-1)))
        np.testing.assert_array_equal(sprites.outline_icon(image, False, False), image)
        m.face_textures = [7]
        with self.assertRaisesRegex(ValueError, "missing texture 7"):
            sprites.render_icon(item, m, {})
        texture = np.full((2, 2, 4), [255, 0, 0, 255], dtype=np.uint8)
        texture[0, 1] = [0, 255, 0, 255]
        colored = np.array(sprites.render_icon(item, m, {7: texture}))
        self.assertTrue(np.any(colored[:, :, 0] > 0))
        self.assertTrue(np.any(colored[:, :, 1] > 0))
        self.assertTrue(np.all(colored[:, :, 2] == 0))
        m.face_alphas = [255]
        self.assertIsNone(sprites.render_icon(item, m, {}).getbbox())
        self.assertIsNone(sprites.render_icon(item, None, {}).getbbox())

    def test_raster_clips_and_blends_without_fabricating_pixels(self):
        canvas = np.zeros((32, 36, 4), dtype=np.uint8)
        points = np.array([[0, 0, 100], [0, 10, 100], [10, 0, 100]])
        sprites.raster_face(canvas, points, [(255, 0, 0, 255)] * 3)
        sprites.raster_face(canvas, points, [(0, 0, 255, 128)] * 3)
        self.assertEqual(tuple(canvas[1, 1]), (127, 0, 128, 255))
        before = canvas.copy()
        sprites.raster_face(canvas, points + 1000, [(0, 255, 0, 255)] * 3)
        sprites.raster_face(canvas, np.array([[1, 1, 100]] * 3), [(0, 255, 0, 255)] * 3)
        sprites.raster_face(canvas, np.array([[1, 1, 100], [1, 1.1, 100], [1.1, 1, 100]]),
                            [(0, 255, 0, 255)] * 3)
        np.testing.assert_array_equal(canvas, before)

    def test_export_failures_are_explicit_and_generated_assets_are_native(self):
        class Store:
            def __init__(self, _path): pass
            def read_group(self, *_args): return {1: b"\xff\x00"}
        with TemporaryDirectory() as tmp, patch.object(sprites, "RcCacheStore", Store):
            with self.assertRaisesRegex(SystemExit, "item sprite decode gaps"):
                sprites.main(["--cache", tmp, "--output", tmp])
            with patch.object(Store, "read_group", return_value={1: b"\x00"}), \
                 patch.object(sprites, "load_texture_sprites", return_value={}):
                with self.assertRaisesRegex(ValueError, "definition missing"):
                    sprites.main(["--cache", tmp, "--output", tmp, "--item-ids", "2"])
                self.assertEqual(sprites.main(["--cache", tmp, "--output", tmp, "--clean"]), 0)
                self.assertEqual(sprites.main(["--cache", tmp, "--output", tmp, "--clean"]), 0)
                self.assertTrue((Path(tmp) / "item_1.png").exists())
        # Bank gear, ammunition, runes, stack forms, and a banknote all have pixels.
        for item_id in (4151, 11802, 11803, 12926, 11212, 21326, 22481, 555, 560, 565, 995, 1004):
            with Image.open(ROOT / f"data/sprites/items/item_{item_id}.png") as icon:
                self.assertEqual(icon.size, (36, 32), item_id)
                self.assertIsNotNone(icon.getbbox(), item_id)

    def test_linked_note_bought_placeholder_and_stack_use_cache_models(self):
        defs = {1: sprites.ItemSpriteDef(1, inventory_model=1, zoom_2d=1024),
                2: sprites.ItemSpriteDef(2, inventory_model=2, zoom_2d=2048),
                3: sprites.ItemSpriteDef(3)}
        model = triangle()
        loader = lambda _: model
        ordinary = sprites.render_item_icon(1, defs, loader, {})
        defs[3].note_id, defs[3].note_template_id = 1, 2
        note = sprites.render_item_icon(3, defs, loader, {})
        self.assertIsNotNone(note.getbbox())
        self.assertNotEqual(note.tobytes(), ordinary.tobytes())
        for kind in ("bought", "placeholder"):
            defs[3] = sprites.ItemSpriteDef(3)
            setattr(defs[3], kind + "_id", 1)
            setattr(defs[3], kind + "_template_id", 2)
            self.assertIsNotNone(sprites.render_item_icon(3, defs, loader, {}).getbbox())
        defs[1].count_obj, defs[1].count_amt = [2], [10]
        self.assertEqual(sprites.render_item_icon(1, defs, loader, {}, quantity=10).tobytes(),
                         sprites.render_item_icon(2, defs, loader, {}).tobytes())
        self.assertEqual(model.vertices_x, [-20, 0, 20])
        defs[3].placeholder_id = 3
        with self.assertRaisesRegex(ValueError, "cyclic icon template"):
            sprites.render_item_icon(3, defs, loader, {})
        with self.assertRaisesRegex(ValueError, "missing inventory model"):
            sprites.render_item_icon(1, defs, lambda _: None, {})
        with self.assertRaises(KeyError):
            sprites.render_item_icon(999, defs, loader, {})

    def test_recolor_retexture_and_resize_do_not_modify_cached_model(self):
        original = triangle()
        original.face_textures = [1]
        model = copy.deepcopy(original)
        item = sprites.ItemSpriteDef(1, resize_x=64, resize_y=256, resize_z=32,
            recolor_src=[5000], recolor_dst=[6000], retexture_src=[1], retexture_dst=[2])
        sprites.apply_item_overrides(model, item)
        self.assertEqual(model.face_colors, [6000])
        self.assertEqual(model.face_textures, [2])
        self.assertEqual(model.vertices_x, [-10, 0, 10])
        self.assertEqual(model.vertices_y, [-40, 40, -40])
        self.assertEqual(original.face_colors, [5000])


if __name__ == "__main__":
    unittest.main()
