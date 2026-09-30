#ifndef TECHNICAL_MODEL_CATALOG_H
#define TECHNICAL_MODEL_CATALOG_H
#include "technical_texture_catalog.h"
#include <QByteArray>
#include <QJsonObject>

namespace ei {
// User-owned JSON: never part of the managed executable manifest.
class TechnicalModelCatalog
{
public:
    bool load(const QString& path, QString* error = nullptr);
    bool save(QString* error = nullptr);
    QString path() const { return m_path; }
    bool writable() const { return m_valid; }
    QString russianName(const QString& model) const;
    void setRussianName(const QString& model, const QString& name);
    void registerModels(const QStringList& models);
    static QString modelKey(QString name);
    static QString profileKey(const QStringList& roots);
    static bool matches(const QString& model, const QString& russianName, const QString& query);
    void restoreScan(const QString& profile, TechnicalTextureCatalog* scan) const;
    void captureScan(const QString& profile, const TechnicalTextureCatalog& scan);
    QVector<STechnicalTextureChoice> textureChoices(const QString& profile, const QString& model,
        const QStringList& available, const TechnicalTextureCatalog& scan) const;
    void bindTexture(const QString& profile, const QString& model, const QString& texture);
private:
    QString m_path;
    QJsonObject m_document;
    QByteArray m_original;
    bool m_existed = false;
    bool m_valid = false;
};
}
#endif
