# Modern terrain atlas R124 identity publishing test

This build adds explicit terrain-resource identity and safe `textures.res`
publication to the reviewed R123 modern terrain painter.

## Changed

- New content identities use the readable flat form `zone2_<12 hex>`; legacy
  `_eime_` identities are flattened when finalized again.
- Finalize now offers exactly two identity actions: create a new content
  identity, or use/replace an identity supplied by the user.
- MPR Settings shows `Terrain resource identity` and provides the same two
  synchronized finalization actions. MMP page names remain derived and hidden.
- Finalize can either keep the standalone map atlas or merge the finalized MMP
  pages into a derived `textures.res` selected by the user. The selected source
  archive is read-only; the combined archive is written in the new package.
- Existing byte-identical identity pages are reused. Differing pages fail
  closed unless `Use / replace` was chosen. Unrelated RES members are verified
  unchanged.
- `atlas-package.json` and the semantic profile remain editor-side files and
  are never embedded in `textures.res`.
- Identity-only finalization first proves that the current packed map forms an
  exact shared-corner semantic field. It refuses ambiguous legacy topology
  rather than altering terrain meaning.

## Focused manual checks

1. Open MPR Settings > Resource identity. Confirm the current identity is
   visible and individual `NNN.mmp` member names are not exposed.
2. Paint/sculpt, finalize with `Create a new content identity`, and confirm the
   proposed name is `zoneN_<12 hex>` with no `_eime_` chaining.
3. Choose `Build from existing textures.res`, select a disposable/source copy,
   and confirm the finalized folder contains one MPR, `textures.res`, manifest,
   and semantic profile. The selected source archive must remain byte-identical.
4. Repeat using the same identity. Identical pages should reuse cleanly;
   differing pages must be refused until `Use / replace` is selected.
5. Open the finalized MPR manually and verify painting and sculpting remain.

## Known boundary

The editor produces a derived `textures.res`; it does not directly mutate an
installed/shared game archive. Installing or replacing that archive remains an
explicit user/mod-packaging action. Rotation-equivalent physical deduplication
remains disabled.
