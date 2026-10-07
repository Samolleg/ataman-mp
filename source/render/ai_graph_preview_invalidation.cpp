#include "ai_graph_preview_invalidation.h"

#include "core/map_object_placement_policy.h"

#include <QUndoCommand>

bool aiGraphObjectPropertyInvalidatesPreview(ENodeType nodeType,
                                             EObjParam property)
{
    if(!ei::contributesStaticNavigationCollision(nodeType))
        return false;
    switch(property)
    {
    case eObjParam_NID:
    case eObjParam_TEMPLATE:
    case eObjParam_BODYPARTS:
    case eObjParam_COMPLECTION_X:
    case eObjParam_COMPLECTION_Y:
    case eObjParam_COMPLECTION_Z:
    case eObjParam_COMPLECTION:
    case eObjParam_POSITION_X:
    case eObjParam_POSITION_Y:
    case eObjParam_POSITION_Z:
    case eObjParam_POSITION:
    case eObjParam_ROTATION_X:
    case eObjParam_ROTATION_Y:
    case eObjParam_ROTATION_Z:
    case eObjParam_ROTATION:
        return true;
    default:
        return false;
    }
}

bool aiGraphUndoCommandInvalidatesPreview(const QUndoCommand* command)
{
    if(!command)
        return false;
    const IAiGraphPreviewInvalidatingCommand* invalidating =
        dynamic_cast<const IAiGraphPreviewInvalidatingCommand*>(command);
    if(invalidating && invalidating->invalidatesAiGraphPreview())
        return true;
    for(int childIndex = 0; childIndex < command->childCount(); ++childIndex)
        if(aiGraphUndoCommandInvalidatesPreview(command->child(childIndex)))
            return true;
    return false;
}
