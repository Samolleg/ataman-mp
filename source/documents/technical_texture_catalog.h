#ifndef TECHNICAL_TEXTURE_CATALOG_H
#define TECHNICAL_TEXTURE_CATALOG_H

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QMap>

namespace ei {

struct STechnicalTextureChoice
{
    QString name;
    int observedCount = 0;
};

// Read-only catalog of primary textures actually paired with models in MOBs.
// Scan is incremental so entering the creation dialog never parses every map
// on the GUI thread in one operation. A cached file is rescanned if it changes.
class TechnicalTextureCatalog
{
public:
    void refreshSources(const QStringList& mapRoots);
    bool hasPendingFiles() const { return !m_pending.isEmpty(); }
    int pendingFileCount() const { return m_pending.size(); }
    int sourceFileCount() const { return m_sources.size(); }
    int failedFileCount() const;
    void scanNextFile();
    QJsonObject saveCache() const;
    void restoreCache(const QJsonObject& cache);
    QMap<QString, QStringList> observedModelTextures() const;
    QVector<STechnicalTextureChoice> exactChoices(const QString& model,
                                                   const QStringList& available) const;

private:
    struct SourceFile
    {
        QString path;
        qint64 size = -1;
        QDateTime modified;
    };
    struct FileResult
    {
        SourceFile source;
        bool loaded = false;
        QHash<QString, QHash<QString, int>> pairs;
    };

    QVector<SourceFile> m_sources;
    QStringList m_pending;
    QHash<QString, FileResult> m_results;
};

} // namespace ei

#endif // TECHNICAL_TEXTURE_CATALOG_H
