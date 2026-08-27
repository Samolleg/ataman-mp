#!/usr/bin/env python3
"""Render one consistent questionnaire for every outstanding R90/R91 item."""

from __future__ import annotations

import argparse
import base64
import hashlib
import html
import json
import shutil
from pathlib import Path

from PIL import Image

from generate_terrain_atlas_presets import editor_tiles, read_member, res_index


CORNER_ORDER = ("NW", "NE", "SE", "SW")
DISPLAY_ORDER = ("NW", "NE", "SW", "SE")


def require_base_resolution_original_tiles(tiles, namespace: str,
                                           archive: Path) -> None:
    """Reject HD/mod replacement pages mislabeled as original-game evidence.

    The shipped base-game terrain pages decode to 64px (occasionally 128px)
    tiles.  JMV's replacement ``textures.res`` contains 256px terrain pages
    under many of the same namespace/slot names.  Same-name lookup alone is
    therefore not counterpart provenance and previously caused replacement
    artwork to be presented as ``ORIGINAL``.
    """
    sizes = {(int(tile.shape[1]), int(tile.shape[0])) for tile in tiles}
    if not sizes or any(width > 128 or height > 128
                        for width, height in sizes):
        raise ValueError(
            f"{archive} is not base-resolution original evidence for "
            f"{namespace}: decoded tile sizes {sorted(sizes)}")


