#ifndef TECHNICAL_MODEL_CATALOG_H
#define TECHNICAL_MODEL_CATALOG_H
#include "technical_texture_catalog.h"
#include <QByteArray>
#include <QJsonObject>
#include "model_connections.h"
#include "object_assembly.h"

namespace ei {
// User-owned JSON: never part of the managed executable manifest.
class TechnicalModelCatalog
{
public:
    bool load(const QString& path, QString* error = nullptr);
    // Import the old executable-side catalog only when this profile has none.
    bool loadProfile(const QString& databasePath, const QString& legacyPath,
                     const QString& profile, QString* error = nullptr);
    static QString pathForDatabase(const QString& databasePath);
    bool save(QString* error = nullptr);
    QString path() const { return m_path; }
    bool writable() const { return m_valid; }
    quint64 revision() const { return m_revision; }
    QString russianName(const QString& model) const;
    void setRussianName(const QString& model, const QString& name);
    bool connections(const QString& model, QVector<ModelConnection>* points, QString* error = nullptr) const;
    bool setConnections(const QString& model, const QVector<ModelConnection>& points, QString* error = nullptr);
    QString category(const QString& model) const;
    QString region(const QString& model) const;
    bool setPlacement(const QString& model, const QString& category, const QString& region);
    QVector<ObjectAssembly> assemblies(QString* error = nullptr) const;
    bool setAssembly(const ObjectAssembly& assembly, QString* error = nullptr);
    bool removeAssembly(const QString& id);
    QJsonObject assemblyInstances(const QString& mobPath) const;
    void setAssemblyInstances(const QString& mobPath, const QJsonObject& instances);
    static QStringList categoryIds();
    static QStringList regionIds();
    void registerModels(const QStringList& models);
    static QString modelKey(QString name);
    static QString profileKey(const QStringList& roots);
    static bool matches(const QString& model, const QString& russianName, const QString& query);
    void restoreScan(const QString& profile, TechnicalTextureCatalog* scan) const;
    void captureScan(const QString& profile, const TechnicalTextureCatalog& scan);
    QVector<STechnicalTextureChoice> textureChoices(const QString& profile, const QString& model,
        const QStringList& available, const TechnicalTextureCatalog& scan) const;
    void bindTexture(const QString& profile, const QString& model, const QString& texture);
    bool hasTextureBindings(const QString& profile) const;
private:
    // Portable library belongs to this database-side catalog. Absolute-root
    // profile keys remain solely for legacy overrides and local scan caches.
    void refreshPortableBindings();
    QString m_path;
    QJsonObject m_document;
    QJsonObject m_portableBindings;
    QByteArray m_original;
    bool m_existed = false;
    bool m_valid = false;
    quint64 m_revision = 0;
};
}
#endif
