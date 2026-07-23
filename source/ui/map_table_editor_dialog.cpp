#include "map_table_editor_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "res_save_coordinator.h"

namespace {
enum EExitColumn
{
    ExitId, DestinationZone, DestinationExit,
    DeployX1, DeployY1, DeployX2, DeployY2,
    RemoveX1, RemoveY1, RemoveX2, RemoveY2,
    PassTime, ViewAngle, DeployAngle, ExitColumnCount
};

QString number(double value)
{
    return QString::number(value, 'g', 15);
}

QTableWidgetItem* item(const QString& text)
{
    return new QTableWidgetItem(text);
}
}

CMapTableEditorDialog::CMapTableEditorDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Map Catalog Editor (map.txt in texts.res)"));
    resize(1180, 720);
    QVBoxLayout* root = new QVBoxLayout(this);
    QHBoxLayout* archiveRow = new QHBoxLayout;
    m_archiveEdit = new QLineEdit;
    m_archiveEdit->setObjectName("mapArchivePath");
    m_archiveEdit->setReadOnly(true);
    QPushButton* openButton = new QPushButton(tr("Open texts.res Copy..."));
    m_importButton = new QPushButton(tr("Import map.txt..."));
    m_exportButton = new QPushButton(tr("Export map.txt..."));
    m_importButton->setObjectName("mapImportText");
    m_exportButton->setObjectName("mapExportText");
    archiveRow->addWidget(new QLabel(tr("Archive:")));
    archiveRow->addWidget(m_archiveEdit, 1);
    archiveRow->addWidget(openButton);
    archiveRow->addWidget(m_importButton);
    archiveRow->addWidget(m_exportButton);
    root->addLayout(archiveRow);

    QLabel* safety = new QLabel(tr(
        "Edits are staged into map.txt only. Unknown tags and all other RES members are preserved. "
        "Save creates a verified backup before replacing an existing destination."));
    safety->setWordWrap(true);
    root->addWidget(safety);

    QSplitter* splitter = new QSplitter;
    QWidget* left = new QWidget;
    QVBoxLayout* leftLayout = new QVBoxLayout(left);
    m_zoneList = new QListWidget;
    m_zoneList->setObjectName("mapZoneList");
    QPushButton* newButton = new QPushButton(tr("New Registration"));
    m_duplicateButton = new QPushButton(tr("Duplicate Selected Zone"));
    leftLayout->addWidget(new QLabel(tr("Zones")));
    leftLayout->addWidget(m_zoneList, 1);
    leftLayout->addWidget(newButton);
    leftLayout->addWidget(m_duplicateButton);
    splitter->addWidget(left);

    m_tabs = new QTabWidget;
    QWidget* general = new QWidget;
    QFormLayout* generalForm = new QFormLayout(general);
    m_zoneIdEdit = new QLineEdit;
    m_zoneIdEdit->setObjectName("mapZoneId");
    m_allodIdEdit = new QLineEdit;
    m_zoneTypeCombo = new QComboBox;
    m_zoneTypeCombo->addItems(QStringList() << "game" << "brief" << "edge");
    m_mprEdit = new QLineEdit;
    m_mobEdit = new QLineEdit;
    generalForm->addRow(tr("Zone ID"), m_zoneIdEdit);
    generalForm->addRow(tr("Allod / island ID"), m_allodIdEdit);
    generalForm->addRow(tr("Zone type"), m_zoneTypeCombo);
    generalForm->addRow(tr("Landscape MPR resource"), m_mprEdit);
    generalForm->addRow(tr("Objects MOB resource"), m_mobEdit);
    QLabel* generalNote = new QLabel(tr(
        "Resource names are archive namespaces, normally written without .mpr/.mob extensions. "
        "Values are never generated automatically."));
    generalNote->setWordWrap(true);
    generalForm->addRow(generalNote);
    m_applyButton = new QPushButton(tr("Apply Selected Zone"));
    m_registerButton = new QPushButton(tr("Register New Zone"));
    m_registerButton->setVisible(false);
    QWidget* generalActions = new QWidget;
    QHBoxLayout* generalActionLayout = new QHBoxLayout(generalActions);
    generalActionLayout->setContentsMargins(0, 0, 0, 0);
    generalActionLayout->addWidget(m_applyButton);
    generalActionLayout->addWidget(m_registerButton);
    generalActionLayout->addStretch();
    generalForm->addRow(generalActions);
    m_tabs->addTab(general, tr("General"));

    QWidget* maps = new QWidget;
    QFormLayout* mapsForm = new QFormLayout(maps);
    m_mapsEnabled = new QCheckBox(tr("Zone has #maps minimap metadata"));
    m_mapsEnabled->setObjectName("mapMinimapEnabled");
    m_sectorWidth = new QSpinBox;
    m_sectorHeight = new QSpinBox;
    m_sectorWidth->setRange(1, 4096);
    m_sectorHeight->setRange(1, 4096);
    m_minimapEdit = new QLineEdit;
    m_minimapEdit->setObjectName("mapMinimapTexture");
    m_objectivesEdit = new QLineEdit;
    mapsForm->addRow(m_mapsEnabled);
    mapsForm->addRow(tr("Map sectors X"), m_sectorWidth);
    mapsForm->addRow(tr("Map sectors Y"), m_sectorHeight);
    mapsForm->addRow(tr("Minimap texture name"), m_minimapEdit);
    mapsForm->addRow(tr("Objectives texture name"), m_objectivesEdit);
    QLabel* mapsNote = new QLabel(tr(
        "These names refer to textures supplied through the selected resource profile. "
        "This editor changes registration only; it does not silently create or install textures."));
    mapsNote->setWordWrap(true);
    mapsForm->addRow(mapsNote);
    m_tabs->addTab(maps, tr("Minimap"));

    QWidget* entrances = new QWidget;
    QVBoxLayout* entranceLayout = new QVBoxLayout(entrances);
    m_exitTable = new QTableWidget(0, ExitColumnCount);
    m_exitTable->setObjectName("mapEntranceTable");
    m_exitTable->setHorizontalHeaderLabels(QStringList()
        << tr("ID") << tr("Destination") << tr("Dest exit")
        << tr("Deploy X1") << tr("Y1") << tr("X2") << tr("Y2")
        << tr("Remove X1") << tr("Y1") << tr("X2") << tr("Y2")
        << tr("Pass time") << tr("View") << tr("Deploy angle"));
    m_exitTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_exitTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    QHBoxLayout* exitButtons = new QHBoxLayout;
    QPushButton* addExitButton = new QPushButton(tr("Add Entrance"));
    QPushButton* removeExitButton = new QPushButton(tr("Remove Entrance"));
    exitButtons->addWidget(addExitButton);
    exitButtons->addWidget(removeExitButton);
    exitButtons->addStretch();
    entranceLayout->addWidget(new QLabel(tr(
        "Empty optional cells mean the corresponding #deploy, #remove, #passtime, #view, or #deployangle tag is absent.")));
    entranceLayout->addWidget(m_exitTable, 1);
    entranceLayout->addLayout(exitButtons);
    m_tabs->addTab(entrances, tr("Entrances"));

    QWidget* rawTab = new QWidget;
    QVBoxLayout* rawLayout = new QVBoxLayout(rawTab);
    QLabel* rawNote = new QLabel(tr(
        "Exact source span from the selected #zone header to immediately before the next #zone header. "
        "Hand-authored separators and comments between zones may describe either side; Endurance preserves them but does not guess ownership."));
    rawNote->setWordWrap(true);
    m_rawReview = new QPlainTextEdit;
    m_rawReview->setObjectName("mapRawReview");
    m_rawReview->setReadOnly(true);
    m_rawReview->setLineWrapMode(QPlainTextEdit::NoWrap);
    rawLayout->addWidget(rawNote);
    rawLayout->addWidget(m_rawReview, 1);
    m_tabs->addTab(rawTab, tr("Raw Source Span"));
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    QHBoxLayout* actions = new QHBoxLayout;
    m_saveButton = new QPushButton(tr("Save Verified RES As..."));
    m_saveButton->setObjectName("mapSaveVerifiedRes");
    QPushButton* closeButton = new QPushButton(tr("Close"));
    actions->addStretch();
    actions->addWidget(m_saveButton);
    actions->addWidget(closeButton);
    root->addLayout(actions);
    m_statusEdit = new QLineEdit;
    m_statusEdit->setReadOnly(true);
    root->addWidget(m_statusEdit);

    connect(openButton, &QPushButton::clicked, this, &CMapTableEditorDialog::chooseArchive);
    connect(m_importButton, &QPushButton::clicked, this, &CMapTableEditorDialog::importText);
    connect(m_exportButton, &QPushButton::clicked, this, &CMapTableEditorDialog::exportText);
    connect(newButton, &QPushButton::clicked, this, &CMapTableEditorDialog::beginNewZone);
    connect(m_duplicateButton, &QPushButton::clicked, this, &CMapTableEditorDialog::beginDuplicateZone);
    connect(m_zoneList, &QListWidget::currentRowChanged, this, &CMapTableEditorDialog::selectZone);
    connect(m_applyButton, &QPushButton::clicked, this, &CMapTableEditorDialog::applySelectedZone);
    connect(m_registerButton, &QPushButton::clicked, this, &CMapTableEditorDialog::registerNewZone);
    connect(addExitButton, &QPushButton::clicked, this, &CMapTableEditorDialog::addExit);
    connect(removeExitButton, &QPushButton::clicked, this, &CMapTableEditorDialog::removeExit);
    connect(m_saveButton, &QPushButton::clicked, this, &CMapTableEditorDialog::saveArchiveAs);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_mapsEnabled, &QCheckBox::toggled, m_sectorWidth, &QWidget::setEnabled);
    connect(m_mapsEnabled, &QCheckBox::toggled, m_sectorHeight, &QWidget::setEnabled);
    connect(m_mapsEnabled, &QCheckBox::toggled, m_minimapEdit, &QWidget::setEnabled);
    connect(m_mapsEnabled, &QCheckBox::toggled, m_objectivesEdit, &QWidget::setEnabled);
    setArchiveUiEnabled(false);
}

