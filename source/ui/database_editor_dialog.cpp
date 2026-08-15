#include "database_editor_dialog.h"
#include "database_workbook.h"
#include "database_schema.h"
#include "profile_database.h"
#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QComboBox>
#include <QCompleter>
#include <QAction>
#include <QFontDatabase>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QSplitter>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QScreen>
#include <QShortcut>
#include <QScrollBar>
#include <QSettings>
#include <QSet>
#include <QGuiApplication>
#include <functional>
#include "backup_manager.h"

namespace ei {
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
static QJsonDocument jsonArrayDocument(const QVariant& stored)
{
    QJsonParseError error;const QJsonDocument document=QJsonDocument::fromJson(stored.toString().toUtf8(),&error);return error.error==QJsonParseError::NoError&&document.isArray()?document:QJsonDocument();
}
static QString jsonScalarText(const QJsonValue& value)
{
    if(value.isBool())return value.toBool()?QStringLiteral("true"):QStringLiteral("false");if(value.isDouble())return QString::number(value.toDouble(),'g',15);if(value.isString())return value.toString();return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}
static QJsonValue editedScalar(const QString& text,const QJsonValue& original)
{
    if(original.isBool())return QJsonValue(text.trimmed().compare("true",Qt::CaseInsensitive)==0||text.trimmed()=="1");if(original.isDouble())return QJsonValue(text.trimmed().toDouble());return QJsonValue(text);
}
static QString structuredLabel(const SDatabaseTable& table,const SDatabaseFieldSchema& field,int id)
{
    for(const auto& header:DatabaseSchema::instance().headers(table.schemaFamily,table.blockId,field.storageId))if(header.index==id&&!header.name.isEmpty())return header.name;return QString();
}
static QString authoringText(const SDatabaseTable& table,const SDatabaseFieldSchema& field,const QVariant& stored)
{
    if(stored.type()==QVariant::StringList)return stored.toStringList().join('\n');const QJsonDocument document=jsonArrayDocument(stored);if(document.isNull())return stored.toString();const QJsonArray values=document.array();bool strings=true;for(const auto& value:values)strings&=value.isString();if(strings){QStringList out;for(const auto& value:values)out<<value.toString();return out.join('\n');}QStringList out;for(int index=0;index<values.size();++index){const QJsonValue value=values[index];if(!value.isObject()){out<<QString("[%1] = %2").arg(index).arg(jsonScalarText(value));continue;}const QJsonObject object=value.toObject();const int id=object.value("id").toInt(index);const QString name=structuredLabel(table,field,id),prefix=QString("[%1]%2").arg(id).arg(name.isEmpty()?QString():" "+name);if(object.contains("value")){out<<prefix+" = "+jsonScalarText(object.value("value"));continue;}if(object.contains("raw")){out<<QString("Record %1 raw = %2").arg(prefix,object.value("raw").toString());continue;}out<<QString("Record %1").arg(prefix);for(const auto& nested:object.value("fields").toArray()){const QJsonObject child=nested.toObject();out<<QString("  [%1] = %2").arg(child.value("id").toInt()).arg(jsonScalarText(child.value("value")));}if(index+1<values.size())out<<QString();}return out.join('\n');
}
static QVariant editorValue(const SDatabaseTable& table,const SDatabaseFieldSchema& field,const QVariant& original,const QString& text)
{
    if(field.type==EDatabaseFieldType::Integer)return text.toLongLong();if(field.type==EDatabaseFieldType::Decimal)return text.toDouble();
    if(field.type==EDatabaseFieldType::TextList){QStringList values;for(const QString& line:text.split(QRegExp("[\\r\\n]+"),QString::SkipEmptyParts))if(!line.trimmed().isEmpty())values<<line.trimmed();return values;}
    const QJsonDocument document=jsonArrayDocument(original);if(document.isNull())return text;QJsonArray values=document.array();bool strings=true;for(const auto& value:values)strings&=value.isString();if(strings){QStringList result;for(const QString& line:text.split(QRegExp("[\\r\\n]+"),QString::SkipEmptyParts))if(!line.trimmed().isEmpty())result<<line.trimmed();return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(result)).toJson(QJsonDocument::Compact));}
    int recordId=-1;const QStringList lines=text.split(QRegExp("[\\r\\n]+"));for(const QString& rawLine:lines){const QString line=rawLine.trimmed();QRegExp record("^Record \\[([0-9]+)\\]");if(record.indexIn(line)==0){recordId=record.cap(1).toInt();const int equals=line.indexOf('=');if(line.contains(" raw ")&&equals>=0)for(int i=0;i<values.size();++i){QJsonObject object=values[i].toObject();if(object.value("id").toInt()==recordId){object["raw"]=line.mid(equals+1).trimmed();values[i]=object;break;}}continue;}if(!line.startsWith('['))continue;const int close=line.indexOf(']'),equals=line.indexOf('=',close+1);bool idOk=false;const int id=close>1?line.mid(1,close-1).toInt(&idOk):-1;if(!idOk||equals<0)continue;const QString edited=line.mid(equals+1).trimmed();if(recordId>=0){for(int i=0;i<values.size();++i){QJsonObject object=values[i].toObject();if(object.value("id").toInt()!=recordId)continue;QJsonArray fields=object.value("fields").toArray();for(int j=0;j<fields.size();++j){QJsonObject child=fields[j].toObject();if(child.value("id").toInt()!=id)continue;child["value"]=editedScalar(edited,child.value("value"));fields[j]=child;break;}object["fields"]=fields;values[i]=object;break;}continue;}bool found=false;for(int i=0;i<values.size();++i){if(values[i].isObject()){QJsonObject object=values[i].toObject();if(object.value("id").toInt()!=id)continue;object["value"]=editedScalar(edited,object.value("value"));values[i]=object;found=true;break;}}if(!found&&id>=0&&id<values.size())values[id]=editedScalar(edited,values[id]);}Q_UNUSED(table);return QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact));
}
enum DatabaseGridRole {
    RowKeyRole = Qt::UserRole,
    FieldKeyRole,
    FullTextRole,
    RawValueRole,
    ReferenceTableRole,
    ChoicesRole,
    FilterTextRole
};

class PersistentColumnMenu final : public QMenu
{
public:
    explicit PersistentColumnMenu(QWidget* parent=nullptr):QMenu(parent){}
protected:
    void mouseReleaseEvent(QMouseEvent* event)override
    {
        QAction* action=actionAt(event->pos());
        if(action&&action->property("databaseStayOpen").toBool())
        {action->trigger();setActiveAction(action);return;}
        QMenu::mouseReleaseEvent(event);
    }
    void keyPressEvent(QKeyEvent* event)override
    {
        QAction* action=activeAction();
        if(action&&action->property("databaseStayOpen").toBool()
           &&(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter
              ||event->key()==Qt::Key_Space))
        {action->trigger();return;}
        QMenu::keyPressEvent(event);
    }
};

class DatabaseTableModel final : public QAbstractTableModel
{
public:
    using CommitHandler = std::function<bool(const QString&,const QString&,const QVariant&)>;
    explicit DatabaseTableModel(const QSharedPointer<DatabaseDocument>& document,
                                QObject* parent=nullptr)
        : QAbstractTableModel(parent), m_document(document) {}

