#include "bodypartedit_form.h"
#include "ui_bodypartedit_form.h"
#include "layout_components/bodypart_checkbox.h"
#include <QSharedPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QSettings>
#include <QHeaderView>
#include <QRegularExpression>
#include <QSet>
#include <QMouseEvent>
#include <QItemSelectionModel>
#include <QTimer>

namespace {

class CBodyPartStateItem final : public QTableWidgetItem
{
public:
    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(Qt::UserRole).toInt() < other.data(Qt::UserRole).toInt();
    }
};

QString stateSearchText(Qt::CheckState state)
{
    if(state == Qt::Checked)
        return QStringLiteral("checked true on enabled");
    if(state == Qt::Unchecked)
        return QStringLiteral("unchecked false off disabled");
    return QStringLiteral("mixed partial undefined");
}

int rowForPart(QTableWidget* table, const QString& part)
{
    for(int row = 0; row < table->rowCount(); ++row)
        if(table->item(row, 0)
           && table->item(row, 0)->text().compare(part, Qt::CaseInsensitive) == 0)
            return row;
    return -1;
}

int rowForCheckBox(QTableWidget* table, const QObject* checkBox)
{
    for(int row = 0; row < table->rowCount(); ++row)
        if(table->cellWidget(row, 1) == checkBox)
            return row;
    return -1;
}

QStringList selectedPartNames(QTableWidget* table)
{
    QStringList parts;
    const QModelIndexList selectedRows =
        table->selectionModel()->selectedRows(0);
    for(const QModelIndex& selected : selectedRows)
        if(QTableWidgetItem* item = table->item(selected.row(), 0))
            parts.append(item->text());
    return parts;
}

}

CBodyPartEditForm::CBodyPartEditForm(QWidget *parent) :
    QWidget(parent)
  ,ui(new Ui::CBodyPartEditForm)
  ,m_clipboardFile(QString("%1%2%3").arg(QDir::tempPath()).arg(QDir::separator()).arg("bodypart_buffer.json"))
{
    ui->setupUi(this);
    setWindowTitle(tr("Model part visibility"));
    const QSize savedSize = QSettings().value(
        "ui/bodyPartEditorSize", QSize(384, 559)).toSize();
    if(savedSize.isValid()) resize(savedSize);
    connect(ui->lineFilter, &QLineEdit::textChanged,
            this, &CBodyPartEditForm::applyFilter);
    connect(ui->tablePart->selectionModel(),
            &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection&, const QItemSelection&) {
        const QStringList selected = selectedPartNames(ui->tablePart);
        if(selected.size() > 1)
        {
            m_recentMultiSelectionParts = selected;
            return;
        }

        // QTableWidget can collapse a row selection before an embedded
        // checkbox receives the same mouse press. Retain the prior selection
        // through that event dispatch, but do not leave it available for a
        // later, unrelated click.
        QTimer::singleShot(0, this, [this] {
            if(m_pendingToggleParts.isEmpty()
               && selectedPartNames(ui->tablePart).size() < 2)
                m_recentMultiSelectionParts.clear();
        });
    });
}

CBodyPartEditForm::~CBodyPartEditForm()
{
    delete ui;
}

void CBodyPartEditForm::setPartData(const QSharedPointer<IPropertyBase>& prop)
{
    reset();
    m_prop.reset(prop->clone());
    populateTable();
}

void CBodyPartEditForm::populateTable()
{
    ui->tablePart->setSortingEnabled(false);
    ui->tablePart->setRowCount(0);
    propBodyPart* parts = dynamic_cast<propBodyPart*>(m_prop.get());
    const auto arrStat = parts->value();
    int i(0);
    for(auto& item: arrStat.toStdMap())
    {
        ui->tablePart->insertRow(i);
        ui->tablePart->setItem(i, 0, new QTableWidgetItem(item.first));
        auto pCheck = new CBodyPartCheckBox(ui->tablePart);
        auto state = Qt::CheckState::PartiallyChecked;
        if(item.second->isInit())
            state = item.second->value() ? Qt::CheckState::Checked : Qt::CheckState::Unchecked;
        pCheck->setCheckState(state);
        ui->tablePart->setCellWidget(i, 1, pCheck);
        pCheck->installEventFilter(this);
        QTableWidgetItem* checkSort = new CBodyPartStateItem;
        checkSort->setData(Qt::UserRole, int(state));
        checkSort->setFlags(checkSort->flags() & ~Qt::ItemIsEditable);
        ui->tablePart->setItem(i, 1, checkSort);
        connect(pCheck, &QCheckBox::stateChanged, this,
                [this, checkSort](int changedState) {
            const Qt::CheckState state = Qt::CheckState(changedState);
            checkSort->setData(Qt::UserRole, int(state));
            applyFilter(ui->lineFilter->text());
        });
        const int occurrences = parts->serializedOccurrenceCount(item.first);
        const bool recognized = parts->isRecognizedPart(item.first);
        QString sourceText;
        if(occurrences > 0)
            sourceText = recognized
                ? (occurrences == 1 ? tr("Serialized")
                                    : tr("Serialized x%1").arg(occurrences))
                : (occurrences == 1 ? tr("Unknown serialized")
                                    : tr("Unknown serialized x%1").arg(occurrences));
        else
            sourceText = tr("Model option");
        QTableWidgetItem* source = new QTableWidgetItem(sourceText);
        source->setFlags(source->flags() & ~Qt::ItemIsEditable);
        ui->tablePart->setItem(i, 2, source);
        ++i;
    }
    ui->tablePart->setSortingEnabled(true);
    applyFilter(ui->lineFilter->text());
}