void CMapTableEditorDialog::setArchiveUiEnabled(bool enabled)
{
    m_zoneList->setEnabled(enabled);
    m_importButton->setEnabled(enabled);
    m_exportButton->setEnabled(enabled);
    m_applyButton->setEnabled(enabled && !m_newZoneMode);
    m_duplicateButton->setEnabled(enabled && m_selectedZone >= 0 && !m_newZoneMode);
    m_saveButton->setEnabled(enabled);
    m_zoneIdEdit->setEnabled(enabled);
    m_allodIdEdit->setEnabled(enabled);
    m_zoneTypeCombo->setEnabled(enabled);
    m_mprEdit->setEnabled(enabled);
    m_mobEdit->setEnabled(enabled);
    m_mapsEnabled->setEnabled(enabled);
    m_exitTable->setEnabled(enabled);
}

void CMapTableEditorDialog::chooseArchive()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open a copied or mod-profile texts.res"), QString(), tr("RES archives (*.res)"));
    if(!path.isEmpty())
        openArchive(path);
}

bool CMapTableEditorDialog::openArchive(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        setStatus(file.errorString(), true);
        return false;
    }
    const QByteArray bytes = file.readAll();
    QStringList errors;
    CResArchiveDocument archive;
    if(!archive.loadBytes(bytes, path, &errors))
    {
        setStatus(errors.join(" | "), true);
        return false;
    }
    QString memberName;
    for(const SResArchiveMember& member : archive.members())
    {
        if(member.name.compare("map.txt", Qt::CaseInsensitive) == 0)
        {
            if(!memberName.isEmpty())
            {
                setStatus(tr("Archive contains more than one case-insensitive map.txt member."), true);
                return false;
            }
            memberName = member.name;
        }
    }
    if(memberName.isEmpty())
    {
        setStatus(tr("Archive does not contain map.txt."), true);
        return false;
    }
    CMapTableDocument table;
    if(!table.loadBytes(archive.memberPayload(memberName), &errors))
    {
        setStatus(errors.join(" | "), true);
        return false;
    }
    m_archive = archive;
    m_table = table;
    m_archivePath = QFileInfo(path).absoluteFilePath();
    m_mapMemberName = memberName;
    m_archiveEdit->setText(m_archivePath);
    m_newZoneMode = false;
    setArchiveUiEnabled(true);
    refreshZoneList(m_table.zones().isEmpty() ? -1 : 0);
    setStatus(tr("Loaded %1 zones; no files were changed.").arg(m_table.zones().size()));
    return true;
}

