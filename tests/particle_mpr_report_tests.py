import importlib.util
import json
from pathlib import Path
import struct
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "particle_mpr_report.py"
SPEC = importlib.util.spec_from_file_location("particle_mpr_report", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class ParticleMprReportTests(unittest.TestCase):
    def test_mp_lava_type_and_material(self):
        header = struct.pack("<If6IHI", MODULE.MP_SIGNATURE, 10.0, 1, 1, 1, 512, 2, 64, 1, 1)
        material = struct.pack("<i10f", 3, 1, 0.5, 0, 1, 0.8, 0.2, 0.1, 0, 0, 0)
        data = header + material + struct.pack("<2i", 0, MODULE.LAVA_TILE_TYPE) + struct.pack("<HH", 1, 4)
        parsed = MODULE.parse_mp(data)
        self.assertEqual(parsed["lava_tile_indices"], [1])
        self.assertEqual(parsed["animated_tiles"], [{"index": 0, "tile_index": 1, "phase_count": 4}])
        self.assertEqual(parsed["trailing_bytes"], 0)

    def test_liquid_sector_identifies_lava_tile(self):
        vertices = bytes(33 * 33 * 8)
        land = bytes(16 * 16 * 2)
        water = struct.pack("<H", 1) + bytes((16 * 16 - 1) * 2)
        materials = struct.pack("<h", 2) + bytes((16 * 16 - 1) * 2)
        data = struct.pack("<IB", MODULE.SEC_SIGNATURE, 3) + vertices + vertices + land + water + materials
        parsed = MODULE.parse_sector(data, [0, MODULE.LAVA_TILE_TYPE], "zone000000.sec")
        self.assertEqual(parsed["lava_tile_count"], 1)
        self.assertEqual(parsed["liquid_tiles"][0]["material_index"], 2)
        self.assertEqual(parsed["trailing_bytes"], 0)

    def test_gz10g_fixtures_distinguish_mob_from_mpr_lava(self):
        fixture_root = Path(__file__).parent / "fixtures" / "particle_runtime"
        mob = json.loads((fixture_root / "gz10g-mob-negative.json").read_text(encoding="utf-8"))
        lava = json.loads((fixture_root / "gz10g-lava-records.json").read_text(encoding="utf-8"))
        self.assertEqual(mob["source"]["sha256"], "6C2FF44B72CE9850B4BBFF9759A00D17BC7DA3DDCC9CC7E124138110D31B78B2")
        self.assertEqual(mob["counts"], {"particle": 0, "torch": 0})
        self.assertEqual(lava["source"]["sha256"], "56F0A601330D88912EFCC17EF58A2EDB5168640D58E965CC040FC30E9076B925")
        self.assertEqual(lava["map"]["lava_tile_indices"], [5, 6, 7])
        self.assertEqual(lava["summary"]["lava_tile_count"], 637)
        self.assertEqual(lava["summary"]["particle_records"], 0)
        self.assertEqual({tuple(sector["lava_material_indices"]) for sector in lava["sectors"]}, {(1,)})


if __name__ == "__main__":
    unittest.main()