    void setCommitHandler(const CommitHandler& handler) { m_commit = handler; }
    void setTable(const QString& tableKey)
    {
        if(m_tableKey==tableKey) { refresh(); return; }
        beginResetModel();m_tableKey=tableKey;rebuildIndexes();endResetModel();
    }
    void refresh(){beginResetModel();rebuildIndexes();endResetModel();}
    QString tableKey()const{return m_tableKey;}
    int rowCount(const QModelIndex& parent=QModelIndex())const override{return parent.isValid()?0:m_rows.size();}
    int columnCount(const QModelIndex& parent=QModelIndex())const override{return parent.isValid()?0:m_fields.size()+1;}
    QVariant headerData(int section,Qt::Orientation orientation,int role)const override
    {
        if(orientation!=Qt::Horizontal||role!=Qt::DisplayRole||section<0||section>=columnCount())return {};
        if(section==0)return QStringLiteral("#");
        const auto* field=fieldAt(section);return field?(field->label.isEmpty()?field->key:field->label):QVariant();
    }
    QVariant data(const QModelIndex& index,int role=Qt::DisplayRole)const override
    {
        const SDatabaseRow* row=rowAt(index.row());const auto* table=m_document->table(m_tableKey);if(!row||!table)return {};
        const int rowNumber=row->sourceRecordIndex>=0?row->sourceRecordIndex+1:m_rows.value(index.row())+1;
        if(index.column()==0)
        {
            if(role==Qt::DisplayRole||role==Qt::EditRole||role==RawValueRole)return rowNumber;
            if(role==RowKeyRole)return row->stableKey;
            if(role==FilterTextRole){QStringList values;values<<QString("#%1").arg(rowNumber)<<QString::number(rowNumber)<<row->sourceStableKey;for(int fieldIndex:m_fields)values<<authoringText(*table,table->fields[fieldIndex],row->values.value(table->fields[fieldIndex].key));return values.join(' ').toLower();}
            if(role==Qt::TextAlignmentRole)return int(Qt::AlignRight|Qt::AlignVCenter);
            if(role==Qt::ToolTipRole)return tr("Stable physical row number. Search for #%1 to filter this row.").arg(rowNumber);
            return {};
        }
        const SDatabaseFieldSchema* field=fieldAt(index.column());if(!field)return {};
        const QVariant stored=row->values.value(field->key);const QString full=authoringText(*table,*field,stored);
        if(role==Qt::DisplayRole){QString preview=full;preview.replace('\n',", ");return boundedCellPreview(preview);}
        if(role==Qt::EditRole)return field->type==EDatabaseFieldType::Integer||field->type==EDatabaseFieldType::Decimal?stored:QVariant(full);
        if(role==RowKeyRole)return row->stableKey;
        if(role==FieldKeyRole)return field->key;
        if(role==FullTextRole)return full;
        if(role==RawValueRole)return stored;
        if(role==ReferenceTableRole)return field->referenceTable;
        if(role==ChoicesRole)return choicesForColumn(index.column());
        if(role==FilterTextRole){QStringList values;values<<QString("#%1").arg(rowNumber)<<QString::number(rowNumber)<<row->sourceStableKey;for(int fieldIndex:m_fields)values<<authoringText(*table,table->fields[fieldIndex],row->values.value(table->fields[fieldIndex].key));return values.join(' ').toLower();}
        if(role==Qt::ForegroundRole&&!field->referenceTable.isEmpty())return QBrush(QColor(35,110,190));
        if(role==Qt::FontRole&&!field->referenceTable.isEmpty()){QFont font=QApplication::font();font.setUnderline(true);return font;}
        if(role==Qt::ToolTipRole)
        {
            QString reason;const bool editable=field->writable&&m_document->fieldEditable(m_tableKey,row->stableKey,field->key,&reason);
            if(!editable)return reason.isEmpty()?tr("Read-only raw payload. It remains preserved byte-for-byte; editing is disabled until this field's type and serializer are verified."):reason;
            if(!field->referenceTable.isEmpty())return tr("Choose a referenced row from the list. Right-click and select Open Referenced Row to follow the link.");
            return tr("Preview limited to %1 of %2 characters. Double-click or press Enter/F2 to edit the complete value.").arg(data(index,Qt::DisplayRole).toString().size()).arg(full.size());
        }
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex& index)const override
    {
        Qt::ItemFlags result=QAbstractTableModel::flags(index);const SDatabaseRow* row=rowAt(index.row());if(index.column()==0)return result&~Qt::ItemIsEditable;const auto* field=fieldAt(index.column());if(!row||!field)return result;
        QString reason;const bool editable=field->writable&&m_document->fieldEditable(m_tableKey,row->stableKey,field->key,&reason);
        const auto* table=m_document->table(m_tableKey);const bool inlineEditor=field->type==EDatabaseFieldType::Integer||field->type==EDatabaseFieldType::Decimal||field->type==EDatabaseFieldType::Reference||suggestedChoiceField(*field,table);
        return editable&&inlineEditor?result|Qt::ItemIsEditable:result&~Qt::ItemIsEditable;
    }
    bool setData(const QModelIndex& index,const QVariant& value,int role=Qt::EditRole)override
    {
        if(role!=Qt::EditRole||!index.isValid()||index.column()==0||!m_commit)return false;const SDatabaseRow* row=rowAt(index.row());const auto* field=fieldAt(index.column());if(!row||!field)return false;
        const QString oldKey=row->stableKey;QVariant typed=value;if(field->type==EDatabaseFieldType::Integer)typed=value.toLongLong();else if(field->type==EDatabaseFieldType::Decimal)typed=value.toDouble();
        if(!m_commit(oldKey,field->key,typed))return false;
        const auto* table=m_document->table(m_tableKey);const QString identityKey=table&&table->identityFieldId>=0?QString("fld.%1.0").arg(table->identityFieldId):QString("name");
        if(field->key==identityKey)refresh();else{m_choices.remove(index.column());emit dataChanged(index,index,{Qt::DisplayRole,Qt::EditRole,Qt::ToolTipRole,RawValueRole,FullTextRole,FilterTextRole,ChoicesRole});}
        return true;
    }
    int rowForIdentity(const QString& identity)const
    {
        for(int i=0;i<m_rows.size();++i){const auto* row=rowAt(i);if(!row)continue;const QString display=m_document->displayIdentity(m_tableKey,row->stableKey);if(row->stableKey.compare(identity,Qt::CaseInsensitive)==0||display.compare(identity,Qt::CaseInsensitive)==0)return i;}return -1;
    }
    const SDatabaseFieldSchema* fieldForIndex(const QModelIndex& index)const{return fieldAt(index.column());}
    QString fieldKeyForColumn(int column)const{return column==0?QStringLiteral("#"):(fieldAt(column)?fieldAt(column)->key:QString());}
    int columnForFieldKey(const QString& key)const
    {for(int column=1;column<columnCount();++column)if(fieldKeyForColumn(column)==key)return column;return 0;}
    bool isOpaqueColumn(int column)const{const auto* field=fieldAt(column);return field&&field->opaque;}
    QString columnSource(int column)const
    {
        if(column==0)return QStringLiteral("editor");
        const auto* field=fieldAt(column);if(!field)return QString();
        return field->storageId<0||field->key.startsWith(QStringLiteral("jmv."),Qt::CaseInsensitive)
            ?QStringLiteral("tables.lua"):QStringLiteral("database.res");
    }
private:
    const SDatabaseRow* rowAt(int modelRow)const{const auto* table=m_document->table(m_tableKey);if(!table||modelRow<0||modelRow>=m_rows.size())return nullptr;const int row=m_rows[modelRow];return row>=0&&row<table->rows.size()?&table->rows[row]:nullptr;}
    const SDatabaseFieldSchema* fieldAt(int modelColumn)const{const auto* table=m_document->table(m_tableKey);const int mapped=modelColumn-1;if(!table||mapped<0||mapped>=m_fields.size())return nullptr;const int field=m_fields[mapped];return field>=0&&field<table->fields.size()?&table->fields[field]:nullptr;}
    static bool suggestedChoiceField(const SDatabaseFieldSchema& field,const SDatabaseTable* table)
    {
        if(!table||field.type!=EDatabaseFieldType::Text||field.opaque||field.storageId==table->identityFieldId)return false;const QString label=field.label.toLower();for(const QString& token:{QStringLiteral("type"),QStringLiteral("class"),QStringLiteral("kind"),QStringLiteral("material"),QStringLiteral("terrain"),QStringLiteral("damage"),QStringLiteral("target"),QStringLiteral("school")})if(label.contains(token))return true;return false;
    }
    void rebuildIndexes()
    {
        m_rows.clear();m_fields.clear();m_choices.clear();const auto* table=m_document->table(m_tableKey);if(!table)return;
        for(int field=0;field<table->fields.size();++field)m_fields<<field;
        for(int row=0;row<table->rows.size();++row)if(!table->rows[row].deleted)m_rows<<row;
    }
    QStringList choicesForColumn(int column)const
    {
        if(column==0)return {};if(m_choices.contains(column))return m_choices.value(column);const auto* table=m_document->table(m_tableKey);const auto* field=fieldAt(column);QStringList values;if(!table||!field)return values;
        if(!field->referenceTable.isEmpty()){const auto* target=m_document->table(field->referenceTable);if(target)for(const auto& row:target->rows)if(!row.deleted){const QString identity=m_document->displayIdentity(field->referenceTable,row.stableKey);if(!identity.isEmpty())values<<identity;}}
        else if(suggestedChoiceField(*field,table)){QSet<QString> unique;bool suitable=true;for(int rowIndex:m_rows){const QString value=table->rows[rowIndex].values.value(field->key).toString().trimmed();if(value.isEmpty())continue;if(value.size()>80||value.startsWith('[')||value.startsWith('{')){suitable=false;break;}unique.insert(value);if(unique.size()>64){suitable=false;break;}}if(suitable)values=unique.values();}
        values.removeDuplicates();std::sort(values.begin(),values.end(),[](const QString&a,const QString&b){return a.compare(b,Qt::CaseInsensitive)<0;});m_choices.insert(column,values);return values;
    }
    QSharedPointer<DatabaseDocument> m_document;QString m_tableKey;QVector<int> m_rows,m_fields;mutable QMap<int,QStringList> m_choices;CommitHandler m_commit;
};

