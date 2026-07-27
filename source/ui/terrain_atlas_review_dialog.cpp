#include "terrain_atlas_review_dialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

#include "endurance_document.h"

namespace {
const char* FamilyMime = "application/x-endurance-atlas-family";
const char* TileMime = "application/x-endurance-atlas-tile";
const int KindRole = Qt::UserRole;
const int IdRole = Qt::UserRole + 1;
const int SourceFamilyRole = Qt::UserRole + 2;
enum ETreeKind { GroupKind = 0, FamilyKind = 1, TileKind = 2 };

QByteArray numberMime(qint32 number)
{
    return QByteArray::number(number);
}

qint32 mimeNumber(const QMimeData* data, const char* type)
{
    bool ok = false;
    const qint32 value = data->data(type).toInt(&ok);
    return ok ? value : -1;
}

QIcon tileIcon(const QVector<QImage>& images, int slot)
{
    if(slot < 0 || slot >= images.size()) return QIcon();
    const QPixmap pixmap = QPixmap::fromImage(images.at(slot)).scaled(
        56, 56, Qt::KeepAspectRatio, Qt::FastTransformation);
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    icon.addPixmap(pixmap, QIcon::Active);
    icon.addPixmap(pixmap, QIcon::Selected);
    return icon;
}
}

class CAtlasMaterialTree : public QTreeWidget
{
public:
    explicit CAtlasMaterialTree(QWidget* parent = nullptr) : QTreeWidget(parent)
    {
        setHeaderLabels({tr("Materials and assigned tiles"), tr("Tiles")});
        header()->setSectionResizeMode(0, QHeaderView::Stretch);
        header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        setIconSize(QSize(56, 56));
        setSelectionMode(QAbstractItemView::SingleSelection);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDefaultDropAction(Qt::MoveAction);
    }

    std::function<void(int, qint32, qint32)> tileDropped;

protected:
    void startDrag(Qt::DropActions) override
    {
        QTreeWidgetItem* selected = currentItem();
        if(!selected) return;
        QMimeData* mime = new QMimeData;
        if(selected->data(0, KindRole).toInt() == FamilyKind)
            mime->setData(FamilyMime, numberMime(selected->data(0, IdRole).toInt()));
        else if(selected->data(0, KindRole).toInt() == TileKind)
        {
            mime->setData(TileMime, numberMime(selected->data(0, IdRole).toInt()));
            mime->setData(FamilyMime,
                numberMime(selected->data(0, SourceFamilyRole).toInt()));
        }
        else
        {
            delete mime;
            return;
        }
        QDrag* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::MoveAction);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if(event->mimeData()->hasFormat(TileMime)) event->acceptProposedAction();
        else QTreeWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if(event->mimeData()->hasFormat(TileMime)) event->acceptProposedAction();
        else QTreeWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        if(!event->mimeData()->hasFormat(TileMime))
        {
            QTreeWidget::dropEvent(event);
            return;
        }
        QTreeWidgetItem* target = itemAt(event->pos());
        while(target && target->data(0, KindRole).toInt() != FamilyKind)
            target = target->parent();
        if(!target) return;
        const int tile = mimeNumber(event->mimeData(), TileMime);
        const qint32 source = mimeNumber(event->mimeData(), FamilyMime);
        const qint32 destination = target->data(0, IdRole).toInt();
        if(tile >= 0 && destination > 0 && tileDropped)
        {
            event->acceptProposedAction();
            const auto callback = tileDropped;
            QTimer::singleShot(0, this, [callback, tile, source, destination]() {
                callback(tile, source, destination);
            });
        }
    }
};

class CAtlasTilePool : public QListWidget
{
public:
    explicit CAtlasTilePool(QWidget* parent = nullptr) : QListWidget(parent)
    {
        setViewMode(QListView::IconMode);
        setResizeMode(QListView::Adjust);
        setIconSize(QSize(72, 72));
        setGridSize(QSize(96, 104));
        setMovement(QListView::Static);
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        viewport()->setAcceptDrops(true);
    }

