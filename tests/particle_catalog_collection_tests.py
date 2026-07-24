import importlib.util
from pathlib import Path
import struct
import sys
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "build_particle_catalog_collection.py"
SPEC = importlib.util.spec_from_file_location("particle_catalog_collection", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class ParticleCatalogCollectionTests(unittest.TestCase):
    def test_flat_sector_has_expected_shape(self):
        sector = MODULE.flat_sector()
        self.assertEqual(struct.unpack_from("<IB", sector), (MODULE.SEC_SIGNATURE, 0))
        self.assertEqual(len(sector), 5 + 33 * 33 * 8 + 16 * 16 * 2)
        tile_offset = 5 + 33 * 33 * 8
        self.assertEqual(
            set(struct.unpack_from("<256H", sector, tile_offset)),
            {MODULE.ZONE1_GROUND_PACKED_TILE},
        )

    def test_archive_round_trip_inventory(self):
        archive = MODULE.build_archive([
            ("zone1.mp", MODULE.synthetic_zone1_mp()),
            ("zone1000000.sec", MODULE.flat_sector()),
            ("zone1000001.sec", MODULE.flat_sector()),
            ("zone1001000.sec", MODULE.flat_sector()),
            ("zone1001001.sec", MODULE.flat_sector()),
        ])
        members = MODULE.read_members(archive)
        self.assertEqual([member["name"] for member in members], [
            "zone1.mp", "zone1000000.sec", "zone1000001.sec",
            "zone1001000.sec", "zone1001001.sec"
        ])
        self.assertEqual(members[1]["payload"], MODULE.flat_sector())

    def test_collection_uses_portable_relative_references(self):
        text = MODULE.collection_text(
            "../test-scenes/particle-catalog-grid/particle-zone1-flat-2x2.mpr",
            "../test-scenes/particle-catalog-grid/all-particles-grid.mob",
        )
        self.assertIn('map="gz1g"', text)
        self.assertIn('mpr="../test-scenes/particle-catalog-grid/', text)
        self.assertIn('mob="../test-scenes/particle-catalog-grid/', text)
        self.assertNotIn(":\\", text)

    def test_mp_uses_zone1_common_ground_contract(self):
        mp = MODULE.synthetic_zone1_mp()
        header = struct.unpack_from("<If6IHI", mp)
        self.assertEqual(header[0], MODULE.MP_SIGNATURE)
        self.assertEqual(header[2:4], (2, 2))
        self.assertEqual(header[4:8], (8, 512, 512, 64))
        tile_types = struct.unpack_from("<512i", mp, 38 + 44)
        self.assertEqual(tile_types[1], 1)
        self.assertEqual(sum(value != 0 for value in tile_types), 1)


if __name__ == "__main__":
    unittest.main()
