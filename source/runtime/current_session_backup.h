#ifndef CURRENT_SESSION_BACKUP_H
#define CURRENT_SESSION_BACKUP_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

bool snapshotCurrentSession(const QString& sourcePath, const QString& runRoot,
                            QJsonObject* snapshot, QString* error = nullptr);
bool restoreCurrentSession(const QJsonObject& snapshot, QStringList* messages);

#endif // CURRENT_SESSION_BACKUP_H