    std::function<void(int)> tileDropped;

protected:
    void startDrag(Qt::DropActions) override
    {
        QListWidgetItem* selected = currentItem();
        if(!selected) return;
        QMimeData* mime = new QMimeData;
        mime->setData(TileMime, numberMime(selected->data(IdRole).toInt()));
        mime->setData(FamilyMime, numberMime(-1));
        QDrag* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::MoveAction);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if(event->mimeData()->hasFormat(TileMime)) event->acceptProposedAction();
        else QListWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if(event->mimeData()->hasFormat(TileMime)) event->acceptProposedAction();
        else QListWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        const int tile = mimeNumber(event->mimeData(), TileMime);
        if(tile >= 0 && tileDropped)
        {
            event->acceptProposedAction();
            const auto callback = tileDropped;
            QTimer::singleShot(0, this, [callback, tile]() { callback(tile); });
        }
    }
};

class CFamilyCornerButton : public QPushButton
{
public:
    explicit CFamilyCornerButton(const QString& corner, QWidget* parent = nullptr)
        : QPushButton(parent), m_corner(corner)
    {
        setAcceptDrops(true);
        setMinimumSize(145, 72);
    }

    std::function<void(qint32)> familyDropped;

    void setFamily(const QString& name)
    {
        setText(QStringLiteral("%1\n%2").arg(m_corner, name));
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if(event->mimeData()->hasFormat(FamilyMime)
           && !event->mimeData()->hasFormat(TileMime))
            event->acceptProposedAction();
    }

    void dropEvent(QDropEvent* event) override
    {
        const qint32 family = mimeNumber(event->mimeData(), FamilyMime);
        if(family > 0 && familyDropped)
        {
            familyDropped(family);
            event->acceptProposedAction();
        }
    }

private:
    QString m_corner;
};