void CMapTableEditorDialog::refreshZoneList(int selectedRow)
{
    m_zoneList->blockSignals(true);
    m_zoneList->clear();
    for(const SMapTableZone& zone : m_table.zones())
    {
        QListWidgetItem* listItem = new QListWidgetItem(
            QStringLiteral("%1  [%2 / %3]").arg(zone.zoneId, zone.allodId, zone.zoneType));
        listItem->setData(Qt::UserRole, zone.zoneId);
        m_zoneList->addItem(listItem);
    }
    m_zoneList->setCurrentRow(selectedRow);
    m_zoneList->blockSignals(false);
    selectZone(selectedRow);
}

void CMapTableEditorDialog::selectZone(int row)
{
    if(row < 0 || row >= m_table.zones().size())
        return;
    m_newZoneMode = false;
    m_selectedZone = row;
    m_applyButton->setVisible(true);
    m_applyButton->setEnabled(true);
    m_registerButton->setVisible(false);
    m_duplicateButton->setEnabled(true);
    loadZone(m_table.zones().at(row));
}

void CMapTableEditorDialog::loadZone(const SMapTableZone& zone)
{
    m_zoneIdEdit->setText(zone.zoneId);
    m_allodIdEdit->setText(zone.allodId);
    while(m_zoneTypeCombo->count() > 3)
        m_zoneTypeCombo->removeItem(m_zoneTypeCombo->count() - 1);
    int zoneTypeIndex = m_zoneTypeCombo->findText(zone.zoneType, Qt::MatchFixedString);
    if(zoneTypeIndex < 0 && !zone.zoneType.isEmpty())
    {
        m_zoneTypeCombo->addItem(zone.zoneType);
        zoneTypeIndex = m_zoneTypeCombo->count() - 1;
        m_zoneTypeCombo->setItemData(zoneTypeIndex,
            tr("Unsupported source value; choose game, brief, or edge before verified RES save."),
            Qt::ToolTipRole);
    }
    m_zoneTypeCombo->setCurrentIndex(zoneTypeIndex >= 0 ? zoneTypeIndex : 0);
    m_mprEdit->setText(zone.mprName);
    m_mobEdit->setText(zone.mobName);
    m_mapsEnabled->setChecked(zone.maps.present);
    m_sectorWidth->setValue(qMax(1, zone.maps.sectorWidth));
    m_sectorHeight->setValue(qMax(1, zone.maps.sectorHeight));
    m_minimapEdit->setText(zone.maps.minimapTexture);
    m_objectivesEdit->setText(zone.maps.objectivesTexture);
    m_exitTable->setRowCount(0);
    for(const SMapTableExit& exit : zone.exits)
    {
        const int row = m_exitTable->rowCount();
        m_exitTable->insertRow(row);
        m_exitTable->setItem(row, ExitId, item(QString::number(exit.exitId)));
        m_exitTable->setItem(row, DestinationZone, item(exit.destinationZoneId));
        m_exitTable->setItem(row, DestinationExit, item(QString::number(exit.destinationExitId)));
        if(exit.hasDeployRectangle)
        {
            m_exitTable->setItem(row, DeployX1, item(number(exit.deployRectangle.left())));
            m_exitTable->setItem(row, DeployY1, item(number(exit.deployRectangle.top())));
            m_exitTable->setItem(row, DeployX2, item(number(exit.deployRectangle.right())));
            m_exitTable->setItem(row, DeployY2, item(number(exit.deployRectangle.bottom())));
        }
        if(exit.hasRemoveRectangle)
        {
            m_exitTable->setItem(row, RemoveX1, item(number(exit.removeRectangle.left())));
            m_exitTable->setItem(row, RemoveY1, item(number(exit.removeRectangle.top())));
            m_exitTable->setItem(row, RemoveX2, item(number(exit.removeRectangle.right())));
            m_exitTable->setItem(row, RemoveY2, item(number(exit.removeRectangle.bottom())));
        }
        if(exit.hasPassTime) m_exitTable->setItem(row, PassTime, item(number(exit.passTime)));
        if(exit.hasViewAngle) m_exitTable->setItem(row, ViewAngle, item(number(exit.viewAngle)));
        if(exit.hasDeployAngle) m_exitTable->setItem(row, DeployAngle, item(number(exit.deployAngle)));
    }
    m_rawReview->setPlainText(zone.rawBlock);
}