void CBodyPartEditForm::reset()
{
    m_pendingToggleParts.clear();
    m_recentMultiSelectionParts.clear();
    ui->tablePart->clear();
    ui->tablePart->setColumnCount(3);
    ui->tablePart->setRowCount(0);
    ui->tablePart->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tablePart->setSelectionMode(QAbstractItemView::ExtendedSelection);
    QStringList header;
    header.append(tr("Part"));
    header.append(tr("Enabled"));
    header.append(tr("Source"));
    ui->tablePart->setHorizontalHeaderLabels(header);
    ui->tablePart->horizontalHeader()->setVisible(true);
    QHeaderView* headerView = ui->tablePart->horizontalHeader();
    headerView->setSectionResizeMode(QHeaderView::Interactive);
    const QByteArray savedHeader = QSettings().value(
        "ui/bodyPartEditorHeader").toByteArray();
    if(savedHeader.isEmpty() || !headerView->restoreState(savedHeader))
    {
        ui->tablePart->setColumnWidth(0, 150);
        ui->tablePart->setColumnWidth(1, 78);
        ui->tablePart->setColumnWidth(2, 150);
    }
    ui->tablePart->verticalHeader()->hide();
}

void CBodyPartEditForm::on_pushCancel_clicked()
{
    close();
}


void CBodyPartEditForm::on_pushApply_clicked()
{
    syncTableToProperty();
    emit onApplyChangesSignal(m_prop);
    close();
}

void CBodyPartEditForm::syncTableToProperty()
{
    auto arrStat = dynamic_cast<propBodyPart*>(m_prop.get())->value();

    for(int i(0); i<ui->tablePart->rowCount(); ++i)
    {
        auto pBox = dynamic_cast<QCheckBox*>(ui->tablePart->cellWidget(i, 1));
        if(pBox->checkState() == Qt::CheckState::PartiallyChecked)
            continue;

        arrStat[ui->tablePart->item(i,0)->text()]->resetValue(pBox->checkState() == Qt::CheckState::Checked);
    }
}


void CBodyPartEditForm::on_toolCopy_clicked()
{
    syncTableToProperty();
    QJsonObject obj;
    obj.insert("Version", 3);
    QJsonArray part;
    const QStringList serialized =
        dynamic_cast<propBodyPart*>(m_prop.get())->effectiveSerializedValue();
    for(const QString& value : serialized)
        part.append(value);
    obj.insert("bodyparts", part);
    QJsonDocument doc(obj);

    if (!m_clipboardFile.open(QIODevice::WriteOnly))
    {
        Q_ASSERT("Couldn't open bodypart copypast buffer file." && false);
    }
    else
    {
        m_clipboardFile.write(doc.toJson(QJsonDocument::JsonFormat::Indented));
        m_clipboardFile.close();
    }
}