CTerrainAtlasReviewDialog::CTerrainAtlasReviewDialog(
    const STerrainAtlasProfile& profile, const QVector<QImage>& tileImages,
    QWidget* parent)
    : QDialog(parent), m_profile(profile), m_tileImages(tileImages),
      m_materials(new CAtlasMaterialTree(this)),
      m_unassigned(new CAtlasTilePool(this)), m_preview(new QLabel(this)),
      m_tileTitle(new QLabel(this)), m_tileHelp(new QLabel(this)),
      m_binding(new QLabel(this)), m_storage(new QLabel(this)),
      m_diagnostics(new QLabel(this)), m_familyReason(new QLabel(this)),
      m_familyClass(new QComboBox(this)),
      m_confirmFamily(new QPushButton(tr("Confirm material"), this)),
      m_thumbnail(new QPushButton(tr("Use selected tile as thumbnail"), this)),
      m_apply(new QPushButton(tr("Apply + Close"), this))
{
    setWindowTitle(tr("Material atlas"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(1320, 820);
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* explanation = new QLabel(tr(
        "The editor has already made every safe assignment it could. Painting can use those materials now. "
        "Unusual tiles stay in the unassigned pool and never disable the valid palette. Drag a tile onto a material to assign it; "
        "for transition tiles, select the tile and drag material rows onto its four corners. Missing transitions only produce hard paint edges."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    m_binding->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_binding->setText(tr("Namespace: %1   Slots: %2   Decoded atlas SHA-256: %3")
        .arg(m_profile.mprNamespace).arg(m_profile.atlasSlotCount)
        .arg(QString::fromLatin1(m_profile.atlasPixelSha256)));
    layout->addWidget(m_binding);
    m_storage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_storage->setWordWrap(true);
    layout->addWidget(m_storage);

    QHBoxLayout* body = new QHBoxLayout;
    QVBoxLayout* materialSide = new QVBoxLayout;
    materialSide->addWidget(new QLabel(tr("Materials — drag a material row onto a tile corner"), this));
    materialSide->addWidget(m_materials, 1);
    QHBoxLayout* familyActions = new QHBoxLayout;
    QPushButton* add = new QPushButton(tr("New"), this);
    QPushButton* rename = new QPushButton(tr("Rename"), this);
    QPushButton* remove = new QPushButton(tr("Delete empty"), this);
    familyActions->addWidget(add);
    familyActions->addWidget(rename);
    familyActions->addWidget(remove);
    materialSide->addLayout(familyActions);
    m_familyClass->addItem(tr("Ground"), 0);
    m_familyClass->addItem(tr("Underwater only"), 1);
    m_familyClass->addItem(tr("Overlay"), 2);
    m_familyClass->addItem(tr("Ground + Overlay"), 3);
    m_familyClass->addItem(tr("Ground (surface + underwater)"), 4);
    materialSide->addWidget(m_familyClass);
    m_familyReason->setWordWrap(true);
    materialSide->addWidget(m_familyReason);
    materialSide->addWidget(m_confirmFamily);
    body->addLayout(materialSide, 4);

    QVBoxLayout* inspector = new QVBoxLayout;
    m_tileTitle->setAlignment(Qt::AlignCenter);
    QFont titleFont = m_tileTitle->font();
    titleFont.setBold(true);
    m_tileTitle->setFont(titleFont);
    inspector->addWidget(m_tileTitle);
    m_preview->setFixedSize(260, 260);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    inspector->addWidget(m_preview, 0, Qt::AlignHCenter);
    QGridLayout* corners = new QGridLayout;
    m_corners[0] = new CFamilyCornerButton(tr("NW"), this);
    m_corners[1] = new CFamilyCornerButton(tr("NE"), this);
    m_corners[2] = new CFamilyCornerButton(tr("SE"), this);
    m_corners[3] = new CFamilyCornerButton(tr("SW"), this);
    corners->addWidget(m_corners[0], 0, 0);
    corners->addWidget(m_corners[1], 0, 1);
    corners->addWidget(m_corners[3], 1, 0);
    corners->addWidget(m_corners[2], 1, 1);
    inspector->addLayout(corners);
    m_tileHelp->setWordWrap(true);
    inspector->addWidget(m_tileHelp);
    inspector->addWidget(m_thumbnail);
    inspector->addStretch();
    body->addLayout(inspector, 3);

    QVBoxLayout* poolSide = new QVBoxLayout;
    poolSide->addWidget(new QLabel(tr("Unassigned / unusual tiles"), this));
    QLabel* poolHelp = new QLabel(tr(
        "These tiles are excluded from automatic painting. Drag one to a material for a uniform assignment, then edit individual corners if it is a transition."), this);
    poolHelp->setWordWrap(true);
    poolSide->addWidget(poolHelp);
    poolSide->addWidget(m_unassigned, 1);
    body->addLayout(poolSide, 3);
    layout->addLayout(body, 1);

    m_diagnostics->setWordWrap(true);
    layout->addWidget(m_diagnostics);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton* open = buttons->addButton(tr("Load profile..."), QDialogButtonBox::ActionRole);
    QPushButton* copy = buttons->addButton(tr("Save copy..."), QDialogButtonBox::ActionRole);
    buttons->addButton(m_apply, QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(open, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::openProfile);
    connect(copy, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::exportCopy);
    connect(m_apply, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::applyAndClose);
    connect(add, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::createMaterial);
    connect(rename, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::renameSelectedFamily);
    connect(remove, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::deleteSelectedFamily);
    connect(m_familyClass, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CTerrainAtlasReviewDialog::changeSelectedClassification);
    connect(m_confirmFamily, &QPushButton::clicked,
            this, &CTerrainAtlasReviewDialog::confirmSelectedFamily);
    connect(m_thumbnail, &QPushButton::clicked,
            this, &CTerrainAtlasReviewDialog::useSelectedTileAsThumbnail);
    connect(m_materials, &QTreeWidget::itemClicked,
            this, &CTerrainAtlasReviewDialog::selectTreeItem);
    connect(m_unassigned, &QListWidget::itemSelectionChanged,
            this, &CTerrainAtlasReviewDialog::selectPoolItem);
    m_materials->tileDropped = [this](int tile, qint32 source, qint32 target) {
        assignTile(tile, source, target);
    };
    m_unassigned->tileDropped = [this](int tile) { unassignTile(tile); };
    for(int corner = 0; corner < 4; ++corner)
    {
        m_corners[corner]->familyDropped = [this, corner](qint32 id) {
            assignCorner(corner, id);
        };
        connect(m_corners[corner], &QPushButton::clicked, this,
                [this, corner]() { chooseCornerMaterial(corner); });
    }

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("terrainAtlasReview/geometry")).toByteArray());
    connect(this, &QDialog::finished, this, [this](int) {
        QSettings settings;
        settings.setValue(QStringLiteral("terrainAtlasReview/geometry"), saveGeometry());
    });
    rebuildVisuals();
}

STerrainAtlasProfileFamily* CTerrainAtlasReviewDialog::family(qint32 id)
{
    for(STerrainAtlasProfileFamily& candidate : m_profile.families)
        if(candidate.id == id) return &candidate;
    return nullptr;
}

const STerrainAtlasProfileFamily* CTerrainAtlasReviewDialog::family(qint32 id) const
{
    for(const STerrainAtlasProfileFamily& candidate : m_profile.families)
        if(candidate.id == id) return &candidate;
    return nullptr;
}

STerrainAtlasProfileSlot* CTerrainAtlasReviewDialog::slot(int id)
{
    for(STerrainAtlasProfileSlot& candidate : m_profile.slotDefinitions)
        if(candidate.slot == id) return &candidate;
    return nullptr;
}

const STerrainAtlasProfileSlot* CTerrainAtlasReviewDialog::slot(int id) const
{
    for(const STerrainAtlasProfileSlot& candidate : m_profile.slotDefinitions)
        if(candidate.slot == id) return &candidate;
    return nullptr;
}

QString CTerrainAtlasReviewDialog::familyName(qint32 id) const
{
    const STerrainAtlasProfileFamily* found = family(id);
    return found ? found->name : tr("Unknown material %1").arg(id);
}

int CTerrainAtlasReviewDialog::familyThumbnailSlot(qint32 familyId) const
{
    const STerrainAtlasProfileFamily* entry = family(familyId);
    if(!entry) return -1;
    if(entry->thumbnailSlot >= 0) return entry->thumbnailSlot;
    for(int member : entry->memberSlots)
    {
        const STerrainAtlasProfileSlot* candidate = slot(member);
        if(candidate && candidate->assigned
           && candidate->signature.northWest == familyId
           && candidate->signature.northEast == familyId
           && candidate->signature.southEast == familyId
           && candidate->signature.southWest == familyId)
            return member;
    }
    return entry->memberSlots.isEmpty() ? -1 : entry->memberSlots.front();
}

void CTerrainAtlasReviewDialog::rebuildVisuals(qint32 selectFamily, int selectSlot)
{
    m_rebuilding = true;
    m_selectedSlot = selectSlot;
    m_materials->clear();
    m_unassigned->clear();
    QTreeWidgetItem* groups[3];
    const QString names[] = {tr("Ground"), tr("Underwater"), tr("Overlay")};
    for(int group = 0; group < 3; ++group)
    {
        groups[group] = new QTreeWidgetItem(m_materials, {names[group]});
        groups[group]->setData(0, KindRole, GroupKind);
        groups[group]->setFlags(Qt::ItemIsEnabled);
        QFont font = groups[group]->font(0);
        font.setBold(true);
        groups[group]->setFont(0, font);
        groups[group]->setExpanded(true);
    }
    QTreeWidgetItem* selectItem = nullptr;
    for(const STerrainAtlasProfileFamily& entry : m_profile.families)
    {
        const int group = !entry.landAllowed && entry.overlayAllowed
            ? 2 : (entry.underwaterRole == ETerrainAtlasUnderwaterRole::UnderwaterOnly ? 1 : 0);
        const int thumbnail = familyThumbnailSlot(entry.id);
        QTreeWidgetItem* familyItem = new QTreeWidgetItem(groups[group],
            {entry.name + (entry.ambiguous ? tr("  • confirm") : QString()),
             tr("%1").arg(entry.memberSlots.size())});
        familyItem->setIcon(0, tileIcon(m_tileImages, thumbnail));
        familyItem->setData(0, KindRole, FamilyKind);
        familyItem->setData(0, IdRole, entry.id);
        const QString sections = entry.landAllowed && entry.overlayAllowed
            ? tr("Ground + Overlay") : (entry.overlayAllowed ? tr("Overlay") : tr("Ground"));
        familyItem->setToolTip(0, tr("%1 atlas slots assigned. Allowed section: %2. Drag this row onto a selected tile corner.")
            .arg(entry.memberSlots.size()).arg(sections));
        for(int member : entry.memberSlots)
        {
            const STerrainAtlasProfileSlot* definition = slot(member);
            if(!definition || !definition->assigned) continue;
            QTreeWidgetItem* tileItem = new QTreeWidgetItem(familyItem,
                {member == thumbnail ? tr("Ⓣ  Tile %1").arg(member)
                                     : tr("Tile %1").arg(member), QString()});
            tileItem->setIcon(0, tileIcon(m_tileImages, member));
            tileItem->setData(0, KindRole, TileKind);
            tileItem->setData(0, IdRole, member);
            tileItem->setData(0, SourceFamilyRole, entry.id);
            tileItem->setToolTip(0, tr("NW %1 / NE %2 / SE %3 / SW %4")
                .arg(familyName(definition->signature.northWest))
                .arg(familyName(definition->signature.northEast))
                .arg(familyName(definition->signature.southEast))
                .arg(familyName(definition->signature.southWest)));
            if(member == thumbnail)
            {
                QFont thumbnailFont = tileItem->font(0);
                thumbnailFont.setBold(true);
                tileItem->setFont(0, thumbnailFont);
                tileItem->setToolTip(0, tr("Thumbnail tile for %1. %2")
                    .arg(entry.name, tileItem->toolTip(0)));
            }
            if(member == selectSlot) selectItem = tileItem;
        }
        if(entry.id == selectFamily && selectSlot < 0) selectItem = familyItem;
    }
    for(const STerrainAtlasProfileSlot& definition : m_profile.slotDefinitions)
        if(!definition.assigned || definition.ambiguous)
        {
            QListWidgetItem* item = new QListWidgetItem(
                tileIcon(m_tileImages, definition.slot), tr("Tile %1").arg(definition.slot),
                m_unassigned);
            item->setData(IdRole, definition.slot);
            item->setToolTip(tr("Parser proposal: NW %1 / NE %2 / SE %3 / SW %4")
                .arg(familyName(definition.signature.northWest))
                .arg(familyName(definition.signature.northEast))
                .arg(familyName(definition.signature.southEast))
                .arg(familyName(definition.signature.southWest)));
            if(definition.slot == selectSlot) m_unassigned->setCurrentItem(item);
        }
    if(selectItem)
    {
        m_materials->setCurrentItem(selectItem);
        QTreeWidgetItem* parent = selectItem->parent();
        if(parent) parent->setExpanded(true);
    }
    m_storage->setText(tr("Active profile: %1   (Use Save copy/Load profile for another visible, reopenable location.)")
        .arg(QDir::toNativeSeparators(
            CTerrainAtlasProfileStore::profilePath(m_profile.mprNamespace))));
    m_rebuilding = false;
    updateInspector();
    updateDiagnostics();
}

qint32 CTerrainAtlasReviewDialog::selectedFamilyId() const
{
    QTreeWidgetItem* item = m_materials->currentItem();
    if(!item || !item->isSelected()) return -1;
    while(item && item->data(0, KindRole).toInt() != FamilyKind) item = item->parent();
    return item ? item->data(0, IdRole).toInt() : -1;
}

void CTerrainAtlasReviewDialog::selectTreeItem(QTreeWidgetItem* item)
{
    if(m_rebuilding || !item) return;
    m_unassigned->clearSelection();
    m_selectedSlot = item->data(0, KindRole).toInt() == TileKind
        ? item->data(0, IdRole).toInt() : -1;
    updateInspector();
}

void CTerrainAtlasReviewDialog::selectPoolItem()
{
    if(m_rebuilding || !m_unassigned->currentItem()) return;
    m_materials->clearSelection();
    m_selectedSlot = m_unassigned->currentItem()->data(IdRole).toInt();
    updateInspector();
}

void CTerrainAtlasReviewDialog::updateInspector()
{
    const qint32 familyId = selectedFamilyId();
    const STerrainAtlasProfileFamily* selectedFamily = family(familyId);
    m_rebuilding = true;
    if(selectedFamily)
    {
        const int classification = selectedFamily->landAllowed && selectedFamily->overlayAllowed
            ? 3 : (!selectedFamily->landAllowed && selectedFamily->overlayAllowed
                ? 2 : (selectedFamily->underwaterRole == ETerrainAtlasUnderwaterRole::UnderwaterOnly
                    ? 1 : (selectedFamily->underwaterRole == ETerrainAtlasUnderwaterRole::Mixed ? 4 : 0)));
        m_familyClass->setCurrentIndex(classification);
        m_familyClass->setEnabled(true);
        m_confirmFamily->setEnabled(selectedFamily->ambiguous);
        m_familyReason->setText(selectedFamily->ambiguous
            ? tr("Confirmation needed: the parser could not prove this material's identity or classification. Check its name, section, thumbnail, and assigned tiles, then confirm once.")
            : tr("This material is resolved. Moving or editing one of its tiles updates all dependent membership automatically."));
    }
    else
    {
        m_familyClass->setEnabled(false);
        m_confirmFamily->setEnabled(false);
        m_familyReason->setText(tr("Select a material to rename, classify, confirm, or choose its thumbnail."));
    }
    m_rebuilding = false;

    const STerrainAtlasProfileSlot* selected = slot(m_selectedSlot);
    if(!selected)
    {
        const int thumbnail = selectedFamily ? familyThumbnailSlot(selectedFamily->id) : -1;
        m_tileTitle->setText(selectedFamily
            ? tr("%1 — palette thumbnail").arg(selectedFamily->name)
            : tr("Select a tile"));
        if(thumbnail >= 0 && thumbnail < m_tileImages.size())
            m_preview->setPixmap(QPixmap::fromImage(
                m_tileImages.at(thumbnail)).scaled(
                    256, 256, Qt::KeepAspectRatio, Qt::FastTransformation));
        else
        {
            m_preview->setPixmap(QPixmap());
            m_preview->setText(tr("Tile preview"));
        }
        m_tileHelp->setText(tr("A transition tile may reference two, three, or four materials. It appears under every material it references, but counts once in atlas coverage."));
        m_thumbnail->setEnabled(false);
        for(CFamilyCornerButton* corner : m_corners)
        {
            corner->setFamily(tr("—"));
            corner->setEnabled(false);
        }
        return;
    }
    m_tileTitle->setText(tr("Tile %1%2").arg(selected->slot)
        .arg(selected->assigned ? QString() : tr(" — unassigned")));
    if(selected->slot >= 0 && selected->slot < m_tileImages.size())
        m_preview->setPixmap(QPixmap::fromImage(
            m_tileImages.at(selected->slot)).scaled(
            256, 256, Qt::KeepAspectRatio, Qt::FastTransformation));
    else
        m_preview->setText(tr("Preview unavailable"));
    const qint32 ids[] = {selected->signature.northWest, selected->signature.northEast,
                          selected->signature.southEast, selected->signature.southWest};
    for(int corner = 0; corner < 4; ++corner)
    {
        m_corners[corner]->setFamily(familyName(ids[corner]));
        m_corners[corner]->setEnabled(selected->assigned);
    }
    m_tileHelp->setText(selected->assigned
        ? tr("Drag a material row onto a corner. Ground/underwater and overlay materials can never be mixed in one tile.")
        : tr("This proposal is not used by painting. First drag the tile onto a material to choose its layer and base assignment; then refine individual corners."));
    bool containsSelected = selectedFamily && selected->assigned;
    bool foundSelected = false;
    for(qint32 id : ids) foundSelected |= id == familyId;
    m_thumbnail->setEnabled(containsSelected && foundSelected);
}

void CTerrainAtlasReviewDialog::assignTile(int tileId, qint32 sourceFamily,
                                           qint32 targetFamily)
{
    CValidationReport report;
    const bool okay = sourceFamily > 0
        ? m_profile.replaceSlotFamily(tileId, sourceFamily, targetFamily, &report)
        : m_profile.assignSlotToFamily(tileId, targetFamily, &report);
    if(!okay)
    {
        showReport(tr("Tile was not moved"), report);
        return;
    }
    rebuildVisuals(targetFamily, tileId);
}

void CTerrainAtlasReviewDialog::unassignTile(int tileId)
{
    CValidationReport report;
    if(!m_profile.unassignSlot(tileId, &report))
    {
        showReport(tr("Tile was not unassigned"), report);
        return;
    }
    rebuildVisuals(-1, tileId);
}

void CTerrainAtlasReviewDialog::assignCorner(int corner, qint32 familyId)
{
    if(m_selectedSlot < 0) return;
    CValidationReport report;
    if(!m_profile.setSlotCornerFamily(m_selectedSlot, corner, familyId, &report))
    {
        showReport(tr("Corner assignment is not valid"), report);
        return;
    }
    rebuildVisuals(familyId, m_selectedSlot);
}

void CTerrainAtlasReviewDialog::chooseCornerMaterial(int corner)
{
    const STerrainAtlasProfileSlot* selected = slot(m_selectedSlot);
    if(!selected || !selected->assigned || corner < 0 || corner >= 4) return;
    const qint32 cornerIds[] = {selected->signature.northWest,
                                selected->signature.northEast,
                                selected->signature.southEast,
                                selected->signature.southWest};
    const STerrainAtlasProfileFamily* current = family(cornerIds[corner]);
    if(!current) return;
    QStringList names;
    QVector<qint32> ids;
    int currentIndex = 0;
    for(const STerrainAtlasProfileFamily& candidate : m_profile.families)
        if((candidate.landAllowed && current->landAllowed)
           || (candidate.overlayAllowed && current->overlayAllowed))
        {
            if(candidate.id == current->id) currentIndex = names.size();
            names.append(candidate.name);
            ids.append(candidate.id);
        }
    bool okay = false;
    const QString chosen = QInputDialog::getItem(this, tr("Assign tile corner"),
        tr("Material"), names, currentIndex, false, &okay);
    const int index = names.indexOf(chosen);
    if(okay && index >= 0) assignCorner(corner, ids.at(index));
}

void CTerrainAtlasReviewDialog::renameSelectedFamily()
{
    STerrainAtlasProfileFamily* selected = family(selectedFamilyId());
    if(!selected) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename material"),
        tr("Material name (also shown in the paint palette)"), QLineEdit::Normal,
        selected->name, &ok).trimmed();
    if(!ok || name.isEmpty()) return;
    const qint32 id = selected->id;
    selected->name = name;
    selected->ambiguous = false;
    m_profile.approved = false;
    rebuildVisuals(id);
}

void CTerrainAtlasReviewDialog::createMaterial()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New material"),
        tr("Material name"), QLineEdit::Normal, tr("New material"), &ok).trimmed();
    if(!ok || name.isEmpty()) return;
    qint32 id = 1;
    for(const STerrainAtlasProfileFamily& entry : m_profile.families)
        id = qMax(id, entry.id + 1);
    STerrainAtlasProfileFamily created;
    created.id = id;
    created.name = name;
    created.layer = ETerrainAtlasLayer::Land;
    created.ambiguous = true;
    m_profile.families.append(created);
    m_profile.approved = false;
    rebuildVisuals(id);
}

