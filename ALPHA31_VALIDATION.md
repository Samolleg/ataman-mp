# Alpha.31 final hardening test matrix

Use copied MOB/MPR files only. This is the closed-group release candidate for
the completed MOB hardening pass, not approval to edit the only live map copy.

## Save coverage

Run each case in a clean session and save to a new MOB:

1. Move an existing unit and change several ordinary properties together.
2. Copy/paste one object in the same MOB, finish placement, and save.
3. Copy/paste an object, change its model/texture/database entry, and save.
4. Delete one object, save, undo, and confirm the restored Save As is identical.
5. Add and remove patrol points and look points; exercise undo/redo before save.
6. Create an object after selecting an existing same-type template. Creation
   without such a template must explain the requirement before changing the MOB.
7. Edit an embedded Cyrillic script. Unsupported non-CP1251 characters must be
   rejected before the edit is applied.
8. Undo every change and confirm the dirty marker clears.

Same-MOB copy/paste is covered. Cross-MOB copying remains part of the future
multi-MOB workflow and is not authorized by this release.

## Preservation checks

For each output:

- reopen it in a fresh Endurance session;
- compare it with the source using **Tools > Compare MOB Files** and MobExplorer;
- confirm only intended objects/fields were added, removed, or changed;
- confirm copied objects retain complete body parts, scripts, inventory, logic,
  and other source fields;
- retain the clean workflow log and source/output SHA-256 values.

Expected logs contain `RAW_LEAF_EDITS`, `RAW_OBJECT_SEQUENCE`, or
`RAW_OBJECT_SEQUENCE+RAW_LEAF_EDITS`, followed by staged validation and atomic
save success. No save preview appears.

## JMV game checks

Test representative outputs through the normal Starter/JMV path:

- load with `drawall`;
- verify copied/deleted units and patrol behavior;
- exercise changed scripts or chests where applicable;
- unload and reload the map;
- traverse normally and exit gracefully.

MPR editing and direct in-game launch are not part of this alpha. Unchanged MPR
Save As remains byte-identical.
