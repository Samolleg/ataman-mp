import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class LandscapeNoiseBrushContractTests(unittest.TestCase):
    def test_noise_is_available_for_terrain_and_overlay_sculpting(self):
        tools = source("source/ui/landscape_tools.cpp")
        model_header = source("source/documents/terrain_edit_model.h")
        view = source("source/render/view.cpp")
        self.assertIn('m_tool->addItem(tr("Noise")', tools)
        self.assertIn("makeNoise(", model_header)
        self.assertIn("makeOverlayNoise(", model_header)
        self.assertIn("ELandscapeSculptTool::Noise", view)

    def test_each_stroke_owns_one_stable_noise_seed(self):
        view = source("source/render/view.cpp")
        header = source("source/render/view.h")
        self.assertIn("m_sculptStrokeNoiseSeed", header)
        self.assertIn("QRandomGenerator::global()->generate()", view)
        self.assertIn("m_sculptStrokeNoiseSeed", view)

    def test_noise_uses_signed_deterministic_coordinate_samples(self):
        model = source("source/documents/terrain_edit_model.cpp")
        self.assertIn("terrainNoiseSample", model)
        self.assertIn("noiseSeed", model)
        self.assertIn('QStringLiteral("Noise overlay height")', model)


if __name__ == "__main__":
    unittest.main()
