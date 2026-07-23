"""Compare one 8-position EI BON member from two staged RES archives."""

from __future__ import annotations

import hashlib
import importlib
import json
import os
from pathlib import Path
import struct

import numpy as np


def required(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"{name} is required")
    return value


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


left_path = Path(required("EI_BON_LEFT_RES")).resolve()
right_path = Path(required("EI_BON_RIGHT_RES")).resolve()
member = required("EI_BON_MEMBER")
report_path = Path(required("EI_BON_REPORT")).resolve()
report_path.parent.mkdir(parents=True, exist_ok=True)

resfile = importlib.import_module("ei_figer.resfile")


def read(path: Path) -> tuple[bytes, np.ndarray]:
    archive = resfile.ResFile(str(path))
    with archive.open(member, "r") as stream:
        raw = stream.read()
    if len(raw) != 96:
        raise RuntimeError(f"Expected a 96-byte eight-position BON member, got {len(raw)} bytes")
    values = np.asarray(struct.unpack("<24f", raw), dtype=np.float32).reshape((8, 3))
    return raw, values


left_raw, left = read(left_path)
right_raw, right = read(right_path)
delta = right.astype(np.float64) - left.astype(np.float64)
report = {
    "schema": 1,
    "status": "ok",
    "member": member,
    "left": {"res": str(left_path), "sha256": digest(left_raw), "positions": left.tolist()},
    "right": {"res": str(right_path), "sha256": digest(right_raw), "positions": right.tolist()},
    "byte_equal": left_raw == right_raw,
    "position_equal": bool(np.array_equal(left, right)),
    "mismatch_count": int(np.count_nonzero(left != right)),
    "max_abs_delta": float(np.max(np.abs(delta))) if delta.size else 0.0,
    "delta": delta.tolist(),
}
report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
print("EI_BON_SEMANTIC_DIFF", report_path, report["position_equal"])
