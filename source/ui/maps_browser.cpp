#include "maps_browser.h"

#include <algorithm>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum EMapItemData
{
    eMprPath = Qt::UserRole,
    eMobPath,
    eZoneId
};
}

CMapsBrowser::CMapsBrowser(QWidget* parent)
    : QWidget(parent)
    , m_filter(new QLineEdit(this))
    , m_tree(new QTreeWidget(this))
    , m_status(new QLabel(this))
{
    m_filter->setPlaceholderText(tr("Filter maps..."));
    m_filter->setClearButtonEnabled(true);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Map"), tr("Resources")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_filter);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_status);

    connect(m_filter, &QLineEdit::textChanged, this, &CMapsBrowser::applyFilter);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &CMapsBrowser::activateItem);
}

void CMapsBrowser::refresh(const QVector<QString>& resourceRoots,
                           const QVector<QString>& mapRoots)
{
    const SMapCatalogResult result = CMapCatalog::load(resourceRoots, mapRoots);
    m_entries = result.entries;
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
    m_tree->clear();
    QTreeWidgetItem* maps = new QTreeWidgetItem(m_tree, {tr("Maps")});
    maps->setFirstColumnSpanned(true);
    maps->setExpanded(true);
    for(const SMapCatalogEntry& entry : m_entries)
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(maps);
        item->setText(0, entry.zoneId);
        item->setText(1, QString("%1 + %2").arg(entry.mprName, entry.mobName));
        item->setData(0, eMprPath, entry.mprPath);
        item->setData(0, eMobPath, entry.mobPath);
        item->setData(0, eZoneId, entry.zoneId);
        item->setToolTip(0, tr("Allod: %1\nType: %2\nSource: %3")
            .arg(entry.allodId, entry.zoneType, entry.sourceArchive));
        if(!entry.isComplete())
        {
            item->setDisabled(true);
            item->setToolTip(1, tr("The MPR or MOB file is missing from the active map roots."));
        }
    }
    applyFilter(m_filter->text());
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
}

void CMapsBrowser::activateItem(QTreeWidgetItem* item, int)
{
    if(!item || item->isDisabled() || item->data(0, eZoneId).toString().isEmpty())
        return;
    emit openMapRequested(item->data(0, eMprPath).toString(),
                          item->data(0, eMobPath).toString(),
                          item->data(0, eZoneId).toString());
}