def sha256(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            value.update(block)
    return value.hexdigest().upper()


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


class Renderer:
    def __init__(self, r90_audit: Path, r72_audit: Path, presets: Path,
                 hd_archive: Path, original_archive: Path,
                 output: Path) -> None:
        self.r90_path = r90_audit
        self.r72_path = r72_audit
        self.r90 = json.loads(r90_audit.read_text(encoding="utf-8"))
        self.r72 = json.loads(r72_audit.read_text(encoding="utf-8"))
        self.catalog = json.loads(presets.read_text(encoding="utf-8"))
        self.by_namespace = {
            row["namespace"].casefold(): row for row in self.catalog["presets"]}
        self.hd_archive = hd_archive
        self.hd_index = res_index(hd_archive)
        self.original_archive = original_archive
        self.original_index = res_index(original_archive)
        self.output = output
        self.images = output.parent / "images"
        self.images.mkdir(parents=True, exist_ok=True)
        for stale in self.images.iterdir():
            if stale.is_file():
                stale.unlink()
        self.copied: dict[tuple[str, str], str] = {}
        self.tile_cache: dict[tuple[str, str], list] = {}
        self.cards: list[str] = []
        self.question_keys: list[str] = []
        self.questioned_sources: set[str] = set()

    def copy_asset(self, root: Path, record: dict, prefix: str = "") -> str:
        relative = record["path"]
        key = (str(root), relative)
        if key in self.copied:
            return self.copied[key]
        source = root / relative
        if not source.is_file():
            raise FileNotFoundError(source)
        name = prefix + source.name
        target = self.images / name
        if target.exists() and sha256(target) != sha256(source):
            target = self.images / f"{sha256(source)[:10]}-{name}"
        # Questionnaire assets are normalized to one raw canvas size as well
        # as one CSS size.  This prevents a 64/128px source from being silently
        # magnified beside a native-256 reference while the boxes merely look
        # equal.  Variant rows stay separate, so both sides of a comparison
        # undergo the same normalization.
        with Image.open(source) as image:
            image = image.convert("RGB")
            if image.size != (256, 256):
                image = image.resize((256, 256), Image.Resampling.NEAREST)
            image.save(target, optimize=True)
        result = f"images/{target.name}"
        self.copied[key] = result
        return result

    def atlas_tiles(self, namespace: str, variant: str = "hd"):
        key = (variant, namespace.casefold())
        if key in self.tile_cache:
            return self.tile_cache[key]
        preset = self.by_namespace[namespace.casefold()]
        archive = self.hd_archive if variant == "hd" else self.original_archive
        index = self.hd_index if variant == "hd" else self.original_index
        part_count = int(preset["slotCount"]) // 64
        for part in range(part_count):
            name = f"{preset['namespace']}{part:03d}.mmp".casefold()
            if name not in index:
                raise KeyError(f"missing {name} in {variant} archive")
        tiles = editor_tiles(archive, index,
                             preset["namespace"], part_count)
        if variant == "original":
            require_base_resolution_original_tiles(
                tiles, preset["namespace"], archive)
        self.tile_cache[key] = tiles
        return tiles

    def save_tile(self, namespace: str, slot: int, prefix: str = "atlas",
                  variant: str = "hd", rotation_clockwise: int = 0) -> str:
        rotation_clockwise %= 360
        # Asset identity, not card position, controls the filename so hundreds
        # of repeated references share one PNG instead of consuming disk space.
        name = (f"tile-{variant}-{namespace.casefold()}-slot{slot}-"
                f"r{rotation_clockwise}.png")
        target = self.images / name
        if not target.exists():
            tile = self.atlas_tiles(namespace, variant)[slot]
            image = Image.fromarray(tile[:, :, :3], "RGB")
            if rotation_clockwise:
                image = image.rotate(-rotation_clockwise)
            if image.size != (256, 256):
                image = image.resize((256, 256), Image.Resampling.NEAREST)
            image.save(target, optimize=True)
        return f"images/{name}"

    def native_size(self, namespace: str, slot: int, variant: str) -> tuple[int, int]:
        tile = self.atlas_tiles(namespace, variant)[slot]
        return int(tile.shape[1]), int(tile.shape[0])

    def corner_families(self, namespace: str, slot: int) -> dict[str, str]:
        preset = self.by_namespace[namespace.casefold()]
        signatures = base64.b64decode(preset["signaturesBase64"])
        values = signatures[slot * 4:slot * 4 + 4]
        family_names = {int(row["id"]): row["name"] for row in preset["families"]}
        return {corner: family_names.get(int(value), f"Family {int(value)}")
                for corner, value in zip(CORNER_ORDER, values)}

    def semantic_detail(self, namespace: str, slot: int, *,
                        questioned: bool = False) -> str:
        corners = self.corner_families(namespace, slot)
        prefix = "Current catalog says " if questioned else ""
        unique = set(corners.values())
        if len(unique) == 1:
            return f"{prefix}Uniform: {next(iter(unique))}"
        return (f"{prefix}NW: {corners['NW']} · NE: {corners['NE']} | "
                f"SW: {corners['SW']} · SE: {corners['SE']}")

    @staticmethod
    def figure(src: str, title: str, detail: str = "", target: bool = False,
               corner: str | None = None, *, option: str = "",
               source_key: str = "", variant: str = "hd",
               role: str | None = None, trusted: bool = False,
               native_size: tuple[int, int] = (256, 256)) -> str:
        marker = f'<span class="corner-marker {corner.lower()}"></span>' if corner else ""
        klass = "tile target" if target else "tile"
        role = role or ("target" if target else "context")
        metadata = (f'data-role="{esc(role)}" data-source-key="{esc(source_key)}" '
                    f'data-option="{esc(option)}" data-trusted="{str(trusted).lower()}" '
                    f'data-variant="{esc(variant)}" data-comparison-kind="whole-tile" '
                    f'data-native-width="{native_size[0]}" data-native-height="{native_size[1]}"')
        return (f'<figure class="{klass}" {metadata}><div class="tile-image"><img src="{esc(src)}" '
                f'width="256" height="256">{marker}</div><figcaption><strong>{esc(title)}</strong>'
                f'<span>{esc(detail)}</span></figcaption></figure>')

    @staticmethod
    def options(values: list[str], default: str) -> str:
        ordered = [default] + [row for row in values if row != default]
        return "".join(
            f'<option value="{esc(row)}"{" selected" if row == default else ""}>'
            f'{esc(row + " (current)" if row == default else row)}</option>'
            for row in dict.fromkeys(ordered))

    def add_card(self, key: str, title: str, summary: str, body: str,
                 controls: str, defaults: dict[str, str]) -> None:
        number = len(self.cards) + 1
        self.question_keys.append(key)
        self.cards.append(
            f'<article class="question" data-key="{esc(key)}" '
            f'data-defaults="{esc(json.dumps(defaults, ensure_ascii=False))}">'
            f'<h2>{number}. {esc(title)}</h2><p class="summary">{esc(summary)}</p>'
            f'{body}<div class="controls">{controls}</div>'
            f'<label class="comment">Comment<textarea class="note" '
            f'placeholder="Optional comparison observation or family name"></textarea></label></article>')

    def whole_reference(self, record: dict, root: Path, label: str) -> str:
        evidence = record.get("evidence", record)
        asset = evidence.get("referenceWholeTile") or evidence.get("targetImage")
        if asset:
            src = self.copy_asset(root, asset, "r90-")
        elif record.get("sourceNamespace") is not None and record.get("sourceSlot") is not None:
            src = self.save_tile(
                record["sourceNamespace"], int(record["sourceSlot"]),
                "r90-reference", record.get("sourceVariant", "hd"),
                int(record.get("rotationClockwise", 0)))
        else:
            return ""
        detail = ""
        if record.get("sourceNamespace") is not None:
            detail = (f"{record['sourceNamespace']} slot {record['sourceSlot']}, "
                      f"{int(record.get('rotationClockwise', 0))} degrees clockwise. "
                      f"{self.semantic_detail(record['sourceNamespace'], int(record['sourceSlot']))}")
        source_key = ""
        if record.get("sourceNamespace") is not None:
            source_key = (f"{record['sourceNamespace'].casefold()}:{int(record['sourceSlot'])}:"
                          f"{record.get('sourceVariant', 'hd')}")
        return self.figure(src, label, detail, option=label, source_key=source_key,
                           variant=record.get("sourceVariant", "hd"),
                           role="reference", trusted=True)

    def family_reference_specs(self, family_name: str, option: str,
                               *, limit: int = 3,
                               preferred_namespace: str | None = None,
                               include_all_preferred: bool = False) -> list[dict]:
        """Return catalog references outside the complete questioned-source set."""
        rendered: list[dict] = []
        preferred_owner = (preferred_namespace or "").casefold()
        presets = list(self.catalog["presets"])
        if preferred_owner:
            presets.sort(key=lambda row: 0 if row["namespace"].casefold()
                         == preferred_owner else 1)
        for preset in presets:
            family = next((row for row in preset["families"]
                           if row["name"] == family_name), None)
            if family is None:
                continue
            signatures = base64.b64decode(preset["signaturesBase64"])
            family_id = int(family["id"])
            uniform_slots = [slot for slot in range(int(preset["slotCount"]))
                             if list(signatures[slot * 4:slot * 4 + 4])
                             == [family_id] * 4]
            preferred_slot = family.get("thumbnailSlot")
            if preferred_slot in uniform_slots:
                uniform_slots.remove(preferred_slot)
                uniform_slots.insert(0, preferred_slot)
            candidates = [candidate for candidate in uniform_slots
                          if (f"{preset['namespace'].casefold()}:{candidate}:hd"
                              not in self.questioned_sources)]
            if not (include_all_preferred
                    and preset["namespace"].casefold() == preferred_owner):
                candidates = candidates[:1]
            for slot in candidates:
                try:
                    self.atlas_tiles(preset["namespace"], "hd")[slot]
                    self.atlas_tiles(preset["namespace"], "original")[slot]
                except (KeyError, IndexError):
                    continue
                rendered.append({"namespace": preset["namespace"], "slot": slot,
                                 "title": family_name, "option": option,
                                 "rotation": 0})
                if len(rendered) >= limit:
                    return rendered
        return rendered

    def reference_figure(self, spec: dict, variant: str,
                         *, prefix: str = "family-reference") -> str:
        namespace = spec["namespace"]
        slot = int(spec["slot"])
        rotation = int(spec.get("rotation", 0))
        src = self.save_tile(namespace, slot, prefix, variant, rotation)
        native = self.native_size(namespace, slot, variant)
        return self.figure(
            src, spec["title"],
            (f"{namespace} slot {slot}, {rotation} degrees clockwise. "
             f"Native {native[0]}x{native[1]}; displayed 256x256. "
             f"{self.semantic_detail(namespace, slot)}"),
            option=spec["option"],
            source_key=f"{namespace.casefold()}:{slot}:{variant}",
            variant=variant, role="reference", trusted=True,
            native_size=native)

    def target_figure(self, namespace: str, slot: int, variant: str,
                      title: str, *, corner: str | None = None,
                      prefix: str = "review-target") -> str:
        src = self.save_tile(namespace, slot, prefix, variant)
        native = self.native_size(namespace, slot, variant)
        return self.figure(
            src, title,
            (f"{namespace} slot {slot} {variant.upper()}. "
             f"Native {native[0]}x{native[1]}; displayed 256x256. "
             f"{self.semantic_detail(namespace, slot, questioned=True)}"),
            True, corner, source_key=f"{namespace.casefold()}:{slot}:{variant}",
            variant=variant, native_size=native)

    def comparison_rows(self, namespace: str, slot: int, refs: list[dict],
                        title: str, *, corner: str | None = None,
                        prefix: str = "review") -> str:
        rows = []
        for variant in ("hd", "original"):
            figures = [self.target_figure(namespace, slot, variant, title,
                                          corner=corner, prefix=f"{prefix}-target")]
            figures += [self.reference_figure(spec, variant,
                                              prefix=f"{prefix}-reference")
                        for spec in refs]
            rows.append(
                f'<h4>{variant.upper()} evidence</h4><div class="comparison-row" '
                f'data-variant="{variant}" data-comparison-kind="whole-tile">'
                f'{"".join(figures)}</div>')
        return "".join(rows)

    def audit_specs(self, concrete_options: list[dict]) -> tuple[list[dict], list[str]]:
        specs: list[dict] = []
        supported: list[str] = []
        for option in concrete_options:
            option_specs = []
            for record in option.get("references", [])[:3]:
                if record.get("sourceNamespace") is None or record.get("sourceSlot") is None:
                    continue
                namespace = record["sourceNamespace"]
                slot = int(record["sourceSlot"])
                try:
                    self.atlas_tiles(namespace, "hd")[slot]
                    self.atlas_tiles(namespace, "original")[slot]
                except (KeyError, IndexError):
                    continue
                option_specs.append({
                    "namespace": namespace, "slot": slot,
                    "rotation": int(record.get("rotationClockwise", 0)),
                    "title": option["value"], "option": option["value"]})
            if option_specs:
                supported.append(option["value"])
                specs.extend(option_specs)
        return specs, supported

    def r90_cards(self) -> None:
        root = self.r90_path.parent
        scopes = self.r90["scopes"]

        scope = scopes["bz11kSlot462"]
        sections = []
        defaults = {}
        for corner in DISPLAY_ORDER:
            info = scope["corners"][corner]
            default = info["currentAssignment"]
            refs, values = self.audit_specs(info["concreteOptions"])
            current_refs = self.family_reference_specs(default, default)
            refs = current_refs + [row for row in refs if row["option"] != default]
            values = [default] + [row for row in values if row != default]
            defaults[corner] = default
            values += ["Different existing family (describe in comment)",
                       "New distinct family (name in comment)", "Still uncertain"]
            sections.append(
                f'<section class="corner"><h3>{corner}</h3>'
                f'{self.comparison_rows("bz11k", 462, refs, f"Target {corner}", corner=corner, prefix=f"bz11k-462-{corner.lower()}")}'
                f'<label>{corner} assignment<select class="answer" '
                f'data-field="{corner}" data-current-option="{esc(default)}">{self.options(values, default)}</select></label></section>')
        self.add_card(scope["key"], "bz11k / Zone14 slot 462 - all four corners",
                      "Previously suppressed because no binding error was proven; shown here because you flagged it.",
                      "".join(sections), "", defaults)

        scope = scopes["zone7Slot480SW"]
        combined_options = []
        seen_options = set()
        for variant in ("hd", "original"):
            for option in scope["variants"][variant]["concreteOptions"]:
                if option["value"] not in seen_options:
                    combined_options.append(option)
                    seen_options.add(option["value"])
        refs, values = self.audit_specs(combined_options)
        default = "Ground / Dirt"
        refs = self.family_reference_specs(default, default) + [
            row for row in refs if row["option"] != default]
        values = [default] + [row for row in values if row != default] + [
            "Different existing family (describe in comment)",
            "New distinct family (name in comment)", "Still uncertain"]
        sections = [
            f'<section class="corner"><h3>SW</h3>'
            f'{self.comparison_rows("Zone7", 480, refs, "Target SW", corner="SW", prefix="zone7-480-sw")}'
            f'<label>SW assignment<select class="answer" data-field="SW" data-current-option="{esc(default)}">'
            f'{self.options(values, default)}</select></label></section>']
        self.add_card(scope["key"], "Zone7 slot 480 SW - provisional identity",
                      "HD and original are both shown at full-tile 256x256 scale.",
                      "".join(sections), "", {"SW": default})

        scope = scopes["bz16hSlot169Unresolved"]
        sections, defaults = [], {}
        for corner in ("NE", "SE", "SW"):
            info = scope["corners"][corner]
            default = "Keep current assignment"
            current_family = self.corner_families("bz16h", 169)[corner]
            refs, values = self.audit_specs(info["concreteOptions"])
            refs = self.family_reference_specs(current_family, default) + refs
            defaults[corner] = default
            values += ["Different existing family (describe in comment)",
                       "New distinct family (name in comment)", "Still uncertain"]
            sections.append(
                f'<section class="corner"><h3>{corner}</h3>'
                f'{self.comparison_rows("bz16h", 169, refs, f"Target {corner}", corner=corner, prefix=f"bz16h-169-{corner.lower()}")}'
                f'<label>{corner} assignment<select class="answer" data-field="{corner}" data-current-option="{esc(default)}">{self.options(values, default)}</select></label></section>')
        self.add_card(scope["key"], "bz16h slot 169 - unresolved corners",
                      "NW Ground / Red is fixed context; compare the remaining corners without mixing crop and whole-tile scales.",
                      "".join(sections), "", defaults)

        scope = scopes["rockOrangeFamilyName"]
        specs = []
        values = ["Keep current name"]
        for name, target in scope["targets"].items():
            for option in target["options"]:
                for component in option.get("components", []):
                    if component.get("sourceNamespace") is not None:
                        specs.append({"namespace": component["sourceNamespace"],
                                      "slot": int(component["sourceSlot"]),
                                      "rotation": int(component.get("rotationClockwise", 0)),
                                      "title": option["value"], "option": option["value"]})
                        values.append(option["value"])
        bodies = []
        for name in scope["targets"]:
            namespace, slot_text = name.split(":", 1)
            bodies.append(self.comparison_rows(namespace, int(slot_text), specs,
                                               f"Target {name}", prefix=f"rock-name-{namespace.lower()}"))
        values += ["Different name (write in comment)", "Still uncertain"]
        controls = (f'<label>Family name<select class="answer" data-field="value" data-review-kind="family-name">'
                    f'{self.options(values, "Keep current name")}</select></label>')
        self.add_card(scope["key"], "Rock / Orange family name",
                      scope["auditInterpretation"],
                      "".join(bodies), controls,
                      {"value": "Keep current name"})

    def sand_cards(self) -> None:
        root = self.r72_path.parent
        rows = self.r72["r72SandStripe"]["importedUniformSources"]
        by_family = {}
        for row in rows:
            by_family.setdefault(row["material"], []).append(row)
        for row in rows:
            default = "Keep current family and keep as paint variant"
            figures = self.family_reference_specs(row["material"], default)
            figures += self.family_reference_specs(
                "Sand / Common", "Move to Sand / Common")
            figures += self.family_reference_specs(
                "Sand / Dark", "Move to Sand / Dark")
            values = ["Keep current family and keep as paint variant",
                      "Keep family identity but exclude from paint variants",
                      "Move to Sand / Common", "Move to Sand / Dark",
                      "New distinct family (name in comment)",
                      "Not uniform - assign all four corners in comment",
                      "Different family (describe in comment)", "Still uncertain"]
            key = f"r92-{row['sourceNamespace'].lower()}-slot{row['sourceSlot']}-sand"
            controls = (f'<label>Decision<select class="answer" data-field="value" data-current-option="{esc(default)}">'
                        f'{self.options(values, values[0])}</select></label>')
            flagged = (row["sourceNamespace"].casefold() in {"zone14", "bz11k"}
                       and row["sourceSlot"] in {218, 219})
            summary = ("Specifically flagged smooth/repeating Sand / Dark artwork. Valid HD binding does not dismiss the visual report."
                       if flagged else "Exact source used in the hand-reported Sand stripes; included regardless of binding validity.")
            self.add_card(key, f"{row['sourceNamespace']} slot {row['sourceSlot']} - {row['material']}",
                          summary,
                          self.comparison_rows(row["sourceNamespace"], int(row["sourceSlot"]),
                                               figures, "Target source", prefix=key),
                          controls, {"value": values[0]})

    def zone1_uniform_cards(self) -> None:
        root = self.r72_path.parent
        rows = self.r72["zone1Diagnostic"]["uniformSourcesTouched"]
        per_slot = {int(row["sourceSlot"]): row
                    for row in self.r72["zone1Diagnostic"]["perAuthoredSlotAudit"]}
        family_values = list(dict.fromkeys(row["currentFamily"] for row in rows))
        for row in rows:
            slot = int(row["sourceSlot"])
            audit = per_slot[slot]
            target = self.copy_asset(root, audit["targetImage"], "zone1-")
            values = [f"Uniform: {row['currentFamily']}",
                      *[f"Uniform: {name}" for name in family_values if name != row["currentFamily"]],
                      "Not uniform - assign all four corners in comment",
                      "New distinct family (name in comment)", "Still uncertain"]
            supported_values = []
            figures = []
            for value in values:
                if not value.startswith("Uniform: "):
                    continue
                family_name = value.removeprefix("Uniform: ")
                refs = self.family_reference_specs(family_name, value)
                if refs:
                    figures += refs
                    supported_values.append(value)
            values = supported_values + [
                "Not uniform - assign all four corners in comment",
                "New distinct family (name in comment)", "Still uncertain"]
            default = f"Uniform: {row['currentFamily']}"
            if default not in values:
                raise RuntimeError(f"no trusted reference for {default}")
            controls = (f'<label>Decision<select class="answer" data-field="value" data-current-option="{esc(default)}">'
                        f'{self.options(values, default)}</select></label>')
            self.add_card(f"r92-zone1-slot{slot}-reported-uniform",
                          f"Zone1 slot {slot} - reported uniform source",
                          "Used in the hand-reported painting diagnostic and previously omitted because the binding was valid.",
                          self.comparison_rows("Zone1", slot, figures, "Target source",
                                               prefix=f"zone1-{slot}-uniform"), controls,
                          {"value": default})

    def conflict_cards(self) -> None:
        alarms = self.r72["connectedAlarms"][0]["affectedZone1Rows"]
        grouped: dict[int, list[dict]] = {}
        for row in alarms:
            grouped.setdefault(int(row["sourceSlot"]), []).append(row)
        for slot, rows in grouped.items():
            sections, defaults = [], {}
            for row in rows:
                corner = row["corner"]
                default = row["currentFamily"]
                defaults[corner] = default
                figures = (self.family_reference_specs("Grass / Dark", "Grass / Dark")
                           + self.family_reference_specs("Grass / Common", "Grass / Common"))
                values = [default, "Grass / Common", "Different existing family (describe in comment)",
                          "New distinct family (name in comment)", "Still uncertain"]
                sections.append(
                    f'<section class="corner"><h3>{corner}</h3>'
                    f'{self.comparison_rows("Zone1", slot, figures, f"Target {corner}", corner=corner, prefix=f"zone1-{slot}-{corner.lower()}-conflict")}'
                    f'<label>{corner} assignment<select class="answer" data-field="{corner}" data-current-option="{esc(default)}">{self.options(values, default)}</select></label></section>')
            self.add_card(f"r92-zone1-slot{slot}-exact-patch-conflict",
                          f"Zone1 slot {slot} - suppressed exact corner conflict",
                          "The current Grass / Dark corner has an exact rotated pixel match to a Grass / Common reference. It was previously omitted because direction was unresolved.",
                          "".join(sections), "", defaults)

    def render(self) -> dict:
        sand_rows = self.r72["r72SandStripe"]["importedUniformSources"]
        zone1_rows = self.r72["zone1Diagnostic"]["uniformSourcesTouched"]
        conflict_rows = self.r72["connectedAlarms"][0]["affectedZone1Rows"]
        self.questioned_sources = {
            "bz11k:462:hd", "zone7:480:hd", "zone7:480:original",
            "bz16h:169:hd", "bz15h:2:hd", "zone17:2:hd",
            *(f"{row['sourceNamespace'].casefold()}:{int(row['sourceSlot'])}:hd"
              for row in sand_rows),
            *(f"zone1:{int(row['sourceSlot'])}:hd" for row in zone1_rows),
            *(f"zone1:{int(row['sourceSlot'])}:hd" for row in conflict_rows),
        }
        self.r90_cards()
        self.sand_cards()
        self.zone1_uniform_cards()
        self.conflict_cards()
        style = """
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;padding:18px;background:#202124;color:#f1f3f4;font:16px Segoe UI,Arial,sans-serif}h1,h2,h3{font-weight:500}.lead{max-width:1500px;border-left:5px solid #fbbc04;background:#292a2d;padding:12px}.toolbar{position:sticky;top:0;z-index:20;display:flex;gap:8px;align-items:center;flex-wrap:wrap;background:#17181b;border:1px solid #666;padding:10px}.toolbar button{font:inherit;color:#fff;background:#303134;border:1px solid #777;padding:8px;border-radius:4px}.toolbar .primary{background:#1769aa}#export{width:min(1500px,97vw);height:150px;background:#111;color:#eee}.question{max-width:1650px;margin:22px 0;padding:14px;background:#292a2d;border:1px solid #666;border-radius:8px}.summary{color:#fbbc04}.comparison-row{display:flex;gap:12px;overflow-x:auto;align-items:flex-start;padding-bottom:8px}.tile{flex:0 0 280px;margin:0;padding:8px;background:#17181b;border:1px solid #5f6368}.tile.target{border:3px solid #e57373}.tile-image{position:relative;width:256px;height:256px}.tile img{display:block;width:256px;height:256px;object-fit:contain;background:#000}.tile figcaption span{display:block;margin-top:3px}.corner-marker{position:absolute;width:50%;height:50%;border:5px solid #ff3b30;pointer-events:none}.corner-marker.nw{left:0;top:0}.corner-marker.ne{right:0;top:0}.corner-marker.sw{left:0;bottom:0}.corner-marker.se{right:0;bottom:0}.corner{margin:14px 0;padding:12px;background:#222326;border:1px solid #666}.controls,label{display:grid;gap:6px;max-width:1100px;margin-top:12px}.answer,.note{font:inherit;color:#fff;background:#303134;border:1px solid #777;padding:8px}.note{min-height:70px}@media(max-width:700px){.tile{flex-basis:216px}.tile-image,.tile img{width:192px;height:192px}}
"""
        script = r"""
const prefix='semantic-r92-complete:';const qs=[...document.querySelectorAll('.question')],out=document.getElementById('export'),status=document.getElementById('status');
function controls(q){return[...q.querySelectorAll('.answer')]}function defaults(q){return JSON.parse(q.dataset.defaults)}function state(q){const values={};for(const el of controls(q))values[el.dataset.field]=el.value;return{values,note:q.querySelector('.note').value.trim()}}function changed(q,s){const d=defaults(q);return Object.keys(s.values).some(k=>s.values[k]!==d[k])||!!s.note}function line(q,s){const order=['NW','NE','SE','SW','hd-SW','original-SW','value'];const fields=order.filter(k=>k in s.values).map(k=>k+'='+s.values[k]);return q.dataset.key+': '+fields.join(', ')+(s.note?' - '+s.note:'')}function text(){return['Semantic-atlas R92 complete review decisions:',...qs.map(q=>[q,state(q)]).filter(([q,s])=>changed(q,s)).map(([q,s])=>line(q,s)),'Apply only explicit changed decisions; untouched defaults remain context, not new confirmations.'].join('\n')}function update(m){out.value=text();status.textContent=m||qs.filter(q=>changed(q,state(q))).length+' changed response(s)'}function save(q){try{localStorage.setItem(prefix+q.dataset.key,JSON.stringify(state(q)))}catch(e){}update('Saved locally')}for(const q of qs){try{const s=JSON.parse(localStorage.getItem(prefix+q.dataset.key)||'null');if(s){for(const el of controls(q))if(s.values&&s.values[el.dataset.field])el.value=s.values[el.dataset.field];q.querySelector('.note').value=s.note||''}}catch(e){}for(const el of q.querySelectorAll('select,textarea'))el.addEventListener(el.tagName==='TEXTAREA'?'input':'change',()=>save(q))}function selectText(m){out.focus();out.select();update(m)}document.getElementById('select').onclick=()=>selectText('Answer text selected');document.getElementById('copy').onclick=async()=>{try{await navigator.clipboard.writeText(text());update('Answers copied')}catch(e){selectText('Clipboard unavailable; answer text selected')}};document.getElementById('download').onclick=()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([text()],{type:'text/plain;charset=utf-8'}));a.download='semantic-atlas-r92-answers.txt';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),0);update('Answers downloaded')};document.getElementById('submit').onclick=async()=>{if(window.openai&&typeof window.openai.sendFollowUpMessage==='function'){await window.openai.sendFollowUpMessage({prompt:text(),title:'Submit semantic-atlas R92 review'});update('Responses submitted')}else selectText('Direct submission unavailable; answer text selected')};document.getElementById('reset').onclick=()=>{for(const q of qs){localStorage.removeItem(prefix+q.dataset.key);const d=defaults(q);for(const el of controls(q))el.value=d[el.dataset.field];q.querySelector('.note').value=''}update('Local answers reset')};update();window.__SEMANTIC_R92_READY__={questions:qs.length,allImages256:true,displayOrder:['NW','NE','SW','SE'],serializationOrder:['NW','NE','SE','SW']};
"""
        document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Semantic atlas R92 complete review</title><style>{style}</style></head><body data-questionnaire-contract="semantic-atlas-questionnaire-v1"><h1>Semantic atlas R92 - complete outstanding review</h1><p class="lead">One format only. Every target and reference image is a full 256x256 tile shown at the same scale. The red quadrant marks the corner being judged. This includes the four R90 follow-ups, all nine reported Sand sources, all fourteen Zone1 uniform sources used in the reported diagnostic, and all eight previously suppressed exact corner conflicts across seven tiles.</p><div class="toolbar"><button class="primary" id="copy">Copy changed answers</button><button id="download">Download answers.txt</button><button id="select">Select answer text</button><button id="submit">Submit answers</button><button id="reset">Reset local answers</button><span id="status"></span></div><textarea id="export" readonly></textarea><main>{''.join(self.cards)}</main><script>{script}</script></body></html>'''
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.output.write_text(document, encoding="utf-8")
        manifest = {
            "schema": "ei-semantic-atlas-r92-complete-questionnaire-v1",
            "questionnaireContract": "semantic-atlas-questionnaire-v1",
            "questions": len(self.cards),
            "questionKeys": self.question_keys,
            "markedItemKeys": self.question_keys,
            "resolvedMarkedItemKeys": [],
            "displayOrder": list(DISPLAY_ORDER),
            "serializationOrder": list(CORNER_ORDER),
            "browserValidation": {
                "status": "not-run",
                "requiredForHandoff": True,
                "reason": "Must be set by an independent browser interaction check, never by the renderer."
            },
            "r90Questions": 4,
            "sandSources": 9,
            "zone1UniformSources": 14,
            "zone1ConflictTiles": 7,
            "zone1ConflictCorners": 8,
            "allDisplayedImages": "256x256 full tiles",
            "r90AuditSha256": sha256(self.r90_path),
            "r72AuditSha256": sha256(self.r72_path),
            "htmlSha256": sha256(self.output),
            "copiedOrExtractedImages": len(list(self.images.glob("*.png"))),
        }
        self.output.with_suffix(".manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r90-audit", required=True, type=Path)
    parser.add_argument("--r72-audit", required=True, type=Path)
    parser.add_argument("--presets", required=True, type=Path)
    parser.add_argument("--hd-archive", required=True, type=Path)
    parser.add_argument("--original-archive", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = Renderer(args.r90_audit, args.r72_audit, args.presets,
                      args.hd_archive, args.original_archive,
                      args.output).render()
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
