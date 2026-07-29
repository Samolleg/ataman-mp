#ifndef LANDSCAPE_OPERATION_POLICY_H
#define LANDSCAPE_OPERATION_POLICY_H

#include "types.h"

inline bool landscapeOperationDisablesEditing(
    EButtonOp type, bool landscapePaintMode,
    bool preserveLandscapeEditing = false)
{
    return !preserveLandscapeEditing
        && type != EButtonOpTilebrush
        && landscapePaintMode;
}

#endif
