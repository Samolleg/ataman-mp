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

#endif // OPERATION_HISTORY_POLICY_H

