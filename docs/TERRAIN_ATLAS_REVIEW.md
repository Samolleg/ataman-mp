# Semantic terrain atlas review

Endurance reads atlas images exactly as the game does. The review tool does not
edit or generate texture atlases. It creates a namespace- and atlas-hash-bound
description that tells automatic painting which existing slots represent each
land or overlay material and which corner transitions they contain.

## Review workflow

1. Open **Landscape Tools**. If the map has neither an authored template nor an
   approved profile, accept the prompt to generate a review proposal.
2. The proposal first finds uniform material tiles, then assigns transition
   corners to those anchored materials. Transition tiles cannot merge two
   material families merely because their colours form a gradual chain. By
   default the dialog shows only genuinely unresolved families and slots; use
   **Show all inferred families and slots** for expert inspection.
3. Review each highlighted material family:
   - rename it to a recognizable material such as `Dark grass`, `Limestone`,
     `Water`, or `Swamp`;
   - choose **Land** or **Overlay**;
   - use **Underwater** only for land textures intended to sit below an overlay;
   - clear the family **Needs review** flag only after these choices are correct.
4. Select a family and inspect every listed atlas slot. The preview is the
   actual decoded atlas image. `NW / NE / SE / SW` describes the material at
   each corner after the stored tile rotation is applied.
5. When a slot preview and its four corner-family assignments are correct,
   select it and click **Mark selected Reviewed**.
6. Use **Split selected slots** only when those pictures belong to a different
   visual material from the rest of the family. Use **Merge selected families**
   only when two proposed families are the same material.
7. An unused slot is inferred automatically when it matches observed material
   anchors unambiguously. Only an unused material with no observed layer anchor,
   or a corner with multiple equally plausible anchors, requires confirmation.
8. **Save draft** preserves incomplete work without enabling automatic paint.
   **Approve and save** is available after every family and slot ambiguity has
   been resolved.

Missing transitions do not prevent approval. They mean the atlas has no exact
blend for that material combination; automatic painting keeps guaranteed brush
coverage and uses a hard edge instead of inventing atlas content.

Profiles are stored outside the MPR and are accepted only for the same MPR
namespace, decoded atlas pixel hash, and slot count. Changing the supplied atlas
therefore requires a new review rather than silently reusing stale assignments.
