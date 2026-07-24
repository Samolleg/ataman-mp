# AI graph diagnostic regeneration manual test

Use disposable copied Base/JMV maps only. Do not edit the installed game or a
proprietary source corpus in place. This build does not serialize AI graphs.

## Changelist

- regenerate all eight preview profiles asynchronously from a snapshot of the
  current canonical terrain and loaded static world-object/lever collision
  proxies;
- retain the authored/loaded graph so **Use loaded graph** can restore it for
  direct comparison;
- color links and the selected profile name from purple through green while
  retaining yellow nodes and red isolated-node warnings;
- enlarge the graph widget;
- change the Landscape Tools shortcut from **B** to **L**, including migration
  of the old default binding;
- stage validated unsaved MPR edits directly into Test Current Map without
  replacing the source MPR;
- keep detailed MPR member/hash/semantic evidence in `workflow.log` instead of
  displaying it in the ordinary Save flow.

## Test cases

1. Load a disposable Zone 1 collection. Press **N**, move the profile slider
   through all eight values, and confirm links plus the profile name use the
   same purple-to-green color. Yellow nodes and red isolated nodes must remain
   distinct and all widget text must fit.
2. Click **Regenerate preview**. The editor must remain responsive. When the
   worker completes, the source label must say `Regenerated diagnostic` and the
   viewport must update. Click **Use loaded graph** and confirm the original
   graph returns.
3. Around fence objects such as map ID 568, compare layers. Layer 0 is a
   finite-clearance levitating profile (2.0 m high), not unrestricted flight;
   a fence-height solid part may therefore break its links. Record any obvious
   disagreement between loaded and regenerated graphs.
4. On the disposable collection, move a fence or another static world object,
   regenerate, and confirm connectivity changes near the edited object. Undo,
   regenerate again, and confirm it returns. Dynamic units must not become
   blockers.
5. Sculpt a blocking ridge, regenerate, and compare the human-sized layer.
   Undo the ridge and regenerate again. The generated preview must never dirty
   or write a graph/MOB/MPR by itself.
6. Press **L** to open and close Landscape Tools. **B** must no longer toggle
   the tool unless the user explicitly assigns it in Settings.
7. Make an MPR terrain edit but do not save it. Run **Test Current Map**. The
   validated staged run should include the edit while the source MPR hash and
   timestamp remain unchanged.
8. Explicitly Save a disposable changed MPR. It should use the normal progress
   flow without a technical SEC/member confirmation. Verify the backup,
   changed members, candidate hash, and semantic diff remain recorded in
   `workflow.log`.

## Boundary

Regeneration is a diagnostic approximation for editor preview. It does not
write `.grp` or embedded graph bytes and is not yet a source-compatible graph
serializer. Exact game-side movement remains the final validation.
