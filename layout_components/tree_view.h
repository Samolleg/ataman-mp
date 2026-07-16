#ifndef CTREEVIEW_H
#define CTREEVIEW_H
#include <QTreeWidget>
#include "types.h"

class CView;
class CNode;

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
class CTreeView : public QTreeWidget
{
    Q_OBJECT
public:
    explicit CTreeView(QWidget *parent = nullptr);
    void attachView(CView* pView) {m_pView = pView;}
    QString objectNameByType(ENodeType type);
    ENodeType objectTypeByString(QString str);
    void revealNode(CNode* pNode);
    void clearViewportSelectionMarker();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    int parentDeepCount(QTreeWidgetItem* pItem);
    void recalcParent(QTreeWidgetItem* pItem);
    QTreeWidgetItem* category(QString& categoryName);

public slots:
    void itemClickedOver(QTreeWidgetItem* pItem, int column);
    void itemDoubleClickedOver(QTreeWidgetItem* pItem, int column);
    void onNodeDelete(uint nodeId);
    void addNodeToTree(CNode* pNode);
private slots:
    void onChangeNodeId(uint oldId, uint newId);
    void onChangeObjectName(CNode* pNode);

private:
    CView* m_pView;
};

#endif // CTREEVIEW_H
