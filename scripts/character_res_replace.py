"""Cleanly replace one member in a RES while preserving unrelated payloads.

Required environment variables:
  EI_RES_SOURCE   immutable complete source RES
  EI_RES_DONOR    donor RES containing the edited member
  EI_RES_OUTPUT   new output path; must not exist
  EI_RES_MEMBER   member to copy from donor to source
  EI_RES_REPORT   JSON provenance report; separate from inputs/output

Run under the audited Blender/ei_figer lane. The script rebuilds in source
member order and never edits either input.
"""

from __future__ import annotations

import hashlib
import importlib
import io
import json
import os
from pathlib import Path


def required(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"{name} is required")
    return value


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_member(resfile, archive_bytes: bytes, name: str) -> bytes:
    archive = resfile.ResFile(io.BytesIO(archive_bytes))
    with archive.open(name, "r") as stream:
        return stream.read()


def replace_member(resfile, archive_bytes: bytes, name: str, payload: bytes) -> bytes:
    source = resfile.ResFile(io.BytesIO(archive_bytes))
    output_buffer = io.BytesIO()
    output = resfile.ResFile(output_buffer, "w")
    found = False
    for info in source.iter_files():
        with source.open(info.name, "r") as stream:
            member_payload = stream.read()
        if info.name == name:
            member_payload = payload
            found = True
        with output.open(info.name, "w") as stream:
            stream.write(member_payload)
    output.close()
    if not found:
        raise RuntimeError(f"Member is absent from source archive: {name}")
    return output_buffer.getvalue()


source_path = Path(required("EI_RES_SOURCE")).resolve()
donor_path = Path(required("EI_RES_DONOR")).resolve()
output_path = Path(required("EI_RES_OUTPUT")).resolve()
report_path = Path(required("EI_RES_REPORT")).resolve()
member = required("EI_RES_MEMBER")

if source_path == donor_path:
    raise RuntimeError("Source and donor must be distinct files")
if output_path in {source_path, donor_path}:
    raise RuntimeError("Output must not overwrite either input")
if report_path in {source_path, donor_path, output_path}:
    raise RuntimeError("Report must not overwrite an input or output")
if output_path.exists():
    raise RuntimeError(f"Output already exists: {output_path}")
if not source_path.is_file() or not donor_path.is_file():
    raise RuntimeError("Source and donor RES files must exist")

resfile = importlib.import_module("ei_figer.resfile")
source_bytes = source_path.read_bytes()
donor_bytes = donor_path.read_bytes()
source_member = read_member(resfile, source_bytes, member)
donor_member = read_member(resfile, donor_bytes, member)
if source_member == donor_member:
    raise RuntimeError("Donor member is byte-identical to the source")

rebuilt = replace_member(resfile, source_bytes, member, donor_member)
if read_member(resfile, rebuilt, member) != donor_member:
    raise RuntimeError("Rebuilt member does not match donor")

output_path.parent.mkdir(parents=True, exist_ok=True)
output_path.write_bytes(rebuilt)
report = {
    "schema": 1,
    "status": "ok",
    "rebuild": "clean-source-order",
    "source": {"path": str(source_path), "size": len(source_bytes), "sha256": digest(source_bytes)},
    "donor": {"path": str(donor_path), "size": len(donor_bytes), "sha256": digest(donor_bytes)},
    "output": {"path": str(output_path), "size": len(rebuilt), "sha256": digest(rebuilt)},
    "member": {
        "name": member,
        "source_size": len(source_member),
        "source_sha256": digest(source_member),
        "donor_size": len(donor_member),
        "donor_sha256": digest(donor_member),
    },
}
report_path.parent.mkdir(parents=True, exist_ok=True)
report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
print("EI_RES_REPLACE", output_path, report["output"]["sha256"])
