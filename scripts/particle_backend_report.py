#!/usr/bin/env python3
"""Fingerprint the isolated 3DfpFPU particle-billboard implementation."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any


REFERENCE_SHA256 = "A7B11BF5C85898C4A9E1F02F609F05C87BAC627834FAE21A3466230C63E6FC82"
REFERENCE_SIZE = 147456
MEMORY_IMAGE_SIZE = 155648
IMAGE_BASE = 0x10000000
VTABLE_RVA = 0x19300
SLOT = 0x60
RANGES = {
    "particle_billboard_transform": (0x6F00, 0x73BC),
}
SITES = {
    "source_position_and_scale_load": (0x6FCF, 0x22),
    "project_submitted_scale": (0x708A, 0x1F),
    "corner_minus_minus": (0x70D3, 0x20),
    "corner_plus_minus": (0x7174, 0x42),
    "corner_plus_plus": (0x721F, 0x42),
    "corner_minus_plus": (0x7243, 0xBE),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def report(dll_path: Path, memory_image_path: Path,
           require_reference: bool = False) -> dict[str, Any]:
    dll = dll_path.read_bytes()
    fingerprint = sha256(dll)
    if require_reference and fingerprint != REFERENCE_SHA256:
        raise ValueError(
            f"3DfpFPU.dll SHA-256 {fingerprint} does not match required {REFERENCE_SHA256}"
        )
    if require_reference and len(dll) != REFERENCE_SIZE:
        raise ValueError(
            f"3DfpFPU.dll size {len(dll)} does not match required {REFERENCE_SIZE}"
        )

    memory = memory_image_path.read_bytes()
    if len(memory) != MEMORY_IMAGE_SIZE:
        raise ValueError(
            f"memory image size {len(memory)} does not match required {MEMORY_IMAGE_SIZE}"
        )
    slot_offset = VTABLE_RVA + SLOT
    slot_target = struct.unpack_from("<I", memory, slot_offset)[0]
    if slot_target != IMAGE_BASE + RANGES["particle_billboard_transform"][0]:
        raise ValueError(
            f"interface slot +0x{SLOT:X} target 0x{slot_target:08X} is unexpected"
        )

    ranges = {}
    for name, (start, end) in RANGES.items():
        payload = memory[start:end]
        ranges[name] = {
            "start_rva": f"0x{start:08X}",
            "end_rva": f"0x{end:08X}",
            "size": len(payload),
            "sha256": sha256(payload),
        }
    sites = {}
    for name, (rva, size) in SITES.items():
        sites[name] = {"rva": f"0x{rva:08X}",
                       "bytes": memory[rva:rva + size].hex().upper()}

    return {
        "format": "ei-particle-backend-report-v1",
        "source": {
            "path": str(dll_path.resolve()),
            "size": len(dll),
            "sha256": fingerprint,
        },
        "memory_image": {
            "path": str(memory_image_path.resolve()),
            "size": len(memory),
            "sha256": sha256(memory),
            "image_base": f"0x{IMAGE_BASE:08X}",
        },
        "interface": {
            "vtable_rva": f"0x{VTABLE_RVA:08X}",
            "slot": f"0x{SLOT:02X}",
            "target_va": f"0x{slot_target:08X}",
            "target_rva": f"0x{slot_target - IMAGE_BASE:08X}",
        },
        "ranges": ranges,
        "sites": sites,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path)
    parser.add_argument("memory_image", type=Path,
                        help="isolated memory image from dump_d3dimm_interface")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-reference", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = report(args.dll, args.memory_image, args.require_reference)
        encoded = json.dumps(result, indent=2, ensure_ascii=False,
                             sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8", newline="\n")
        else:
            sys.stdout.write(encoded)
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
