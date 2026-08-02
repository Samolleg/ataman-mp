#ifndef TRAP_COLLECTION_WIDGET_H
#define TRAP_COLLECTION_WIDGET_H

#include <QWidget>

class CMagicTrap;
class CNode;
class QTreeWidget;

class CTrapCollectionWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit CTrapCollectionWidget(CMagicTrap* trap, QWidget* parent = nullptr);

signals:
    void selectPoint(CNode* point);
    void moveActivationArea(int from, int to);
    void moveCastTarget(int from, int to);
    void addActivationArea();
    void addCastTarget();

private:
    QWidget* actionWidget(CNode* point, int index, int previous, int next,
                          bool castTarget);
    QTreeWidget* m_tree{nullptr};
};

#endif // TRAP_COLLECTION_WIDGET_H
