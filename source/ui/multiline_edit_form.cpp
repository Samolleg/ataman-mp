#include "ui_theme.h"
#include "multiline_edit_form.h"
#include "ui_multiline_edit_form.h"
#include "profile_database.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QMessageBox>

CMultiLineEditForm::CMultiLineEditForm(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CMultiLineEditForm),
    m_suggestions(new QComboBox(this)),
    m_diagnostic(new QLabel(this))
{
    ui->setupUi(this);
    m_suggestions->setEditable(true);
    m_suggestions->setInsertPolicy(QComboBox::NoInsert);
    QPushButton* add = new QPushButton(tr("Add current database value"), this);
    QHBoxLayout* picker = new QHBoxLayout;
    picker->addWidget(m_suggestions, 1);
    picker->addWidget(add);
    ui->verticalLayout->insertLayout(0, picker);
    m_diagnostic->setWordWrap(true);
    m_diagnostic->setStyleSheet(ei::uiTextStyle(ei::UiTextTone::Warning));
    ui->verticalLayout->insertWidget(1, m_diagnostic);
    connect(add, &QPushButton::clicked, this, [this]()
    {
        const QString value = m_suggestions->currentText().trimmed();
        if(value.isEmpty())
            return;
        QString text = ui->plainTextEdit->toPlainText();
        if(!text.isEmpty() && !text.endsWith('\n'))
            text += '\n';
        ui->plainTextEdit->setPlainText(text + value);
    });
}

void CMultiLineEditForm::setSuggestions(const QStringList& suggestions, bool itemGrammar)
{
    m_knownValues = suggestions;
    m_itemGrammar = itemGrammar;
    m_suggestions->clear();
    m_suggestions->addItems(suggestions);
    m_suggestions->setVisible(!suggestions.isEmpty());
}

CMultiLineEditForm::~CMultiLineEditForm()
{
    delete ui;
}

void CMultiLineEditForm::setText(const QString& text)
{
    ui->plainTextEdit->setPlainText(text); //text.join("\n")
}

void CMultiLineEditForm::on_pushApply_clicked()
{
    QStringList findings;
    for(const QString& raw : ui->plainTextEdit->toPlainText().split('\n', QString::SkipEmptyParts))
    {
        const QString value = raw.trimmed();
        QString syntax;
        if(m_itemGrammar && !ei::validateItemReferenceSyntax(value, &syntax))
            findings.append(value + ": " + syntax);
        QString prototype = value.section('[', 0, 0).section('.', 0, 0);
        bool found = false;
        for(const QString& known : m_knownValues)
            if(known.compare(prototype, Qt::CaseInsensitive) == 0) { found = true; break; }
        if(!m_knownValues.isEmpty() && !found)
            findings.append(value + tr(": not found in the active profile database"));
    }
    m_diagnostic->setText(findings.join('\n'));
    if(!findings.isEmpty())
        QMessageBox::warning(this, tr("Profile database validation"),
            tr("The values will be preserved, but the active profile reported:\n\n") + findings.join('\n'));
    emit onTextApplySignal(ui->plainTextEdit->toPlainText());
    close();
}


void CMultiLineEditForm::on_pushCancel_clicked()
{
    close();
}