class DatabaseFilterModel final : public QSortFilterProxyModel
{
public:
    explicit DatabaseFilterModel(QObject* parent=nullptr):QSortFilterProxyModel(parent){setDynamicSortFilter(true);setSortCaseSensitivity(Qt::CaseInsensitive);}
    void setSearchText(const QString& text){m_search=text.trimmed().toLower();invalidateFilter();}
protected:
    bool filterAcceptsRow(int row,const QModelIndex& parent)const override{return m_search.isEmpty()||sourceModel()->index(row,0,parent).data(FilterTextRole).toString().contains(m_search);}
    bool lessThan(const QModelIndex& left,const QModelIndex& right)const override
    {
        const QVariant a=left.data(RawValueRole),b=right.data(RawValueRole);if((a.type()==QVariant::Int||a.type()==QVariant::LongLong||a.type()==QVariant::Double)&&(b.type()==QVariant::Int||b.type()==QVariant::LongLong||b.type()==QVariant::Double))return a.toDouble()<b.toDouble();return a.toString().compare(b.toString(),Qt::CaseInsensitive)<0;
    }
private:QString m_search;
};

class DatabaseValueDelegate final : public QStyledItemDelegate
{
public:
    explicit DatabaseValueDelegate(QObject* parent=nullptr):QStyledItemDelegate(parent){}
    QWidget* createEditor(QWidget* parent,const QStyleOptionViewItem& option,const QModelIndex& index)const override
    {
        const QStringList choices=index.data(ChoicesRole).toStringList();if(choices.isEmpty())return QStyledItemDelegate::createEditor(parent,option,index);auto* combo=new QComboBox(parent);combo->setEditable(true);combo->setInsertPolicy(QComboBox::NoInsert);combo->addItems(choices);combo->setMaxVisibleItems(24);combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);combo->completer()->setFilterMode(Qt::MatchContains);return combo;
    }
    void setEditorData(QWidget* editor,const QModelIndex& index)const override{if(auto* combo=qobject_cast<QComboBox*>(editor)){combo->setEditText(index.data(Qt::EditRole).toString());combo->lineEdit()->selectAll();return;}QStyledItemDelegate::setEditorData(editor,index);}
    void setModelData(QWidget* editor,QAbstractItemModel* model,const QModelIndex& index)const override{if(auto* combo=qobject_cast<QComboBox*>(editor)){model->setData(index,combo->currentText(),Qt::EditRole);return;}QStyledItemDelegate::setModelData(editor,model,index);}
};

class DatabaseFrozenTableView final : public QTableView
{
public:
    explicit DatabaseFrozenTableView(QWidget* parent=nullptr):QTableView(parent)
    {
        m_frozen=new QTableView(this);
        m_frozen->setObjectName(QStringLiteral("databaseFrozenGrid"));
        m_frozen->setFocusPolicy(Qt::NoFocus);
        m_frozen->verticalHeader()->hide();
        m_frozen->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_frozen->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_frozen->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_frozen->setSelectionMode(QAbstractItemView::SingleSelection);
        m_frozen->setAlternatingRowColors(true);
        setWordWrap(false);m_frozen->setWordWrap(false);
        verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        verticalHeader()->setMinimumSectionSize(22);verticalHeader()->setDefaultSectionSize(22);
        m_frozen->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        m_frozen->verticalHeader()->setMinimumSectionSize(22);
        m_frozen->verticalHeader()->setDefaultSectionSize(22);
        m_frozen->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed|QAbstractItemView::SelectedClicked);
        viewport()->stackUnder(m_frozen);
        connect(horizontalHeader(),&QHeaderView::sectionResized,this,
            [this](int section,int,int size){m_frozen->setColumnWidth(section,size);updateFrozenGeometry();});
        connect(verticalHeader(),&QHeaderView::sectionResized,this,
            [this](int section,int,int size){m_frozen->setRowHeight(section,size);});
        connect(verticalScrollBar(),&QScrollBar::valueChanged,
                m_frozen->verticalScrollBar(),&QScrollBar::setValue);
        connect(m_frozen->verticalScrollBar(),&QScrollBar::valueChanged,
                verticalScrollBar(),&QScrollBar::setValue);
        connect(m_frozen->horizontalHeader(),&QHeaderView::sectionClicked,this,
                [this](int section){sortByColumn(section,
                    horizontalHeader()->sortIndicatorSection()==section
                    && horizontalHeader()->sortIndicatorOrder()==Qt::AscendingOrder
                        ?Qt::DescendingOrder:Qt::AscendingOrder);});
    }
    void setModel(QAbstractItemModel* value) override
    {
        QTableView::setModel(value);m_frozen->setModel(value);
        m_frozen->setSelectionModel(selectionModel());syncFrozenColumns();
    }
    void setSharedDelegate(QAbstractItemDelegate* delegate)
    {setItemDelegate(delegate);m_frozen->setItemDelegate(delegate);}
    QTableView* frozenView()const{return m_frozen;}
    int frozenColumnCount()const{return m_frozenColumns;}
    void setFrozenColumnCount(int count)
    {m_frozenColumns=qBound(0,count,model()?model()->columnCount():0);
     setProperty("frozenColumnCount",m_frozenColumns);syncFrozenColumns();}
    void syncFrozenColumns()
    {
        if(!model())return;
        for(int row=0;row<model()->rowCount();++row)
            m_frozen->setRowHeight(row,rowHeight(row));
        for(int column=0;column<model()->columnCount();++column)
        {
            m_frozen->setColumnHidden(column,column>=m_frozenColumns||isColumnHidden(column));
            m_frozen->setColumnWidth(column,columnWidth(column));
        }
        m_frozen->setVisible(m_frozenColumns>0);updateFrozenGeometry();
    }
    void scrollTo(const QModelIndex& index,ScrollHint hint=EnsureVisible)override
    {if(index.column()>=m_frozenColumns)QTableView::scrollTo(index,hint);}
protected:
    void resizeEvent(QResizeEvent* event)override
    {QTableView::resizeEvent(event);updateFrozenGeometry();}
    QModelIndex moveCursor(CursorAction action,Qt::KeyboardModifiers modifiers)override
    {
        const QModelIndex current=QTableView::moveCursor(action,modifiers);
        if(action==MoveLeft&&current.column()>=m_frozenColumns
           &&visualRect(current).left()<frozenWidth())
            horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                +visualRect(current).left()-frozenWidth());
        return current;
    }
