#include "tree_view.h"
#include <QKeyEvent>
#include <QDebug>
#include <QApplication>
#include <QKeySequence>
#include "view.h"
#include "node.h"
#include <QHeaderView>
#include <QTimer>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QSignalBlocker>
#include <QContextMenuEvent>
#include <QMenu>
#include <algorithm>
#include <QCollator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QMouseEvent>
#include <QStyle>
#include <QPainter>
#include <QScopedValueRollback>
#include "technical_model_catalog.h"
#include "objects/unit.h"
#include "mob/mob.h"
#include "log.h"
#include "scene.h"

CTreeView::CTreeView(QWidget *parent) : QTreeWidget(parent), m_pView(nullptr),
    m_filterTimer(new QTimer(this))
{
    setIndentation(18);
    setRootIsDecorated(true);
    setUniformRowHeights(true);
    setWordWrap(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_catalogPath = QCoreApplication::applicationDirPath() + "/model-catalog.json";
    connect(this, &QTreeWidget::itemSelectionChanged, this, &CTreeView::syncSelectionToView);
    m_filterTimer->setSingleShot(true);
    m_filterTimer->setInterval(150);
    connect(m_filterTimer, &QTimer::timeout, this, &CTreeView::applyFilter);
    connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if(item->data(0, Qt::UserRole + 10).isValid() && !dynamic_cast<CTreeObject*>(item))
            itemClickedOver(item, column);
    });
    QObject::connect(this, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(itemDoubleClickedOver(QTreeWidgetItem*,int)));
}

bool CTreeObject::operator<(const QTreeWidgetItem& other) const
{
    QCollator order(QLocale(QLocale::English));
    order.setNumericMode(true); order.setCaseSensitivity(Qt::CaseInsensitive);
    return order.compare(text(0), other.text(0)) < 0;
}

void CTreeView::prepareForRebuild()
{
    m_filterTimer->stop(); m_expansionBeforeFilter.clear();
}
void CTreeView::clear()
{
    const QSignalBlocker guard(this);
    prepareForRebuild(); m_rows.clear(); m_usedRows.clear(); m_nextPosition.clear();
    QTreeWidget::clear();
}
void CTreeView::beginRefresh()
{
    m_filterTimer->stop();
    m_savedScroll = verticalScrollBar()->value();
    m_refreshSignals = blockSignals(true);
    setUpdatesEnabled(false);
    m_usedRows.clear(); m_nextPosition.clear();
    const QFileInfo info(m_catalogPath);
    if(info.lastModified().toMSecsSinceEpoch() != m_catalogStamp || info.size() != m_catalogSize) {
        m_catalogStamp = info.lastModified().toMSecsSinceEpoch(); m_catalogSize = info.size();
        m_aliases.clear(); QFile file(m_catalogPath);
        if(file.open(QIODevice::ReadOnly)) {
            const auto models = QJsonDocument::fromJson(file.readAll()).object().value("models").toObject();
            for(auto it = models.begin(); it != models.end(); ++it)
                m_aliases.insert(ei::TechnicalModelCatalog::modelKey(it.key()), it.value().toObject().value("russianName").toString());
        }
    }
}

QIcon CTreeView::objectIcon(ENodeType type) const
{
    // Distinct type pictograms; no dependency on a model or game texture.
    static QHash<int, QIcon> icons;
    if(icons.contains(int(type))) return icons.value(int(type));
    QPixmap image(32,32); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(120,200,225),2)); p.setBrush(QColor(65,115,130));
    switch(type) {
    case eLight: p.setBrush(QColor(255,205,65)); p.drawEllipse(QRectF(8,8,16,16)); break;
    case eSound: p.drawRect(QRectF(5,12,7,9)); p.drawArc(QRectF(10,5,17,23),-70*16,140*16); break;
    case eUnit: p.drawEllipse(QRectF(12,3,8,8)); p.drawRoundedRect(QRectF(8,13,16,16),5,5); break;
    case eParticle: p.drawLine(5,5,27,27); p.drawLine(5,27,27,5); p.drawEllipse(QRectF(11,11,10,10)); break;
    case eLever: p.drawRect(QRectF(6,24,20,4)); p.drawLine(16,25,23,7); p.drawEllipse(QRectF(20,3,7,7)); break;
    case eMagicTrap: p.drawEllipse(QRectF(4,4,24,24)); p.drawLine(7,25,25,7); break;
    case eTorch: p.setBrush(QColor(255,155,60)); p.drawEllipse(QRectF(10,3,12,14)); p.drawLine(16,17,16,29); break;
    default: p.drawRect(QRectF(6,8,20,20)); p.drawLine(6,8,16,3); p.drawLine(16,3,26,8); break;
    }
    const QIcon icon(image); icons.insert(int(type), icon); return icon;
}

