import hashlib
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).parents[1]
BUNDLE = ROOT / "artifacts" / "particle-runtime-crash-bisection"
REPORT_SCRIPT = ROOT / "scripts" / "particle_mob_report.py"
SPEC = importlib.util.spec_from_file_location("particle_mob_report_bisection", REPORT_SCRIPT)
REPORT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(REPORT)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


class ParticleRuntimeBisectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(
            (BUNDLE / "BISECTION_MANIFEST.json").read_text(encoding="utf-8")
        )
        cls.variants = {item["name"]: item for item in cls.manifest["variants"]}
        canonical_path = ROOT / "artifacts" / "particle-catalog-grid" / "all-particles-grid.mob"
        cls.canonical = {
            REPORT.record_id(record): record
            for record in REPORT.collect_records(canonical_path.read_bytes(), include_raw=True)
        }

    def test_variant_partition_contract(self):
        expected = {
            "00-empty-control": [],
            "10-half-000-041": list(range(0, 42)),
            "11-half-042-082": list(range(42, 83)),
            "20-quarter-000-020": list(range(0, 21)),
            "21-quarter-021-041": list(range(21, 42)),
            "22-quarter-042-062": list(range(42, 63)),
            "23-quarter-063-082": list(range(63, 83)),
        }
        for row in range(9):
            expected[f"3{row}-row-{row}"] = list(
                range(row * 10, min(row * 10 + 10, 83))
            )
        self.assertEqual(set(self.variants), set(expected))
        for name, indices in expected.items():
            self.assertEqual(self.variants[name]["indices"], indices)
            self.assertEqual(self.variants[name]["count"], len(indices))

    def test_collections_target_canonical_7x5_terrain(self):
        mpr = BUNDLE / "test-scenes" / "particle-catalog-bisection" / "particle-zone1-flat-7x5.mpr"
        self.assertEqual(sha256(mpr), self.manifest["sourceMprSha256"])
        self.assertEqual(
            self.manifest["sourceMprSha256"],
            "2D4A14374676364FC7E36DE4C93B0B66F7161A9DC3A67B2134E05F116A6B7192",
        )
        for variant in self.manifest["variants"]:
            text = (BUNDLE / "collections" / variant["collection"]).read_text(
                encoding="utf-8"
            )
            self.assertIn('map="gz1g"', text)
            self.assertIn("particle-zone1-flat-7x5.mpr", text)
            self.assertIn(variant["mob"]["fileName"], text)
            self.assertNotIn("flat-3x3", text)

    def test_subset_records_preserve_canonical_bytes_and_order(self):
        scene = BUNDLE / "test-scenes" / "particle-catalog-bisection"
        for variant in self.manifest["variants"]:
            mob = scene / variant["mob"]["fileName"]
            self.assertEqual(sha256(mob), variant["mob"]["sha256"])
            records = REPORT.collect_records(mob.read_bytes(), include_raw=True)
            ids = [REPORT.record_id(record) for record in records]
            expected_ids = [900000 + index for index in variant["indices"]]
            self.assertEqual(ids, expected_ids)
            for record in records:
                source = self.canonical[REPORT.record_id(record)]
                self.assertEqual(record["raw_record_hex"], source["raw_record_hex"])

    def test_corrected_catalog_positions_are_retained(self):
        positions = []
        for record in self.canonical.values():
            fields = REPORT.field_map(record)
            positions.append(fields["PARTICL_POSITION"])
        self.assertEqual(min(point[0] for point in positions), 148.0)
        self.assertEqual(max(point[0] for point in positions), 220.0)
        self.assertEqual(min(point[1] for point in positions), 72.0)
        self.assertEqual(max(point[1] for point in positions), 136.0)


if __name__ == "__main__":
    unittest.main()
