#!/usr/bin/env python3
"""Fingerprint stable byte evidence from the read-only EI executable."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any


REFERENCE_SHA256 = "BE0FC330035E03983CCE590038CC7F881F1F5D1BCF082717F31776DD4DEE94E2"
RANGES = {
    "torch_particle_attachment": (0x40EB60, 0x40ECA5),
    "atlas_rectangle_builder": (0x4885B4, 0x488790),
    "spawn_callback": (0x48E9E0, 0x48EC20),
    "update_callback": (0x48EC20, 0x48ED60),
    "scheduler": (0x496EB0, 0x4975B0),
    "blend_switch": (0x498A8A, 0x498AC3),
}
SITES = {
    "torch_lookup_type_0x2001": (0x40EBD8, 18),
    "scheduler_update_call": (0x4972F5, 24),
    "scheduler_spawn_call": (0x497514, 24),
    "scheduler_fallback_spawn_call": (0x497575, 24),
    "blend_additive_value_2": (0x498A8A, 29),
    "blend_alpha_value_6": (0x498AA7, 28),
}
CONSTANTS = {
    "half_float": (0x73B86C, "f"),
    "three_tenths_float": (0x73C54C, "f"),
    "u32_normalization_double": (0x73C468, "d"),
    "half_double": (0x73CC88, "d"),
    "late_motion_float": (0x73CD18, "f"),
    "late_scale_growth_float": (0x73CE38, "f"),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


class PeImage:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 0x40 or data[:2] != b"MZ":
            raise ValueError("not a DOS/PE image")
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ValueError("PE header is missing or truncated")
        coff = pe_offset + 4
        section_count = struct.unpack_from("<H", data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", data, coff + 16)[0]
        optional = coff + 20
        if optional + optional_size > len(data):
            raise ValueError("PE optional header is truncated")
        magic = struct.unpack_from("<H", data, optional)[0]
        if magic != 0x10B:
            raise ValueError(f"expected PE32 optional header, found 0x{magic:04X}")
        self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
        sections_offset = optional + optional_size
        self.sections = []
        for index in range(section_count):
            offset = sections_offset + index * 40
            if offset + 40 > len(data):
                raise ValueError("PE section table is truncated")
            raw_name, virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<8s4I", data, offset)
            self.sections.append({
                "name": raw_name.rstrip(b"\0").decode("ascii", errors="replace"),
                "virtual_size": virtual_size, "rva": rva,
                "raw_size": raw_size, "raw_offset": raw_offset,
            })

    def read_va(self, va: int, size: int) -> bytes:
        rva = va - self.image_base
        for section in self.sections:
            extent = max(section["virtual_size"], section["raw_size"])
            if section["rva"] <= rva and rva + size <= section["rva"] + extent:
                offset = section["raw_offset"] + rva - section["rva"]
                if offset + size <= len(self.data):
                    return self.data[offset:offset + size]
        raise ValueError(f"VA range 0x{va:08X}+0x{size:X} is not file-backed")


def report(path: Path, required_sha256: str | None = None) -> dict[str, Any]:
    data = path.read_bytes()
    fingerprint = sha256(data)
    if required_sha256 and fingerprint.upper() != required_sha256.upper():
        raise ValueError(f"executable SHA-256 {fingerprint} does not match required {required_sha256.upper()}")
    image = PeImage(data)
    ranges = {}
    for name, (start, end) in RANGES.items():
        payload = image.read_va(start, end - start)
        ranges[name] = {"start_va": f"0x{start:08X}", "end_va": f"0x{end:08X}",
                        "size": len(payload), "sha256": sha256(payload)}
    sites = {}
    for name, (address, size) in SITES.items():
        sites[name] = {"va": f"0x{address:08X}",
                       "bytes": image.read_va(address, size).hex().upper()}
    constants = {}
    for name, (address, kind) in CONSTANTS.items():
        payload = image.read_va(address, struct.calcsize("<" + kind))
        constants[name] = {"va": f"0x{address:08X}", "bytes": payload.hex().upper(),
                           "value": struct.unpack("<" + kind, payload)[0]}
    vtable_entry = struct.unpack("<I", image.read_va(0x73CFD0 + 0x18, 4))[0]
    return {
        "format": "ei-particle-binary-report-v1",
        "source": {"path": str(path.resolve()), "size": len(data), "sha256": fingerprint},
        "image_base": f"0x{image.image_base:08X}", "sections": image.sections,
        "particle_vtable_scheduler_slot": {
            "slot_va": "0x0073CFE8", "target_va": f"0x{vtable_entry:08X}"},
        "ranges": ranges, "sites": sites, "constants": constants,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--output", type=Path, help="JSON destination; stdout when omitted")
    parser.add_argument("--require-reference", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = report(args.exe, REFERENCE_SHA256 if args.require_reference else None)
        encoded = json.dumps(result, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8", newline="\n")
        else:
            sys.stdout.write(encoded)
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
