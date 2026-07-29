#include "terrain_atlas_review_dialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFrame>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
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
      m_diagnostics(new QLabel(this)), m_approve(new QPushButton(tr("Approve and save"), this)),
      m_showAll(new QCheckBox(tr("Show all inferred families and slots"), this))
{
    setWindowTitle(tr("Review semantic terrain atlas"));
    resize(1240, 760);
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* explanation = new QLabel(tr(
        "The editor has already assigned the corner families it can infer. This view shows only unresolved items by default. "
        "Confirm a highlighted family or slot when it is correct; split/merge only when the proposed material identity is wrong. "
        "Use Show all for expert inspection. No atlas image or MPR data is modified."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    m_binding->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_binding->setText(tr("Namespace: %1   Slots: %2   Decoded atlas SHA-256: %3")
        .arg(m_profile.mprNamespace).arg(m_profile.atlasSlotCount)
        .arg(QString::fromLatin1(m_profile.atlasPixelSha256)));
    layout->addWidget(m_binding);
    QLabel* storage = new QLabel(tr("Editor profile location: %1")
        .arg(QDir::toNativeSeparators(
            CTerrainAtlasProfileStore::profilePath(m_profile.mprNamespace))), this);
    storage->setTextInteractionFlags(Qt::TextSelectableByMouse);
    storage->setWordWrap(true);
    layout->addWidget(storage);

    QHBoxLayout* body = new QHBoxLayout();
    QVBoxLayout* familySide = new QVBoxLayout();
    familySide->addWidget(new QLabel(tr("Material families"), this));
    familySide->addWidget(m_showAll);
    m_families->setColumnCount(6);
    m_families->setHorizontalHeaderLabels({tr("ID"), tr("Name"), tr("Layer"),
        tr("Underwater"), tr("Needs review"), tr("Slots")});
    m_families->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_families->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_families->verticalHeader()->setVisible(false);
    m_families->horizontalHeader()->setMinimumSectionSize(55);
    for(int column = 0; column < m_families->columnCount(); ++column)
        m_families->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    const int familyWidths[] = {55, 300, 120, 105, 115, 65};
    for(int column = 0; column < m_families->columnCount(); ++column)
        m_families->setColumnWidth(column, familyWidths[column]);
    familySide->addWidget(m_families, 2);
    QHBoxLayout* familyActions = new QHBoxLayout();
    QPushButton* split = new QPushButton(tr("Split selected slots..."), this);
    QPushButton* merge = new QPushButton(tr("Merge selected families..."), this);
    familyActions->addWidget(split);
    familyActions->addWidget(merge);
    familySide->addLayout(familyActions);
    body->addLayout(familySide, 3);

    QVBoxLayout* slotSide = new QVBoxLayout();
    slotSide->addWidget(new QLabel(tr("Atlas slots in selected family"), this));
    m_slots->setColumnCount(3);
    m_slots->setHorizontalHeaderLabels({tr("Slot"), tr("Corner families (NW / NE / SE / SW)"),
                                        tr("Status")});
    m_slots->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_slots->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_slots->verticalHeader()->setVisible(false);
    m_slots->horizontalHeader()->setMinimumSectionSize(60);
    for(int column = 0; column < m_slots->columnCount(); ++column)
        m_slots->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
    m_slots->setColumnWidth(0, 70);
    m_slots->setColumnWidth(1, 310);
    m_slots->setColumnWidth(2, 135);
    slotSide->addWidget(m_slots, 2);
    QHBoxLayout* slotActions = new QHBoxLayout();
    QPushButton* reviewed = new QPushButton(tr("Mark selected Reviewed"), this);
    QPushButton* needsReview = new QPushButton(tr("Mark selected Needs review"), this);
    slotActions->addWidget(reviewed);
    slotActions->addWidget(needsReview);
    slotSide->addLayout(slotActions);
    m_preview->setFixedSize(220, 220);
    m_preview->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    slotSide->addWidget(m_preview);
    body->addLayout(slotSide, 2);
    layout->addLayout(body, 1);

    m_diagnostics->setWordWrap(true);
    layout->addWidget(m_diagnostics);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton* draft = buttons->addButton(tr("Save draft"), QDialogButtonBox::ActionRole);
    QPushButton* exportButton = buttons->addButton(
        tr("Export profile copy..."), QDialogButtonBox::ActionRole);
    buttons->addButton(m_approve, QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(draft, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::saveDraft);
    connect(exportButton, &QPushButton::clicked,
            this, &CTerrainAtlasReviewDialog::exportCopy);
    connect(m_approve, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::approveAndSave);
    connect(split, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::splitSelectedFamily);
    connect(merge, &QPushButton::clicked, this, &CTerrainAtlasReviewDialog::mergeSelectedFamilies);
    connect(reviewed, &QPushButton::clicked, this,
            &CTerrainAtlasReviewDialog::markSelectedSlotsReviewed);
    connect(needsReview, &QPushButton::clicked, this,
            &CTerrainAtlasReviewDialog::markSelectedSlotsForReview);
    connect(m_showAll, &QCheckBox::toggled, this, [this](bool) {
        rebuildTables(selectedFamilyId());
    });
    connect(m_families, &QTableWidget::itemSelectionChanged,
            this, &CTerrainAtlasReviewDialog::refreshSlots);
    connect(m_families, &QTableWidget::cellChanged,
            this, &CTerrainAtlasReviewDialog::applyFamilyCellChange);
    connect(m_slots, &QTableWidget::itemSelectionChanged,
            this, &CTerrainAtlasReviewDialog::updatePreview);
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("terrainAtlasReview/geometry")).toByteArray());
    m_families->horizontalHeader()->restoreState(
        settings.value(QStringLiteral("terrainAtlasReview/familyHeader")).toByteArray());
    m_slots->horizontalHeader()->restoreState(
        settings.value(QStringLiteral("terrainAtlasReview/slotHeader")).toByteArray());
    connect(this, &QDialog::finished, this, [this](int) {
        QSettings settings;
        settings.setValue(QStringLiteral("terrainAtlasReview/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("terrainAtlasReview/familyHeader"),
                          m_families->horizontalHeader()->saveState());
        settings.setValue(QStringLiteral("terrainAtlasReview/slotHeader"),
                          m_slots->horizontalHeader()->saveState());
    });
    if(m_profile.provenance.startsWith(QStringLiteral("authored-template:")))
        m_showAll->setChecked(true);
    rebuildTables();
}

void CTerrainAtlasReviewDialog::rebuildTables(qint32 selectFamilyId)
{
    m_rebuilding = true;
    QVector<int> visibleFamilies;
    for(int index = 0; index < m_profile.families.size(); ++index)
    {
        const STerrainAtlasProfileFamily& family = m_profile.families.at(index);
        bool unresolved = family.ambiguous;
        for(const STerrainAtlasProfileSlot& slot : m_profile.slotDefinitions)
        {
            if(!slot.ambiguous)
                continue;
            const qint32 ids[] = {slot.signature.northWest, slot.signature.northEast,
                                  slot.signature.southEast, slot.signature.southWest};
            unresolved |= std::find(std::begin(ids), std::end(ids), family.id)
                != std::end(ids);
        }
        if(m_showAll->isChecked() || unresolved)
            visibleFamilies.append(index);
    }
    m_families->setRowCount(visibleFamilies.size());
    int selectedRow = 0;
    for(int row = 0; row < visibleFamilies.size(); ++row)
    {
        const STerrainAtlasProfileFamily& family =
            m_profile.families.at(visibleFamilies.at(row));
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
    if(!visibleFamilies.isEmpty()) m_families->selectRow(selectedRow);
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
        if((m_showAll->isChecked() || slot.ambiguous)
           && std::find(std::begin(ids), std::end(ids), familyId) != std::end(ids))
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
        QSize(216, 216), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void CTerrainAtlasReviewDialog::applyFamilyCellChange(int row, int column)
{
    if(m_rebuilding || row < 0 || !m_families->item(row, 0)) return;
    const qint32 familyId = m_families->item(row, 0)->data(Qt::UserRole).toInt();
    int familyIndex = -1;
    for(int index = 0; index < m_profile.families.size(); ++index)
        if(m_profile.families.at(index).id == familyId)
        {
            familyIndex = index;
            break;
        }
    if(familyIndex < 0) return;
    STerrainAtlasProfileFamily& family = m_profile.families[familyIndex];
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

void CTerrainAtlasReviewDialog::saveDraft()
{
    m_profile.approved = false;
    accept();
}

void CTerrainAtlasReviewDialog::exportCopy()
{
    STerrainAtlasProfile exported = m_profile;
    exported.approved = false;
    const QString suggested = QStringLiteral("%1.semantic-atlas.json")
        .arg(exported.mprNamespace.toLower());
    const QString path = QFileDialog::getSaveFileName(this,
        tr("Export semantic atlas profile copy"), suggested,
        tr("Semantic atlas profiles (*.semantic-atlas.json);;JSON files (*.json)"));
    if(path.isEmpty())
        return;
    CValidationReport report;
    if(!exported.save(path, &report))
    {
        showReport(tr("Could not export semantic atlas profile"), report);
        return;
    }
    QMessageBox::information(this, tr("Semantic atlas profile exported"),
        tr("A draft copy was exported to:\n%1")
            .arg(QDir::toNativeSeparators(path)));
}

void CTerrainAtlasReviewDialog::approveAndSave()
{
    for(const STerrainAtlasProfileFamily& family : m_profile.families)
        if(family.ambiguous)
        {
            rebuildTables(family.id);
            QMessageBox::warning(this, tr("Atlas review incomplete"),
                tr("Material family '%1' still has Needs review checked. Inspect its name, layer, underwater classification, and member slots; then clear that family flag if the assignment is correct.")
                    .arg(family.name));
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
