#!/usr/bin/env python3
"""Shared candidate-coverage policy for semantic-atlas inverse audits."""

from __future__ import annotations

import base64
from collections import defaultdict


PROVISIONAL_REASON = (
    "PROVISIONAL current-catalog A/A/A/A candidate; not human-confirmed"
)

R122_REPORTED_SOURCE_CALIBRATION = {
    "reviewedCorners": 48,
    "rankOneMatches": 47,
    "rankOneAccuracy": 47 / 48,
    "changedCorners": 15,
    "rankOneMatchesOnChangedCorners": 14,
}

# These close or context-dependent families caused the only R122 rank-one
# failure and most of the formerly threshold-suppressed correct suggestions.
# They must remain visibly co-ranked in reported-source review; this list is
# not an alias/merge policy and never authorizes unattended relabeling.
R122_ATTENTION_FAMILY_SETS = (
    frozenset(("Sand / Common grayish", "Sand / Gravel", "Sand / Yellow")),
    frozenset(("Sand / Common", "Sand / Light", "Sand / Grainy",
               "Sand / Dark pink")),
    frozenset(("Water / Still", "Sand / Underwater",
               "Rock / Underwater emerald")),
    frozenset(("Grass / Burnt", "Grass / Faded", "Stone / Mossy")),
    frozenset(("Paving / Square", "Ground / Light")),
    frozenset(("Ground / Brown", "Ground / Dark brown")),
)


def reported_source_review_decision(current_family: str,
                                    family_ranking: list[dict]) -> dict:
    """Select review defaults for an explicitly user-reported authored source.

    This is deliberately separate from the conservative full-catalog alarm
    gate. A reported source is already mandatory human review, so hiding its
    rank-one result behind a global feature-distance threshold provides no
    safety and made 13/15 R122 corrections unnecessarily manual. The result is
    questionnaire guidance only: it never authorizes unattended correction.
    """
    if not family_ranking:
        return {
            "reviewWorthy": True, "preselectFamily": current_family,
            "reason": "reported-source-without-ranked-candidate",
            "automaticCorrectionAllowed": False,
        }
    leader = family_ranking[0]
    attention = sorted({family for group in R122_ATTENTION_FAMILY_SETS
                        if leader["family"] in group or current_family in group
                        for family in group}, key=str.casefold)
    return {
        "reviewWorthy": True,
        "preselectFamily": leader["family"],
        "reason": (
            "reported-source-rank-one-agrees" if leader["family"] == current_family
            else "reported-source-rank-one-disagrees"),
        "featureDistance": leader.get("featureDistance"),
        "attentionFamilies": attention,
        "showAtLeastTopThree": True,
        "allFourCornersEditable": True,
        "automaticCorrectionAllowed": False,
        "calibration": dict(R122_REPORTED_SOURCE_CALIBRATION),
    }


def promote_prior_batch_uniform_decisions(
        document: dict,
        trusted: dict[tuple[str, int], list[str]],
        excluded: set[tuple[str, int]],
        source: str) -> dict:
    """Turn reviewed A/A/A/A targets into later-batch reference evidence.

    A questionnaire target must be excluded while that questionnaire is being
    audited. Once the user explicitly confirms all four corners as one family,
    retaining that exclusion in later audits discards the strongest available
    same-atlas comparator and creates systematic false disagreements.
    """
    promoted = []
    for namespace, slots in document.get("cornerCorrections", {}).items():
        for slot_text, mapping in slots.items():
            if not isinstance(mapping, dict):
                continue
            values = [mapping.get(corner) for corner in ("NW", "NE", "SE", "SW")]
            if any(value is None for value in values) or len(set(values)) != 1:
                continue
            key = (namespace.casefold(), int(slot_text))
            excluded.discard(key)
            trusted[key].append(
                f"explicit prior-batch four-corner uniform in {source}")
            promoted.append({
                "namespace": namespace,
                "slot": int(slot_text),
                "family": values[0],
            })
    return {"promotedUniforms": promoted, "promotedCount": len(promoted)}


