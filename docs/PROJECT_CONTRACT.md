# EI Mapper: Endurance Edition project contract

## Ataman fork scope

This product/data contract is retained by the local ei-mapper-ataman fork from
19168b91a10562525fb6c83b665275c2fd43cf4c. Active development and authorization
rules are in root AGENTS.md and CONTRIBUTING.md. The owner's 2026-10-01 request
authorizes initial publication of Ataman88 to private Samolleg/ataman-mp;
the owner requires a separate history export excluding game files and old
builds, with the original local history preserved.
further network/publication work needs its own explicit authorization.
Historical upstream references grant no permission to publish,
integrate, delete or mutate user data. Licensing and attribution remain intact.

Contributor onboarding is in [COAUTHOR_HANDOFF.md](COAUTHOR_HANDOFF.md).
The cleaned GitHub lineage has different commit IDs from the original local
history. A fresh clone uses its assigned accepted base, not a missing historical
SHA. This distinction changes neither the editing contract nor data ownership.

This document contains enduring product and compatibility rules. The roadmap
contains only version goals and approved implementation evidence.

## Editing contract

- Normal supported edits should feel normal: no per-field approval list or save
  preview merely because a wolf moved or a property changed.
- Preserve every source byte the user did not intentionally change and
  propagate every intended edit.
- Run raw-data checks, fresh reopen validation, backups, and atomic replacement
  automatically.
- Refuse only operations Endurance cannot represent safely. Treat refusal as an
  implementation gap rather than a permanent workflow.
- Opening, inspection, validation, and warnings never repair data automatically.
- Existing known defects may be preserved for compatibility only through an
  explicit policy; repair remains an intentional transaction.

## Compatibility evidence

1. **Structurally valid:** complete byte coverage and valid lengths/encoding.
2. **Profile compatible:** passes Base or JMV semantic/resource rules.
3. **Runtime validated:** passes recorded tests on fingerprinted components.

The game is the final compatibility target. Corpus checks, public tools, and
independent parsers provide supporting evidence, not a substitute for runtime
validation.

## Profile boundaries

- Base EI and JMV are separate runtime contracts.
- Base behavior must never require JMV data, DLL behavior, equipment layouts,
  map conventions, or launch facilities.
- Other mods opt into capabilities through their own future providers; they are
  not required to emulate JMV.
- DLL-defined behavior is never loaded into the editor process. Runtime adapters
  are fingerprinted, isolated, and fail closed.
- Test Current Map is an optional profile capability; core editing, validation,
  collections, and saving do not depend on it.

## MOB and object knowledge

- Preserve database/prototype, model, texture, template, inner-map name,
  `ParentID`, `Import`, `InScript`, model parts, and unknown fields even where
  their runtime effect is incomplete or profile-specific.
- Do not conflate model name with the MOB `Template` field.
- Ordinary humanoid appearance/equipment is resolved through the active
  profile. `Import` may switch relevant unit data to the MOB-embedded source;
  missing or conflicting evidence produces diagnostics, not mutation.
- Model-part arrays are serialized gameplay data. Preserve every token,
  including unknown tokens, exact ordering, and duplicates.
- Duplicate IDs and declared range overlap are evidence for validation, not
  authorization for silent renumbering.
- Static map makers may use a large scripted gate to force runtime graph
  regeneration. Preserve and diagnose that convention rather than treating it
  as ordinary malformed data.

## MPR, resources, and graph ownership

- MPR/SEC documents, MOB documents, AI graphs, profile resource caches, and
  renderer/view state have separate ownership and save lifecycles.
- A changed MPR never falls back to the legacy rebuilding serializer.
- AI graph regeneration is explicit and independently persisted; it is not an
  automatic MPR save side effect.
- Embedded primary-MOB graph data and external `.grp` fallback remain distinct
  sources. Conflicts are surfaced rather than silently normalized.
- Larger or unusual maps remain inspectable when structurally safe. Runtime
  limits are profile-specific validation findings.

## Runtime safety

- Test runs stage exact validated in-memory documents into owned temporary
  generations and never replace editor sources or live campaign saves.
- Runtime support is granted per fingerprinted executable/layout and guarded
  call site, not by filename or size alone.
- Endurance attaches to or stops only processes it owns. A surviving orphan is
  identified by PID, path, creation time, hash, and run identity—not PID alone.
- Cleanup restores/removes only hash-owned artifacts. Externally changed files
  are retained for manual review.

## Development evidence

- Reproduce the original issue before changing code and trace the pipeline to
  the first incorrect value or decision.
- A fix is complete only when that reproduction passes without proportional
  regressions.
- Keep research notes, rejected experiments, corpus measurements, and detailed
  alpha changelists in `docs/`, validation reports, or Git history.
