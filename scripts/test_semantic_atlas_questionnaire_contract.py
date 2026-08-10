#!/usr/bin/env python3
"""Regression tests for the semantic-atlas questionnaire handoff contract."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from PIL import Image

from validate_semantic_atlas_questionnaire import validate_questionnaire


GOOD_HTML = """<!doctype html>
<html><body data-questionnaire-contract="semantic-atlas-questionnaire-v1">
<button id="copy"></button><button id="download"></button>
<button id="select"></button><button id="submit"></button><button id="reset"></button>
<textarea id="export"></textarea>
<article class="question" data-key="atlas-slot-corner"
 data-defaults='{"NW":"Family / A"}'>
 <div class="comparison-row" data-variant="hd" data-comparison-kind="whole-tile"
  data-native-width="256" data-native-height="256">
  <figure class="tile target" data-role="target" data-source-key="atlas:9"
   data-variant="hd" data-comparison-kind="whole-tile" data-native-width="256"
   data-native-height="256"><img src="target.png" width="256" height="256">
   <figcaption>Native 256x256; displayed 256x256. Current catalog says NW: Family / A</figcaption></figure>
  <figure class="tile" data-role="reference" data-source-key="atlas:1"
   data-option="Family / A" data-trusted="true" data-variant="hd"
   data-comparison-kind="whole-tile" data-native-width="256" data-native-height="256">
   <img src="a.png" width="256" height="256"><figcaption>Native 256x256; displayed 256x256. Uniform: Family / A</figcaption></figure>
  <figure class="tile" data-role="reference" data-source-key="atlas:2"
   data-option="Family / B" data-trusted="true" data-variant="hd"
   data-comparison-kind="whole-tile" data-native-width="256" data-native-height="256">
   <img src="b.png" width="256" height="256"><figcaption>Native 256x256; displayed 256x256. Uniform: Family / B</figcaption></figure>
 </div>
 <div class="comparison-row" data-variant="original" data-comparison-kind="whole-tile"
  data-native-width="64" data-native-height="64">
  <figure class="tile target" data-role="target" data-source-key="atlas:9:original"
   data-variant="original" data-comparison-kind="whole-tile" data-native-width="64"
   data-native-height="64"><img src="target-original.png" width="256" height="256">
   <figcaption>Native 64x64; displayed 256x256. Current catalog says NW: Family / A</figcaption></figure>
  <figure class="tile" data-role="reference" data-source-key="atlas:1:original"
   data-option="Family / A" data-trusted="true" data-variant="original"
   data-comparison-kind="whole-tile" data-native-width="64" data-native-height="64">
   <img src="a-original.png" width="256" height="256"><figcaption>Native 64x64; displayed 256x256. Uniform: Family / A</figcaption></figure>
  <figure class="tile" data-role="reference" data-source-key="atlas:2:original"
   data-option="Family / B" data-trusted="true" data-variant="original"
   data-comparison-kind="whole-tile" data-native-width="64" data-native-height="64">
   <img src="b-original.png" width="256" height="256"><figcaption>Native 64x64; displayed 256x256. Uniform: Family / B</figcaption></figure>
 </div>
 <select class="answer" data-field="NW" data-current-option="Family / A"><option selected>Family / A</option>
  <option>Family / B</option><option>Still uncertain</option></select>
 <textarea class="note"></textarea>
