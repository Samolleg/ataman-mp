# Alpha 26 modern-atlas R137 RES selection and Test Current fix

This revision prevents finalized modern maps from silently publishing terrain
pages to an unexpected sidecar and fixes Test Current staging for maps whose
validated atlas archive does not share the MPR filename.

## Changes

- **Save MPR** and **Save MPR As...** now always show the exact RES archive
  chooser before publishing modern terrain pages. A currently bound archive is
  the initial selection only when it is already inside a configured resource
  root; a temporary Maps-folder sidecar redirects the chooser to the profile's
  mod `Res\\textures.res`. Nothing is updated silently.
- Publishing a persisted editable draft no longer treats its temporary
  `<draft>-atlas.res` sidecar as the implicit destination. The chooser starts
  at the configured mod resource folder with `textures.res` selected.
- Test Current stages the exact validated archive bound to the open MPR instead
  of guessing only `<mpr>-atlas.res` or a sibling `textures.res`.
- Runtime staging extracts only the contiguous atlas pages declared by the
  MPR's identity and page count. Unrelated MMP identities and stale higher
  pages in a general-purpose RES are ignored rather than causing an empty or
  rejected runtime atlas.
- No installed MPR, RES, semantic profile, or game file is modified by this
  build.

## Focused manual test

1. Open a known-good finalized modern map and make one modern terrain paint.
2. Choose **Save MPR**. Confirm the RES chooser appears, shows the currently
   bound archive as its initial selection, and does not update anything until
   you explicitly approve that file.
3. Use **Save editable modern draft as...**, reopen the draft, then choose
   **Save MPR As...**. Confirm the RES chooser opens in the configured JMV mod
   resource folder rather than selecting the draft sidecar in `Maps`.
4. Reopen the published MPR and use **Test Current**. The staged map must load
   with the exact published terrain pages; it must not start with an untextured
   atlas or crash because zero pages were staged.
5. Start the game normally and load the map after publishing its pages to a RES
   configured for that runtime profile.

## Known limitations

- A RES deliberately chosen outside the game's configured resource roots is
  still not discoverable by a normal standalone game launch. The editor warns
  about that location, but an explicit user choice is respected.
- Rotation-equivalent physical deduplication remains disabled by decision.