def independent_corner_review_decision(
        current_family: str,
        target_binding_pixel_sha256: str,
        ranking: dict,
        *,
        current_family_has_anchor: bool,
        exact_family_ambiguity: bool) -> dict:
    """Conservatively decide whether rank one merits human corner review.

    Topology is deliberately absent. A second-place family never replaces the
    independent leader merely because it occurs elsewhere in the tile. Exact
    decoded pixels may cross atlas bindings; non-exact evidence must use a
    human-reviewed uniform from the target physical binding and beat the best
    current-family uniform in both the feature and residual comparisons.

    Uniform targets are not protected specially: sufficiently strong
    independent evidence can reopen a nominal or previously reviewed uniform,
    but only for human review and never for unattended relabeling.
    """
    top = list(ranking.get("top3", []))
    if not top:
        return {"reviewWorthy": False, "reason": "no-ranked-candidate"}
    leader = top[0]
    if leader.get("family") == current_family:
        return {"reviewWorthy": False, "reason": "rank-one-agrees"}
    if not current_family_has_anchor:
        return {"reviewWorthy": False, "reason": "no-current-family-comparator"}
    if exact_family_ambiguity:
        return {"reviewWorthy": False, "reason": "exact-family-ambiguity"}

    exact_families = set(ranking.get("exactFamilies", []))
    exact_conflict = (
        bool(leader.get("exactDecodedCornerPixels"))
        and leader.get("family") in exact_families
        and current_family not in exact_families
    )
    if exact_conflict:
        same_binding = (
            leader.get("sourceBindingPixelSha256")
            == target_binding_pixel_sha256)
        if (not same_binding
                and leader.get("trustTier") != "human-reviewed"):
            return {
                "reviewWorthy": False,
                "reason": "unqualified-exact-cross-binding-leader",
            }
        return {
            "reviewWorthy": True,
            "reason": "unique-exact-decoded-corner",
            "currentComparator": next(
                (row for row in top if row.get("family") == current_family),
                None),
        }

    if leader.get("sourceBindingPixelSha256") != target_binding_pixel_sha256:
        return {"reviewWorthy": False, "reason": "non-exact-cross-binding-leader"}
    if leader.get("trustTier") != "human-reviewed":
        return {"reviewWorthy": False, "reason": "non-exact-provisional-leader"}

    current = next(
        (row for row in top[1:] if row.get("family") == current_family), None)
    if current is None:
        return {"reviewWorthy": False, "reason": "current-family-not-in-top-three"}
    leader_feature = float(leader.get("featureDistance", float("inf")))
    current_feature = float(current.get("featureDistance", float("inf")))
    feature_advantage = (
        (current_feature - leader_feature) / max(current_feature, 1e-12))
    leader_residual = float(leader.get("compositeResidual", float("inf")))
    current_residual = float(current.get("compositeResidual", float("inf")))
    residual_advantage = (
        (current_residual - leader_residual) / max(current_residual, 1e-12))
    near_exact = (
        float(leader.get("rawPixelMAE", float("inf"))) <= 0.01
        and float(leader.get("colorNormalizedMAE", float("inf"))) <= 0.015
    )
    review = bool(
        near_exact and feature_advantage >= 0.25 and residual_advantage >= 0.10)
    return {
        "reviewWorthy": review,
        "reason": (
            "strong-local-rank-one-vs-current"
            if review else "insufficient-local-rank-one-evidence"),
        "featureAdvantageOverCurrent": round(feature_advantage, 7),
        "residualAdvantageOverCurrent": round(residual_advantage, 7),
        "currentComparator": current,
    }


def expand_all_catalog_uniform_variants(
        catalog: dict,
        trusted: dict[tuple[str, int], list[str]],
        excluded: set[tuple[str, int]]) -> dict:
    """Add every nonexcluded nominal uniform as a labeled audit candidate.

    A reviewed anchor proves a family is representable, but it does not prove
    that one artwork variant generated every authored blend. Exhaustive inverse
    comparison therefore needs every remaining A/A/A/A slot and every legal
    rotation. Existing human trust is preserved; newly added slots remain
    provisional and can only support review, never unattended relabeling.
    """
    additions: dict[str, list[dict]] = defaultdict(list)
    added_keys = set()
    eligible_keys = set()
    for preset in catalog["presets"]:
        namespace = preset["namespace"].casefold()
        payload = base64.b64decode(preset["signaturesBase64"], validate=True)
        names = {int(row["id"]): row["name"] for row in preset["families"]}
        for slot in range(int(preset["slotCount"])):
            signature = payload[slot * 4:slot * 4 + 4]
            if len(set(signature)) != 1:
                continue
            key = (namespace, slot)
            if key in excluded:
                continue
            eligible_keys.add(key)
            if key in trusted:
                continue
            trusted[key].append(PROVISIONAL_REASON)
            added_keys.add(key)
            additions[names[signature[0]]].append({
                "namespace": namespace, "slot": slot})
    return {
        "eligibleVariantCount": len(eligible_keys),
        "addedVariantCount": len(added_keys),
        "addedVariantsByFamily": dict(additions),
    }


def expand_missing_family_candidates(
        catalog: dict,
        trusted: dict[tuple[str, int], list[str]],
        excluded: set[tuple[str, int]],
        maximum_per_missing_family: int = 4) -> dict:
    """Represent every family without promoting nominal uniforms to truth.

    Human-reviewed references remain authoritative. Only families absent from
    that pool receive bounded provisional A/A/A/A sources. Callers must expose
    the provisional tier and must never use it for unattended relabeling.
    """
    family_for_key = {}
    candidates: dict[str, list[tuple[str, int]]] = defaultdict(list)
    for preset in catalog["presets"]:
        namespace = preset["namespace"].casefold()
        payload = base64.b64decode(preset["signaturesBase64"], validate=True)
        names = {int(row["id"]): row["name"] for row in preset["families"]}
        for slot in range(int(preset["slotCount"])):
            signature = payload[slot * 4:slot * 4 + 4]
            if len(set(signature)) != 1:
                continue
            key = (namespace, slot)
            family = names[signature[0]]
            family_for_key[key] = family
            if key not in excluded:
                candidates[family].append(key)

    reviewed_families = {
        family_for_key[key] for key in trusted if key in family_for_key
    }
    additions: dict[str, list[dict]] = defaultdict(list)
    for family, keys in sorted(candidates.items(), key=lambda item: item[0].casefold()):
        if family in reviewed_families:
            continue
        chosen = []
        seen_namespaces = set()
        for key in keys:
            if key[0] in seen_namespaces and len(chosen) >= 2:
                continue
            chosen.append(key)
            seen_namespaces.add(key[0])
            if len(chosen) >= maximum_per_missing_family:
                break
        for key in chosen:
            trusted[key].append(PROVISIONAL_REASON)
            additions[family].append({"namespace": key[0], "slot": key[1]})

    represented = reviewed_families | set(additions)
    mapped = {row["name"] for preset in catalog["presets"]
              for row in preset["families"]}
    return {
        "reviewedFamilyNamesBeforeExpansion": sorted(
            reviewed_families, key=str.casefold),
        "provisionalCandidatesByFamily": dict(additions),
        "mappedFamilyNames": sorted(mapped, key=str.casefold),
        "representedFamilyNames": sorted(represented, key=str.casefold),
        "missingFamilyNames": sorted(mapped - represented, key=str.casefold),
    }
