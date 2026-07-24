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
    @staticmethod
    def runtime_lookup(archive, requested_name):
        _, count, table_offset, names_size = struct.unpack_from("<4I", archive)
        entry_size = struct.calcsize("<iIIIHI")
        names = archive[
            table_offset + count * entry_size:
            table_offset + count * entry_size + names_size
        ]
        index = MODULE.ei_string_hash(requested_name, count)
        visited = set()
        while index != -1:
            if index in visited or not 0 <= index < count:
                raise AssertionError("invalid RES hash chain")
            visited.add(index)
            row = struct.unpack_from("<iIIIHI", archive, table_offset + index * entry_size)
            next_index, size, offset, _, name_length, name_offset = row
            name = names[name_offset:name_offset + name_length].decode("cp1251")
            if name.casefold() == requested_name.casefold():
                return archive[offset:offset + size]
            index = next_index
        return None

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
        archive_members = [("zone1.mp", MODULE.synthetic_zone1_mp())]
        archive_members.extend(
            (f"zone1{x:03d}{y:03d}.sec", MODULE.flat_sector())
            for x in range(MODULE.SECTOR_COUNT_X)
            for y in range(MODULE.SECTOR_COUNT_Y)
        )
        archive = MODULE.build_archive(archive_members)
        members = MODULE.read_members(archive)
        self.assertEqual(
            {member["name"] for member in members},
            {name for name, _ in archive_members},
        )
        for name, payload in archive_members:
            self.assertEqual(self.runtime_lookup(archive, name.upper()), payload)
        self.assertEqual(
            self.runtime_lookup(archive, "Zone1000000.SEC"), MODULE.flat_sector()
        )

    def test_collection_uses_portable_relative_references(self):
        text = MODULE.collection_text(
            "../test-scenes/particle-catalog-grid/particle-zone1-flat-7x5.mpr",
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
        self.assertEqual(
            header[2:4], (MODULE.SECTOR_COUNT_X, MODULE.SECTOR_COUNT_Y)
        )
        self.assertEqual(header[4:8], (8, 512, 512, 64))
        tile_types = struct.unpack_from("<512i", mp, 38 + 44)
        self.assertEqual(tile_types[1], 1)
        self.assertEqual(sum(value != 0 for value in tile_types), 1)

    def test_borrowed_zone1_bootstrap_entrance_is_inside_terrain(self):
        left, top, right, bottom = MODULE.ZONE1_BOOTSTRAP_DEPLOY
        self.assertGreaterEqual(left, 0)
        self.assertGreaterEqual(top, 0)
        self.assertLessEqual(right, MODULE.SECTOR_COUNT_X * MODULE.SECTOR_WORLD_SIZE)
        self.assertLessEqual(bottom, MODULE.SECTOR_COUNT_Y * MODULE.SECTOR_WORLD_SIZE)


if __name__ == "__main__":
    unittest.main()
