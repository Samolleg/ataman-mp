#ifndef BEHAVIOR_COLLECTION_WIDGET_H
#define BEHAVIOR_COLLECTION_WIDGET_H

#include <QWidget>

class CNode;
class CUnit;
class QTreeWidget;

class CBehaviorCollectionWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit CBehaviorCollectionWidget(CUnit* unit, int logicSlot,
                                       QWidget* parent = nullptr);

signals:
    void selectPoint(CNode* point);
    void movePatrolPoint(int from, int to);
    void moveLookPoint(int patrol, int from, int to);
    void addPatrolPoint();
    void addLookPoint(int patrol);

private:
    QWidget* actionWidget(CNode* point, int patrol, int index,
                          int previous, int next, bool lookPoint);
    QTreeWidget* m_tree{nullptr};
};

#endif // BEHAVIOR_COLLECTION_WIDGET_H
