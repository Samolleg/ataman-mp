# Semantic atlas profile contract

Endurance's generic atlas conversion path treats automatic analysis as a
proposal, never as permission to paint. The proposal is derived only from the
decoded atlas tiles already loaded by the editor, the MPR's per-slot category
metadata, and packed tile usage observed in the open map.

The deterministic first-pass proposal:

- fingerprints the decoded RGBA tile sequence with SHA-256;
- clusters corner samples only within the same MPR tile category and proposed
  land/overlay layer;
- records a four-corner material-family signature for every atlas slot;
- classifies actual land-only and overlay-only use separately;
- proposes an underwater land family only when all observed land uses are
  beneath active overlays;
- flags unused, mixed-layer, and mixed underwater/non-underwater evidence as
  ambiguous; and
- reports family pairs for which it found no mixed transition slot.

Profiles use JSON schema version 1 and are bound to the case-insensitive MPR
namespace, decoded-atlas pixel hash, and exact slot count. A namespace, hash,
or count mismatch disables the profile. Inferred profiles begin with
`approved: false`; semantic catalog construction refuses them until a human
review has resolved every ambiguity and explicitly approved the profile.

The profile contains metadata and corner-family assignments only. It does not
contain atlas imagery, MPR/SEC payloads, resource archives, or installation
paths, and saving it does not modify an MPR, MMP, or RES archive. Manual atlas
tile painting remains the fallback when no reviewed profile is available.

This increment supplies the inference, validation, persistence, and approval
gate. The editor review surface can edit family names, split/merge decisions,
layer and underwater classification, then persist the reviewed profile through
this contract without changing terrain resources.