QString CTreeView::modelAlias(const QString& model) const
{ return m_aliases.value(ei::TechnicalModelCatalog::modelKey(model)); }
void CTreeView::setModelCatalogPath(const QString& path)
{ m_catalogPath = path; m_catalogStamp = m_catalogSize = -1; }
void CTreeView::setShowAllMobs(bool all)
{
    if(m_showAllMobs == all) return;
    m_showAllMobs = all;
    if(m_pView) m_pView->collectObjectTreeData();
}
QTreeWidgetItem* CTreeView::acquireRow(const QString& key, QTreeWidgetItem* parent, CNode* node, bool plain)
{
    auto* item = m_rows.value(key, nullptr);
    if(!item) {
        item = plain ? new QTreeWidgetItem : new CTreeObject(static_cast<QTreeWidgetItem*>(nullptr), node);
        if(parent) parent->addChild(item); else addTopLevelItem(item);
        m_rows.insert(key, item); item->setData(0, RowKeyRole, key);
        item->setExpanded(!parent);
    } else if(item->parent() != parent) {
        if(item->parent()) item->parent()->takeChild(item->parent()->indexOfChild(item));
        else takeTopLevelItem(indexOfTopLevelItem(item));
        if(parent) parent->addChild(item); else addTopLevelItem(item);
    }
    if(auto* object = dynamic_cast<CTreeObject*>(item)) object->setNode(node);
    item->setFlags(Qt::ItemIsEnabled | ((node || plain) ? Qt::ItemIsSelectable : Qt::NoItemFlags));
    // An empty QBrush is still a valid foreground role and paints black.
    // Clear the role so normal, selected and disabled rows inherit the palette.
    for(int column = 0; column < 3; ++column)
        item->setData(column, Qt::ForegroundRole, QVariant());
    const int desired = m_nextPosition[parent]++;
    const int actual = parent ? parent->indexOfChild(item) : indexOfTopLevelItem(item);
    if(actual != desired) {
        const bool expanded = item->isExpanded();
        if(parent) { parent->takeChild(actual); parent->insertChild(desired, item); }
        else { takeTopLevelItem(actual); insertTopLevelItem(desired, item); }
        item->setExpanded(expanded);
    }
    m_usedRows.insert(key); return item;
}
void CTreeView::finishRefresh()
{
    // Remove obsolete subtrees once; retained rows keep their identity and expansion.
    QList<QTreeWidgetItem*> obsolete;
    QTreeWidgetItemIterator it(this);
    while(*it) { auto* item = *it; ++it;
        if(!m_usedRows.contains(item->data(0, RowKeyRole).toString())) obsolete.append(item);
    }
    QSet<QTreeWidgetItem*> stale = QSet<QTreeWidgetItem*>::fromList(obsolete);
    for(auto* item : obsolete) {
        m_rows.remove(item->data(0, RowKeyRole).toString()); m_expansionBeforeFilter.remove(item);
    }
    QList<QTreeWidgetItem*> roots;
    for(auto* item : obsolete) if(!stale.contains(item->parent())) roots.append(item);
    qDeleteAll(roots);
    blockSignals(m_refreshSignals); setUpdatesEnabled(true);
    applyFilter();
    if(m_pView) {
        const auto selected = m_pView->selectionSnapshot();
        markNodes(selected, selected.isEmpty() ? nullptr : selected.back());
    }
    verticalScrollBar()->setValue(m_savedScroll);
}
QTreeWidgetItem* CTreeView::rowForNode(CNode* node) const
{
    QTreeWidgetItemIterator it(const_cast<CTreeView*>(this));
    while(*it) { auto* row=dynamic_cast<CTreeObject*>(*it); if(row && row->node()==node) return row; ++it; }
    return nullptr;
}
void CTreeView::mousePressEvent(QMouseEvent* event)
{
    auto* item=itemAt(event->pos()); auto* row=dynamic_cast<CTreeObject*>(item);
    if(event->button()==Qt::LeftButton && row && !row->node()) {
        item->setExpanded(!item->isExpanded()); event->accept(); return;
    }
    QTreeWidget::mousePressEvent(event);
}
void CTreeView::syncSelectionToView()
{
    if(m_syncing || !m_pView) return;
    QScopedValueRollback<bool> guard(m_syncing, true);
    if(currentItem() && currentItem()->data(0, Qt::UserRole + 10).isValid()
       && !dynamic_cast<CTreeObject*>(currentItem())) {
        itemClickedOver(currentItem(), 0); return;
    }
    const QString currentKey = currentItem() ? currentItem()->data(0, RowKeyRole).toString() : QString();
    QList<CNode*> nodes;
    for(auto* item : selectedItems()) if(auto* row=dynamic_cast<CTreeObject*>(item))
        if(row->node()) nodes.append(row->node());
    if(nodes.isEmpty()) {
        if(currentItem() && !dynamic_cast<CTreeObject*>(currentItem())) return;
        m_pView->clearObjectSelection();
    } else {
        int slot = -1;
        for(auto* item = currentItem(); item; item = item->parent())
            if(item->data(0, Qt::UserRole + 10).isValid()) {
                slot = item->data(0, Qt::UserRole + 10).toInt(); break;
            }
        if(slot >= 0) m_pView->setLogicSlotIndex(slot);
        clearLogicSlotFocus(); m_pView->selectNodes(nodes, false);
    }
    const QSignalBlocker block(this);
    if(auto* current = m_rows.value(currentKey)) setCurrentItem(current, 0, QItemSelectionModel::NoUpdate);
}
void CTreeView::selectGroup(QTreeWidgetItem* group, bool foundOnly)
{
    if(!group || !m_pView) return;
    QList<CNode*> nodes; QList<QTreeWidgetItem*> pending{group};
    while(!pending.isEmpty()) {
        auto* item=pending.takeLast();
        if(foundOnly && item->isHidden()) continue;
        if(auto* row=dynamic_cast<CTreeObject*>(item)) if(row->node()) {
            if(item->flags().testFlag(Qt::ItemIsSelectable)) nodes.append(row->node());
            continue; // Behavior points are not instances of the object group.
        }
        for(int i=0;i<item->childCount();++i) pending.append(item->child(i));
    }
    if(!nodes.isEmpty()) m_pView->selectNodes(nodes, false);
}

