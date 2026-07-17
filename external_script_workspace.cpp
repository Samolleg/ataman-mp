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
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &CExternalScriptWorkspaceManager::onDirectoryChanged);
    connect(&m_debounce, &QTimer::timeout,
            this, &CExternalScriptWorkspaceManager::processPendingChange);
}

QString CExternalScriptWorkspaceManager::displayScript(CMob* mob)
{
    if(!mob)
        return QString();
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    return cp1251->toUnicode(mob->script().toLatin1());
}

bool CExternalScriptWorkspaceManager::encodeScript(const QString& display, QString* stored)
{
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    const QByteArray encoded = cp1251->fromUnicode(display);
    if(cp1251->toUnicode(encoded) != display)
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
    if(!existingWorkspace)
    {
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            ei::log(eLogWarning, QString("Could not initialize external script workspace path:%1").arg(path));
            return QString();
        }
        file.write(displayScript(mob).toUtf8());
        file.close();
    }

    m_mobPaths.insert(mob, path);
    m_pathMobs.insert(path, mob);
    if(!m_watcher.files().contains(path))
        m_watcher.addPath(path);
    if(!m_watcher.directories().contains(directory))
        m_watcher.addPath(directory);
    ei::log(eLogInfo, QString("Registered external script workspace mob:%1 path:%2")
            .arg(mob->mobName(), path));

    if(existingWorkspace)
    {
        QFile file(path);
        if(file.open(QIODevice::ReadOnly))
        {
            const QString externalText = QString::fromUtf8(file.readAll());
            file.close();
            if(externalText != displayScript(mob))
                schedulePath(path, 0);
        }
    }
    return path;
}

void CExternalScriptWorkspaceManager::unregisterMob(CMob* mob)
{
    const QString path = m_mobPaths.take(mob);
    if(path.isEmpty())
        return;
    const QString directory = QFileInfo(path).absolutePath();
    m_pathMobs.remove(path);
    m_pendingPaths.remove(path);
    m_readRetries.remove(path);
    m_lastHandledText.remove(path);
    m_watcher.removePath(path);

    bool directoryStillUsed = false;
    for(const QString& remaining : m_mobPaths)
        directoryStillUsed = directoryStillUsed || QFileInfo(remaining).absolutePath() == directory;
    if(!directoryStillUsed)
        m_watcher.removePath(directory);
    ei::log(eLogInfo, QString("Unregistered external script workspace path:%1").arg(path));
}

QString CExternalScriptWorkspaceManager::workspacePath(CMob* mob) const
{
    return m_mobPaths.value(mob);
}

bool CExternalScriptWorkspaceManager::openEditor(CMob* mob)
{
    const QString path = registerMob(mob);
    if(path.isEmpty())
        return false;
    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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
    ei::log(eLogInfo, QString("External script file notification path:%1").arg(path));
    schedulePath(path);
}

void CExternalScriptWorkspaceManager::onDirectoryChanged(const QString& directory)
{
    ei::log(eLogInfo, QString("External script workspace notification path:%1").arg(directory));
    for(auto it = m_pathMobs.constBegin(); it != m_pathMobs.constEnd(); ++it)
    {
        if(QFileInfo(it.key()).absolutePath() == directory)
            schedulePath(it.key());
    }
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
    const QString path = *m_pendingPaths.constBegin();
    m_pendingPaths.remove(path);
    CMob* mob = m_pathMobs.value(path, nullptr);
    if(!mob)
    {
        continuePending();
        return;
    }

    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(QFileInfo::exists(path) && ++m_readRetries[path] <= 5)
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
    const QString externalText = QString::fromUtf8(file.readAll());
    file.close();
    if(QFileInfo::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);
    if(externalText == displayScript(mob) || externalText == m_lastHandledText.value(path))
    {
        continuePending();
        return;
    }

    QString storedScript;
    if(!encodeScript(externalText, &storedScript))
    {
        m_lastHandledText.insert(path, externalText);
        m_promptOpen = true;
        QMessageBox::critical(m_promptParent, "External script changed",
            "The external file contains characters that cannot round-trip through Windows-1251. The MOB script was not changed.");
        m_promptOpen = false;
        continuePending();
        return;
    }

    m_promptOpen = true;
    const QMessageBox::StandardButton answer = QMessageBox::question(
            m_promptParent, "External script changed",
            QString("Import the changed external script into %1? This will create one undo-history entry.")
                    .arg(mob->mobName()), QMessageBox::Yes | QMessageBox::No);
    m_promptOpen = false;
    m_lastHandledText.insert(path, externalText);
    if(answer == QMessageBox::Yes)
    {
        emit scriptImportAccepted(mob, storedScript, externalText);
        ei::log(eLogInfo, QString("External script import accepted mob:%1 path:%2 characters:%3")
                .arg(mob->mobName(), path).arg(externalText.size()));
    }
    continuePending();
}
