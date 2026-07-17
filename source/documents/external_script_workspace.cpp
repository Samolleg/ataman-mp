#include "external_script_workspace.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextCodec>
#include <QUrl>
#include <QProcess>
#include <QApplication>

#include "mob/mob.h"
#include "log.h"

CExternalScriptWorkspaceManager::CExternalScriptWorkspaceManager(
        QWidget* promptParent, QObject* parent)
    : QObject(parent), m_promptParent(promptParent), m_promptOpen(false)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, &CExternalScriptWorkspaceManager::onFileChanged);
    connect(&m_debounce, &QTimer::timeout,
            this, &CExternalScriptWorkspaceManager::processPendingChange);
    if(QApplication* application = qobject_cast<QApplication*>(QApplication::instance()))
    {
        connect(application, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state)
        {
            if(state == Qt::ApplicationActive)
                continuePending();
        });
    }
}

QString CExternalScriptWorkspaceManager::displayScript(CMob* mob)
{
    if(!mob)
        return QString();
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    return cp1251->toUnicode(mob->script().toLatin1());
}

QString CExternalScriptWorkspaceManager::canonicalScriptText(QString text)
{
    if(!text.isEmpty() && text.front() == QChar(0xfeff))
        text.remove(0, 1);
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text;
}

bool CExternalScriptWorkspaceManager::encodeScript(
        CMob* mob, const QString& display, QString* stored)
{
    QString normalized = canonicalScriptText(display);
    const QString current = displayScript(mob);
    if(current.contains("\r\n"))
        normalized.replace("\n", "\r\n");
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    const QByteArray encoded = cp1251->fromUnicode(normalized);
    if(cp1251->toUnicode(encoded) != normalized)
        return false;
    if(stored)
        *stored = QString::fromLatin1(encoded);
    return true;
}

QString CExternalScriptWorkspaceManager::createWorkspacePath(CMob* mob) const
{
    if(!mob)
        return QString();
    QByteArray identity = mob->originalData();
    if(identity.isEmpty())
        identity = mob->filePath().absoluteFilePath().toUtf8();
    const QByteArray hash = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QDir::separator() + "script-workspaces" + QDir::separator()
            + QString::fromLatin1(hash);
    return directory + QDir::separator() + mob->filePath().completeBaseName() + ".lua";
}

QString CExternalScriptWorkspaceManager::pendingMarkerPath(const QString& path)
{
    return path + ".pending";
}

void CExternalScriptWorkspaceManager::markPending(const QString& path)
{
    const QString markerPath = pendingMarkerPath(path);
    if(QFileInfo::exists(markerPath))
        return;
    QFile marker(markerPath);
    if(marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        marker.write("unimported external script change\n");
        marker.close();
    }
}

void CExternalScriptWorkspaceManager::clearPending(const QString& path)
{
    QFile::remove(pendingMarkerPath(path));
}

QString CExternalScriptWorkspaceManager::registerMob(CMob* mob)
{
    if(!mob)
        return QString();
    if(m_mobPaths.contains(mob))
        return m_mobPaths.value(mob);

    const QString path = createWorkspacePath(mob);
    const QString directory = QFileInfo(path).absolutePath();
    if(path.isEmpty() || !QDir().mkpath(directory))
    {
        ei::log(eLogWarning, QString("Could not create external script workspace for MOB:%1")
                .arg(mob->filePath().absoluteFilePath()));
        return QString();
    }

    const bool existingWorkspace = QFileInfo::exists(path);
    const bool recoverableWorkspace = existingWorkspace
            && QFileInfo::exists(pendingMarkerPath(path));
    if(!recoverableWorkspace)
    {
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            ei::log(eLogWarning, QString("Could not initialize external script workspace path:%1").arg(path));
            return QString();
        }
        file.write(canonicalScriptText(displayScript(mob)).toUtf8());
        file.close();
        clearPending(path);
    }

    m_mobPaths.insert(mob, path);
    m_pathMobs.insert(path, mob);
    if(!m_watcher.files().contains(path))
        m_watcher.addPath(path);
    ei::log(eLogInfo, QString("Registered external script workspace mob:%1 path:%2")
            .arg(mob->mobName(), path));

    if(recoverableWorkspace)
    {
        QFile file(path);
        if(file.open(QIODevice::ReadOnly))
        {
            const QString externalText = canonicalScriptText(QString::fromUtf8(file.readAll()));
            file.close();
            if(externalText != canonicalScriptText(displayScript(mob)))
                schedulePath(path, 0);
            else
                clearPending(path);
        }
    }
    return path;
}

void CExternalScriptWorkspaceManager::unregisterMob(CMob* mob)
{
    const QString path = m_mobPaths.take(mob);
    if(path.isEmpty())
        return;
    m_pathMobs.remove(path);
    m_pendingPaths.remove(path);
    m_readRetries.remove(path);
    m_lastHandledText.remove(path);
    m_notificationCounts.remove(path);
    m_watcher.removePath(path);
    ei::log(eLogInfo, QString("Unregistered external script workspace path:%1").arg(path));
}

QString CExternalScriptWorkspaceManager::workspacePath(CMob* mob) const
{
    return m_mobPaths.value(mob);
}