QString CMapTableEditorDialog::cellText(int row, int column) const
{
    const QTableWidgetItem* tableItem = m_exitTable->item(row, column);
    return tableItem ? tableItem->text().trimmed() : QString();
}

bool CMapTableEditorDialog::collectZone(SMapTableZone* zone, QString* error) const
{
    if(!zone)
        return false;
    zone->zoneId = m_zoneIdEdit->text().trimmed();
    zone->allodId = m_allodIdEdit->text().trimmed();
    zone->zoneType = m_zoneTypeCombo->currentText();
    zone->mprName = m_mprEdit->text().trimmed();
    zone->mobName = m_mobEdit->text().trimmed();
    zone->maps.present = m_mapsEnabled->isChecked();
    zone->maps.sectorWidth = m_sectorWidth->value();
    zone->maps.sectorHeight = m_sectorHeight->value();
    zone->maps.minimapTexture = m_minimapEdit->text().trimmed();
    zone->maps.objectivesTexture = m_objectivesEdit->text().trimmed();
    zone->exits.clear();
    const auto parseDouble = [this, error](int row, int column, double* value) {
        const QString text = cellText(row, column);
        if(text.isEmpty()) return 0;
        bool ok = false;
        const double parsed = text.toDouble(&ok);
        if(!ok && error) *error = tr("Entrance row %1 contains an invalid number: %2").arg(row + 1).arg(text);
        if(ok && value) *value = parsed;
        return ok ? 1 : -1;
    };
    for(int row = 0; row < m_exitTable->rowCount(); ++row)
    {
        SMapTableExit exit;
        bool idOk = false;
        bool destinationOk = false;
        exit.exitId = cellText(row, ExitId).toInt(&idOk);
        exit.destinationZoneId = cellText(row, DestinationZone);
        exit.destinationExitId = cellText(row, DestinationExit).toInt(&destinationOk);
        if(!idOk || !destinationOk)
        {
            if(error) *error = tr("Entrance row %1 requires numeric exit IDs.").arg(row + 1);
            return false;
        }
        double rectangle[4];
        int present = 0;
        for(int column = DeployX1; column <= DeployY2; ++column)
        {
            const int parsed = parseDouble(row, column, &rectangle[column - DeployX1]);
            if(parsed < 0) return false;
            present += parsed;
        }
        if(present != 0 && present != 4)
        {
            if(error) *error = tr("Entrance row %1 must provide all four deploy coordinates or none.").arg(row + 1);
            return false;
        }
        if(present == 4)
        {
            exit.hasDeployRectangle = true;
            exit.deployRectangle = QRectF(QPointF(rectangle[0], rectangle[1]), QPointF(rectangle[2], rectangle[3]));
        }
        present = 0;
        for(int column = RemoveX1; column <= RemoveY2; ++column)
        {
            const int parsed = parseDouble(row, column, &rectangle[column - RemoveX1]);
            if(parsed < 0) return false;
            present += parsed;
        }
        if(present != 0 && present != 4)
        {
            if(error) *error = tr("Entrance row %1 must provide all four remove coordinates or none.").arg(row + 1);
            return false;
        }
        if(present == 4)
        {
            exit.hasRemoveRectangle = true;
            exit.removeRectangle = QRectF(QPointF(rectangle[0], rectangle[1]), QPointF(rectangle[2], rectangle[3]));
        }
        int parsed = parseDouble(row, PassTime, &exit.passTime);
        if(parsed < 0)
            return false;
        exit.hasPassTime = parsed > 0;
        parsed = parseDouble(row, ViewAngle, &exit.viewAngle);
        if(parsed < 0)
            return false;
        exit.hasViewAngle = parsed > 0;
        parsed = parseDouble(row, DeployAngle, &exit.deployAngle);
        if(parsed < 0)
            return false;
        exit.hasDeployAngle = parsed > 0;
        zone->exits.append(exit);
    }
    return true;
}

