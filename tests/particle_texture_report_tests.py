import importlib.util
import json
from pathlib import Path
import struct
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "particle_texture_report.py"
SPEC = importlib.util.spec_from_file_location("particle_texture_report", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class ParticleTextureReportTests(unittest.TestCase):
    def test_reference_fixture_pins_weather_atlases(self):
        path = Path(__file__).parent / "fixtures" / "particle_runtime" / "weather-particle-textures.json"
        fixture = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(fixture["source"]["sha256"], MODULE.REFERENCE_JMV_TEXTURES_SHA256)
        by_name = {item["name"]: item for item in fixture["textures"]}
        self.assertEqual(set(by_name), {
            "tornado.mmp", "tendrilight.mmp", "firesmoke.mmp",
            "banshee.mmp", "acidray.mmp", "changehero.mmp", "rainsnow.mmp",
            "firearrowsmoke.mmp", "link.mmp", "protectiona.mmp",
            "protectione.mmp", "protectionf.mmp", "clayring.mmp",
            "teleport.mmp", "stun.mmp", "zoneexit.mmp", "shapechange.mmp",
            "bag.mmp", "casting6.mmp", "casting2.mmp", "casting1.mmp",
            "visionfog.mmp", "healing.mmp", "rikarrowsmoke.mmp",
        })
        for name in ("tornado.mmp", "tendrilight.mmp", "firesmoke.mmp",
                     "banshee.mmp", "changehero.mmp", "firearrowsmoke.mmp",
                     "link.mmp", "protectione.mmp", "protectionf.mmp",
                     "clayring.mmp", "teleport.mmp", "stun.mmp",
                     "zoneexit.mmp", "shapechange.mmp", "bag.mmp",
                     "casting6.mmp", "casting2.mmp", "casting1.mmp",
                     "visionfog.mmp", "healing.mmp", "rikarrowsmoke.mmp"):
            item = by_name[name]
            self.assertEqual((item["width"], item["height"]), (1024, 1024))
            self.assertEqual(item["mipCount"], 11)
            self.assertEqual(item["textureFormat"], "0x33545844")
            self.assertEqual(item["atlas"]["phaseCount"], 16)
        protection_a = by_name["protectiona.mmp"]
        self.assertEqual((protection_a["width"], protection_a["height"]),
                         (1024, 1024))
        self.assertEqual(protection_a["mipCount"], 1)
        self.assertEqual(protection_a["textureFormat"], "0x33545844")
        self.assertEqual((by_name["acidray.mmp"]["width"],
                          by_name["acidray.mmp"]["height"]), (512, 512))
        self.assertEqual(by_name["acidray.mmp"]["mipCount"], 10)
        self.assertEqual((by_name["rainsnow.mmp"]["width"],
                          by_name["rainsnow.mmp"]["height"]), (2048, 2048))
        self.assertEqual(by_name["rainsnow.mmp"]["mipCount"], 12)

        base_path = (Path(__file__).parent / "fixtures" / "particle_runtime"
                     / "base-particle-textures.json")
        base_fixture = json.loads(base_path.read_text(encoding="utf-8"))
        self.assertEqual(base_fixture["source"]["sha256"],
                         MODULE.REFERENCE_BASE_TEXTURES_SHA256)
        self.assertEqual(len(base_fixture["textures"]), 20)
        base_by_name = {item["name"]: item for item in base_fixture["textures"]}
        self.assertEqual(set(base_by_name), {
            "stench.mmp", "link.mmp", "protectiona.mmp", "protectione.mmp",
            "protectionf.mmp", "clayring.mmp", "teleport.mmp",
            "antimagic.mmp", "stun.mmp",
            "zoneexit.mmp", "shapechange.mmp", "bag.mmp",
            "casting6.mmp", "casting2.mmp", "casting1.mmp",
            "visionfog.mmp", "healing.mmp", "rikarrowsmoke.mmp",
            "changehero.mmp", "tornado.mmp",
        })
        stench = base_by_name["stench.mmp"]
        self.assertEqual(stench["name"], "stench.mmp")
        self.assertEqual((stench["width"], stench["height"]), (256, 256))
        self.assertEqual(stench["mipCount"], 3)
        self.assertEqual(stench["textureFormat"], "0x00004444")
        self.assertEqual(stench["atlas"]["phaseCount"], 16)
        for name, item in base_by_name.items():
            self.assertEqual((item["width"], item["height"]), (256, 256))
            self.assertEqual(item["mipCount"], 3)
            expected_format = ("0x33545844" if name in {
                "stun.mmp", "rikarrowsmoke.mmp", "changehero.mmp"
            } else "0x00004444")
            self.assertEqual(item["textureFormat"], expected_format)
            self.assertEqual(item["atlas"]["phaseCount"], 16)

    def test_invalid_archive_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "not an EI RES"):
            MODULE.archive_members(struct.pack("<4I", 0, 0, 16, 0))


if __name__ == "__main__":
    unittest.main()
