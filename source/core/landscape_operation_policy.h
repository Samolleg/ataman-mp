#ifndef LANDSCAPE_OPERATION_POLICY_H
#define LANDSCAPE_OPERATION_POLICY_H

#include "operation_history_policy.h"
#include "types.h"

inline bool landscapeOperationDisablesEditing(
    EButtonOp type, bool landscapeEditingEnabled,
    bool preserveLandscapeEditing = false)
{
    return !preserveLandscapeEditing
        && type != EButtonOpTilebrush
        && landscapeEditingEnabled;
}

// Palette changes are emitted synchronously while viewport input handlers are
// still on the stack. Replacing an already-active tile brush in that callback
// destroys the handler that is currently processing RMB sampling. Keep the
// current brush alive; actual tool transitions still go through normally.
inline bool landscapeOperationNeedsTransition(
    EButtonOp requested, EOperationMethod current)
{
    return requested != EButtonOpTilebrush
        || current != eOperationMethodTileBrush;
}

#endif