void CTerrainAtlasReviewDialog::deleteSelectedFamily()
{
    const qint32 id = selectedFamilyId();
    if(id < 0) return;
    for(const STerrainAtlasProfileSlot& definition : m_profile.slotDefinitions)
    {
        const qint32 ids[] = {definition.signature.northWest, definition.signature.northEast,
                              definition.signature.southEast, definition.signature.southWest};
        for(qint32 referenced : ids)
            if(referenced == id)
            {
                QMessageBox::information(this, tr("Material is still referenced"),
                    tr("Move or resolve every tile that references this material before deleting it."));
                return;
            }
    }
    for(int index = 0; index < m_profile.families.size(); ++index)
        if(m_profile.families.at(index).id == id)
        {
            m_profile.families.removeAt(index);
            m_profile.approved = false;
            break;
        }
    rebuildVisuals();
}

void CTerrainAtlasReviewDialog::changeSelectedClassification(int index)
{
    if(m_rebuilding) return;
    STerrainAtlasProfileFamily* selected = family(selectedFamilyId());
    if(!selected) return;
    STerrainAtlasProfile candidate = m_profile;
    STerrainAtlasProfileFamily* changed = nullptr;
    for(STerrainAtlasProfileFamily& entry : candidate.families)
        if(entry.id == selected->id) changed = &entry;
    if(!changed) return;
    changed->landAllowed = index != 2;
    changed->overlayAllowed = index == 2 || index == 3;
    changed->layer = !changed->landAllowed && changed->overlayAllowed
        ? ETerrainAtlasLayer::Overlay : ETerrainAtlasLayer::Land;
    changed->underwaterRole = index == 1
        ? ETerrainAtlasUnderwaterRole::UnderwaterOnly
        : (index == 4 ? ETerrainAtlasUnderwaterRole::Mixed
                      : ETerrainAtlasUnderwaterRole::Surface);
    changed->underwater = changed->underwaterRole
        == ETerrainAtlasUnderwaterRole::UnderwaterOnly;
    CValidationReport report;
    const STerrainAtlasAnalysis analysis = candidate.usableAnalysis(&report);
    if(!analysis.usable && candidate.assignedSlotCount() > 0)
    {
        showReport(tr("Material section was not changed"), report);
        m_rebuilding = true;
        m_familyClass->setCurrentIndex(selected->landAllowed && selected->overlayAllowed
            ? 3 : (!selected->landAllowed && selected->overlayAllowed
                ? 2 : (selected->underwaterRole == ETerrainAtlasUnderwaterRole::UnderwaterOnly
                    ? 1 : (selected->underwaterRole == ETerrainAtlasUnderwaterRole::Mixed ? 4 : 0))));
        m_rebuilding = false;
        return;
    }
    selected->layer = changed->layer;
    selected->underwater = changed->underwater;
    selected->landAllowed = changed->landAllowed;
    selected->overlayAllowed = changed->overlayAllowed;
    selected->underwaterRole = changed->underwaterRole;
    selected->ambiguous = false;
    m_profile.approved = false;
    rebuildVisuals(selected->id);
}

