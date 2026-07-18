# 0.2.0-alpha.9 patrol semantics correction

Alpha.9 supersedes alpha.8's experimental retained-patrol reuse behavior.

Unit NID 1038 in the supplied `zone1test5.mob` uses Radius behavior while its
logic block also retains one `GUARD_PT`. That point is dormant data, not a
hidden/deleted point. Pressing P with the unit selected must retain the
established operation:

1. insert a new first patrol point;
2. change Radius behavior to Path;
3. reveal the pre-existing retained route as well as the new point.

Undo removes only the newly inserted point and restores Radius behavior. The
pre-existing `GUARD_PT` remains byte-preserved. Redo inserts the new point and
returns to Path as one compound user action.

The alpha.8 fixes for selected-marker draw ordering, look-setting inheritance,
and G/Move support for patrol/look/trap points remain in alpha.9.
