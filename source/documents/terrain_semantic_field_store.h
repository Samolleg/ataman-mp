#ifndef TERRAIN_SEMANTIC_FIELD_STORE_H
#define TERRAIN_SEMANTIC_FIELD_STORE_H

#include <QByteArray>
#include <QFileInfo>
#include <QString>
#include <QVector>

class CValidationReport;

// Editor-only, map-scoped shared-corner material state.  The game ignores this
// RES member.  It intentionally stores only the final semantic values: there
// is no placement order, tool ownership, or hidden Manual-paint history.
struct STerrainSemanticFieldDocument
{
    int schemaVersion{1};
    QString mprNamespace;
    QByteArray mprSha256;
    QByteArray atlasPixelSha256;
    QByteArray profileSha256;
    int vertexWidth{0};
    int vertexHeight{0};
    QVector<qint32> land;
    QVector<qint32> overlay;

    QByteArray toBytes(CValidationReport* report = nullptr) const;
    static bool fromBytes(const QByteArray& bytes,
                          STerrainSemanticFieldDocument* document,
                          CValidationReport* report = nullptr,
                          const QString& sourceName = QString());
    bool validateBinding(const QString& expectedNamespace,
                         const QByteArray& expectedMprSha256,
                         const QByteArray& expectedAtlasPixelSha256,
                         const QByteArray& expectedProfileSha256,
                         int expectedVertexWidth,
                         int expectedVertexHeight,
                         CValidationReport* report = nullptr) const;

    static QString memberName(const QString& mprNamespace,
                              const QFileInfo& mprFile);
};

#endif // TERRAIN_SEMANTIC_FIELD_STORE_H
