#!/usr/bin/env python3
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import spawn_index
import migrate_npc_runtime_foundation as npc_migration


class SpawnIndexTests(unittest.TestCase):
    def test_npc_round_trip_preserves_source_order(self) -> None:
        rows = [
            (10, 3200, 3456, 0, 2, 0, 0),
            (11, 1280, 1280, 2, 4, 5, 1),
            (12, 3201, 3457, 1, 6, 2, 0),
        ]
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "world.npc-spawns.indexed.bin"
            spawn_index.write_npc_spawns(path, rows)
            self.assertEqual(spawn_index.read_npc_spawns(path), rows)

            header = spawn_index.read_indexed_header(
                path, spawn_index.NPC_SPAWN_INDEX_MAGIC
            )
            self.assertEqual(header[2], 3)
            self.assertEqual(header[3], 4 + spawn_index.NPC_PAYLOAD.size)
            self.assertEqual(header[4], 2)
            self.assertEqual(header[5:9], (1, 1, 1, 0))

    def test_npc_migration_repairs_only_missing_wander_policy(self) -> None:
        rows = [
            (10, 3200, 3456, 0, 2, 0, 0),
            (11, 3201, 3456, 0, 4, 7, 0),
        ]
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source.bin"
            repaired = root / "repaired.bin"
            preserved = root / "preserved.bin"
            spawn_index.write_npc_spawns(source, rows)

            npc_migration.write_spawns(source, repaired, True)
            self.assertEqual(
                spawn_index.read_npc_spawns(repaired),
                [
                    (10, 3200, 3456, 0, 6, 255, 0),
                    (11, 3201, 3456, 0, 6, 255, 0),
                ],
            )

            npc_migration.write_spawns(source, preserved, False)
            self.assertEqual(spawn_index.read_npc_spawns(preserved), rows)

        policy = (0, 25, 100, 0, 0, 0, 0, 0, 0, -1, -1)
        row = npc_migration.NpcRow(10, b"", False, 0, policy)
        self.assertEqual(npc_migration.derived_policy(row, False), policy)
        self.assertEqual(
            npc_migration.derived_policy(row, True)[0],
            npc_migration.DEFAULT_WANDER_RANGE,
        )

    def test_ground_items_round_trip(self) -> None:
        rows = [
            (995, 100, 3213, 3428, 0, 0),
            (2357, 1, 3208, 3425, 0, 0),
            (995, 25, 3213, 3428, 1, 0),
        ]
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "world.ground-items.indexed.bin"
            spawn_index.write_ground_item_spawns(path, rows)
            self.assertEqual(spawn_index.read_ground_item_spawns(path), rows)

    def test_rejects_out_of_range_coordinates(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.bin"
            with self.assertRaisesRegex(ValueError, "outside mapsquare index"):
                spawn_index.write_npc_spawns(
                    path, [(10, -1, 3200, 0, 2, 0, 0)]
                )

    def test_rejects_trailing_bytes(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.bin"
            spawn_index.write_ground_item_spawns(
                path, [(995, 1, 3200, 3200, 0, 0)]
            )
            with path.open("ab") as handle:
                handle.write(b"x")
            with self.assertRaisesRegex(ValueError, "trailing"):
                spawn_index.read_ground_item_spawns(path)


if __name__ == "__main__":
    unittest.main()
