#!/usr/bin/env python3
from __future__ import annotations

import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import object_placement_index as index


class ObjectPlacementIndexTests(unittest.TestCase):
    @staticmethod
    def write_fixture(path: Path, trailing: bytes = b"") -> list[tuple[int, ...]]:
        rows = [
            (1276, 1, 3213, 3428, (50 << 8) | 53, 0, 10, 1, 0),
            (11780, 2, 3196, 3384, (49 << 8) | 52, 0, 0, 1, 0),
        ]
        by_page: dict[int, list[tuple[int, ...]]] = {}
        for row in rows:
            by_page.setdefault(row[4], []).append(row)
        with path.open("wb") as handle:
            handle.write(index.HEADER.pack(
                index.OBJECT_PLACEMENT_INDEX_MAGIC,
                index.OBJECT_PLACEMENT_INDEX_VERSION,
                len(rows),
                index.RECORD.size,
                len(by_page),
                2,
                0,
                0,
                0,
            ))
            first = 0
            for mapsquare in range(index.OBJECT_PLACEMENT_MAPSQUARE_COUNT):
                count = len(by_page.get(mapsquare, ()))
                handle.write(index.INDEX_ENTRY.pack(first, count))
                first += count
            for mapsquare in range(index.OBJECT_PLACEMENT_MAPSQUARE_COUNT):
                for row in by_page.get(mapsquare, ()):
                    handle.write(index.RECORD.pack(*row))
            handle.write(trailing)
        return sorted(rows, key=lambda row: row[4])

    def test_reads_indexed_rows_in_mapsquare_order(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "world.object-placements.indexed.bin"
            expected = self.write_fixture(path)
            self.assertEqual(list(index.iter_object_placements(path)), expected)
            header = index.read_header(path)
            self.assertEqual(header[2], 2)
            self.assertEqual(header[4], 2)

    def test_rejects_trailing_bytes(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.bin"
            self.write_fixture(path, b"x")
            with self.assertRaisesRegex(ValueError, "trailing"):
                list(index.iter_object_placements(path))

    def test_rejects_wrong_mapsquare_record(self) -> None:
        with TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.bin"
            self.write_fixture(path)
            record_offset = (
                index.HEADER.size
                + index.INDEX_ENTRY.size
                * index.OBJECT_PLACEMENT_MAPSQUARE_COUNT
            )
            with path.open("r+b") as handle:
                handle.seek(record_offset + 16)
                handle.write((0).to_bytes(2, "little"))
            with self.assertRaisesRegex(ValueError, "wrong mapsquare"):
                list(index.iter_object_placements(path))


if __name__ == "__main__":
    unittest.main()
