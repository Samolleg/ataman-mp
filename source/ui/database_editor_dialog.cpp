#include "database_editor_dialog.h"
#include "database_workbook.h"
#include "profile_database.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QAction>
#include <QFontDatabase>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSet>
#include <QGuiApplication>
#include "backup_manager.h"

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
static QString boundedCellPreview(const QString& value)
{
    QString preview=value;
    preview.replace('\r',' ');preview.replace('\n',' ');
    const int limit=96;
    return preview.size()>limit?preview.left(limit-1)+QChar(0x2026):preview;
}
static bool jsonStringList(const QString& value,QStringList* result)
{
    QJsonParseError error;const QJsonDocument document=QJsonDocument::fromJson(value.toUtf8(),&error);if(error.error!=QJsonParseError::NoError||!document.isArray())return false;QStringList values;for(const QJsonValue& item:document.array()){if(!item.isString())return false;values<<item.toString();}if(result)*result=values;return true;
}
static QString authoringText(const QVariant& stored)
{
    if(stored.type()==QVariant::StringList)return stored.toStringList().join('\n');QStringList values;if(jsonStringList(stored.toString(),&values))return values.join('\n');return stored.toString();
}
static QVariant editorValue(const SDatabaseFieldSchema& schema,const QVariant& original,const QString& text)
{
    if(schema.type==EDatabaseFieldType::Integer)return text.toLongLong();if(schema.type==EDatabaseFieldType::Decimal)return text.toDouble();
    if(schema.type==EDatabaseFieldType::TextList){QStringList values;for(const QString& line:text.split(QRegExp("[\\r\\n]+"),QString::SkipEmptyParts))if(!line.trimmed().isEmpty())values<<line.trimmed();return values;}
    QStringList ignored;if(jsonStringList(original.toString(),&ignored)){QStringList values;for(const QString& line:text.split(QRegExp("[\\r\\n]+"),QString::SkipEmptyParts))if(!line.trimmed().isEmpty())values<<line.trimmed();return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(values)).toJson(QJsonDocument::Compact));}
    return text;
}
static QVariant typedEditorValue(const SDatabaseFieldSchema& schema,const QString& text)
{
    if(schema.type==EDatabaseFieldType::Integer)return text.toLongLong();
    if(schema.type==EDatabaseFieldType::Decimal)return text.toDouble();
    if(schema.type!=EDatabaseFieldType::TextList)return text;
    QStringList parts;QString part;int depth=0;
    for(const QChar ch:text){if(ch=='['||ch=='('||ch=='{')++depth;else if(ch==']'||ch==')'||ch=='}')depth=qMax(0,depth-1);if(ch==','&&depth==0){if(!part.trimmed().isEmpty())parts<<part.trimmed();part.clear();}else part+=ch;}
    if(!part.trimmed().isEmpty())parts<<part.trimmed();return parts;
}
DatabaseEditorDialog::DatabaseEditorDialog(const QSharedPointer<DatabaseDocument>& document,QWidget* parent):QDialog(nullptr,Qt::Window|Qt::WindowSystemMenuHint|Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint),m_document(document)
{
    Q_UNUSED(parent);setWindowTitle(tr("Database Editor — %1").arg(document->profileId()));setWindowIcon(QApplication::windowIcon());setAttribute(Qt::WA_DeleteOnClose);resize(1180,720);setModal(false);
    auto* layout=new QVBoxLayout(this); auto* source=new QLabel(tr("Profile: %1  •  Provider: %2\nSource: %3").arg(document->profileId(),document->providerId(),document->sourceArchivePath()),this);source->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(source);
    auto* filterRow=new QWidget(this);auto* filterLayout=new QHBoxLayout(filterRow);filterLayout->setContentsMargins(0,0,0,0);
    m_filter=new QLineEdit(filterRow);m_filter->setPlaceholderText(tr("Filter rows across visible fields…"));filterLayout->addWidget(m_filter,1);
    m_showOpaque=new QCheckBox(tr("Show unknown/raw columns"),filterRow);m_showOpaque->setChecked(true);m_showOpaque->setToolTip(tr("Shows descriptor-unknown fields as read-only hexadecimal data. They remain preserved even while hidden."));filterLayout->addWidget(m_showOpaque);layout->addWidget(filterRow);
    auto* split=new QSplitter(this);m_tables=new QListWidget(split);m_tables->setMinimumWidth(190);m_grid=new QTableWidget(split);m_grid->setObjectName("databaseGrid");m_grid->setSortingEnabled(true);m_grid->setSelectionBehavior(QAbstractItemView::SelectRows);m_grid->setAlternatingRowColors(true);m_grid->setContextMenuPolicy(Qt::CustomContextMenu);m_grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);m_grid->horizontalHeader()->setDefaultSectionSize(150);m_grid->horizontalHeader()->setMinimumSectionSize(70);m_grid->horizontalHeader()->setMaximumSectionSize(300);split->setStretchFactor(1,1);layout->addWidget(split,1);
    auto* buttons=new QDialogButtonBox(this);m_cloneButton=buttons->addButton(tr("Clone Row…"),QDialogButtonBox::ActionRole);m_deleteButton=buttons->addButton(tr("Delete Row"),QDialogButtonBox::ActionRole);auto* reviewButton=buttons->addButton(tr("Review Changes…"),QDialogButtonBox::ActionRole);auto* exportButton=buttons->addButton(tr("Export XLSX…"),QDialogButtonBox::ActionRole);auto* importButton=buttons->addButton(tr("Import XLSX…"),QDialogButtonBox::ActionRole);auto* applyButton=buttons->addButton(tr("Apply Database"),QDialogButtonBox::ActionRole);auto* saveButton=buttons->addButton(tr("Save Database As…"),QDialogButtonBox::ActionRole);saveButton->setObjectName("databaseSaveAsButton");buttons->addButton(QDialogButtonBox::Close);layout->addWidget(buttons);m_status=new QLabel(this);m_status->setWordWrap(true);layout->addWidget(m_status);
    for(const QString& key:document->tableKeys()){const auto* table=document->table(key);if(table->physicalTable<0)continue;auto* item=new QListWidgetItem(tr("%1 (%2)").arg(table->label).arg(table->rows.size()),m_tables);item->setData(Qt::UserRole,key);}
    m_filterTimer=new QTimer(this);m_filterTimer->setSingleShot(true);m_filterTimer->setInterval(120);connect(m_filterTimer,&QTimer::timeout,this,&DatabaseEditorDialog::rebuildGrid);connect(m_tables,&QListWidget::currentRowChanged,this,&DatabaseEditorDialog::selectTable);connect(m_filter,&QLineEdit::textChanged,this,[this](){m_filterTimer->start();});connect(m_showOpaque,&QCheckBox::toggled,this,&DatabaseEditorDialog::rebuildGrid);connect(m_grid,&QTableWidget::cellChanged,this,&DatabaseEditorDialog::editCell);connect(m_grid,&QTableWidget::cellDoubleClicked,this,&DatabaseEditorDialog::editFieldDialog);connect(m_grid,&QTableWidget::customContextMenuRequested,this,&DatabaseEditorDialog::showRowContextMenu);connect(m_cloneButton,&QPushButton::clicked,this,&DatabaseEditorDialog::cloneSelected);connect(m_deleteButton,&QPushButton::clicked,this,&DatabaseEditorDialog::deleteSelected);connect(reviewButton,&QPushButton::clicked,this,&DatabaseEditorDialog::review);connect(exportButton,&QPushButton::clicked,this,&DatabaseEditorDialog::exportXlsx);connect(importButton,&QPushButton::clicked,this,&DatabaseEditorDialog::importXlsx);connect(applyButton,&QPushButton::clicked,this,&DatabaseEditorDialog::apply);connect(saveButton,&QPushButton::clicked,this,&DatabaseEditorDialog::saveAs);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::close);
    auto*editFullValue=new QAction(tr("Edit Full Value…"),m_grid);editFullValue->setShortcuts({QKeySequence(Qt::Key_Return),QKeySequence(Qt::Key_Enter),QKeySequence(Qt::Key_F2)});editFullValue->setShortcutContext(Qt::WidgetShortcut);m_grid->addAction(editFullValue);connect(editFullValue,&QAction::triggered,this,[this](){if(m_grid->currentRow()>=0&&m_grid->currentColumn()>=0)editFieldDialog(m_grid->currentRow(),m_grid->currentColumn());});
    if(m_tables->count())m_tables->setCurrentRow(0);updateStatus();
}
QString DatabaseEditorDialog::currentTableKey()const{auto*i=m_tables->currentItem();return i?i->data(Qt::UserRole).toString():QString();}
QString DatabaseEditorDialog::selectedRowKey()const{auto*i=m_grid->item(m_grid->currentRow(),0);return i?i->data(Qt::UserRole).toString():QString();}
QString DatabaseEditorDialog::runtimeRowIdentity(const QString& tableKey,const QString& rowKey)const{return tableKey==QStringLiteral("units.monsters")?m_document->displayIdentity(tableKey,rowKey):rowKey;}
void DatabaseEditorDialog::selectTable(){rebuildGrid();}
bool DatabaseEditorDialog::selectPrototype(const QString& prototype)
{
    const QString wanted = prototype.trimmed();
    if(wanted.isEmpty())
        return false;

    // Navigation must not inherit editor state which can hide the requested row.
    // Clear first so the table switch/rebuild exposes every prototype.
    if(!m_filter->text().isEmpty()){m_filter->clear();m_filterTimer->stop();rebuildGrid();}
    for(int i = 0; i < m_tables->count(); ++i)
        if(m_tables->item(i)->data(Qt::UserRole).toString() == "units.monsters")
        {
            m_tables->setCurrentRow(i);
            break;
        }

    for(int row = 0; row < m_grid->rowCount(); ++row)
    {
        QTableWidgetItem* item = m_grid->item(row, 0);
        if(item && (item->data(Qt::UserRole).toString().compare(wanted, Qt::CaseInsensitive) == 0
                    || item->text().compare(wanted,Qt::CaseInsensitive)==0))
        {
            m_grid->setCurrentCell(row, 0);
            m_grid->selectRow(row);
            m_grid->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            return true;
        }
    }
    return false;
}
void DatabaseEditorDialog::rebuildGrid()
{
    const auto* table=m_document->table(currentTableKey());if(!table)return;
    m_cloneButton->setEnabled(table->structuralEditing);m_deleteButton->setEnabled(table->structuralEditing);
    const QString structuralReason=table->structuralEditing?QString():tr("This complete/raw table is visible for preservation review. Structural editing is disabled until its typed serializer is verified.");
    m_cloneButton->setToolTip(structuralReason);m_deleteButton->setToolTip(structuralReason);m_refreshing=true;m_grid->setSortingEnabled(false);m_grid->clear();
    QVector<int> visibleFields;for(int i=0;i<table->fields.size();++i)if(!table->structuralEditing||m_showOpaque->isChecked()||!table->fields[i].opaque)visibleFields<<i;
    m_grid->setColumnCount(visibleFields.size());QStringList headers;for(int index:visibleFields)headers<<(table->fields[index].label.isEmpty()?translatedFieldLabel(table->fields[index].key,this):table->fields[index].label);m_grid->setHorizontalHeaderLabels(headers);m_grid->setRowCount(0);
    const QString filter=m_filter->text().trimmed();
    for(const auto&r:table->rows){if(r.deleted)continue;QStringList values;for(int index:visibleFields)values<<r.values.value(table->fields[index].key).toString();if(!filter.isEmpty()&&!values.join(' ').contains(filter,Qt::CaseInsensitive)&&!r.sourceStableKey.contains(filter,Qt::CaseInsensitive))continue;int row=m_grid->rowCount();m_grid->insertRow(row);
        for(int c=0;c<visibleFields.size();++c){const auto&f=table->fields[visibleFields[c]];const QVariant stored=r.values.value(f.key);const QString editText=authoringText(stored);QString previewText=editText;previewText.replace('\n',", ");auto*item=new QTableWidgetItem(boundedCellPreview(previewText));item->setData(Qt::UserRole,r.stableKey);item->setData(Qt::UserRole+1,f.key);item->setData(Qt::UserRole+2,editText);QString reason;const bool editable=f.writable&&m_document->fieldEditable(table->key,r.stableKey,f.key,&reason);const bool dialogEdit=editable&&(f.type==EDatabaseFieldType::Text||f.type==EDatabaseFieldType::TextList||f.type==EDatabaseFieldType::Reference);
            if(!editable){item->setToolTip(reason.isEmpty()?tr("Read-only raw payload. It remains preserved byte-for-byte; editing is disabled until this field's type and serializer are verified."):reason);item->setFlags(item->flags()&~Qt::ItemIsEditable);}
            else if(dialogEdit){item->setToolTip(tr("Preview limited to %1 of %2 characters. Double-click or press Enter/F2 to edit the complete value.").arg(item->text().size()).arg(editText.size()));item->setFlags(item->flags()&~Qt::ItemIsEditable);}
            m_grid->setItem(row,c,item);}}
    if(m_grid->columnCount()>0)m_grid->setColumnWidth(0,220);m_grid->setSortingEnabled(true);m_refreshing=false;updateStatus();
}
void DatabaseEditorDialog::refreshGridValue(QTableWidgetItem*item,const QVariant&value,bool dialogEdit){if(!item)return;const QString full=authoringText(value);QString preview=full;preview.replace('\n',", ");m_refreshing=true;m_grid->setSortingEnabled(false);item->setText(boundedCellPreview(preview));item->setData(Qt::UserRole+2,full);if(dialogEdit)item->setToolTip(tr("Preview limited to %1 of %2 characters. Double-click or press Enter/F2 to edit the complete value.").arg(item->text().size()).arg(full.size()));m_grid->setSortingEnabled(true);m_refreshing=false;updateStatus();}
void DatabaseEditorDialog::editCell(int row,int column)
{
    if(m_refreshing)return;const auto*table=m_document->table(currentTableKey());auto*item=m_grid->item(row,column);if(!table||!item)return;const QString rowKey=item->data(Qt::UserRole).toString(),fieldKey=item->data(Qt::UserRole+1).toString();const SDatabaseFieldSchema*schema=nullptr;for(const auto&candidate:table->fields)if(candidate.key==fieldKey){schema=&candidate;break;}if(!schema)return;const QVariant value=typedEditorValue(*schema,item->text());QString diagnostic;const int before=m_document->changes().size();if(!m_document->setValue(table->key,rowKey,schema->key,value,&diagnostic)){QMessageBox::warning(this,tr("Edit rejected"),diagnostic);rebuildGrid();}else{publishLive();if(fieldKey=="name"||fieldKey==QString("fld.%1.0").arg(table->identityFieldId))rebuildGrid();else refreshGridValue(item,value,false);if(m_document->changes().size()>before)emit transactionCommitted(m_document->changes().last().description,table->key,fieldKey=="name"?QString():runtimeRowIdentity(table->key,rowKey));}
}
void DatabaseEditorDialog::editFieldDialog(int row,int column)
{
    const auto*table=m_document->table(currentTableKey());QTableWidgetItem*item=m_grid->item(row,column);if(!table||!item)return;const QString rowKey=item->data(Qt::UserRole).toString(),fieldKey=item->data(Qt::UserRole+1).toString();const SDatabaseFieldSchema*schema=nullptr;for(const auto&candidate:table->fields)if(candidate.key==fieldKey){schema=&candidate;break;}QString reason;if(!schema||!schema->writable||!m_document->fieldEditable(table->key,rowKey,fieldKey,&reason)||!(schema->type==EDatabaseFieldType::Text||schema->type==EDatabaseFieldType::TextList||schema->type==EDatabaseFieldType::Reference))return;
    QDialog dialog(this);dialog.setObjectName("databaseFieldEditorDialog");dialog.setWindowTitle(tr("Edit %1").arg(schema->label));QScreen*screen=QGuiApplication::screenAt(mapToGlobal(rect().center()));const QRect available=screen?screen->availableGeometry():QRect(0,0,1024,768);const int width=qMax(360,qMin(720,available.width()-80)),height=qMax(240,qMin(460,available.height()-100));dialog.setMinimumSize(qMin(440,width),qMin(280,height));dialog.resize(width,height);auto*layout=new QVBoxLayout(&dialog);const QString identity=m_document->displayIdentity(table->key,rowKey);auto*label=new QLabel(tr("%1 — %2").arg(identity.isEmpty()?rowKey:identity,schema->label),&dialog);label->setWordWrap(true);layout->addWidget(label);auto*editor=new QPlainTextEdit(&dialog);editor->setObjectName("databaseFieldEditorText");editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));editor->setPlainText(item->data(Qt::UserRole+2).toString());QVariant storedForEditor;for(const auto& candidate:table->rows)if(candidate.stableKey==rowKey){storedForEditor=candidate.values.value(fieldKey);break;}QStringList parsedList;const bool listEditor=schema->type==EDatabaseFieldType::TextList||jsonStringList(storedForEditor.toString(),&parsedList);editor->setToolTip(listEditor?tr("One list entry per line. The brackets and quotation marks used internally are intentionally hidden."):tr("This is the complete stored value. Long lines wrap visually."));layout->addWidget(editor,1);auto*count=new QLabel(&dialog);count->setObjectName("databaseFieldEditorCount");layout->addWidget(count);auto updateCount=[count,editor,listEditor,this](){count->setText(listEditor?tr("%1 item(s) — one per line").arg(editor->toPlainText().split(QRegExp("[\\r\\n]+"),QString::SkipEmptyParts).size()):tr("%1 character(s) — complete value").arg(editor->toPlainText().size()));};connect(editor,&QPlainTextEdit::textChanged,&dialog,updateCount);updateCount();auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->setObjectName("databaseFieldEditorButtons");buttons->button(QDialogButtonBox::Ok)->setDefault(true);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);editor->selectAll();editor->setFocus();if(dialog.exec()!=QDialog::Accepted)return;
    const QVariant original=table->rows.isEmpty()?QVariant():[&](){for(const auto& candidate:table->rows)if(candidate.stableKey==rowKey)return candidate.values.value(fieldKey);return QVariant();}();const QVariant value=editorValue(*schema,original,editor->toPlainText());QString diagnostic;const int before=m_document->changes().size();if(!m_document->setValue(table->key,rowKey,fieldKey,value,&diagnostic)){QMessageBox::warning(this,tr("Edit rejected"),diagnostic);rebuildGrid();return;}publishLive();if(fieldKey=="name"||fieldKey==QString("fld.%1.0").arg(table->identityFieldId))rebuildGrid();else refreshGridValue(item,value,true);if(m_document->changes().size()>before)emit transactionCommitted(m_document->changes().last().description,table->key,fieldKey=="name"?QString():runtimeRowIdentity(table->key,rowKey));
}
void DatabaseEditorDialog::showRowContextMenu(const QPoint& position)
{
    QTableWidgetItem*item=m_grid->itemAt(position);if(!item)return;m_grid->setCurrentCell(item->row(),item->column());m_grid->selectRow(item->row());const auto*table=m_document->table(currentTableKey());auto*menu=new QMenu(m_grid);menu->setAttribute(Qt::WA_DeleteOnClose);menu->setObjectName("databaseRowContextMenu");QAction*copy=menu->addAction(tr("Copy Row ID"));copy->setObjectName("databaseCopyRowIdAction");connect(copy,&QAction::triggered,this,&DatabaseEditorDialog::copySelectedRowId);QAction*clone=menu->addAction(tr("Duplicate / Clone Row…"));clone->setObjectName("databaseCloneRowAction");clone->setEnabled(table&&table->structuralEditing);connect(clone,&QAction::triggered,this,&DatabaseEditorDialog::cloneSelected);menu->popup(m_grid->viewport()->mapToGlobal(position));
}
void DatabaseEditorDialog::copySelectedRowId(){const QString key=selectedRowKey();if(key.isEmpty())return;const QString identity=m_document->displayIdentity(currentTableKey(),key);QApplication::clipboard()->setText(identity.isEmpty()?key:identity);}
void DatabaseEditorDialog::cloneSelected(){const QString key=selectedRowKey();if(key.isEmpty())return;const QString originalName=m_document->displayIdentity(currentTableKey(),key);QDialog dialog(this);dialog.setWindowTitle(tr("Clone row"));auto*layout=new QVBoxLayout(&dialog);auto*label=new QLabel(tr("New stable name / ID:"),&dialog);auto*name=new QLineEdit(originalName.isEmpty()?key:originalName,&dialog);auto*reason=new QLabel(&dialog);reason->setWordWrap(true);auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(label);layout->addWidget(name);layout->addWidget(reason);layout->addWidget(buttons);auto update=[&](){const QString value=name->text().trimmed();const bool unchanged=value.compare(key,Qt::CaseInsensitive)==0;const bool available=m_document->identityAvailable(currentTableKey(),value);buttons->button(QDialogButtonBox::Ok)->setEnabled(!unchanged&&available);reason->setText(value.isEmpty()?tr("The ID cannot be empty."):unchanged?tr("Enter a new ID; the original ID is already in use."):!available?tr("That ID is already used by another row."):tr("The new ID is available."));};connect(name,&QLineEdit::textChanged,&dialog,[&](const QString&){update();});connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);update();name->selectAll();name->setFocus();if(dialog.exec()!=QDialog::Accepted)return;const QString newId=name->text().trimmed();QString d;if(!m_document->cloneRow(currentTableKey(),key,newId,&d))QMessageBox::warning(this,tr("Clone rejected"),d);else{publishLive();emit transactionCommitted(m_document->changes().last().description,currentTableKey(),newId);}rebuildGrid();selectPrototype(newId);}
void DatabaseEditorDialog::deleteSelected(){QString key=selectedRowKey();if(key.isEmpty())return;if(QMessageBox::question(this,tr("Delete row"),tr("Mark '%1' for deletion? References will be validated before publication.").arg(key))!=QMessageBox::Yes)return;const QString tableKey=currentTableKey(),runtimeKey=runtimeRowIdentity(tableKey,key);QString d;if(!m_document->deleteRow(tableKey,key,&d))QMessageBox::warning(this,tr("Delete rejected"),d);else{publishLive();emit transactionCommitted(m_document->changes().last().description,tableKey,runtimeKey);}rebuildGrid();}
void DatabaseEditorDialog::publishLive(){QString diagnostic;const auto snapshot=m_document->currentSnapshot(&diagnostic);if(snapshot)ProfileDatabaseContext::instance().publishEditedSnapshot(snapshot);else QMessageBox::warning(this,tr("Database refresh failed"),diagnostic);}
void DatabaseEditorDialog::refreshFromHistory(){publishLive();rebuildGrid();}
void DatabaseEditorDialog::review(){QString text;QSet<QString> seen;for(const auto&c:m_document->changes())text+=QString("• %1\n").arg(c.description);for(const auto&f:m_document->validate())if(!seen.contains(f.message)){seen.insert(f.message);text+=QString("\n%1 %2").arg(f.verified?tr("Verified:"):tr("Opaque:"),f.message);}if(text.isEmpty())text=tr("No pending changes.");QDialog dialog(this);dialog.setWindowFlags(Qt::Dialog|Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint);dialog.setWindowTitle(tr("Pending Database Changes"));QScreen* activeScreen=QGuiApplication::screenAt(mapToGlobal(rect().center()));if(!activeScreen)activeScreen=QGuiApplication::primaryScreen();const QRect available=activeScreen?activeScreen->availableGeometry():QRect(0,0,1024,768);dialog.resize(qMin(760,available.width()-80),qMin(520,available.height()-100));dialog.setMinimumSize(420,280);auto* layout=new QVBoxLayout(&dialog);auto* review=new QPlainTextEdit(&dialog);review->setReadOnly(true);review->setLineWrapMode(QPlainTextEdit::NoWrap);review->setPlainText(text);layout->addWidget(review);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::accept);layout->addWidget(buttons);dialog.exec();}
bool DatabaseEditorDialog::applyDocument(QString* backupPath,QString* diagnostic){CBackupManager backups;const SBackupResult backup=backups.snapshotBeforeSave(QFileInfo(m_document->sourceArchivePath()));if(!backup.success&&!backup.skipped){if(diagnostic)*diagnostic=backup.error;return false;}if(!m_document->applyToSource(diagnostic))return false;if(backupPath)*backupPath=backup.backupPath;return true;}
void DatabaseEditorDialog::apply(){QString backup,diagnostic;if(!applyDocument(&backup,&diagnostic)){QMessageBox::critical(this,tr("Database apply failed"),diagnostic);return;}QMessageBox::information(this,tr("Database applied"),tr("The active database was updated atomically.\n\nBackup: %1").arg(backup));}
void DatabaseEditorDialog::saveAs(){const QString suggested=QFileInfo(m_document->sourceArchivePath()).fileName();const QString path=QFileDialog::getSaveFileName(this,tr("Save Database As"),suggested,tr("Evil Islands database (*.res)"));if(path.isEmpty())return;if(QFileInfo(path).canonicalFilePath().compare(QFileInfo(m_document->sourceArchivePath()).canonicalFilePath(),Qt::CaseInsensitive)==0){QMessageBox::warning(this,tr("Choose a staging path"),tr("Saving over the opened database is blocked. Choose a separate staging path so the source remains recoverable."));return;}QString diagnostic;if(!m_document->saveArchive(path,&diagnostic)){QMessageBox::critical(this,tr("Database save failed"),diagnostic);return;}QMessageBox::information(this,tr("Database saved"),tr("The database was staged successfully at:\n%1\n\nThe source archive was not modified.").arg(path));}
void DatabaseEditorDialog::exportXlsx(){const QString path=QFileDialog::getSaveFileName(this,tr("Export Database XLSX"),QStringLiteral("database.xlsx"),tr("Excel workbook (*.xlsx)"));if(path.isEmpty())return;QString diagnostic;if(!DatabaseWorkbook::exportXlsx(*m_document,path,&diagnostic))QMessageBox::critical(this,tr("XLSX export failed"),diagnostic);}
void DatabaseEditorDialog::importXlsx(){const QString path=QFileDialog::getOpenFileName(this,tr("Import Database XLSX"),QString(),tr("Excel workbook (*.xlsx)"));if(path.isEmpty())return;SDatabaseWorkbookImport imported;QString diagnostic;if(!DatabaseWorkbook::importXlsx(*m_document,path,&imported,&diagnostic)||!m_document->replaceTables(imported.tables,tr("Imported %1").arg(QFileInfo(path).fileName()),&diagnostic)){QMessageBox::critical(this,tr("XLSX import cancelled"),tr("No database changes were applied.\n\n%1").arg(diagnostic));return;}publishLive();rebuildGrid();emit transactionCommitted(m_document->changes().last().description,QString(),QString());if(!m_document->validate().isEmpty())QMessageBox::warning(this,tr("XLSX import completed with warnings"),tr("The workbook was imported as one undoable change. Existing source-data findings are retained for review; only newly introduced critical issues cancel import."));}
void DatabaseEditorDialog::updateStatus(){int errors=0;for(const auto&f:m_document->validate())errors+=f.severity==SDatabaseValidationFinding::Severity::Error;m_status->setText(tr("%1 pending change(s) • %2 verified source finding(s) • Full schema authoring: 26 database tables. Descriptor-unknown fields remain visible as hexadecimal and serialize without touching unrelated data.").arg(m_document->changes().size()).arg(errors));}
}
