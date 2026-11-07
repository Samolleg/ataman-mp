#pragma once
#include <QSettings>
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>

// A user choice made after this migration is authoritative. Crash protection
// is evaluated separately and must never be acknowledged by a migration.
inline bool atamanRebuild64Preference(QSettings& settings, bool headless)
{
    if(headless) return false;
    const auto root=QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(root);QLockFile migration(QDir(root).filePath("renderer-default-migration-287.lock"));
    migration.setStaleLockTime(0);const bool locked=migration.tryLock(2000);
    settings.sync();
    if(!locked)return settings.value("ui/renderer","Rebuild64").toString()=="Rebuild64";
    if(!settings.value("ui/rebuild64DefaultMigration287", false).toBool()) {
        settings.setValue("ui/renderer", "Rebuild64");
        settings.setValue("ui/rebuild64DefaultMigration287", true);
        settings.sync();
    }
    return settings.value("ui/renderer", "Rebuild64").toString() == "Rebuild64";
}
