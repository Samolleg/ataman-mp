"""Replace one nested RES member while preserving every unrelated payload.

Required environment variables:
  EI_NESTED_SOURCE_RES   immutable complete source outer RES
  EI_NESTED_DONOR_RES    donor outer RES containing the edited nested member
  EI_NESTED_OUTPUT_RES   new output path; must not exist
  EI_NESTED_OUTER_MEMBER nested RES member in both outer archives (for example unhuma.mod)
  EI_NESTED_INNER_MEMBER member to copy from donor to source nested RES
  EI_NESTED_REPORT       JSON provenance report; must be outside the inputs

Run under the audited Blender/ei_figer lane so the exact resfile implementation
is available. This script never edits either input and refuses output overwrite.
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


source_path = Path(required("EI_NESTED_SOURCE_RES")).resolve()
donor_path = Path(required("EI_NESTED_DONOR_RES")).resolve()
output_path = Path(required("EI_NESTED_OUTPUT_RES")).resolve()
report_path = Path(required("EI_NESTED_REPORT")).resolve()
outer_member = required("EI_NESTED_OUTER_MEMBER")
inner_member = required("EI_NESTED_INNER_MEMBER")

if source_path == donor_path:
    raise RuntimeError("Source and donor must be distinct files")
if output_path in {source_path, donor_path}:
    raise RuntimeError("Output must not overwrite either input")
if report_path in {source_path, donor_path, output_path}:
    raise RuntimeError("Report must not overwrite an input or the output archive")
if output_path.exists():
    raise RuntimeError(f"Output already exists: {output_path}")
if not source_path.is_file() or not donor_path.is_file():
    raise RuntimeError("Source and donor RES files must exist")

resfile = importlib.import_module("ei_figer.resfile")
source_bytes = source_path.read_bytes()
donor_bytes = donor_path.read_bytes()
source_nested = read_member(resfile, source_bytes, outer_member)
donor_nested = read_member(resfile, donor_bytes, outer_member)
source_inner = read_member(resfile, source_nested, inner_member)
donor_inner = read_member(resfile, donor_nested, inner_member)
if source_inner == donor_inner:
    raise RuntimeError("Donor inner member is byte-identical to the source")

rebuilt_nested = replace_member(resfile, source_nested, inner_member, donor_inner)
rebuilt_outer = replace_member(resfile, source_bytes, outer_member, rebuilt_nested)
if read_member(resfile, read_member(resfile, rebuilt_outer, outer_member), inner_member) != donor_inner:
    raise RuntimeError("Rebuilt nested member does not match donor")

output_path.parent.mkdir(parents=True, exist_ok=True)
output_path.write_bytes(rebuilt_outer)
report = {
    "schema": 1,
    "status": "ok",
    "rebuild": "clean-source-order",
    "source": {"path": str(source_path), "size": len(source_bytes), "sha256": digest(source_bytes)},
    "donor": {"path": str(donor_path), "size": len(donor_bytes), "sha256": digest(donor_bytes)},
    "output": {"path": str(output_path), "size": len(rebuilt_outer), "sha256": digest(rebuilt_outer)},
    "outer_member": {
        "name": outer_member,
        "source_size": len(source_nested),
        "source_sha256": digest(source_nested),
        "output_size": len(rebuilt_nested),
        "output_sha256": digest(rebuilt_nested),
    },
    "inner_member": {
        "name": inner_member,
        "source_size": len(source_inner),
        "source_sha256": digest(source_inner),
        "donor_size": len(donor_inner),
        "donor_sha256": digest(donor_inner),
    },
}
report_path.parent.mkdir(parents=True, exist_ok=True)
report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
print("EI_NESTED_RES_REPLACE", output_path, report["output"]["sha256"])
