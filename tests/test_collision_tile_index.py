#!/usr/bin/env python3
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import collision_tile_index as index


class CollisionTileIndexTests(unittest.TestCase):
    @staticmethod
    def write_fixture(path: Path, trailing: bytes = b"") -> list[tuple[int, ...]]:
        rows = [
            ((50 << 8) | 53, 0, 3, 0, 0x200000),
            ((50 << 8) | 53, 33, 44, 1, 0x20),
            ((62 << 8) | 62, 32, 32, 0, 0x100),
        ]
        by_page: dict[int, list[tuple[int, ...]]] = {}
        for row in rows:
            by_page.setdefault(row[0], []).append(row)
        with path.open("wb") as handle:
            handle.write(index.HEADER.pack(
                index.COLLISION_TILE_INDEX_MAGIC,
                index.COLLISION_TILE_INDEX_VERSION,
                len(rows),
                index.RECORD.size,
                len(by_page),
                2,
                1,
                0,
                0,
            ))
            first = 0
            for mapsquare in range(index.COLLISION_TILE_MAPSQUARE_COUNT):
                count = len(by_page.get(mapsquare, ()))
                handle.write(index.INDEX_ENTRY.pack(first, count))
                first += count
            for mapsquare in range(index.COLLISION_TILE_MAPSQUARE_COUNT):
                for _, local_x, local_y, plane, flags in by_page.get(
                    mapsquare, ()
                ):
                    handle.write(index.RECORD.pack(
                        local_x, local_y, plane, 0, flags
                    ))
            handle.write(trailing)
        return rows

    def test_reads_indexed_rows_in_mapsquare_order(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "world.collision-tiles.indexed.bin"
            expected = self.write_fixture(path)
            self.assertEqual(list(index.iter_collision_tiles(path)), expected)
            header = index.read_header(path)
            self.assertEqual(header[2], 3)
            self.assertEqual(header[4], 2)

    def test_rejects_trailing_bytes(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.bin"
            self.write_fixture(path, b"x")
            with self.assertRaisesRegex(ValueError, "trailing"):
                index.read_header(path)

    def test_rejects_non_contiguous_directory(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.bin"
            self.write_fixture(path)
            with path.open("r+b") as handle:
                handle.seek(index.HEADER.size + index.INDEX_ENTRY.size)
                handle.write(index.INDEX_ENTRY.pack(1, 0))
            with self.assertRaisesRegex(ValueError, "page range"):
                list(index.iter_collision_tiles(path))

    def test_rejects_duplicate_or_unsorted_rows(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.bin"
            self.write_fixture(path)
            records = (
                index.HEADER.size
                + index.INDEX_ENTRY.size * index.COLLISION_TILE_MAPSQUARE_COUNT
            )
            with path.open("r+b") as handle:
                handle.seek(records + index.RECORD.size)
                handle.write(index.RECORD.pack(0, 3, 0, 0, 0x20))
            with self.assertRaisesRegex(ValueError, "duplicate or unsorted"):
                list(index.iter_collision_tiles(path))


if __name__ == "__main__":
    unittest.main()