void CMapTableEditorDialog::applySelectedZone()
{
    applyCurrentZone();
}

bool CMapTableEditorDialog::applyCurrentZone()
{
    if(m_selectedZone < 0 || m_selectedZone >= m_table.zones().size())
        return false;
    SMapTableZone zone = m_table.zones().at(m_selectedZone);
    QString error;
    if(!collectZone(&zone, &error))
    {
        setStatus(error, true);
        return false;
    }
    const QString originalId = m_table.zones().at(m_selectedZone).zoneId;
    const SMapTableEditResult result = m_table.replaceZone(originalId, zone);
    if(!result.success)
    {
        setStatus(result.toText(), true);
        return false;
    }
    refreshZoneList(m_selectedZone);
    setStatus(result.changedFields.isEmpty() ? tr("No map metadata changed.")
                                            : result.changedFields.join(", "));
    return true;
}

void CMapTableEditorDialog::beginNewZone()
{
    if(m_archivePath.isEmpty())
        return;
    m_newZoneMode = true;
    m_selectedZone = -1;
    m_zoneList->clearSelection();
    m_duplicateButton->setEnabled(false);
    m_tabs->setCurrentIndex(0);
    SMapTableZone blank;
    blank.maps.sectorWidth = 1;
    blank.maps.sectorHeight = 1;
    loadZone(blank);
    m_rawReview->setPlainText(tr(
        "New registration preview is created only after every required value is supplied. "
        "No weather, sky, camera, figure, cage, or LOD metadata will be invented."));
    m_applyButton->setVisible(false);
    m_registerButton->setVisible(true);
    setStatus(tr("Enter explicit registration values, then choose Register New Zone."));
}

