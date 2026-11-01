#ifndef CTREEVIEW_H
#define CTREEVIEW_H
#include <QTreeWidget>
#include "ui/flat_tree_widget.h"
#include <QHash>
#include <QSet>
#include <QPointer>
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
    bool operator<(const QTreeWidgetItem& other) const override;
    uint nodeId() const;
    uint storedId() const {return m_nodeId;}
    void setId(uint id) {m_nodeId = id;}
    CNode* node() const;
    void setNode(CNode* node);

private:
    uint m_nodeId;
    QPointer<CNode> m_pNode;
};

///
/// \brief The CTreeView class provides display of objects on the scene
/// \todo display of patrol points (partially implemented)
///
class CTreeView : public QTreeWidget
{
    Q_OBJECT
public:
    enum EItemDataRole
    {
        BehaviorItemRole = Qt::UserRole + 20,
        FilterTextRole,
        RowKeyRole
    };

    explicit CTreeView(QWidget *parent = nullptr);
    void attachView(CView* pView) {m_pView = pView;}
    QString objectNameByType(ENodeType type) const;
    ENodeType objectTypeByString(QString str) const;
    void revealNode(CNode* pNode);
    void markNode(CNode* pNode, bool scrollToNode = false);
    void markNodes(const QList<CNode*>& nodes, CNode* primaryNode,
                   bool scrollToPrimary = false, bool refilter = true);
    void clearViewportSelectionMarker();
    CUnit* focusedLogicUnit() const;
    int focusedLogicSlot() const { return m_focusedLogicSlot; }
    void focusLogicSlot(CUnit* owner, int logicSlot, bool scrollToItem = false);
    void clearLogicSlotFocus();
    void prepareForRebuild();
    void beginRefresh();
    void clear();
    QTreeWidgetItem* acquireRow(const QString& key, QTreeWidgetItem* parent,
                                CNode* node = nullptr, bool plain = false);
    void finishRefresh();
    void setRefreshSuspended(bool suspended);
    void setShowAllMobs(bool all);
    bool showAllMobs() const { return m_showAllMobs; }
    QIcon objectIcon(ENodeType type) const;
    QString modelAlias(const QString& model) const;
    void setModelCatalogPath(const QString& path);
    void selectGroup(QTreeWidgetItem* group, bool foundOnly);
    QTreeWidgetItem* rowForNode(CNode* node) const;
    void setValidationNids(const QSet<uint>& critical, const QSet<uint>& warning);

signals:
    void filterMatchCountChanged(int visible, int total);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

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
    void syncSelectionToView();
    bool m_syncing{false};
    bool m_showAllMobs{false};
    bool m_refreshSignals{false};
    bool m_refreshSuspended{false};
    bool m_suspendedSignals{false}, m_suspendedUpdates{true}, m_suspendedEnabled{true};
    int m_savedScroll{0};
    QHash<QString, QTreeWidgetItem*> m_rows;
    QSet<QString> m_usedRows;
    QHash<QTreeWidgetItem*, int> m_nextPosition;
    QString m_catalogPath;
    bool m_autoCatalogPath{true};
    qint64 m_catalogStamp{-1}, m_catalogSize{-1};
    QHash<QString, QString> m_aliases;
    CView* m_pView;
    QTimer* m_filterTimer;
    QString m_filterText;
    QHash<QTreeWidgetItem*, bool> m_expansionBeforeFilter;
    QSet<uint> m_criticalNids;
    QSet<uint> m_warningNids;
    QPointer<CUnit> m_focusedLogicUnit;
    int m_focusedLogicSlot{-1};
};

#endif // CTREEVIEW_H
