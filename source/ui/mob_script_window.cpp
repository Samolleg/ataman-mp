#include "window_geometry.h"
#include "mob_script_window.h"
#include "malachite_bridge.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTextCodec>
#include <QVBoxLayout>
#include <QTextCursor>
#include <QFileInfo>
#include <QLabel>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include "scenario_analysis.h"

#include "mob/mob.h"
#include "mob/script_editor.h"
#include "mob/script_highlighter.h"
#include "view.h"

CMobScriptWindow::CMobScriptWindow(CMob* mob, CView* view, QWidget* parent)
    : QWidget(parent, Qt::Window), m_mob(mob), m_view(view),
      m_editor(new CScriptEditor(this)), m_closeAccepted(false)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle(tr("MOB Script: %1").arg(
        mob ? mob->filePath().absoluteFilePath() : QString()));
    ei::rememberWindowGeometry(this, QStringLiteral("mobScript"));
    resize(900, 650);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_editor);
    QHBoxLayout* buttons = new QHBoxLayout;
    QPushButton* external = new QPushButton(tr("Open External Editor"), this);
    QPushButton* apply = new QPushButton(tr("Apply and Close"), this);
    QPushButton* closeButton = new QPushButton(tr("Close"), this);
    buttons->addWidget(external);
    QPushButton* scenarioButton = new QPushButton(tr("Scenario references at cursor"), this);
    scenarioButton->setObjectName("scenarioReferencesAtCursor");
    buttons->addWidget(scenarioButton);
    connect(scenarioButton, &QPushButton::clicked, this, [this]() {
        emit scenarioLocationRequested(m_mob, m_editor->textCursor().position());
    });
    buttons->addStretch();
    buttons->addWidget(apply);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    m_committedDisplayScript = displayScript(mob);
    m_editor->setPlainText(m_committedDisplayScript);
    m_committedDisplayScript = m_editor->toPlainText();
    new CScriptHighlighter(m_editor->document());
    connect(m_editor, &QPlainTextEdit::textChanged, this, &CMobScriptWindow::analysisBufferChanged);
    connect(external, &QPushButton::clicked, this, &CMobScriptWindow::openExternalEditor);
    connect(apply, &QPushButton::clicked, this, &CMobScriptWindow::applyScript);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    QShortcut* saveShortcut = new QShortcut(QKeySequence::Save, this);
    connect(saveShortcut, &QShortcut::activated, this, &CMobScriptWindow::applyScript);
    if(QFileInfo::exists(CMalachiteBridge::executable()))
        QTimer::singleShot(0,this,&CMobScriptWindow::startMalachite);
}

void CMobScriptWindow::startMalachite()
{
    if(m_malachite) return;
    // A draft entered before the asynchronous startup belongs to the classic
    // buffer. Never hide it behind a different snapshot.
    if(m_editor->toPlainText()!=m_committedDisplayScript) return;
    m_editor->setReadOnly(true);
    auto* bridge = new CMalachiteBridge(m_mob,m_view,this);
    m_malachite=bridge;
    if(auto* screen=QGuiApplication::primaryScreen()) {
        const auto available=screen->availableGeometry().size()-QSize(60,80);
        resize(qMin(qMax(width(),2100),available.width()),qMin(qMax(height(),1250),available.height()));
    }
    qobject_cast<QVBoxLayout*>(layout())->insertWidget(0,bridge,1);
    connect(bridge,&CMalachiteBridge::opened,this,[this](bool){m_editor->hide();setWindowTitle(tr("Malachite — MOB Script: %1").arg(m_mob->filePath().absoluteFilePath()));});
    connect(bridge,&CMalachiteBridge::draftChanged,this,[this](){emit analysisBufferChanged();});
    connect(bridge,&CMalachiteBridge::failed,this,[this,bridge](const QString& error){
        m_editor->setPlainText(bridge->text());m_editor->setReadOnly(false);m_editor->show();bridge->hide();
        auto* warning=new QLabel(tr("Malachite is unavailable. Script text retained: %1").arg(error),this);
        warning->setWordWrap(true);qobject_cast<QVBoxLayout*>(layout())->insertWidget(0,warning);
        m_malachite=nullptr;bridge->deleteLater();emit analysisBufferChanged();
    });
    bridge->start(qEnvironmentVariableIntValue("EI_MALACHITE_VERIFY")==1);
}

QString CMobScriptWindow::analysisText() const
{
    return m_malachite ? m_malachite->text() : m_editor->toPlainText();
}

bool CMobScriptWindow::navigateToSource(const QString& analyzedText, int offset)
{
    if(scenario::normalizedText(analyzedText) != analysisText())
        return false;
    if(m_malachite && m_malachite->ready()) {
        m_malachite->command("navigate",{{"offset",offset}});
        show();raise();activateWindow();return true;
    }
    QTextCursor cursor(m_editor->document());
    cursor.setPosition(qBound(0, offset, m_editor->toPlainText().size()));
    cursor.select(QTextCursor::LineUnderCursor);
    m_editor->setTextCursor(cursor);
    m_editor->centerCursor();
    show(); raise(); activateWindow(); m_editor->setFocus();
    return true;
}