void CTreeView::setValidationNids(const QSet<uint>& critical,
                                  const QSet<uint>& warning)
{
    m_criticalNids = critical;
    m_warningNids = warning;
    if(!m_filterText.trimmed().isEmpty())
        applyFilter();
}

void CTreeView::setFilterText(const QString& text)
{
    const bool wasEmpty = m_filterText.trimmed().isEmpty();
    m_filterText = text;
    if(wasEmpty && !m_filterText.trimmed().isEmpty())
    {
        m_expansionBeforeFilter.clear();
        QTreeWidgetItemIterator it(this);
        while(*it)
        {
            m_expansionBeforeFilter.insert(*it, (*it)->isExpanded());
            ++it;
        }
    }
    m_filterTimer->start();
}

bool CTreeView::textMatches(const QString& candidate, const QString& query,
                            ETextMatchMode mode) const
{
    const QString haystack = candidate.toCaseFolded();
    const QString needle = query.toCaseFolded();
    if(needle.isEmpty())
        return true;
    if(mode == ETextMatchMode::Exact)
        return haystack == needle;
    if(mode == ETextMatchMode::Contains)
        return haystack.contains(needle);
    int at = 0;
    for(const QChar ch : haystack)
        if(at < needle.size() && ch == needle.at(at))
            ++at;
    return at == needle.size();
}

QString CTreeView::validationState(CNode* node) const
{
    if(!node)
        return QString();
    if(m_criticalNids.contains(node->mapId()))
        return "critical";
    if(m_warningNids.contains(node->mapId()))
        return "warning";
    return "clean";
}

bool CTreeView::nodeMatches(CNode* node, const QStringList& terms) const
{
    if(!node)
        return false;
    CUnit* unit = dynamic_cast<CUnit*>(node);
    const QString type = objectNameByType(node->nodeType()) + " "
        + QString::number(int(node->nodeType()));
    const QString database = unit ? unit->databaseName() : QString();
    CMob* owner = m_pView ? m_pView->ownerMob(node) : nullptr;
    const QString mob = owner ? owner->mobName() : QString();
    const QMap<QString, QString> fields{
        {"name", node->mapName()}, {"alias", modelAlias(node->modelName())}, {"id", QString::number(node->mapId())},
        {"nid", QString::number(node->mapId())}, {"type", type},
        {"model", node->modelName()}, {"texture", node->textureName()},
        {"db", database}, {"database", database}, {"prototype", database},
        {"mob", mob}, {"validation", validationState(node)}
    };
    const QString everything = QStringList(fields.values()).join(' ');
    for(const QString& rawTerm : terms)
    {
        const int colon = rawTerm.indexOf(':');
        const int equals = rawTerm.indexOf('=');
        const int separator = colon > 0 && equals > 0 ? qMin(colon, equals)
            : qMax(colon, equals);
        if(separator > 0)
        {
            const QString key = rawTerm.left(separator).toCaseFolded();
            QString value = rawTerm.mid(separator + 1);
            if(fields.contains(key))
            {
                ETextMatchMode mode = rawTerm.at(separator) == '='
                    ? ETextMatchMode::Exact : ETextMatchMode::Contains;
                if(mode != ETextMatchMode::Exact && value.startsWith('~'))
                {
                    value.remove(0, 1);
                    mode = ETextMatchMode::Fuzzy;
                }
                if(!textMatches(fields.value(key), value, mode))
                    return false;
                continue;
            }
        }
        QString value = rawTerm;
        ETextMatchMode mode = ETextMatchMode::Contains;
        if(value.startsWith('~'))
        {
            value.remove(0, 1);
            mode = ETextMatchMode::Fuzzy;
        }
        if(!textMatches(everything, value, mode))
            return false;
    }
    return true;
}

