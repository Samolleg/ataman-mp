#ifndef RESOURCE_PROFILE_H
#define RESOURCE_PROFILE_H

#include <QString>
#include <QStringList>
#include <QVector>

struct SResourceProfile
{
    QString id;
    QString displayName;
    QString source;
    QStringList fixedModRoots;
    QString searchParent;
    QStringList directoryNameContains;
    QStringList requiredFiles;
    QStringList requiredDirectories;
    QStringList signatureFiles;
    QStringList resourceDirectories;
    QStringList mapDirectories;
};

struct SResourceProfileEvaluation
{
    bool valid{false};
    QString gameRoot;
    QString modRoot;
    QString profileId;
    QString profileName;
    QStringList resourceDirectories;
    QStringList mapDirectories;
    QStringList messages;
    int archiveCount{0};
};

QVector<SResourceProfile> builtInResourceProfiles();
QVector<SResourceProfile> loadResourceProfiles(const QString& externalDirectory = QString());
SResourceProfileEvaluation evaluateResourceProfile(const QString& gameRoot,
                                                   const SResourceProfile& profile);
QStringList discoverGameInstallations();

#endif // RESOURCE_PROFILE_H