</article>
<script>localStorage.setItem('x','y'); navigator.clipboard.writeText('x');
window.openai.sendFollowUpMessage({prompt:'x'}); new Blob(['x']);
document.getElementById('reset'); const order=['NW','NE','SE','SW'];</script>
</body></html>"""


GOOD_MANIFEST = {
    "questionnaireContract": "semantic-atlas-questionnaire-v1",
    "questionKeys": ["atlas-slot-corner"],
    "markedItemKeys": ["atlas-slot-corner"],
    "displayOrder": ["NW", "NE", "SW", "SE"],
    "serializationOrder": ["NW", "NE", "SE", "SW"],
    "browserValidation": {"status": "passed"},
}


class QuestionnaireContractTests(unittest.TestCase):
    def validate(self, html_text: str = GOOD_HTML, *, manifest=None,
                 image_sizes=None, handoff=False) -> list[str]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            page = root / "questionnaire.html"
            page.write_text(html_text, encoding="utf-8")
            for name, size in (image_sizes or {
                    "target.png": (256, 256), "a.png": (256, 256),
                    "b.png": (256, 256), "target-original.png": (256, 256),
                    "a-original.png": (256, 256), "b-original.png": (256, 256)}).items():
                Image.new("RGB", size).save(root / name)
            manifest_path = root / "manifest.json"
            manifest_path.write_text(json.dumps(
                GOOD_MANIFEST if manifest is None else manifest), encoding="utf-8")
            return validate_questionnaire(page, manifest_path, handoff=handoff)

    def test_valid_questionnaire_passes(self) -> None:
        self.assertEqual([], self.validate(handoff=True))

    def test_missing_concrete_option_reference_fails(self) -> None:
        broken = GOOD_HTML.replace(' data-option="Family / B"',
                                   ' data-option="Family / C"')
        self.assertTrue(any("Family / B" in row for row in self.validate(broken)))

    def test_mismatched_raw_image_size_fails(self) -> None:
        errors = self.validate(image_sizes={
            "target.png": (256, 256), "a.png": (128, 128), "b.png": (256, 256),
            "target-original.png": (256, 256), "a-original.png": (256, 256),
            "b-original.png": (256, 256)})
        self.assertTrue(any("actual image size" in row for row in errors))

    def test_missing_visible_corner_mapping_fails(self) -> None:
        broken = GOOD_HTML.replace("Uniform: Family / B", "Family / B reference")
        self.assertTrue(any("uniform/corner mapping" in row
                            for row in self.validate(broken)))

    def test_each_concrete_option_requires_original_reference(self) -> None:
        broken = GOOD_HTML.replace(' data-option="Family / B" data-trusted="true" data-variant="original"',
                                   ' data-option="Family / C" data-trusted="true" data-variant="original"')
        self.assertTrue(any("Family / B" in row and "original" in row
                            for row in self.validate(broken)))

    def test_explicitly_unavailable_original_reference_passes(self) -> None:
        page = GOOD_HTML.replace(
            'data-source-key="atlas:2:original"\n   data-option="Family / B" data-trusted="true" data-variant="original"',
            'data-source-key="atlas:2:missing-original"\n   data-option="Family / B" data-trusted="false" data-evidence-unavailable="true" data-variant="original"')
        self.assertEqual([], self.validate(page))

    def test_variant_specific_answer_field_fails(self) -> None:
        broken = GOOD_HTML.replace('data-field="NW"', 'data-field="hd-NW"')
        self.assertTrue(any("asset variant" in row for row in self.validate(broken)))

    def test_current_assignment_requires_uniform_reference(self) -> None:
        broken = GOOD_HTML.replace('data-current-option="Family / A"',
                                   'data-current-option="Family / Missing"')
        self.assertTrue(any("current assignment" in row
                            for row in self.validate(broken)))

    def test_keep_current_escape_does_not_launder_questioned_reference(self) -> None:
        page = GOOD_HTML.replace(
            'data-current-option="Family / A"><option selected>Family / A</option>',
            'data-current-option="Keep current assignment">'
            '<option selected>Keep current assignment</option><option>Family / A</option>')
        self.assertEqual([], self.validate(page))

    def test_questioned_source_cannot_be_trusted_reference(self) -> None:
        broken = GOOD_HTML.replace('data-source-key="atlas:1"',
                                   'data-source-key="atlas:9"')
        self.assertTrue(any("questioned source" in row for row in self.validate(broken)))

    def test_wrong_serialization_order_fails(self) -> None:
        manifest = dict(GOOD_MANIFEST)
        manifest["serializationOrder"] = ["NW", "NE", "SW", "SE"]
        self.assertTrue(any("serializationOrder" in row
                            for row in self.validate(manifest=manifest)))

    def test_checkbox_and_not_answered_fail(self) -> None:
        broken = GOOD_HTML.replace(
            '<textarea class="note"></textarea>',
            '<input type="checkbox"><option>Not answered</option>'
            '<textarea class="note"></textarea>')
        errors = self.validate(broken)
        self.assertTrue(any("checkbox" in row for row in errors))
        self.assertTrue(any("Not answered" in row for row in errors))

    def test_unreconciled_marked_item_fails(self) -> None:
        manifest = dict(GOOD_MANIFEST)
        manifest["markedItemKeys"] = ["atlas-slot-corner", "missing-mark"]
        self.assertTrue(any("missing-mark" in row
                            for row in self.validate(manifest=manifest)))

    def test_handoff_requires_browser_validation(self) -> None:
        manifest = dict(GOOD_MANIFEST)
        manifest["browserValidation"] = {"status": "not-run"}
        self.assertEqual([], self.validate(manifest=manifest, handoff=False))
        self.assertTrue(any("browserValidation" in row
                            for row in self.validate(manifest=manifest, handoff=True)))


if __name__ == "__main__":
    unittest.main()
