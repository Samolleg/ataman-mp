import importlib.util
import json
from pathlib import Path
import struct
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "particle_mob_report.py"
SPEC = importlib.util.spec_from_file_location("particle_mob_report", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


def node(tag, payload):
    return struct.pack("<II", tag, len(payload) + 8) + payload


class ParticleMobReportTests(unittest.TestCase):
    def test_decodes_particle_and_torch_records(self):
        particle = node(0xDD01, b"".join([
            node(0xDD02, struct.pack("<I", 7)),
            node(0xDD03, struct.pack("<3f", 1.0, 2.0, 3.0)),
            node(0xDD05, b"Particles source-7\0"),
            node(0xDD06, struct.pack("<I", 0x2001)),
            node(0xDD07, struct.pack("<f", 0.3)),
        ]))
        torch = node(0xBBBF0000, b"".join([
            node(0xBBBF0001, struct.pack("<f", 2.0)),
            node(0xBBBF0002, struct.pack("<3f", 0.0, 0.0, 1.0)),
            node(0xBBBF0003, b"nature\\fire\\big.wav\0"),
            node(0xB002, struct.pack("<I", 9)),
            node(0xB004, b"Camp fire 00-9\0"),
            node(0xB009, struct.pack("<3f", 1.0, 2.0, 3.0)),
        ]))
        data = node(0xA000, node(0xB000, particle + torch))
        records = MODULE.collect_records(data, include_raw=True)
        self.assertEqual([record["record_type"] for record in records], ["particle", "torch"])
        self.assertEqual(MODULE.field_map(records[0])["PARTICL_TYPE"], 0x2001)
        self.assertEqual(MODULE.field_map(records[1])["TORCH_SOUND"], "nature\\fire\\big.wav")
        self.assertTrue(records[0]["raw_record_hex"].startswith("01DD0000"))
        proximity = MODULE.add_associations(records)
        self.assertEqual(proximity[0]["nearest_particles"][0]["distance"], 0.0)

    def test_rejects_node_crossing_parent_boundary(self):
        data = struct.pack("<II", 0xA000, 16) + struct.pack("<II", 0xB000, 9)
        with self.assertRaisesRegex(ValueError, "invalid MOB node"):
            MODULE.collect_records(data)

    def test_decodes_complete_world_wind_settings(self):
        world_set = node(0xABD0, b"".join([
            node(0xABD1, struct.pack("<3f", -0.5, 0.25, 0.0)),
            node(0xABD2, struct.pack("<f", 0.2)),
            node(0xABD3, struct.pack("<f", 6.4)),
            node(0xABD4, struct.pack("<f", 0.2)),
            node(0xABD5, struct.pack("<f", 0.6)),
        ]))
        settings = MODULE.collect_world_settings(node(0xA000, world_set), include_raw=True)
        self.assertEqual(len(settings), 1)
        fields = MODULE.field_map(settings[0])
        self.assertEqual(fields["WS_WIND_DIR"], [-0.5, 0.25, 0.0])
        self.assertAlmostEqual(fields["WS_WIND_STR"], 0.2)
        self.assertTrue(settings[0]["raw_record_hex"].startswith("D0AB0000"))

    def test_gz1g_fixture_fingerprints_exact_authored_records(self):
        fixture_path = Path(__file__).parent / "fixtures" / "particle_runtime" / "gz1g-authored-records.json"
        fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
        self.assertEqual(
            fixture["source"]["sha256"],
            "FEFFDA09F7279F65562DB37BBDCDFC475D59C4EE6FF1678958E5117949FB9E5A",
        )
        self.assertEqual(fixture["counts"], {"particle": 8, "torch": 4})
        self.assertEqual(fixture["format"], "ei-particle-mob-report-v2")
        self.assertEqual(fixture["selection"], [357, 1365])
        records = {MODULE.record_id(record): record for record in fixture["records"]}
        self.assertEqual(records[1365]["sha256"], "D7450142441196107260624C61A4119B55D7FB11D443E7F9DE14FC13B4EDED34")
        self.assertEqual(MODULE.field_map(records[1365])["TORCH_SOUND"], "nature\\fire\\big.wav")
        self.assertEqual(records[357]["sha256"], "E2FFEEB487E2AA3CB51EFE2C1FCBD8CBDD19D44B9A5C290AAE005348167D7FD5")
        self.assertEqual(MODULE.field_map(records[357])["PARTICL_TYPE"], 0x2001)
        self.assertEqual(len(fixture["world_settings"]), 1)
        world_fields = MODULE.field_map(fixture["world_settings"][0])
        self.assertEqual(
            world_fields["WS_WIND_DIR"],
            [-0.7281999588012695, -0.6853646636009216, 0.0],
        )
        self.assertEqual(world_fields["WS_WIND_STR"], 0.20000000298023224)


if __name__ == "__main__":
    unittest.main()