void CMapTableEditorDialog::beginDuplicateZone()
{
    if(m_archivePath.isEmpty() || m_selectedZone < 0 || m_selectedZone >= m_table.zones().size())
        return;
    const SMapTableZone source = m_table.zones().at(m_selectedZone);
    m_newZoneMode = true;
    m_selectedZone = -1;
    m_zoneList->clearSelection();
    m_duplicateButton->setEnabled(false);
    m_tabs->setCurrentIndex(0);
    SMapTableZone duplicate = source;
    duplicate.zoneId.clear();
    duplicate.rawBlock.clear();
    loadZone(duplicate);
    m_rawReview->setPlainText(tr(
        "Structured metadata was copied from %1. The new Zone ID is intentionally blank. "
        "Unknown tags, comments, and raw source ownership were not copied; review every resource and entrance before registering.")
        .arg(source.zoneId));
    m_applyButton->setVisible(false);
    m_registerButton->setVisible(true);
    setStatus(tr("Duplicating %1: enter a unique Zone ID and review all copied metadata.").arg(source.zoneId));
}

void CMapTableEditorDialog::registerNewZone()
{
    SMapTableZone zone;
    QString error;
    if(!collectZone(&zone, &error))
    {
        setStatus(error, true);
        return;
    }
    const SMapTableEditResult result = m_table.appendZone(zone);
    if(!result.success)
    {
        setStatus(result.toText(), true);
        return;
    }
    m_newZoneMode = false;
    refreshZoneList(m_table.zones().size() - 1);
    setStatus(result.toText());
}

void CMapTableEditorDialog::addExit()
{
    const int row = m_exitTable->rowCount();
    m_exitTable->insertRow(row);
    int nextId = 1;
    for(int existing = 0; existing < row; ++existing)
        nextId = qMax(nextId, cellText(existing, ExitId).toInt() + 1);
    m_exitTable->setItem(row, ExitId, item(QString::number(nextId)));
    m_exitTable->setItem(row, DestinationExit, item("0"));
    m_exitTable->setCurrentCell(row, DestinationZone);
}

void CMapTableEditorDialog::removeExit()
{
    const int row = m_exitTable->currentRow();
    if(row >= 0)
        m_exitTable->removeRow(row);
}