bool CTreeView::filterItem(QTreeWidgetItem* item, const QStringList& terms,
                           bool ancestorMatched, int* visible, int* total)
{
    const bool behaviorItem = item->data(0, BehaviorItemRole).toBool();
    CTreeObject* objectItem = dynamic_cast<CTreeObject*>(item);
    CNode* node = objectItem ? objectItem->node() : nullptr;
    if(node && total)
        ++(*total);
    const QString cachedText = item->data(0, FilterTextRole).toString();
    const bool cachedTextMatch = behaviorItem && !terms.isEmpty()
        && std::all_of(terms.begin(), terms.end(), [this, &cachedText](const QString& term)
           {
               QString value = term;
               ETextMatchMode mode = ETextMatchMode::Contains;
               if(value.startsWith('~'))
               {
                   value.remove(0, 1);
                   mode = ETextMatchMode::Fuzzy;
               }
               return !term.contains(':') && !term.contains('=')
                   && textMatches(cachedText, value, mode);
           });
    const bool textualGroupMatch = !node && !terms.isEmpty()
        && std::all_of(terms.begin(), terms.end(), [this, item](const QString& term)
           {
               QString value = term;
               ETextMatchMode mode = ETextMatchMode::Contains;
               if(value.startsWith('~'))
               {
                   value.remove(0, 1);
                   mode = ETextMatchMode::Fuzzy;
               }
               return !term.contains(':') && !term.contains('=')
                   && textMatches(item->text(0), value, mode);
           });
    // If a parent category already matched (for example "unit" matching the
    // Units group), every descendant is visible. Avoid rebuilding expensive
    // object metadata for thousands of children that cannot change the result.
    const bool selfMatches = terms.isEmpty() || ancestorMatched
        // Behavior rows can outlive a logic projection refresh until the next
        // tree rebuild. Their searchable text is cached when the row is built,
        // so filtering never dereferences a possibly stale logic-node pointer.
        || cachedTextMatch
        || (node && !behaviorItem && nodeMatches(node, terms))
        || textualGroupMatch;
    bool childMatches = false;
    for(int index = 0; index < item->childCount(); ++index)
        childMatches = filterItem(item->child(index), terms,
                                  ancestorMatched || selfMatches,
                                  visible, total) || childMatches;
    const bool shown = ancestorMatched || selfMatches || childMatches;
    item->setHidden(!shown);
    if(node && shown && visible)
        ++(*visible);
    if(!terms.isEmpty() && shown && item->childCount() > 0)
        item->setExpanded(true);
    return shown;
}

void CTreeView::applyFilter()
{
    QElapsedTimer elapsed;
    elapsed.start();
    const bool updatesWereEnabled = updatesEnabled();
    setUpdatesEnabled(false);
    QSignalBlocker signalBlocker(this);
    QString normalized = m_filterText.trimmed();
    normalized.replace(QRegularExpression("\\s*([:=])\\s*"), "\\1");
    QStringList terms;
    const QRegularExpression tokenExpression("\\\"([^\\\"]+)\\\"|(\\S+)");
    QRegularExpressionMatchIterator matches = tokenExpression.globalMatch(normalized);
    while(matches.hasNext())
    {
        const QRegularExpressionMatch match = matches.next();
        terms.append(match.captured(1).isEmpty() ? match.captured(2) : match.captured(1));
    }
    int visible = 0;
    int total = 0;
    for(int index = 0; index < topLevelItemCount(); ++index)
        filterItem(topLevelItem(index), terms, false, &visible, &total);
    if(terms.isEmpty() && !m_expansionBeforeFilter.isEmpty())
    {
        for(auto it = m_expansionBeforeFilter.constBegin();
            it != m_expansionBeforeFilter.constEnd(); ++it)
            if(it.key()) it.key()->setExpanded(it.value());
        m_expansionBeforeFilter.clear();
    }
    if(updatesWereEnabled)
        setUpdatesEnabled(true);
    viewport()->update();
    signalBlocker.unblock();
    emit filterMatchCountChanged(visible, total);
    const qint64 elapsedMs = elapsed.elapsed();
    if(elapsedMs >= 100)
        ei::log(eLogInfo, QString("Outliner filter applied elapsedMs:%1 visible:%2 total:%3 query:%4")
                .arg(elapsedMs).arg(visible).arg(total).arg(m_filterText));
}

