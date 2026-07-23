#!/usr/bin/env python3
"""Tests for reusable cache region selections and pipeline command planning."""

from __future__ import annotations

import sys
import unittest
from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import region_sets
import data_pipeline


class RegionSetTests(unittest.TestCase):
    def test_varrock_matches_existing_export_footprint(self) -> None:
        expected = [
            (region_x, region_y)
            for region_y in range(51, 56)
            for region_x in range(48, 53)
        ]
        self.assertEqual(region_sets.varrock(), expected)

    def test_edgeville_is_centered_on_named_tile(self) -> None:
        regions = region_sets.edgeville()
        self.assertEqual(len(regions), 25)
        self.assertEqual(regions[0], (46, 52))
        self.assertEqual(regions[-1], (50, 56))
        self.assertIn((48, 54), regions)

    def test_around_tile_is_row_major_and_clips_world_edges(self) -> None:
        self.assertEqual(
            region_sets.around_tile(64, 64, 1),
            [
                (0, 0), (1, 0), (2, 0),
                (0, 1), (1, 1), (2, 1),
                (0, 2), (1, 2), (2, 2),
            ],
        )
        self.assertEqual(region_sets.around_tile(0, 0, 2), [
            (0, 0), (1, 0), (2, 0),
            (0, 1), (1, 1), (2, 1),
            (0, 2), (1, 2), (2, 2),
        ])

    def test_around_tile_rejects_invalid_coordinates_and_radius(self) -> None:
        for args in ((-1, 0, 0), (0, 16384, 0), (0, 0, -1), (0, 0, 256)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                region_sets.around_tile(*args)

    def test_full_selection_uses_cache_index_and_row_major_order(self) -> None:
        entries = {
            1: SimpleNamespace(region_x=50, region_y=53),
            2: SimpleNamespace(region_x=48, region_y=54),
            3: SimpleNamespace(region_x=49, region_y=53),
        }
        with (
            patch("region_sets.RcCacheStore") as store_type,
            patch("region_sets.find_all_map_region_files", return_value=entries) as find_all,
        ):
            cache_root = Path("/cache")
            self.assertEqual(
                region_sets.full_from_cache(cache_root),
                [(49, 53), (50, 53), (48, 54)],
            )
            store_type.assert_called_once_with(cache_root)
            find_all.assert_called_once_with(store_type.return_value)

    def test_resolve_supports_documented_expressions(self) -> None:
        selection = region_sets.resolve("around:3213,3428,r=1")
        self.assertEqual(selection.name, "around_3213_3428_r1")
        self.assertEqual(len(selection.regions), 9)
        self.assertEqual(region_sets.resolve("VARROCK").regions, tuple(region_sets.varrock()))
        self.assertEqual(region_sets.resolve("edgeville").regions, tuple(region_sets.edgeville()))
        with patch("region_sets.full_from_cache", return_value=[(1, 2)]):
            self.assertEqual(
                region_sets.resolve("full", Path("/cache")),
                region_sets.RegionSet("full", ((1, 2),)),
            )
        with self.assertRaisesRegex(ValueError, "requires RUNEC_B237_CACHE"):
            region_sets.resolve("full")
        with self.assertRaisesRegex(ValueError, "unknown region set"):
            region_sets.resolve("falador")


class RegionPipelineTests(unittest.TestCase):
    @staticmethod
    def context(region_set: str = "varrock") -> data_pipeline.PipelineContext:
        return data_pipeline.PipelineContext(
            data_root=Path("/tmp/runec-region-test"),
            dist_root=Path("/tmp/runec-region-dist"),
            generated_root=Path("/tmp/runec-region-generated"),
            version="test",
            check=False,
            force=False,
            region_set=region_set,
        )

    def test_documented_cli_and_environment_selectors_normalize(self) -> None:
        around = data_pipeline.parse_args([
            "export-regions",
            "--center-x", "3213",
            "--center-y", "3428",
            "--radius-regions", "1",
        ])
        self.assertEqual(around.region_set_spec, "around:3213,3428,r=1")
        self.assertEqual(
            data_pipeline.parse_args(["export-regions", "--full"]).region_set_spec,
            "full",
        )
        self.assertEqual(
            data_pipeline.parse_args(["export-regions", "--region-set", "varrock"]).region_set_spec,
            "varrock",
        )
        default_radius = data_pipeline.parse_args([
            "export-regions",
            "--center-x", "3213",
            "--center-y", "3428",
        ])
        self.assertEqual(default_radius.region_set_spec, "around:3213,3428,r=2")
        with patch.dict("os.environ", {"RUNEC_REGION_SET": "edgeville"}):
            selected = data_pipeline.parse_args(["export-cache-derived-assets"])
        self.assertEqual(selected.region_set_spec, "edgeville")

    def test_region_export_spec_targets_selected_data_root(self) -> None:
        ctx = self.context("around:3213,3428,r=0")
        selection = region_sets.resolve(ctx.region_set)
        spec = data_pipeline.region_export_spec(ctx, selection)

        self.assertEqual(spec.dataset, "mapsquare_visuals")
        self.assertEqual(spec.logical_paths, ("regions/",))
        self.assertEqual(spec.commands[0][5], "50,53")
        self.assertEqual(
            spec.commands[0][-1],
            "/tmp/runec-region-test/regions",
        )
        self.assertIn("--split-by-mapsquare", spec.commands[0])
        self.assertEqual(
            spec.commands[0][spec.commands[0].index("--jobs") + 1],
            "1",
        )

        with (
            patch(
                "data_pipeline.region_sets.resolve",
                return_value=region_sets.RegionSet("full", ((1, 2),)),
            ),
            patch.dict("os.environ", {"RUNEC_REGION_EXPORT_JOBS": "7"}),
        ):
            full_selection = data_pipeline.selected_region_set(self.context("full"))
            full_spec = data_pipeline.region_export_spec(
                self.context("full"), full_selection)
        self.assertEqual(
            full_spec.commands[0][full_spec.commands[0].index("--jobs") + 1],
            "7",
        )

    def test_pipeline_selection_records_bounds_and_builds_dynamic_spec(self) -> None:
        ctx = self.context("edgeville")
        specs, selection = data_pipeline.cache_derived_rebuild_specs(ctx)
        self.assertEqual(selection.name, "edgeville")
        self.assertEqual(specs[-1].dataset, "mapsquare_visuals")

        record: dict[str, object] = {}
        data_pipeline.record_region_selection(record, selection, ctx.region_set)
        self.assertEqual(record["region_selection"], {
            "requested": "edgeville",
            "name": "edgeville",
            "mapsquares": 25,
            "bounds": {
                "min_region_x": 46,
                "max_region_x": 50,
                "min_region_y": 52,
                "max_region_y": 56,
            },
        })

    def test_pipeline_selection_reports_invalid_and_empty_sets(self) -> None:
        with self.assertRaisesRegex(data_pipeline.PipelineError, "cannot resolve region set"):
            data_pipeline.selected_region_set(self.context("unknown"))
        with patch(
            "data_pipeline.region_sets.resolve",
            return_value=region_sets.RegionSet("empty", ()),
        ):
            with self.assertRaisesRegex(data_pipeline.PipelineError, "resolved to no mapsquares"):
                data_pipeline.selected_region_set(self.context("empty"))

    def test_export_regions_stage_blocks_without_cache_input(self) -> None:
        record: dict[str, object] = {}
        with patch.dict("os.environ", {}, clear=True):
            with self.assertRaisesRegex(data_pipeline.PipelineError, "RUNEC_B237_CACHE"):
                data_pipeline.stage_export_regions(self.context(), record)
        self.assertEqual(record["region_selection"]["name"], "varrock")
        self.assertEqual(record["required_rebuild_gaps"][0]["dataset"], "mapsquare_visuals")

    def test_export_regions_stage_runs_existing_rebuild_path(self) -> None:
        record: dict[str, object] = {}
        with (
            patch.dict("os.environ", {"RUNEC_B237_CACHE": "/cache"}, clear=True),
            patch("data_pipeline.run_rebuild_specs") as run_specs,
        ):
            data_pipeline.stage_export_regions(self.context(), record)
        run_specs.assert_called_once()
        self.assertEqual(record["region_selection"]["mapsquares"], 25)

    def test_cache_asset_stage_uses_selected_regions_with_or_without_inputs(self) -> None:
        blocked_record: dict[str, object] = {}
        with patch.dict("os.environ", {}, clear=True):
            data_pipeline.stage_export_cache_derived_assets(self.context("edgeville"), blocked_record)
        self.assertEqual(blocked_record["region_selection"]["name"], "edgeville")
        self.assertEqual(blocked_record["status_detail"], "blocked_required_rebuild_inputs")

        built_record: dict[str, object] = {}
        with (
            patch.dict(
                "os.environ",
                {"RUNEC_B237_CACHE": "/cache", "RUNEC_B237_DUMP": "/dump"},
                clear=True,
            ),
            patch("data_pipeline.run_command") as run_command,
            patch("data_pipeline.run_rebuild_specs") as run_specs,
        ):
            data_pipeline.stage_export_cache_derived_assets(self.context(), built_record)
        run_command.assert_called_once()
        run_specs.assert_called_once()
        self.assertNotIn("status_detail", built_record)

    def test_pack_stage_requires_full_mapsquares_only_for_release_scope(self) -> None:
        with (
            patch("data_pipeline.fail_if_required_rebuild_gaps"),
            patch("data_pipeline.run_command") as run_command,
            patch("data_pipeline.copy_dist_manifest_to_data"),
            patch("data_pipeline.file_ref", return_value={}),
        ):
            partial_record: dict[str, object] = {}
            data_pipeline.stage_pack_runtime_data(
                self.context("varrock"), partial_record)
            partial_args = run_command.call_args.args[0]
            self.assertIn("--allow-partial-mapsquares", partial_args)
            self.assertEqual(
                partial_record["mapsquare_scope"], "development-partial")

            run_command.reset_mock()
            full_record: dict[str, object] = {}
            data_pipeline.stage_pack_runtime_data(
                self.context("full"), full_record)
            full_args = run_command.call_args.args[0]
            self.assertNotIn("--allow-partial-mapsquares", full_args)
            self.assertEqual(full_record["mapsquare_scope"], "production-full")

    def test_region_cli_rejects_ambiguous_or_incomplete_radius_modes(self) -> None:
        invalid = (
            ["export-regions", "--center-x", "3213"],
            ["export-regions", "--radius-regions", "2"],
            ["export-regions", "--full", "--region-set", "varrock"],
        )
        for argv in invalid:
            with self.subTest(argv=argv), redirect_stderr(StringIO()):
                with self.assertRaises(SystemExit):
                    data_pipeline.parse_args(argv)

    def test_main_lists_and_dispatches_standalone_region_stage(self) -> None:
        with patch("builtins.print") as output:
            self.assertEqual(data_pipeline.main(["list-stages"]), 0)
        self.assertTrue(any("export-regions" in str(call) for call in output.call_args_list))

        with (
            patch("data_pipeline.run_stage", return_value={"stage": "export-regions"}) as run_stage,
            patch("data_pipeline.write_build_record") as write_build_record,
            patch("builtins.print"),
        ):
            self.assertEqual(data_pipeline.main(["export-regions"]), 0)
        run_stage.assert_called_once()
        write_build_record.assert_called_once()


if __name__ == "__main__":
    unittest.main()
