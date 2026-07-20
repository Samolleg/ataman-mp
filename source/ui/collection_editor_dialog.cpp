#include "collection_editor_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

CCollectionEditorDialog::CCollectionEditorDialog(
    const SEnduranceCollection& initial,
    const QVector<SMapCatalogEntry>& mapEntries, QWidget* parent)
    : QDialog(parent)
    , m_name(new QLineEdit(initial.name, this))
    , m_profile(new QLineEdit(initial.profileId, this))
    , m_map(new QComboBox(this))
    , m_mpr(new QLineEdit(initial.mprReference, this))
    , m_mobs(new QListWidget(this))
{
    setWindowTitle(tr("Edit Endurance Collection"));
    resize(720, 480);
    m_map->setEditable(true);
    m_map->addItem(tr("No map.txt mapping"), QString());
    for(const SMapCatalogEntry& entry : mapEntries)
        m_map->addItem(QString("%1 — %2 / %3")
            .arg(entry.zoneId, entry.mprName, entry.mobName), entry.zoneId);
    const int mapped = m_map->findData(initial.mapZoneId);
    if(mapped >= 0) m_map->setCurrentIndex(mapped);
    else m_map->setEditText(initial.mapZoneId);
    m_mobs->addItems(initial.mobReferences);

    QPushButton* browseMpr = new QPushButton(tr("Browse..."), this);
    QHBoxLayout* mprRow = new QHBoxLayout;
    mprRow->addWidget(m_mpr, 1);
    mprRow->addWidget(browseMpr);
    QFormLayout* form = new QFormLayout;
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Profile ID:"), m_profile);
    form->addRow(tr("map.txt mapping:"), m_map);
    form->addRow(tr("MPR:"), mprRow);

    QPushButton* add = new QPushButton(tr("Add MOB(s)..."), this);
    QPushButton* remove = new QPushButton(tr("Remove"), this);
    QPushButton* up = new QPushButton(tr("Up"), this);
    QPushButton* down = new QPushButton(tr("Down"), this);
    QHBoxLayout* mobButtons = new QHBoxLayout;
    mobButtons->addWidget(add);
    mobButtons->addWidget(remove);
    mobButtons->addWidget(up);
    mobButtons->addWidget(down);
    mobButtons->addStretch(1);
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Ordered MOB files (main first, then Auto/supplemental):"), this));
    layout->addWidget(m_mobs, 1);
    layout->addLayout(mobButtons);
    layout->addWidget(buttons);

    connect(browseMpr, &QPushButton::clicked, this, &CCollectionEditorDialog::chooseMpr);
    connect(add, &QPushButton::clicked, this, &CCollectionEditorDialog::addMobs);
    connect(remove, &QPushButton::clicked, this, &CCollectionEditorDialog::removeMobs);
    connect(up, &QPushButton::clicked, this, &CCollectionEditorDialog::moveMobUp);
    connect(down, &QPushButton::clicked, this, &CCollectionEditorDialog::moveMobDown);
    connect(buttons, &QDialogButtonBox::accepted, this, &CCollectionEditorDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

SEnduranceCollection CCollectionEditorDialog::collection() const
{
    SEnduranceCollection value;
    value.name = m_name->text().trimmed();
    value.profileId = m_profile->text().trimmed();
    value.mapZoneId = m_map->currentData().toString();
    if(value.mapZoneId.isEmpty() && m_map->currentIndex() < 0)
        value.mapZoneId = m_map->currentText().trimmed();
    value.mprReference = m_mpr->text().trimmed();
    for(int index = 0; index < m_mobs->count(); ++index)
        value.mobReferences.append(m_mobs->item(index)->text().trimmed());
    return value;
}

void CCollectionEditorDialog::chooseMpr()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose MPR"),
        m_mpr->text(), tr("Evil Islands MPR (*.mpr)"));
    if(!path.isEmpty()) m_mpr->setText(path);
}

void CCollectionEditorDialog::addMobs()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add MOB files"),
        m_mpr->text(), tr("Evil Islands MOB (*.mob)"));
    for(const QString& path : paths) m_mobs->addItem(path);
}

void CCollectionEditorDialog::removeMobs()
{
    qDeleteAll(m_mobs->selectedItems());
}

void CCollectionEditorDialog::moveMobUp()
{
    const int row = m_mobs->currentRow();
    if(row > 0)
    {
        QListWidgetItem* item = m_mobs->takeItem(row);
        m_mobs->insertItem(row - 1, item);
        m_mobs->setCurrentRow(row - 1);
    }
}

void CCollectionEditorDialog::moveMobDown()
{
    const int row = m_mobs->currentRow();
    if(row >= 0 && row + 1 < m_mobs->count())
    {
        QListWidgetItem* item = m_mobs->takeItem(row);
        m_mobs->insertItem(row + 1, item);
        m_mobs->setCurrentRow(row + 1);
    }
}

void CCollectionEditorDialog::validateAndAccept()
{
    const SEnduranceCollection value = collection();
    if(value.name.isEmpty() || value.mprReference.isEmpty() || value.mobReferences.isEmpty())
    {
        QMessageBox::warning(this, tr("Collection"),
            tr("Enter a name, one MPR, and at least one MOB."));
        return;
    }
    accept();
}
