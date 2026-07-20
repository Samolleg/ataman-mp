#ifndef PROFILE_DATABASE_PROVIDER_SUPPORT_H
#define PROFILE_DATABASE_PROVIDER_SUPPORT_H

#include "../../resources/profile_database.h"

namespace ei {

QString activeDatabaseArchive(const QStringList& resourceDirectories, bool firstDirectoryOnly);
QString siblingArchive(const QString& databasePath, const QString& fileName);
QMap<QString, QByteArray> readDatabaseArchive(const QString& path,
                                              QVector<SDatabaseDiagnostic>* diagnostics);
void applyRelationalMembers(const QMap<QString, QByteArray>& members, const QString& sourcePath,
                            SProfileDatabaseSnapshot* snapshot);
void applyActionMembers(const QMap<QString, QByteArray>& members, const QString& sourcePath,
                        bool overlay, SProfileDatabaseSnapshot* snapshot);
SDatabaseProvenance databaseProvenance(const SProfileDatabaseSnapshot& snapshot,
                                       const QString& path, const QString& member,
                                       bool overlay);

}

#endif
