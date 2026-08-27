#ifndef AI_GRAPH_PREVIEW_INVALIDATION_H
#define AI_GRAPH_PREVIEW_INVALIDATION_H

#include "core/types.h"

class QUndoCommand;

class IAiGraphPreviewInvalidatingCommand
{
public:
    virtual ~IAiGraphPreviewInvalidatingCommand() = default;
    virtual bool invalidatesAiGraphPreview() const = 0;
};

struct SAiGraphPreviewSourceTransition
{
    bool enableAutomaticRegeneration{false};
    bool cancelPendingRegeneration{false};
    bool queueRegeneration{false};
};

SAiGraphPreviewSourceTransition aiGraphPreviewSourceTransition(
    bool selectLoadedSource, bool regeneratedGraphFresh);
bool aiGraphObjectPropertyInvalidatesPreview(ENodeType nodeType,
                                             EObjParam property);
bool aiGraphUndoCommandInvalidatesPreview(const QUndoCommand* command);

#endif // AI_GRAPH_PREVIEW_INVALIDATION_H
