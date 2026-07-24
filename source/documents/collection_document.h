#ifndef COLLECTION_DOCUMENT_H
#define COLLECTION_DOCUMENT_H

#include <QString>
#include <QStringList>
#include <QVector>

struct SEnduranceCollection
{
    int version{1};
    QString name;
    QString profileId;
    QString mapZoneId;
    QString mprReference;
    QStringList mobReferences;
    QString sourcePath;
};

struct SResolvedCollection
{
    SEnduranceCollection document;
    QString mprPath;
    QStringList mobPaths;
    QStringList errors;

    bool isValid() const { return errors.isEmpty() && !mprPath.isEmpty() && !mobPaths.isEmpty(); }
};

class CCollectionDocument
{
public:
    static QString defaultDirectory();
    static bool load(const QString& path, SEnduranceCollection* output, QString* error = nullptr);
    static bool save(const QString& path, const SEnduranceCollection& collection,
                     QString* error = nullptr);
    static bool equivalent(const SEnduranceCollection& left,
                           const SEnduranceCollection& right);
    static SResolvedCollection resolve(const SEnduranceCollection& collection,
                                       const QVector<QString>& mapRoots);
    static QString portableReference(const QString& absolutePath,
                                     const QString& collectionPath,
                                     const QVector<QString>& mapRoots);
};

#endif