void CTreeView::keyPressEvent(QKeyEvent *pEvent)
{
    if(pEvent->matches(QKeySequence::Copy))
    {
        if(focusedLogicUnit() && focusedLogicSlot() >= 0)
            m_pView->copyLogicSlot(focusedLogicUnit(), focusedLogicSlot());
        else
            m_pView->selectedObjectToClipboardBuffer();
        pEvent->accept();
        return;
    }
    if(pEvent->matches(QKeySequence::Paste))
    {
        m_pView->clipboradObjectsToScene();
        pEvent->accept();
        return;
    }
    auto key = pEvent->nativeVirtualKey();
    if(key == eKey_Delete || pEvent->key() == Qt::Key_Delete)
    {
        auto pItem = currentItem();
        if(nullptr == pItem)
            return;

        int parentCount = parentDeepCount(pItem);
        if(parentCount > 0)
        {
            m_pView->deleteSelectedNodes();
        }
        clearSelection();
        pEvent->accept();
        return;
    }
    QTreeWidget::keyPressEvent(pEvent);
}

void CTreeView::contextMenuEvent(QContextMenuEvent* event)
{
    QTreeWidgetItem* item = itemAt(event->pos());
    if(auto* group = dynamic_cast<CTreeObject*>(item)) if(!group->node()) {
        QMenu menu(this);
        auto* found = menu.addAction(tr("Выделить найденные объекты группы"));
        auto* all = menu.addAction(tr("Выделить всю группу"));
        auto* chosen = menu.exec(event->globalPos());
        if(chosen == found || chosen == all) selectGroup(item, chosen == found);
        return;
    }
    if(!item || !item->data(0, Qt::UserRole + 10).isValid()
       || dynamic_cast<CTreeObject*>(item))
    {
        QTreeWidget::contextMenuEvent(event);
        return;
    }
    CUnit* owner = reinterpret_cast<CUnit*>(
        item->data(0, Qt::UserRole + 11).toULongLong());
    const int logicSlot = item->data(0, Qt::UserRole + 10).toInt();
    if(!owner)
        return;
    focusLogicSlot(owner, logicSlot);
    QMenu menu(this);
    QAction* copy = menu.addAction(tr("Copy Logic slot"));
    QAction* paste = menu.addAction(tr("Paste Logic slot"));
    QAction* pasteWorld = menu.addAction(
        tr("Paste Logic slot at original world positions"));
    QAction* selected = menu.exec(event->globalPos());
    if(selected == copy)
        m_pView->copyLogicSlot(owner, logicSlot);
    else if(selected == paste)
        m_pView->pasteLogicSlotFromClipboard(false);
    else if(selected == pasteWorld)
        m_pView->pasteLogicSlotFromClipboard(true);
}

int CTreeView::parentDeepCount(QTreeWidgetItem *pItem)
{
    int parentCount=0;
    auto pParent = pItem->parent();
    while(nullptr != pParent)
    {
        pParent = pParent->parent();
        ++parentCount;
    }
    return parentCount;
}

void CTreeView::recalcParent(QTreeWidgetItem *pItem)
{
    int nChildren = pItem->childCount();
    switch (nChildren)
    {
    case 0:
        if(pItem->parent()) // empty children, not root elem
        {
            auto pTopParent = pItem->parent();
            pTopParent->removeChild(pItem);
            removeItemWidget(pItem, 0);
        }
        break;
    case 1:
        if(pItem->parent()) //not root elem
        {
            //move child elem to parent
            auto pObject = dynamic_cast<CTreeObject*>(pItem);
            auto pChild = dynamic_cast<CTreeObject*>(pItem->child(0));
            pObject->setId(pChild->nodeId());
            pObject->removeChild(pChild);
            removeItemWidget(pChild, 0);
            pObject->setText(1, "1");
        }
        break;
    default:
        pItem->setText(1, QString::number(nChildren));
        break;
    }

}