void CBodyPartEditForm::on_toolPaste_clicked()
{
    if (!m_clipboardFile.open(QIODevice::ReadOnly))
    {
        Q_ASSERT("Couldn't open bodypart copypaste buffer file." && false);
        return;
    }

    if (m_clipboardFile.size() == 0)
    {
        qDebug() << "empty copypasteBuffer file";
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(m_clipboardFile.readAll(), &parseError);
    m_clipboardFile.close();
    QJsonObject obj = doc.object();
    QStringList serialized;
    if(obj["Version"].toInt() == 3 && obj["bodyparts"].isArray())
    {
        for(const QJsonValue& value : obj["bodyparts"].toArray())
            if(value.isString()) serialized.append(value.toString());
    }
    else if(obj["Version"].toInt() == 2 && obj["bodyparts"].isObject())
    {
        const QJsonObject legacy = obj["bodyparts"].toObject();
        for(const QString& key : legacy.keys())
            if(legacy.value(key).toBool()) serialized.append(key);
    }
    else return;
    dynamic_cast<propBodyPart*>(m_prop.get())->replaceSerializedValue(serialized);
    populateTable();
}

void CBodyPartEditForm::applyFilter(const QString& text)
{
    const QStringList terms = text.trimmed().toLower().split(
        QRegularExpression("\\s+"), QString::SkipEmptyParts);
    for(int row = 0; row < ui->tablePart->rowCount(); ++row)
    {
        const QTableWidgetItem* stateItem = ui->tablePart->item(row, 1);
        const Qt::CheckState state = stateItem
            ? Qt::CheckState(stateItem->data(Qt::UserRole).toInt())
            : Qt::PartiallyChecked;
        const QString stateText = stateSearchText(state);
        const QString searchable = QString("%1 %2")
            .arg(ui->tablePart->item(row, 0)->text(),
                 ui->tablePart->item(row, 2)->text())
            .toLower();
        static const QSet<QString> stateAliases = {
            "checked", "true", "on", "enabled",
            "unchecked", "false", "off", "disabled",
            "mixed", "partial", "undefined"
        };
        const QStringList rowStateAliases = stateText.split(' ');
        bool matches = true;
        for(const QString& term : terms)
            matches = matches && (stateAliases.contains(term)
                ? rowStateAliases.contains(term) : searchable.contains(term));
        ui->tablePart->setRowHidden(row, !matches);
    }
}

void CBodyPartEditForm::closeEvent(QCloseEvent* event)
{
    QSettings settings;
    settings.setValue("ui/bodyPartEditorSize", size());
    settings.setValue("ui/bodyPartEditorHeader",
                      ui->tablePart->horizontalHeader()->saveState());
    QWidget::closeEvent(event);
}

bool CBodyPartEditForm::eventFilter(QObject* watched, QEvent* event)
{
    CBodyPartCheckBox* clicked = qobject_cast<CBodyPartCheckBox*>(watched);
    if(!clicked || (event->type() != QEvent::MouseButtonPress
                    && event->type() != QEvent::MouseButtonDblClick
                    && event->type() != QEvent::MouseButtonRelease))
        return QWidget::eventFilter(watched, event);

    QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
    if(mouse->button() != Qt::LeftButton)
        return QWidget::eventFilter(watched, event);

    const int clickedRow = rowForCheckBox(ui->tablePart, clicked);
    if(clickedRow < 0 || !ui->tablePart->item(clickedRow, 0))
        return QWidget::eventFilter(watched, event);
    const QString clickedPart = ui->tablePart->item(clickedRow, 0)->text();

    if(event->type() == QEvent::MouseButtonPress
       || event->type() == QEvent::MouseButtonDblClick)
    {
        m_pendingToggleParts = selectedPartNames(ui->tablePart);
        if(m_pendingToggleParts.size() < 2
           && m_recentMultiSelectionParts.size() > 1
           && m_recentMultiSelectionParts.contains(
               clickedPart, Qt::CaseInsensitive))
            m_pendingToggleParts = m_recentMultiSelectionParts;
        const bool clickedIsSelected = m_pendingToggleParts.contains(
            clickedPart, Qt::CaseInsensitive);
        if(!clickedIsSelected || m_pendingToggleParts.size() < 2)
        {
            m_pendingToggleParts = QStringList(clickedPart);
            ui->tablePart->clearSelection();
            ui->tablePart->selectionModel()->select(
                ui->tablePart->model()->index(clickedRow, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
        ui->tablePart->setCurrentItem(ui->tablePart->item(clickedRow, 0),
                                      QItemSelectionModel::NoUpdate);
        return true;
    }

    if(m_pendingToggleParts.isEmpty())
        return true;
    const Qt::CheckState targetState = clicked->checkState() == Qt::Checked
        ? Qt::Unchecked : Qt::Checked;
    QList<QCheckBox*> checkBoxes;
    for(const QString& part : m_pendingToggleParts)
    {
        const int row = rowForPart(ui->tablePart, part);
        if(row >= 0)
            if(QCheckBox* checkBox = qobject_cast<QCheckBox*>(
                    ui->tablePart->cellWidget(row, 1)))
                checkBoxes.append(checkBox);
    }
    for(QCheckBox* checkBox : checkBoxes)
        checkBox->setCheckState(targetState);

    ui->tablePart->clearSelection();
    for(const QString& part : m_pendingToggleParts)
    {
        const int row = rowForPart(ui->tablePart, part);
        if(row >= 0)
            ui->tablePart->selectionModel()->select(
                ui->tablePart->model()->index(row, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    m_recentMultiSelectionParts = m_pendingToggleParts;
    m_pendingToggleParts.clear();
    return true;
}

