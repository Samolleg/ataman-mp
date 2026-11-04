"""Validate the isolated dpi_minimap_check matrix and process receipts."""
import argparse
import json
from pathlib import Path


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("--phase", default="pass")
args = parser.parse_args()
failures = []
checks = 0


def check(condition, description):
    global checks
    checks += 1
    if not condition:
        failures.append(description)


for tag in ("100", "125", "150", "175", "mixed", "owner"):
    name = f"{args.phase}-{tag}"
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