bool CExternalScriptWorkspaceManager::openEditor(CMob* mob, const QString& editorExecutable)
{
    const QString path = registerMob(mob);
    if(path.isEmpty())
        return false;
    if(!QFileInfo::exists(pendingMarkerPath(path)))
    {
        // Internal edits may have changed the embedded script since the MOB was
        // registered. Start an external session from the current document, not
        // from an obsolete managed file, without generating our own import event.
        m_watcher.removePath(path);
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            m_watcher.addPath(path);
            QMessageBox::warning(m_promptParent, "External editor",
                "Could not synchronize the managed script file with the active MOB.");
            return false;
        }
        file.write(canonicalScriptText(displayScript(mob)).toUtf8());
        file.close();
        clearPending(path);
        m_lastHandledText.remove(path);
        m_watcher.addPath(path);
    }
    bool opened = false;
    if(!editorExecutable.trimmed().isEmpty())
    {
        const QFileInfo executable(editorExecutable.trimmed());
        if(!executable.isFile())
        {
            QMessageBox::warning(m_promptParent, "External editor",
                QString("The configured external editor does not exist:\n%1\n\nChoose another editor in Settings > General, or clear it to use the Windows file association.")
                    .arg(QDir::toNativeSeparators(editorExecutable)));
            return false;
        }
        opened = QProcess::startDetached(executable.absoluteFilePath(), QStringList() << path);
    }
    else
        opened = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    if(!opened)
        QMessageBox::warning(m_promptParent, "External editor",
            "No application is associated with script files. Configure a Windows file association and try again.");
    return opened;
}

void CExternalScriptWorkspaceManager::schedulePath(const QString& path, int delayMs)
{
    if(path.isEmpty() || !m_pathMobs.contains(path))
        return;
    m_pendingPaths.insert(path);
    if(!m_promptOpen)
        m_debounce.start(qMax(0, delayMs));
}

void CExternalScriptWorkspaceManager::onFileChanged(const QString& path)
{
    ++m_notificationCounts[path];
    if(!m_pendingPaths.contains(path))
        markPending(path);
    schedulePath(path);
}

void CExternalScriptWorkspaceManager::continuePending()
{
    if(!m_promptOpen && !m_pendingPaths.isEmpty())
        m_debounce.start();
}

void CExternalScriptWorkspaceManager::processPendingChange()
{
    if(m_promptOpen || m_pendingPaths.isEmpty())
        return;
    if(QApplication::applicationState() != Qt::ApplicationActive)
    {
        // Saving in an external editor must not create a modal prompt behind
        // that editor. Keep the change queued and show it when Endurance regains
        // foreground focus via applicationStateChanged.
        return;
    }
    const QString path = *m_pendingPaths.constBegin();
    m_pendingPaths.remove(path);
    const int notificationCount = m_notificationCounts.take(path);
    if(notificationCount > 0)
        ei::log(eLogInfo, QString("Processing external script change path:%1 coalescedNotifications:%2")
                .arg(path).arg(notificationCount));
    CMob* mob = m_pathMobs.value(path, nullptr);
    if(!mob)
    {
        continuePending();
        return;
    }

    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(++m_readRetries[path] <= 5)
            schedulePath(path, 350);
        else
        {
            ei::log(eLogWarning, QString("External script remained unreadable path:%1").arg(path));
            m_readRetries.remove(path);
        }
        continuePending();
        return;
    }
    m_readRetries.remove(path);
    const QString externalText = canonicalScriptText(QString::fromUtf8(file.readAll()));
    file.close();
    if(QFileInfo::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);
    if(externalText == canonicalScriptText(displayScript(mob))
       || externalText == m_lastHandledText.value(path))
    {
        clearPending(path);
        continuePending();
        return;
    }

    QString storedScript;
    if(!encodeScript(mob, externalText, &storedScript))
    {
        m_lastHandledText.insert(path, externalText);
        m_promptOpen = true;
        QMessageBox::critical(m_promptParent, "External script changed",
            "The external file contains characters that cannot round-trip through Windows-1251. The MOB script was not changed.");
        m_promptOpen = false;
        clearPending(path);
        continuePending();
        return;
    }

    m_promptOpen = true;
    QWidget* promptWindow = m_promptParent ? m_promptParent->window() : nullptr;
    ei::log(eLogInfo, QString("Showing external script import prompt mob:%1 path:%2")
            .arg(mob->mobName(), path));
    const QMessageBox::StandardButton answer = QMessageBox::question(
            promptWindow, "External script changed",
            QString("Import the changed external script into %1? This will create one undo-history entry.")
                    .arg(mob->mobName()), QMessageBox::Yes | QMessageBox::No);
    m_promptOpen = false;
    m_lastHandledText.insert(path, externalText);
    clearPending(path);
    if(answer == QMessageBox::Yes)
    {
        emit scriptImportAccepted(mob, storedScript, externalText);
        ei::log(eLogInfo, QString("External script import accepted mob:%1 path:%2 characters:%3")
                .arg(mob->mobName(), path).arg(externalText.size()));
    }
    continuePending();
}
