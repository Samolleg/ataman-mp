#include "terrain_atlas_review_dialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "endurance_document.h"

namespace {
QString signatureText(const STerrainTransitionSignature& signature)
{
    return QStringLiteral("%1 / %2 / %3 / %4")
        .arg(signature.northWest).arg(signature.northEast)
        .arg(signature.southEast).arg(signature.southWest);
}
}

CTerrainAtlasReviewDialog::CTerrainAtlasReviewDialog(
    const STerrainAtlasProfile& profile, const QVector<QImage>& tileImages,
    QWidget* parent)
    : QDialog(parent), m_profile(profile), m_tileImages(tileImages),
      m_families(new QTableWidget(this)), m_slots(new QTableWidget(this)),
      m_preview(new QLabel(this)), m_binding(new QLabel(this)),
      m_diagnostics(new QLabel(this)), m_approve(new QPushButton(tr("Approve and save"), this))
{
    setWindowTitle(tr("Review semantic terrain atlas"));
    resize(980, 680);
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* explanation = new QLabel(tr(
        "1. Review each material family name and Land/Overlay classification. "
        "2. Select a family, inspect its slot previews and corner labels, and mark correct slots Reviewed. "
        "3. Split only when selected slots belong to a different visual material; merge only when two families are the same material. "
        "4. Clear every justified Needs review flag, then approve. No atlas image or MPR data is modified."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    m_binding->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_binding->setText(tr("Namespace: %1   Slots: %2   Decoded atlas SHA-256: %3")
        .arg(m_profile.mprNamespace).arg(m_profile.atlasSlotCount)
        .arg(QString::fromLatin1(m_profile.atlasPixelSha256)));
    layout->addWidget(m_binding);

    QHBoxLayout* body = new QHBoxLayout();
    QVBoxLayout* familySide = new QVBoxLayout();
    familySide->addWidget(new QLabel(tr("Material families"), this));
    m_families->setColumnCount(6);
    m_families->setHorizontalHeaderLabels({tr("ID"), tr("Name"), tr("Layer"),
        tr("Underwater"), tr("Needs review"), tr("Slots")});
    m_families->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_families->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_families->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    familySide->addWidget(m_families, 2);
    QHBoxLayout* familyActions = new QHBoxLayout();
    QPushButton* split = new QPushButton(tr("Split selected slots..."), this);
    QPushButton* merge = new QPushButton(tr("Merge selected families..."), this);
    QPushButton* guide = new QPushButton(tr("How to review..."), this);
    familyActions->addWidget(split);
    familyActions->addWidget(merge);
    familyActions->addWidget(guide);
    familySide->addLayout(familyActions);
    body->addLayout(familySide, 3);

    QVBoxLayout* slotSide = new QVBoxLayout();
    slotSide->addWidget(new QLabel(tr("Atlas slots in selected family"), this));
    m_slots->setColumnCount(3);
    m_slots->setHorizontalHeaderLabels({tr("Slot"), tr("Corner families (NW / NE / SE / SW)"),
                                        tr("Status")});
    m_slots->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_slots->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_slots->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    slotSide->addWidget(m_slots, 2);
    QHBoxLayout* slotActions = new QHBoxLayout();
    QPushButton* reviewed = new QPushButton(tr("Mark selected Reviewed"), this);
    QPushButton* needsReview = new QPushButton(tr("Mark selected Needs review"), this);
    slotActions->addWidget(reviewed);
    slotActions->addWidget(needsReview);
    slotSide->addLayout(slotActions);
    m_preview->setMinimumSize(180, 180);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    slotSide->addWidget(m_preview);
    body->addLayout(slotSide, 2);
    layout->addLayout(body, 1);

    m_diagnostics->setWordWrap(true);
    layout->addWidget(m_diagnostics);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton* draft = buttons->addButton(tr("Save draft"), QDialogButtonBox::ActionRole);
    buttons->addButton(m_approve, QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(draft, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::saveDraft);
    connect(m_approve, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::approveAndSave);
    connect(split, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::splitSelectedFamily);
    connect(merge, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::mergeSelectedFamilies);
    connect(reviewed, &QPushButton::clicked, this,
            &CTerrainAtlasReviewDialog::markSelectedSlotsReviewed);
    connect(needsReview, &QPushButton::clicked, this,
            &CTerrainAtlasReviewDialog::markSelectedSlotsForReview);
    connect(guide, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::showGuide);
    connect(m_families, &QTableWidget::itemSelectionChanged,
            this, &CTerrainAtlasReviewDialog::refreshSlots);
    connect(m_families, &QTableWidget::cellChanged,
            this, &CTerrainAtlasReviewDialog::applyFamilyCellChange);
    connect(m_slots, &QTableWidget::itemSelectionChanged,
            this, &CTerrainAtlasReviewDialog::updatePreview);
    rebuildTables();
}

void CTerrainAtlasReviewDialog::rebuildTables(qint32 selectFamilyId)
{
    m_rebuilding = true;
    m_families->setRowCount(m_profile.families.size());
    int selectedRow = 0;
    for(int row = 0; row < m_profile.families.size(); ++row)
    {
        const STerrainAtlasProfileFamily& family = m_profile.families.at(row);
        QTableWidgetItem* id = new QTableWidgetItem(QString::number(family.id));
        id->setData(Qt::UserRole, family.id);
        id->setFlags(id->flags() & ~Qt::ItemIsEditable);
        m_families->setItem(row, 0, id);
        m_families->setItem(row, 1, new QTableWidgetItem(family.name));
        QComboBox* layer = new QComboBox(m_families);
        layer->addItem(tr("Land"), int(ETerrainAtlasLayer::Land));
        layer->addItem(tr("Overlay"), int(ETerrainAtlasLayer::Overlay));
        layer->setCurrentIndex(family.layer == ETerrainAtlasLayer::Land ? 0 : 1);
        m_families->setCellWidget(row, 2, layer);
        QCheckBox* underwater = new QCheckBox(m_families);
        underwater->setChecked(family.underwater);
        underwater->setEnabled(family.layer == ETerrainAtlasLayer::Land);
        m_families->setCellWidget(row, 3, underwater);
        QCheckBox* ambiguous = new QCheckBox(m_families);
        ambiguous->setChecked(family.ambiguous);
        m_families->setCellWidget(row, 4, ambiguous);
        QTableWidgetItem* count = new QTableWidgetItem(QString::number(family.memberSlots.size()));
        count->setFlags(count->flags() & ~Qt::ItemIsEditable);
        m_families->setItem(row, 5, count);
        connect(layer, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, row](int) { applyFamilyCellChange(row, 2); });
        connect(underwater, &QCheckBox::toggled, this,
                [this, row](bool) { applyFamilyCellChange(row, 3); });
        connect(ambiguous, &QCheckBox::toggled, this,
                [this, row](bool) { applyFamilyCellChange(row, 4); });
        if(family.id == selectFamilyId) selectedRow = row;
    }
    m_rebuilding = false;
    if(!m_profile.families.isEmpty()) m_families->selectRow(selectedRow);
    refreshSlots();
    updateDiagnostics();
}

void CTerrainAtlasReviewDialog::updateDiagnostics()
{
    int ambiguousFamilies = 0, ambiguousSlots = 0, missingTransitions = 0;
    for(const STerrainAtlasProfileFamily& family : m_profile.families)
        ambiguousFamilies += family.ambiguous;
    for(const STerrainAtlasProfileSlot& slot : m_profile.slotDefinitions)
        ambiguousSlots += slot.ambiguous;
    for(int a = 0; a < m_profile.families.size(); ++a)
        for(int b = a + 1; b < m_profile.families.size(); ++b)
        {
            const auto& first = m_profile.families.at(a);
            const auto& second = m_profile.families.at(b);
            if(first.layer != second.layer) continue;
            bool transition = false;
            for(const STerrainAtlasProfileSlot& slot : m_profile.slotDefinitions)
            {
                const qint32 ids[] = {slot.signature.northWest, slot.signature.northEast,
                                      slot.signature.southEast, slot.signature.southWest};
                bool hasFirst = false, hasSecond = false;
                for(qint32 id : ids) { hasFirst |= id == first.id; hasSecond |= id == second.id; }
                transition |= hasFirst && hasSecond;
            }
            missingTransitions += !transition;
        }
    m_diagnostics->setText(tr(
        "%1 family ambiguity flag(s), %2 slot ambiguity flag(s), %3 missing same-layer family transition(s). "
        "Missing transitions are diagnostic: automatic painting keeps a hard edge instead of inventing a tile.")
        .arg(ambiguousFamilies).arg(ambiguousSlots).arg(missingTransitions));
}

qint32 CTerrainAtlasReviewDialog::selectedFamilyId() const
{
    const int row = m_families->currentRow();
    return row >= 0 && m_families->item(row, 0)
        ? m_families->item(row, 0)->data(Qt::UserRole).toInt() : -1;
}

QVector<qint32> CTerrainAtlasReviewDialog::selectedFamilyIds() const
{
    QSet<int> rows;
    for(const QModelIndex& index : m_families->selectionModel()->selectedRows()) rows.insert(index.row());
    QVector<qint32> result;
    for(int row : rows) result.append(m_families->item(row, 0)->data(Qt::UserRole).toInt());
    std::sort(result.begin(), result.end());
    return result;
}

QVector<int> CTerrainAtlasReviewDialog::selectedSlotIds() const
{
    QVector<int> result;
    for(const QModelIndex& index : m_slots->selectionModel()->selectedRows())
        result.append(m_slots->item(index.row(), 0)->data(Qt::UserRole).toInt());
    std::sort(result.begin(), result.end());
    return result;
}

void CTerrainAtlasReviewDialog::refreshSlots()
{
    const qint32 familyId = selectedFamilyId();
    QVector<const STerrainAtlasProfileSlot*> matching;
    for(const STerrainAtlasProfileSlot& slot : m_profile.slotDefinitions)
    {
        const qint32 ids[] = {slot.signature.northWest, slot.signature.northEast,
                              slot.signature.southEast, slot.signature.southWest};
        if(std::find(std::begin(ids), std::end(ids), familyId) != std::end(ids))
            matching.append(&slot);
    }
    m_slots->setRowCount(matching.size());
    for(int row = 0; row < matching.size(); ++row)
    {
        const STerrainAtlasProfileSlot& slot = *matching.at(row);
        QTableWidgetItem* id = new QTableWidgetItem(QString::number(slot.slot));
        id->setData(Qt::UserRole, slot.slot);
        id->setFlags(id->flags() & ~Qt::ItemIsEditable);
        m_slots->setItem(row, 0, id);
        QTableWidgetItem* signature = new QTableWidgetItem(signatureText(slot.signature));
        signature->setFlags(signature->flags() & ~Qt::ItemIsEditable);
        m_slots->setItem(row, 1, signature);
        QCheckBox* status = new QCheckBox(tr("Needs review"), m_slots);
        status->setChecked(slot.ambiguous);
        status->setProperty("atlasSlot", slot.slot);
        m_slots->setCellWidget(row, 2, status);
        connect(status, &QCheckBox::toggled, this, [this, status](bool checked) {
            const int slotId = status->property("atlasSlot").toInt();
            for(STerrainAtlasProfileSlot& candidate : m_profile.slotDefinitions)
                if(candidate.slot == slotId)
                {
                    candidate.ambiguous = checked;
                    m_profile.approved = false;
                    updateDiagnostics();
                    break;
                }
        });
    }
    updatePreview();
}

void CTerrainAtlasReviewDialog::updatePreview()
{
    const QVector<int> selected = selectedSlotIds();
    if(selected.isEmpty() || selected.front() < 0 || selected.front() >= m_tileImages.size())
    {
        m_preview->setText(tr("Select an atlas slot to preview it."));
        m_preview->setPixmap(QPixmap());
        return;
    }
    m_preview->setPixmap(QPixmap::fromImage(m_tileImages.at(selected.front())).scaled(
        m_preview->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void CTerrainAtlasReviewDialog::applyFamilyCellChange(int row, int column)
{
    if(m_rebuilding || row < 0 || row >= m_profile.families.size()) return;
    STerrainAtlasProfileFamily& family = m_profile.families[row];
    if(column == 1 && m_families->item(row, 1))
        family.name = m_families->item(row, 1)->text().trimmed();
    else if(column == 2)
    {
        QComboBox* layer = qobject_cast<QComboBox*>(m_families->cellWidget(row, 2));
        family.layer = layer && layer->currentData().toInt() == int(ETerrainAtlasLayer::Overlay)
            ? ETerrainAtlasLayer::Overlay : ETerrainAtlasLayer::Land;
        QCheckBox* underwater = qobject_cast<QCheckBox*>(m_families->cellWidget(row, 3));
        if(underwater)
        {
            underwater->setEnabled(family.layer == ETerrainAtlasLayer::Land);
            if(family.layer == ETerrainAtlasLayer::Overlay) underwater->setChecked(false);
        }
    }
    else if(column == 3)
    {
        QCheckBox* underwater = qobject_cast<QCheckBox*>(m_families->cellWidget(row, 3));
        family.underwater = underwater && underwater->isChecked()
            && family.layer == ETerrainAtlasLayer::Land;
    }
    else if(column == 4)
    {
        QCheckBox* ambiguous = qobject_cast<QCheckBox*>(m_families->cellWidget(row, 4));
        family.ambiguous = ambiguous && ambiguous->isChecked();
    }
    m_profile.approved = false;
    updateDiagnostics();
}

void CTerrainAtlasReviewDialog::splitSelectedFamily()
{
    const qint32 familyId = selectedFamilyId();
    const QVector<int> selectedSlots = selectedSlotIds();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Split material family"),
        tr("Name for the new family"), QLineEdit::Normal, tr("Split family"), &ok).trimmed();
    if(!ok) return;
    CValidationReport report;
    if(!m_profile.splitFamily(familyId, selectedSlots, name, &report))
    {
        showReport(tr("Cannot split family"), report);
        return;
    }
    rebuildTables(familyId);
}

void CTerrainAtlasReviewDialog::mergeSelectedFamilies()
{
    const QVector<qint32> ids = selectedFamilyIds();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Merge material families"),
        tr("Name for the merged family"), QLineEdit::Normal, tr("Merged family"), &ok).trimmed();
    if(!ok) return;
    CValidationReport report;
    if(!m_profile.mergeFamilies(ids, name, &report))
    {
        showReport(tr("Cannot merge families"), report);
        return;
    }
    rebuildTables(*std::min_element(ids.constBegin(), ids.constEnd()));
}

void CTerrainAtlasReviewDialog::markSelectedSlotsReviewed()
{
    const QVector<int> selectedSlots = selectedSlotIds();
    if(selectedSlots.isEmpty())
    {
        QMessageBox::information(this, tr("Review atlas slots"),
            tr("Select one or more slot rows first."));
        return;
    }
    for(STerrainAtlasProfileSlot& slot : m_profile.slotDefinitions)
        if(selectedSlots.contains(slot.slot))
            slot.ambiguous = false;
    m_profile.approved = false;
    refreshSlots();
    updateDiagnostics();
}

void CTerrainAtlasReviewDialog::markSelectedSlotsForReview()
{
    const QVector<int> selectedSlots = selectedSlotIds();
    if(selectedSlots.isEmpty())
    {
        QMessageBox::information(this, tr("Review atlas slots"),
            tr("Select one or more slot rows first."));
        return;
    }
    for(STerrainAtlasProfileSlot& slot : m_profile.slotDefinitions)
        if(selectedSlots.contains(slot.slot))
            slot.ambiguous = true;
    m_profile.approved = false;
    refreshSlots();
    updateDiagnostics();
}

void CTerrainAtlasReviewDialog::showGuide()
{
    QMessageBox::information(this, tr("Semantic atlas review guide"), tr(
        "The editor is converting atlas pictures into named paint materials; it is not generating or editing the atlas.\n\n"
        "For each family:\n"
        "- Give it a recognizable name such as Dark grass, Limestone, Water, or Swamp.\n"
        "- Choose Land or Overlay. Underwater is only for land textures normally hidden below overlays.\n"
        "- Inspect the listed slot pictures and their NW / NE / SE / SW corner-family labels.\n"
        "- If a slot and its corner labels are correct, select it and click Mark selected Reviewed.\n"
        "- If selected slots are visually a different material, split those slots into a new family.\n"
        "- If two proposed families are actually the same material, select both family rows and merge them.\n"
        "- Unused slots still require a decision because the editor cannot know whether an unused picture is a valid future brush or transition.\n\n"
        "Approval is enabled only after every family and slot ambiguity has been explicitly resolved. Missing transitions are allowed: painting will use a hard edge instead of inventing atlas content."));
}

void CTerrainAtlasReviewDialog::saveDraft()
{
    m_profile.approved = false;
    accept();
}

void CTerrainAtlasReviewDialog::approveAndSave()
{
    for(int row = 0; row < m_profile.families.size(); ++row)
        if(m_profile.families.at(row).ambiguous)
        {
            m_families->selectRow(row);
            QMessageBox::warning(this, tr("Atlas review incomplete"),
                tr("Material family '%1' still has Needs review checked. Inspect its name, layer, underwater classification, and member slots; then clear that family flag if the assignment is correct.")
                    .arg(m_profile.families.at(row).name));
            return;
        }
    for(const STerrainAtlasProfileSlot& slot : m_profile.slotDefinitions)
        if(slot.ambiguous)
        {
            qint32 familyId = slot.signature.northWest;
            rebuildTables(familyId);
            for(int row = 0; row < m_slots->rowCount(); ++row)
                if(m_slots->item(row, 0)
                   && m_slots->item(row, 0)->data(Qt::UserRole).toInt() == slot.slot)
                {
                    m_slots->selectRow(row);
                    break;
                }
            QMessageBox::warning(this, tr("Atlas review incomplete"),
                tr("Slot %1 still needs review. Its row is now selected. Inspect the preview and NW / NE / SE / SW labels. If they are correct, click Mark selected Reviewed. Split or merge only when the proposed family assignment is actually wrong.")
                    .arg(slot.slot));
            return;
        }
    m_profile.approved = true;
    CValidationReport report;
    if(!m_profile.approvedAnalysis(&report).usable)
    {
        m_profile.approved = false;
        showReport(tr("Atlas profile cannot be approved"), report);
        return;
    }
    accept();
}

void CTerrainAtlasReviewDialog::showReport(const QString& title,
                                           const CValidationReport& report)
{
    QMessageBox::warning(this, title, report.toText());
}