void CMapTableEditorDialog::exportText()
{
    if(m_archivePath.isEmpty())
        return;
    if(m_newZoneMode)
    {
        setStatus(tr("Register or cancel the new zone before exporting."), true);
        return;
    }
    if(!applyCurrentZone())
        return;

    const QString suggested = QFileInfo(m_archivePath).dir().filePath("map.txt");
    const QString destination = QFileDialog::getSaveFileName(
        this, tr("Export exact map.txt bytes"), suggested, tr("Text files (*.txt);;All files (*)"));
    if(destination.isEmpty())
        return;

    QSaveFile file(destination);
    if(!file.open(QIODevice::WriteOnly))
    {
        setStatus(file.errorString(), true);
        return;
    }
    const QByteArray bytes = m_table.serialize();
    if(file.write(bytes) != bytes.size() || !file.commit())
    {
        setStatus(file.errorString(), true);
        return;
    }
    setStatus(tr("Exported the exact current map.txt bytes; the RES archive was not changed."));
}

void CMapTableEditorDialog::importText()
{
    if(m_archivePath.isEmpty())
        return;
    if(m_newZoneMode)
    {
        setStatus(tr("Register or cancel the new zone before importing."), true);
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("Import hand-edited map.txt"),
        tr("Endurance can verify basic map.txt structure and preserve the imported bytes, but it "
           "cannot prove that zone types, profile-specific tags, resource names, travel links, or "
           "script references are correct for the game. Import replaces the current in-memory "
           "catalog; no RES file is changed until you use Save Verified RES As.\n\n"
           "Continue and accept responsibility for the hand-edited catalog?"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if(answer != QMessageBox::Yes)
        return;

    const QString source = QFileDialog::getOpenFileName(
        this, tr("Import map.txt"), QFileInfo(m_archivePath).absolutePath(),
        tr("Text files (*.txt);;All files (*)"));
    if(source.isEmpty())
        return;
    QFile file(source);
    if(!file.open(QIODevice::ReadOnly))
    {
        setStatus(file.errorString(), true);
        return;
    }
    const QByteArray bytes = file.readAll();
    QStringList errors;
    CMapTableDocument imported;
    if(!imported.loadBytes(bytes, &errors) || imported.zones().isEmpty())
    {
        if(errors.isEmpty())
            errors.append(tr("The imported file contains no valid #zone records."));
        setStatus(tr("Import rejected: %1").arg(errors.join(" | ")), true);
        return;
    }

    m_table = imported;
    m_newZoneMode = false;
    refreshZoneList(0);
    setStatus(tr("Imported %1 zones into memory; the RES archive is unchanged until verified save.")
              .arg(m_table.zones().size()));
}

void CMapTableEditorDialog::saveArchiveAs()
{
    if(m_archivePath.isEmpty())
        return;
    if(m_newZoneMode)
    {
        setStatus(tr("Register or cancel the new zone before saving."), true);
        return;
    }
    if(!applyCurrentZone())
        return;
    const SMapTableEditResult validation = m_table.validateAllZones();
    if(!validation.success)
    {
        setStatus(tr("Verified save rejected: %1").arg(validation.toText()), true);
        return;
    }
    const QString destination = QFileDialog::getSaveFileName(
        this, tr("Save verified texts.res"), m_archivePath, tr("RES archives (*.res)"));
    if(destination.isEmpty())
        return;
    const QByteArray currentPayload = m_archive.memberPayload(m_mapMemberName);
    SResMemberSaveRequest request;
    request.source = QFileInfo(m_archivePath);
    request.destination = QFileInfo(destination);
    request.memberName = m_mapMemberName;
    request.expectedMemberSha256 = QCryptographicHash::hash(
        currentPayload, QCryptographicHash::Sha256).toHex();
    request.replacementPayload = m_table.serialize();
    const SResMemberSaveResult result = commitStagedResMemberReplacement(m_archive, request);
    if(!result.success)
    {
        setStatus(result.phase + ": " + result.error, true);
        return;
    }
    const QString message = result.noOp ? tr("Destination already matches; no write was needed.")
        : tr("Verified RES saved. Backup: %1").arg(
              result.backupPath.isEmpty() ? tr("not needed (new file)") : result.backupPath);
    if(!openArchive(destination))
        return;
    setStatus(message);
}

void CMapTableEditorDialog::setStatus(const QString& text, bool error)
{
    m_statusEdit->setText(text.simplified());
    m_statusEdit->setStyleSheet(error ? "color: #b00020;" : QString());
}