QTreeWidgetItem *CTreeView::category(QString &categoryName)
{
    QTreeWidgetItemIterator it(this);
    while (*it)
    {
        if ((*it)->text(0) == categoryName)
            return (*it);
        ++it;
    }
    return nullptr;
}

QString CTreeView::objectNameByType(ENodeType type) const
{
    QMap<ENodeType, QString> type2str;
    type2str.insert(ENodeType::eWorldObject, tr("World objects"));
    type2str.insert(ENodeType::eUnit, tr("Units"));
    type2str.insert(ENodeType::eTorch, tr("Torches"));
    type2str.insert(ENodeType::eLever, tr("Levers"));
    type2str.insert(ENodeType::eMagicTrap, tr("Magic traps"));
    type2str.insert(ENodeType::eLight, tr("Light sources"));
    type2str.insert(ENodeType::eSound, tr("Sound sources"));
    type2str.insert(ENodeType::eParticle, tr("Particle sources"));

    return type2str[type];
}

ENodeType CTreeView::objectTypeByString(QString str) const
{
    QMap<ENodeType, QString> type2str;
    type2str.insert(ENodeType::eWorldObject, tr("World objects"));
    type2str.insert(ENodeType::eUnit, tr("Units"));
    type2str.insert(ENodeType::eTorch, tr("Torches"));
    type2str.insert(ENodeType::eLever, tr("Levers"));
    type2str.insert(ENodeType::eMagicTrap, tr("Magic traps"));
    type2str.insert(ENodeType::eLight, tr("Light sources"));
    type2str.insert(ENodeType::eSound, tr("Sound sources"));
    type2str.insert(ENodeType::eParticle, tr("Particle sources"));
    return type2str.key(str);
}

void CTreeView::itemClickedOver(QTreeWidgetItem *pItem, int column)
{
    Q_UNUSED(column);
    if(!pItem || !m_pView) return;
    if(auto* group = dynamic_cast<CTreeObject*>(pItem)) if(!group->node()) {
        group->setExpanded(!group->isExpanded()); return;
    }

    if(pItem->data(0, Qt::UserRole + 10).isValid()
       && !dynamic_cast<CTreeObject*>(pItem))
    {
        const int logicSlot = pItem->data(0, Qt::UserRole + 10).toInt();
        CUnit* owner = reinterpret_cast<CUnit*>(
            pItem->data(0, Qt::UserRole + 11).toULongLong());
        if(owner)
        {
            m_focusedLogicUnit = owner;
            m_focusedLogicSlot = logicSlot;
            const QList<CNode*> selected = m_pView->selectionSnapshot();
            const bool preserveUnitGroup = selected.size() > 1
                && selected.contains(owner)
                && std::all_of(selected.begin(), selected.end(), [](CNode* node)
                   { return dynamic_cast<CUnit*>(node) != nullptr; });
            if(!preserveUnitGroup)
                m_pView->selectNode(owner, false);
            m_pView->setLogicSlotIndex(logicSlot);
            focusLogicSlot(owner, logicSlot, false);
        }
        return;
    }

    CTreeObject* objectItem = dynamic_cast<CTreeObject*>(pItem);
    if(!objectItem)
        return;
    QList<CNode*> nodes;
    if(objectItem->node())
        nodes.append(objectItem->node());
    int logicSlot = -1;
    for(QTreeWidgetItem* ancestor = pItem; ancestor; ancestor = ancestor->parent())
        if(ancestor->data(0, Qt::UserRole + 10).isValid())
        {
            logicSlot = ancestor->data(0, Qt::UserRole + 10).toInt();
            if(nodes.isEmpty())
            {
                CNode* owner = reinterpret_cast<CNode*>(
                    ancestor->data(0, Qt::UserRole + 11).toULongLong());
                if(owner) nodes.append(owner);
            }
            break;
        }
    if(logicSlot >= 0)
        m_pView->setLogicSlotIndex(logicSlot);
    clearLogicSlotFocus();
    const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
    m_pView->selectNodes(nodes,
        modifiers.testFlag(Qt::ShiftModifier)
        || modifiers.testFlag(Qt::ControlModifier));
}

void CTreeView::onNodeDelete(uint nodeId)
{
    Q_UNUSED(nodeId);
    if(m_pView) m_pView->collectObjectTreeData();
}

