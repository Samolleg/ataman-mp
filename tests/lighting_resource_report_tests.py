import importlib.util
import struct
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "lighting_resource_report.py"
SPEC = importlib.util.spec_from_file_location("lighting_resource_report", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class LightingResourceReportTests(unittest.TestCase):
    def test_directory_report_filters_and_decodes_cp1251(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "LightsGipat.ini").write_bytes("[sky]\r\ntime00 = 1, 2, 3\r\n".encode("cp1251"))
            (root / "ignored.bin").write_bytes(b"ignored")
            report = MODULE.report(root, ["lights*.ini"])
            self.assertEqual(report["source"]["kind"], "directory")
            self.assertEqual([entry["name"] for entry in report["members"]],
                             ["LightsGipat.ini"])
            self.assertIn("time00 = 1, 2, 3", report["members"][0]["text_cp1251"])

    def test_directory_does_not_read_unmatched_files(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "LightsGipat.ini").write_bytes(b"[sky]\n")
            unreadable = root / "ignored.bin"
            unreadable.mkdir()
            report = MODULE.report(root, ["lights*.ini"])
            self.assertEqual([entry["name"] for entry in report["members"]],
                             ["LightsGipat.ini"])

    def test_res_report_rejects_out_of_bounds_member(self):
        header = struct.pack("<4I", MODULE.RES_SIGNATURE, 1, 16, 1)
        entry = struct.pack("<iIIIHI", 0, 100, 9999, 0, 1, 0)
        with self.assertRaisesRegex(ValueError, "out of bounds"):
            list(MODULE.members(header + entry + b"x"))


if __name__ == "__main__":
    unittest.main()
