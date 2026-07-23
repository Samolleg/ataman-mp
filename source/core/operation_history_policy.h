#ifndef OPERATION_HISTORY_POLICY_H
#define OPERATION_HISTORY_POLICY_H

enum EOperationMethod
{
    eOperationMethodSelect = 0,
    eOperationMethodMoveAxis,
    eOperationMethodRotateAxis,
    eOperationMethodScaleAxis,
    eOperationMethodTileBrush
};

inline bool operationAllowsHistoryNavigation(EOperationMethod method)
{
    return method == eOperationMethodSelect || method == eOperationMethodTileBrush;
}

inline bool operationRequiresCancellationForDocumentTransition(EOperationMethod method)
{
    // Select has no transient transform, while the tile brush is a persistent
    // landscape tool whose current stroke can be settled without changing the
    // selected operation.
    return method != eOperationMethodSelect && method != eOperationMethodTileBrush;
}

#endif // OPERATION_HISTORY_POLICY_H