void CTreeView::addNodeToTree(CNode *pNode)
{
    Q_UNUSED(pNode);
    if(m_pView)
        m_pView->collectObjectTreeData();
    return;
#if 0
    QString catName = "";
    QString groupName = "";
    ENodeType type = pNode->nodeType();

    switch (type)
    {
    case ENodeType::eTorch:
    case ENodeType::eLever:
    case ENodeType::eMagicTrap:
    case ENodeType::eLight:
    case ENodeType::eSound:
    case ENodeType::eParticle:
    case ENodeType::eWorldObject:
    {
        groupName = pNode->mapName();
        catName = objectNameByType(type);
        break;
    }
    case ENodeType::eUnit:
    {
        catName = objectNameByType(type);
        auto pUnitItem = dynamic_cast<CUnit*>(pNode);
        groupName = pUnitItem->databaseName();
        break;
    }
    default:
    {
        //do not collect nothing for Tree View
        break;
    }
    }

    if(!catName.isEmpty())
    {
        auto pCatItem = category(catName);
        if(pCatItem)
        {
            bool bFound = false;
            for(int i(0); i<pCatItem->childCount(); ++i)
            {
                auto pChild = pCatItem->child(i);
                if(pChild->text(0) == groupName)
                {
                    bFound = true;
                    auto pItem = new CTreeObject(dynamic_cast<CTreeObject*>(pChild), pNode);
                    pItem->setText(0, QString::number(pNode->mapId()));
                    recalcParent(pChild);
                }
            }
            if(!bFound)
            {
                auto pItem = new CTreeObject(dynamic_cast<CTreeObject*>(pCatItem), pNode);
                pItem->setText(0, groupName);
                pItem->setText(1, "1");
                recalcParent(pCatItem);
            }
        }
        clearSelection();
        sortItems(0, Qt::SortOrder::AscendingOrder);
        applyFilter();
    }
#endif
}

void CTreeView::itemDoubleClickedOver(QTreeWidgetItem* pItem, int column)
{
    if(pItem->data(0, Qt::UserRole + 10).isValid()
       && !dynamic_cast<CTreeObject*>(pItem))
    {
        itemClickedOver(pItem, column);
        return;
    }

    CTreeObject* objectItem = dynamic_cast<CTreeObject*>(pItem);
    if(!objectItem || !objectItem->node() || column != 0)
        return;
    CNode* node = objectItem->node();
    for(QTreeWidgetItem* ancestor = pItem; ancestor; ancestor = ancestor->parent())
        if(ancestor->data(0, Qt::UserRole + 10).isValid())
        {
            m_pView->setLogicSlotIndex(
                ancestor->data(0, Qt::UserRole + 10).toInt());
            break;
        }
    m_pView->selectNode(node, false, true);
    m_pView->moveCamToSelectedObjects();
}

void CTreeView::revealNode(CNode* pNode)
{
    markNode(pNode, true);
}

void CTreeView::markNode(CNode* pNode, bool scrollToNode)
{
    if(!pNode)
        return;
    QList<CNode*> nodes;
    if(m_pView)
        nodes = m_pView->selectionSnapshot();
    if(!nodes.contains(pNode))
        nodes.append(pNode);
    markNodes(nodes, pNode, scrollToNode);
}

void CTreeView::markNodes(const QList<CNode*>& nodes, CNode* primaryNode,
                          bool scrollToPrimary)
{
    const QSignalBlocker signalGuard(this);
    QSet<CNode*> wanted;
    for(CNode* node : nodes)
        if(node) wanted.insert(node);
    if(wanted.isEmpty())
    {
        clearViewportSelectionMarker();
        return;
    }

    if(m_focusedLogicUnit && !wanted.contains(m_focusedLogicUnit))
        clearLogicSlotFocus();
    clearSelection();
    CTreeObject* primaryItem = nullptr;
    QTreeWidgetItemIterator it(this);
    while(*it)
    {
        CTreeObject* item = dynamic_cast<CTreeObject*>(*it);
        if(item && wanted.contains(item->node()))
        {
            if(item->node() != m_focusedLogicUnit)
                item->setSelected(true);
            if(CScene::getInstance()->getMode() == eEditModeLogic
               && item->node()
               && (item->node()->nodeType() == eUnit
                   || item->node()->nodeType() == eMagicTrap))
                expandBehaviorHierarchy(item);
            for(QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent())
                parent->setExpanded(true);
            if(item->node() == primaryNode)
                primaryItem = item;
        }
        ++it;
    }
    if(primaryItem)
    {
        setCurrentItem(primaryItem, 0, QItemSelectionModel::NoUpdate);
        if(scrollToPrimary)
            scrollToItem(primaryItem, QAbstractItemView::PositionAtCenter);
    }
    if(m_focusedLogicUnit)
        focusLogicSlot(m_focusedLogicUnit, m_focusedLogicSlot, scrollToPrimary);
    applyFilter();
}

