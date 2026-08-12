#include "database_editor_dialog.h"
#include "database_workbook.h"
#include "profile_database.h"
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QAction>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace ei {
static QString translatedTableLabel(const QString& key, QObject* context)
{
    if(key=="raceModels") return context->tr("Race models");
    if(key=="monsters") return context->tr("Monster prototypes");
    if(key=="items") return context->tr("Items");
    return key;
}
static QString translatedFieldLabel(const QString& key, QObject* context)
{
    if(key=="name") return context->tr("Name"); if(key=="model") return context->tr("Model");
    if(key=="primaryTextures") return context->tr("Primary textures"); if(key=="secondaryTextures") return context->tr("Secondary textures");
    if(key=="baseRace") return context->tr("Base race"); if(key=="skinIndex") return context->tr("Skin"); if(key=="hairIndex") return context->tr("Hair");
    if(key=="complexionX") return context->tr("Complexion X"); if(key=="complexionY") return context->tr("Complexion Y"); if(key=="complexionZ") return context->tr("Complexion Z");
    if(key=="items") return context->tr("Items"); if(key=="wears") return context->tr("Wears"); if(key=="weapon") return context->tr("Weapon"); if(key=="secondWeapon") return context->tr("Second weapon");
    if(key=="category") return context->tr("Category"); if(key=="type") return context->tr("Type"); if(key=="typeId") return context->tr("Type ID");
    if(key=="materialType") return context->tr("Material type"); if(key=="textureType1") return context->tr("Texture 1"); if(key=="textureType2") return context->tr("Texture 2"); if(key=="wearOrder") return context->tr("Wear order");
    return key;
}
DatabaseEditorDialog::DatabaseEditorDialog(const QSharedPointer<DatabaseDocument>& document,QWidget* parent):QDialog(parent),m_document(document)
{
    setWindowTitle(tr("Database Editor — %1").arg(document->profileId())); resize(1180,720); setModal(false);
    auto* layout=new QVBoxLayout(this); auto* source=new QLabel(tr("Profile: %1  •  Provider: %2\nSource: %3").arg(document->profileId(),document->providerId(),document->sourceArchivePath()),this);source->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(source);
    auto* filterRow=new QWidget(this);auto* filterLayout=new QHBoxLayout(filterRow);filterLayout->setContentsMargins(0,0,0,0);
    m_filter=new QLineEdit(filterRow);m_filter->setPlaceholderText(tr("Filter rows across visible fields…"));filterLayout->addWidget(m_filter,1);
    m_showOpaque=new QCheckBox(tr("Show unknown/raw columns"),filterRow);m_showOpaque->setToolTip(tr("Shows descriptor-unknown fields as read-only hexadecimal data. They remain preserved even while hidden."));filterLayout->addWidget(m_showOpaque);layout->addWidget(filterRow);
    auto* split=new QSplitter(this);m_tables=new QListWidget(split);m_tables->setMinimumWidth(190);m_grid=new QTableWidget(split);m_grid->setSortingEnabled(true);m_grid->setSelectionBehavior(QAbstractItemView::SelectRows);m_grid->setAlternatingRowColors(true);m_grid->horizontalHeader()->setStretchLastSection(true);split->setStretchFactor(1,1);layout->addWidget(split,1);
    auto* buttons=new QDialogButtonBox(this);auto* clone=buttons->addButton(tr("Clone Row…"),QDialogButtonBox::ActionRole);auto* remove=buttons->addButton(tr("Delete Row"),QDialogButtonBox::ActionRole);m_undo=buttons->addButton(tr("Undo"),QDialogButtonBox::ActionRole);m_undo->setObjectName("databaseUndoButton");m_redo=buttons->addButton(tr("Redo"),QDialogButtonBox::ActionRole);m_redo->setObjectName("databaseRedoButton");auto* reviewButton=buttons->addButton(tr("Review Changes…"),QDialogButtonBox::ActionRole);auto* exportButton=buttons->addButton(tr("Export XLSX…"),QDialogButtonBox::ActionRole);auto* importButton=buttons->addButton(tr("Import XLSX…"),QDialogButtonBox::ActionRole);auto* saveButton=buttons->addButton(tr("Save Database As…"),QDialogButtonBox::ActionRole);saveButton->setObjectName("databaseSaveAsButton");buttons->addButton(QDialogButtonBox::Close);layout->addWidget(buttons);m_status=new QLabel(this);m_status->setWordWrap(true);layout->addWidget(m_status);
    for(const QString& key:document->tableKeys()){const auto* table=document->table(key);auto* item=new QListWidgetItem(tr("%1 (%2)").arg(translatedTableLabel(key,this)).arg(table->rows.size()),m_tables);item->setData(Qt::UserRole,key);}
    connect(m_tables,&QListWidget::currentRowChanged,this,&DatabaseEditorDialog::selectTable);connect(m_filter,&QLineEdit::textChanged,this,&DatabaseEditorDialog::rebuildGrid);connect(m_showOpaque,&QCheckBox::toggled,this,&DatabaseEditorDialog::rebuildGrid);connect(m_grid,&QTableWidget::cellChanged,this,&DatabaseEditorDialog::editCell);connect(clone,&QPushButton::clicked,this,&DatabaseEditorDialog::cloneSelected);connect(remove,&QPushButton::clicked,this,&DatabaseEditorDialog::deleteSelected);connect(m_undo,&QPushButton::clicked,this,&DatabaseEditorDialog::undo);connect(m_redo,&QPushButton::clicked,this,&DatabaseEditorDialog::redo);connect(reviewButton,&QPushButton::clicked,this,&DatabaseEditorDialog::review);connect(exportButton,&QPushButton::clicked,this,&DatabaseEditorDialog::exportXlsx);connect(importButton,&QPushButton::clicked,this,&DatabaseEditorDialog::importXlsx);connect(saveButton,&QPushButton::clicked,this,&DatabaseEditorDialog::saveAs);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::close);
    auto* undoAction=new QAction(tr("Undo database edit"),this);undoAction->setShortcut(QKeySequence::Undo);undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);addAction(undoAction);connect(undoAction,&QAction::triggered,this,&DatabaseEditorDialog::undo);
    auto* redoAction=new QAction(tr("Redo database edit"),this);redoAction->setShortcuts(QKeySequence::Redo);redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);addAction(redoAction);connect(redoAction,&QAction::triggered,this,&DatabaseEditorDialog::redo);
    if(m_tables->count())m_tables->setCurrentRow(0);updateStatus();
}
QString DatabaseEditorDialog::currentTableKey()const{auto*i=m_tables->currentItem();return i?i->data(Qt::UserRole).toString():QString();}
QString DatabaseEditorDialog::selectedRowKey()const{auto*i=m_grid->item(m_grid->currentRow(),0);return i?i->data(Qt::UserRole).toString():QString();}
void DatabaseEditorDialog::selectTable(){rebuildGrid();}
void DatabaseEditorDialog::rebuildGrid(){const auto* table=m_document->table(currentTableKey());if(!table)return;m_refreshing=true;m_grid->setSortingEnabled(false);m_grid->clear();QVector<int> visibleFields;for(int i=0;i<table->fields.size();++i)if(m_showOpaque->isChecked()||!table->fields[i].opaque)visibleFields<<i;m_grid->setColumnCount(visibleFields.size());QStringList headers;for(int index:visibleFields)headers<<translatedFieldLabel(table->fields[index].key,this);m_grid->setHorizontalHeaderLabels(headers);m_grid->setRowCount(0);const QString filter=m_filter->text().trimmed();for(const auto&r:table->rows){if(r.deleted)continue;QStringList values;for(int index:visibleFields)values<<r.values.value(table->fields[index].key).toString();if(!filter.isEmpty()&&!values.join(' ').contains(filter,Qt::CaseInsensitive))continue;int row=m_grid->rowCount();m_grid->insertRow(row);for(int c=0;c<visibleFields.size();++c){const auto&f=table->fields[visibleFields[c]];QString text=r.values.value(f.key).type()==QVariant::StringList?r.values.value(f.key).toStringList().join(", "):r.values.value(f.key).toString();auto*item=new QTableWidgetItem(text);item->setData(Qt::UserRole,r.stableKey);item->setData(Qt::UserRole+1,f.key);if(!f.writable)item->setFlags(item->flags()&~Qt::ItemIsEditable);m_grid->setItem(row,c,item);}}m_grid->setSortingEnabled(true);m_grid->resizeColumnsToContents();m_refreshing=false;updateStatus();}
void DatabaseEditorDialog::editCell(int row,int column){if(m_refreshing)return;const auto*table=m_document->table(currentTableKey());auto*item=m_grid->item(row,column);if(!table||!item)return;const QString fieldKey=item->data(Qt::UserRole+1).toString();const SDatabaseFieldSchema*schema=nullptr;for(const auto&candidate:table->fields)if(candidate.key==fieldKey){schema=&candidate;break;}if(!schema)return;QString diagnostic;QVariant value=item->text();if(schema->type==EDatabaseFieldType::Integer)value=item->text().toInt();else if(schema->type==EDatabaseFieldType::Decimal)value=item->text().toDouble();else if(schema->type==EDatabaseFieldType::TextList)value=item->text().split(',',QString::SkipEmptyParts);if(!m_document->setValue(table->key,item->data(Qt::UserRole).toString(),schema->key,value,&diagnostic)){QMessageBox::warning(this,tr("Edit rejected"),diagnostic);rebuildGrid();}else{publishLive();rebuildGrid();}}
void DatabaseEditorDialog::cloneSelected(){QString key=selectedRowKey();if(key.isEmpty())return;bool ok=false;QString name=QInputDialog::getText(this,tr("Clone row"),tr("New stable name:"),QLineEdit::Normal,QString(),&ok);if(!ok)return;QString d;if(!m_document->cloneRow(currentTableKey(),key,name,&d))QMessageBox::warning(this,tr("Clone rejected"),d);else publishLive();rebuildGrid();}
void DatabaseEditorDialog::deleteSelected(){QString key=selectedRowKey();if(key.isEmpty())return;if(QMessageBox::question(this,tr("Delete row"),tr("Mark '%1' for deletion? References will be validated before publication.").arg(key))!=QMessageBox::Yes)return;QString d;if(!m_document->deleteRow(currentTableKey(),key,&d))QMessageBox::warning(this,tr("Delete rejected"),d);rebuildGrid();}
void DatabaseEditorDialog::publishLive(){QString diagnostic;const auto snapshot=m_document->currentSnapshot(&diagnostic);if(snapshot)ProfileDatabaseContext::instance().publishEditedSnapshot(snapshot);else QMessageBox::warning(this,tr("Database refresh failed"),diagnostic);}
void DatabaseEditorDialog::undo(){if(m_document->undo())publishLive();rebuildGrid();}void DatabaseEditorDialog::redo(){if(m_document->redo())publishLive();rebuildGrid();}
void DatabaseEditorDialog::review(){QString text;for(const auto&c:m_document->changes())text+=QString("• %1\n").arg(c.description);for(const auto&f:m_document->validate())text+=QString("\n%1 %2").arg(f.verified?tr("Verified:"):tr("Opaque:"),f.message);if(text.isEmpty())text=tr("No pending changes.");text+=tr("\n\nNative publication is available only for the supported scalar fields listed in the manifest. Clone and delete remain review-only.\n%1").arg(QString::fromUtf8(QJsonDocument(m_document->pendingChangeManifest()).toJson(QJsonDocument::Indented)));QMessageBox box(QMessageBox::Information,tr("Pending Database Changes"),text,QMessageBox::Ok,this);box.setDetailedText(text);box.exec();}
void DatabaseEditorDialog::saveAs(){const QString suggested=QFileInfo(m_document->sourceArchivePath()).fileName();const QString path=QFileDialog::getSaveFileName(this,tr("Save Database As"),suggested,tr("Evil Islands database (*.res)"));if(path.isEmpty())return;if(QFileInfo(path).canonicalFilePath().compare(QFileInfo(m_document->sourceArchivePath()).canonicalFilePath(),Qt::CaseInsensitive)==0){QMessageBox::warning(this,tr("Choose a staging path"),tr("Saving over the opened database is blocked. Choose a separate staging path so the source remains recoverable."));return;}QString diagnostic;if(!m_document->saveArchive(path,&diagnostic)){QMessageBox::critical(this,tr("Database save failed"),diagnostic);return;}QMessageBox::information(this,tr("Database saved"),tr("The database was staged successfully at:\n%1\n\nThe source archive was not modified.").arg(path));}
void DatabaseEditorDialog::exportXlsx(){const QString path=QFileDialog::getSaveFileName(this,tr("Export Database XLSX"),QStringLiteral("database.xlsx"),tr("Excel workbook (*.xlsx)"));if(path.isEmpty())return;QString diagnostic;if(!DatabaseWorkbook::exportXlsx(*m_document,path,&diagnostic))QMessageBox::critical(this,tr("XLSX export failed"),diagnostic);}
void DatabaseEditorDialog::importXlsx(){const QString path=QFileDialog::getOpenFileName(this,tr("Import Database XLSX"),QString(),tr("Excel workbook (*.xlsx)"));if(path.isEmpty())return;SDatabaseWorkbookImport imported;QString diagnostic;if(!DatabaseWorkbook::importXlsx(*m_document,path,&imported,&diagnostic)||!m_document->replaceTables(imported.tables,tr("Imported %1").arg(QFileInfo(path).fileName()),&diagnostic)){QMessageBox::critical(this,tr("XLSX import cancelled"),tr("No database changes were applied.\n\n%1").arg(diagnostic));return;}publishLive();rebuildGrid();if(!m_document->validate().isEmpty())QMessageBox::warning(this,tr("XLSX import completed with warnings"),tr("The workbook was imported as one undoable change. Review the validation panel and pending-change report."));}
void DatabaseEditorDialog::updateStatus(){m_undo->setEnabled(m_document->canUndo());m_redo->setEnabled(m_document->canRedo());int errors=0;for(const auto&f:m_document->validate())errors+=f.severity==SDatabaseValidationFinding::Severity::Error;m_status->setText(tr("%1 pending change(s) • %2 verified error(s) • Native staged save and XLSX interchange are available.").arg(m_document->changes().size()).arg(errors));}
}
