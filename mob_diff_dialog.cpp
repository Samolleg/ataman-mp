#include "mob_diff_dialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QTableWidget>
#include <QVBoxLayout>

#include "endurance_document.h"

namespace {
QString objectLabel(const QJsonObject& object)
{
    QStringList parts;
    if(object.contains("nid"))
        parts << QString("NID %1").arg(object.value("nid").toDouble(), 0, 'f', 0);
    if(!object.value("name").toString().isEmpty())
        parts << object.value("name").toString();
    if(!object.value("prototype").toString().isEmpty())
        parts << object.value("prototype").toString();
    parts << object.value("path").toString();
    return parts.join(" | ");
}

QString summaryText(const QJsonObject& summary)
{
    static const QStringList order = {"added", "removed", "modified", "moved", "ambiguous", "unchanged"};
    QStringList parts;
    for(const QString& key : order)
        if(summary.contains(key))
            parts << QString("%1: %2").arg(key).arg(summary.value(key).toInt());
    return parts.join("   ");
}
}

CMobDiffDialog::CMobDiffDialog(const QFileInfo& baseline, const QFileInfo& candidate,
                               QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Perspective MOB Diff - Details"));
    resize(1200, 720);
    setModal(false);

    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* files = new QLabel(tr("Baseline: %1\nCandidate: %2")
        .arg(baseline.absoluteFilePath(), candidate.absoluteFilePath()), this);
    files->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(files);

    CValidationReport baselineReport;
    CValidationReport candidateReport;
    CMobDocument baselineDocument;
    CMobDocument candidateDocument;
    const bool baselineLoaded = baselineDocument.load(baseline, &baselineReport);
    const bool candidateLoaded = candidateDocument.load(candidate, &candidateReport);
    if(!baselineLoaded || !candidateLoaded)
    {
        QLabel* error = new QLabel(tr("Comparison refused because one or both MOBs failed strict validation.\n\n%1\n%2")
            .arg(baselineReport.toText(), candidateReport.toText()), this);
        error->setWordWrap(true);
        error->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(error);
        QDialogButtonBox* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(closeButtons);
        return;
    }

    const CMobSemanticDiff diff = CMobSemanticDiff::compare(baselineDocument, candidateDocument);
    QJsonObject report = diff.toJson();
    report.insert("baseline", baseline.absoluteFilePath());
    report.insert("candidate", candidate.absoluteFilePath());
    m_jsonReport = QJsonDocument(report).toJson(QJsonDocument::Indented);
    m_textReport = diff.toText().toUtf8();

    m_summary = new QLabel(summaryText(report.value("summary").toObject()), this);
    layout->addWidget(m_summary);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Filter kind, match method, NID, name, prototype, or section path..."));
    layout->addWidget(m_filter);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({tr("Kind"), tr("Match"), tr("Confidence"),
                                        tr("Baseline"), tr("Candidate"),
                                        tr("Baseline SHA-256"), tr("Candidate SHA-256")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSortingEnabled(false);
    const QJsonArray entries = report.value("entries").toArray();
    const QJsonArray documentEntries = report.value("documentEntries").toArray();
    m_table->setRowCount(entries.size() + documentEntries.size());
    for(int row = 0; row < entries.size(); ++row)
    {
        const QJsonObject entry = entries.at(row).toObject();
        m_table->setItem(row, 0, new QTableWidgetItem(entry.value("kind").toString()));
        m_table->setItem(row, 1, new QTableWidgetItem(entry.value("matchMethod").toString()));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::number(entry.value("confidence").toDouble(), 'f', 2)));
        m_table->setItem(row, 3, new QTableWidgetItem(objectLabel(entry.value("baseline").toObject())));
        m_table->setItem(row, 4, new QTableWidgetItem(objectLabel(entry.value("candidate").toObject())));
        m_table->setItem(row, 5, new QTableWidgetItem(entry.value("baseline").toObject().value("sha256").toString()));
        m_table->setItem(row, 6, new QTableWidgetItem(entry.value("candidate").toObject().value("sha256").toString()));
    }
    for(int index = 0; index < documentEntries.size(); ++index)
    {
        const int row = entries.size() + index;
        const QJsonObject entry = documentEntries.at(index).toObject();
        QTableWidgetItem* kind = new QTableWidgetItem(entry.value("kind").toString());
        kind->setData(Qt::UserRole, entry.value("details").toString());
        m_table->setItem(row, 0, kind);
        m_table->setItem(row, 1, new QTableWidgetItem(entry.value("category").toString()));
        m_table->setItem(row, 2, new QTableWidgetItem(QString()));
        const QString path = entry.value("path").toString();
        m_table->setItem(row, 3, new QTableWidgetItem(entry.value("kind").toString() == "added" ? QString() : path));
        m_table->setItem(row, 4, new QTableWidgetItem(entry.value("kind").toString() == "removed" ? QString() : path));
        m_table->setItem(row, 5, new QTableWidgetItem(entry.value("baselineSha256").toString()));
        m_table->setItem(row, 6, new QTableWidgetItem(entry.value("candidateSha256").toString()));
    }
    m_table->setSortingEnabled(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    m_details = new QPlainTextEdit(this);
    m_details->setReadOnly(true);
    m_details->setPlaceholderText(tr("Select a script or document-section change to see its readable diff."));
    m_details->setMinimumHeight(140);
    layout->addWidget(m_details);

    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* exportText = new QPushButton(tr("Export Text..."), this);
    QPushButton* exportJson = new QPushButton(tr("Export JSON..."), this);
    QPushButton* close = new QPushButton(tr("Close"), this);
    actions->addWidget(exportText);
    actions->addWidget(exportJson);
    actions->addStretch(1);
    actions->addWidget(close);
    layout->addLayout(actions);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) { applyFilter(text); });
    connect(m_table, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
        const QTableWidgetItem* kind = row >= 0 ? m_table->item(row, 0) : nullptr;
        m_details->setPlainText(kind ? kind->data(Qt::UserRole).toString() : QString());
    });
    connect(exportText, &QPushButton::clicked, this, [this]() { exportReport(false); });
    connect(exportJson, &QPushButton::clicked, this, [this]() { exportReport(true); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
}

void CMobDiffDialog::applyFilter(const QString& text)
{
    if(!m_table)
        return;
    const QString needle = text.trimmed();
    for(int row = 0; row < m_table->rowCount(); ++row)
    {
        QString haystack;
        for(int column = 0; column < m_table->columnCount(); ++column)
            if(const QTableWidgetItem* item = m_table->item(row, column))
                haystack += item->text() + ' ';
        if(const QTableWidgetItem* kind = m_table->item(row, 0))
            haystack += kind->data(Qt::UserRole).toString();
        m_table->setRowHidden(row, !needle.isEmpty() && !haystack.contains(needle, Qt::CaseInsensitive));
    }
}

void CMobDiffDialog::exportReport(bool json)
{
    activateWindow();
    raise();
    const QString filter = json ? tr("JSON reports (*.json)") : tr("Text reports (*.txt)");
    const QString path = QFileDialog::getSaveFileName(this, tr("Export MOB diff"),
        json ? "mob-diff.json" : "mob-diff.txt", filter);
    if(path.isEmpty())
        return;
    QSaveFile file(path);
    const QByteArray& data = json ? m_jsonReport : m_textReport;
    if(!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        QMessageBox::warning(this, tr("Export MOB diff"), tr("Could not write the report:\n%1").arg(file.errorString()));
}
