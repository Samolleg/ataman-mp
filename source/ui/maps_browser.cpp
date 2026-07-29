#include "maps_browser.h"

#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "collection_document.h"

namespace {
enum EMapItemData
{
    eMprPath = Qt::UserRole,
    eMobPath,
    eZoneId,
    eCollectionPath,
    eSourceArchive
};
}

CMapsBrowser::CMapsBrowser(QWidget* parent)
    : QWidget(parent)
    , m_filter(new QLineEdit(this))
    , m_tree(new QTreeWidget(this))
    , m_status(new QLabel(this))
    , m_newCollection(new QPushButton(tr("New Collection..."), this))
    , m_refresh(new QPushButton(tr("Refresh maps and collections"), this))
{
    m_filter->setObjectName("mapsBrowserFilter");
    m_tree->setObjectName("mapsBrowserTree");
    m_newCollection->setObjectName("newCollectionButton");
    m_refresh->setObjectName("refreshMapsAndCollectionsButton");
    m_filter->setPlaceholderText(tr("Filter maps..."));
    m_filter->setClearButtonEnabled(true);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Map"), tr("Resources")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_filter);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_newCollection);
    layout->addWidget(m_refresh);
    layout->addWidget(m_status);

    connect(m_filter, &QLineEdit::textChanged, this, &CMapsBrowser::applyFilter);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &CMapsBrowser::activateItem);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &CMapsBrowser::showContextMenu);
    connect(m_newCollection, &QPushButton::clicked,
            this, &CMapsBrowser::newCollectionRequested);
    connect(m_refresh, &QPushButton::clicked, this, &CMapsBrowser::refreshRequested);
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item)
    {
        if(item == m_tree->topLevelItem(0))
            QSettings().setValue("ui/mapsBrowser/mapsExpanded", true);
        else if(item == m_tree->topLevelItem(1))
            QSettings().setValue("ui/mapsBrowser/collectionsExpanded", true);
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item)
    {
        if(item == m_tree->topLevelItem(0))
            QSettings().setValue("ui/mapsBrowser/mapsExpanded", false);
        else if(item == m_tree->topLevelItem(1))
            QSettings().setValue("ui/mapsBrowser/collectionsExpanded", false);
    });
}

SMapCatalogResult CMapsBrowser::refresh(const QVector<QString>& resourceRoots,
                                        const QVector<QString>& mapRoots,
                                        const QString& collectionsRoot)
{
    const SMapCatalogResult result = CMapCatalog::load(resourceRoots, mapRoots);
    refresh(result, collectionsRoot);
    return result;
}

void CMapsBrowser::refresh(const SMapCatalogResult& result,
                           const QString& collectionsRoot)
{
    m_entries = result.entries;
    m_collectionPaths.clear();
    QDir collectionDirectory(collectionsRoot);
    for(const QFileInfo& file : collectionDirectory.entryInfoList(
            QStringList() << "*.collection", QDir::Files | QDir::Readable, QDir::Name))
        m_collectionPaths.append(file.absoluteFilePath());
    rebuildTree();
    if(result.sourceArchive.isEmpty())
        m_status->setText(result.messages.join('\n'));
    else
    {
        const int complete = std::count_if(m_entries.constBegin(), m_entries.constEnd(),
            [](const SMapCatalogEntry& entry) { return entry.isComplete(); });
        m_status->setText(tr("%1 of %2 maps available\n%3")
            .arg(complete).arg(m_entries.size()).arg(result.sourceArchive));
        m_status->setToolTip(result.messages.join('\n'));
    }
}

void CMapsBrowser::rebuildTree()
{
    QSettings settings;
    const bool mapsExpanded = settings.value("ui/mapsBrowser/mapsExpanded", true).toBool();
    const bool collectionsExpanded = settings.value(
        "ui/mapsBrowser/collectionsExpanded", true).toBool();
    m_tree->clear();
    QTreeWidgetItem* maps = new QTreeWidgetItem(m_tree, {tr("Maps")});
    maps->setFirstColumnSpanned(true);
    maps->setExpanded(mapsExpanded);
    for(const SMapCatalogEntry& entry : m_entries)
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(maps);
        item->setText(0, entry.zoneId);
        item->setText(1, QString("%1 + %2").arg(entry.mprName, entry.mobName));
        item->setData(0, eMprPath, entry.mprPath);
        item->setData(0, eMobPath, entry.mobPath);
        item->setData(0, eZoneId, entry.zoneId);
        item->setData(0, eSourceArchive, entry.sourceArchive);
        item->setToolTip(0, tr("Allod: %1\nType: %2\nSource: %3")
            .arg(entry.allodId, entry.zoneType, entry.sourceArchive));
        if(!entry.isComplete())
        {
            item->setDisabled(true);
            item->setToolTip(1, tr("The MPR or MOB file is missing from the active map roots."));
        }
    }
    QTreeWidgetItem* collections = new QTreeWidgetItem(m_tree, {tr("Collections")});
    collections->setFirstColumnSpanned(true);
    collections->setExpanded(collectionsExpanded);
    for(const QString& path : m_collectionPaths)
    {
        const QFileInfo file(path);
        SEnduranceCollection collection;
        QString collectionError;
        const bool readable = CCollectionDocument::load(path, &collection,
                                                        &collectionError);
        const QString displayName = readable && !collection.name.trimmed().isEmpty()
            ? collection.name.trimmed() : file.completeBaseName();
        QTreeWidgetItem* item = new QTreeWidgetItem(collections,
            {displayName, tr("Portable map set")});
        item->setData(0, eCollectionPath, path);
        item->setToolTip(0, readable
            ? QDir::toNativeSeparators(path)
            : tr("%1\n\nCould not read collection metadata: %2")
                .arg(QDir::toNativeSeparators(path), collectionError));
    }
    if(m_collectionPaths.isEmpty())
        collections->setToolTip(0, tr("No .collection files were found. Create one from File > Collections."));
    applyFilter(m_filter->text());
}

