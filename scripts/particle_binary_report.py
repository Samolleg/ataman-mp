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
    "global_render_particle_lookup": (0x40EB60, 0x40ECA5),
    "atlas_rectangle_builder": (0x4885B4, 0x488790),
    "tendri_factory_branch": (0x485B72, 0x485D58),
    "spawn_callback": (0x48E9E0, 0x48EC20),
    "update_callback": (0x48EC20, 0x48ED60),
    "type_0x2010_spawn_wrapper": (0x48ED60, 0x48EE20),
    "particle_attach_snapshot": (0x496A20, 0x496A60),
    "scheduler": (0x496EB0, 0x4975B0),
    "render_interpolation": (0x4976B0, 0x497CD5),
    "particle_batch_scheduler": (0x498480, 0x498620),
    "blend_switch": (0x498A8A, 0x498AC3),
    "logical_quant_loop": (0x4572FC, 0x45759A),
    "torch_client_particle_setup": (0x52D330, 0x52D40E),
    "world_quant_particle_dispatch": (0x5641F0, 0x56448A),
    "direct3d_particle_blend_setup": (0x56A1CD, 0x56A1F1),
}
SITES = {
    "global_render_lookup_type_0x2001": (0x40EBD8, 18),
    "global_render_lookup_call": (0x4575AC, 26),
    "spawn_initial_color_white": (0x48E9EC, 21),
    "tendri_callback_selection": (0x485C28, 37),
    "ordinary_update_scale_and_motion": (0x48ECFD, 96),
    "special_spawn_random_offset": (0x48EDC4, 64),
    "attach_copies_color_endpoint": (0x496A4A, 12),
    "scheduler_refreshes_motion_fields": (0x496ECC, 82),
    "scheduler_particle_snapshot": (0x4972DE, 48),
    "render_position_scale_interpolation": (0x497B4C, 76),
    "render_argb_interpolation": (0x497B98, 236),
    "scheduler_update_call": (0x4972F5, 24),
    "scheduler_spawn_call": (0x497514, 24),
    "scheduler_fallback_spawn_call": (0x497575, 24),
    "main_world_quant_call": (0x457338, 59),
    "world_particle_batch_call": (0x564471, 25),
    "batch_emitter_scheduler_call": (0x4984D4, 31),
    "torch_client_class_name": (0x52D190, 6),
    "torch_particle_factory_call": (0x52D38E, 18),
    "torch_particle_prewarm_loop": (0x52D3F0, 30),
    "blend_additive_value_2": (0x498A8A, 29),
    "blend_alpha_value_6": (0x498AA7, 28),
    "direct3d_source_blend_srcalpha": (0x56A1CD, 18),
    "direct3d_destination_blend_invsrcalpha": (0x56A1DF, 18),
}
CONSTANTS = {
    "half_float": (0x73B86C, "f"),
    "three_tenths_float": (0x73C54C, "f"),
    "u32_normalization_double": (0x73C468, "d"),
    "half_double": (0x73CC88, "d"),
    "late_motion_float": (0x73CD18, "f"),
    "late_scale_growth_float": (0x73CE38, "f"),
    "logical_quant_normalization_float": (0x73C348, "f"),
    "logical_quant_milliseconds": (0x78FF34, "I"),
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