void CTreeView::focusLogicSlot(CUnit* owner, int logicSlot, bool scrollToFocused)
{
    const QSignalBlocker signalGuard(this);
    m_focusedLogicUnit = owner;
    m_focusedLogicSlot = logicSlot;
    QTreeWidgetItemIterator it(this);
    while(*it)
    {
        QTreeWidgetItem* item = *it;
        if(!dynamic_cast<CTreeObject*>(item)
           && item->data(0, Qt::UserRole + 10).isValid()
           && item->data(0, Qt::UserRole + 10).toInt() == logicSlot
           && reinterpret_cast<CUnit*>(item->data(
                0, Qt::UserRole + 11).toULongLong()) == owner)
        {
            clearSelection();
            item->setSelected(true);
            setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
            for(QTreeWidgetItem* parent = item->parent(); parent; parent = parent->parent())
                parent->setExpanded(true);
            item->setExpanded(true);
            if(scrollToFocused)
                scrollToItem(item, QAbstractItemView::PositionAtCenter);
            return;
        }
        ++it;
    }
    clearLogicSlotFocus();
}

void CTreeView::clearLogicSlotFocus()
{
    m_focusedLogicUnit = nullptr;
    m_focusedLogicSlot = -1;
}

void CTreeView::expandBehaviorHierarchy(QTreeWidgetItem* ownerItem)
{
    if(!ownerItem)
        return;
    ownerItem->setExpanded(true);
    for(int index = 0; index < ownerItem->childCount(); ++index)
    {
        QTreeWidgetItem* child = ownerItem->child(index);
        if(!child || !child->data(0, BehaviorItemRole).toBool())
            continue;
        const QVariant slot = child->data(0, Qt::UserRole + 10);
        if(!slot.isValid() || !m_pView
           || slot.toInt() == m_pView->logicSlotIndex())
            child->setExpanded(true);
    }
}

void CTreeView::clearViewportSelectionMarker()
{
    const QSignalBlocker signalGuard(this);
    clearLogicSlotFocus();
    clearSelection();
    setCurrentItem(nullptr);
}

void CTreeView::onChangeNodeId(uint oldId, uint newId)
{
    Q_UNUSED(oldId);
    Q_UNUSED(newId);
    if(m_pView) m_pView->collectObjectTreeData();
    return;
#if 0
    QTreeWidgetItemIterator it(this);
    while (*it)
    {
        auto pNode = dynamic_cast<CTreeObject*>(*it);
        if (pNode && pNode->storedId()==oldId)
        {
            pNode->setId(newId);
            if(pNode->text(0) == QString::number(oldId))
                pNode->setText(0, QString::number(newId));
            break;
        }
        ++it;
    }
    sortItems(0, Qt::SortOrder::AscendingOrder);
    applyFilter();
#endif
}

void CTreeView::onChangeObjectName(CNode *pNode)
{
    Q_UNUSED(pNode);
    if(m_pView) m_pView->collectObjectTreeData();
    return;
#if 0
    uint nodeId = pNode->mapId();
    QTreeWidgetItemIterator it(this);
    while (*it)
    {
        auto pItem = dynamic_cast<CTreeObject*>(*it);
        if (pItem && pItem->nodeId()==nodeId)
        {
            auto pParent = (*it)->parent();
            pParent->removeChild(*it);
            removeItemWidget(*it, 0);
            recalcParent(pParent);
            break;
        }
        ++it;
    }
    addNodeToTree(pNode);

    //sortItems(0, Qt::SortOrder::AscendingOrder);
#endif
}

CTreeObject::CTreeObject(QTreeWidget *pParent): QTreeWidgetItem(pParent)
  ,m_nodeId(0)
  ,m_pNode(nullptr)
{

}

CTreeObject::CTreeObject(QTreeWidgetItem *pParent, CNode* pNode) : QTreeWidgetItem(pParent)
  ,m_nodeId(pNode ? pNode->mapId() : 0)
  ,m_pNode(pNode)
{

}

CTreeObject::CTreeObject(CTreeObject *pParent, CNode* pNode) : QTreeWidgetItem(pParent)
  ,m_nodeId(pNode ? pNode->mapId() : 0)
  ,m_pNode(pNode)
{
    CNode* parentNode = pParent->node();
    if(parentNode)
    {
        pParent->setId(0);
        pParent->setNode(nullptr);
        auto pItem = new CTreeObject(pParent, parentNode);
        pItem->setText(0, QString::number(parentNode->mapId()));
    }
}

uint CTreeObject::nodeId() const
{
    return m_pNode ? m_pNode->mapId() : m_nodeId;
}

void CTreeObject::setNode(CNode* node)
{
    m_pNode = node;
    m_nodeId = node ? node->mapId() : 0;
}
