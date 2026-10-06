#!/usr/bin/env python3
"""Storage metadata tests; no external reference checkout or cache required."""
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from export_storage_policy import object_masks, ranges
from rc_cache import decode_item_definition


class StoragePolicyTests(unittest.TestCase):
    def masks(self, name, actions):
        return object_masks(SimpleNamespace(name=name, actions=actions))

    def test_only_reviewed_personal_storage_actions(self):
        self.assertEqual(self.masks("Bank booth", ["Use", "Bank", "Collect"]), (3, 0))
        self.assertEqual(self.masks("Bank chest", ["Use"]), (1, 0))
        self.assertEqual(self.masks("Bank deposit box", ["Deposit"]), (0, 1))
        self.assertEqual(self.masks("Bank booth", ["Bank", "Deposit-box"]), (1, 2))
        for name in ("Fossil storage", "Food chute", "Deposit Pool", "Deposit pot"):
            self.assertEqual(self.masks(name, ["Deposit", "Use", "Collect"]), (0, 0))

    def test_item_parameters_are_preserved(self):
        item = decode_item_definition(1, bytes.fromhex("f9010000003b0000000100"))
        self.assertTrue(item.complete)
        self.assertEqual(len(item.params), 1)
        self.assertEqual((item.params[0].key, item.params[0].int_value), (59, 1))
        self.assertFalse(item.params[0].is_string)
        self.assertEqual(decode_item_definition(2, b"\0").params, [])

    def test_compact_ranges_are_lossless(self):
        self.assertEqual(ranges([]), [])
        self.assertEqual(ranges({9, 2, 1, 7, 6, 5}), [[1, 2], [5, 7], [9, 9]])


if __name__ == "__main__":
    unittest.main()
