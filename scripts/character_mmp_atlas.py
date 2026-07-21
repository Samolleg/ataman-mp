#!/usr/bin/env python3
"""Decode and compose staged EI PNT3 character-atlas layers.

Inputs are standalone copied MMP files. Outputs are an ordinary straight-alpha
PNG for Blender and a JSON provenance report. The implementation mirrors
source/resources/mmp_codec.cpp; it never writes MMP or RES inputs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
from typing import Any

from PIL import Image


MMP_SIGNATURE = 0x00504D4D
PNT3 = 0x33544E50
MMP_HEADER_SIZE = 76


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def decode_pnt3(compressed: bytes, decoded_size: int) -> bytes:
    decoded = bytearray()
    cursor = 0
    while cursor + 4 <= len(compressed):
        value = struct.unpack_from("<I", compressed, cursor)[0]
        transparent_run = value != 0 and value & 0xFF000000 == 0
        if transparent_run:
            if value & 0x0F:
                raise ValueError("PNT3 transparent run is not four-pixel aligned")
            if value > decoded_size - len(decoded):
                raise ValueError("PNT3 transparent run exceeds decoded image bounds")
            decoded.extend(b"\0" * value)
            cursor += 4
            continue
        if cursor + 16 > len(compressed):
            raise ValueError("PNT3 literal block is truncated")
        if len(decoded) + 16 > decoded_size:
            raise ValueError("PNT3 literal data exceeds decoded image bounds")
        decoded.extend(compressed[cursor:cursor + 16])
        cursor += 16
    if cursor != len(compressed):
        raise ValueError("PNT3 payload is not dword-aligned")
    decoded.extend(b"\0" * (decoded_size - len(decoded)))
    return bytes(decoded)


def read_layer(path: Path) -> tuple[dict[str, Any], bytes]:
    data = path.read_bytes()
    if len(data) < MMP_HEADER_SIZE:
        raise ValueError(f"MMP header is truncated: {path}")
    signature, width, height, stored_size, pixel_format = struct.unpack_from("<5I", data)
    if signature != MMP_SIGNATURE:
        raise ValueError(f"Not an MMP texture: {path}")
    if pixel_format != PNT3:
        raise ValueError(f"Character composition requires PNT3, got 0x{pixel_format:08X}: {path}")
    payload_end = MMP_HEADER_SIZE + stored_size
    if payload_end > len(data):
        raise ValueError(f"PNT3 compressed size exceeds the MMP payload: {path}")
    premultiplied_bgra = decode_pnt3(data[MMP_HEADER_SIZE:payload_end], width * height * 4)
    return ({
        "path": str(path),
        "size": len(data),
        "sha256": sha256_bytes(data),
        "width": width,
        "height": height,
        "stored_size": stored_size,
        "pixel_format": "0x33544E50",
        "premultiplied_bgra_sha256": sha256_bytes(premultiplied_bgra),
        "trailing_bytes": len(data) - payload_end,
    }, premultiplied_bgra)


def composite(layers: list[bytes]) -> bytes:
    if not layers:
        raise ValueError("At least one PNT3 layer is required")
    size = len(layers[0])
    if any(len(layer) != size for layer in layers):
        raise ValueError("Texture layers have inconsistent dimensions")
    result = bytearray(size)
    for source in layers:
        for byte in range(0, size, 4):
            inverse_alpha = 255 - source[byte + 3]
            for channel in range(4):
                blended = source[byte + channel] + ((result[byte + channel] * inverse_alpha) >> 8)
                result[byte + channel] = min(255, blended)
    return bytes(result)


def straight_rgba(premultiplied_bgra: bytes) -> bytes:
    output = bytearray(len(premultiplied_bgra))
    for offset in range(0, len(output), 4):
        blue, green, red, alpha = premultiplied_bgra[offset:offset + 4]
        if alpha:
            red = min(255, (red * 255 + alpha // 2) // alpha)
            green = min(255, (green * 255 + alpha // 2) // alpha)
            blue = min(255, (blue * 255 + alpha // 2) // alpha)
        else:
            red = green = blue = 0
        output[offset:offset + 4] = bytes((red, green, blue, alpha))
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layer", action="append", required=True,
                        help="PNT3 MMP layer in bottom-to-top composition order")
    parser.add_argument("--output", required=True, help="staged straight-alpha PNG output")
    parser.add_argument("--report", required=True, help="staged JSON provenance report")
    parser.add_argument("--orientation", choices=("raw", "rotate-180", "mirror-x", "mirror-y"),
                        default="raw", help="PNG orientation adapter; decoded/composed hashes remain raw")
    args = parser.parse_args()

    layer_paths = [Path(value).resolve() for value in args.layer]
    output = Path(args.output).resolve()
    report_path = Path(args.report).resolve()
    if output.suffix.lower() != ".png":
        raise ValueError("Output must be a PNG")
    if output in layer_paths or report_path in layer_paths:
        raise ValueError("Output/report must not overwrite an input layer")
    input_parents = {path.parent for path in layer_paths}
    if output.parent in input_parents or report_path.parent in input_parents:
        raise ValueError("Output/report must be staged outside every input-layer directory")
    records: list[dict[str, Any]] = []
    decoded: list[bytes] = []
    dimensions: tuple[int, int] | None = None
    for path in layer_paths:
        if not path.is_file():
            raise FileNotFoundError(path)
        record, pixels = read_layer(path)
        current = (record["width"], record["height"])
        if dimensions is not None and current != dimensions:
            raise ValueError(f"Texture layers have inconsistent dimensions: {dimensions} vs {current}")
        dimensions = current
        records.append(record)
        decoded.append(pixels)

    assert dimensions is not None
    premultiplied = composite(decoded)
    rgba = straight_rgba(premultiplied)
    output.parent.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    image = Image.frombytes("RGBA", dimensions, rgba)
    transpose = {
        "rotate-180": Image.Transpose.ROTATE_180,
        "mirror-x": Image.Transpose.FLIP_LEFT_RIGHT,
        "mirror-y": Image.Transpose.FLIP_TOP_BOTTOM,
    }.get(args.orientation)
    if transpose is not None:
        image = image.transpose(transpose)
    output_rgba = image.tobytes()
    image.save(output, format="PNG", optimize=False)
    report = {
        "schema": 1,
        "status": "ok",
        "composition": "editor-compatible premultiplied BGRA source-over, divisor 256",
        "layers": records,
        "width": dimensions[0],
        "height": dimensions[1],
        "premultiplied_bgra_sha256": sha256_bytes(premultiplied),
        "straight_rgba_sha256": sha256_bytes(rgba),
        "output_orientation": args.orientation,
        "output_rgba_sha256": sha256_bytes(output_rgba),
        "output": {
            "path": str(output),
            "size": output.stat().st_size,
            "sha256": sha256_file(output),
        },
    }
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(report["output"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
