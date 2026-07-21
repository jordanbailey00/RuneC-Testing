#!/usr/bin/env python3
"""Tests for aggregate and mapsquare-split visual export orchestration."""

from __future__ import annotations

import argparse
import io
import struct
import sys
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "cache_pipeline"))

import export_models
import export_objects
import export_scene_slice
import export_terrain
import pack_runtime_data
from export_textures import TextureAtlas


class MapsquareSceneTests(unittest.TestCase):
    def test_parsers_are_deterministic_and_validate_bounds(self) -> None:
        self.assertEqual(export_scene_slice.parse_planes("2,0,2"), [2, 0])
        self.assertEqual(
            export_scene_slice.plane_path(Path("scene"), 2, ".terrain"),
            Path("scene.p2.terrain"),
        )
        self.assertEqual(
            export_scene_slice.parse_regions("51,53 50,54 50,53 51,53"),
            [(50, 53), (51, 53), (50, 54)],
        )
        for raw in ("", "50", "256,53"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                export_scene_slice.parse_regions(raw)
        for raw in ("", "4"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                export_scene_slice.parse_planes(raw)

    def test_cli_rejects_ambiguous_or_incomplete_output_contracts(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            cache.mkdir()
            common = ["--cache", str(cache)]
            center = ["--center-x", "3213", "--center-y", "3428"]
            invalid = (
                [],
                ["--center-x", "3213"],
                [*center, "--regions", "50,53"],
                [*center, "--radius-regions", "-1"],
                [*center, "--planes", "4", "--output-prefix", str(root / "scene")],
                [*center, "--split-by-mapsquare"],
                [
                    *center,
                    "--split-by-mapsquare",
                    "--output-dir", str(root / "regions"),
                    "--output-prefix", str(root / "scene"),
                ],
                center,
                [
                    *center,
                    "--output-prefix", str(root / "scene"),
                    "--output-dir", str(root / "regions"),
                ],
            )
            for args in invalid:
                with (
                    self.subTest(args=args),
                    redirect_stderr(io.StringIO()),
                    self.assertRaises(SystemExit) as raised,
                ):
                    export_scene_slice.main([*common, *args])
                self.assertEqual(raised.exception.code, 2)

    def test_split_exporters_validate_inputs_before_cache_decode(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            for exporter in (
                export_terrain.export_modern_terrain_split,
                export_objects.export_modern_objects_split,
            ):
                with self.subTest(exporter=exporter.__name__, case="cache"):
                    with self.assertRaises(FileNotFoundError):
                        exporter(cache, [(50, 53)], output)

            cache.mkdir()
            for exporter in (
                export_terrain.export_modern_terrain_split,
                export_objects.export_modern_objects_split,
            ):
                with self.subTest(exporter=exporter.__name__, case="regions"):
                    with self.assertRaises(ValueError):
                        exporter(cache, [], output)
                with self.subTest(exporter=exporter.__name__, case="planes"):
                    with self.assertRaises(ValueError):
                        exporter(cache, [(50, 53)], output, [])

    def test_split_mode_dispatches_one_bounded_batch(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            cache.mkdir()
            with (
                patch(
                    "export_scene_slice.write_mapsquare_catalog",
                    return_value={(50, 53), (51, 53)},
                ) as catalog_export,
                patch(
                    "export_scene_slice.export_modern_terrain_split",
                    return_value=[output / "50_53.p0.terrain"],
                ) as terrain_export,
                patch(
                    "export_scene_slice.export_modern_objects_split",
                    return_value=[output / "50_53.p0.objects"],
                ) as object_export,
            ):
                export_scene_slice.main([
                    "--cache", str(cache),
                    "--regions", "51,53 50,53",
                    "--planes", "0,2",
                    "--split-by-mapsquare",
                    "--output-dir", str(output),
                ])

            expected_regions = [(50, 53), (51, 53)]
            catalog_export.assert_called_once_with(cache, output)
            terrain_export.assert_called_once_with(
                cache, expected_regions, output, [0, 2]
            )
            object_export.assert_called_once_with(
                cache,
                expected_regions,
                output,
                [0, 2],
                rsmod_visual_levels=True,
            )

    def test_split_mode_skips_authoritative_void_mapsquares(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            cache.mkdir()
            present = {(28, 81), (29, 81)}
            with (
                patch(
                    "export_scene_slice.write_mapsquare_catalog",
                    return_value=present,
                ),
                patch(
                    "export_scene_slice.export_modern_terrain_split",
                    return_value=[],
                ) as terrain_export,
                patch(
                    "export_scene_slice.export_modern_objects_split",
                    return_value=[],
                ) as object_export,
            ):
                export_scene_slice.main([
                    "--cache", str(cache),
                    "--regions", "28,81 29,81 30,81",
                    "--planes", "0",
                    "--split-by-mapsquare",
                    "--output-dir", str(output),
                ])

            expected = [(28, 81), (29, 81)]
            terrain_export.assert_called_once_with(cache, expected, output, [0])
            object_export.assert_called_once_with(
                cache, expected, output, [0], rsmod_visual_levels=True)

    def test_mapsquare_catalog_is_sorted_and_marks_terrain_regions(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            cache.mkdir()
            entries = {
                1: argparse.Namespace(
                    region_x=30, region_y=81, has_terrain=False),
                2: argparse.Namespace(
                    region_x=29, region_y=81, has_terrain=True),
                3: argparse.Namespace(
                    region_x=28, region_y=80, has_terrain=True),
            }
            with patch(
                "export_scene_slice.find_all_map_region_files",
                return_value=entries,
            ):
                present = export_scene_slice.write_mapsquare_catalog(
                    cache, output)

            payload = (output / "mapsquare.catalog").read_bytes()
            magic, count = struct.unpack_from("<II", payload)
            coordinates = [
                struct.unpack_from("<BB", payload, 8 + index * 2)
                for index in range(count)
            ]
            self.assertEqual(magic, export_scene_slice.MAPSQUARE_CATALOG_MAGIC)
            self.assertEqual(coordinates, [(28, 80), (29, 81)])
            self.assertEqual(present, {(28, 80), (29, 81)})

    def test_split_mode_stops_cleanly_when_every_requested_region_is_void(
        self,
    ) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            output = root / "regions"
            cache.mkdir()
            with (
                patch(
                    "export_scene_slice.write_mapsquare_catalog",
                    return_value=set(),
                ),
                patch(
                    "export_scene_slice.export_modern_terrain_split",
                ) as terrain_export,
                patch(
                    "export_scene_slice.export_modern_objects_split",
                ) as object_export,
            ):
                export_scene_slice.main([
                    "--cache", str(cache),
                    "--regions", "30,81",
                    "--planes", "0",
                    "--split-by-mapsquare",
                    "--output-dir", str(output),
                ])

            terrain_export.assert_not_called()
            object_export.assert_not_called()

    def test_aggregate_mode_remains_available_as_dev_cache(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            cache = root / "cache"
            prefix = root / "scene"
            cache.mkdir()
            with (
                patch("export_scene_slice.export_modern_terrain") as terrain_export,
                patch("export_scene_slice.export_modern_objects") as object_export,
            ):
                export_scene_slice.main([
                    "--cache", str(cache),
                    "--center-x", "3213",
                    "--center-y", "3428",
                    "--radius-regions", "0",
                    "--planes", "0",
                    "--output-prefix", str(prefix),
                ])

            terrain_export.assert_called_once_with(
                cache_dir=cache,
                regions=[(50, 53)],
                output=prefix.with_suffix(".terrain"),
                scene_plane=0,
            )
            object_export.assert_called_once_with(
                cache_dir=cache,
                regions=[(50, 53)],
                output=prefix.with_suffix(".objects"),
                scene_plane=0,
                rsmod_visual_levels=True,
            )


class TerrainBoundaryTests(unittest.TestCase):
    @staticmethod
    def region(region_x: int, region_y: int) -> export_terrain.RegionTerrain:
        terrain = export_terrain.RegionTerrain(region_x=region_x, region_y=region_y)
        terrain.heights = [
            [[0 for _ in range(65)] for _ in range(65)] for _ in range(4)
        ]
        terrain.underlay_ids = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        terrain.overlay_ids = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        terrain.shapes = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        terrain.rotations = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        terrain.settings = [
            [[0 for _ in range(64)] for _ in range(64)] for _ in range(4)
        ]
        return terrain

    def test_underlay_blending_crosses_mapsquare_boundary(self) -> None:
        west = self.region(50, 53)
        east = self.region(51, 53)
        for x in range(64):
            for y in range(64):
                west.underlay_ids[0][x][y] = 1
                east.underlay_ids[0][x][y] = 2

        west_floor = export_terrain.FloorDef(floor_id=0, rgb=0x397A3D)
        east_floor = export_terrain.FloorDef(floor_id=1, rgb=0x3D597A)
        export_terrain._rgb_to_hsl(west_floor.rgb, west_floor)
        export_terrain._rgb_to_hsl(east_floor.rgb, east_floor)
        floors = {0: west_floor, 1: east_floor}

        isolated_west = export_terrain._lit_underlay_rgb(
            {(50, 53): west}, 50, 53, 0, 63, 32, floors, 96)
        isolated_east = export_terrain._lit_underlay_rgb(
            {(51, 53): east}, 51, 53, 0, 0, 32, floors, 96)
        context = {(50, 53): west, (51, 53): east}
        blended_west = export_terrain._lit_underlay_rgb(
            context, 50, 53, 0, 63, 32, floors, 96)
        blended_east = export_terrain._lit_underlay_rgb(
            context, 51, 53, 0, 0, 32, floors, 96)

        self.assertIsNotNone(isolated_west)
        self.assertIsNotNone(isolated_east)
        self.assertIsNotNone(blended_west)
        self.assertIsNotNone(blended_east)
        isolated_delta = sum(
            (a - b) ** 2 for a, b in zip(isolated_west, isolated_east)
        )
        blended_delta = sum(
            (a - b) ** 2 for a, b in zip(blended_west, blended_east)
        )
        self.assertLess(blended_delta, isolated_delta)

    def test_streamed_heightmap_contains_outer_corner_samples(self) -> None:
        local = self.region(50, 53)
        east = self.region(51, 53)
        north = self.region(50, 54)
        northeast = self.region(51, 54)
        for plane in range(4):
            for y in range(65):
                east.heights[plane][0][y] = -256
            for x in range(65):
                north.heights[plane][x][0] = -384
            northeast.heights[plane][0][0] = -512

        context = {
            (50, 53): local,
            (51, 53): east,
            (50, 54): north,
            (51, 54): northeast,
        }
        min_x, min_y, width, height, values = export_terrain.build_heightmap(
            {(50, 53): local}, context_regions=context)

        self.assertEqual((min_x, min_y), (3200, 3392))
        self.assertEqual((width, height), (65, 65))
        self.assertEqual(values[64 + 10 * width], 2.0)
        self.assertEqual(values[10 + 64 * width], 3.0)
        self.assertEqual(values[64 + 64 * width], 4.0)

    def test_streamed_heightmap_retains_local_edge_without_neighbor(self) -> None:
        local = self.region(50, 53)
        local.heights[0][64][10] = -128
        local.heights[0][10][64] = -256
        local.heights[0][64][64] = -384

        _, _, width, height, values = export_terrain.build_heightmap(
            {(50, 53): local})

        self.assertEqual((width, height), (65, 65))
        self.assertEqual(values[64 + 10 * width], 1.0)
        self.assertEqual(values[10 + 64 * width], 2.0)
        self.assertEqual(values[64 + 64 * width], 3.0)
        self.assertEqual(
            export_terrain._context_height(
                {(50, 53): local}, local, 50, 53, 0, -1, 10),
            local.heights[0][0][10],
        )


class SharedMaterialTests(unittest.TestCase):
    @staticmethod
    def atlas() -> TextureAtlas:
        return TextureAtlas(
            width=1,
            height=1,
            pixels=b"\xff\xff\xff\xff",
            white_u=0.5,
            white_v=0.5,
        )

    def test_model_writer_can_reference_an_external_shared_atlas(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "chunk.object_anim.models"
            export_models.write_models_binary(
                output,
                [],
                atlas=self.atlas(),
                write_atlas_companion=False,
            )
            magic, count = struct.unpack_from("<II", output.read_bytes())
            self.assertEqual(magic, export_models.MDL3_MAGIC)
            self.assertEqual(count, 0)
            self.assertFalse(output.with_suffix(".atlas").exists())

            companion_output = root / "default.models"
            export_models.write_models_binary(
                companion_output,
                [],
                atlas=self.atlas(),
            )
            self.assertTrue(companion_output.with_suffix(".atlas").is_file())

    def test_empty_chunk_keeps_oanim_contract_without_material_copy(self) -> None:
        with TemporaryDirectory() as tmp:
            output = Path(tmp) / "50_53.p3.objects"
            with (
                patch("export_objects.stitch_region_edges") as stitch_edges,
                patch("export_objects.build_heightmap") as build_heightmap,
            ):
                export_objects._build_and_write(
                    argparse.Namespace(output=output),
                    [],
                    {},
                    lambda _model_id: None,
                    {(50, 53): object()},
                    {},
                    self.atlas(),
                    prebuilt_heightmaps={0: (3200, 3392, 1, 1, [0.0])},
                    write_anim_atlas=False,
                    always_write_anim_sidecar=True,
                    verbose=False,
                )
                stitch_edges.assert_not_called()
                build_heightmap.assert_not_called()

            object_magic = struct.unpack_from("<I", output.read_bytes())[0]
            anim_magic, version, count = struct.unpack(
                "<III", output.with_suffix(".oanim").read_bytes()
            )
            self.assertEqual(object_magic, export_objects.OBJ2_MAGIC)
            self.assertEqual(anim_magic, export_objects.OANM_MAGIC)
            self.assertEqual(version, export_objects.OANM_VERSION)
            self.assertEqual(count, 0)
            self.assertFalse(output.with_suffix(".atlas").exists())
            self.assertFalse(output.with_suffix(".object_anim.atlas").exists())

    def test_packer_classifies_chunk_and_shared_material_files(self) -> None:
        names = (
            "50_53.p0.terrain",
            "50_53.p0.objects",
            "50_53.p0.oanim",
            "50_53.p0.object_anim.models",
            "mapsquare.materials.atlas",
            "mapsquare.materials.tanim",
            "mapsquare.catalog",
        )
        for name in names:
            with self.subTest(name=name):
                self.assertTrue(pack_runtime_data.is_region_runtime_file(Path(name)))


if __name__ == "__main__":
    unittest.main()