private:
    int frozenWidth()const
    {
        int width=0;
        for(int column=0;model()&&column<m_frozenColumns;++column)
            if(!isColumnHidden(column))width+=columnWidth(column);
        return width;
    }
    void updateFrozenGeometry()
    {
        if(!m_frozen)return;
        const int width=frozenWidth();
        m_frozen->setGeometry(verticalHeader()->width()+frameWidth(),frameWidth(),
            width,viewport()->height()+horizontalHeader()->height());
    }
    QTableView* m_frozen{nullptr};
    int m_frozenColumns{0};
};

DatabaseEditorDialog::DatabaseEditorDialog(const QSharedPointer<DatabaseDocument>& document,QWidget* parent):QDialog(nullptr,Qt::Window|Qt::WindowSystemMenuHint|Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint),m_document(document)
{
    Q_UNUSED(parent);setWindowTitle(tr("Database Editor — %1").arg(document->profileId()));setWindowIcon(QApplication::windowIcon());setAttribute(Qt::WA_DeleteOnClose);resize(1180,720);setModal(false);
    auto* layout=new QVBoxLayout(this); auto* source=new QLabel(tr("Profile: %1  •  Provider: %2\nSource: %3").arg(document->profileId(),document->providerId(),document->sourceArchivePath()),this);source->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(source);
    auto* filterRow=new QWidget(this);auto* filterLayout=new QHBoxLayout(filterRow);filterLayout->setContentsMargins(0,0,0,0);
    m_filter=new QLineEdit(filterRow);m_filter->setObjectName(QStringLiteral("databaseRowFilter"));m_filter->setPlaceholderText(tr("Filter rows across all fields or enter #row…"));filterLayout->addWidget(m_filter,1);layout->addWidget(filterRow);
    auto* split=new QSplitter(this);m_tables=new QListWidget(split);m_tables->setMinimumWidth(190);m_grid=new DatabaseFrozenTableView(split);m_grid->setObjectName("databaseGrid");m_model=new DatabaseTableModel(document,m_grid);m_proxy=new DatabaseFilterModel(m_grid);m_proxy->setObjectName("databaseFilterModel");m_proxy->setSourceModel(m_model);m_grid->setModel(m_proxy);m_grid->setSharedDelegate(new DatabaseValueDelegate(m_grid));m_grid->setSortingEnabled(true);m_grid->setSelectionBehavior(QAbstractItemView::SelectRows);m_grid->setSelectionMode(QAbstractItemView::SingleSelection);m_grid->setAlternatingRowColors(true);m_grid->setContextMenuPolicy(Qt::CustomContextMenu);m_grid->frozenView()->setContextMenuPolicy(Qt::CustomContextMenu);m_grid->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed|QAbstractItemView::SelectedClicked);m_grid->verticalHeader()->setDefaultSectionSize(22);m_grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);m_grid->horizontalHeader()->setDefaultSectionSize(150);m_grid->horizontalHeader()->setMinimumSectionSize(45);m_grid->horizontalHeader()->setMaximumSectionSize(300);m_grid->horizontalHeader()->setSectionsMovable(false);m_grid->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);m_grid->frozenView()->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);split->setStretchFactor(1,1);layout->addWidget(split,1);
    auto* constructorButton=new QPushButton(tr("Item / Spell Constructor…"),this);constructorButton->setObjectName("databaseItemSpellConstructorButton");layout->addWidget(constructorButton,0,Qt::AlignLeft);connect(constructorButton,&QPushButton::clicked,this,[this](){emit itemSpellConstructorRequested(QString());});
    auto* buttons=new QDialogButtonBox(this);m_cloneButton=buttons->addButton(tr("Clone Row…"),QDialogButtonBox::ActionRole);m_deleteButton=buttons->addButton(tr("Delete Row"),QDialogButtonBox::ActionRole);auto* reviewButton=buttons->addButton(tr("Review Changes…"),QDialogButtonBox::ActionRole);auto* exportButton=buttons->addButton(tr("Export XLSX…"),QDialogButtonBox::ActionRole);auto* importButton=buttons->addButton(tr("Import XLSX…"),QDialogButtonBox::ActionRole);auto* applyButton=buttons->addButton(tr("Apply Database"),QDialogButtonBox::ActionRole);auto* saveButton=buttons->addButton(tr("Save Database As…"),QDialogButtonBox::ActionRole);saveButton->setObjectName("databaseSaveAsButton");buttons->addButton(QDialogButtonBox::Close);layout->addWidget(buttons);m_status=new QLabel(this);m_status->setWordWrap(true);layout->addWidget(m_status);
    importButton->setText(tr("Import…"));
    auto* importMenu=new QMenu(importButton);
    auto* importXlsxAction=importMenu->addAction(tr("Import XLSX…"));
    auto* importResAction=importMenu->addAction(tr("Import database.res…"));
    auto* importLuaAction=importMenu->addAction(tr("Import tables.lua…"));
    auto* importBundleAction=importMenu->addAction(tr("Import database.res + tables.lua…"));
    importLuaAction->setEnabled(!m_document->sourceTablesLuaPath().isEmpty());
    importBundleAction->setEnabled(!m_document->sourceTablesLuaPath().isEmpty());
    importButton->setMenu(importMenu);
    connect(importXlsxAction,&QAction::triggered,this,&DatabaseEditorDialog::importXlsx);
    connect(importResAction,&QAction::triggered,this,&DatabaseEditorDialog::importDatabaseRes);
    connect(importLuaAction,&QAction::triggered,this,&DatabaseEditorDialog::importTablesLua);
    connect(importBundleAction,&QAction::triggered,this,&DatabaseEditorDialog::importDatabaseBundle);
    for(const QString& key:document->tableKeys()){const auto* table=document->table(key);if(table->physicalTable<0)continue;auto* item=new QListWidgetItem(tr("%1 (%2)").arg(table->label).arg(table->rows.size()),m_tables);item->setData(Qt::UserRole,key);}
    m_model->setCommitHandler([this](const QString& row,const QString& field,const QVariant& value){return commitGridValue(row,field,value);});
    m_filterTimer=new QTimer(this);m_filterTimer->setSingleShot(true);m_filterTimer->setInterval(80);connect(m_filterTimer,&QTimer::timeout,this,[this](){m_proxy->setSearchText(m_filter->text());});connect(m_tables,&QListWidget::currentRowChanged,this,&DatabaseEditorDialog::selectTable);connect(m_filter,&QLineEdit::textChanged,this,[this](){m_filterTimer->start();});const auto editCell=[this](const QModelIndex& index){const QModelIndex source=m_proxy->mapToSource(index);const auto* field=m_model->fieldForIndex(source);if(!field)return;if(!field->referenceTable.isEmpty()||!source.data(ChoicesRole).toStringList().isEmpty())m_grid->edit(index);else editFieldDialog(index);};connect(m_grid,&QTableView::doubleClicked,this,editCell);connect(m_grid->frozenView(),&QTableView::doubleClicked,this,editCell);connect(m_grid,&QTableView::customContextMenuRequested,this,&DatabaseEditorDialog::showRowContextMenu);connect(m_grid->frozenView(),&QTableView::customContextMenuRequested,this,[this](const QPoint& point){const QModelIndex index=m_grid->frozenView()->indexAt(point);if(!index.isValid())return;m_grid->setCurrentIndex(index);m_grid->selectRow(index.row());showRowContextMenu(m_grid->visualRect(index).center());});const auto headerMenu=[this](QHeaderView* header,const QPoint& point){const int logical=header->logicalIndexAt(point);if(logical>=0)showColumnContextMenu(logical,header->mapToGlobal(point));};connect(m_grid->horizontalHeader(),&QHeaderView::customContextMenuRequested,this,[this,headerMenu](const QPoint& point){headerMenu(m_grid->horizontalHeader(),point);});connect(m_grid->frozenView()->horizontalHeader(),&QHeaderView::customContextMenuRequested,this,[this,headerMenu](const QPoint& point){headerMenu(m_grid->frozenView()->horizontalHeader(),point);});connect(m_grid->horizontalHeader(),&QHeaderView::sectionResized,this,[this](){m_grid->syncFrozenColumns();saveCurrentColumnState();});connect(m_grid->horizontalHeader(),&QHeaderView::sectionMoved,this,[this](){saveCurrentColumnState();});connect(m_cloneButton,&QPushButton::clicked,this,&DatabaseEditorDialog::cloneSelected);connect(m_deleteButton,&QPushButton::clicked,this,&DatabaseEditorDialog::deleteSelected);connect(reviewButton,&QPushButton::clicked,this,&DatabaseEditorDialog::review);connect(exportButton,&QPushButton::clicked,this,&DatabaseEditorDialog::exportXlsx);connect(importButton,&QPushButton::clicked,this,&DatabaseEditorDialog::importXlsx);connect(applyButton,&QPushButton::clicked,this,&DatabaseEditorDialog::apply);connect(saveButton,&QPushButton::clicked,this,&DatabaseEditorDialog::saveAs);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::close);
    auto*editFullValue=new QAction(tr("Edit Full Value…"),m_grid);editFullValue->setShortcuts({QKeySequence(Qt::Key_Return),QKeySequence(Qt::Key_Enter),QKeySequence(Qt::Key_F2)});editFullValue->setShortcutContext(Qt::WidgetShortcut);m_grid->addAction(editFullValue);connect(editFullValue,&QAction::triggered,this,[this](){if(m_grid->currentIndex().isValid())editFieldDialog(m_grid->currentIndex());});
    auto*openReference=new QAction(tr("Open Referenced Row"),m_grid);openReference->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Return));openReference->setShortcutContext(Qt::WidgetShortcut);m_grid->addAction(openReference);connect(openReference,&QAction::triggered,this,&DatabaseEditorDialog::openCurrentReference);
    auto*undoShortcut=new QShortcut(QKeySequence::Undo,this);undoShortcut->setContext(Qt::WindowShortcut);connect(undoShortcut,&QShortcut::activated,this,&DatabaseEditorDialog::undoRequested);
    auto*redoShortcut=new QShortcut(QKeySequence::Redo,this);redoShortcut->setContext(Qt::WindowShortcut);connect(redoShortcut,&QShortcut::activated,this,&DatabaseEditorDialog::redoRequested);
    if(m_tables->count())m_tables->setCurrentRow(0);updateStatus();
}
DatabaseEditorDialog::~DatabaseEditorDialog(){saveCurrentColumnState();}
QString DatabaseEditorDialog::columnSettingsKey(const QString& suffix)const
{
    QString scope=m_document->profileId()+QLatin1Char('/')+currentTableKey();
    scope.replace(QLatin1Char('\\'),QLatin1Char('_'));scope.replace(QLatin1Char('/'),QLatin1Char('_'));
    return QStringLiteral("databaseEditor/columns/%1/%2").arg(scope,suffix);
}
void DatabaseEditorDialog::saveCurrentColumnState()
{
    if(!m_grid||!m_model||currentTableKey().isEmpty()||m_model->tableKey()!=currentTableKey())return;
    QSettings settings;settings.setValue(columnSettingsKey("headerState"),m_grid->horizontalHeader()->saveState());
    const int count=m_grid->frozenColumnCount();settings.setValue(columnSettingsKey("frozenThrough"),
        count>0?m_model->fieldKeyForColumn(count-1):QString());
}
void DatabaseEditorDialog::restoreCurrentColumnState()
{
    if(!m_grid||!m_model||currentTableKey().isEmpty())return;
    QSettings settings;const QByteArray state=settings.value(columnSettingsKey("headerState")).toByteArray();
    const QString frozen=settings.value(columnSettingsKey("frozenThrough")).toString();
    if(!state.isEmpty()){const QSignalBlocker blocker(m_grid->horizontalHeader());m_grid->horizontalHeader()->restoreState(state);}
    int frozenCount=0;for(int column=0;column<m_model->columnCount();++column)
        if(m_model->fieldKeyForColumn(column)==frozen){frozenCount=column+1;break;}
    m_grid->setFrozenColumnCount(frozenCount);m_grid->syncFrozenColumns();
}
void DatabaseEditorDialog::showColumnContextMenu(int logicalColumn,const QPoint& globalPosition)
{
    if(logicalColumn<0||logicalColumn>=m_model->columnCount())return;
    PersistentColumnMenu menu(this);menu.setObjectName(QStringLiteral("databaseColumnMenu"));
    const auto stayOpen=[](QAction* action){action->setProperty("databaseStayOpen",true);return action;};
    QAction* lock=menu.addAction(tr("Lock columns through “%1”")
        .arg(m_model->headerData(logicalColumn,Qt::Horizontal,Qt::DisplayRole).toString()));
    stayOpen(lock);
    lock->setObjectName(QStringLiteral("databaseLockColumnsAction"));
    connect(lock,&QAction::triggered,this,[this,logicalColumn](){m_grid->setFrozenColumnCount(logicalColumn+1);saveCurrentColumnState();});
    QAction* unlock=stayOpen(menu.addAction(tr("Unlock columns")));unlock->setObjectName(QStringLiteral("databaseUnlockColumnsAction"));unlock->setEnabled(m_grid->frozenColumnCount()>0);
    connect(unlock,&QAction::triggered,this,[this](){m_grid->setFrozenColumnCount(0);saveCurrentColumnState();});
    menu.addSeparator();
    const auto addSourceMenu=[&](const QString& source,const QString& title,const QString& objectName)
    {
        bool present=false;for(int column=0;column<m_model->columnCount();++column)present|=m_model->columnSource(column)==source;if(!present)return;
        auto* columns=new PersistentColumnMenu(&menu);columns->setTitle(title);columns->setObjectName(objectName);menu.addMenu(columns);
        QAction* showSource=stayOpen(columns->addAction(tr("Show all")));showSource->setObjectName(QStringLiteral("databaseShowAllColumns_%1").arg(source));
        QAction* hideSource=stayOpen(columns->addAction(tr("Hide all")));hideSource->setObjectName(QStringLiteral("databaseHideAllColumns_%1").arg(source));
        if(source==QStringLiteral("database.res"))
        {
            QAction* showUnknown=stayOpen(columns->addAction(tr("Show unknown/raw columns")));showUnknown->setObjectName(QStringLiteral("databaseShowUnknownColumnsAction"));connect(showUnknown,&QAction::triggered,this,[this](){for(int column=0;column<m_model->columnCount();++column)if(m_model->isOpaqueColumn(column))m_grid->setColumnHidden(column,false);m_grid->syncFrozenColumns();saveCurrentColumnState();});
            QAction* hideUnknown=stayOpen(columns->addAction(tr("Hide unknown/raw columns")));hideUnknown->setObjectName(QStringLiteral("databaseHideUnknownColumnsAction"));connect(hideUnknown,&QAction::triggered,this,[this](){for(int column=0;column<m_model->columnCount();++column)if(m_model->isOpaqueColumn(column))m_grid->setColumnHidden(column,true);m_grid->syncFrozenColumns();saveCurrentColumnState();});
        }
        columns->addSeparator();
        QVector<QAction*> visibilityActions;
        for(int column=0;column<m_model->columnCount();++column)if(m_model->columnSource(column)==source)
        {
            QAction* visible=stayOpen(columns->addAction(m_model->headerData(column,Qt::Horizontal,Qt::DisplayRole).toString()));visible->setObjectName(QStringLiteral("databaseColumnVisibilityAction_%1").arg(column));visible->setCheckable(true);visible->setChecked(!m_grid->isColumnHidden(column));visible->setData(column);visibilityActions.append(visible);connect(visible,&QAction::toggled,this,[this,column](bool checked){m_grid->setColumnHidden(column,!checked);m_grid->syncFrozenColumns();saveCurrentColumnState();});
        }
        const auto setSourceVisible=[this,visibilityActions](bool visible){for(QAction* action:visibilityActions){const QSignalBlocker blocker(action);action->setChecked(visible);m_grid->setColumnHidden(action->data().toInt(),!visible);}m_grid->syncFrozenColumns();saveCurrentColumnState();};
        connect(showSource,&QAction::triggered,this,[setSourceVisible](){setSourceVisible(true);});
        connect(hideSource,&QAction::triggered,this,[setSourceVisible](){setSourceVisible(false);});
    };
    addSourceMenu(QStringLiteral("database.res"),tr("database.res"),QStringLiteral("databaseResColumnsSubmenu"));
    addSourceMenu(QStringLiteral("tables.lua"),tr("tables.lua (JMV)"),QStringLiteral("databaseLuaColumnsSubmenu"));
    addSourceMenu(QStringLiteral("editor"),tr("Editor columns"),QStringLiteral("databaseEditorColumnsSubmenu"));
    menu.exec(globalPosition);
}
QString DatabaseEditorDialog::currentTableKey()const{auto*i=m_tables->currentItem();return i?i->data(Qt::UserRole).toString():QString();}
QString DatabaseEditorDialog::selectedRowKey()const{return m_grid->currentIndex().isValid()?m_grid->currentIndex().data(RowKeyRole).toString():QString();}
QString DatabaseEditorDialog::runtimeRowIdentity(const QString& tableKey,const QString& rowKey)const{return tableKey==QStringLiteral("units.monsters")?m_document->displayIdentity(tableKey,rowKey):rowKey;}
void DatabaseEditorDialog::selectTable(){rebuildGrid();}
bool DatabaseEditorDialog::selectPrototype(const QString& prototype)
{
    return selectTableRow("units.monsters",prototype);
}
bool DatabaseEditorDialog::selectFinding(const QString& tableKey,const QString& rowKey)
{
    return selectTableRow(tableKey,rowKey);
}
bool DatabaseEditorDialog::selectTableRow(const QString& tableKey,const QString& identity)
{
    if(identity.trimmed().isEmpty())return false;
    if(!m_filter->text().isEmpty()){m_filter->clear();m_filterTimer->stop();m_proxy->setSearchText(QString());}
    bool tableFound=false;for(int i=0;i<m_tables->count();++i)if(m_tables->item(i)->data(Qt::UserRole).toString().compare(tableKey,Qt::CaseInsensitive)==0){m_tables->setCurrentRow(i);tableFound=true;break;}if(!tableFound)return false;
    if(m_model->tableKey().compare(tableKey,Qt::CaseInsensitive)!=0){saveCurrentColumnState();{const QSignalBlocker blocker(m_grid->horizontalHeader());m_model->setTable(tableKey);}restoreCurrentColumnState();m_proxy->setSearchText(QString());}
    const int sourceRow=m_model->rowForIdentity(identity);if(sourceRow<0)return false;const auto*table=m_document->table(tableKey);const int identityColumn=table?m_model->columnForFieldKey(QStringLiteral("fld.%1.0").arg(table->identityFieldId)):0;const QModelIndex source=m_model->index(sourceRow,identityColumn),view=m_proxy->mapFromSource(source);if(!view.isValid())return false;m_grid->selectRow(view.row());m_grid->setCurrentIndex(view);m_grid->scrollTo(view,QAbstractItemView::PositionAtCenter);m_grid->setFocus();return true;
}
void DatabaseEditorDialog::rebuildGrid()
{
    const auto* table=m_document->table(currentTableKey());if(!table)return;
    m_cloneButton->setEnabled(table->structuralEditing);m_deleteButton->setEnabled(table->structuralEditing);
    const QString structuralReason=table->structuralEditing?QString():tr("This complete/raw table is visible for preservation review. Structural editing is disabled until its typed serializer is verified.");
    m_cloneButton->setToolTip(structuralReason);m_deleteButton->setToolTip(structuralReason);const QString selected=selectedRowKey();const bool changedTable=m_model->tableKey()!=table->key;if(changedTable)saveCurrentColumnState();if(changedTable){const QSignalBlocker blocker(m_grid->horizontalHeader());m_model->setTable(table->key);}else m_model->setTable(table->key);m_proxy->setSearchText(m_filter->text());if(changedTable)restoreCurrentColumnState();if(m_model->columnCount()>0&&!QSettings().contains(columnSettingsKey("headerState")))m_grid->setColumnWidth(0,55);if(!selected.isEmpty()){const int row=m_model->rowForIdentity(selected);if(row>=0){const QModelIndex view=m_proxy->mapFromSource(m_model->index(row,0));if(view.isValid()){m_grid->selectRow(view.row());m_grid->setCurrentIndex(view);}}}m_grid->syncFrozenColumns();updateStatus();
}
bool DatabaseEditorDialog::commitGridValue(const QString& rowKey,const QString& fieldKey,const QVariant& value)
{
    const auto* table=m_document->table(currentTableKey());if(!table)return false;QString diagnostic;const int before=m_document->changes().size();if(!m_document->setValue(table->key,rowKey,fieldKey,value,&diagnostic)){QMessageBox::warning(this,tr("Edit rejected"),diagnostic);return false;}if(m_document->changes().size()==before)return true;publishLive();emit transactionCommitted(m_document->changes().last().description,table->key,fieldKey=="name"?QString():runtimeRowIdentity(table->key,rowKey));updateStatus();return true;
}
void DatabaseEditorDialog::editFieldDialog(const QModelIndex& viewIndex)
{
    if(!viewIndex.isValid())return;const QModelIndex source=m_proxy->mapToSource(viewIndex);const QModelIndex* item=&source;const auto*table=m_document->table(currentTableKey());if(!table||!source.isValid())return;const QString rowKey=source.data(RowKeyRole).toString(),fieldKey=source.data(FieldKeyRole).toString();const SDatabaseFieldSchema*schema=m_model->fieldForIndex(source);QString reason;if(!schema||!schema->writable||!m_document->fieldEditable(table->key,rowKey,fieldKey,&reason)||!(schema->type==EDatabaseFieldType::Text||schema->type==EDatabaseFieldType::TextList||schema->type==EDatabaseFieldType::Reference))return;
    QDialog dialog(this);dialog.setObjectName("databaseFieldEditorDialog");dialog.setWindowTitle(tr("Edit %1").arg(schema->label));QScreen*screen=QGuiApplication::screenAt(mapToGlobal(rect().center()));const QRect available=screen?screen->availableGeometry():QRect(0,0,1024,768);const int width=qMax(360,qMin(720,available.width()-80)),height=qMax(240,qMin(460,available.height()-100));dialog.setMinimumSize(qMin(440,width),qMin(280,height));dialog.resize(width,height);auto*layout=new QVBoxLayout(&dialog);const QString identity=m_document->displayIdentity(table->key,rowKey);auto*label=new QLabel(tr("%1 — %2").arg(identity.isEmpty()?rowKey:identity,schema->label),&dialog);label->setWordWrap(true);layout->addWidget(label);auto*editor=new QPlainTextEdit(&dialog);editor->setObjectName("databaseFieldEditorText");editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));editor->setPlainText(item->data(Qt::UserRole+2).toString());QVariant storedForEditor;for(const auto& candidate:table->rows)if(candidate.stableKey==rowKey){storedForEditor=candidate.values.value(fieldKey);break;}QStringList parsedList;const bool listEditor=schema->type==EDatabaseFieldType::TextList||jsonStringList(storedForEditor.toString(),&parsedList);editor->setToolTip(listEditor?tr("One list entry per line. The brackets and quotation marks used internally are intentionally hidden."):tr("This is the complete stored value. Long lines wrap visually."));layout->addWidget(editor,1);auto*count=new QLabel(&dialog);count->setObjectName("databaseFieldEditorCount");layout->addWidget(count);auto updateCount=[count,editor,listEditor,this](){count->setText(listEditor?tr("%1 item(s) — one per line").arg(editor->toPlainText().split(QRegExp("[\\r\\n]+"),QString::SkipEmptyParts).size()):tr("%1 character(s) — complete value").arg(editor->toPlainText().size()));};connect(editor,&QPlainTextEdit::textChanged,&dialog,updateCount);updateCount();auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->setObjectName("databaseFieldEditorButtons");buttons->button(QDialogButtonBox::Ok)->setDefault(true);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);editor->selectAll();editor->setFocus();if(dialog.exec()!=QDialog::Accepted)return;
    const QVariant original=table->rows.isEmpty()?QVariant():[&](){for(const auto& candidate:table->rows)if(candidate.stableKey==rowKey)return candidate.values.value(fieldKey);return QVariant();}();const QVariant value=editorValue(*table,*schema,original,editor->toPlainText());if(!commitGridValue(rowKey,fieldKey,value))return;m_model->refresh();m_proxy->setSearchText(m_filter->text());
}
void DatabaseEditorDialog::showRowContextMenu(const QPoint& position)
{
    const QModelIndex index=m_grid->indexAt(position);if(!index.isValid())return;m_grid->setCurrentIndex(index);m_grid->selectRow(index.row());const auto*table=m_document->table(currentTableKey());auto*menu=new QMenu(m_grid);menu->setAttribute(Qt::WA_DeleteOnClose);menu->setObjectName("databaseRowContextMenu");QAction*copy=menu->addAction(tr("Copy Row ID"));copy->setObjectName("databaseCopyRowIdAction");connect(copy,&QAction::triggered,this,&DatabaseEditorDialog::copySelectedRowId);const QModelIndex source=m_proxy->mapToSource(index);const QString referenceTable=source.data(ReferenceTableRole).toString(),referenceIdentity=source.data(Qt::EditRole).toString().trimmed();if(!referenceTable.isEmpty()&&!referenceIdentity.isEmpty()){QAction*open=menu->addAction(tr("Open Referenced Row"));open->setObjectName("databaseOpenReferenceAction");connect(open,&QAction::triggered,this,[this,referenceTable,referenceIdentity](){if(!selectTableRow(referenceTable,referenceIdentity))QMessageBox::information(this,tr("Reference not found"),tr("The referenced row '%1' is not present in %2.").arg(referenceIdentity,referenceTable));});}QAction*clone=menu->addAction(tr("Duplicate / Clone Row…"));clone->setObjectName("databaseCloneRowAction");clone->setEnabled(table&&table->structuralEditing);connect(clone,&QAction::triggered,this,&DatabaseEditorDialog::cloneSelected);menu->popup(m_grid->viewport()->mapToGlobal(position));
}
void DatabaseEditorDialog::openCurrentReference(){const QModelIndex view=m_grid->currentIndex();if(!view.isValid())return;const QModelIndex source=m_proxy->mapToSource(view);const QString table=source.data(ReferenceTableRole).toString(),identity=source.data(Qt::EditRole).toString().trimmed();if(table.isEmpty()||identity.isEmpty())return;if(!selectTableRow(table,identity))QMessageBox::information(this,tr("Reference not found"),tr("The referenced row '%1' is not present in %2.").arg(identity,table));}
void DatabaseEditorDialog::copySelectedRowId(){const QString key=selectedRowKey();if(key.isEmpty())return;const QString identity=m_document->displayIdentity(currentTableKey(),key);QApplication::clipboard()->setText(identity.isEmpty()?key:identity);}
void DatabaseEditorDialog::cloneSelected(){const QString key=selectedRowKey(),tableKey=currentTableKey();if(key.isEmpty())return;const QString originalName=m_document->displayIdentity(tableKey,key);QDialog dialog(this);dialog.setWindowTitle(tr("Clone row"));auto*layout=new QVBoxLayout(&dialog);auto*label=new QLabel(tr("New stable name / ID:"),&dialog);auto*name=new QLineEdit(originalName.isEmpty()?key:originalName,&dialog);auto*reason=new QLabel(&dialog);reason->setWordWrap(true);auto*buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(label);layout->addWidget(name);layout->addWidget(reason);layout->addWidget(buttons);auto update=[&](){const QString value=name->text().trimmed();const bool unchanged=value.compare(key,Qt::CaseInsensitive)==0;const bool available=m_document->identityAvailable(tableKey,value);buttons->button(QDialogButtonBox::Ok)->setEnabled(!unchanged&&available);reason->setText(value.isEmpty()?tr("The ID cannot be empty."):unchanged?tr("Enter a new ID; the original ID is already in use."):!available?tr("That ID is already used by another row."):tr("The new ID is available."));};connect(name,&QLineEdit::textChanged,&dialog,[&](const QString&){update();});connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);update();name->selectAll();name->setFocus();if(dialog.exec()!=QDialog::Accepted)return;const QString newId=name->text().trimmed();QString d;if(!m_document->cloneRow(tableKey,key,newId,&d))QMessageBox::warning(this,tr("Clone rejected"),d);else{publishLive();emit transactionCommitted(m_document->changes().last().description,tableKey,newId);}m_model->refresh();m_proxy->setSearchText(m_filter->text());selectTableRow(tableKey,newId);updateStatus();}
void DatabaseEditorDialog::deleteSelected(){QString key=selectedRowKey();if(key.isEmpty())return;if(QMessageBox::question(this,tr("Delete row"),tr("Mark '%1' for deletion? References will be validated before publication.").arg(key))!=QMessageBox::Yes)return;const QString tableKey=currentTableKey(),runtimeKey=runtimeRowIdentity(tableKey,key);QString d;if(!m_document->deleteRow(tableKey,key,&d))QMessageBox::warning(this,tr("Delete rejected"),d);else{publishLive();emit transactionCommitted(m_document->changes().last().description,tableKey,runtimeKey);}rebuildGrid();}
void DatabaseEditorDialog::publishLive(){QString diagnostic;const auto snapshot=m_document->currentSnapshot(&diagnostic);if(snapshot)ProfileDatabaseContext::instance().publishEditedSnapshot(snapshot);else QMessageBox::warning(this,tr("Database refresh failed"),diagnostic);}
void DatabaseEditorDialog::refreshFromHistory(){publishLive();rebuildGrid();}
void DatabaseEditorDialog::review(){QString text;QSet<QString> seen;for(const auto&c:m_document->changes())text+=QString("• %1\n").arg(c.description);for(const auto&f:m_document->validate())if(!seen.contains(f.message)){seen.insert(f.message);text+=QString("\n%1 %2").arg(f.verified?tr("Verified:"):tr("Opaque:"),f.message);}if(text.isEmpty())text=tr("No pending changes.");QDialog dialog(this);dialog.setWindowFlags(Qt::Dialog|Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowMinMaxButtonsHint|Qt::WindowCloseButtonHint);dialog.setWindowTitle(tr("Pending Database Changes"));QScreen* activeScreen=QGuiApplication::screenAt(mapToGlobal(rect().center()));if(!activeScreen)activeScreen=QGuiApplication::primaryScreen();const QRect available=activeScreen?activeScreen->availableGeometry():QRect(0,0,1024,768);dialog.resize(qMin(760,available.width()-80),qMin(520,available.height()-100));dialog.setMinimumSize(420,280);auto* layout=new QVBoxLayout(&dialog);auto* review=new QPlainTextEdit(&dialog);review->setReadOnly(true);review->setLineWrapMode(QPlainTextEdit::NoWrap);review->setPlainText(text);layout->addWidget(review);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::accept);layout->addWidget(buttons);dialog.exec();}
bool DatabaseEditorDialog::applyDocument(QString* backupPath,QString* diagnostic){CBackupManager backups;QStringList backupPaths;const SBackupResult backup=backups.snapshotBeforeSave(QFileInfo(m_document->sourceArchivePath()));if(!backup.success&&!backup.skipped){if(diagnostic)*diagnostic=backup.error;return false;}backupPaths<<backup.backupPath;if(!m_document->sourceTablesLuaPath().isEmpty()){const SBackupResult luaBackup=backups.snapshotBeforeSave(QFileInfo(m_document->sourceTablesLuaPath()));if(!luaBackup.success&&!luaBackup.skipped){if(diagnostic)*diagnostic=luaBackup.error;return false;}backupPaths<<luaBackup.backupPath;}if(!m_document->applyToSource(diagnostic))return false;if(backupPath)*backupPath=backupPaths.join('\n');return true;}
void DatabaseEditorDialog::apply(){QString backup,diagnostic;if(!applyDocument(&backup,&diagnostic)){QMessageBox::critical(this,tr("Database apply failed"),diagnostic);return;}QMessageBox::information(this,tr("Database applied"),tr("The active database and linked JMV Lua data were updated as one recoverable transaction.\n\nBackups:\n%1").arg(backup));}
void DatabaseEditorDialog::saveAs(){const QString suggested=QFileInfo(m_document->sourceArchivePath()).fileName();const QString path=QFileDialog::getSaveFileName(this,tr("Save Database As"),suggested,tr("Evil Islands database (*.res)"));if(path.isEmpty())return;if(QFileInfo(path).canonicalFilePath().compare(QFileInfo(m_document->sourceArchivePath()).canonicalFilePath(),Qt::CaseInsensitive)==0){QMessageBox::warning(this,tr("Choose a staging path"),tr("Saving over the opened database is blocked. Choose a separate staging path so the source remains recoverable."));return;}QString luaPath;if(!m_document->sourceTablesLuaPath().isEmpty()){luaPath=QFileDialog::getSaveFileName(this,tr("Save linked JMV tables.lua As"),QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral("tables.lua")),tr("Lua table (*.lua)"));if(luaPath.isEmpty())return;if(QFileInfo(luaPath).canonicalFilePath().compare(QFileInfo(m_document->sourceTablesLuaPath()).canonicalFilePath(),Qt::CaseInsensitive)==0){QMessageBox::warning(this,tr("Choose a staging path"),tr("Saving over the opened tables.lua is blocked. Choose a separate staging path so the source remains recoverable."));return;}}QString diagnostic;const bool saved=luaPath.isEmpty()?m_document->saveArchive(path,&diagnostic):m_document->saveBundle(path,luaPath,&diagnostic);if(!saved){QMessageBox::critical(this,tr("Database save failed"),diagnostic);return;}const QString destinations=luaPath.isEmpty()?path:tr("%1\n%2").arg(path,luaPath);QMessageBox::information(this,tr("Database saved"),tr("The database bundle was staged successfully at:\n%1\n\nThe source files were not modified.").arg(destinations));}
void DatabaseEditorDialog::exportXlsx()
{
    const QString path=QFileDialog::getSaveFileName(this,tr("Export Database XLSX"),QStringLiteral("database.xlsx"),tr("Excel workbook (*.xlsx)"));
    if(path.isEmpty())return;
    QString diagnostic;
    if(!DatabaseWorkbook::exportXlsx(*m_document,path,&diagnostic))
    {QMessageBox::critical(this,tr("XLSX export failed"),diagnostic);return;}
    if(!m_document->sourceTablesLuaPath().isEmpty())
    {
        const QString luaPath=QDir(QFileInfo(path).absolutePath()).filePath(
            QFileInfo(path).completeBaseName()+QStringLiteral(".tables.lua"));
        QSaveFile lua(luaPath);const QByteArray bytes=m_document->buildTablesLua(&diagnostic);
        if(bytes.isEmpty()||!lua.open(QIODevice::WriteOnly)||lua.write(bytes)!=bytes.size()||!lua.commit())
            QMessageBox::warning(this,tr("JMV Lua companion export failed"),
                tr("The XLSX was exported, but its editable Lua companion could not be written.\n\n%1").arg(diagnostic.isEmpty()?lua.errorString():diagnostic));
    }
}
void DatabaseEditorDialog::finishImport(const QString& warningTitle,const QString& description)
{
    publishLive();rebuildGrid();emit transactionCommitted(description,QString(),QString());
    if(!m_document->validate().isEmpty())
        QMessageBox::warning(this,warningTitle,tr("The import was applied as one undoable change. Existing source-data findings are retained for review; only newly introduced critical issues cancel import."));
}
void DatabaseEditorDialog::importXlsx()
{
    const QString path=QFileDialog::getOpenFileName(this,tr("Import Database XLSX"),QString(),tr("Excel workbook (*.xlsx)"));
    if(path.isEmpty())return;
    SDatabaseWorkbookImport imported;QString diagnostic;
    if(!DatabaseWorkbook::importXlsx(*m_document,path,&imported,&diagnostic))
    {QMessageBox::critical(this,tr("XLSX import cancelled"),tr("No database changes were applied.\n\n%1").arg(diagnostic));return;}
    QByteArray luaBytes=DatabaseWorkbook::embeddedTablesLua(path);
    const QString companion=QDir(QFileInfo(path).absolutePath()).filePath(
        QFileInfo(path).completeBaseName()+QStringLiteral(".tables.lua"));
    if(QFileInfo::exists(companion)){QFile lua(companion);if(lua.open(QIODevice::ReadOnly))luaBytes=lua.readAll();}
    const QString description=tr("Imported %1").arg(QFileInfo(path).fileName());
    if(!m_document->replaceTablesWithLua(imported.tables,luaBytes,description,&diagnostic,true))
    {QMessageBox::critical(this,tr("XLSX import cancelled"),tr("No database changes were applied.\n\n%1").arg(diagnostic));return;}
    finishImport(tr("XLSX import completed with warnings"),description);
}
bool DatabaseEditorDialog::importNative(const QByteArray& databaseBytes,const QByteArray& luaBytes,const QString& description)
{
    QString diagnostic;
    if(!m_document->importNativeBundle(databaseBytes,luaBytes,description,&diagnostic))
    {QMessageBox::critical(this,tr("Native database import cancelled"),tr("No database or Lua changes were applied.\n\n%1").arg(diagnostic));return false;}
    finishImport(tr("Native import completed with warnings"),description);return true;
}
void DatabaseEditorDialog::importDatabaseRes()
{
    const QString path=QFileDialog::getOpenFileName(this,tr("Import database.res"),QString(),tr("Evil Islands database (*.res)"));if(path.isEmpty())return;
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){QMessageBox::critical(this,tr("Native database import cancelled"),file.errorString());return;}
    importNative(file.readAll(),QByteArray(),tr("Imported %1").arg(QFileInfo(path).fileName()));
}
void DatabaseEditorDialog::importTablesLua()
{
    const QString path=QFileDialog::getOpenFileName(this,tr("Import tables.lua"),QString(),tr("Lua table (*.lua)"));if(path.isEmpty())return;
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){QMessageBox::critical(this,tr("Native database import cancelled"),file.errorString());return;}
    importNative(QByteArray(),file.readAll(),tr("Imported %1").arg(QFileInfo(path).fileName()));
}
void DatabaseEditorDialog::importDatabaseBundle()
{
    const QString databasePath=QFileDialog::getOpenFileName(this,tr("Import database.res"),QString(),tr("Evil Islands database (*.res)"));if(databasePath.isEmpty())return;
    const QString luaPath=QFileDialog::getOpenFileName(this,tr("Import linked tables.lua"),QFileInfo(databasePath).absolutePath(),tr("Lua table (*.lua)"));if(luaPath.isEmpty())return;
    QFile database(databasePath),lua(luaPath);if(!database.open(QIODevice::ReadOnly)||!lua.open(QIODevice::ReadOnly)){QMessageBox::critical(this,tr("Native database import cancelled"),tr("The selected database/Lua pair could not be read."));return;}
    importNative(database.readAll(),lua.readAll(),tr("Imported %1 + %2").arg(QFileInfo(databasePath).fileName(),QFileInfo(luaPath).fileName()));
}
void DatabaseEditorDialog::updateStatus(){m_status->setText(tr("%1 pending change(s) • Use Review or the Validation panel for current findings • Full schema authoring: 26 database tables. Descriptor-unknown fields remain visible as hexadecimal and serialize without touching unrelated data.").arg(m_document->changes().size()));}
}
