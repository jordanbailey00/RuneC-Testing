#!/usr/bin/env python3
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import spawn_index


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