void CTerrainAtlasReviewDialog::confirmSelectedFamily()
{
    STerrainAtlasProfileFamily* selected = family(selectedFamilyId());
    if(!selected) return;
    const qint32 id = selected->id;
    selected->ambiguous = false;
    m_profile.approved = false;
    rebuildVisuals(id);
}

void CTerrainAtlasReviewDialog::useSelectedTileAsThumbnail()
{
    const qint32 familyId = selectedFamilyId();
    CValidationReport report;
    if(!m_profile.setFamilyThumbnail(familyId, m_selectedSlot, &report))
    {
        showReport(tr("Thumbnail was not changed"), report);
        return;
    }
    rebuildVisuals(familyId, m_selectedSlot);
}

void CTerrainAtlasReviewDialog::updateDiagnostics()
{
    int unresolvedMaterials = 0;
    QSet<qint32> activeFamilies;
    for(const STerrainAtlasProfileSlot& definition : m_profile.slotDefinitions)
        if(definition.assigned && !definition.ambiguous)
        {
            activeFamilies.insert(definition.signature.northWest);
            activeFamilies.insert(definition.signature.northEast);
            activeFamilies.insert(definition.signature.southEast);
            activeFamilies.insert(definition.signature.southWest);
        }
    for(const STerrainAtlasProfileFamily& entry : m_profile.families)
        unresolvedMaterials += entry.ambiguous && activeFamilies.contains(entry.id);
    const int assigned = m_profile.assignedSlotCount();
    m_diagnostics->setText(tr(
        "%1 / %2 atlas slots assigned (%3 unassigned). %4 active material confirmation(s) remain. "
        "Unassigned tiles are a debug warning, not a painting blocker; they will use hard-edge fallback when no valid transition exists.")
        .arg(assigned).arg(m_profile.atlasSlotCount)
        .arg(qMax(0, m_profile.atlasSlotCount - assigned))
        .arg(unresolvedMaterials));
}