QString CMobScriptWindow::displayScript(CMob* mob)
{
    if(!mob)
        return QString();
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    return cp1251->toUnicode(mob->script().toLatin1());
}

QString CMobScriptWindow::canonicalScriptText(QString text)
{
    if(!text.isEmpty() && text.front() == QChar(0xfeff))
        text.remove(0, 1);
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text;
}

bool CMobScriptWindow::hasLocalChanges() const
{
    return m_malachite ? m_malachite->hasDraft() : m_editor->toPlainText() != m_committedDisplayScript;
}

bool CMobScriptWindow::applyCurrentText()
{
    if(!m_mob || !m_view)
        return false;
    if(m_malachite && m_malachite->hasDraft()) return false;
    const QString display = canonicalScriptText(analysisText());
    const QString currentDisplay = displayScript(m_mob);
    if(display == canonicalScriptText(currentDisplay))
    {
        m_committedDisplayScript = m_editor->toPlainText();
        m_editor->document()->setModified(false);
        return true;
    }
    QString storedDisplay = display;
    if(currentDisplay.contains("\r\n"))
        storedDisplay.replace("\n", "\r\n");
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    const QByteArray encoded = cp1251->fromUnicode(storedDisplay);
    if(cp1251->toUnicode(encoded) != storedDisplay)
    {
        QMessageBox::critical(this, tr("Script encoding"),
            tr("The script contains characters that cannot be stored in Windows-1251. Nothing was applied."));
        return false;
    }
    const QString stored = QString::fromLatin1(encoded);
    if(stored != m_mob->script())
        m_view->commitMobScriptEdit(m_mob, stored, tr("Edit MOB script"));
    m_committedDisplayScript = m_editor->toPlainText();
    setWindowTitle(tr("MOB Script: %1").arg(m_mob->filePath().absoluteFilePath()));
    m_editor->document()->setModified(false);
    return true;
}

void CMobScriptWindow::applyScript()
{
    if(applyCurrentText())
        close();
}

void CMobScriptWindow::openExternalEditor()
{
    CView* view = m_view;
    CMob* mob = m_mob;
    if(view && mob && requestClose())
        view->openExternalMobScript(mob);
}

void CMobScriptWindow::onMobScriptChanged(CMob* mob, const QString& display)
{
    if(mob != m_mob)
        return;
    const QString normalizedDisplay = canonicalScriptText(display);
    if(m_malachite) {
        m_editor->setPlainText(normalizedDisplay);
        m_committedDisplayScript=normalizedDisplay;
        return;
    }
    if(normalizedDisplay == canonicalScriptText(m_editor->toPlainText()))
    {
        m_committedDisplayScript = m_editor->toPlainText();
        setWindowTitle(tr("MOB Script: %1").arg(m_mob->filePath().absoluteFilePath()));
        m_editor->document()->setModified(false);
        return;
    }
    if(hasLocalChanges())
    {
        // Undo, redo, or an accepted external import changed the document while
        // this editor has its own unapplied text. Preserve both: the document
        // becomes the new base and the local text remains available to apply.
        m_committedDisplayScript = normalizedDisplay;
        setWindowTitle(tr("MOB Script: %1 [unapplied local text retained]")
                       .arg(m_mob->filePath().absoluteFilePath()));
        emit analysisBufferChanged();
        return;
    }
    m_editor->setPlainText(normalizedDisplay);
    m_committedDisplayScript = m_editor->toPlainText();
    setWindowTitle(tr("MOB Script: %1").arg(m_mob->filePath().absoluteFilePath()));
    m_editor->document()->setModified(false);
}

void CMobScriptWindow::onExternalMobScriptImported(CMob* mob, const QString& display)
{
    if(mob != m_mob)
        return;

    const QString normalizedDisplay = canonicalScriptText(display);
    m_editor->setPlainText(normalizedDisplay);
    m_committedDisplayScript = m_editor->toPlainText();
    setWindowTitle(tr("MOB Script: %1").arg(m_mob->filePath().absoluteFilePath()));
    m_editor->document()->setModified(false);
}

bool CMobScriptWindow::requestClose()
{
    if(!m_mob) return true;
    m_closeAccepted = false;
    close();
    return m_closeAccepted;
}

void CMobScriptWindow::closeEvent(QCloseEvent* event)
{
    if(!m_mob) { m_closeAccepted = true; event->accept(); return; }
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
    // WA_DeleteOnClose is deferred; detach this buffer before a CMob can be
    // destroyed or a replacement editor opened for the same map.
    delete m_malachite; m_malachite=nullptr;
    m_mob = nullptr;
    emit analysisBufferChanged();
    event->accept();
}
