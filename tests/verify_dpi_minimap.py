"""Validate the isolated dpi_minimap_check matrix and process receipts."""
import argparse
import json
from pathlib import Path


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("--phase", default="pass")
parser.add_argument("--windows-scales", action="store_true")
args = parser.parse_args()
failures = []
checks = 0


def check(condition, description):
    global checks
    checks += 1
    if not condition:
        failures.append(description)


tags = ("100", "125", "150", "175", "200", "auto", "owner150") if args.windows_scales else ("100", "125", "150", "175", "mixed", "owner")
for tag in tags:
    prefix = "matrix" if args.phase == "pass" else args.phase
    name = (tag if tag == "owner150" and prefix == "matrix" else f"{prefix}-{tag}") if args.windows_scales else f"{args.phase}-{tag}"
    directory = args.root / name
    result = json.loads((directory / f"{name}.json").read_text(encoding="utf-8-sig"))
    process = json.loads((directory / "process.json").read_text(encoding="utf-8-sig"))
    check(process["exitCode"] == 0, f"{name} process exit")
    check(result["visibleNativeWindows"] == 0, f"{name} hidden windows")
    check(result["sceneReady"] and result["closed"], f"{name} ready scene and ordinary close")
    width, height = result["widget"][2:]
    dpr = result["dpr"]
    physical = [int(width * dpr + 0.5), int(height * dpr + 0.5)]
    check(result["glViewport"] == [0, 0] + physical, f"{name} physical GL viewport")
    check(result["rendererSize"] == physical, f"{name} renderer dimensions")
    check(result["queryReady"] and result["queryFboSize"] == physical, f"{name} query FBO")
    check(result["logicalPickReadsPhysicalPixel"], f"{name} logical-coordinate picking")
    if args.windows_scales:
        target = 1.5 if tag == "owner150" else (result["monitors"][0]["dpi"] / 96 if tag == "auto" else int(tag) / 100)
        check(abs(dpr - target) < 0.0001, f"{name} fractional DPR without rounding to integer")
        check(result["nativeClient"] == physical, f"{name} native drawable physical size")
        check(result["frameSize"] == physical, f"{name} physical offscreen scene readback")
        check(result["toolPanelHeight"] <= 220 and result["lastToolReachable"], f"{name} bounded toolbar with every tool reachable")
        for overlay in ("timeOfDayWidget", "environmentPreviewWidget", "aiGraphPreviewWidget"):
            check(result["minimapApartFrom_" + overlay], f"{name} compact minimap does not cover {overlay}")
        check(result["logicalDpi"] == 96 and result["uiScale"] == 100, f"{name} no second font scale")
        check(result["fontPixelSize"] == result["systemFontPixels"] > 0, f"{name} Windows font normalized to 96 DPI")
        check(result["fontHeight"] == result["applicationFontHeight"], f"{name} production font inherited by viewport")
        check(not result["calibratedFont"], f"{name} no test font calibration")
        # Toolbar source rasters provide at least 2x detail (all requested
        # 100..200% modes). Above that Qt scales the existing source normally.
        check(abs(min(result["iconPixmapDpr"], 2) - min(dpr, 2)) < 0.0001, f"{name} high-DPI toolbar icons")
        check(abs(result["minimapPixmapDpr"] - dpr) < 0.0001 and result["minimapClickCentered"], f"{name} sharp minimap and normalized picking")
        expected_sources = [[], ["gz1g_bz1g"], ["gz1g_gz2g"], ["gz1g_gz3g"], [], ["gz1g_gz7g"]]
        check(len(result["entrances"]) == 6, f"{name} all six genuine gz1g entrances")
        for entrance, sources in zip(result["entrances"], expected_sources):
            check(entrance["sources"] == sources, f"{name} incoming routes entrance {entrance['id']}")
            check(entrance["zone"] == "gz1g" and entrance["id"] == entrance["ordinaryExit"] == entrance["customExit"]
                  and entrance["ordinaryValid"] and entrance["customValid"], f"{name} correct zone and local entrance for both launch planners")
    check(result["zone"] == "gz1g" and result["minimapVisible"], f"{name} ordinary map minimap")
    check(result["minimapInside"] and not result["clippedByAncestor"], f"{name} visible viewport")
    check(result["inferredZone"] == "gz1g" and result["inferredMinimapVisible"], f"{name} MPR/MOB inference")
    check(not result["clippedButtons"], f"{name} button captions")
    for identity in result["identities"]:
        check(identity["requested"] == identity["resolved"], f"{name} explicit {identity['requested']}")
        check(identity["visible"] == (identity["requested"] != "bz1g"), f"{name} briefing visibility")
    for mode in result["sizeModes"]:
        check(mode["inside"], f"{name} minimap size {mode['level']}")
    check(result["collapsedInside"], f"{name} collapsed minimap")
    for overlay in result["overlays"]:
        x, y, w, h = overlay["rect"]
        check(x >= 0 and y >= 0 and x + w <= width and y + h <= height,
              f"{name} overlay {overlay['name']}")

print(json.dumps({"checks": checks, "failures": failures}, ensure_ascii=False, indent=2))
raise SystemExit(bool(failures))
