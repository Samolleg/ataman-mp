#ifndef CTREEVIEW_H
#define CTREEVIEW_H
#include <QTreeWidget>
#include "ui/flat_tree_widget.h"
#include <QHash>
#include <QSet>
#include "types.h"

class CView;
class CNode;
class CUnit;
class QTimer;
class QContextMenuEvent;

///
/// \brief The CTreeObject class stores additional information about the object ID so that it can be selected in the 3D scene.
///
class CTreeObject : public QTreeWidgetItem//, public QObject
{
    //Q_OBJECT
public:
    CTreeObject(QTreeWidget* pParent);
    CTreeObject(QTreeWidgetItem* pParent, CNode* pNode);
    CTreeObject(CTreeObject* pParent, CNode* pNode);
    uint nodeId() const;
    uint storedId() const {return m_nodeId;}
    void setId(uint id) {m_nodeId = id;}
    CNode* node() const {return m_pNode;}
    void setNode(CNode* node);

private:
    uint m_nodeId;
    CNode* m_pNode;
};

///
/// \brief The CTreeView class provides display of objects on the scene
/// \todo display of patrol points (partially implemented)
///
class CTreeView : public CFlatTreeWidget
{
    Q_OBJECT
public:
    enum EItemDataRole
    {
        BehaviorItemRole = Qt::UserRole + 20,
        FilterTextRole
    };

    explicit CTreeView(QWidget *parent = nullptr);
    void attachView(CView* pView) {m_pView = pView;}
    QString objectNameByType(ENodeType type) const;
    ENodeType objectTypeByString(QString str) const;
    void revealNode(CNode* pNode);
    void markNode(CNode* pNode, bool scrollToNode = false);
    void markNodes(const QList<CNode*>& nodes, CNode* primaryNode,
                   bool scrollToPrimary = false);
    void clearViewportSelectionMarker();
    CUnit* focusedLogicUnit() const { return m_focusedLogicUnit; }
    int focusedLogicSlot() const { return m_focusedLogicSlot; }
    void focusLogicSlot(CUnit* owner, int logicSlot, bool scrollToItem = false);
    void clearLogicSlotFocus();
    void prepareForRebuild();
    void setValidationNids(const QSet<uint>& critical, const QSet<uint>& warning);

signals:
    void filterMatchCountChanged(int visible, int total);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    enum class ETextMatchMode
    {
        Contains,
        Exact,
        Fuzzy
    };

    int parentDeepCount(QTreeWidgetItem* pItem);
    void recalcParent(QTreeWidgetItem* pItem);
    QTreeWidgetItem* category(QString& categoryName);
    bool filterItem(QTreeWidgetItem* item, const QStringList& terms,
                    bool ancestorMatched, int* visible, int* total);
    bool nodeMatches(CNode* node, const QStringList& terms) const;
    void expandBehaviorHierarchy(QTreeWidgetItem* ownerItem);
    bool textMatches(const QString& candidate, const QString& query,
                     ETextMatchMode mode = ETextMatchMode::Contains) const;
    QString validationState(CNode* node) const;

public slots:
    void setFilterText(const QString& text);
    void applyFilter();
    void itemClickedOver(QTreeWidgetItem* pItem, int column);
    void itemDoubleClickedOver(QTreeWidgetItem* pItem, int column);
    void onNodeDelete(uint nodeId);
    void addNodeToTree(CNode* pNode);
private slots:
    void onChangeNodeId(uint oldId, uint newId);
    void onChangeObjectName(CNode* pNode);

private:
    CView* m_pView;
    QTimer* m_filterTimer;
    QString m_filterText;
    QHash<QTreeWidgetItem*, bool> m_expansionBeforeFilter;
    QSet<uint> m_criticalNids;
    QSet<uint> m_warningNids;
    CUnit* m_focusedLogicUnit{nullptr};
    int m_focusedLogicSlot{-1};
};

#endif // CTREEVIEW_H
