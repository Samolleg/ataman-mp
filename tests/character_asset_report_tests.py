import importlib.util
from pathlib import Path
import struct
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "character_asset_report.py"
SPEC = importlib.util.spec_from_file_location("character_asset_report", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class CharacterAssetReportTests(unittest.TestCase):
    def test_fig_reports_incomplete_triangle_tail(self):
        header = (1, 1, 1, 4, 2, 0, 0, 0, 0)
        data = b"FIG8" + struct.pack("<9I", *header)
        data += bytes(3 * 8 * 3 * 4 + 8 * 4)
        data += bytes(1 * 3 * 8 * 4 * 4)
        data += bytes(1 * 4 * 4 * 4)
        data += struct.pack("<2f", 0.25, 0.75)
        data += struct.pack("<4H", 0, 1, 0, 1)
        data += struct.pack("<6H", 0, 0, 0, 1, 1, 0)
        parsed = MODULE.parse_fig(data)
        self.assertEqual(parsed["triangle_index_count"], 3)
        self.assertEqual(parsed["triangle_count"], 1)
        self.assertEqual(parsed["dangling_indices"], [1])
        self.assertEqual(parsed["dangling_components"], [[1, 1, 0]])
        self.assertEqual(parsed["diagnostics"][0]["code"], "FIG_INDEX_TAIL_NOT_A_TRIANGLE")

    def test_lnk_tree_and_missing_parent(self):
        def text(value):
            raw = value.encode("ascii") + b"\0"
            return struct.pack("<i", len(raw)) + raw
        data = struct.pack("<i", 2) + text("root") + struct.pack("<i", 0)
        data += text("child") + text("root")
        parsed = MODULE.parse_lnk(data)
        self.assertEqual(parsed["roots"], ["root"])
        self.assertEqual(parsed["missing_parents"], [])
        self.assertEqual(parsed["cycles"], [])
        self.assertEqual(parsed["trailing_bytes"], 0)

    def test_lnk_cycle_is_reported(self):
        def text(value):
            raw = value.encode("ascii") + b"\0"
            return struct.pack("<i", len(raw)) + raw
        data = struct.pack("<i", 2) + text("a") + text("b") + text("b") + text("a")
        parsed = MODULE.parse_lnk(data)
        self.assertEqual(parsed["cycles"], [["a", "b", "a"]])

    def test_anm_counts_and_trailing_bytes(self):
        data = struct.pack("<i4fi3fi", 1, 1, 0, 0, 0, 1, 0, 0, 0, 0) + b"tail"
        parsed = MODULE.parse_anm(data)
        self.assertEqual(parsed["rotation_frames"], 1)
        self.assertEqual(parsed["translation_frames"], 1)
        self.assertEqual(parsed["morph_frames"], 0)
        self.assertEqual(parsed["trailing_bytes"], 4)

    def test_anm_explicit_zero_morph_vertex_count(self):
        data = struct.pack("<iiii", 0, 0, 0, 0)
        parsed = MODULE.parse_anm(data)
        self.assertTrue(parsed["explicit_zero_morph_vertex_count"])
        self.assertEqual(parsed["trailing_bytes"], 0)

    def test_mmp_header(self):
        data = struct.pack("<5I", MODULE.MMP_SIGNATURE, 1024, 1024, 0, MODULE.PNT3)
        data += bytes(56)
        parsed = MODULE.parse_mmp(data)
        self.assertTrue(parsed["pnt3"])
        self.assertTrue(parsed["payload_within_file"])


if __name__ == "__main__":
    unittest.main()