void CTerrainAtlasReviewDialog::applyAndClose()
{
    // Approval is derived metadata, not a second user workflow. The profile is
    // always saveable; incomplete/unusual slots remain visibly unassigned.
    // Applying also promotes the profile above auto-inference provenance so a
    // later parser revision never discards explicit names/corner corrections.
    m_profile.provenance = QStringLiteral("user-authored-custom-profile");
    m_profile.approved = true;
    CValidationReport report;
    if(!m_profile.approvedAnalysis(&report).usable)
        m_profile.approved = false;
    accept();
}

void CTerrainAtlasReviewDialog::exportCopy()
{
    const QString suggested = QStringLiteral("%1.semantic-atlas.json")
        .arg(m_profile.mprNamespace.toLower());
    const QString path = QFileDialog::getSaveFileName(this,
        tr("Save material atlas profile copy"), suggested,
        tr("Material atlas profiles (*.semantic-atlas.json);;JSON files (*.json)"));
    if(path.isEmpty()) return;
    CValidationReport report;
    if(!m_profile.save(path, &report))
    {
        showReport(tr("Could not save material atlas profile"), report);
        return;
    }
    QMessageBox::information(this, tr("Profile copy saved"),
        tr("Saved to:\n%1\n\nUse Load profile in this window to reopen it.")
            .arg(QDir::toNativeSeparators(path)));
}

void CTerrainAtlasReviewDialog::openProfile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load material atlas profile"),
        QString(), tr("Material atlas profiles (*.semantic-atlas.json *.json)"));
    if(path.isEmpty()) return;
    STerrainAtlasProfile loaded;
    CValidationReport report;
    if(!STerrainAtlasProfile::load(path, &loaded, &report)
       || !loaded.validateBinding(m_profile.mprNamespace, m_profile.atlasPixelSha256,
                                  m_profile.atlasSlotCount, &report))
    {
        showReport(tr("Profile does not match this atlas"), report);
        return;
    }
    m_profile = loaded;
    m_selectedSlot = -1;
    rebuildVisuals();
}

void CTerrainAtlasReviewDialog::showReport(const QString& title,
                                           const CValidationReport& report)
{
    QMessageBox::warning(this, title, report.toText());
}