void CMapsBrowser::showContextMenu(const QPoint& position)
{
    QTreeWidgetItem* item = m_tree->itemAt(position);
    if(!item || item->parent() == nullptr)
        return;
    m_tree->setCurrentItem(item);
    const QString collectionPath = item->data(0, eCollectionPath).toString();
    QMenu menu(this);
    if(!collectionPath.isEmpty())
    {
        QAction* open = menu.addAction(tr("Open"));
        QAction* edit = menu.addAction(tr("Edit..."));
        QAction* duplicate = menu.addAction(tr("Duplicate..."));
        QAction* saveAs = menu.addAction(tr("Save As..."));
        menu.addSeparator();
        QAction* remove = menu.addAction(tr("Delete"));
        QAction* selected = menu.exec(m_tree->viewport()->mapToGlobal(position));
        if(selected == open) emit openCollectionRequested(collectionPath);
        else if(selected == edit) emit editCollectionRequested(collectionPath);
        else if(selected == duplicate) emit duplicateCollectionRequested(collectionPath);
        else if(selected == saveAs) emit saveCollectionAsRequested(collectionPath);
        else if(selected == remove) emit deleteCollectionRequested(collectionPath);
        return;
    }

    const QString zoneId = item->data(0, eZoneId).toString();
    if(zoneId.isEmpty())
        return;
    const QString mprPath = item->data(0, eMprPath).toString();
    const QString mobPath = item->data(0, eMobPath).toString();
    const QString sourceArchive = item->data(0, eSourceArchive).toString();
    QAction* open = menu.addAction(tr("Open Map"));
    open->setEnabled(!item->isDisabled() && !mprPath.isEmpty() && !mobPath.isEmpty());
    QAction* edit = menu.addAction(tr("Edit in Catalog Editor..."));
    edit->setEnabled(!sourceArchive.isEmpty());
    QAction* duplicate = menu.addAction(tr("Duplicate as Collection..."));
    duplicate->setEnabled(!mprPath.isEmpty() && !mobPath.isEmpty());
    QAction* selected = menu.exec(m_tree->viewport()->mapToGlobal(position));
    if(selected == open) emit openMapRequested(mprPath, mobPath, zoneId);
    else if(selected == edit) emit editMapRequested(sourceArchive, zoneId);
    else if(selected == duplicate)
        emit duplicateMapAsCollectionRequested(zoneId, mprPath, mobPath);
}

void CMapsBrowser::applyFilter(const QString& text)
{
    if(m_tree->topLevelItemCount() == 0)
        return;
    QTreeWidgetItem* maps = m_tree->topLevelItem(0);
    const QString needle = text.trimmed();
    for(int index = 0; index < maps->childCount(); ++index)
    {
        QTreeWidgetItem* item = maps->child(index);
        const bool match = needle.isEmpty() || item->text(0).startsWith(needle, Qt::CaseInsensitive)
            || item->text(1).contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
    if(m_tree->topLevelItemCount() > 1)
    {
        QTreeWidgetItem* collections = m_tree->topLevelItem(1);
        for(int index = 0; index < collections->childCount(); ++index)
        {
            QTreeWidgetItem* item = collections->child(index);
            item->setHidden(!needle.isEmpty()
                && !item->text(0).startsWith(needle, Qt::CaseInsensitive));
        }
    }
}

void CMapsBrowser::activateItem(QTreeWidgetItem* item, int)
{
    if(!item || item->isDisabled())
        return;
    const QString collection = item->data(0, eCollectionPath).toString();
    if(!collection.isEmpty())
    {
        emit openCollectionRequested(collection);
        return;
    }
    if(item->data(0, eZoneId).toString().isEmpty())
        return;
    emit openMapRequested(item->data(0, eMprPath).toString(),
                          item->data(0, eMobPath).toString(),
                          item->data(0, eZoneId).toString());
}
