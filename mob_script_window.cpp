#include "mob_script_window.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTextCodec>
#include <QVBoxLayout>

#include "mob/mob.h"
#include "mob/script_editor.h"
#include "view.h"

CMobScriptWindow::CMobScriptWindow(CMob* mob, CView* view, QWidget* parent)
    : QWidget(parent, Qt::Window), m_mob(mob), m_view(view),
      m_editor(new CScriptEditor(this)), m_closeAccepted(false)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle(QString("MOB Script: %1").arg(mob ? mob->filePath().absoluteFilePath() : QString()));
    resize(900, 650);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_editor);
    QHBoxLayout* buttons = new QHBoxLayout;
    QPushButton* external = new QPushButton(tr("Open External Editor"), this);
    QPushButton* apply = new QPushButton(tr("Apply"), this);
    QPushButton* closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(external);
    buttons->addStretch();
    buttons->addWidget(apply);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    m_committedDisplayScript = displayScript(mob);
    m_editor->setPlainText(m_committedDisplayScript);
    connect(external, &QPushButton::clicked, this, &CMobScriptWindow::openExternalEditor);
    connect(apply, &QPushButton::clicked, this, &CMobScriptWindow::applyScript);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    QShortcut* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &CMobScriptWindow::applyScript);
}

QString CMobScriptWindow::displayScript(CMob* mob)
{
    if(!mob)
        return QString();
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    return cp1251->toUnicode(mob->script().toLatin1());
}

bool CMobScriptWindow::hasLocalChanges() const
{
    return m_editor->toPlainText() != m_committedDisplayScript;
}

bool CMobScriptWindow::applyCurrentText()
{
    if(!m_mob || !m_view)
        return false;
    const QString display = m_editor->toPlainText();
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    const QByteArray encoded = cp1251->fromUnicode(display);
    if(cp1251->toUnicode(encoded) != display)
    {
        QMessageBox::critical(this, tr("Script encoding"),
            tr("The script contains characters that cannot be stored in Windows-1251. Nothing was applied."));
        return false;
    }
    const QString stored = QString::fromLatin1(encoded);
    if(stored != m_mob->script())
        m_view->commitMobScriptEdit(m_mob, stored, tr("Edit MOB script"));
    m_committedDisplayScript = display;
    setWindowTitle(QString("MOB Script: %1").arg(m_mob->filePath().absoluteFilePath()));
    m_editor->document()->setModified(false);
    return true;
}

void CMobScriptWindow::applyScript()
{
    applyCurrentText();
}

void CMobScriptWindow::openExternalEditor()
{
    if(m_view && m_mob)
        m_view->openExternalMobScript(m_mob);
}

void CMobScriptWindow::onMobScriptChanged(CMob* mob, const QString& display)
{
    if(mob != m_mob)
        return;
    if(display == m_editor->toPlainText())
    {
        m_committedDisplayScript = display;
        setWindowTitle(QString("MOB Script: %1").arg(m_mob->filePath().absoluteFilePath()));
        m_editor->document()->setModified(false);
        return;
    }
    if(hasLocalChanges())
    {
        // Undo, redo, or an accepted external import changed the document while
        // this editor has its own unapplied text. Preserve both: the document
        // becomes the new base and the local text remains available to apply.
        m_committedDisplayScript = display;
        setWindowTitle(QString("MOB Script: %1 [unapplied local text retained]")
                       .arg(m_mob->filePath().absoluteFilePath()));
        return;
    }
    m_editor->setPlainText(display);
    m_committedDisplayScript = display;
    setWindowTitle(QString("MOB Script: %1").arg(m_mob->filePath().absoluteFilePath()));
    m_editor->document()->setModified(false);
}

bool CMobScriptWindow::requestClose()
{
    m_closeAccepted = false;
    close();
    return m_closeAccepted;
}

void CMobScriptWindow::closeEvent(QCloseEvent* event)
{
    if(hasLocalChanges())
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Close MOB Script"), tr("The script editor has unapplied changes. Apply them before closing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if(answer == QMessageBox::Cancel)
        {
            m_closeAccepted = false;
            event->ignore();
            return;
        }
        if(answer == QMessageBox::Save && !applyCurrentText())
        {
            m_closeAccepted = false;
            event->ignore();
            return;
        }
    }
    m_closeAccepted = true;
    event->accept();
}
