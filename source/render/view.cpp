#include <QtWidgets>
#include <QDebug>
#include <QtMath>       //for fabs
#include <QMap>         //for parameters
#include <QAbstractScrollArea>
#include <QUndoStack>
#include <QTreeWidget>
#include <QPair>
#include <QRandomGenerator>
#include <QMessageBox>
#include <QCryptographicHash>
#include <QOpenGLFramebufferObject>
#include <QFrame>
#include <QLabel>
#include <QComboBox>
#include <QSlider>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtConcurrent>
#include <QtEndian>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <limits>

#include "view.h"
#include "landscape_operation_policy.h"
#include "gl_dispatch.h"
#include "render_scheduling.h"
#include "camera.h"
#include "unit_animation.h"
#include <QDateTime>
#include "mob\mob.h"
#include "resourcemanager.h"
#include "mmp_codec.h"
#include "resource_name.h"
#include "node.h"
#include "landscape.h"
#include "key_manager.h"
#include "log.h"
#include "settings.h"
#include "undo.h"
#include "objects/object_base.h"
#include "objects/worldobj.h"
#include "objects/lever.h"
#include "objects/unit.h"
#include "objects/magictrap.h"
#include "mob\mob_parameters.h"
#include "layout_components/progressview.h"
#include "layout_components/tablemanager.h"
#include "operationmanager.h"
#include "ogl_utils.h"
#include "scene.h"
#include "round_mob_form.h"
#include "layout_components/tree_view.h"
#include "property.h"
#include "tile.h"
#include "view_frustum.h"
#include "render_performance.h"
#include "render_queue.h"
#include "render_environment.h"
#include "shadow_cascade.h"
#include "particle_animation.h"
#include "utils.h"
#include "part.h"
#include "objects/light.h"
#include "objects/particle.h"
#include "objects/torch.h"
#include "external_script_workspace.h"
#include "mob_script_window.h"
#include "script_reference_analyzer.h"
#include "landscape_tools.h"
#include "endurance_document.h"
#include "staged_save_validator.h"
#include "terrain_atlas_analyzer.h"
#include "terrain_atlas_profile.h"
#include "terrain_atlas_review_dialog.h"
#include "terrain_virtual_atlas.h"
#include "res_archive_document.h"
#include "ai_graph_document.h"
#include "ai_graph_preview_builder.h"
#include "portable_diagnostic_writer.h"

class CLogic;

namespace {
QString paintIssueCategoryLabel(const QString& category)
{
    if(category == QStringLiteral("tile-mismatch"))
        return QObject::tr("WRONG MATERIAL / FAMILY");
    if(category == QStringLiteral("wrong-rotation"))
        return QObject::tr("WRONG ROTATION");
    return QObject::tr("NO / WRONG BLEND");
}

CTerrainTransitionCatalog globalMaterialCatalog(
    const STerrainAtlasProfile& profile,
    const CTerrainTransitionCatalog& source,
    ETerrainAtlasLayer layer)
{
    Q_UNUSED(layer);
    return CTerrainAtlasPresetCatalog::globalizedCatalog(profile, source);
}

QImage loadGlobalUniformSource(const STerrainGlobalMaterialFamily& family,
                               const QString& preferredNamespace,
                               const QSize& requiredSize,
                               QMap<QString, QByteArray>* encodedPageCache,
                               QSet<QString>* missingPageCache,
                               QMap<QString, QImage>* decodedTileCache)
{
    QVector<STerrainGlobalMaterialSource> sources = family.sources;
    std::stable_sort(sources.begin(), sources.end(),
        [&preferredNamespace](const STerrainGlobalMaterialSource& left,
                              const STerrainGlobalMaterialSource& right) {
            const bool leftPreferred = left.mprNamespace.compare(
                preferredNamespace, Qt::CaseInsensitive) == 0;
            const bool rightPreferred = right.mprNamespace.compare(
                preferredNamespace, Qt::CaseInsensitive) == 0;
            return leftPreferred && !rightPreferred;
        });
    QImage fallback;
    for(const STerrainGlobalMaterialSource& source : sources)
    {
        const QStringList names = ei::terrainTextureResourceNames(
            source.mprNamespace, source.textureCount);
        const int pageIndex = source.uniformSlot / 64;
        if(pageIndex < 0 || pageIndex >= names.size())
            continue;
        const QString pageName = names.at(pageIndex).toCaseFolded();
        const QString tileCacheKey = QStringLiteral("%1|%2|%3x%4")
            .arg(pageName).arg(source.uniformSlot % 64)
            .arg(requiredSize.width()).arg(requiredSize.height());
        if(decodedTileCache && decodedTileCache->contains(tileCacheKey))
            return decodedTileCache->value(tileCacheKey);
        const int localSlot = source.uniformSlot % 64;
        QImage tile;
        QString diagnostic;
        // Several hundred material cards often share a few dozen MMP pages.
        // Read each encoded page once, then decode only each requested tile.
        // This avoids both whole-page RGBA expansion and repeated multi-MiB
        // RES reads for every family on that page.
        if(!ei::decodeMmpDxt1AtlasTileCached(
               pageName, localSlot, encodedPageCache, missingPageCache,
               [](const QString& name, QByteArray* payload) {
                   return CTextureList::getInstance()->terrainMmpPayload(
                       name, payload);
               }, &tile, &diagnostic))
            continue;
        tile = tile.convertToFormat(QImage::Format_RGBA8888);
        if(requiredSize.isEmpty() || tile.size() == requiredSize)
        {
            if(decodedTileCache)
                decodedTileCache->insert(tileCacheKey, tile);
            return tile;
        }
        if(fallback.isNull()) fallback = tile;
    }
    // Material identity is independent of a map atlas's native tile
    // resolution. Prefer a same-size reviewed anchor above, but do not make a
    // valid stock JMV family unpaintable when only another resolution exists.
    return fallback.isNull() ? QImage() : fallback.scaled(
        requiredSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QMap<qint32, ETerrainBlendStyle> globalMaterialBlendStyles(
    const QVector<STerrainGlobalMaterialFamily>& families)
{
    QMap<qint32, ETerrainBlendStyle> result;
    for(const STerrainGlobalMaterialFamily& family : families)
        result.insert(family.id,
            CTerrainVirtualAtlas::blendStyleForMaterialName(family.name));
    return result;
}

QMap<qint32, qint32> materialRuntimeTypes(
    const STerrainAtlasProfile& profile,
    const QVector<STerrainGlobalMaterialFamily>& families)
{
    QMap<qint32, qint32> result;
    for(const STerrainAtlasProfileFamily& family : profile.families)
        if(family.tileType >= 0 && family.tileType < 16)
            result.insert(family.id, family.tileType);
    for(const STerrainGlobalMaterialFamily& family : families)
        if(family.runtimeTerrainType >= 0 && family.runtimeTerrainType < 16)
            result.insert(family.id, family.runtimeTerrainType);
    return result;
}

QByteArray globalTransitionKey(const STerrainTransitionSignature& signature)
{
    QByteArray key(16, '\0');
    const qint32 values[4] = {signature.northWest, signature.northEast,
                             signature.southEast, signature.southWest};
    for(int corner = 0; corner < 4; ++corner)
        qToLittleEndian<qint32>(values[corner],
            reinterpret_cast<uchar*>(key.data() + corner * 4));
    return key;
}

int terrainTransitionTopology(const STerrainTransitionSignature& signature)
{
    QMap<qint32, int> counts;
    for(qint32 material : {signature.northWest, signature.northEast,
                           signature.southEast, signature.southWest})
        ++counts[material];
    if(counts.size() != 2)
        return counts.size();
    if(counts.values().contains(1))
        return 21;
    return signature.northWest == signature.southEast
        && signature.northEast == signature.southWest ? 22 : 23;
}

QImage loadGlobalTransitionTile(const STerrainGlobalTransitionSource& source,
                                const QSize& requiredSize,
                                QMap<QString, QByteArray>* encodedPageCache,
                                QSet<QString>* missingPageCache,
                                QMap<QString, QImage>* decodedTileCache)
{
    const QStringList names = ei::terrainTextureResourceNames(
        source.mprNamespace, source.textureCount);
    const int pageIndex = source.slot / 64;
    if(pageIndex < 0 || pageIndex >= names.size())
        return {};
    const QString pageName = names.at(pageIndex).toCaseFolded();
    const int localSlot = source.slot % 64;
    const QString tileCacheKey = QStringLiteral("%1|%2|%3x%4")
        .arg(pageName).arg(localSlot)
        .arg(requiredSize.width()).arg(requiredSize.height());
    if(decodedTileCache && decodedTileCache->contains(tileCacheKey))
        return decodedTileCache->value(tileCacheKey);
    QImage tile;
    QString diagnostic;
    if(!ei::decodeMmpDxt1AtlasTileCached(
           pageName, localSlot, encodedPageCache, missingPageCache,
           [](const QString& name, QByteArray* payload) {
               return CTextureList::getInstance()->terrainMmpPayload(
                   name, payload);
           }, &tile, &diagnostic))
        return {};
    tile = tile.convertToFormat(QImage::Format_RGBA8888);
    if(!requiredSize.isEmpty() && tile.size() != requiredSize)
        tile = tile.scaled(requiredSize, Qt::IgnoreAspectRatio,
                           Qt::SmoothTransformation);
    if(decodedTileCache) decodedTileCache->insert(tileCacheKey, tile);
    return tile;
}

QVector4D aiGraphLayerColor(int layer)
{
    static const QVector4D colors[CAiGraphDocument::LayerCount] = {
        {0.55f, 0.30f, 1.00f, 0.90f}, {0.42f, 0.38f, 1.00f, 0.90f},
        {0.30f, 0.50f, 0.94f, 0.90f}, {0.20f, 0.62f, 0.84f, 0.90f},
        {0.14f, 0.70f, 0.72f, 0.90f}, {0.12f, 0.75f, 0.59f, 0.90f},
        {0.15f, 0.79f, 0.45f, 0.90f}, {0.28f, 0.82f, 0.36f, 0.90f}
    };
    return colors[qBound(0, layer, CAiGraphDocument::LayerCount - 1)];
}

QByteArray terrainAtlasPixelSha256(const QVector<QImage>& images)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(images.size()));
    hash.addData("\n", 1);
    for(const QImage& source : images)
    {
        const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
        hash.addData(QByteArray::number(image.width()));
        hash.addData("x", 1);
        hash.addData(QByteArray::number(image.height()));
        hash.addData("\n", 1);
        for(int y = 0; y < image.height(); ++y)
            hash.addData(reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 4);
    }
    return hash.result().toHex();
}

QJsonObject terrainGridSignatureJson(const STerrainTransitionSignature& signature)
{
    QJsonObject result;
    result.insert("x0y0", signature.northWest);
    result.insert("x1y0", signature.northEast);
    result.insert("x1y1", signature.southEast);
    result.insert("x0y1", signature.southWest);
    return result;
}

QJsonObject packedTerrainTileJson(quint16 packed)
{
    const SMprPackedTile tile = SMprPackedTile::decode(packed);
    QJsonObject result;
    result.insert("decimal", int(packed));
    result.insert("hex", QString("0x%1").arg(packed, 4, 16, QLatin1Char('0')));
    result.insert("atlasIndex", int(tile.atlasIndex));
    result.insert("tileIndex", int(tile.tileIndex));
    result.insert("atlasSlot", tile.atlasSlot());
    result.insert("rotation", int(tile.rotation));
    return result;
}

class CTerrainHeightPreviewCommand : public QUndoCommand
{
public:
    CTerrainHeightPreviewCommand(CView* view, const STerrainHeightCommand& command,
                                 bool alreadyApplied = false)
        : m_view(view), m_command(command), m_skipInitialRedo(alreadyApplied)
    {
        setText(command.description);
    }
    void undo() override { m_view->applyTerrainHeightPreview(m_command, false); }
    void redo() override
    {
        if(m_skipInitialRedo)
        {
            m_skipInitialRedo = false;
            return;
        }
        m_view->applyTerrainHeightPreview(m_command, true);
    }
private:
    CView* m_view;
    STerrainHeightCommand m_command;
    bool m_skipInitialRedo{false};
};

class CTerrainVerticalScalePreviewCommand : public QUndoCommand
{
public:
    CTerrainVerticalScalePreviewCommand(
        CView* view, const STerrainVerticalScaleCommand& command)
        : m_view(view), m_command(command)
    {
        setText(command.description);
    }
    void undo() override { m_view->applyTerrainVerticalScalePreview(m_command, false); }
    void redo() override { m_view->applyTerrainVerticalScalePreview(m_command, true); }
private:
    CView* m_view;
    STerrainVerticalScaleCommand m_command;
};

class CTerrainTilePreviewCommand : public QUndoCommand
{
public:
    CTerrainTilePreviewCommand(CView* view, const STerrainTileCommand& command,
                               bool alreadyApplied)
        : m_view(view), m_command(command), m_skipInitialRedo(alreadyApplied)
    {
        setText(command.description);
    }
    void undo() override { m_view->applyTerrainTilePreview(m_command, false); }
    void redo() override
    {
        if(m_skipInitialRedo)
        {
            m_skipInitialRedo = false;
            return;
        }
        m_view->applyTerrainTilePreview(m_command, true);
    }
private:
    CView* m_view;
    STerrainTileCommand m_command;
    bool m_skipInitialRedo{false};
};

class COverlaySurfacePreviewCommand : public QUndoCommand
{
public:
    COverlaySurfacePreviewCommand(CView* view, const SOverlaySurfaceCommand& command,
                                  bool alreadyApplied)
        : m_view(view), m_command(command), m_skipInitialRedo(alreadyApplied)
    {
        setText(command.description);
    }
    void undo() override { m_view->applyOverlaySurfacePreview(m_command, false); }
    void redo() override
    {
        if(m_skipInitialRedo)
        {
            m_skipInitialRedo = false;
            return;
        }
        m_view->applyOverlaySurfacePreview(m_command, true);
    }
private:
    CView* m_view;
    SOverlaySurfaceCommand m_command;
    bool m_skipInitialRedo{false};
};

void appendOverlaySurfaceCommand(SOverlaySurfaceCommand* stroke,
                                 const SOverlaySurfaceCommand& stamp)
{
    if(!stroke)
        return;
    if(stroke->description.isEmpty())
        stroke->description = stamp.description;
    stroke->heightClampedVertexCount += stamp.heightClampedVertexCount;
    for(const SOverlaySectorDelta& delta : stamp.sectors)
    {
        bool merged = false;
        for(SOverlaySectorDelta& existing : stroke->sectors)
        {
            if(existing.sectorX != delta.sectorX || existing.sectorY != delta.sectorY)
                continue;
            existing.afterPayload = delta.afterPayload;
            merged = true;
            break;
        }
        if(!merged)
            stroke->sectors.append(delta);
    }
}

QString openGlErrorName(GLenum error)
{
    switch(error)
    {
    case GL_INVALID_ENUM: return QStringLiteral("GL_INVALID_ENUM");
    case GL_INVALID_VALUE: return QStringLiteral("GL_INVALID_VALUE");
    case GL_INVALID_OPERATION: return QStringLiteral("GL_INVALID_OPERATION");
    case GL_OUT_OF_MEMORY: return QStringLiteral("GL_OUT_OF_MEMORY");
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return QStringLiteral("GL_INVALID_FRAMEBUFFER_OPERATION");
    default: return QStringLiteral("unknown");
    }
}

struct SParticleVisualStyle
{
    SParticleVisualStyle(const char* texture = nullptr,
                         const QVector4D& color = QVector4D(0.72f, 0.80f, 1.0f, 0.72f),
                         float scale = 1.0f, float edgeSoftness = 0.34f,
                         bool useAdditiveBlend = false)
        : textureName(texture), tint(color), scaleMultiplier(scale),
          softness(edgeSoftness), additive(useAdditiveBlend) {}
    const char* textureName;
    QVector4D tint;
    float scaleMultiplier;
    float softness;
    bool additive;
};

SParticleVisualStyle particleVisualStyle(uint kind)
{
    // Particle IDs are the symbolic values exposed by the stock/JMV MOB
    // editor tables. Texture names are limited to direct corpus matches;
    // every other kind deliberately uses the procedural soft sprite instead
    // of borrowing an unrelated resource.
    switch(kind)
    {
    case 8192: return {nullptr, QVector4D(1.0f, 0.42f, 0.12f, 0.88f), 0.75f, 0.20f, true}; // Fireball
    case 8193: return {"tendrismoke", QVector4D(1.0f, 1.0f, 1.0f, 0.92f), 1.25f, 0.18f, true};
    case 8194:
    case 8195:
    case 8208:
    case 8209:
    case 8257:
        return {nullptr, QVector4D(1.0f, 0.48f, 0.12f, 0.86f), 1.15f, 0.18f, true};
    case 8196:
        return {"tendrismoke", QVector4D(1.0f, 1.0f, 1.0f, 0.72f), 1.55f, 0.42f, false};
    case 8198:
        return {nullptr, QVector4D(0.28f, 1.0f, 0.42f, 0.70f), 1.1f, 0.26f, true};
    case 8199:
        return {"poisoncloud", QVector4D(0.42f, 0.86f, 0.22f, 0.68f), 1.8f, 0.48f, false};
    case 8200:
    case 8201:
        return {nullptr, QVector4D(0.42f, 0.78f, 1.0f, 0.66f), 1.65f, 0.42f, false};
    case 8202:
        return {"tornado", QVector4D(0.76f, 0.80f, 0.84f, 0.70f), 2.0f, 0.36f, false};
    case 8210:
    case 8258:
        return {nullptr, QVector4D(0.48f, 1.0f, 0.34f, 0.78f), 1.0f, 0.24f, true};
    case 8240:
    case 8259:
        return {nullptr, QVector4D(0.38f, 0.70f, 1.0f, 0.86f), 1.0f, 0.20f, true};
    case 8253:
    case 8254:
        return {"teleport00", QVector4D(0.62f, 0.46f, 1.0f, 0.82f), 1.55f, 0.22f, true};
    case 8203:
    case 8204:
    case 8205:
        return {nullptr, QVector4D(0.66f, 0.52f, 1.0f, 0.76f), 1.15f, 0.24f, true};
    default:
        return {};
    }
}

bool isPersistentFireParticle(uint kind)
{
    return kind == 8193u || kind == 8195u || kind == 8208u;
}

bool isPersistentSmokeParticle(uint kind)
{
    return kind == 8196u;
}

struct SSkyVertex
{
    QVector3D position;
    QVector2D texture;
};

class SkyFigureReader
{
public:
    explicit SkyFigureReader(const QByteArray& bytes) : m_bytes(bytes) {}

    bool skip(qint64 count)
    {
        if(count < 0 || m_offset + count > m_bytes.size()) return false;
        m_offset += int(count);
        return true;
    }

    bool u16(quint16* value)
    {
        if(!value || m_offset + 2 > m_bytes.size()) return false;
        *value = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(m_bytes.constData() + m_offset));
        m_offset += 2;
        return true;
    }

    bool u32(quint32* value)
    {
        if(!value || m_offset + 4 > m_bytes.size()) return false;
        *value = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(m_bytes.constData() + m_offset));
        m_offset += 4;
        return true;
    }

    bool f32(float* value)
    {
        quint32 bits = 0;
        if(!u32(&bits) || !value) return false;
        static_assert(sizeof(float) == sizeof(quint32), "32-bit float required");
        std::memcpy(value, &bits, sizeof(float));
        return qIsFinite(*value);
    }

    int offset() const { return m_offset; }
    int size() const { return m_bytes.size(); }

private:
    const QByteArray& m_bytes;
    int m_offset{0};
};

QJsonArray jsonVector(const QVector3D& value)
{
    return QJsonArray{value.x(), value.y(), value.z()};
}

QJsonArray jsonMatrix(const QMatrix4x4& value)
{
    QJsonArray result;
    const float* data = value.constData();
    for(int index = 0; index < 16; ++index)
        result.append(data[index]);
    return result;
}

QJsonObject projectedPoint(const QMatrix4x4& matrix, const QVector3D& world)
{
    const QVector4D clip = matrix * QVector4D(world, 1.0f);
    QJsonObject result;
    result["clip"] = QJsonArray{clip.x(), clip.y(), clip.z(), clip.w()};
    if(!qFuzzyIsNull(clip.w()))
    {
        const QVector3D ndc = clip.toVector3D() / clip.w();
        const QVector3D atlas = ndc * 0.5f + QVector3D(0.5f, 0.5f, 0.5f);
        result["ndc"] = jsonVector(ndc);
        result["atlas"] = jsonVector(atlas);
        result["inside"] = atlas.x() >= 0.0f && atlas.x() <= 1.0f
            && atlas.y() >= 0.0f && atlas.y() <= 1.0f
            && atlas.z() >= 0.0f && atlas.z() <= 1.0f;
    }
    return result;
}

QImage visualizedPackedDepth(const QImage& source)
{
    QImage result(source.size(), QImage::Format_ARGB32);
    result.fill(Qt::white);
    for(int y = 0; y < source.height(); ++y)
        for(int x = 0; x < source.width(); ++x)
        {
            const QColor packed = source.pixelColor(x, y);
            const double r = packed.redF();
            const double g = packed.greenF();
            const double b = packed.blueF();
            const double a = packed.alphaF();
            const double depth = r + g / 255.0 + b / 65025.0
                    + a / 16581375.0;
            const int shade = qBound(0, qRound(depth * 255.0), 255);
            result.setPixelColor(x, y, QColor(shade, shade, shade));
        }
    return result;
}

QVector<SSkyVertex> decodeLegacySkyFigure(const QByteArray& bytes, QString* diagnostic)
{
    // nask0sky is the stock pre-signature, six-morph FIG variant. Keep this
    // bounded parser local to the sky path instead of relaxing the FIG8 model
    // parser used by ordinary map objects.
    SkyFigureReader reader(bytes);
    quint32 group = 0, textureNumber = 0, vertexBlocks = 0;
    if(!reader.u32(&group) || !reader.u32(&textureNumber)
            || !reader.skip(6 * (3 * 3 + 1) * qint64(sizeof(float)))
            || !reader.u32(&vertexBlocks)
            || vertexBlocks == 0 || vertexBlocks > 65536)
    {
        if(diagnostic) *diagnostic = "invalid legacy sky header";
        return {};
    }

    const int sourceVertexCount = int(vertexBlocks) * 4;
    QVector<QVector3D> sourceVertices(sourceVertexCount);
    for(quint32 block = 0; block < vertexBlocks; ++block)
    {
        for(int axis = 0; axis < 3; ++axis)
        {
            for(int morph = 0; morph < 6; ++morph)
            {
                for(int lane = 0; lane < 4; ++lane)
                {
                    float value = 0.0f;
                    if(!reader.f32(&value))
                    {
                        if(diagnostic) *diagnostic = "truncated legacy sky vertices";
                        return {};
                    }
                    if(morph == 0)
                        sourceVertices[int(block) * 4 + lane][axis] = value;
                }
            }
        }
    }

    quint32 normalBlocks = 0, uvCount = 0;
    if(!reader.u32(&normalBlocks) || normalBlocks > 65536
            || !reader.skip(qint64(normalBlocks) * 4 * 4 * sizeof(float))
            || !reader.u32(&uvCount) || uvCount == 0 || uvCount > 1048576)
    {
        if(diagnostic) *diagnostic = "invalid legacy sky normals/UV header";
        return {};
    }
    QVector<QVector2D> uvs;
    uvs.resize(int(uvCount));
    for(QVector2D& uv : uvs)
    {
        float u = 0.0f, v = 0.0f;
        if(!reader.f32(&u) || !reader.f32(&v))
        {
            if(diagnostic) *diagnostic = "truncated legacy sky UVs";
            return {};
        }
        uv = QVector2D(u, v);
    }

    quint32 faceCount = 0, componentCount = 0;
    if(!reader.u32(&faceCount) || faceCount > 1048576
            || !reader.skip(qint64(faceCount) * 52)
            || !reader.u32(&componentCount) || componentCount == 0
            || componentCount > 1048576)
    {
        if(diagnostic) *diagnostic = "invalid legacy sky face/component header";
        return {};
    }
    struct Component { quint16 vertex{0}; quint16 normal{0}; quint16 uv{0}; };
    QVector<Component> components;
    components.resize(int(componentCount));
    for(Component& component : components)
    {
        if(!reader.u16(&component.vertex) || !reader.u16(&component.normal)
                || !reader.u16(&component.uv))
        {
            if(diagnostic) *diagnostic = "truncated legacy sky components";
            return {};
        }
    }

    quint32 morphComponentCount = 0, indexCount = 0;
    if(!reader.u32(&morphComponentCount) || morphComponentCount > 1048576
            || !reader.skip(qint64(morphComponentCount) * 2 * sizeof(quint16))
            || !reader.u32(&indexCount) || indexCount == 0
            || indexCount > 4194304)
    {
        if(diagnostic) *diagnostic = "invalid legacy sky morph/index header";
        return {};
    }

    QVector<SSkyVertex> result;
    result.reserve(int(indexCount));
    for(quint32 index = 0; index < indexCount; ++index)
    {
        quint16 componentIndex = 0;
        if(!reader.u16(&componentIndex) || componentIndex >= components.size())
        {
            if(diagnostic) *diagnostic = "invalid legacy sky index";
            return {};
        }
        const Component& component = components.at(componentIndex);
        if(component.vertex >= sourceVertices.size() || component.uv >= uvs.size())
        {
            if(diagnostic) *diagnostic = "legacy sky component is out of bounds";
            return {};
        }
        result.append(SSkyVertex{sourceVertices.at(component.vertex),
                                 uvs.at(component.uv)});
    }
    if(reader.offset() != reader.size())
    {
        if(diagnostic) *diagnostic = "legacy sky contains trailing data";
        return {};
    }
    return result;
}

}
CView::CView(QWidget *parent, const QGLWidget *pShareWidget) : QGLWidget(parent, pShareWidget)
  ,m_gpuTimer(nullptr)
  ,m_gpuTimerPending(false)
  ,m_pSettings(nullptr)
  ,m_pUndoStack(nullptr)
  ,m_pProgress(nullptr)
  ,m_pendingOperationOwnsPaste(false)
  ,m_pendingLogicPlacementMacro(false)
  ,m_pendingPasteMob(nullptr)
  ,m_clipboard_buffer_file(QString("%1%2%3").arg(QDir::tempPath()).arg(QDir::separator()).arg("copy_paste_buffer.json"))
  ,m_recentOpenedFile_file(QString("%1%2%3").arg(QDir::tempPath()).arg(QDir::separator()).arg("recent_opened.json"))
  ,m_activeMob(nullptr)
  ,m_pTree(nullptr)
  ,m_pRoundForm(nullptr)
  ,m_bDrawLand(true)
  ,m_bDrawWater(true)
  ,m_bPreviewTile(false)
  ,m_tileBrushCommandId(0)
  ,m_landscapeTools(nullptr)
  ,m_terrainEditAvailable(false)
  ,m_sculptStrokeActive(false)
  ,m_sculptRepeatTimer(new QTimer(this))
  ,m_tileGridVisible(false)
  ,m_lastLandscapeEditMode(-1)
  ,m_hasViewportMousePos(false)
  ,m_testSpawnMarkerValid(false)
  ,m_testSpawnMarkerAdjusted(false)
  ,m_spatialIndexDirty(true)
  ,m_sceneFingerprintDirty(true)
{
    setFocusPolicy(Qt::ClickFocus);

    m_cam.reset(new CCamera);
    m_timer = new QTimer;
    m_aReadState.resize(eReadCount);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(updateWindow()));
    m_mprModifyTimer = new QTimer;
    connect(m_mprModifyTimer, SIGNAL(timeout()), this, SLOT(checkNewLandVersion()));
    m_operationBackup.clear();
    m_unitMovementBackup.clear();
    m_selectFrame.reset(new CSelectFrame);
    //qDebug() << format();
    //qDebug() << isSharing();
    QApplication* application = static_cast<QApplication *>(QApplication::instance());
    application->inputMethod()->reset();
    QLocale a = application->inputMethod()->locale();
    QString lang = a.languageToString(a.language());
    a.setDefault(QLocale::English);
    m_pLand = CLandscape::getInstance();
    m_scriptWorkspaces.reset(new CExternalScriptWorkspaceManager(this, this));
    connect(m_scriptWorkspaces.get(), &CExternalScriptWorkspaceManager::scriptImportAccepted,
            this, &CView::importExternalMobScript);
    // A single cadence owns sculpt stamps. Mouse events only update the latest
    // target, preventing the timer and pointer stream from doing duplicate
    // canonical-sector rebuilds during a drag.
    m_sculptRepeatTimer->setInterval(50);
    connect(m_sculptRepeatTimer, &QTimer::timeout, this, [this]()
    {
        if(m_sculptStrokeActive)
            sculptTerrainTowardLatestPoint();
    });
    m_aiGraphRegenerationTimer = new QTimer(this);
    m_aiGraphRegenerationTimer->setSingleShot(true);
    m_aiGraphRegenerationTimer->setInterval(500);
    connect(m_aiGraphRegenerationTimer, &QTimer::timeout, this, [this]()
    {
        if(!m_aiGraphAutoRegenerationEnabled)
            return;
        if(m_aiGraphBuildWatcher && m_aiGraphBuildWatcher->isRunning())
        {
            m_aiGraphRegenerationPending = true;
            return;
        }
        m_aiGraphRegenerationPending = false;
        startAiGraphPreviewRegeneration();
    });
}

void CView::attachLandscapeTools(CLandscapeTools* tools)
{
    m_landscapeTools = tools;
    if(m_landscapeTools)
    {
        m_lastLandscapeEditMode = int(m_landscapeTools->editMode());
        connect(m_landscapeTools, &CLandscapeTools::importHeightmapRequested,
                this, &CView::importTerrainHeightmap);
        connect(m_landscapeTools, &CLandscapeTools::tilePickerRequested,
                this, &CView::openAtlasTiles);
        connect(m_landscapeTools, &CLandscapeTools::atlasProfileReviewRequested,
                this, &CView::reviewTerrainAtlasProfile);
        connect(m_landscapeTools, &CLandscapeTools::exportPaintDiagnosticRequested,
                this, &CView::exportTerrainPaintDiagnostic);
        connect(m_landscapeTools, &CLandscapeTools::finalizeVirtualAtlasRequested,
                this, &CView::finalizeVirtualTerrainAtlas);
        connect(m_landscapeTools, &CLandscapeTools::editingStateChanged,
                this, &CView::syncLandscapeToolState);
        connect(m_landscapeTools, &CLandscapeTools::paintTerrainTypeChanged,
                this, [this](qint32 terrainType)
        {
            Q_UNUSED(terrainType);
            if(!manualTilePaintingEnabled())
            {
                setPreviewTile(false);
                unsetCursor();
            }
            if(m_hasViewportMousePos)
            {
                const bool land = CScene::getInstance()->isLandTileEditMode();
                updatePreviewTile(getTerrainBrushPos(m_lastViewportMousePos.x(),
                                                      m_lastViewportMousePos.y()), land);
            }
        });
        connect(m_landscapeTools, &CLandscapeTools::terrainVisibilityChanged,
                this, [this](bool drawLand, bool drawWater)
        {
            setDrawLand(drawLand);
            setDrawWater(drawWater);
        });
        m_landscapeTools->setTerrainVisibility(m_bDrawLand, m_bDrawWater);
    }
}

void CView::setDrawWater(bool bDraw)
{
    if(m_bDrawWater == bDraw)
        return;
    m_bDrawWater = bDraw;
    if(m_landscapeTools)
        m_landscapeTools->setTerrainVisibility(m_bDrawLand, m_bDrawWater);
    update();
}

void CView::setDrawLand(bool bDraw)
{
    if(m_bDrawLand == bDraw)
        return;
    m_bDrawLand = bDraw;
    if(m_landscapeTools)
        m_landscapeTools->setTerrainVisibility(m_bDrawLand, m_bDrawWater);
    update();
}

void CView::activateLandscapeTilePainting()
{
    prepareLandscapeTilePainting();
    changeOperation(EButtonOpTilebrush);
}

void CView::prepareLandscapeTilePainting(bool showTools)
{
    if(!m_landscapeTools)
        return;
    if(m_landscapeTools->editMode() != ELandscapeEditMode::TilePaint)
        clearLandscapeBrushSelection();
    m_landscapeTools->activateMode(ELandscapeEditMode::TilePaint, false);
    m_lastLandscapeEditMode = int(ELandscapeEditMode::TilePaint);
    CScene::getInstance()->setLandTileEditMode(true);
    if(showTools)
        emit showLandscapeToolsSignal();
    emit updateMsg(tr("Land tile painting enabled. Choose an atlas tile in Landscape Tools."));
}

void CView::setLandscapeEditingEnabled(bool enabled)
{
    if(m_landscapeTools)
        m_landscapeTools->setEditingEnabled(enabled, false);
    if(!enabled)
    {
        m_terrainPaintOutlineValid = false;
        m_sculptOutlineValid = false;
        update();
    }
    syncLandscapeToolState();
}

void CView::setLandscapeInteractionSuspended(bool suspended)
{
    if(m_landscapeInteractionSuspended == suspended)
        return;
    m_landscapeInteractionSuspended = suspended;
    finishSculptStroke();
    endTileBrushGroup();
    m_sculptRepeatTimer->stop();
    m_sculptOutlineValid = false;
    m_terrainPaintOutlineValid = false;
    setPreviewTile(false);
    unsetCursor();
    if(suspended && m_pOp
       && m_pOp->operationMethod() != eOperationMethodTileBrush
       && m_pOp->operationMethod() != eOperationMethodSelect)
        cancelPendingOperation();
    update();
}

void CView::closeLandscapeTools()
{
    emit hideLandscapeToolsSignal();
}

void CView::syncLandscapeToolState()
{
    finishSculptStroke();
    const bool markingPaintIssue = m_landscapeTools
        && m_landscapeTools->paintIssueMarkingEnabled();
    if(!markingPaintIssue)
    {
        m_paintIssueDragActive = false;
        m_paintIssueHoverValid = false;
        m_paintIssueSelectedCells.clear();
        QToolTip::hideText();
    }
    if(m_landscapeTools)
    {
        const ELandscapeEditMode current = m_landscapeTools->editMode();
        if(current != ELandscapeEditMode::Sculpt
           && current != ELandscapeEditMode::OverlaySculpt)
            m_sculptOutlineValid = false;
        if(m_lastLandscapeEditMode >= 0
           && landscapeModeChangeRequiresBrushReset(
               ELandscapeEditMode(m_lastLandscapeEditMode), current))
            clearLandscapeBrushSelection();
        m_lastLandscapeEditMode = int(current);
    }
    if(!m_landscapeTools || !m_landscapeTools->editingEnabled())
    {
        if(m_pOp && m_pOp->operationMethod() == eOperationMethodTileBrush)
            changeOperation(EButtonOpSelect);
        setPreviewTile(false);
        return;
    }
    if(markingPaintIssue)
    {
        endTileBrushGroup();
        if(m_pOp && m_pOp->operationMethod() == eOperationMethodTileBrush)
            changeOperation(EButtonOpSelect, true);
        setPreviewTile(false);
        unsetCursor();
        emit updateMsg(tr("Paint issue marking mode: %1. Drag across every bad tile; normal painting is disabled until you choose a material brush.")
                       .arg(paintIssueCategoryLabel(
                           m_landscapeTools->paintIssueCategory())));
        update();
        return;
    }
    switch(m_landscapeTools->editMode())
    {
    case ELandscapeEditMode::Sculpt:
        if(m_pOp && m_pOp->operationMethod() == eOperationMethodTileBrush)
            changeOperation(EButtonOpSelect);
        setPreviewTile(false);
        emit updateMsg(tr("Landscape sculpt mode enabled."));
        break;
    case ELandscapeEditMode::TilePaint:
        CScene::getInstance()->setLandTileEditMode(true);
        changeOperation(EButtonOpTilebrush);
        emit updateMsg(tr("Land tile painting enabled. Choose an atlas tile in Landscape Tools."));
        break;
    case ELandscapeEditMode::OverlaySurface:
        CScene::getInstance()->setLandTileEditMode(false);
        changeOperation(EButtonOpTilebrush);
        emit updateMsg(tr("Overlay surface coverage enabled. Add targets an existing overlay first and otherwise projects to land; Remove targets overlays only."));
        break;
    case ELandscapeEditMode::OverlayPaint:
        CScene::getInstance()->setLandTileEditMode(false);
        changeOperation(EButtonOpTilebrush);
        emit updateMsg(tr("Overlay painting enabled for existing overlay cells only."));
        break;
    case ELandscapeEditMode::OverlaySculpt:
        if(m_pOp && m_pOp->operationMethod() == eOperationMethodTileBrush)
            changeOperation(EButtonOpSelect);
        CScene::getInstance()->setLandTileEditMode(false);
        setPreviewTile(false);
        emit updateMsg(tr("Overlay-only sculpting enabled for active surfaces."));
        break;
    }
    ensureManualTileSelection();
}

void CView::clearLandscapeBrushSelection()
{
    if(m_pTileForm)
        m_pTileForm->clearTileSelection();
    if(m_landscapeTools)
        m_landscapeTools->selectPaintTerrainType(-1);
    setPreviewTile(false);
    m_terrainPaintOutlineValid = false;
    unsetCursor();
}

void CView::ensureManualTileSelection()
{
    if(!m_landscapeTools || !m_pTileForm
       || !m_landscapeTools->manualTileSelectionEnabled()
       || !m_terrainAtlasAnalysis.usable)
        return;
    const ETerrainTileLayer layer = m_landscapeTools->editMode()
            == ELandscapeEditMode::OverlayPaint
        ? ETerrainTileLayer::Overlay : ETerrainTileLayer::Land;
    const CTerrainTransitionCatalog& catalog = layer == ETerrainTileLayer::Overlay
            && m_landscapeTools->landFamiliesAllowedInOverlay()
        ? m_terrainAtlasAnalysis.overlayCatalogWithLandFamilies()
        : m_terrainAtlasAnalysis.catalog(layer);
    int selectedIndex = -1;
    int selectedRotation = 0;
    m_pTileForm->getSelectedTile(selectedIndex, selectedRotation);
    bool selectedValid = false;
    if(selectedIndex >= 0)
    {
        SMprPackedTile selected;
        selected.atlasIndex = quint8(selectedIndex / 64);
        selected.tileIndex = quint8(selectedIndex % 64);
        selected.rotation = quint8(selectedRotation & 3);
        catalog.signatureForPacked(selected.encode(), &selectedValid);
    }
    if(selectedValid)
        return;
    for(int slot = 0; slot < m_pTileForm->tileImages().size(); ++slot)
    {
        SMprPackedTile candidate;
        candidate.atlasIndex = quint8(slot / 64);
        candidate.tileIndex = quint8(slot % 64);
        bool found = false;
        catalog.signatureForPacked(candidate.encode(), &found);
        if(!found)
            continue;
        m_pTileForm->selectTile(slot);
        onChangeCursorTile(m_pTileForm->tileWithRot(slot));
        if(m_hasViewportMousePos)
        {
            const bool land = layer == ETerrainTileLayer::Land;
            updatePreviewTile(getTerrainBrushPos(m_lastViewportMousePos.x(),
                                                 m_lastViewportMousePos.y()), land);
        }
        return;
    }
}

void CView::updateReadState(EReadState state)
{
    m_aReadState[state] = true;
    qDebug() << state << " read";
}

CView::~CView()
{
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    closeMprSettingsDock();
//    for(auto& param : m_arrParamWindow)
//    {
//        param->close();
//        delete param;
//    }

    // QOpenGLBuffer/QOpenGLShaderProgram/QOpenGLTimerQuery destruction requires
    // this widget's context. MainWindow deletes CView explicitly, and without
    // reacquiring it Qt can reach QOpenGLContext::surface() through a null
    // currentContext after the window has already disappeared.
    if(m_timer)
        m_timer->stop();
    if(m_mprModifyTimer)
        m_mprModifyTimer->stop();
    ei::setCrashContext("shutdown/view-opengl-resources");
    if(isValid())
        makeCurrent();
    if(m_pLand)
        m_pLand->clearPreviewTerrainAtlas();
    ei::setCrashContext("shutdown/view-gpu-timer");
    if(m_gpuTimer)
    {
        // QOpenGLContext::surface() may still be non-null after the underlying
        // Windows surface has become unusable. Calling destroy() or the query
        // destructor during final widget teardown can then crash in Qt5Gui.
        // This is one diagnostic object at process exit; detach it and let the
        // driver release it with the context instead of racing the dead surface.
        ei::log(eLogInfo, "GPU timer query retained for process-exit context cleanup");
        m_gpuTimer->setParent(nullptr);
        m_gpuTimer = nullptr;
        m_gpuTimerPending = false;
    }
    RenderQueueContext::instance().releaseGpuResources();
    if(m_skyBuffer.isCreated())
        m_skyBuffer.destroy();
    if(m_particleBuffer.isCreated())
        m_particleBuffer.destroy();
    releaseShadowFramebuffers();
    ei::setCrashContext("shutdown/view-mobs");

    const int meshCount = CPart::sharedMeshCount();
    const quint64 meshBytes = CPart::sharedMeshBytes();
    CPart::deferGpuCleanupToContextDestruction();
    ei::log(eLogInfo, QString("Shutdown scene teardown started mobs:%1 sharedMeshes:%2 sharedMeshBytes:%3")
            .arg(m_aMob.size()).arg(meshCount).arg(meshBytes));

    for(auto& mob: m_aMob)
    {
        QElapsedTimer mobTimer;
        mobTimer.start();
        const QString mobName = mob ? mob->mobName() : QStringLiteral("<null>");
        const int nodeCount = mob ? mob->nodes().size() : 0;
        m_scriptWorkspaces->unregisterMob(mob);
        delete mob;
        ei::log(eLogInfo, QString("Shutdown MOB teardown name:%1 nodes:%2 elapsedMs:%3")
                .arg(mobName).arg(nodeCount).arg(mobTimer.elapsed()));
    }
    if(isValid() && !m_testEntranceLabelTextures.isEmpty())
    {
        makeCurrent();
        ei::gl::DeleteTextures(m_testEntranceLabelTextures.size(),
                         m_testEntranceLabelTextures.constData());
        m_testEntranceLabelTextures.clear();
        // Keep the context current for renderer member destruction below.
    }
    m_aMob.clear();

    delete m_timer;
    m_timer = nullptr;
    delete m_mprModifyTimer;
    m_mprModifyTimer = nullptr;
    // Deliberately leave the context current: shader-program members are
    // destroyed after this destructor body and before QGLWidget tears down the
    // context itself.
    ei::setCrashContext("shutdown/view-members");
    ei::log(eLogInfo, QString("Shutdown view teardown body complete elapsedMs:%1")
            .arg(shutdownTimer.elapsed()));
}


void CView::attach(CSettings* pSettings, QTableWidget* pParam, QUndoStack* pStack, CProgressView* pProgress, QLineEdit* pMouseCoord, CTreeView *pTree, CTileForm* pTileForm) //todo: use signal\slots
{
    m_pSettings = pSettings;
    initEnvironmentPreviewWidget();
    initAiGraphPreviewWidget();
    connect(m_pSettings, &CSettings::renderSettingsChanged,
            this, &CView::applyRenderScheduling);
    m_cam->attachSettings(pSettings);
    m_cam->reset();
    m_tableManager.reset(new CTableManager(pParam));
    QObject::connect(m_tableManager.get(), SIGNAL(onUpdateProperty(const QSharedPointer<IPropertyBase>)), this, SLOT(onParamChange(const QSharedPointer<IPropertyBase>&)));
    QObject::connect(m_tableManager.get(), SIGNAL(onUpdateProperties(const QList<QSharedPointer<IPropertyBase>>&)),
                     this, SLOT(onPropertiesChange(const QList<QSharedPointer<IPropertyBase>>&)));
    m_pUndoStack = pStack;
    m_lastUndoStackIndex = m_pUndoStack ? m_pUndoStack->index() : -1;
    connect(m_pUndoStack, &QUndoStack::indexChanged, this, [this](int index)
    {
        bool selectionOnly = m_lastUndoStackIndex >= 0
            && index != m_lastUndoStackIndex && m_pUndoStack;
        const int first = qMin(index, m_lastUndoStackIndex);
        const int last = qMax(index, m_lastUndoStackIndex);
        if(first < 0 || last > (m_pUndoStack ? m_pUndoStack->count() : 0))
            selectionOnly = false;
        for(int commandIndex = first; selectionOnly && commandIndex < last; ++commandIndex)
            selectionOnly = dynamic_cast<const CSelectionCommand*>(
                m_pUndoStack->command(commandIndex)) != nullptr;
        if(!selectionOnly)
            refreshMobDirtyStates();
        bool navigationChange = false;
        for(int commandIndex = first; !navigationChange && commandIndex < last;
            ++commandIndex)
        {
            const QUndoCommand* command = m_pUndoStack->command(commandIndex);
            navigationChange = dynamic_cast<const CSelectionCommand*>(command) == nullptr
                && dynamic_cast<const CTerrainTilePreviewCommand*>(command) == nullptr;
        }
        if(navigationChange)
            scheduleAiGraphPreviewRegeneration();
        m_lastUndoStackIndex = index;
        m_spatialIndexDirty = true;
    });
    connect(this, &CView::selectionChanged, this, [this]()
    {
        m_spatialIndexDirty = true;
    });

    m_pProgress = pProgress;
    m_pOp.reset(new COperation(new CSelect(this)));
    m_pOp->attachCam(m_cam.get());
    m_cam->attachKeyManager(m_pOp->keyManager());
    m_pOp->attachMouseCoordFiled(pMouseCoord);
    CLogger::getInstance()->attachSettings(pSettings);

    m_pTree = pTree;
    m_pTree->attachView(this);
    m_pTileForm = pTileForm;
    connect(m_pTileForm, &CTileForm::onSelect, this, [this](const QPixmap& pixmap)
    {
        onChangeCursorTile(pixmap);
        int index = -1;
        int rotation = 0;
        m_pTileForm->getSelectedTile(index, rotation);
        if(m_landscapeTools && index >= 0 && m_terrainAtlasAnalysis.usable)
        {
            SMprPackedTile packed;
            packed.atlasIndex = quint8(index / 64);
            packed.tileIndex = quint8(index % 64);
            packed.rotation = quint8(rotation & 3);
            const ETerrainTileLayer layer = m_landscapeTools->editMode()
                    == ELandscapeEditMode::OverlayPaint
                ? ETerrainTileLayer::Overlay : ETerrainTileLayer::Land;
            const CTerrainTransitionCatalog& catalog =
                layer == ETerrainTileLayer::Overlay
                    && m_landscapeTools->landFamiliesAllowedInOverlay()
                ? m_terrainAtlasAnalysis.overlayCatalogWithLandFamilies()
                : m_terrainAtlasAnalysis.catalog(layer);
            bool found = false;
            const STerrainTransitionSignature signature =
                catalog.signatureForPacked(packed.encode(), &found);
            if(found && signature.northWest == signature.northEast
               && signature.northWest == signature.southEast
               && signature.northWest == signature.southWest)
                m_landscapeTools->selectPaintTerrainType(signature.northWest);
            else
                m_landscapeTools->selectPaintTerrainType(-1);
        }
    });

    // MOB-only workspaces need the same responsive viewport as terrain-backed
    // workspaces. Previously this timer was started only by loadLandscape().
    // Swap interval controls normal display pacing. A zero-interval precise
    // timer removes the former artificial ~67 FPS ceiling and lets benchmark
    // mode measure uncapped throughput.
    m_timer->setTimerType(Qt::PreciseTimer);
    applyRenderScheduling();
    m_timer->start();
}

void CView::initEnvironmentPreviewWidget()
{
    if(m_environmentPreviewWidget || !m_pSettings)
        return;
    m_environmentPreviewWidget = new QFrame(this);
    m_environmentPreviewWidget->setObjectName("environmentPreviewWidget");
    m_environmentPreviewWidget->setMinimumWidth(220);
    m_environmentPreviewWidget->setFrameShape(QFrame::StyledPanel);
    m_environmentPreviewWidget->setStyleSheet(
        "QFrame#environmentPreviewWidget { background: rgba(25,25,25,210); color: white; border: 1px solid #777; border-radius: 4px; }"
        "QLabel, QComboBox, QToolButton { color: white; }");
    QVBoxLayout* outer = new QVBoxLayout(m_environmentPreviewWidget);
    outer->setContentsMargins(7, 5, 7, 6);
    outer->setSpacing(4);
    QHBoxLayout* title = new QHBoxLayout;
    title->setContentsMargins(0, 0, 0, 0);
    QLabel* titleLabel = new QLabel(tr("Lighting mode"), m_environmentPreviewWidget);
    m_environmentPreviewCollapse = new QToolButton(m_environmentPreviewWidget);
    m_environmentPreviewCollapse->setText(QString::fromUtf8("\xE2\x88\x92"));
    m_environmentPreviewCollapse->setToolTip(tr("Minimize lighting mode"));
    title->addWidget(titleLabel);
    title->addStretch(1);
    title->addWidget(m_environmentPreviewCollapse);
    outer->addLayout(title);

    m_environmentPreviewBody = new QWidget(m_environmentPreviewWidget);
    QVBoxLayout* body = new QVBoxLayout(m_environmentPreviewBody);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(4);
    m_environmentPreviewMode = new QComboBox(m_environmentPreviewBody);
    m_environmentPreviewMode->addItem(tr("Unlit"), 0);
    m_environmentPreviewMode->addItem(tr("Simple shading"), 1);
    m_environmentPreviewMode->addItem(tr("Full environment"), 2);
    body->addWidget(m_environmentPreviewMode);

    m_timeOfDayWidget = new QFrame(this);
    m_timeOfDayWidget->setObjectName("timeOfDayWidget");
    m_timeOfDayWidget->setMinimumWidth(220);
    m_timeOfDayWidget->setFrameShape(QFrame::StyledPanel);
    m_timeOfDayWidget->setStyleSheet(
        "QFrame#timeOfDayWidget { background: rgba(25,25,25,210); color: white; border: 1px solid #777; border-radius: 4px; }"
        "QLabel { color: white; }");
    QVBoxLayout* timeOuter = new QVBoxLayout(m_timeOfDayWidget);
    timeOuter->setContentsMargins(7, 5, 7, 6);
    timeOuter->setSpacing(4);
    QLabel* timeTitle = new QLabel(tr("Time of day"), m_timeOfDayWidget);
    timeOuter->addWidget(timeTitle);
    QHBoxLayout* timeRow = new QHBoxLayout;
    timeRow->setContentsMargins(0, 0, 0, 0);
    m_timeOfDayLabel = new QLabel(tr("12:00"), m_timeOfDayWidget);
    m_timeOfDayLabel->setMinimumWidth(42);
    m_environmentPreviewTime = new QSlider(Qt::Horizontal, m_timeOfDayWidget);
    m_environmentPreviewTime->setRange(0, 1439);
    m_environmentPreviewTime->setSingleStep(10);
    m_environmentPreviewTime->setPageStep(60);
    m_environmentPreviewTime->setToolTip(tr(
        "Set the editor time of day. When changed, this time is also applied to JMV Test Current Map and hot reload."));
    timeRow->addWidget(m_timeOfDayLabel);
    timeRow->addWidget(m_environmentPreviewTime, 1);
    timeOuter->addLayout(timeRow);
    outer->addWidget(m_environmentPreviewBody);

    const COptInt* mode = dynamic_cast<const COptInt*>(m_pSettings->opt("renderLightingMode"));
    m_environmentPreviewMode->setCurrentIndex(
        qBound(0, mode ? mode->value() : 2, 2));
    connect(m_environmentPreviewCollapse, &QToolButton::clicked, this, [this]()
    {
        const bool show = !m_environmentPreviewBody->isVisible();
        m_environmentPreviewBody->setVisible(show);
        m_environmentPreviewCollapse->setText(show ? QString::fromUtf8("\xE2\x88\x92") : "+");
        m_environmentPreviewWidget->adjustSize();
        positionEnvironmentPreviewWidget();
    });
    connect(m_environmentPreviewMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index)
    {
        if(m_environmentPreviewSyncing || !m_pSettings)
            return;
        if(COptInt* option = dynamic_cast<COptInt*>(m_pSettings->opt("renderLightingMode")))
            option->setValue(qBound(0, index, 2));
        m_pSettings->persistOptions();
        update();
    });
    connect(m_environmentPreviewTime, &QSlider::valueChanged, this, [this](int minutes)
    {
        const int hour = minutes / 60;
        const int minute = minutes % 60;
        m_timeOfDayLabel->setText(QString("%1:%2")
            .arg(hour, 2, 10, QLatin1Char('0')).arg(minute, 2, 10, QLatin1Char('0')));
        if(m_environmentPreviewSyncing || !m_pSettings)
            return;
        if(COptBool* overrideTime = dynamic_cast<COptBool*>(m_pSettings->opt("overrideWorldTime")))
            overrideTime->setValue(true);
        if(COptInt* time = dynamic_cast<COptInt*>(m_pSettings->opt("renderTimeOfDay")))
            time->setValue(hour * 100 + minute);
        m_pSettings->persistOptions();
        update();
    });
    m_environmentPreviewWidget->adjustSize();
    m_timeOfDayWidget->adjustSize();
    positionEnvironmentPreviewWidget();
    m_environmentPreviewWidget->show();
    m_timeOfDayWidget->show();
    m_environmentPreviewWidget->raise();
    m_timeOfDayWidget->raise();
    applyEnvironmentCompatibilityMode();
}

void CView::positionEnvironmentPreviewWidget()
{
    int y = 12;
    if(m_timeOfDayWidget)
    {
        m_timeOfDayWidget->move(12, y);
        y += m_timeOfDayWidget->height() + 8;
    }
    if(m_environmentPreviewWidget)
    {
        m_environmentPreviewWidget->move(12, y);
        y += m_environmentPreviewWidget->height() + 8;
    }
    if(m_aiGraphPreviewWidget)
        m_aiGraphPreviewWidget->move(12,
            qMax(12, height() - m_aiGraphPreviewWidget->height() - 12));
}

void CView::initAiGraphPreviewWidget()
{
    if(m_aiGraphPreviewWidget)
        return;
    m_aiGraphPreviewWidget = new QFrame(this);
    m_aiGraphPreviewWidget->setObjectName(QStringLiteral("aiGraphPreviewWidget"));
    m_aiGraphPreviewWidget->setFixedWidth(520);
    m_aiGraphPreviewWidget->setFrameShape(QFrame::StyledPanel);
    m_aiGraphPreviewWidget->setAttribute(Qt::WA_OpaquePaintEvent, true);
    m_aiGraphPreviewWidget->setAutoFillBackground(true);
    QPalette graphPalette = m_aiGraphPreviewWidget->palette();
    graphPalette.setColor(QPalette::Window, QColor(25, 25, 25));
    m_aiGraphPreviewWidget->setPalette(graphPalette);
    m_aiGraphPreviewWidget->setStyleSheet(
        "QFrame#aiGraphPreviewWidget { background-color: rgb(25,25,25); color: white; border: 1px solid #777; border-radius: 4px; }"
        "QWidget#aiGraphPreviewBody, QLabel { background-color: rgb(25,25,25); color: white; }"
        "QToolButton { color: white; }");
    QVBoxLayout* layout = new QVBoxLayout(m_aiGraphPreviewWidget);
    layout->setContentsMargins(7, 5, 7, 6);
    layout->setSpacing(4);

    QHBoxLayout* title = new QHBoxLayout;
    QLabel* titleLabel = new QLabel(tr("AI navigation graph"), m_aiGraphPreviewWidget);
    m_aiGraphPreviewToggle = new QToolButton(m_aiGraphPreviewWidget);
    m_aiGraphPreviewToggle->setCheckable(true);
    m_aiGraphPreviewToggle->setText(tr("Show (N)"));
    m_aiGraphPreviewCollapse = new QToolButton(m_aiGraphPreviewWidget);
    m_aiGraphPreviewCollapse->setText(QString::fromUtf8("\xE2\x88\x92"));
    m_aiGraphPreviewCollapse->setToolTip(tr("Minimize AI navigation graph"));
    title->addWidget(titleLabel);
    title->addStretch(1);
    title->addWidget(m_aiGraphPreviewToggle);
    title->addWidget(m_aiGraphPreviewCollapse);
    layout->addLayout(title);

    m_aiGraphPreviewBody = new QWidget(m_aiGraphPreviewWidget);
    m_aiGraphPreviewBody->setObjectName(QStringLiteral("aiGraphPreviewBody"));
    QVBoxLayout* body = new QVBoxLayout(m_aiGraphPreviewBody);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(4);

    m_aiGraphLayerLabel = new QLabel(m_aiGraphPreviewBody);
    m_aiGraphLayerLabel->setWordWrap(true);
    m_aiGraphLayerLabel->setMinimumWidth(480);
    body->addWidget(m_aiGraphLayerLabel);
    m_aiGraphLayerSlider = new QSlider(Qt::Horizontal, m_aiGraphPreviewBody);
    m_aiGraphLayerSlider->setRange(0, CAiGraphDocument::LayerCount - 1);
    m_aiGraphLayerSlider->setSingleStep(1);
    m_aiGraphLayerSlider->setPageStep(1);
    m_aiGraphLayerSlider->setTickPosition(QSlider::TicksBelow);
    m_aiGraphLayerSlider->setValue(3);
    body->addWidget(m_aiGraphLayerSlider);
    m_aiGraphSourceLabel = new QLabel(tr("No graph source loaded"), m_aiGraphPreviewBody);
    m_aiGraphSourceLabel->setWordWrap(true);
    m_aiGraphSourceLabel->setTextFormat(Qt::PlainText);
    m_aiGraphSourceLabel->setMinimumWidth(480);
    body->addWidget(m_aiGraphSourceLabel);

    QHBoxLayout* actions = new QHBoxLayout;
    m_aiGraphRegenerateButton = new QToolButton(m_aiGraphPreviewBody);
    m_aiGraphRegenerateButton->setText(tr("Regenerate preview"));
    m_aiGraphRegenerateButton->setToolTip(tr(
        "Build a non-saving diagnostic graph from current terrain and static-object collision proxies."));
    m_aiGraphUseLoadedButton = new QToolButton(m_aiGraphPreviewBody);
    m_aiGraphUseLoadedButton->setCheckable(true);
    m_aiGraphUseLoadedButton->setText(tr("Loaded graph override: OFF"));
    m_aiGraphUseLoadedButton->setToolTip(tr(
        "Toggle between the graph loaded from the map and a regenerated diagnostic preview."));
    actions->addWidget(m_aiGraphUseLoadedButton);
    actions->addWidget(m_aiGraphRegenerateButton);
    body->addLayout(actions);
    layout->addWidget(m_aiGraphPreviewBody);

    connect(m_aiGraphPreviewToggle, &QToolButton::toggled, this, [this](bool visible)
    {
        if(visible && !m_aiGraphDocument.isValid())
        {
            m_aiGraphPreviewToggle->setChecked(false);
            emit updateMsg(tr("No valid AI graph is available for the loaded map."));
            return;
        }
        m_aiGraphPreviewVisible = visible;
        m_aiGraphPreviewToggle->setText(visible ? tr("Hide (N)") : tr("Show (N)"));
        update();
    });
    connect(m_aiGraphPreviewCollapse, &QToolButton::clicked, this, [this]()
    {
        const bool show = !m_aiGraphPreviewBody->isVisible();
        m_aiGraphPreviewBody->setVisible(show);
        m_aiGraphPreviewCollapse->setText(show
            ? QString::fromUtf8("\xE2\x88\x92") : "+");
        m_aiGraphPreviewCollapse->setToolTip(show
            ? tr("Minimize AI navigation graph")
            : tr("Expand AI navigation graph"));
        m_aiGraphPreviewWidget->adjustSize();
        m_aiGraphPreviewWidget->update();
        positionEnvironmentPreviewWidget();
    });
    connect(m_aiGraphLayerSlider, &QSlider::valueChanged, this, [this](int)
    {
        m_aiGraphOverlayDirty = true;
        syncAiGraphPreviewWidget();
        update();
    });
    connect(m_aiGraphRegenerateButton, &QToolButton::clicked,
            this, &CView::startAiGraphPreviewRegeneration);
    connect(m_aiGraphUseLoadedButton, &QToolButton::clicked, this, [this](bool loaded)
    {
        if(!loaded)
        {
            startAiGraphPreviewRegeneration();
            return;
        }
        if(!m_aiGraphLoadedDocument.isValid())
        {
            m_aiGraphUseLoadedButton->setChecked(false);
            return;
        }
        ++m_aiGraphBuildRequest;
        m_aiGraphAutoRegenerationEnabled = false;
        m_aiGraphRegenerationPending = false;
        if(m_aiGraphRegenerationTimer)
            m_aiGraphRegenerationTimer->stop();
        m_aiGraphDocument = m_aiGraphLoadedDocument;
        m_aiGraphSource = m_aiGraphLoadedSource;
        m_aiGraphOverlayDirty = true;
        if(m_aiGraphPreviewToggle && !m_aiGraphPreviewToggle->isChecked())
            m_aiGraphPreviewToggle->setChecked(true);
        syncAiGraphPreviewWidget();
        update();
    });
    QShortcut* shortcut = new QShortcut(QKeySequence(Qt::Key_N), this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, [this]()
    {
        if(!m_aiGraphDocument.isValid())
        {
            emit updateMsg(tr("No valid AI graph is available for the loaded map."));
            return;
        }
        m_aiGraphPreviewToggle->toggle();
    });
    m_aiGraphPreviewWidget->adjustSize();
    m_aiGraphPreviewWidget->show();
    m_aiGraphPreviewWidget->raise();
    syncAiGraphPreviewWidget();
    positionEnvironmentPreviewWidget();
}

void CView::syncAiGraphPreviewWidget()
{
    if(!m_aiGraphPreviewWidget)
        return;
    const int layer = m_aiGraphLayerSlider ? m_aiGraphLayerSlider->value() : 3;
    const QVector4D color = aiGraphLayerColor(layer);
    const QColor profileColor = QColor::fromRgbF(color.x(), color.y(), color.z());
    m_aiGraphLayerLabel->setText(tr("Layer %1 - <b><span style=\"color:%2\">%3</span></b> (%4 m)")
        .arg(layer).arg(profileColor.name())
        .arg(CAiGraphDocument::layerLabel(layer).toHtmlEscaped())
        .arg(CAiGraphDocument::canonicalHeight(layer), 0, 'f', 1));
    const bool available = m_aiGraphDocument.isValid();
    m_aiGraphLayerSlider->setEnabled(available);
    m_aiGraphSourceLabel->setText(available
        ? tr("%1\n%2 x %3")
              .arg(m_aiGraphSource).arg(m_aiGraphDocument.width()).arg(m_aiGraphDocument.height())
        : tr("No graph source loaded"));
    m_aiGraphPreviewToggle->setToolTip(available
        ? tr("Show or hide the current AI graph layer in the viewport (N).")
        : tr("Load a MOB with an embedded graph or a matching external .grp file."));
    if(m_aiGraphRegenerateButton)
        m_aiGraphRegenerateButton->setEnabled(m_terrainEditAvailable
            && (!m_aiGraphBuildWatcher || !m_aiGraphBuildWatcher->isRunning()));
    if(m_aiGraphUseLoadedButton)
    {
        const bool loadedOverride = m_aiGraphLoadedDocument.isValid()
            && m_aiGraphSource == m_aiGraphLoadedSource;
        const QSignalBlocker blocker(m_aiGraphUseLoadedButton);
        m_aiGraphUseLoadedButton->setChecked(loadedOverride);
        m_aiGraphUseLoadedButton->setText(loadedOverride
            ? tr("Loaded graph override: ON")
            : tr("Loaded graph override: OFF"));
        m_aiGraphUseLoadedButton->setEnabled(m_aiGraphLoadedDocument.isValid());
    }
    if(m_aiGraphPreviewBody->layout())
        m_aiGraphPreviewBody->layout()->activate();
    m_aiGraphPreviewWidget->adjustSize();
    m_aiGraphPreviewWidget->update();
    positionEnvironmentPreviewWidget();
}

void CView::refreshAiGraphPreviewSource()
{
    ++m_aiGraphBuildRequest;
    m_aiGraphAutoRegenerationEnabled = false;
    m_aiGraphRegenerationPending = false;
    if(m_aiGraphRegenerationTimer)
        m_aiGraphRegenerationTimer->stop();
    m_aiGraphDocument.clear();
    m_aiGraphLoadedDocument.clear();
    m_aiGraphSource.clear();
    m_aiGraphLoadedSource.clear();
    m_aiGraphOpenLinks.clear();
    m_aiGraphNodes.clear();
    m_aiGraphIsolatedNodes.clear();
    m_aiGraphOverlayDirty = true;

    QByteArray sourceBytes;
    CMob* sourceMob = nullptr;
    if(m_environmentMob && !m_environmentMob->aiGraphData().isEmpty())
        sourceMob = m_environmentMob;
    if(sourceMob)
    {
        sourceBytes = sourceMob->aiGraphData();
        m_aiGraphSource = tr("Embedded: %1").arg(sourceMob->filePath().fileName());
    }
    else if(m_pLand && m_pLand->isMprLoad())
    {
        const QFileInfo mpr = m_pLand->filePath();
        const QFileInfo external(QDir(mpr.absolutePath()).filePath(mpr.completeBaseName() + ".grp"));
        QFile file(external.absoluteFilePath());
        if(file.open(QIODevice::ReadOnly))
        {
            sourceBytes = file.readAll();
            m_aiGraphSource = tr("External: %1").arg(external.fileName());
        }
    }

    if(!sourceBytes.isEmpty())
    {
        QString error;
        QStringList warnings;
        if(!m_aiGraphDocument.parse(sourceBytes, &error, &warnings))
        {
            ei::log(eLogWarning, QString("AI graph preview rejected %1: %2")
                    .arg(m_aiGraphSource, error));
            m_aiGraphSource.clear();
        }
        else
        {
            ei::log(eLogInfo, QString("AI graph preview source %1 dimensions:%2x%3")
                    .arg(m_aiGraphSource).arg(m_aiGraphDocument.width())
                    .arg(m_aiGraphDocument.height()));
            for(const QString& warning : warnings)
                ei::log(eLogWarning, "AI graph preview: " + warning);
            m_aiGraphLoadedDocument = m_aiGraphDocument;
            m_aiGraphLoadedSource = m_aiGraphSource;
        }
    }
    if(!m_aiGraphDocument.isValid())
    {
        m_aiGraphPreviewVisible = false;
        if(m_aiGraphPreviewToggle)
            m_aiGraphPreviewToggle->setChecked(false);
    }
    syncAiGraphPreviewWidget();
    update();
}

bool CView::captureAiGraphPreviewBuildInput(SAiGraphPreviewBuildInput* input,
                                            QString* error)
{
    if(error)
        error->clear();
    if(!input || !m_terrainEditAvailable || !m_pLand || !m_pLand->isMprLoad())
    {
        if(error)
            *error = tr("Load an MPR with canonical terrain data first.");
        return false;
    }

    input->graphWidth = m_aiGraphLoadedDocument.isValid()
        ? m_aiGraphLoadedDocument.width()
        : qCeil(float(m_terrainEdit.vertexCountX() - 1) / 4.0f);
    input->graphHeight = m_aiGraphLoadedDocument.isValid()
        ? m_aiGraphLoadedDocument.height()
        : qCeil(float(m_terrainEdit.vertexCountY() - 1) / 4.0f);
    if(input->graphWidth * 4 != m_terrainEdit.vertexCountX() - 1
       || input->graphHeight * 4 != m_terrainEdit.vertexCountY() - 1)
    {
        if(error)
            *error = tr("AI graph dimensions do not match the loaded terrain bounds.");
        return false;
    }
    const int aiWidth = input->graphWidth * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    const int aiHeight = input->graphHeight * CAiGraphPreviewBuilder::AiTilesPerGraphCell;
    input->terrainHeights.resize(aiWidth * aiHeight);
    input->waterDepths.fill(0.0f, aiWidth * aiHeight);
    input->terrainTypes.fill(quint8(eUndefined), aiWidth * aiHeight);
    const QVector<SMaterial>& materials = m_pLand->materials();
    for(int y = 0; y < aiHeight; ++y)
        for(int x = 0; x < aiWidth; ++x)
        {
            const int index = y * aiWidth + x;
            input->terrainHeights[index] = m_terrainEdit.navigationHeightSample(
                x, y, ETerrainTileLayer::Land);
            const int globalTileX = x / 4;
            const int globalTileY = y / 4;
            bool landAvailable = false;
            const quint16 landPacked = m_terrainEdit.packedTile(
                globalTileX, globalTileY, ETerrainTileLayer::Land, &landAvailable);
            bool typeFound = false;
            const qint32 terrainType = landAvailable
                ? m_terrainEdit.tileType(landPacked, &typeFound) : qint32(eUndefined);
            if(typeFound && terrainType >= 0 && terrainType < eLast)
                input->terrainTypes[index] = quint8(terrainType);

            bool overlayAvailable = false;
            const qint16 materialIndex = m_terrainEdit.overlayMaterialIndex(
                globalTileX, globalTileY, &overlayAvailable);
            if(overlayAvailable && materialIndex >= 0 && materialIndex < materials.size()
               && materials.at(materialIndex).type == eTerrainWater)
            {
                bool heightAvailable = false;
                const float overlayHeight = m_terrainEdit.navigationHeightSample(
                    x, y, ETerrainTileLayer::Overlay, &heightAvailable);
                if(heightAvailable)
                    input->waterDepths[index] = qMax(
                        0.0f, overlayHeight - input->terrainHeights.at(index));
            }
        }

    QString traversalError;
    input->terrainTraversal = CAiGraphPreviewBuilder::parseTerrainTraversalRegistry(
        CObjectList::getInstance()->resourceData(QStringLiteral("tileDesc.reg")),
        &traversalError);
    if(input->terrainTraversal.isEmpty())
        ei::log(eLogWarning, "AI graph preview: " + traversalError);

    input->collisionProxies.clear();
    input->collisionMetadataApproximate = false;
    for(CMob* mob : m_aMob)
        if(mob)
            for(CNode* node : mob->nodes())
            {
                if(!node || node->isMarkDeleted()
                   || (node->nodeType() != eWorldObject && node->nodeType() != eLever))
                    continue;
                CObjectBase* object = dynamic_cast<CObjectBase*>(node);
                if(!object)
                    continue;
                for(const SNavigationCollisionProxy& source : object->navigationCollisionProxies())
                {
                    SAiGraphCollisionProxy proxy;
                    proxy.footprint = source.footprint;
                    proxy.minimumZ = source.minimumZ;
                    proxy.maximumZ = source.maximumZ;
                    switch(source.kind)
                    {
                    case ENavigationVolumeKind::Hard:
                        proxy.passability = EAiGraphObjectPassability::Hard; break;
                    case ENavigationVolumeKind::Easy:
                        proxy.passability = EAiGraphObjectPassability::Easy; break;
                    case ENavigationVolumeKind::Danger:
                        proxy.passability = EAiGraphObjectPassability::Danger; break;
                    case ENavigationVolumeKind::Bridge:
                        proxy.passability = EAiGraphObjectPassability::Bridge; break;
                    case ENavigationVolumeKind::Ignored:
                        proxy.passability = EAiGraphObjectPassability::Ignored; break;
                    case ENavigationVolumeKind::Blocking:
                    default:
                        proxy.passability = EAiGraphObjectPassability::Blocking; break;
                    }
                    proxy.authored = source.authored;
                    proxy.objectId = source.objectId;
                    input->collisionProxies.append(proxy);
                    input->collisionMetadataApproximate |= !source.authored;
                }
            }
    return true;
}

bool CView::buildGeneratedAiGraphCandidate(QByteArray* data, QString* error)
{
    if(data)
        data->clear();
    if(!data)
    {
        if(error)
            *error = tr("No generated AI graph output buffer was provided.");
        return false;
    }
    SAiGraphPreviewBuildInput input;
    if(!captureAiGraphPreviewBuildInput(&input, error))
        return false;
    const SAiGraphPreviewBuildResult result = CAiGraphPreviewBuilder::build(input);
    if(!result.valid)
    {
        if(error)
            *error = result.error;
        return false;
    }
    const QByteArray candidate = result.document.serialize(error);
    if(candidate.isEmpty())
        return false;
    CAiGraphDocument reopened;
    QString reopenError;
    if(!reopened.parse(candidate, &reopenError))
    {
        if(error)
            *error = tr("Generated AI graph failed fresh validation: %1")
                .arg(reopenError);
        return false;
    }
    *data = candidate;
    return true;
}

void CView::startAiGraphPreviewRegeneration()
{
    if(m_aiGraphBuildWatcher && m_aiGraphBuildWatcher->isRunning())
        return;
    if(m_aiGraphUseLoadedButton)
    {
        const QSignalBlocker blocker(m_aiGraphUseLoadedButton);
        m_aiGraphUseLoadedButton->setChecked(false);
        m_aiGraphUseLoadedButton->setText(tr("Loaded graph override: OFF"));
    }
    m_aiGraphRegenerationPending = false;
    if(m_aiGraphRegenerationTimer)
        m_aiGraphRegenerationTimer->stop();
    SAiGraphPreviewBuildInput input;
    QString error;
    if(!captureAiGraphPreviewBuildInput(&input, &error))
    {
        emit updateMsg(error);
        return;
    }
    m_aiGraphAutoRegenerationEnabled = true;

    if(!m_aiGraphBuildWatcher)
    {
        m_aiGraphBuildWatcher = new QFutureWatcher<SAiGraphPreviewBuildResult>(this);
        connect(m_aiGraphBuildWatcher, &QFutureWatcher<SAiGraphPreviewBuildResult>::finished,
                this, [this]()
        {
            const quint64 request = m_aiGraphBuildWatcher->property("requestId").toULongLong();
            const SAiGraphPreviewBuildResult result = m_aiGraphBuildWatcher->result();
            if(request != m_aiGraphBuildRequest)
            {
                syncAiGraphPreviewWidget();
                if(m_aiGraphRegenerationPending && m_aiGraphRegenerationTimer)
                    m_aiGraphRegenerationTimer->start();
                return;
            }
            if(!result.valid)
            {
                emit updateMsg(tr("AI graph preview regeneration failed: %1").arg(result.error));
                syncAiGraphPreviewWidget();
                if(m_aiGraphRegenerationPending && m_aiGraphRegenerationTimer)
                    m_aiGraphRegenerationTimer->start();
                return;
            }
            m_aiGraphDocument = result.document;
            m_aiGraphSource = tr("Regenerated: %1/%2 authored collision parts, %3 bridge cells, %4 water samples")
                .arg(result.authoredCollisionProxyCount).arg(result.collisionProxyCount)
                .arg(result.bridgeAiTiles).arg(result.waterAiTiles);
            m_aiGraphOverlayDirty = true;
            if(m_aiGraphPreviewToggle && !m_aiGraphPreviewToggle->isChecked())
                m_aiGraphPreviewToggle->setChecked(true);
            ei::log(eLogInfo, QString("AI graph diagnostic regeneration complete dimensions:%1x%2 proxies:%3 authored_proxies:%4 bridge_tiles:%5 human_blocked_tiles:%6")
                .arg(result.document.width()).arg(result.document.height())
                .arg(result.collisionProxyCount).arg(result.authoredCollisionProxyCount)
                .arg(result.bridgeAiTiles).arg(result.blockedAiTiles));
            for(const QString& warning : result.warnings)
                ei::log(eLogWarning, "AI graph preview: " + warning);
            syncAiGraphPreviewWidget();
            update();
            if(m_aiGraphRegenerationPending && m_aiGraphRegenerationTimer)
                m_aiGraphRegenerationTimer->start();
        });
    }
    const quint64 request = ++m_aiGraphBuildRequest;
    m_aiGraphBuildWatcher->setProperty("requestId", QVariant::fromValue(request));
    m_aiGraphRegenerateButton->setEnabled(false);
    m_aiGraphSourceLabel->setText(tr("Regenerating diagnostic graph..."));
    ei::log(eLogInfo, QString("AI graph diagnostic regeneration started dimensions:%1x%2 proxies:%3")
        .arg(input.graphWidth).arg(input.graphHeight).arg(input.collisionProxies.size()));
    m_aiGraphBuildWatcher->setFuture(QtConcurrent::run([input]()
    {
        return CAiGraphPreviewBuilder::build(input);
    }));
}

void CView::scheduleAiGraphPreviewRegeneration()
{
    if(!m_aiGraphAutoRegenerationEnabled || !m_terrainEditAvailable
       || !m_aiGraphRegenerationTimer)
        return;
    m_aiGraphRegenerationPending = true;
    m_aiGraphRegenerationTimer->start();
}

void CView::applyEnvironmentCompatibilityMode()
{
    if(!m_openGlesCompatibility || !m_environmentPreviewMode || !m_pSettings)
        return;

    // Qt 5's ANGLE fallback exposes only OpenGL ES 2.0. The desktop Full
    // environment path relies on shadow-map framebuffer formats and sky-pass
    // state that ES2 rejects (GL_INVALID_OPERATION/GL_INVALID_ENUM). Keep
    // ANGLE useful as an explicit compatibility renderer instead of silently
    // presenting a broken Full option.
    if(QStandardItemModel* model = qobject_cast<QStandardItemModel*>(
            m_environmentPreviewMode->model()))
    {
        if(QStandardItem* fullEnvironment = model->item(2))
        {
            fullEnvironment->setEnabled(false);
            fullEnvironment->setText(tr("Full environment (unavailable in ANGLE)"));
            fullEnvironment->setToolTip(tr(
                "ANGLE uses OpenGL ES 2.0; resource sky and shadow maps require the desktop renderer."));
        }
    }

    const COptInt* option = dynamic_cast<const COptInt*>(m_pSettings->opt("renderLightingMode"));
    m_environmentPreviewSyncing = true;
    m_environmentPreviewMode->setCurrentIndex(option
        ? qBound(0, option->value(), 1) : 1);
    m_environmentPreviewSyncing = false;
    m_environmentPreviewMode->setToolTip(tr(
        "ANGLE compatibility renderer: Unlit and Simple shading are available."));
}

void CView::syncEnvironmentPreviewWidget(float mapTimeHours)
{
    if(!m_environmentPreviewWidget || !m_pSettings)
        return;
    const COptBool* overrideTime = dynamic_cast<const COptBool*>(m_pSettings->opt("overrideWorldTime"));
    const COptInt* storedTime = dynamic_cast<const COptInt*>(m_pSettings->opt("renderTimeOfDay"));
    int minutes = qRound(mapTimeHours * 60.0f) % 1440;
    if(overrideTime && overrideTime->value() && storedTime)
        minutes = (storedTime->value() / 100) * 60 + qMin(59, storedTime->value() % 100);
    const COptInt* mode = dynamic_cast<const COptInt*>(m_pSettings->opt("renderLightingMode"));
    m_environmentPreviewSyncing = true;
    const int maximumMode = m_openGlesCompatibility ? 1 : 2;
    m_environmentPreviewMode->setCurrentIndex(
        qBound(0, mode ? mode->value() : maximumMode, maximumMode));
    if(!m_environmentPreviewTime->isSliderDown())
        m_environmentPreviewTime->setValue(qBound(0, minutes, 1439));
    m_environmentPreviewSyncing = false;
}

void CView::resizeEvent(QResizeEvent* event)
{
    QGLWidget::resizeEvent(event);
    positionEnvironmentPreviewWidget();
}

void CView::applyRenderScheduling()
{
    const COptInt* capOption = m_pSettings
        ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderFpsCap")) : nullptr;
    const bool benchmarkMode = qApp->property("renderBenchmark").toBool();
    const int configuredCap = capOption ? qBound(0, capOption->value(), 1000) : 0;
    const int effectiveCap = benchmarkMode ? 0 : configuredCap;
    m_timer->setInterval(ei::renderTimerIntervalMs(effectiveCap));
    ei::log(eLogInfo, QString("Render scheduler fps-cap:%1 timer-ms:%2 benchmark-override:%3")
            .arg(effectiveCap == 0 ? QString("unlimited") : QString::number(effectiveCap))
            .arg(m_timer->interval())
            .arg(benchmarkMode ? "yes" : "no"));
}

void CView::refreshAfterResourceSetup()
{
    CObjectList::getInstance()->refreshConfiguredSources();
    CTextureList::getInstance()->refreshConfiguredSources();
    m_terrainGlobalUniformTileCache.clear();
    m_terrainGlobalTransitionEncodedPageCache.clear();
    m_terrainGlobalTransitionMissingPages.clear();
    m_terrainGlobalTransitionTileCache.clear();
    m_spatialIndexDirty = true;
    m_skyResourcesChecked = false;
    m_staticShadowFingerprint.clear();
    m_farStaticShadowFingerprint.clear();
    updateGeometry();
    if(width() > 0 && height() > 0)
        resizeGL(width(), height());
    updateGL();
    ei::log(eLogInfo, "Viewport and resource lists refreshed after game/mod setup");
}

void CView::updateWindow()
{
    updateGL();
}

void CView::rebuildSpatialIndices()
{
    m_spatialIndices.clear();
    m_spatialOverflowNodes.clear();
    for(CMob* mob : m_aMob)
    {
        QSharedPointer<CSpatialGrid<CNode*>> grid(new CSpatialGrid<CNode*>(32.0f));
        QVector<CNode*> overflow;
        for(CNode* node : mob->nodes())
        {
            if(!node || node->isMarkDeleted() || node->nodeState() == ENodeState::eHidden)
                continue;
            const bool selected = node->nodeState() == ENodeState::eSelect;
            CBox bounds = node->getBBox();
            const QVector3D center = bounds.center() + node->drawPosition();
            const float radius = bounds.radius();
            const bool invalid = !bounds.isInit() || !qIsFinite(center.x())
                || !qIsFinite(center.y()) || !qIsFinite(center.z()) || !qIsFinite(radius);
            bool oversized = false;
            if(!invalid)
            {
                const int minX = qFloor((center.x() - radius) / 32.0f);
                const int maxX = qFloor((center.x() + radius) / 32.0f);
                const int minY = qFloor((center.y() - radius) / 32.0f);
                const int maxY = qFloor((center.y() + radius) / 32.0f);
                oversized = qint64(maxX - minX + 1) * qint64(maxY - minY + 1) > 64;
            }
            if(invalid || oversized)
                overflow.append(node);
            if(selected || invalid)
                grid->insert(node, node->drawPosition(), 0.0f, true);
            else
                grid->insert(node, center, radius);
        }
        m_spatialIndices.insert(mob, grid);
        m_spatialOverflowNodes.insert(mob, overflow);
        if(!overflow.isEmpty())
            ei::log(eLogWarning, QString("Spatial grid retained %1 diagnostic overflow nodes for %2")
                    .arg(overflow.size()).arg(mob->mobName()));
    }
    m_spatialIndexDirty = false;
}

QVector<CNode*> CView::spatiallyVisibleNodes(CMob* mob, const CViewFrustum* frustum,
                                             SSpatialQueryStats* stats)
{
    if(m_spatialIndexDirty)
        rebuildSpatialIndices();
    const QSharedPointer<CSpatialGrid<CNode*>> grid = m_spatialIndices.value(mob);
    if(!grid)
    {
        QVector<CNode*> nodes;
        if(mob)
            for(CNode* node : mob->nodes())
                nodes.append(node);
        return nodes;
    }
    return grid->query(frustum, stats);
}

QString CView::sceneSourceFingerprint()
{
    if(!m_sceneFingerprintDirty)
        return m_sceneFingerprint;
    QCryptographicHash sceneHash(QCryptographicHash::Sha256);
    const auto addFile = [&sceneHash](const QFileInfo& info)
    {
        sceneHash.addData(info.fileName().toLower().toUtf8());
        QFile file(info.absoluteFilePath());
        QCryptographicHash fileHash(QCryptographicHash::Sha256);
        if(file.open(QIODevice::ReadOnly))
            while(!file.atEnd())
                fileHash.addData(file.read(1024 * 1024));
        sceneHash.addData(fileHash.result());
    };
    if(m_pLand && m_pLand->isMprLoad())
        addFile(m_pLand->filePath());
    for(CMob* mob : m_aMob)
        addFile(mob->filePath());
    m_sceneFingerprint = QString::fromLatin1(sceneHash.result().toHex());
    m_sceneFingerprintDirty = false;
    return m_sceneFingerprint;
}

void CView::initShaders()
{
    const auto reportShaderFailure = [](const char* stage,
                                        const QOpenGLShaderProgram& program)
    {
        ei::log(eLogWarning, QStringLiteral("OpenGL shader failure (%1): %2")
            .arg(QString::fromLatin1(stage), program.log()));
    };
    // Compile vertex shader
    if (!m_program.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/vshader.vsh"))
    {
        reportShaderFailure("object vertex", m_program);
        close();
        return;
    }

    // Compile fragment shader
    if (!m_program.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/fshader.fsh"))
    {
        reportShaderFailure("object fragment", m_program);
        close();
        return;
    }

    // Link shader pipeline
    if (!m_program.link())
    {
        reportShaderFailure("object link", m_program);
        close();
        return;
    }
    logShaderProgramDiagnostics(QStringLiteral("object"), m_program, true);

    m_program.bind();
    m_program.setUniformValue("customColor", QVector4D(0.0, 0.0, 0.0, 0.0));
    m_program.setUniformValue("u_instanced", false);
    //release shader program?

    // Compile land shader
    if (!m_landProgram.addShaderFromSourceFile(QOpenGLShader::Vertex, ":/landshader.vsh"))
    {
        reportShaderFailure("terrain vertex", m_landProgram);
        close();
        return;
    }
    if (!m_landProgram.addShaderFromSourceFile(QOpenGLShader::Fragment, ":/landshader.fsh"))  //use absolute path for recompile shader every app rise
    {
        reportShaderFailure("terrain fragment", m_landProgram);
        close();
        return;
    }
    if (!m_landProgram.link())
    {
        reportShaderFailure("terrain link", m_landProgram);
        close();
        return;
    }
    logShaderProgramDiagnostics(QStringLiteral("terrain"), m_landProgram, true);

    m_landProgram.bind();
    m_landProgram.setUniformValue("u_instanced", false);
    m_landProgram.setUniformValue("u_lightPosition", QVector4D(0.0, 0.0, 2.0, 1.0));
    m_landProgram.setUniformValue("u_lightPower", 5.0f);
    m_landProgram.setUniformValue("u_lightColor", QVector4D(1.0, 1.0, 1.0, 1.0));
    m_landProgram.setUniformValue("u_highlight", false);
    m_landProgram.setUniformValue("u_solidColorEnabled", false);
    m_landProgram.setUniformValue("u_overlayMotionEnabled", false);
    m_landProgram.setUniformValue("u_overlayTimeSeconds", 0.0f);
    m_landProgram.setUniformValue("u_overlayWindDirection", QVector3D());
    m_landProgram.setUniformValue("u_overlayWindStrength", 0.0f);

    bool skySuccess = m_skyProgram.addShaderFromSourceFile(
        QOpenGLShader::Vertex, ":/skybox.vsh");
    skySuccess = skySuccess && m_skyProgram.addShaderFromSourceFile(
        QOpenGLShader::Fragment, ":/skybox.fsh");
    skySuccess = skySuccess && m_skyProgram.link();
    logShaderProgramDiagnostics(QStringLiteral("sky"), m_skyProgram, skySuccess);
    if(!skySuccess)
    {
        close();
        return;
    }
    m_skyProgram.bind();
    m_skyProgram.setUniformValue("qt_Texture0", 0);

    // Keep the particle subsystem out of this isolation build. A failed or
    // incomplete auxiliary program must not poison an otherwise valid
    // terrain-only GL context (notably the ANGLE fallback).

    bool shadowSuccess = m_shadowProgram.addShaderFromSourceFile(
        QOpenGLShader::Vertex, ":/vshader.vsh");
    shadowSuccess = shadowSuccess && m_shadowProgram.addShaderFromSourceFile(
        QOpenGLShader::Fragment, ":/shadow.fsh");
    shadowSuccess = shadowSuccess && m_shadowProgram.link();
    logShaderProgramDiagnostics(QStringLiteral("shadow"), m_shadowProgram,
                                shadowSuccess);
    if(!shadowSuccess)
    {
        close();
        return;
    }
    m_shadowProgram.bind();
    m_shadowProgram.setUniformValue("qt_Texture0", 0);
    m_shadowProgram.setUniformValue("u_shadowAlphaTest", false);


    // Compile select shader
    bool selectSuccess = m_selectProgram.addShaderFromSourceFile(
        QOpenGLShader::Vertex, ":/vshader.vsh");
    selectSuccess = selectSuccess && m_selectProgram.addShaderFromSourceFile(
        QOpenGLShader::Fragment, ":/select.fsh");
    selectSuccess = selectSuccess && m_selectProgram.link();
    logShaderProgramDiagnostics(QStringLiteral("selection"), m_selectProgram,
                                selectSuccess);
    if(!selectSuccess)
    {
        close();
        return;
    }
    m_selectProgram.bind();
    m_selectProgram.setUniformValue("u_instanced", false);

}

void CView::initializeGL()
{
    ei::setCrashContext("opengl/initialize");
    makeCurrent();
    armOpenGlDiagnostics(QStringLiteral("OpenGL initialization"), 2);
    logOpenGlData();
    logOpenGlDiagnosticState(QStringLiteral("initialize/begin"));
    qglClearColor(Qt::black);
    //ei::gl::Enable(GL_EXT_texture_filter_anisotropic);
    ei::gl::Enable(GL_DEPTH_TEST);
    //ei::gl::Enable(GL_CULL_FACE); //draw only front faces (disable if need draw both sides)
    const QOpenGLContext* initialContext = QOpenGLContext::currentContext();
    m_openGlesCompatibility = initialContext && initialContext->isOpenGLES();
    applyEnvironmentCompatibilityMode();
    const bool fixedAlphaTest = initialContext && !initialContext->isOpenGLES();
    if(fixedAlphaTest)
    {
        ei::gl::AlphaFunc(GL_GREATER, 0.1f);
        ei::gl::Enable(GL_ALPHA_TEST);
    }
    ei::gl::Enable(GL_BLEND);
    ei::gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    logOpenGlErrors(QStringLiteral("initialize/fixed-state"));
    initShaders();
    logOpenGlDiagnosticState(QStringLiteral("initialize/shaders"));
    m_skyBuffer.create();
    m_skyBuffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
    ei::log(eLogInfo, "Particle GL subsystem deferred until a visible effect is submitted");
    m_skyBuffer.bind();
    QString skySource;
    const QByteArray skyFigure = CObjectList::getInstance()->resourceData(
        "nask0sky.fig", &skySource);
    QString skyDiagnostic;
    const QVector<SSkyVertex> skyVertices = decodeLegacySkyFigure(
        skyFigure, &skyDiagnostic);
    m_skyVertexCount = skyVertices.size();
    m_skyBuffer.allocate(skyVertices.constData(),
                         skyVertices.size() * int(sizeof(SSkyVertex)));
    m_skyBuffer.release();
    logOpenGlErrors(QStringLiteral("initialize/sky-buffer"));
    if(m_skyVertexCount > 0)
        ei::log(eLogInfo, QString("Profile sky geometry resolved: nask0sky.fig (%1 vertices) from %2")
                .arg(m_skyVertexCount).arg(skySource));
    else
        ei::log(eLogWarning, "Profile sky geometry unavailable: " + skyDiagnostic);
    const COptInt* shadowQualityOption = m_pSettings
        ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderShadowQuality")) : nullptr;
    if(!m_openGlesCompatibility)
        ensureShadowFramebuffers(qBound(0,
            shadowQualityOption ? shadowQualityOption->value() : 2, 2));
    else
        ei::log(eLogInfo,
            "Full environment disabled on ANGLE/OpenGL ES 2.0; shadow atlases were not created");
    // QOpenGLFramebufferObject construction may leave its newly-created FBO
    // bound. Desktop QGLWidget drivers commonly repair this before paintGL,
    // while ANGLE preserves the binding; the first scene frame would then be
    // cleared and rendered into the last shadow atlas, leaving the viewport
    // black. Explicitly restore Qt's context-owned presentation target.
    QOpenGLFramebufferObject::bindDefault();
    m_shadowUpdateTimer.start();
    logOpenGlDiagnosticState(QStringLiteral("initialize/shadow-framebuffers"));

    CTextureList::getInstance()->initResource();
    CObjectList::getInstance()->initResource();
    // Qt 5.12's ANGLE backend does not implement QOpenGLTimerQuery. Calling
    // create() there emits "Not supported on OpenGL ES" and corrupts the
    // platform query object's teardown state (observed as c0000374 in
    // ntdll immediately after initializeGL). Do not construct the wrapper at
    // all on GLES; CPU submission counters remain available.
    const QOpenGLContext* currentContext = QOpenGLContext::currentContext();
    if(currentContext && currentContext->isOpenGLES())
    {
        m_gpuTimer = nullptr;
        ei::log(eLogInfo,
                "GPU timer queries disabled on OpenGL ES/ANGLE");
    }
    else
    {
        m_gpuTimer = new QOpenGLTimerQuery(this);
        if(!m_gpuTimer->create())
        {
            delete m_gpuTimer;
            m_gpuTimer = nullptr;
            ei::log(eLogWarning, "GPU timer queries are unavailable; render counters will report CPU submission only");
        }
    }
    ei::setCrashContext("idle");
    //doneCurrent();
}

void CView::resizeGL(int width, int height)
{
    makeCurrent();
    m_height = height;
    m_width = width;
    //const int side = qMin(width, height);
    //ei::gl::Viewport((width - side) / 2, (height - side) / 2, side, side);
    const float ratio = float(width)/height;
    ei::gl::Viewport(0, 0, width, height);

    m_projection.setToIdentity();
    m_projection.perspective(45, ratio, 1, 1000);
    //ei::gl::Ortho(-2, +2, -2, +2, -2.0, 2.0);   // draw objects in orthogaphic view
    //ei::gl::Frustum(-2.0, 2.0, -2.0, 2.0, 1.5, 20.0); //convert project matrix to perpective view
    //doneCurrent();
}

void CView::drawSkybox(const QMatrix4x4& cameraMatrix,
                       const ei::SRenderEnvironment& environment)
{
    if(!m_skyBuffer.isCreated() || m_skyVertexCount <= 0 || !m_skyProgram.bind())
        return;
    // game.exe constructs one sky object and assigns Sky00 to the outdoor
    // state (Sky01 is the alternate/cave state). It never binds sky00-sky05
    // as simultaneous faces.
    QOpenGLTexture* fallback = CTextureList::getInstance()->textureDefault();
    QOpenGLTexture* skyTexture = CTextureList::getInstance()->texture("sky00");
    const bool complete = skyTexture && skyTexture != fallback;
    if(!m_skyResourcesChecked || complete != m_skyResourcesAvailable)
    {
        m_skyResourcesChecked = true;
        m_skyResourcesAvailable = complete;
        ei::log(complete ? eLogInfo : eLogWarning,
                complete
                    ? "Profile outdoor sky resource resolved: sky00"
                    : "Profile outdoor sky is unavailable; disabling the sky pass");
    }
    if(!complete)
        return;

    QMatrix4x4 skyView = cameraMatrix;
    skyView(0, 3) = 0.0f;
    skyView(1, 3) = 0.0f;
    skyView(2, 3) = 0.0f;
    // The authored cylinder starts at z=0. Keeping that ring on the eye plane
    // exposed a sharp editor-only horizon. Lower it by one authored ring so
    // the cloud field continues below the horizon as it does in-game; the
    // lower two rings are blended into the map fog in skybox.fsh.
    skyView.translate(0.0f, 0.0f, -6.95752f);
    skyView.rotate(environment.skyRotationDegrees, 0.0f, 0.0f, 1.0f);
    m_skyProgram.setUniformValue("u_viewMmatrix", skyView);
    m_skyProgram.setUniformValue("u_projMmatrix", m_projection);
    m_skyProgram.setUniformValue("u_tint", environment.skyColor);
    m_skyProgram.setUniformValue("u_fogColor", environment.fogColor);
    const COptBool* fogOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("renderFog")) : nullptr;
    m_skyProgram.setUniformValue("u_fogEnabled", !fogOption || fogOption->value());

    ei::gl::DepthMask(GL_FALSE);
    ei::gl::DepthFunc(GL_LEQUAL);
    ei::gl::Disable(GL_BLEND);
    const QOpenGLContext* skyContext = QOpenGLContext::currentContext();
    if(skyContext && !skyContext->isOpenGLES())
        ei::gl::Disable(GL_ALPHA_TEST);
    m_skyBuffer.bind();
    const int position = m_skyProgram.attributeLocation("a_position");
    const int texture = m_skyProgram.attributeLocation("a_texture");
    m_skyProgram.enableAttributeArray(position);
    m_skyProgram.setAttributeBuffer(position, GL_FLOAT,
        int(offsetof(SSkyVertex, position)), 3, int(sizeof(SSkyVertex)));
    m_skyProgram.enableAttributeArray(texture);
    m_skyProgram.setAttributeBuffer(texture, GL_FLOAT,
        int(offsetof(SSkyVertex, texture)), 2, int(sizeof(SSkyVertex)));
    skyTexture->bind(0);
    ei::gl::DrawArrays(GL_TRIANGLES, 0, m_skyVertexCount);
    m_skyBuffer.release();
    ei::gl::DepthMask(GL_TRUE);
    ei::gl::DepthFunc(GL_LESS);
    if(skyContext && !skyContext->isOpenGLES())
        ei::gl::Enable(GL_ALPHA_TEST);
    ei::gl::Enable(GL_BLEND);
}

void CView::applyEnvironmentUniforms(QOpenGLShaderProgram& program,
                                     const ei::SRenderEnvironment& environment,
                                     const QVector<ei::SScenePointLight>& lights,
                                     int lightingMode, bool fogEnabled)
{
    const bool enabled = lightingMode != 0;
    program.setUniformValue("u_environmentEnabled", enabled);
    program.setUniformValue("u_lightingMode", lightingMode);
    program.setUniformValue("u_fogEnabled", lightingMode == 2 && fogEnabled);
    program.setUniformValue("u_ambientStrength", environment.ambientStrength);
    program.setUniformValue("u_sunStrength", environment.sunStrength);
    program.setUniformValue("u_sunDirection", environment.sunDirection);
    program.setUniformValue("u_sunColor", environment.sunColor);
    program.setUniformValue("u_ambientColor", environment.ambientColor);
    program.setUniformValue("u_fogColor", environment.fogColor);
    program.setUniformValue("u_fogStart", environment.fogStart);
    program.setUniformValue("u_fogEnd", environment.fogEnd);
    const QVector3D mapFog = m_pLand && m_pLand->isMprLoad()
        ? QVector3D(float(m_pLand->widthInWorldUnits()),
                    float(m_pLand->heightInWorldUnits()), 4.0f)
        : QVector3D();
    program.setUniformValue("u_mapFog", mapFog);

    QVector4D positionRange[ei::kMaxScenePointLights];
    QVector3D colors[ei::kMaxScenePointLights];
    const int count = qMin(ei::kMaxScenePointLights, lights.size());
    for(int index = 0; index < count; ++index)
    {
        const ei::SScenePointLight& light = lights.at(index);
        positionRange[index] = QVector4D(light.position, qMax(0.001f, light.range));
        QVector3D color = light.color;
        const float maximum = qMax(color.x(), qMax(color.y(), color.z()));
        if(maximum > 1.01f)
            color /= 255.0f;
        colors[index] = color;
    }
    program.setUniformValue("u_pointLightCount", lightingMode == 2 ? count : 0);
    if(count > 0)
    {
        program.setUniformValueArray("u_pointLightPositionRange", positionRange, count);
        program.setUniformValueArray("u_pointLightColor", colors, count);
    }
}

void CView::releaseShadowFramebuffers()
{
    delete m_shadowFbo;
    m_shadowFbo = nullptr;
    delete m_dynamicShadowFbo;
    m_dynamicShadowFbo = nullptr;
    delete m_farShadowFbo;
    m_farShadowFbo = nullptr;
    delete m_farDynamicShadowFbo;
    m_farDynamicShadowFbo = nullptr;
    m_shadowResolution = 0;
    m_shadowInitialized = false;
    m_staticShadowFingerprint.clear();
    m_farStaticShadowFingerprint.clear();
}

bool CView::ensureShadowFramebuffers(int quality)
{
    GLint maximumTextureSize = 0;
    ei::gl::GetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    const int resolution = ei::shadowAtlasResolution(
        qBound(0, quality, 2), int(maximumTextureSize));
    if(resolution <= 0)
    {
        if(m_shadowResolution != 0 || m_shadowFbo)
        {
            releaseShadowFramebuffers();
            ei::log(eLogInfo, "Shadow atlases released (quality Off)");
        }
        return false;
    }
    if(m_shadowResolution == resolution && m_shadowFbo && m_dynamicShadowFbo
            && m_farShadowFbo && m_farDynamicShadowFbo
            && m_shadowFbo->isValid() && m_dynamicShadowFbo->isValid()
            && m_farShadowFbo->isValid() && m_farDynamicShadowFbo->isValid())
        return true;

    releaseShadowFramebuffers();
    QOpenGLFramebufferObjectFormat shadowFormat;
    shadowFormat.setAttachment(QOpenGLFramebufferObject::Depth);
    shadowFormat.setInternalTextureFormat(GL_RGBA8);
    m_shadowFbo = new QOpenGLFramebufferObject(resolution, resolution, shadowFormat);
    m_dynamicShadowFbo = new QOpenGLFramebufferObject(resolution, resolution, shadowFormat);
    m_farShadowFbo = new QOpenGLFramebufferObject(resolution, resolution, shadowFormat);
    m_farDynamicShadowFbo = new QOpenGLFramebufferObject(resolution, resolution, shadowFormat);
    if(!m_shadowFbo->isValid() || !m_dynamicShadowFbo->isValid()
            || !m_farShadowFbo->isValid() || !m_farDynamicShadowFbo->isValid())
    {
        ei::log(eLogWarning, "Shadow framebuffer is unavailable; shadow rendering disabled");
        releaseShadowFramebuffers();
        return false;
    }

    m_shadowResolution = resolution;
    // Packed RGBA depth must never be interpolated channel-by-channel. The
    // receiver performs its own 3x3 PCF over nearest depth samples.
    for(QOpenGLFramebufferObject* target : {m_shadowFbo, m_dynamicShadowFbo,
                                            m_farShadowFbo, m_farDynamicShadowFbo})
    {
        ei::gl::BindTexture(GL_TEXTURE_2D, target->texture());
        ei::gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ei::gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ei::gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ei::gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    ei::gl::BindTexture(GL_TEXTURE_2D, 0);
    ei::log(eLogInfo, QString("Shadow atlases initialized: 4 x %1x%1 (quality %2, GL max %3)")
            .arg(resolution).arg(qBound(0, quality, 2)).arg(maximumTextureSize));
    return true;
}

bool CView::renderShadowMap(const QVector3D& cameraPosition,
                            const QVector3D& shadowFocus,
                            const ei::SRenderEnvironment& environment,
                            RenderQueueContext& queue,
                            float maximumDistance)
{
    Q_UNUSED(cameraPosition)
    if(!m_shadowFbo || !m_dynamicShadowFbo || !m_farShadowFbo || !m_farDynamicShadowFbo
            || !m_shadowFbo->isValid() || !m_dynamicShadowFbo->isValid()
            || !m_farShadowFbo->isValid() || !m_farDynamicShadowFbo->isValid())
        return false;

    const float safeMaximumDistance = qBound(1.0f, maximumDistance, 1024.0f);
    // The near cascade is the high-density contact-shadow region. Making its
    // reach follow camera altitude expanded it to 92-128 units in overview
    // views, reducing the closest actor and tree shadows to a handful of
    // texels. Keep its world-space budget fixed; the far cascade owns reach.
    m_nearShadowSplit = qMin(safeMaximumDistance, 48.0f);
    QVector3D up(0.0f, 0.0f, 1.0f);
    if(qAbs(QVector3D::dotProduct(environment.sunDirection.normalized(), up)) > 0.96f)
        up = QVector3D(0.0f, 1.0f, 0.0f);
    const QVector3D lightDirection = environment.sunDirection.normalized();
    const QVector3D lightRight = QVector3D::crossProduct(lightDirection, up).normalized();
    const QVector3D lightUp = QVector3D::crossProduct(lightRight, lightDirection).normalized();
    const QMatrix4x4 cameraView = m_cam->viewMatrix();

    struct SCascade
    {
        QMatrix4x4 staticView;
        QMatrix4x4 dynamicView;
        QMatrix4x4 projection;
        QMatrix4x4 staticMatrix;
        QMatrix4x4 dynamicMatrix;
    };
    const auto buildCascade = [&](float sliceNear, float sliceFar,
                                  int staticResolution,
                                  int dynamicResolution,
                                  float staticWorldSnap) -> SCascade
    {
        float minimumRight = std::numeric_limits<float>::max();
        float maximumRight = std::numeric_limits<float>::lowest();
        float minimumUp = std::numeric_limits<float>::max();
        float maximumUp = std::numeric_limits<float>::lowest();
        bool cornersOk = false;
        const QVector<QVector3D> corners = ei::cameraFrustumSliceCorners(
            m_projection, cameraView, qMax(1.0f, sliceNear), sliceFar,
            &cornersOk);
        QVector3D sliceCenter;
        if(cornersOk)
        {
            for(const QVector3D& point : corners)
            {
                const float right = QVector3D::dotProduct(point, lightRight);
                const float imageUp = QVector3D::dotProduct(point, lightUp);
                minimumRight = qMin(minimumRight, right);
                maximumRight = qMax(maximumRight, right);
                minimumUp = qMin(minimumUp, imageUp);
                maximumUp = qMax(maximumUp, imageUp);
                sliceCenter += point;
            }
            sliceCenter /= float(corners.size());
        }
        if(minimumRight > maximumRight || minimumUp > maximumUp)
        {
            const float right = QVector3D::dotProduct(shadowFocus, lightRight);
            const float imageUp = QVector3D::dotProduct(shadowFocus, lightUp);
            minimumRight = right - sliceFar;
            maximumRight = right + sliceFar;
            minimumUp = imageUp - sliceFar;
            maximumUp = imageUp + sliceFar;
            sliceCenter = shadowFocus;
        }

        const float guardBand = 12.0f;
        // A shadow map covers receivers, not the union of complete caster
        // bounds. Expanding this rectangle by every intersecting caster makes
        // one large house or tree widen the near atlas to most of the map and
        // reduces grounded actors to a few texels. Geometry outside this
        // light-space receiver footprint cannot project a shadow into it; the
        // rasterizer clips crossing triangles at the orthographic boundary.
        // Keep only a small PCF/camera-motion guard around the receiver slice.
        const float centerRight = (minimumRight + maximumRight) * 0.5f;
        const float centerUp = (minimumUp + maximumUp) * 0.5f;
        const float rawExtent = qMax((maximumRight - minimumRight) * 0.5f,
                                     (maximumUp - minimumUp) * 0.5f) + guardBand;
        const float extent = qMax(8.0f, qCeil(rawExtent / 2.0f) * 2.0f);
        const float lightCoordinate = QVector3D::dotProduct(sliceCenter, lightDirection);
        QVector3D baseFocus = lightRight * centerRight + lightUp * centerUp
            + lightDirection * lightCoordinate;
        const auto snappedFocus = [&](float xySnap, float depthSnap)
        {
            QVector3D focus = baseFocus;
            focus += lightRight * (qRound(centerRight / xySnap) * xySnap - centerRight);
            focus += lightUp * (qRound(centerUp / xySnap) * xySnap - centerUp);
            focus += lightDirection
                * (qRound(lightCoordinate / depthSnap) * depthSnap - lightCoordinate);
            return focus;
        };
        const float staticTexel = (2.0f * extent)
            / float(qMax(1, staticResolution));
        // Static maps are expensive but their casters do not move. A small
        // world-space quantization lets the 12-unit guard band absorb camera
        // pans instead of rebuilding both static atlases almost every frame.
        const float staticSnap = qMax(staticTexel, staticWorldSnap);
        const QVector3D staticFocus = snappedFocus(staticSnap, staticSnap);
        const float dynamicTexel = (2.0f * extent) / float(qMax(1, dynamicResolution));
        const float dynamicSnap = qMax(dynamicTexel, 0.0001f);
        const QVector3D dynamicFocus = snappedFocus(dynamicSnap, dynamicSnap);
        const float depthHalf = qMax(256.0f, safeMaximumDistance + 128.0f);
        SCascade result;
        result.staticView.lookAt(staticFocus - lightDirection * depthHalf, staticFocus, up);
        result.dynamicView.lookAt(dynamicFocus - lightDirection * depthHalf, dynamicFocus, up);
        result.projection.ortho(-extent, extent, -extent, extent,
                                1.0f, depthHalf * 2.0f + 1.0f);
        result.staticMatrix = result.projection * result.staticView;
        result.dynamicMatrix = result.projection * result.dynamicView;
        return result;
    };

    const SCascade nearCascade = buildCascade(0.0f, m_nearShadowSplit,
                                               m_shadowFbo->width(),
                                               m_dynamicShadowFbo->width(),
                                               2.0f);
    const SCascade farCascade = buildCascade(m_nearShadowSplit * 0.8f,
                                              safeMaximumDistance,
                                              m_farShadowFbo->width(),
                                              m_farDynamicShadowFbo->width(),
                                              4.0f);

    QCryptographicHash staticHash(QCryptographicHash::Sha256);
    staticHash.addData(queue.staticShadowFingerprint());
    staticHash.addData(sceneSourceFingerprint().toUtf8());
    staticHash.addData(reinterpret_cast<const char*>(nearCascade.staticMatrix.constData()),
                       16 * int(sizeof(float)));
    const QByteArray staticFingerprint = staticHash.result();
    QCryptographicHash farStaticHash(QCryptographicHash::Sha256);
    farStaticHash.addData(queue.staticShadowFingerprint());
    farStaticHash.addData(sceneSourceFingerprint().toUtf8());
    farStaticHash.addData(reinterpret_cast<const char*>(farCascade.staticMatrix.constData()),
                          16 * int(sizeof(float)));
    farStaticHash.addData(reinterpret_cast<const char*>(&safeMaximumDistance),
                          int(sizeof(safeMaximumDistance)));
    const QByteArray farStaticFingerprint = farStaticHash.result();
    const bool rebuildStatic = staticFingerprint != m_staticShadowFingerprint;
    const bool rebuildFarStatic = farStaticFingerprint != m_farStaticShadowFingerprint;
    const bool rebuildDynamic = !m_shadowInitialized || m_shadowUpdateTimer.elapsed() >= 66;
    if(!rebuildStatic && !rebuildFarStatic && !rebuildDynamic)
        return true;

    RenderPerformanceContext& shadowPerformance = RenderPerformanceContext::instance();
    if(shadowPerformance.collecting())
    {
        shadowPerformance.counters().shadowStaticRebuilds
            += quint64(rebuildStatic) + quint64(rebuildFarStatic);
        shadowPerformance.counters().shadowDynamicRebuilds
            += rebuildDynamic ? 2 : 0;
    }

    ei::gl::Disable(GL_BLEND);
    const QOpenGLContext* shadowContext = QOpenGLContext::currentContext();
    if(shadowContext && !shadowContext->isOpenGLES())
        ei::gl::Disable(GL_ALPHA_TEST);

    // Depth is encoded into an RGBA8 color target. OpenGL enables dithering
    // by default, which is desirable for visible color but corrupts packed
    // depth bytes. In particular, a one-code change in the alpha byte changes
    // the decoded depth by 1/255; over this light-space depth range that can
    // erase several world units of a caster at the receiver and produces the
    // camera-dependent contact gaps seen on grounded units and trees.
    const GLboolean restoreDithering = ei::gl::IsEnabled(GL_DITHER);
    ei::gl::Disable(GL_DITHER);
#ifdef GL_FRAMEBUFFER_SRGB
    // Packed values are linear bytes, not colors. Keep an externally enabled
    // sRGB framebuffer state from transforming them during the shadow pass.
    const GLboolean restoreFramebufferSrgb = ei::gl::IsEnabled(GL_FRAMEBUFFER_SRGB);
    ei::gl::Disable(GL_FRAMEBUFFER_SRGB);
#endif
    const auto beginTarget = [&](QOpenGLFramebufferObject* target,
                                 const QMatrix4x4& projection,
                                 const QMatrix4x4& lightView)
    {
        target->bind();
        ei::gl::Viewport(0, 0, target->width(), target->height());
        ei::gl::ClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        ei::gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_shadowProgram.bind();
        m_shadowProgram.setUniformValue("u_projMmatrix", projection);
        m_shadowProgram.setUniformValue("u_viewMmatrix", lightView);
        m_shadowProgram.setUniformValue("u_instanced", false);
    };
    const auto endTarget = [&](QOpenGLFramebufferObject* target)
    {
        target->release();
        // QOpenGLFramebufferObject::release() is not a reliable presentation
        // boundary on ANGLE: the D3D-backed ES context can retain the shadow
        // FBO until Qt starts its own composition.  Every following scene draw
        // would then target the RGBA shadow atlas (and the window stays white).
        // Bind Qt's actual default target explicitly after every shadow pass.
        QOpenGLFramebufferObject::bindDefault();
    };
    if(rebuildStatic)
    {
        m_staticLightSpaceMatrix = nearCascade.staticMatrix;
        beginTarget(m_shadowFbo, nearCascade.projection, nearCascade.staticView);
        queue.flushShadow(&m_shadowProgram,
                          qMin(safeMaximumDistance, m_nearShadowSplit + 24.0f),
                          nearCascade.staticMatrix,
                          false, true);
        endTarget(m_shadowFbo);
        m_staticShadowFingerprint = staticFingerprint;
    }
    if(rebuildFarStatic)
    {
        m_farStaticLightSpaceMatrix = farCascade.staticMatrix;
        beginTarget(m_farShadowFbo, farCascade.projection, farCascade.staticView);
        queue.flushShadow(&m_shadowProgram, safeMaximumDistance,
                          farCascade.staticMatrix, false, true);
        endTarget(m_farShadowFbo);
        m_farStaticShadowFingerprint = farStaticFingerprint;
    }
    if(rebuildDynamic)
    {
        // The sampling matrix and its depth texture are one atomic snapshot.
        // Updating the matrix on every paint while redrawing the animated map
        // at 15 Hz made unit shadows jump and flicker during camera pans.
        m_dynamicLightSpaceMatrix = nearCascade.dynamicMatrix;
        beginTarget(m_dynamicShadowFbo, nearCascade.projection, nearCascade.dynamicView);
        queue.flushShadow(&m_shadowProgram,
                          qMin(safeMaximumDistance, m_nearShadowSplit + 24.0f),
                          nearCascade.dynamicMatrix,
                          true, false);
        endTarget(m_dynamicShadowFbo);
        m_farDynamicLightSpaceMatrix = farCascade.dynamicMatrix;
        beginTarget(m_farDynamicShadowFbo, farCascade.projection, farCascade.dynamicView);
        queue.flushShadow(&m_shadowProgram, safeMaximumDistance,
                          farCascade.dynamicMatrix, true, false);
        endTarget(m_farDynamicShadowFbo);
        m_shadowUpdateTimer.restart();
    }
    m_shadowInitialized = true;
    ei::gl::Viewport(0, 0, m_width, m_height);
#ifdef GL_FRAMEBUFFER_SRGB
    if(restoreFramebufferSrgb)
        ei::gl::Enable(GL_FRAMEBUFFER_SRGB);
#endif
    if(restoreDithering)
        ei::gl::Enable(GL_DITHER);
    if(shadowContext && !shadowContext->isOpenGLES())
        ei::gl::Enable(GL_ALPHA_TEST);
    ei::gl::Enable(GL_BLEND);
    return true;
}

QString CView::captureShadowDiagnostics(QString* error)
{
    if(error)
        error->clear();
    if(!m_shadowInitialized || !m_shadowFbo || !m_dynamicShadowFbo
            || !m_farShadowFbo || !m_farDynamicShadowFbo)
    {
        if(error)
            *error = tr("Shadow maps are not initialized. Select Full environment, enable shadows, and render the failing view first.");
        return QString();
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
    const QString root = QDir::current().absoluteFilePath("logs/shadow-diagnostics");
    const QString directoryPath = QDir(root).absoluteFilePath("shadow-" + stamp);
    if(!QDir().mkpath(directoryPath))
    {
        if(error)
            *error = tr("Could not create shadow diagnostic folder:\n%1").arg(directoryPath);
        return QString();
    }

    makeCurrent();
    const QImage frame = grabFrameBuffer(true);
    m_shadowCascadeDiagnostic = true;
    ei::gl::Viewport(0, 0, m_width, m_height);
    ei::gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw();
    const QImage cascadeSelection = grabFrameBuffer(true);
    m_shadowCascadeDiagnostic = false;
    ei::gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw();
    struct SShadowImage
    {
        const char* name;
        QOpenGLFramebufferObject* fbo;
        const QMatrix4x4* matrix;
    };
    const SShadowImage shadowImages[] = {
        {"near-static", m_shadowFbo, &m_staticLightSpaceMatrix},
        {"near-dynamic", m_dynamicShadowFbo, &m_dynamicLightSpaceMatrix},
        {"far-static", m_farShadowFbo, &m_farStaticLightSpaceMatrix},
        {"far-dynamic", m_farDynamicShadowFbo, &m_farDynamicLightSpaceMatrix}
    };

    QStringList failures;
    if(frame.isNull() || !frame.save(QDir(directoryPath).absoluteFilePath("frame.png")))
        failures.append("frame.png");
    if(cascadeSelection.isNull() || !cascadeSelection.save(
            QDir(directoryPath).absoluteFilePath("cascade-selection.png")))
        failures.append("cascade-selection.png");

    QJsonObject atlasMetadata;
    for(const SShadowImage& shadow : shadowImages)
    {
        const QString name = QString::fromLatin1(shadow.name);
        const QImage packed = shadow.fbo->toImage();
        if(packed.isNull()
                || !packed.save(QDir(directoryPath).absoluteFilePath(name + "-packed.png")))
            failures.append(name + "-packed.png");
        const QImage depth = visualizedPackedDepth(packed);
        if(depth.isNull()
                || !depth.save(QDir(directoryPath).absoluteFilePath(name + "-depth.png")))
            failures.append(name + "-depth.png");
        QJsonObject atlas;
        atlas["width"] = shadow.fbo->width();
        atlas["height"] = shadow.fbo->height();
        atlas["matrixColumnMajor"] = jsonMatrix(*shadow.matrix);
        atlasMetadata[name] = atlas;
    }

    QVector3D cameraPosition;
    QVector3D cameraPivot;
    QVector3D cameraRotation;
    m_cam->getLocation(cameraPosition, cameraPivot, cameraRotation);
    const QMatrix4x4 viewMatrix = m_cam->viewMatrix();
    bool renderedEyeOk = false;
    const QVector3D renderedEye = ei::cameraEyeFromView(viewMatrix, &renderedEyeOk);

    const auto boolOption = [this](const char* name, bool fallback)
    {
        const COptBool* option = m_pSettings
            ? dynamic_cast<const COptBool*>(m_pSettings->opt(name)) : nullptr;
        return option ? option->value() : fallback;
    };
    const auto intOption = [this](const char* name, int fallback)
    {
        const COptInt* option = m_pSettings
            ? dynamic_cast<const COptInt*>(m_pSettings->opt(name)) : nullptr;
        return option ? option->value() : fallback;
    };
    const bool overrideWorldTime = boolOption("overrideWorldTime", false);
    const float previewTime = ei::clockValueToHours(intOption("renderTimeOfDay", 1200));
    const CWorldSet* worldSet = m_environmentMob ? &m_environmentMob->worldSet() : nullptr;
    const ei::SRenderEnvironment environment = ei::renderEnvironment(
        worldSet, overrideWorldTime, previewTime, m_caveEnvironment,
        m_weatherRegion);

    QJsonObject rootObject;
    rootObject["format"] = "EIMapperEndurance.shadow-diagnostics.v3";
    rootObject["capturedAt"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    rootObject["applicationVersion"] = QCoreApplication::applicationVersion();
    rootObject["sceneFingerprint"] = sceneSourceFingerprint();
    rootObject["viewport"] = QJsonObject{{"width", m_width}, {"height", m_height},
        {"devicePixelRatio", devicePixelRatioF()}};
    rootObject["camera"] = QJsonObject{{"storedOrbitAnchor", jsonVector(cameraPosition)},
        {"renderedEye", jsonVector(renderedEye)}, {"renderedEyeValid", renderedEyeOk},
        {"pivot", jsonVector(cameraPivot)}, {"rotation", jsonVector(cameraRotation)},
        {"viewMatrixColumnMajor", jsonMatrix(viewMatrix)},
        {"projectionMatrixColumnMajor", jsonMatrix(m_projection)}};
    rootObject["shadow"] = QJsonObject{{"initialized", m_shadowInitialized},
        {"nearSplit", m_nearShadowSplit},
        {"configuredMaximumDistance", intOption("renderShadowDistance", 320)},
        {"quality", intOption("renderShadowQuality", 2)},
        {"enabled", intOption("renderShadowQuality", 2) > 0},
        {"atlasResolution", m_shadowResolution},
        {"packedDepthDitheringDisabled", true},
        {"packedDepthFramebufferSrgbDisabled", true},
        {"staticFingerprint", QString::fromLatin1(m_staticShadowFingerprint.toHex())},
        {"farStaticFingerprint", QString::fromLatin1(m_farStaticShadowFingerprint.toHex())},
        {"atlases", atlasMetadata}};
    rootObject["environment"] = QJsonObject{{"lightingMode", intOption("renderLightingMode", 2)},
        {"cave", m_caveEnvironment}, {"timeHours", environment.timeHours},
        {"overrideWorldTime", overrideWorldTime},
        {"sunDirection", jsonVector(environment.sunDirection)},
        {"sunColor", jsonVector(environment.sunColor)},
        {"sunStrength", environment.sunStrength},
        {"ambientColor", jsonVector(environment.ambientColor)},
        {"ambientStrength", environment.ambientStrength},
        {"fogColor", jsonVector(environment.fogColor)},
        {"fogStart", environment.fogStart}, {"fogEnd", environment.fogEnd}};
    rootObject["renderSettings"] = QJsonObject{
        {"frustumCulling", boolOption("frustumCulling", true)},
        {"animateUnits", boolOption("animateUnits", true)},
        {"drawSkybox", intOption("renderLightingMode", 2) == 2},
        {"renderFog", boolOption("renderFog", true)},
        {"renderTextureMipmapping", boolOption("renderTextureMipmapping", true)}};

    const GLubyte* vendor = ei::gl::GetString(GL_VENDOR);
    const GLubyte* renderer = ei::gl::GetString(GL_RENDERER);
    const GLubyte* version = ei::gl::GetString(GL_VERSION);
    rootObject["openGl"] = QJsonObject{
        {"vendor", vendor ? QString::fromLatin1(reinterpret_cast<const char*>(vendor)) : QString()},
        {"renderer", renderer ? QString::fromLatin1(reinterpret_cast<const char*>(renderer)) : QString()},
        {"version", version ? QString::fromLatin1(reinterpret_cast<const char*>(version)) : QString()}};

    QSet<CNode*> selected;
    for(CNode* node : selectedNodes())
        selected.insert(node);
    QJsonArray mobArray;
    QJsonArray nodeArray;
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        QJsonObject mobObject;
        mobObject["path"] = mob->filePath().absoluteFilePath();
        mobObject["active"] = mob == m_activeMob;
        mobObject["environmentOwner"] = mob == m_environmentMob;
        mobObject["visible"] = isMobVisible(mob);
        mobObject["nodeCount"] = mob->nodes().size();
        mobArray.append(mobObject);
        for(CNode* node : mob->nodes())
        {
            if(!node)
                continue;
            CBox bounds = node->getBBox();
            // Node bounding boxes are local to the node. Diagnostic v1
            // accidentally projected them as world coordinates, making its
            // distance and atlas-membership fields misleading even though the
            // render queue itself used transformed per-part bounds.
            const QVector3D center = bounds.isInit()
                ? bounds.center() + node->drawPosition() : node->drawPosition();
            QJsonObject object;
            object["mobPath"] = mob->filePath().absoluteFilePath();
            object["mapId"] = int(node->mapId());
            object["name"] = node->mapName();
            object["type"] = int(node->nodeType());
            object["model"] = node->modelName();
            object["texture"] = node->textureName();
            object["state"] = int(node->nodeState());
            object["selected"] = selected.contains(node);
            object["castsRenderShadow"] = node->castsRenderShadow();
            object["position"] = jsonVector(node->position());
            object["drawPosition"] = jsonVector(node->drawPosition());
            object["boundsInitialized"] = bounds.isInit();
            object["boundsCenter"] = jsonVector(center);
            object["boundsRadius"] = bounds.isInit() ? bounds.radius() : 0.0f;
            object["cameraDistance"] = (center - cameraPosition).length();
            object["viewDepth"] = -(viewMatrix * QVector4D(center, 1.0f)).z();
            QJsonObject projections;
            projections["nearStatic"] = projectedPoint(m_staticLightSpaceMatrix, center);
            projections["nearDynamic"] = projectedPoint(m_dynamicLightSpaceMatrix, center);
            projections["farStatic"] = projectedPoint(m_farStaticLightSpaceMatrix, center);
            projections["farDynamic"] = projectedPoint(m_farDynamicLightSpaceMatrix, center);
            object["centerProjection"] = projections;
            nodeArray.append(object);
        }
    }
    rootObject["mobs"] = mobArray;
    rootObject["nodes"] = nodeArray;

    QSaveFile metadataFile(QDir(directoryPath).absoluteFilePath("shadow-diagnostics.json"));
    if(!metadataFile.open(QIODevice::WriteOnly)
            || metadataFile.write(QJsonDocument(rootObject).toJson(QJsonDocument::Indented)) < 0
            || !metadataFile.commit())
        failures.append("shadow-diagnostics.json");

    QFile instructions(QDir(directoryPath).absoluteFilePath("README.txt"));
    if(instructions.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        instructions.write("EI Mapper Endurance shadow diagnostic capture\n"
            "Send this entire folder without renaming or editing its contents.\n"
            "frame.png is the failing viewport. cascade-selection.png is the same camera view:\n"
            "red = near cascade, blue = far cascade, purple = their blend, black = no coverage.\n"
            "*-packed.png files are exact GPU packed-depth bytes; their colors are encoding data, not shadow color.\n"
            "*-depth.png files decode those bytes to grayscale depth for visual inspection.\n"
            "shadow-diagnostics.json contains camera, cascade, environment, and per-node projection data.\n");
    }
    else
        failures.append("README.txt");

    doneCurrent();
    if(!failures.isEmpty())
    {
        if(error)
            *error = tr("Capture folder was created, but these files failed: %1")
                .arg(failures.join(", "));
        return QString();
    }
    return directoryPath;
}

void CView::applyShadowUniforms(QOpenGLShaderProgram& program, bool enabled,
                                bool receiveStatic, bool receiveDynamic)
{
    const bool available = enabled && m_shadowFbo && m_dynamicShadowFbo
        && m_farShadowFbo && m_farDynamicShadowFbo
        && m_shadowFbo->isValid() && m_dynamicShadowFbo->isValid()
        && m_farShadowFbo->isValid() && m_farDynamicShadowFbo->isValid()
        && m_shadowInitialized;
    program.setUniformValue("u_shadowsEnabled", available);
    program.setUniformValue("u_staticLightSpaceMatrix", m_staticLightSpaceMatrix);
    program.setUniformValue("u_dynamicLightSpaceMatrix", m_dynamicLightSpaceMatrix);
    program.setUniformValue("u_farStaticLightSpaceMatrix", m_farStaticLightSpaceMatrix);
    program.setUniformValue("u_farDynamicLightSpaceMatrix", m_farDynamicLightSpaceMatrix);
    program.setUniformValue("u_nearShadowSplit", m_nearShadowSplit);
    program.setUniformValue("u_shadowCascadeDebug", m_shadowCascadeDiagnostic);
    program.setUniformValue("u_receiveStaticShadow", receiveStatic);
    program.setUniformValue("u_receiveDynamicShadow", receiveDynamic);
    program.setUniformValue("u_staticShadowMap", 1);
    program.setUniformValue("u_dynamicShadowMap", 2);
    program.setUniformValue("u_farStaticShadowMap", 3);
    program.setUniformValue("u_farDynamicShadowMap", 4);
    const float staticTexel = m_shadowFbo
        ? 1.0f / float(m_shadowFbo->width()) : 0.0f;
    const float dynamicTexel = m_dynamicShadowFbo
        ? 1.0f / float(m_dynamicShadowFbo->width()) : 0.0f;
    const float farStaticTexel = m_farShadowFbo
        ? 1.0f / float(m_farShadowFbo->width()) : 0.0f;
    const float farDynamicTexel = m_farDynamicShadowFbo
        ? 1.0f / float(m_farDynamicShadowFbo->width()) : 0.0f;
    program.setUniformValue("u_staticShadowTexelSize",
                            QVector2D(staticTexel, staticTexel));
    program.setUniformValue("u_dynamicShadowTexelSize",
                            QVector2D(dynamicTexel, dynamicTexel));
    program.setUniformValue("u_farStaticShadowTexelSize",
                            QVector2D(farStaticTexel, farStaticTexel));
    program.setUniformValue("u_farDynamicShadowTexelSize",
                            QVector2D(farDynamicTexel, farDynamicTexel));
    if(available)
    {
        QOpenGLFunctions* functions = QOpenGLContext::currentContext()->functions();
        functions->glActiveTexture(GL_TEXTURE1);
        ei::gl::BindTexture(GL_TEXTURE_2D, m_shadowFbo->texture());
        functions->glActiveTexture(GL_TEXTURE2);
        ei::gl::BindTexture(GL_TEXTURE_2D, m_dynamicShadowFbo->texture());
        functions->glActiveTexture(GL_TEXTURE3);
        ei::gl::BindTexture(GL_TEXTURE_2D, m_farShadowFbo->texture());
        functions->glActiveTexture(GL_TEXTURE4);
        ei::gl::BindTexture(GL_TEXTURE_2D, m_farDynamicShadowFbo->texture());
        functions->glActiveTexture(GL_TEXTURE0);
    }
}

void CView::paintGL()
{
    makeCurrent();
    // Treat framebuffer ownership as a frame-entry invariant. Shadow-map and
    // diagnostic passes are allowed to bind private FBOs, but the visible
    // scene must always begin on the widget/context default target.
    const bool defaultFramebufferBound = QOpenGLFramebufferObject::bindDefault();
    const bool diagnoseFrame = m_openGlDiagnosticFramesRemaining > 0;
    if(diagnoseFrame)
    {
        ++m_openGlDiagnosticFrame;
        ei::log(eLogInfo, QStringLiteral(
            "OpenGL diagnostic frame begin id:%1 reason:%2 landscape:%3 mobs:%4")
            .arg(m_openGlDiagnosticFrame)
            .arg(m_openGlDiagnosticReason)
            .arg(m_pLand && m_pLand->isMprLoad()
                ? m_pLand->filePath().fileName() : QStringLiteral("none"))
            .arg(m_aMob.size()));
        ei::log(defaultFramebufferBound ? eLogInfo : eLogWarning,
            QStringLiteral("OpenGL diagnostic presentation target rebound:%1")
                .arg(defaultFramebufferBound ? 1 : 0));
        logOpenGlDiagnosticState(QStringLiteral("frame/begin"));
    }
    ei::gl::Viewport(0, 0, width(), height());
    ei::gl::Enable(GL_DEPTH_TEST);
    ei::gl::DepthMask(GL_TRUE);
    QOpenGLContext* paintContext = QOpenGLContext::currentContext();
    if(paintContext && !paintContext->isOpenGLES())
        ei::gl::Enable(GL_ALPHA_TEST);
    ei::gl::Enable(GL_BLEND);
    ei::gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ei::gl::ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const COptBool* statsOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("renderPerformanceCounters")) : nullptr;
    const bool collectStats = (statsOption && statsOption->value())
        || qApp->property("renderBenchmark").toBool();
    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    performance.beginFrame(collectStats);
    if(collectStats && m_gpuTimer && m_gpuTimerPending && m_gpuTimer->isResultAvailable())
    {
        performance.recordGpuTime(m_gpuTimer->waitForResult());
        m_gpuTimerPending = false;
    }
    const bool timeGpu = collectStats && m_gpuTimer && !m_gpuTimerPending;
    if(timeGpu)
        m_gpuTimer->begin();
    ei::gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if(diagnoseFrame)
        logOpenGlDiagnosticState(QStringLiteral("frame/initial-clear"), true);
    draw();
    if(diagnoseFrame)
        logOpenGlDiagnosticState(QStringLiteral("frame/scene-complete"), true);
    if(timeGpu)
    {
        m_gpuTimer->end();
        m_gpuTimerPending = true;
    }
    performance.endFrame();
    drawTestOverlaysOpenGl();
    if(diagnoseFrame)
    {
        logOpenGlDiagnosticState(QStringLiteral("frame/overlays-complete"), true);
        ei::log(eLogInfo, QStringLiteral("OpenGL diagnostic frame end id:%1")
            .arg(m_openGlDiagnosticFrame));
        --m_openGlDiagnosticFramesRemaining;
    }
    //doneCurrent();
}

bool CView::prepareForApplicationShutdown()
{
    if(m_timer)
        m_timer->stop();
    if(m_mprModifyTimer)
        m_mprModifyTimer->stop();
    if(!isValid())
        return false;
    makeCurrent();
    QOpenGLContext* current = QOpenGLContext::currentContext();
    return current && current->surface();
}

void CView::rebuildAiGraphOverlayGeometry()
{
    m_aiGraphOpenLinks.clear();
    m_aiGraphNodes.clear();
    m_aiGraphIsolatedNodes.clear();
    m_aiGraphOverlayDirty = false;
    if(!m_aiGraphDocument.isValid() || !m_pLand || !m_pLand->isMprLoad())
        return;

    const int layer = m_aiGraphLayerSlider ? m_aiGraphLayerSlider->value() : 3;
    const int width = m_aiGraphDocument.width();
    const int height = m_aiGraphDocument.height();
    QVector<QVector3D> positions(width * height);
    QVector<bool> valid(width * height, false);
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            const int localX = m_aiGraphDocument.localX(layer, x, y);
            const int localY = m_aiGraphDocument.localY(layer, x, y);
            if(localX > 7 || localY > 7)
                continue;
            QVector3D position((x * 8 + localX + 0.5f) * 0.5f,
                               (y * 8 + localY + 0.5f) * 0.5f, 0.0f);
            if(!m_pLand->projectPt(position))
                continue;
            position.setZ(position.z() + 0.20f);
            const int cell = y * width + x;
            positions[cell] = position;
            valid[cell] = true;
            m_aiGraphNodes.append(position);
        }

    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
        {
            const int cell = y * width + x;
            if(!valid[cell])
                continue;
            bool connected = false;
            for(int direction = 0; direction < CAiGraphDocument::DirectionCount; ++direction)
            {
                if(m_aiGraphDocument.weight(layer, x, y, direction) < 0)
                    continue;
                connected = true;
                const QPoint delta = CAiGraphDocument::directionDelta(direction);
                const int nx = x + delta.x();
                const int ny = y + delta.y();
                if(nx < 0 || nx >= width || ny < 0 || ny >= height)
                    continue;
                const int neighbor = ny * width + nx;
                if(!valid[neighbor])
                    continue;
                if(neighbor < cell)
                {
                    const int reverseDirection = (direction + 4) % 8;
                    if(m_aiGraphDocument.weight(layer, nx, ny, reverseDirection) >= 0)
                        continue;
                }
                m_aiGraphOpenLinks.append(positions[cell]);
                m_aiGraphOpenLinks.append(positions[neighbor]);
            }
            if(!connected)
                m_aiGraphIsolatedNodes.append(positions[cell]);
        }
}

void CView::drawTestOverlaysOpenGl()
{
    const bool paintIssueOverlay = m_landscapeTools
        && m_landscapeTools->paintIssueMarkingEnabled()
        && (m_paintIssueHoverValid || !m_paintIssueSelectedCells.isEmpty());
    if((m_testEntranceRectangles.isEmpty() && !m_testSpawnMarkerValid
        && !m_terrainPaintOutlineValid && !m_sculptOutlineValid
        && !paintIssueOverlay
        && !(m_aiGraphPreviewVisible && m_aiGraphDocument.isValid()))
       || width() <= 0 || height() <= 0)
        return;

    // QPainter and QGLWidget do not compose reliably on every double-buffered
    // driver. In particular, painting the entrance labels on the QGLWidget's
    // back buffer caused alternating dark frames on current NVIDIA drivers.
    // Keep the entire per-frame overlay in the native OpenGL pass.
    if(!m_program.bind())
        return;

    // Mesh rendering may leave a VAO with normal/UV/instance attributes
    // enabled. The overlay below uses client-side arrays. If those stale
    // attributes remain enabled after their VBO is released, NVIDIA's driver
    // attempts to fetch offsets such as 0x0c as host pointers and can access-
    // violate in glDrawArrays. Always start the overlay on the default VAO and
    // with a clean attribute set.
    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* functions = context ? context->extraFunctions() : nullptr;
    if(functions)
    {
        functions->glBindVertexArray(0);
        functions->glBindBuffer(GL_ARRAY_BUFFER, 0);
        functions->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        GLint attributeCount = 0;
        functions->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attributeCount);
        for(GLint attribute = 0; attribute < attributeCount; ++attribute)
            functions->glDisableVertexAttribArray(GLuint(attribute));
    }
    QOpenGLBuffer::release(QOpenGLBuffer::VertexBuffer);
    QOpenGLBuffer::release(QOpenGLBuffer::IndexBuffer);
    const int position = m_program.attributeLocation("a_position");
    if(position < 0)
        return;

    m_program.enableAttributeArray(position);
    QMatrix4x4 identity;
    identity.setToIdentity();
    m_program.setUniformValue("u_modelMmatrix", identity);
    m_program.setUniformValue("u_viewMmatrix", m_cam->viewMatrix());
    m_program.setUniformValue("u_projMmatrix", m_projection);
    m_program.setUniformValue("u_highlight", false);
    // Editor overlays are authored colors/textures. They must remain readable
    // regardless of the map clock, fog, scene lights, or shadow maps.
    m_program.setUniformValue("u_environmentEnabled", false);
    m_program.setUniformValue("u_fogEnabled", false);
    m_program.setUniformValue("u_shadowsEnabled", false);
    m_program.setUniformValue("u_pointLightCount", 0);

    ei::gl::Enable(GL_BLEND);
    ei::gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ei::gl::Enable(GL_DEPTH_TEST);
    ei::gl::DepthMask(GL_FALSE);

    if(m_aiGraphPreviewVisible && m_aiGraphDocument.isValid())
    {
        if(m_aiGraphOverlayDirty)
            rebuildAiGraphOverlayGeometry();
        if(!m_aiGraphOpenLinks.isEmpty())
        {
            const int layer = m_aiGraphLayerSlider ? m_aiGraphLayerSlider->value() : 3;
            m_program.setUniformValue("customColor", aiGraphLayerColor(layer));
            ei::gl::LineWidth(1.5f);
            m_program.setAttributeArray(position, m_aiGraphOpenLinks.constData());
            ei::gl::DrawArrays(GL_LINES, 0, m_aiGraphOpenLinks.size());
        }
        if(!m_aiGraphNodes.isEmpty())
        {
            m_program.setUniformValue("customColor", QVector4D(0.95f, 0.82f, 0.20f, 0.88f));
            glPointSize(3.0f);
            m_program.setAttributeArray(position, m_aiGraphNodes.constData());
            ei::gl::DrawArrays(GL_POINTS, 0, m_aiGraphNodes.size());
        }
        if(!m_aiGraphIsolatedNodes.isEmpty())
        {
            m_program.setUniformValue("customColor", QVector4D(1.0f, 0.16f, 0.12f, 1.0f));
            glPointSize(6.0f);
            m_program.setAttributeArray(position, m_aiGraphIsolatedNodes.constData());
            ei::gl::DrawArrays(GL_POINTS, 0, m_aiGraphIsolatedNodes.size());
        }
        glPointSize(1.0f);
    }

    for(int index = 0; index < m_testEntranceRectangles.size(); ++index)
    {
        const QRectF area = m_testEntranceRectangles.at(index).normalized();
        const QPointF corners[] = {
            area.topLeft(), area.topRight(), area.bottomRight(), area.bottomLeft()
        };
        QVector<QVector3D> vertices;
        vertices.reserve(4);
        for(const QPointF& corner : corners)
        {
            QVector3D world(corner.x(), corner.y(), 0.0f);
            if(!CLandscape::getInstance()->projectPt(world))
            {
                vertices.clear();
                break;
            }
            world.setZ(world.z() + 0.15f);
            vertices.append(world);
        }
        if(vertices.size() != 4)
            continue;

        const bool selected = index == m_selectedTestEntrance;
        m_program.setUniformValue("customColor",
            selected ? QVector4D(0.20f, 1.0f, 0.38f, 0.37f)
                     : QVector4D(0.92f, 0.70f, 0.12f, 0.20f));
        m_program.setAttributeArray(position, vertices.constData());
        ei::gl::DrawArrays(GL_TRIANGLE_FAN, 0, vertices.size());

        m_program.setUniformValue("customColor",
            selected ? QVector4D(0.38f, 1.0f, 0.50f, 0.95f)
                     : QVector4D(0.92f, 0.75f, 0.18f, 0.80f));
        ei::gl::LineWidth(selected ? 3.0f : 1.0f);
        ei::gl::DrawArrays(GL_LINE_LOOP, 0, vertices.size());
    }

    drawTerrainPaintOutline(&m_program);

    if(m_testEntranceLabelTexturesDirty)
    {
        if(!m_testEntranceLabelTextures.isEmpty())
            ei::gl::DeleteTextures(m_testEntranceLabelTextures.size(),
                             m_testEntranceLabelTextures.constData());
        m_testEntranceLabelTextures.clear();
        for(const QImage& image : m_testEntranceLabelImages)
            m_testEntranceLabelTextures.append(bindTexture(image, GL_TEXTURE_2D, GL_RGBA));
        m_testEntranceLabelTexturesDirty = false;
    }

    // Labels are painted once into off-screen images and then composited as
    // ordinary OpenGL textures. QPainter never touches QGLWidget's framebuffer.
    const int texture = m_program.attributeLocation("a_texture");
    if(texture >= 0 && !m_testEntranceLabelTextures.isEmpty())
    {
        m_program.enableAttributeArray(texture);
        m_program.setUniformValue("u_viewMmatrix", identity);
        m_program.setUniformValue("u_projMmatrix", identity);
        m_program.setUniformValue("customColor", QVector4D(0.0f, 0.0f, 0.0f, 0.0f));
        m_program.setUniformValue("qt_Texture0", 0);
        ei::gl::Disable(GL_DEPTH_TEST);
        const QRect viewport(0, 0, width(), height());
        const QVector<QVector2D> textureCoordinates = {
            QVector2D(0.0f, 0.0f), QVector2D(1.0f, 0.0f),
            QVector2D(1.0f, 1.0f), QVector2D(0.0f, 1.0f)
        };

        const int count = qMin(m_testEntranceRectangles.size(),
                               m_testEntranceLabelTextures.size());
        for(int index = 0; index < count; ++index)
        {
            const QRectF area = m_testEntranceRectangles.at(index).normalized();
            QVector3D center(area.center().x(), area.center().y(), 0.0f);
            if(!CLandscape::getInstance()->projectPt(center))
                continue;
            center.setZ(center.z() + 0.25f);
            const QVector3D projected = center.project(
                m_cam->viewMatrix(), m_projection, viewport);
            if(projected.z() < 0.0f || projected.z() > 1.0f)
                continue;

            const QImage& image = m_testEntranceLabelImages.at(index);
            const float centerX = (2.0f * projected.x() / float(width())) - 1.0f;
            const float centerY = (2.0f * projected.y() / float(height())) - 1.0f;
            const float halfWidth = float(image.width()) / float(width());
            const float halfHeight = float(image.height()) / float(height());
            const QVector<QVector3D> quad = {
                QVector3D(centerX - halfWidth, centerY - halfHeight, 0.0f),
                QVector3D(centerX + halfWidth, centerY - halfHeight, 0.0f),
                QVector3D(centerX + halfWidth, centerY + halfHeight, 0.0f),
                QVector3D(centerX - halfWidth, centerY + halfHeight, 0.0f)
            };
            ei::gl::BindTexture(GL_TEXTURE_2D, m_testEntranceLabelTextures.at(index));
            m_program.setAttributeArray(position, quad.constData());
            m_program.setAttributeArray(texture, textureCoordinates.constData());
            ei::gl::DrawArrays(GL_TRIANGLE_FAN, 0, quad.size());
        }
        ei::gl::BindTexture(GL_TEXTURE_2D, 0);
        m_program.disableAttributeArray(texture);
    }

    if(m_testSpawnMarkerValid)
    {
        m_program.setUniformValue("u_viewMmatrix", m_cam->viewMatrix());
        m_program.setUniformValue("u_projMmatrix", m_projection);
        m_program.setUniformValue("customColor",
            m_testSpawnMarkerSelected ? QVector4D(0.92f, 1.0f, 0.32f, 1.0f)
            : m_testSpawnMarkerAdjusted ? QVector4D(1.0f, 0.67f, 0.18f, 0.82f)
                                        : QVector4D(0.31f, 1.0f, 0.47f, 0.72f));
        ei::gl::Enable(GL_DEPTH_TEST);
        ei::gl::LineWidth(3.0f);
        QVector<QVector3D> figure;
        const float z = m_testSpawnMarker.z() + 0.08f;
        const int segments = 24;
        for(int i = 0; i < segments; ++i)
        {
            const float angle = float(i) * 2.0f * float(M_PI) / float(segments);
            figure.append(QVector3D(m_testSpawnMarker.x() + qCos(angle) * 0.55f,
                                    m_testSpawnMarker.y() + qSin(angle) * 0.55f, z));
        }
        m_program.setAttributeArray(position, figure.constData());
        ei::gl::DrawArrays(GL_LINE_LOOP, 0, figure.size());
        const QVector<QVector3D> body = {
            QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(), z + 1.72f),
            QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(), z + 0.72f),
            QVector3D(m_testSpawnMarker.x() - 0.42f, m_testSpawnMarker.y(), z + 1.25f),
            QVector3D(m_testSpawnMarker.x() + 0.42f, m_testSpawnMarker.y(), z + 1.25f),
            QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(), z + 0.72f),
            QVector3D(m_testSpawnMarker.x() - 0.34f, m_testSpawnMarker.y(), z),
            QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(), z + 0.72f),
            QVector3D(m_testSpawnMarker.x() + 0.34f, m_testSpawnMarker.y(), z)
        };
        m_program.setAttributeArray(position, body.constData());
        ei::gl::DrawArrays(GL_LINES, 0, body.size());
        QVector<QVector3D> head;
        for(int i = 0; i < segments; ++i)
        {
            const float angle = float(i) * 2.0f * float(M_PI) / float(segments);
            head.append(QVector3D(m_testSpawnMarker.x() + qCos(angle) * 0.24f,
                                  m_testSpawnMarker.y(), z + 1.93f + qSin(angle) * 0.24f));
        }
        m_program.setAttributeArray(position, head.constData());
        ei::gl::DrawArrays(GL_LINE_LOOP, 0, head.size());
    }

    m_program.disableAttributeArray(position);
    m_program.setUniformValue("customColor", QVector4D(0.0f, 0.0f, 0.0f, 0.0f));
    ei::gl::LineWidth(1.0f);
    ei::gl::DepthMask(GL_TRUE);
    ei::gl::Enable(GL_DEPTH_TEST);
}

bool CView::ensureParticleRendererInitialized()
{
    if(m_particleRendererReady)
        return true;
    if(m_particleInitializationAttempted)
        return false;
    m_particleInitializationAttempted = true;

    // Particle rendering is optional. Preserve the scene renderer's bindings
    // while Qt compiles the auxiliary shader and initializes its quad buffer;
    // a failure must disable only particles, never the viewport.
    GLint previousProgram = 0;
    GLint previousArrayBuffer = 0;
    GLint previousVertexArray = 0;
    ei::gl::GetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    ei::gl::GetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    QOpenGLContext* context = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* functions = context ? context->extraFunctions() : nullptr;
    if(functions)
    {
        ei::gl::GetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
        functions->glBindVertexArray(0);
    }

    const auto restoreBindings = [&]()
    {
        if(functions)
            functions->glBindVertexArray(GLuint(previousVertexArray));
        if(functions)
        {
            functions->glBindBuffer(GL_ARRAY_BUFFER, GLuint(previousArrayBuffer));
            functions->glUseProgram(GLuint(previousProgram));
        }
    };
    const auto fail = [&](const QString& stage)
    {
        ei::log(eLogWarning, QStringLiteral("Particle renderer disabled (%1): %2")
            .arg(stage, m_particleProgram.log()));
        if(m_particleBuffer.isCreated())
            m_particleBuffer.destroy();
        restoreBindings();
        return false;
    };

    if(!m_particleProgram.addShaderFromSourceFile(
            QOpenGLShader::Vertex, ":/particle.vsh"))
        return fail(QStringLiteral("vertex shader"));
    if(!m_particleProgram.addShaderFromSourceFile(
            QOpenGLShader::Fragment, ":/particle.fsh"))
        return fail(QStringLiteral("fragment shader"));
    if(!m_particleProgram.link())
        return fail(QStringLiteral("program link"));

    if(!m_particleBuffer.create())
        return fail(QStringLiteral("vertex buffer creation"));
    m_particleBuffer.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    restoreBindings();

    m_particleRendererReady = true;
    ei::log(eLogInfo, "Particle renderer initialized lazily");
    return true;
}

void CView::drawParticles(const QMatrix4x4& cameraMatrix,
                          const CViewFrustum* frustum)
{
    // Keep the particle renderer completely outside terrain-only frames.
    // In particular, do not lazily resolve particle atlases or touch the
    // particle shader/VBO/VAO when an MPR is open without a MOB. Besides
    // avoiding needless work, this is an important render-state boundary:
    // the terrain renderer must not depend on a no-op particle pass restoring
    // state correctly on the following frame.
    bool hasVisibleEffect = false;
    for(CMob* mob : m_aMob)
    {
        if(!isMobVisible(mob))
            continue;
        const QVector<CNode*> candidates = spatiallyVisibleNodes(mob, frustum, nullptr);
        for(CNode* node : candidates)
        {
            if(!node || node->isMarkDeleted()
                    || node->nodeState() == ENodeState::eHidden)
                continue;
            if(dynamic_cast<CParticle*>(node) || dynamic_cast<CTorch*>(node))
            {
                hasVisibleEffect = true;
                break;
            }
        }
        if(hasVisibleEffect)
            break;
    }
    if(!hasVisibleEffect)
        return;
    if(!ensureParticleRendererInitialized())
        return;

    // Resolve every resource before changing shader, buffer, depth or blend
    // state. Texture creation can fail during a profile/map transition and an
    // early return must never leave the rest of the frame using particle GL
    // state.
    CTextureList* textures = CTextureList::getInstance();
    QOpenGLTexture* fallbackTexture = textures->textureDefault();
    QOpenGLTexture* fireSmokeTexture = textures->texture("firesmoke");
    if(fireSmokeTexture == fallbackTexture)
        fireSmokeTexture = nullptr;
    QOpenGLTexture* tendrilSmokeTexture = textures->texture("tendrismoke");
    if(tendrilSmokeTexture == fallbackTexture)
        tendrilSmokeTexture = fireSmokeTexture;
    QOpenGLTexture* tornadoTexture = textures->texture("tornado");
    if(tornadoTexture == fallbackTexture)
        tornadoTexture = nullptr;
    QOpenGLTexture* tendriLightTexture = textures->texture("tendrilight");
    if(tendriLightTexture == fallbackTexture)
        tendriLightTexture = nullptr;
    QOpenGLTexture* acidRayTexture = textures->texture("acidray");
    if(acidRayTexture == fallbackTexture)
        acidRayTexture = nullptr;
    QOpenGLTexture* bansheeTexture = textures->texture("banshee");
    if(bansheeTexture == fallbackTexture)
        bansheeTexture = nullptr;
    QOpenGLTexture* changeHeroTexture = textures->texture("changehero");
    if(changeHeroTexture == fallbackTexture)
        changeHeroTexture = nullptr;
    QOpenGLTexture* rainSnowTexture = textures->texture("rainsnow");
    if(rainSnowTexture == fallbackTexture)
        rainSnowTexture = nullptr;
    QOpenGLTexture* fireArrowSmokeTexture = textures->texture("firearrowsmoke");
    if(fireArrowSmokeTexture == fallbackTexture)
        fireArrowSmokeTexture = nullptr;
    QOpenGLTexture* rikArrowSmokeTexture = textures->texture("rikarrowsmoke");
    if(rikArrowSmokeTexture == fallbackTexture)
        rikArrowSmokeTexture = nullptr;
    QOpenGLTexture* stenchTexture = textures->texture("stench");
    if(stenchTexture == fallbackTexture)
        stenchTexture = nullptr;
    QOpenGLTexture* linkTexture = textures->texture("link");
    if(linkTexture == fallbackTexture)
        linkTexture = nullptr;
    QOpenGLTexture* protectionATexture = textures->texture("protectiona");
    if(protectionATexture == fallbackTexture)
        protectionATexture = nullptr;
    QOpenGLTexture* protectionETexture = textures->texture("protectione");
    if(protectionETexture == fallbackTexture)
        protectionETexture = nullptr;
    QOpenGLTexture* protectionFTexture = textures->texture("protectionf");
    if(protectionFTexture == fallbackTexture)
        protectionFTexture = nullptr;
    QOpenGLTexture* antimagicTexture = textures->texture("antimagic");
    if(antimagicTexture == fallbackTexture)
        antimagicTexture = nullptr;
    QOpenGLTexture* stunTexture = textures->texture("stun");
    if(stunTexture == fallbackTexture)
        stunTexture = nullptr;
    QOpenGLTexture* healingTexture = textures->texture("healing");
    if(healingTexture == fallbackTexture)
        healingTexture = nullptr;
    QOpenGLTexture* explosionTexture = textures->texture("explosion");
    if(explosionTexture == fallbackTexture)
        explosionTexture = nullptr;
    QOpenGLTexture* poisonCloudTexture = textures->texture("poisoncloud");
    if(poisonCloudTexture == fallbackTexture)
        poisonCloudTexture = nullptr;
    QOpenGLTexture* fireworksTexture = textures->texture("fireworks");
    if(fireworksTexture == fallbackTexture)
        fireworksTexture = nullptr;
    QOpenGLTexture* vulkanTexture = textures->texture("vulkan");
    if(vulkanTexture == fallbackTexture)
        vulkanTexture = nullptr;
    QOpenGLTexture* clayRingTexture = textures->texture("clayring");
    if(clayRingTexture == fallbackTexture)
        clayRingTexture = nullptr;
    QOpenGLTexture* teleportTexture = textures->texture("teleport");
    if(teleportTexture == fallbackTexture)
        teleportTexture = nullptr;
    QOpenGLTexture* zoneExitTexture = textures->texture("zoneexit");
    if(zoneExitTexture == fallbackTexture)
        zoneExitTexture = nullptr;
    QOpenGLTexture* shapeChangeTexture = textures->texture("shapechange");
    if(shapeChangeTexture == fallbackTexture)
        shapeChangeTexture = nullptr;
    QOpenGLTexture* bagTexture = textures->texture("bag");
    if(bagTexture == fallbackTexture)
        bagTexture = nullptr;
    QOpenGLTexture* casting6Texture = textures->texture("casting6");
    if(casting6Texture == fallbackTexture)
        casting6Texture = nullptr;
    QOpenGLTexture* casting2Texture = textures->texture("casting2");
    if(casting2Texture == fallbackTexture)
        casting2Texture = nullptr;
    QOpenGLTexture* casting1Texture = textures->texture("casting1");
    if(casting1Texture == fallbackTexture)
        casting1Texture = nullptr;
    QOpenGLTexture* casting3Texture = textures->texture("casting3");
    if(casting3Texture == fallbackTexture)
        casting3Texture = nullptr;
    QOpenGLTexture* casting4Texture = textures->texture("casting4");
    if(casting4Texture == fallbackTexture)
        casting4Texture = nullptr;
    QOpenGLTexture* casting7Texture = textures->texture("casting7");
    if(casting7Texture == fallbackTexture)
        casting7Texture = nullptr;
    QOpenGLTexture* casting8Texture = textures->texture("casting8");
    if(casting8Texture == fallbackTexture)
        casting8Texture = nullptr;
    QOpenGLTexture* visionFogTexture = textures->texture("visionfog");
    if(visionFogTexture == fallbackTexture)
        visionFogTexture = nullptr;

    bool inverseOk = false;
    const QMatrix4x4 inverseView = cameraMatrix.inverted(&inverseOk);
    if(!inverseOk)
        return;
    const QVector3D cameraRight = (inverseView * QVector4D(1.0f, 0.0f, 0.0f, 0.0f))
        .toVector3D().normalized();
    const QVector3D cameraUp = (inverseView * QVector4D(0.0f, 1.0f, 0.0f, 0.0f))
        .toVector3D().normalized();
    if(cameraRight.isNull() || cameraUp.isNull())
        return;

    if(!m_particleProgram.bind())
        return;
    m_particleProgram.setUniformValue("u_projMmatrix", m_projection);
    m_particleProgram.setUniformValue("u_viewMmatrix", cameraMatrix);

    // Scene parts are submitted through cached VAOs. Never configure the
    // dynamic particle stream while the last part VAO is still bound: doing so
    // replaces that cached VAO's attribute layout with the particle VBO and
    // corrupts normal scene submission on the following frame. The default VAO
    // keeps the particle attributes isolated from every shared mesh.
    QOpenGLContext* particleContext = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* particleFunctions
        = particleContext ? particleContext->extraFunctions() : nullptr;
    if(particleFunctions)
        particleFunctions->glBindVertexArray(0);

    ei::gl::Enable(GL_DEPTH_TEST);
    ei::gl::DepthMask(GL_FALSE);
    ei::gl::Enable(GL_BLEND);
    ei::gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const qint64 particleTimeMs = QDateTime::currentMSecsSinceEpoch();
    const float timeSeconds = float(particleTimeMs % 600000) / 1000.0f;
    const qint64 logicalTick = particleTimeMs / ei::particle::kLogicalQuantMilliseconds;
    const float logicalFraction = float(
        particleTimeMs % ei::particle::kLogicalQuantMilliseconds)
        / float(ei::particle::kLogicalQuantMilliseconds);
    const CWorldSet* particleWorldSet = m_environmentMob
        ? &m_environmentMob->worldSet() : nullptr;
    const COptBool* particleOverrideTimeOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("overrideWorldTime")) : nullptr;
    const COptInt* particleTimeOption = m_pSettings
        ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderTimeOfDay")) : nullptr;
    const bool particleOverrideTime = particleOverrideTimeOption
        ? particleOverrideTimeOption->value() : false;
    const float particlePreviewTime = particleTimeOption
        ? ei::clockValueToHours(particleTimeOption->value()) : 12.0f;
    const ei::SRenderEnvironment particleEnvironment = ei::renderEnvironment(
        particleWorldSet, particleOverrideTime, particlePreviewTime,
        m_caveEnvironment, m_weatherRegion);
    const QVector3D windDirection = particleWorldSet
        ? util::vec3FromString(particleWorldSet->data(eWsTypeWindDir))
        : QVector3D();
    const float windStrength = particleWorldSet
        ? particleWorldSet->data(eWsTypeWindStr).toFloat() : 0.0f;
    RenderPerformanceContext& performance = RenderPerformanceContext::instance();

    struct ParticleVertex
    {
        float world[3];
        float texture[2];
        float localTexture[2];
        float tint[4];
    };
    struct ParticleBatch
    {
        QOpenGLTexture* texture{nullptr};
        bool additive{false};
        bool radialMask{false};
        float softness{0.0f};
        int first{0};
        int count{0};
    };
    QVector<ParticleVertex> particleVertices;
    QVector<ParticleBatch> particleBatches;
    particleVertices.reserve(4096);
    particleBatches.reserve(64);

    const auto drawSprite = [&](const QVector3D& center, float scale, float aspect, float rotation,
                                const QVector4D& tint, QOpenGLTexture* texture,
                                const QVector4D& uvRect, bool additive, bool radialMask,
                                float softness)
    {
        const bool compatible = !particleBatches.isEmpty()
            && particleBatches.back().texture == texture
            && particleBatches.back().additive == additive
            && particleBatches.back().radialMask == radialMask
            && qFuzzyCompare(particleBatches.back().softness + 1.0f,
                             softness + 1.0f);
        if(!compatible)
        {
            ParticleBatch batch;
            batch.texture = texture;
            batch.additive = additive;
            batch.radialMask = radialMask;
            batch.softness = softness;
            batch.first = particleVertices.size();
            particleBatches.append(batch);
        }
        const float width = qBound(0.03f, scale, 64.0f);
        const float height = width * qBound(0.25f, aspect, 4.0f);
        const float cosine = qCos(rotation);
        const float sine = qSin(rotation);
        static const float corners[6][2] = {
            {-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f},
            {-0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}
        };
        static const float textureCoordinates[6][2] = {
            {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f},
            {0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}
        };
        for(int vertexIndex = 0; vertexIndex < 6; ++vertexIndex)
        {
            const float sizedX = corners[vertexIndex][0] * width;
            const float sizedY = corners[vertexIndex][1] * height;
            const float localX = cosine * sizedX - sine * sizedY;
            const float localY = sine * sizedX + cosine * sizedY;
            const QVector3D world = center + cameraRight * localX + cameraUp * localY;
            ParticleVertex vertex;
            vertex.world[0] = world.x();
            vertex.world[1] = world.y();
            vertex.world[2] = world.z();
            vertex.texture[0] = uvRect.x() + textureCoordinates[vertexIndex][0] * uvRect.z();
            vertex.texture[1] = uvRect.y() + textureCoordinates[vertexIndex][1] * uvRect.w();
            vertex.localTexture[0] = textureCoordinates[vertexIndex][0];
            vertex.localTexture[1] = textureCoordinates[vertexIndex][1];
            vertex.tint[0] = tint.x();
            vertex.tint[1] = tint.y();
            vertex.tint[2] = tint.z();
            vertex.tint[3] = tint.w();
            particleVertices.append(vertex);
        }
        particleBatches.back().count += 6;
        if(performance.collecting())
            ++performance.counters().nodesSubmitted;
    };

    const auto drawFireSmoke = [&](const CNode* owner, const QVector3D& origin,
                                   float authoredScale, uint seed, uint kind,
                                   int prewarmTicks)
    {
        QOpenGLTexture* atlas = tendrilSmokeTexture
            ? tendrilSmokeTexture : fireSmokeTexture;
        ei::particle::TendriEmitter& emitter = m_tendriEmitters[owner];
        emitter.advanceTo(logicalTick, seed, kind, origin, authoredScale,
                          windDirection, windStrength, prewarmTicks);
        const QVector<ei::particle::RenderSample> samples
            = emitter.samples(logicalFraction);
        for(const ei::particle::RenderSample& sample : samples)
        {
            drawSprite(sample.position,
                       ei::particle::billboardDiameter(sample.scale),
                       1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, 1.0f), atlas,
                       ei::particle::atlasUv(sample.frame), false,
                       atlas == nullptr, 0.18f);
        }
    };

    const auto drawFireball = [&](const CNode* owner, const QVector3D& origin,
                                  float authoredScale, uint seed)
    {
        ei::particle::FireballEmitter& emitter = m_fireballEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       fireSmokeTexture, ei::particle::atlasUv(sample.frame), false,
                       fireSmokeTexture == nullptr, 0.18f);
    };

    const auto drawVulcanSmoke = [&](const CNode* owner, const QVector3D& origin,
                                     float authoredScale, uint seed)
    {
        ei::particle::VulcanSmokeEmitter& emitter = m_vulcanSmokeEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), vulkanTexture,
                       ei::particle::atlasUv(sample.frame), false,
                       vulkanTexture == nullptr, 0.18f);
    };

    const auto drawFireBlast = [&](const CNode* owner, const QVector3D& origin,
                                   float authoredScale, uint seed)
    {
        ei::particle::FireBlastEmitter& emitter = m_fireBlastEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       explosionTexture, ei::particle::atlasUv(sample.frame), false,
                       explosionTexture == nullptr, 0.18f);
    };

    const auto drawHealing = [&](const CNode* owner, const QVector3D& origin,
                                 float authoredScale, uint seed)
    {
        ei::particle::HealingEmitter& emitter = m_healingEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       healingTexture, ei::particle::atlasUv(sample.frame), true,
                       healingTexture == nullptr, 0.18f);
    };

    const auto drawPoisonFog = [&](const CNode* owner, const QVector3D& origin,
                                   float authoredScale, bool aggression, uint seed)
    {
        ei::particle::PoisonFogEmitter& emitter = m_poisonFogEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale, aggression);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(sample.color, sample.alpha), poisonCloudTexture,
                       ei::particle::atlasUv(sample.frame), false,
                       poisonCloudTexture == nullptr, 0.18f);
    };

    const auto drawGeyser = [&](const CNode* owner, const QVector3D& origin,
                                float authoredScale, uint seed)
    {
        ei::particle::GeyserEmitter& emitter = m_geyserEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       fireworksTexture, ei::particle::atlasUv(sample.frame), true,
                       fireworksTexture == nullptr, 0.18f);
    };

    const auto drawTornado = [&](const CNode* owner, const QVector3D& origin,
                                 float authoredScale, bool curseHolder, uint seed)
    {
        ei::particle::TornadoEmitter& emitter = m_tornadoEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale, 100,
                          curseHolder);
        const QVector4D sunlightTint(particleEnvironment.sunColor, 1.0f);
        const QVector<ei::particle::WeatherRenderSample> samples = emitter.samples();
        for(const ei::particle::WeatherRenderSample& sample : samples)
        {
            QVector4D tint = curseHolder
                ? QVector4D(0.0f, 0.0f, 0.0f, 1.0f) : sunlightTint;
            tint.setW(sample.alpha);
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f, tint,
                       tornadoTexture, ei::particle::atlasUv(sample.frame), false,
                       tornadoTexture == nullptr, 0.18f);
        }
    };

    const auto drawLightningBlast = [&](const CNode* owner, const QVector3D& origin,
                                        float authoredScale, uint seed)
    {
        ei::particle::LightningBlastEmitter& emitter = m_lightningBlastEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        const QVector<ei::particle::WeatherRenderSample> samples = emitter.samples();
        for(const ei::particle::WeatherRenderSample& sample : samples)
        {
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       tendriLightTexture, ei::particle::atlasUv(sample.frame), true,
                       tendriLightTexture == nullptr, 0.14f);
        }
    };

    const auto drawCasting = [&](const CNode* owner, const QVector3D& origin,
                                 float authoredScale, uint seed)
    {
        ei::particle::CastingEmitter& emitter = m_castingEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        const QVector<ei::particle::WeatherRenderSample> samples = emitter.samples();
        for(const ei::particle::WeatherRenderSample& sample : samples)
        {
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       fireSmokeTexture, ei::particle::atlasUv(sample.frame), false,
                       fireSmokeTexture == nullptr, 0.18f);
        }
    };

    const auto drawCastingGather = [&](const CNode* owner, const QVector3D& origin,
                                       float authoredScale, quint32 kind, uint seed)
    {
        ei::particle::CastingGatherEmitter& emitter = m_castingGatherEmitters[owner];
        emitter.advanceTo(logicalTick, seed, kind, origin, authoredScale);
        QOpenGLTexture* texture = fireSmokeTexture;
        if(kind == 8232u)
            texture = casting8Texture;
        else if(kind == 8233u)
            texture = casting7Texture;
        else if(kind == 8234u)
            texture = casting4Texture;
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), texture,
                       ei::particle::atlasUv(sample.frame), kind == 8234u,
                       texture == nullptr, 0.18f);
    };

    const auto drawCastingRound = [&](const CNode* owner, const QVector3D& origin,
                                      float authoredScale, quint32 kind, uint seed)
    {
        ei::particle::CastingStarRoundEmitter& emitter = m_castingRoundEmitters[owner];
        emitter.advanceTo(logicalTick, seed, kind, origin, authoredScale);
        QOpenGLTexture* texture = casting2Texture;
        if(kind == 8236u)
            texture = casting3Texture;
        else if(kind == 8237u)
            texture = casting6Texture;
        else if(kind == 8238u)
            texture = casting1Texture;
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), texture,
                       ei::particle::atlasUv(sample.frame), false,
                       texture == nullptr, 0.18f);
    };

    const auto drawCastingFailed = [&](const CNode* owner, const QVector3D& origin,
                                       float authoredScale, uint seed)
    {
        ei::particle::CastingFailedEmitter& emitter = m_castingFailedEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), teleportTexture,
                       ei::particle::atlasUv(sample.frame), false,
                       teleportTexture == nullptr, 0.18f);
    };

    const auto drawNuke = [&](const CNode* owner, const QVector3D& origin,
                              float authoredScale, uint seed)
    {
        ei::particle::NukeEmitter& emitter = m_nukeEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        const QVector<ei::particle::WeatherRenderSample> samples = emitter.samples();
        for(const ei::particle::WeatherRenderSample& sample : samples)
        {
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       acidRayTexture, ei::particle::atlasUv(sample.frame), false,
                       acidRayTexture == nullptr, 0.18f);
        }
    };

    const auto drawBansheeCasting = [&](const CNode* owner, const QVector3D& origin,
                                        float authoredScale, uint seed)
    {
        ei::particle::BansheeCastingEmitter& emitter = m_bansheeCastingEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        const QVector<ei::particle::WeatherRenderSample> samples = emitter.samples();
        for(const ei::particle::WeatherRenderSample& sample : samples)
        {
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       bansheeTexture, ei::particle::atlasUv(sample.frame), false,
                       bansheeTexture == nullptr, 0.18f);
        }
    };

    const auto drawMushroom = [&](const CNode* owner, const QVector3D& origin,
                                  float authoredScale, uint seed)
    {
        ei::particle::MushroomEmitter& emitter = m_mushroomEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       changeHeroTexture, ei::particle::atlasUv(sample.frame), false,
                       changeHeroTexture == nullptr, 0.18f);
    };

    const auto drawBlood = [&](const CNode* owner, const QVector3D& origin,
                               float syntheticCarrierBound, int frame, uint seed)
    {
        ei::particle::Blood1Emitter& emitter = m_blood1Emitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, syntheticCarrierBound, frame);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       rainSnowTexture, ei::particle::atlasUv(sample.frame), false,
                       rainSnowTexture == nullptr, 0.18f);
    };

    const auto drawA4Blood = [&](const CNode* owner, const QVector3D& origin,
                                 uint seed)
    {
        ei::particle::A4BloodEmitter& emitter = m_a4BloodEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       rainSnowTexture, ei::particle::atlasUv(sample.frame), true,
                       rainSnowTexture == nullptr, 0.18f);
    };

    const auto drawFireArrow = [&](const CNode* owner, const QVector3D& origin,
                                   float authoredScale, QOpenGLTexture* atlas,
                                   uint seed)
    {
        ei::particle::FireArrowEmitter& emitter = m_fireArrowEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       atlas, ei::particle::atlasUv(sample.frame), false,
                       atlas == nullptr, 0.18f);
    };

    const auto drawAcidRay = [&](const CNode* owner, const QVector3D& origin,
                                 float authoredScale, uint seed)
    {
        ei::particle::AcidRayEmitter& emitter = m_acidRayEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       acidRayTexture, ei::particle::atlasUv(sample.frame), false,
                       acidRayTexture == nullptr, 0.18f);
    };

    const auto drawBlueGas = [&](const CNode* owner, const QVector3D& origin,
                                 float syntheticCarrierBound, uint seed)
    {
        ei::particle::BlueGasEmitter& emitter = m_blueGasEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, syntheticCarrierBound);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       stenchTexture, ei::particle::atlasUv(sample.frame), false,
                       stenchTexture == nullptr, 0.18f);
    };

    const auto drawLink = [&](const CNode* owner, const QVector3D& origin,
                              float authoredScale, uint seed)
    {
        ei::particle::LinkEmitter& emitter = m_linkEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       linkTexture, ei::particle::atlasUv(sample.frame), false,
                       linkTexture == nullptr, 0.18f);
    };

    const auto drawSphereAcid = [&](const CNode* owner, const QVector3D& origin,
                                    float syntheticCarrierBound, uint seed)
    {
        ei::particle::SphereAcidEmitter& emitter = m_sphereAcidEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, syntheticCarrierBound);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       protectionATexture, ei::particle::atlasUv(sample.frame), true,
                       protectionATexture == nullptr, 0.18f);
    };

    const auto drawSphereElectricity = [&](const CNode* owner, const QVector3D& origin,
                                           float syntheticCarrierBound, uint seed)
    {
        ei::particle::SphereElectricityEmitter& emitter =
            m_sphereElectricityEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, syntheticCarrierBound);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       protectionETexture, ei::particle::atlasUv(sample.frame), true,
                       protectionETexture == nullptr, 0.18f);
    };

    const auto drawSphereFire = [&](const CNode* owner, const QVector3D& origin,
                                    float syntheticCarrierBound, uint seed)
    {
        ei::particle::SphereFireEmitter& emitter = m_sphereFireEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, syntheticCarrierBound);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       protectionFTexture, ei::particle::atlasUv(sample.frame), true,
                       protectionFTexture == nullptr, 0.18f);
    };

    const auto drawAntimagic = [&](const CNode* owner, const QVector3D& origin,
                                   float syntheticCarrierBound, uint seed)
    {
        ei::particle::AntimagicEmitter& emitter = m_antimagicEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, syntheticCarrierBound);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       antimagicTexture, ei::particle::atlasUv(sample.frame), true,
                       antimagicTexture == nullptr, 0.18f);
    };

    const auto drawModifier = [&](const CNode* owner, const QVector3D& origin,
                                  float signedAuthoredScale, int subtype, uint seed)
    {
        ei::particle::ModifierEmitter& emitter = m_modifierEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, signedAuthoredScale, subtype);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       stunTexture, ei::particle::atlasUv(sample.frame), true,
                       stunTexture == nullptr, 0.18f);
    };

    const auto drawClayRing = [&](const CNode* owner, const QVector3D& origin,
                                  float signedAuthoredScale, uint seed)
    {
        ei::particle::ClayRingEmitter& emitter = m_clayRingEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, signedAuthoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       clayRingTexture, ei::particle::atlasUv(sample.frame), true,
                       clayRingTexture == nullptr, 0.18f);
    };

    const auto drawTeleport = [&](const CNode* owner, const QVector3D& origin,
                                  float signedAuthoredScale, uint seed)
    {
        ei::particle::TeleportEmitter& emitter = m_teleportEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, signedAuthoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       teleportTexture, ei::particle::atlasUv(sample.frame), false,
                       teleportTexture == nullptr, 0.18f);
    };

    const auto drawZoneExit = [&](const CNode* owner, const QVector3D& origin,
                                  float authoredScale, uint seed)
    {
        ei::particle::ZoneExitEmitter& emitter = m_zoneExitEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(sample.color, sample.alpha), zoneExitTexture,
                       ei::particle::atlasUv(sample.frame), false,
                       zoneExitTexture == nullptr, 0.18f);
    };

    const auto drawPath = [&](const CNode* owner, const QVector3D& origin, uint seed)
    {
        ei::particle::PathEmitter& emitter = m_pathEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(sample.color, sample.alpha), shapeChangeTexture,
                       ei::particle::atlasUv(sample.frame), false,
                       shapeChangeTexture == nullptr, 0.18f);
    };

    const auto drawPathDestination = [&](const CNode* owner, const QVector3D& origin,
                                         bool failed, uint seed)
    {
        ei::particle::PathDestinationEmitter& emitter = m_pathDestinationEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, failed);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(sample.color, sample.alpha), shapeChangeTexture,
                       ei::particle::atlasUv(sample.frame), false,
                       shapeChangeTexture == nullptr, 0.18f);
    };

    const auto drawMoshka = [&](const CNode* owner, const QVector3D& origin,
                                float authoredScale, uint seed)
    {
        ei::particle::MoshkaEmitter& emitter = m_moshkaEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(sample.color, sample.alpha), bagTexture,
                       ei::particle::atlasUv(sample.frame), false,
                       bagTexture == nullptr, 0.18f);
    };

    const auto drawPortalStar = [&](const CNode* owner, const QVector3D& origin,
                                    float authoredScale, uint seed)
    {
        ei::particle::PortalStarEmitter& emitter = m_portalStarEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       casting6Texture, ei::particle::atlasUv(sample.frame), false,
                       casting6Texture == nullptr, 0.18f);
    };

    const auto drawPortal = [&](const CNode* owner, const QVector3D& origin,
                                float authoredScale, uint seed)
    {
        ei::particle::PortalEmitter& emitter = m_portalEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       teleportTexture, ei::particle::atlasUv(sample.frame), true,
                       teleportTexture == nullptr, 0.18f);
    };

    const auto drawCylinder = [&](const CNode* owner, const QVector3D& origin,
                                  float authoredScale, bool reverse, uint seed)
    {
        ei::particle::CylinderEmitter& emitter = m_cylinderEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale, reverse);
        QOpenGLTexture* texture = reverse ? casting1Texture : casting2Texture;
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), texture,
                       ei::particle::atlasUv(sample.frame), false,
                       texture == nullptr, 0.18f);
    };

    const auto drawStarFlash = [&](const CNode* owner, const QVector3D& origin,
                                   float authoredScale, bool acid, uint seed)
    {
        ei::particle::StarFlashEmitter& emitter = m_starFlashEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        QOpenGLTexture* texture = acid ? acidRayTexture : fireArrowSmokeTexture;
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), texture,
                       ei::particle::atlasUv(sample.frame), false,
                       texture == nullptr, 0.18f);
    };

    const auto drawSparks = [&](const CNode* owner, const QVector3D& origin,
                                float authoredScale, uint seed)
    {
        ei::particle::SparksEmitter& emitter = m_sparksEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha),
                       casting6Texture, ei::particle::atlasUv(sample.frame), true,
                       casting6Texture == nullptr, 0.18f);
    };

    const auto drawVisionStar = [&](const CNode* owner, const QVector3D& origin,
                                    float authoredScale, int subtype, uint seed)
    {
        ei::particle::VisionStarEmitter& emitter = m_visionStarEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale, subtype);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), stunTexture,
                       ei::particle::atlasUv(sample.frame), true,
                       stunTexture == nullptr, 0.18f);
    };

    const auto drawRegeneration = [&](const CNode* owner, const QVector3D& origin,
                                      float authoredScale, uint seed)
    {
        ei::particle::RegenerationEmitter& emitter = m_regenerationEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), stunTexture,
                       ei::particle::atlasUv(sample.frame), true,
                       stunTexture == nullptr, 0.18f);
    };

    const auto drawSilence = [&](const CNode* owner, const QVector3D& origin,
                                 float authoredScale, uint seed)
    {
        ei::particle::SilenceEmitter& emitter = m_silenceEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), stunTexture,
                       ei::particle::atlasUv(sample.frame), true,
                       stunTexture == nullptr, 0.18f);
    };

    const auto drawFeeblemind = [&](const CNode* owner, const QVector3D& origin,
                                    float authoredScale, uint seed)
    {
        ei::particle::FeeblemindEmitter& emitter = m_feeblemindEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), linkTexture,
                       ei::particle::atlasUv(sample.frame), true,
                       linkTexture == nullptr, 0.18f);
    };

    const auto drawFeetCloud = [&](const CNode* owner, const QVector3D& origin,
                                   float authoredScale, bool red, uint seed)
    {
        ei::particle::FeetCloudEmitter& emitter = m_feetCloudEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale, red);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(sample.color, sample.alpha), visionFogTexture,
                       ei::particle::atlasUv(sample.frame), true,
                       visionFogTexture == nullptr, 0.18f);
    };

    const auto drawBallStars = [&](const CNode* owner, const QVector3D& origin,
                                   float authoredScale, uint seed)
    {
        ei::particle::BallStarsEmitter& emitter = m_ballStarsEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), stunTexture,
                       ei::particle::atlasUv(sample.frame), true,
                       stunTexture == nullptr, 0.18f);
    };

    const auto drawCurseStars = [&](const CNode* owner, const QVector3D& origin,
                                    float authoredScale, uint seed)
    {
        ei::particle::CurseStarsEmitter& emitter = m_curseStarsEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), healingTexture,
                       ei::particle::atlasUv(sample.frame), true,
                       healingTexture == nullptr, 0.18f);
    };

    const auto drawStartTrans = [&](const CNode* owner, const QVector3D& origin,
                                    float authoredScale, uint seed)
    {
        ei::particle::StartTransEmitter& emitter = m_startTransEmitters[owner];
        emitter.advanceTo(logicalTick, seed, origin, authoredScale);
        for(const ei::particle::EffectRenderSample& sample : emitter.samples())
            drawSprite(sample.position, sample.scale, 1.0f, 0.0f,
                       QVector4D(1.0f, 1.0f, 1.0f, sample.alpha), zoneExitTexture,
                       ei::particle::atlasUv(sample.frame), false,
                       zoneExitTexture == nullptr, 0.18f);
    };

    for(CMob* mob : m_aMob)
    {
        if(!isMobVisible(mob))
            continue;
        const QVector<CNode*> candidates = spatiallyVisibleNodes(mob, frustum, nullptr);
        for(CNode* node : candidates)
        {
            if(!node || node->isMarkDeleted()
                    || node->nodeState() == ENodeState::eHidden)
                continue;

            if(CTorch* torch = dynamic_cast<CTorch*>(node))
            {
                // A torch remains an ordinary textured world object. Its
                // authored TORCH_PTLINK only adds this attached effect.
                drawFireSmoke(torch, torch->particleWorldPosition(), torch->power(),
                              torch->mapId(), 0x2001u, 60);
                continue;
            }

            CParticle* particle = dynamic_cast<CParticle*>(node);
            if(!particle)
                continue;

            const SParticleVisualStyle style = particleVisualStyle(particle->kind());
            QOpenGLTexture* texture = style.textureName
                ? textures->texture(QString::fromLatin1(style.textureName)) : fallbackTexture;
            const bool useTexture = texture && texture != fallbackTexture;
            texture = useTexture ? texture : nullptr;
            QVector3D center = particle->drawPosition();
            if(center.isNull() && !particle->position().isNull())
                center = particle->position();
            const float authoredScale = particle->particleScale() > 0.0f
                ? particle->particleScale() : 1.0f;
            const float signedAuthoredScale = qFuzzyIsNull(particle->particleScale())
                ? 1.0f : particle->particleScale();
            const float phase = timeSeconds * (0.75f + float(particle->kind() % 7) * 0.08f)
                + float(particle->mapId() % 31) * 0.37f;
            const float pulse = 1.0f + 0.08f * qSin(phase * 2.1f);
            QVector4D tint = style.tint;
            if(particle->nodeState() == ENodeState::eSelect)
                tint = QVector4D(0.10f, 0.90f, 1.0f, qMax(0.78f, tint.w()));

            if(particle->kind() == 8192u)
            {
                drawFireball(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(particle->kind() == 8194u)
            {
                drawFireBlast(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(particle->kind() == 8198u)
            {
                drawHealing(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(particle->kind() == 8199u || particle->kind() == 8200u)
            {
                drawPoisonFog(particle, center, authoredScale,
                              particle->kind() == 8200u, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(particle->kind() == 8201u)
            {
                drawGeyser(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }

            if(particle->kind() == 8197u)
            {
                drawVulcanSmoke(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(isPersistentFireParticle(particle->kind())
                    || isPersistentSmokeParticle(particle->kind()))
            {
                drawFireSmoke(particle, center, authoredScale, particle->mapId(),
                              particle->kind(), 0);
                // Selection is an editor affordance, not a color mutation of
                // the stock particle population.
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(particle->kind() == 8202u)
            {
                drawTornado(particle, center, authoredScale, false,
                            particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(particle->kind() == 8206u)
            {
                drawMushroom(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8207u)
            {
                drawBlood(particle, center, authoredScale, 12, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() >= 8241u && particle->kind() <= 8243u)
            {
                drawBlood(particle, center, authoredScale,
                          13 + int(particle->kind() - 8241u), particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() >= 8244u && particle->kind() <= 8247u)
            {
                drawA4Blood(particle, center, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8248u)
            {
                drawZoneExit(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8249u)
            {
                drawPath(particle, center, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8250u || particle->kind() == 8251u)
            {
                drawPathDestination(particle, center, particle->kind() == 8251u,
                                    particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8252u)
            {
                drawMoshka(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8253u)
            {
                drawPortalStar(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8254u)
            {
                drawPortal(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8255u || particle->kind() == 8256u)
            {
                drawCylinder(particle, center, authoredScale,
                             particle->kind() == 8256u, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8257u || particle->kind() == 8258u)
            {
                drawStarFlash(particle, center, authoredScale,
                              particle->kind() == 8258u, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8259u)
            {
                drawSparks(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if((particle->kind() >= 8260u && particle->kind() <= 8262u)
                    || particle->kind() == 8268u)
            {
                const int subtype = particle->kind() == 8268u
                    ? 3 : int(particle->kind() - 8260u);
                drawVisionStar(particle, center, authoredScale, subtype,
                               particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8263u)
            {
                drawRegeneration(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8264u)
            {
                drawSilence(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8265u)
            {
                drawFeeblemind(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8266u || particle->kind() == 8267u)
            {
                drawFeetCloud(particle, center, authoredScale,
                              particle->kind() == 8267u, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8269u)
            {
                drawBallStars(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8270u)
            {
                drawFireArrow(particle, center, authoredScale,
                              rikArrowSmokeTexture, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8271u)
            {
                drawCurseStars(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8272u)
            {
                drawTornado(particle, center, authoredScale, true,
                            particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8273u)
            {
                drawStartTrans(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8274u)
            {
                drawMushroom(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8209u)
            {
                drawFireArrow(particle, center, authoredScale,
                              fireArrowSmokeTexture, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8210u)
            {
                drawAcidRay(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8211u)
            {
                drawBlueGas(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8212u)
            {
                drawLink(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8213u)
            {
                drawSphereAcid(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8214u)
            {
                drawSphereElectricity(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8215u)
            {
                drawSphereFire(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8216u)
            {
                drawClayRing(particle, center, signedAuthoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, qAbs(signedAuthoredScale) * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8217u)
            {
                drawTeleport(particle, center, signedAuthoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, qAbs(signedAuthoredScale) * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8218u)
            {
                drawAntimagic(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() >= 8219u && particle->kind() <= 8230u)
            {
                drawModifier(particle, center, signedAuthoredScale,
                             int(particle->kind() - 8218u), particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, qAbs(signedAuthoredScale) * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() >= 8231u && particle->kind() <= 8234u)
            {
                drawCastingGather(particle, center, authoredScale, particle->kind(),
                                  particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() >= 8235u && particle->kind() <= 8238u)
            {
                drawCastingRound(particle, center, authoredScale, particle->kind(),
                                 particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8239u)
            {
                drawCastingFailed(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f), 1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false, true, 0.12f);
                continue;
            }
            if(particle->kind() == 8240u)
            {
                drawLightningBlast(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(particle->kind() == 8203u)
            {
                drawCasting(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(particle->kind() == 8204u)
            {
                drawNuke(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            if(particle->kind() == 8205u)
            {
                drawBansheeCasting(particle, center, authoredScale, particle->mapId());
                if(particle->nodeState() == ENodeState::eSelect)
                    drawSprite(center, qMax(0.3f, authoredScale * 0.35f),
                               1.0f, 0.0f,
                               QVector4D(0.10f, 0.90f, 1.0f, 0.72f), nullptr,
                               QVector4D(0.0f, 0.0f, 1.0f, 1.0f), false,
                               true, 0.12f);
                continue;
            }
            drawSprite(center, authoredScale * style.scaleMultiplier * pulse,
                       1.0f, phase * 0.18f, tint, useTexture ? texture : nullptr,
                       QVector4D(0.0f, 0.0f, 1.0f, 1.0f),
                       style.additive, true, style.softness);
        }
    }

    const int worldLocation = m_particleProgram.attributeLocation("a_worldPosition");
    const int textureLocation = m_particleProgram.attributeLocation("a_texture");
    const int localTextureLocation = m_particleProgram.attributeLocation("a_localTexture");
    const int tintLocation = m_particleProgram.attributeLocation("a_tint");
    const bool validAttributes = worldLocation >= 0 && textureLocation >= 0
        && localTextureLocation >= 0 && tintLocation >= 0;
    if(!particleVertices.isEmpty() && validAttributes && m_particleBuffer.bind())
    {
        m_particleBuffer.allocate(particleVertices.constData(),
            particleVertices.size() * int(sizeof(ParticleVertex)));
        const int stride = int(sizeof(ParticleVertex));
        m_particleProgram.enableAttributeArray(worldLocation);
        m_particleProgram.enableAttributeArray(textureLocation);
        m_particleProgram.enableAttributeArray(localTextureLocation);
        m_particleProgram.enableAttributeArray(tintLocation);
        m_particleProgram.setAttributeBuffer(worldLocation, GL_FLOAT,
            int(offsetof(ParticleVertex, world)), 3, stride);
        m_particleProgram.setAttributeBuffer(textureLocation, GL_FLOAT,
            int(offsetof(ParticleVertex, texture)), 2, stride);
        m_particleProgram.setAttributeBuffer(localTextureLocation, GL_FLOAT,
            int(offsetof(ParticleVertex, localTexture)), 2, stride);
        m_particleProgram.setAttributeBuffer(tintLocation, GL_FLOAT,
            int(offsetof(ParticleVertex, tint)), 4, stride);

        QOpenGLTexture* boundTexture = nullptr;
        bool additiveBlend = false;
        for(const ParticleBatch& batch : particleBatches)
        {
            if(batch.texture != boundTexture)
            {
                if(batch.texture)
                    batch.texture->bind(0);
                else
                    ei::gl::BindTexture(GL_TEXTURE_2D, 0);
                boundTexture = batch.texture;
                if(performance.collecting())
                    ++performance.counters().textureBinds;
            }
            if(batch.additive != additiveBlend)
            {
                ei::gl::BlendFunc(GL_SRC_ALPHA,
                    batch.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
                additiveBlend = batch.additive;
            }
            m_particleProgram.setUniformValue("u_useTexture", batch.texture != nullptr);
            m_particleProgram.setUniformValue("u_radialMask", batch.radialMask);
            m_particleProgram.setUniformValue("u_softness", batch.softness);
            ei::gl::DrawArrays(GL_TRIANGLES, batch.first, batch.count);
            if(performance.collecting())
            {
                ++performance.counters().drawCalls;
                ++performance.counters().batches;
                performance.counters().triangles += quint64(batch.count / 3);
            }
        }

        m_particleProgram.disableAttributeArray(tintLocation);
        m_particleProgram.disableAttributeArray(localTextureLocation);
        m_particleProgram.disableAttributeArray(textureLocation);
        m_particleProgram.disableAttributeArray(worldLocation);
        m_particleBuffer.release();
    }

    ei::gl::BindTexture(GL_TEXTURE_2D, 0);
    ei::gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ei::gl::DepthMask(GL_TRUE);
    m_particleProgram.release();
}

void CView::draw()
{

    QMatrix4x4 camMatrix = m_cam->update();
    QVector3D cameraAnchor, cameraPivot, cameraRotation;
    m_cam->getLocation(cameraAnchor, cameraPivot, cameraRotation);
    bool cameraEyeOk = false;
    const QVector3D renderedCameraEye = ei::cameraEyeFromView(camMatrix, &cameraEyeOk);
    const QVector3D cameraPosition = cameraEyeOk ? renderedCameraEye : cameraAnchor;
    const COptBool* animationOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("animateUnits")) : nullptr;
    const bool animationEnabled = !animationOption || animationOption->value();
    const COptBool* mipmappingOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("renderTextureMipmapping")) : nullptr;
    CTextureList::getInstance()->setTerrainMipmappingEnabled(
        !mipmappingOption || mipmappingOption->value());
    AnimationFrameContext::instance().beginFrame(cameraPosition,
        QDateTime::currentMSecsSinceEpoch(), animationEnabled);
    const COptBool* cullingOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("frustumCulling")) : nullptr;
    const bool cullingEnabled = !cullingOption || cullingOption->value();
    const CViewFrustum frustum(m_projection * camMatrix);
    const COptBool* drawLogicOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("drawLogic")) : nullptr;
    const COptBool* drawHelpOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("drawHelp")) : nullptr;
    const bool drawLogic = drawLogicOption && drawLogicOption->value();
    const bool drawHelp = drawHelpOption && drawHelpOption->value();
    const bool logicMode = CScene::getInstance()->getMode() == eEditModeLogic;
    const bool traverseHelpers = drawLogic || drawHelp || logicMode;
    const auto renderBool = [this](const char* name, bool fallback)
    {
        const COptBool* option = m_pSettings
            ? dynamic_cast<const COptBool*>(m_pSettings->opt(name)) : nullptr;
        return option ? option->value() : fallback;
    };
    const COptInt* lightingModeOption = m_pSettings
        ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderLightingMode")) : nullptr;
    const int requestedLightingMode = qBound(0,
        lightingModeOption ? lightingModeOption->value() : 2, 2);
    const int lightingMode = m_openGlesCompatibility
        ? qMin(requestedLightingMode, 1) : requestedLightingMode;
    const bool environmentEnabled = lightingMode == 2;
    // The viewport lighting-model selector owns environment activation. Keep
    // the resource sky in Full environment and out of the compatibility
    // modes instead of exposing a contradictory second Render setting.
    const bool skyEnabled = environmentEnabled;
    const bool fogEnabled = renderBool("renderFog", true);
    const bool sceneLightsEnabled = renderBool("renderSceneLights", true);
    const COptInt* shadowQualityOption = m_pSettings
        ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderShadowQuality")) : nullptr;
    const int shadowQuality = qBound(0,
        shadowQualityOption ? shadowQualityOption->value() : 2, 2);
    const bool shadowsEnabled = environmentEnabled && !m_openGlesCompatibility
        && shadowQuality > 0
        && ensureShadowFramebuffers(shadowQuality);
    const COptInt* shadowDistanceOption = m_pSettings
        ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderShadowDistance")) : nullptr;
    // The old adaptive map stopped at 160 world units. Keep twice that reach
    // by default while allowing large-scene authors to tune the preview.
    const float maximumShadowDistance = float(qBound(1,
        shadowDistanceOption ? shadowDistanceOption->value() : 320, 1024));
    const bool overrideWorldTime = renderBool("overrideWorldTime", false);
    const COptInt* timeOption = m_pSettings
        ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderTimeOfDay")) : nullptr;
    const float previewTime = timeOption
        ? ei::clockValueToHours(timeOption->value()) : 12.0f;
    const CWorldSet* worldSet = m_environmentMob
        ? &m_environmentMob->worldSet() : nullptr;
    const ei::SRenderEnvironment environment = ei::renderEnvironment(
        worldSet, overrideWorldTime, previewTime, m_caveEnvironment,
        m_weatherRegion);
    syncEnvironmentPreviewWidget(environment.timeHours);
    // Fill the open lower edge of the stock sky cylinder with the same color
    // that distant geometry converges to. This removes the editor-only black
    // seam without modifying the profile sky asset.
    if(environmentEnabled && fogEnabled)
        ei::gl::ClearColor(environment.fogColor.x(), environment.fogColor.y(),
                     environment.fogColor.z(), 1.0f);
    else
        ei::gl::ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    ei::gl::Clear(GL_COLOR_BUFFER_BIT);

    QVector<ei::SScenePointLight> sceneLights;
    if(environmentEnabled && sceneLightsEnabled)
    {
        for(CMob* mob : m_aMob)
            for(CNode* node : mob->nodes())
            {
                if(node->nodeState() == ENodeState::eHidden)
                    continue;
                if(CLight* light = dynamic_cast<CLight*>(node))
                {
                    const QVector3D position = light->drawPosition().isNull()
                        ? light->position() : light->drawPosition();
                    // Native generic LIGHT setup adds 2.0 to the authored Z
                    // coordinate before the light is registered. Torch lights
                    // use their particle pivot directly and do not get this lift.
                    const QVector3D runtimePosition = position + QVector3D(0.0f, 0.0f, 2.0f);
                    if(light->range() > 0.0f && (!cullingEnabled
                            || frustum.intersectsSphere(runtimePosition, light->range())))
                    {
                        ei::SScenePointLight sceneLight;
                        sceneLight.position = runtimePosition;
                        sceneLight.color = light->lightColor();
                        sceneLight.range = light->range();
                        sceneLights.append(sceneLight);
                    }
                }
                else if(CTorch* torch = dynamic_cast<CTorch*>(node))
                {
                    const float range = torch->power() * 10.0f;
                    const QVector3D position = torch->particleWorldPosition();
                    if(range > 0.0f && (!cullingEnabled
                            || frustum.intersectsSphere(position, range)))
                    {
                        ei::SScenePointLight sceneLight;
                        sceneLight.position = position;
                        sceneLight.color = QVector3D(0.8f, 0.8f, 0.8f);
                        sceneLight.range = range;
                        sceneLights.append(sceneLight);
                    }
                }
            }
        // Native terrain registers lights into every intersected spatial cell;
        // it does not select the eight nearest lights to the camera. The
        // fingerprinted JMV map corpus peaks at 34 lights in one MOB, so the
        // fixed shader capacity contains every authored map light.
        if(sceneLights.size() > ei::kMaxScenePointLights)
            sceneLights.resize(ei::kMaxScenePointLights);
    }

    RenderPerformanceContext& performance = RenderPerformanceContext::instance();
    if(performance.collecting())
    {
        const COptString* profileOption = m_pSettings
            ? dynamic_cast<const COptString*>(m_pSettings->opt("resourceProfile")) : nullptr;
        SRenderFrameMetadata metadata;
        metadata.viewportWidth = m_width;
        metadata.viewportHeight = m_height;
        metadata.cameraPosition = cameraPosition;
        metadata.cameraPivot = cameraPivot;
        metadata.cameraRotation = cameraRotation;
        metadata.profileId = profileOption ? profileOption->value() : QString();
        metadata.mapName = m_pLand && m_pLand->isMprLoad()
            ? m_pLand->filePath().baseName()
            : (m_activeMob ? m_activeMob->filePath().baseName() : QString());
        metadata.contentFingerprint = sceneSourceFingerprint();
        metadata.benchmarkMode = qApp->property("renderBenchmark").toBool();
        metadata.vsyncEnabled = metadata.benchmarkMode
            ? false : qApp->property("renderConfiguredVSync").toBool();
        const COptInt* fpsCapOption = m_pSettings
            ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderFpsCap")) : nullptr;
        metadata.frameRateCap = metadata.benchmarkMode
            ? 0 : (fpsCapOption ? qBound(0, fpsCapOption->value(), 1000) : 0);
        metadata.animationEnabled = animationEnabled;
        metadata.landEnabled = m_bDrawLand;
        metadata.waterEnabled = m_bDrawWater;
        metadata.logicEnabled = drawLogic;
        metadata.helpEnabled = drawHelp;
        metadata.frustumCullingEnabled = cullingEnabled;
        metadata.shadowQuality = shadowQuality;
        metadata.shadowAtlasResolution = m_shadowResolution;
        performance.setFrameMetadata(metadata);
    }

    const auto visibleNodes = [this, &frustum, cullingEnabled, &performance](CMob* mob)
    {
        SSpatialQueryStats stats;
        const QVector<CNode*> result = spatiallyVisibleNodes(
            mob, cullingEnabled ? &frustum : nullptr, &stats);
        if(performance.collecting())
        {
            performance.counters().nodesVisited += stats.entriesTested;
            const QSharedPointer<CSpatialGrid<CNode*>> grid = m_spatialIndices.value(mob);
            if(grid)
                performance.counters().nodesCulled += qMax(0, grid->entryCount() - result.size());
            performance.counters().spatialCellsTested += stats.cellsTested;
            performance.counters().spatialCellsVisible += stats.cellsVisible;
            performance.counters().spatialCandidates += stats.candidates;
            performance.counters().spatialOverflowEntries += m_spatialOverflowNodes.value(mob).size();
        }
        return result;
    };
    if(environmentEnabled && skyEnabled && !m_caveEnvironment)
        drawSkybox(camMatrix, environment);
    logOpenGlErrors(QStringLiteral("draw/sky"));
    // Set modelview-projection matrix

    // Bind shader pipeline for use
    if (!m_landProgram.bind())
        close();

    // Set modelview-projection matrix
    //m_landProgram.setUniformValue("u_watcherPos", m_cam->pos());
    m_landProgram.setUniformValue("u_projMmatrix", m_projection);
    m_landProgram.setUniformValue("u_viewMmatrix", camMatrix);
    applyEnvironmentUniforms(m_landProgram, environment, sceneLights,
                             lightingMode, fogEnabled);
//    m_landProgram.setUniformValue("u_color", QVector4D(0.0, 1.0, 0.0, 1.0));
//    m_landProgram.setUniformValue("u_lightPosition", QVector4D(0.0, 0.0, 2.0, 1.0));
//    m_landProgram.setUniformValue("u_lightPower", 5.0f);
//    m_landProgram.setUniformValue("u_lightColor", QVector4D(1.0, 1.0, 1.0, 1.0));
//    m_landProgram.setUniformValue("u_highlight", false);
    // Bind shader pipeline for use
    if (!m_program.bind())
        close();

    m_program.setUniformValue("u_projMmatrix", m_projection);
    m_program.setUniformValue("u_viewMmatrix", camMatrix);
    const QVector4D activeHighlight(0.05f, 0.78f, 0.93f, 0.0f);
    const QVector4D lockedHighlight(1.0f, 0.55f, 0.05f, 0.0f);
    m_program.setUniformValue("u_highlightColor", activeHighlight);
    m_program.setUniformValue("qt_Texture0", 0);
    applyEnvironmentUniforms(m_program, environment, sceneLights,
                             lightingMode, fogEnabled);
    applyShadowUniforms(m_program, false);
    RenderQueueContext& renderQueue = RenderQueueContext::instance();
    renderQueue.begin(&m_program, cameraPosition);
    bool terrainDrawn = false;
    const auto drawTerrainColor = [&](bool withShadows)
    {
        if(!m_landProgram.bind())
            close();
        m_landProgram.setUniformValue("u_projMmatrix", m_projection);
        m_landProgram.setUniformValue("u_viewMmatrix", camMatrix);
        applyEnvironmentUniforms(m_landProgram, environment, sceneLights,
                                 lightingMode, fogEnabled);
        applyShadowUniforms(m_landProgram, withShadows, true, true);
        if(m_landscapeVisible && m_pLand && m_pLand->isMprLoad() && m_bDrawLand)
            m_pLand->draw(&m_landProgram, cullingEnabled ? &frustum : nullptr);
        if(m_landscapeVisible && m_pLand && m_pLand->isMprLoad() && m_bPreviewTile)
            drawTilePreview(&m_landProgram);
        if(m_landscapeVisible && m_pLand && m_pLand->isMprLoad()
           && m_bDrawLand && m_tileGridVisible)
        {
            m_landProgram.setUniformValue("u_solidColorEnabled", true);
            m_landProgram.setUniformValue("u_solidColor",
                                          QVector4D(0.05f, 0.9f, 1.0f, 1.0f));
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_POLYGON_OFFSET_LINE);
            glPolygonOffset(-1.0f, -1.0f);
            glLineWidth(1.25f);
            m_pLand->drawTileGrid(&m_landProgram);
            glLineWidth(1.0f);
            glDisable(GL_POLYGON_OFFSET_LINE);
            glDepthFunc(GL_LESS);
            m_landProgram.setUniformValue("u_solidColorEnabled", false);
        }
        logOpenGlErrors(QStringLiteral("draw/terrain"));
        terrainDrawn = true;
        m_program.bind();
        m_program.setUniformValue("u_projMmatrix", m_projection);
        m_program.setUniformValue("u_viewMmatrix", camMatrix);
        m_program.setUniformValue("qt_Texture0", 0);
        applyEnvironmentUniforms(m_program, environment, sceneLights,
                                 lightingMode, fogEnabled);
        // Stock projected shadows use selected receivers. Ordinary static
        // meshes do not receive their own broad static map; this removes the
        // camera-dependent fence/roof acne absent from the game.
        applyShadowUniforms(m_program, withShadows, false, true);
    };

    if(m_activeMob != nullptr)
    {
        QSet<CNode*> colorSubmittedNodes;
        for(auto& mob: m_aMob) //TODO: draw inactive mobs with custom color|alfa or something
        {
            if(!isMobVisible(mob))
                continue;
            if(mob->mobName() == m_activeMob->mobName())
                continue; //skip drawing active mob

            const QVector<CNode*> candidates = visibleNodes(mob);
            for(CNode* node : candidates)
            {
                // Selected nodes from every visible MOB are rendered in the
                // final depth pass below, including locked MOBs.
                if(node->nodeState() == ENodeState::eSelect)
                    continue;
                node->drawObjectVisual(false, &m_program);
                colorSubmittedNodes.insert(node);
                if(performance.collecting())
                    ++performance.counters().nodesSubmitted;
                if(!traverseHelpers && node->nodeState() == ENodeState::eSelect)
                {
                    node->drawLogicVisual(false, &m_program);
                    if(performance.collecting())
                        ++performance.counters().logicSubmissions;
                }
            }
            if(traverseHelpers)
                for(CNode* node : mob->nodes())
                {
                    node->drawLogicVisual(false, &m_program);
                    if(performance.collecting())
                        ++performance.counters().logicSubmissions;
                }
        }

        // Draw the active MOB in two passes so a selected duplicate remains
        // visible when it occupies exactly the same depth as its source.
        const QVector<CNode*> activeCandidates = visibleNodes(m_activeMob);
        for(CNode* node : activeCandidates)
        {
            if(isMobVisible(m_activeMob) && node->nodeState() != ENodeState::eSelect)
            {
                node->drawObjectVisual(true, &m_program);
                colorSubmittedNodes.insert(node);
                if(performance.collecting())
                    ++performance.counters().nodesSubmitted;
            }
        }
        if(traverseHelpers)
            for(CNode* node : m_activeMob->nodes())
                if(node->nodeState() != ENodeState::eSelect)
                {
                    node->drawLogicVisual(true, &m_program);
                    if(performance.collecting())
                        ++performance.counters().logicSubmissions;
                }
        if(environmentEnabled && shadowsEnabled)
        {
            // A caster outside the color frustum can still project a visible
            // shadow. Collect that geometry for the shadow pass only.
            renderQueue.setShadowOnlyCollection(true);
            const QVector3D shadowSweep = environment.sunDirection.normalized()
                * maximumShadowDistance;
            for(CMob* mob : m_aMob)
            {
                if(!isMobVisible(mob))
                    continue;
                if(m_spatialIndexDirty)
                    rebuildSpatialIndices();
                const QSharedPointer<CSpatialGrid<CNode*>> grid
                    = m_spatialIndices.value(mob);
                SSpatialQueryStats shadowStats;
                const QVector<CNode*> shadowCandidates = grid
                    ? grid->querySwept(cullingEnabled ? &frustum : nullptr,
                                       shadowSweep, &shadowStats)
                    : QVector<CNode*>();
                if(performance.collecting())
                {
                    performance.counters().spatialCellsTested += shadowStats.cellsTested;
                    performance.counters().spatialCellsVisible += shadowStats.cellsVisible;
                    performance.counters().spatialCandidates += shadowStats.candidates;
                }
                const QList<CNode*> candidates = grid
                    ? shadowCandidates.toList() : mob->nodes();
                for(CNode* node : candidates)
                {
                    if(!node || colorSubmittedNodes.contains(node)
                            || node->isMarkDeleted()
                            || node->nodeState() == ENodeState::eHidden
                            || !node->castsRenderShadow())
                        continue;
                    CBox bounds = node->getBBox();
                    if(!bounds.isInit())
                        continue;
                    const QVector3D center = bounds.center() + node->drawPosition();
                    const float radius = bounds.radius();
                    if(!qIsFinite(center.x()) || !qIsFinite(center.y())
                            || !qIsFinite(center.z()) || !qIsFinite(radius)
                            || (center - cameraPosition).length()
                                > maximumShadowDistance + radius)
                        continue;
                    node->drawObjectVisual(mob == m_activeMob, &m_program);
                }
            }
            renderQueue.setShadowOnlyCollection(false);
        }
        const bool shadowReady = environmentEnabled && shadowsEnabled
            && renderShadowMap(cameraPosition, cameraPivot, environment,
                               renderQueue, maximumShadowDistance);
        drawTerrainColor(shadowReady);
        renderQueue.flush();
        logOpenGlErrors(QStringLiteral("draw/object-queue"));
        ei::gl::DepthFunc(GL_LEQUAL);
        for(CMob* mob : m_aMob)
            if(isMobVisible(mob))
                for(CNode* node : mob->nodes())
                    if(node->nodeState() == ENodeState::eSelect)
                    {
                        m_program.setUniformValue("u_highlightColor",
                            mob == m_activeMob ? activeHighlight : lockedHighlight);
                        node->drawObjectVisual(mob == m_activeMob, &m_program);
                        renderQueue.flush();
                        node->drawLogicVisual(mob == m_activeMob, &m_program);
                        if(performance.collecting())
                        {
                            ++performance.counters().nodesSubmitted;
                            ++performance.counters().logicSubmissions;
                        }
                    }
        m_program.setUniformValue("u_highlightColor", activeHighlight);
        renderQueue.flush();
        ei::gl::DepthFunc(GL_LESS);
    }

    if(!terrainDrawn)
        drawTerrainColor(false);

    renderQueue.end();
    if(performance.collecting())
    {
        performance.counters().sharedMeshes = quint64(CPart::sharedMeshCount());
        performance.counters().gpuMemoryEstimateBytes = CPart::sharedMeshBytes();
        const SAnimationPerformanceCounters& animation = AnimationFrameContext::instance().counters();
        performance.counters().animationPoseEvaluations = animation.poseEvaluations;
        performance.counters().animationPoseCacheHits = animation.poseCacheHits;
        performance.counters().animationComponentsEvaluated = animation.componentsEvaluated;
    }

    if (m_landscapeVisible && m_pLand && m_pLand->isMprLoad() && m_bDrawWater)
    {
        //turn to landshader again
        if (!m_landProgram.bind())
            close();

        m_landProgram.setUniformValue("transparency", 0.3f);
        const CWorldSet* overlayWorldSet = m_environmentMob
            ? &m_environmentMob->worldSet() : nullptr;
        const QVector3D overlayWindDirection = overlayWorldSet
            ? util::vec3FromString(overlayWorldSet->data(eWsTypeWindDir))
            : QVector3D();
        const float overlayWindStrength = overlayWorldSet
            ? overlayWorldSet->data(eWsTypeWindStr).toFloat() : 0.0f;
        const bool landscapeEditing = m_landscapeTools
            && m_landscapeTools->editingEnabled();
        m_landProgram.setUniformValue("u_overlayMotionEnabled", !landscapeEditing);
        m_landProgram.setUniformValue("u_overlayTimeSeconds",
            m_overlayAnimationTimer.isValid()
                ? float(m_overlayAnimationTimer.elapsed()) / 1000.0f : 0.0f);
        m_landProgram.setUniformValue("u_overlayWindDirection", overlayWindDirection);
        m_landProgram.setUniformValue("u_overlayWindStrength", overlayWindStrength);
        applyEnvironmentUniforms(m_landProgram, environment, sceneLights,
                                 lightingMode, fogEnabled);
        applyShadowUniforms(m_landProgram, environmentEnabled && shadowsEnabled,
                            true, true);
        m_pLand->drawWater(&m_landProgram, cullingEnabled ? &frustum : nullptr);
        logOpenGlErrors(QStringLiteral("draw/water"));
        m_landProgram.setUniformValue("u_overlayMotionEnabled", false);
        m_landProgram.setUniformValue("transparency", 0.0f);
    }

    drawParticles(camMatrix, cullingEnabled ? &frustum : nullptr);
    logOpenGlErrors(QStringLiteral("draw/particles"));


    //draw selection frame using m_program shader
    if (!m_program.bind())
        close();

    if (m_pLand && m_pLand->isMprLoad() && !m_aMob.isEmpty())
    {
        QMatrix4x4 mtrx;
        mtrx.setToIdentity();
        m_program.setUniformValue("u_projMmatrix", mtrx);
        m_program.setUniformValue("u_viewMmatrix", mtrx);
        m_selectFrame->draw(&m_program);
    }
}

void CView::loadLandscape(const QFileInfo& filePath)
{
    if(m_pLand->isMprLoad())
    {
        QMessageBox::warning(this, "Warning","Landscape already loaded. Please close before opening new zone (*mpr)");
        //LOG_FATAL("ahtung"); //test logging critial error
        return;
    }

    const QString mapLocalAtlas = filePath.dir().filePath(
        filePath.completeBaseName() + QStringLiteral("-atlas.res"));
    CTextureList::getInstance()->setMapLocalTerrainArchive(mapLocalAtlas);
    QElapsedTimer totalLoadTimer;
    QElapsedTimer phaseTimer;
    totalLoadTimer.start();
    phaseTimer.start();
    ei::setCrashContext("mpr/open/dispatch:" + filePath.fileName());
    m_pLand->readMap(filePath);
    const qint64 rendererLoadMs = phaseTimer.elapsed();
    if(!m_pLand->isMprLoad())
    {
        CTextureList::getInstance()->setMapLocalTerrainArchive(QString());
        ei::log(eLogWarning, "Landscape failed to load: " + filePath.absoluteFilePath());
        return;
    }
    phaseTimer.restart();
    CValidationReport canonicalReport;
    m_terrainEditAvailable = m_mprDocument.load(filePath, &canonicalReport)
        && !canonicalReport.hasErrors()
        && m_terrainEdit.load(m_mprDocument, &canonicalReport)
        && !canonicalReport.hasErrors();
    const qint64 canonicalLoadMs = phaseTimer.elapsed();
    if(!m_terrainEditAvailable)
    {
        ei::log(eLogWarning, "Canonical terrain editing disabled for this MPR:\n"
            + canonicalReport.toText());
    }
    else
    {
        ei::log(eLogInfo, QString("Canonical terrain preview ready: %1 x %2 vertices")
            .arg(m_terrainEdit.vertexCountX()).arg(m_terrainEdit.vertexCountY()));
    }
    for(CMob* mob : m_aMob)
        mob->reprojectForCurrentTerrain();
    m_landscapeVisible = true;
    m_overlayAnimationTimer.restart();
    armOpenGlDiagnostics(QStringLiteral("landscape loaded: %1")
        .arg(filePath.fileName()), 6);
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMpr, filePath.baseName());
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag, m_pLand->isDirty() ? "*" : "");
    m_lastModifiedLand = filePath.lastModified();
    connect(m_pTileForm, SIGNAL(applyChangesSignal()), this, SLOT(onMapMaterialUpdate()));
    COptInt* pOpt = dynamic_cast<COptInt*>(settings()->opt("landCheckTime"));
    if (pOpt && pOpt->value() != 0)
    {
        m_mprModifyTimer->setInterval(pOpt->value());
        m_mprModifyTimer->start();
    }
    m_pPreviewTile.reset(new CPreviewTile());
    phaseTimer.restart();
    updateTileForm(true);
    const qint64 tileTableMs = phaseTimer.elapsed();
    phaseTimer.restart();
    rebuildTerrainAutoCatalog();
    const qint64 materialCatalogMs = phaseTimer.elapsed();
    ei::log(eLogInfo, QString(
        "MPR editor load phases file:%1 rendererAtlasAndSectorsMs:%2 canonicalMs:%3 tileTableMs:%4 materialCatalogMs:%5 totalMs:%6")
        .arg(filePath.fileName()).arg(rendererLoadMs).arg(canonicalLoadMs)
        .arg(tileTableMs).arg(materialCatalogMs).arg(totalLoadTimer.elapsed()));
    refreshAiGraphPreviewSource();
    refreshMprSettingsDock();
    emit selectionStateChanged(nSelectedNodes() > 0);
    if(m_landscapeTools && m_landscapeTools->editingEnabled())
        syncLandscapeToolState();
    emit loadedMapStateChanged();
}

void CView::unloadLand(bool promptForSave)
{
    if(!m_pLand->isMprLoad())
        return;

    if(promptForSave && m_pLand->isDirty())
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Unload MPR", m_pLand->filePath().fileName() + " has unsaved changes.\nDo you want to save changes?", QMessageBox::Save|QMessageBox::No|QMessageBox::Cancel);
        if(reply == QMessageBox::Save)
        {
            if(!saveLandscapeDocument(m_pLand->filePath()))
                return;
        }
        else if(reply == QMessageBox::Cancel)
            return;

    }

    m_mprModifyTimer->stop();
    m_overlayAnimationTimer.invalidate();
    m_terrainEditAvailable = false;
    finishSculptStroke();
    m_workingTerrainAtlas.reset();
    if(m_landscapeTools)
        m_landscapeTools->setGeneratedAtlasTileCount(0);
    m_authoredTerrainTileImages.clear();
    m_workingAtlasProtectedSlots.clear();
    m_terrainEdit = CTerrainEditModel();
    rebuildTerrainAutoCatalog();
    m_terrainSemanticSeed = 0;
    m_mprDocument = CMprDocument();
    if(isValid())
        makeCurrent();
    m_pLand->unloadMpr();
    CTextureList::getInstance()->setMapLocalTerrainArchive(QString());
    if(isValid())
        doneCurrent();
    refreshMprSettingsDock();
    refreshAiGraphPreviewSource();
    m_caveEnvironment = false;
    for(CMob* mob : m_aMob)
        mob->reprojectForCurrentTerrain();
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMpr, "");
    m_pPreviewTile.clear();
    emit selectionStateChanged(nSelectedNodes() > 0);
    ei::log(eLogInfo, "Landscape unloaded");
    m_landscapeVisible = true;
    emit loadedMapStateChanged();
}

void CView::saveLand()
{
    if(m_pLand->isMprLoad())
    {
        if(saveLandscapeDocument(m_pLand->filePath()))
            emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag, "");
    }
}

void CView::saveLandAs()
{
    if(!m_pLand->isMprLoad())
        return;

    const QFileInfo fileName = QFileDialog::getSaveFileName(
        this, "Save " + m_pLand->filePath().fileName() + " as... ",
        m_pLand->filePath().absoluteFilePath(), tr("Landscape (*.mpr);;)"));
    if(fileName.filePath().isEmpty())
        return;

    if(m_pLand->isDirty())
    {
        const QString sourcePath = QDir::cleanPath(m_pLand->filePath().absoluteFilePath());
        const QString destinationPath = QDir::cleanPath(fileName.absoluteFilePath());
        const bool samePath = QString::compare(sourcePath, destinationPath,
                                               Qt::CaseInsensitive) == 0;
        saveChangedLandscapeDocument(fileName, samePath);
        return;
    }
    //QFileInfo fileName("c:\\konst\\Проклятые Земли (Дополнение)\\Mods\\ferneo_mod\\Maps\\zone1gTest.mpr");

    if(saveLandscapeDocument(fileName))
    {
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMpr, fileName.baseName());
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag, "");
    }
}

bool CView::saveLandscapeDocument(const QFileInfo& destination)
{
    if(m_pLand->isDirty())
    {
        const QString sourcePath = QDir::cleanPath(m_pLand->filePath().absoluteFilePath());
        const QString destinationPath = QDir::cleanPath(destination.absoluteFilePath());
        const bool samePath = QString::compare(sourcePath, destinationPath,
                                               Qt::CaseInsensitive) == 0;
        return saveChangedLandscapeDocument(destination, samePath);
    }

    QProgressDialog progress(tr("Preparing safe MPR save..."), QString(), 0, 4, this);
    progress.setWindowTitle(tr("Saving MPR"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(250);
    progress.setWindowFlags(progress.windowFlags() & ~Qt::WindowContextHelpButtonHint
                             & ~Qt::WindowCloseButtonHint);
    const auto updateProgress = [&progress](const QString& label, int value, int maximum)
    {
        progress.setMaximum(maximum);
        progress.setLabelText(label);
        progress.setValue(value);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    const bool saved = m_pLand->saveMapAs(destination, updateProgress);
    progress.setValue(progress.maximum());
    return saved;
}

bool CView::buildValidatedLandscapeCandidate(QByteArray* data,
                                              CValidationReport* report,
                                              QByteArray* terrainAtlasResource,
                                              int* terrainAtlasPages,
                                              int* terrainAtlasSlots)
{
    if(data)
        data->clear();
    if(terrainAtlasResource)
        terrainAtlasResource->clear();
    if(terrainAtlasPages)
        *terrainAtlasPages = 0;
    if(terrainAtlasSlots)
        *terrainAtlasSlots = 0;
    if(!data || !m_pLand->isMprLoad() || !m_terrainEditAvailable)
    {
        if(report)
            report->add(QStringLiteral("mpr.test.candidate.unavailable"),
                        EValidationSeverity::Error,
                        tr("Canonical terrain ownership is unavailable."));
        return false;
    }
    if(!m_pLand->isDirty())
    {
        *data = m_mprDocument.originalBytes();
        return !data->isEmpty();
    }

    finishSculptStroke();
    endTileBrushGroup();
    CMprEditTransaction transaction(tr("Stage current landscape for map test"));
    CValidationReport localReport;
    if(!m_terrainEdit.appendSectorEdits(&transaction, &localReport)
       || !m_mprDocument.appendMaxZEdit(m_terrainEdit.maxZ(), &transaction, &localReport)
       || localReport.hasErrors() || transaction.isEmpty())
    {
        if(report)
            for(const SValidationFinding& finding : localReport.findings())
                report->add(finding.code, finding.severity,
                            finding.message, finding.location);
        return false;
    }

    QTemporaryDir validationDirectory;
    if(!validationDirectory.isValid())
    {
        if(report)
            report->add(QStringLiteral("mpr.test.stage.temp"),
                        EValidationSeverity::Error,
                        tr("Could not create the isolated MPR validation directory."));
        return false;
    }
    const QFileInfo validationPath(validationDirectory.filePath(
        QStringLiteral("test-current-map.mpr")));
    const SStagedMprTransactionValidation validation =
        validateStagedMprTransaction(m_mprDocument, transaction, validationPath);
    if(!validation.success)
    {
        if(report)
            report->add(QStringLiteral("mpr.test.stage.validation"),
                        EValidationSeverity::Error, validation.error);
        return false;
    }
    *data = validation.candidateBytes;
    if(terrainAtlasResource && m_workingTerrainAtlas
       && m_workingTerrainAtlas->activeTextureCount()
              > int(m_mprDocument.mapInfo().textureCount))
    {
        CMprDocument stagedDocument;
        CValidationReport stagedReport;
        if(!stagedDocument.loadBytes(*data, QStringLiteral("test-current-map.mpr"),
                                     &stagedReport)
           || stagedReport.hasErrors())
        {
            if(report)
                report->add(QStringLiteral("mpr.test.atlas.reopen"),
                            EValidationSeverity::Error,
                            tr("The edited MPR could not be reopened before staging its working atlas."));
            return false;
        }
        QVector<QByteArray> authoredPages;
        const QStringList names = ei::terrainTextureResourceNames(
            m_mprDocument.mapInfo().innerNamespace,
            int(m_mprDocument.mapInfo().textureCount));
        for(const QString& name : names)
        {
            QByteArray payload;
            QString sourcePath;
            if(!CTextureList::getInstance()->terrainMmpPayload(
                   name, &payload, &sourcePath))
            {
                if(report)
                    report->add(QStringLiteral("mpr.test.atlas.source"),
                                EValidationSeverity::Error,
                                tr("The authored terrain page '%1' could not be read for isolated test staging.")
                                    .arg(name));
                return false;
            }
            authoredPages.append(payload);
        }
        const STerrainPortableAtlasPackage package =
            CTerrainVirtualAtlas::buildWorkingPreviewPackage(
                stagedDocument, m_workingTerrainAtlas->activeTileImages(),
                m_workingTerrainAtlas->activeTileTypes(),
                authoredPages, report);
        if(!package.success)
        {
            if(report)
                report->add(QStringLiteral("mpr.test.atlas.package"),
                            EValidationSeverity::Error, package.error);
            return false;
        }
        *data = package.mprBytes;
        *terrainAtlasResource = package.resourceArchiveBytes;
        if(terrainAtlasPages)
            *terrainAtlasPages = package.resourceMembers.size();
        if(terrainAtlasSlots)
            *terrainAtlasSlots = m_workingTerrainAtlas->activeTileImages().size();
        ei::log(eLogInfo, QString(
            "Test Current Map staged working terrain atlas pages:%1 slots:%2 authoredPagesPreserved:%3 generatedExactTiles:%4")
            .arg(package.resourceMembers.size())
            .arg(m_workingTerrainAtlas->activeTileImages().size())
            .arg(authoredPages.size())
            .arg(m_workingTerrainAtlas->generatedTileCount()));
    }
    ei::log(eLogInfo, QString("Test Current Map staged unsaved MPR candidate members:%1 sha256:%2")
        .arg(validation.changedMembers.join(QStringLiteral(",")), validation.sha256));
    ei::log(eLogInfo, QString("Test Current Map staged MPR semantic diff:\n%1")
        .arg(validation.semanticDiff.isEmpty() ? QStringLiteral("(none)")
                                               : validation.semanticDiff));
    return true;
}

bool CView::saveChangedLandscapeDocument(const QFileInfo& destination,
                                         bool adoptAsOpenBaseline)
{
    if(!m_terrainEditAvailable)
    {
        QMessageBox::critical(this, tr("Lossless MPR save unavailable"),
            tr("Canonical terrain ownership is unavailable for this map. No destination was changed."));
        return false;
    }

    QProgressDialog progress(tr("Preparing canonical terrain patches..."), QString(),
                             0, 4, this);
    progress.setWindowTitle(tr("Saving MPR"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.setWindowFlags(progress.windowFlags() & ~Qt::WindowContextHelpButtonHint
                             & ~Qt::WindowCloseButtonHint);
    const auto updateProgress = [&progress](const QString& label, int value)
    {
        progress.setLabelText(label);
        progress.setValue(value);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    updateProgress(tr("Preparing canonical terrain patches..."), 0);

    finishSculptStroke();
    endTileBrushGroup();
    const QString sourcePath = QDir::cleanPath(m_pLand->filePath().absoluteFilePath());
    const QString destinationPath = QDir::cleanPath(destination.absoluteFilePath());
    CMprEditTransaction transaction(adoptAsOpenBaseline
        ? tr("Save canonical landscape")
        : tr("Save canonical landscape copy"));
    CValidationReport transactionReport;
    if(!m_terrainEdit.appendSectorEdits(&transaction, &transactionReport)
       || !m_mprDocument.appendMaxZEdit(m_terrainEdit.maxZ(), &transaction,
                                        &transactionReport)
       || transactionReport.hasErrors() || transaction.isEmpty())
    {
        ei::log(eLogFatal, QString("Refusing changed MPR save because the canonical transaction failed path:%1 findings:%2")
            .arg(destinationPath, transactionReport.toText().trimmed()));
        QMessageBox::critical(this, tr("Lossless MPR save refused"),
            tr("Endurance could not represent the complete terrain edit as provenance-checked sector patches. "
               "No destination was changed.\n\n%1").arg(transactionReport.toText().trimmed()));
        return false;
    }
    updateProgress(tr("Validating staged MPR candidate..."), 1);

    const SStagedMprTransactionValidation preview =
        validateStagedMprTransaction(m_mprDocument, transaction, destination);
    if(!preview.success)
    {
        ei::log(eLogFatal, QString("Refusing changed MPR save because preview validation failed path:%1 error:%2")
            .arg(destinationPath, preview.error));
        QMessageBox::critical(this, tr("Staged MPR validation failed"),
            tr("The temporary candidate failed validation. No destination was changed.\n\n%1")
                .arg(preview.error));
        return false;
    }
    updateProgress(tr("Creating backup and committing atomically..."), 2);

    SMprSaveCommitRequest request;
    request.source = m_pLand->filePath();
    request.destination = destination;
    const SMprSaveCommitResult result =
        commitStagedMprTransaction(m_mprDocument, transaction, request);
    if(!result.success)
    {
        ei::log(eLogFatal, QString("Changed MPR save failed phase:%1 path:%2 error:%3 backup:%4")
            .arg(result.phase, destinationPath, result.error,
                 result.backupPath.isEmpty() ? QStringLiteral("not-created")
                                             : result.backupPath));
        QMessageBox::critical(this, tr("Changed MPR save failed"),
            tr("The changed MPR save did not complete.\n\nPhase: %1\n%2\n\nBackup: %3")
                .arg(result.phase, result.error,
                     result.backupPath.isEmpty() ? tr("not created")
                                                 : result.backupPath));
        return false;
    }

    updateProgress(tr("Reopening and adopting validated terrain..."), 3);
    if(adoptAsOpenBaseline && !adoptCommittedLandscapeBaseline(destination, result))
        return false;

    ei::log(eLogInfo, QString("Changed MPR semantic diff destination:%1\n%2")
        .arg(destinationPath,
             result.semanticDiff.isEmpty() ? QStringLiteral("(none)")
                                           : result.semanticDiff));

    ei::log(eLogInfo, QString("Changed MPR committed source:%1 destination:%2 members:%3 sha256:%4 backup:%5 adopted:%6")
        .arg(sourcePath, destinationPath, result.changedMembers.join(","),
             result.candidateSha256,
             result.backupPath.isEmpty() ? QStringLiteral("not-required")
                                         : result.backupPath,
             adoptAsOpenBaseline ? QStringLiteral("yes") : QStringLiteral("no")));
    if(adoptAsOpenBaseline)
    {
        emit updateMsg(tr("Validated MPR saved: %1").arg(destinationPath));
    }
    else
    {
        emit updateMsg(tr("Validated changed MPR copy written: %1").arg(destinationPath));
    }
    progress.setValue(progress.maximum());
    return true;
}

bool CView::adoptCommittedLandscapeBaseline(const QFileInfo& destination,
                                            const SMprSaveCommitResult& commit)
{
    CMprDocument reopenedDocument;
    CValidationReport report;
    if(!reopenedDocument.load(destination, &report) || report.hasErrors())
    {
        QMessageBox::critical(this, tr("MPR saved; reopen required"),
            tr("The validated MPR was committed, but the editor could not rebind the open document. "
               "The backup is intact. Reopen the MPR before making further edits.\n\n%1")
                .arg(report.toText().trimmed()));
        return false;
    }
    const QString reopenedSha256 = QString::fromLatin1(QCryptographicHash::hash(
        reopenedDocument.originalBytes(), QCryptographicHash::Sha256).toHex());
    if(reopenedSha256.compare(commit.candidateSha256, Qt::CaseInsensitive) != 0)
    {
        QMessageBox::critical(this, tr("MPR saved; reopen required"),
            tr("The committed file hash changed before the editor could adopt it. "
               "The backup is intact. Reopen the MPR before making further edits."));
        return false;
    }

    CTerrainEditModel reopenedTerrain;
    if(!reopenedTerrain.load(reopenedDocument, &report) || report.hasErrors()
       || !m_pLand->adoptCanonicalSave(destination,
                                      reopenedDocument.originalBytes()))
    {
        QMessageBox::critical(this, tr("MPR saved; reopen required"),
            tr("The validated MPR was committed, but its canonical terrain baseline could not be adopted. "
               "The backup is intact. Reopen the MPR before making further edits.\n\n%1")
                .arg(report.toText().trimmed()));
        return false;
    }

    m_mprDocument = reopenedDocument;
    m_terrainEdit = reopenedTerrain;
    m_terrainEditAvailable = true;
    m_lastModifiedLand = QFileInfo(destination.absoluteFilePath()).lastModified();
    rebuildTerrainAutoCatalog();
    m_pLand->setDirty(false);
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMpr,
                              destination.baseName());
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag, "");
    return true;
}

int CView::select(const SSelect &selectParam, bool bAddToSelect)
{
    if(m_aMob.isEmpty())
        return 0;

    const QList<CNode*> selectionBefore = selectionSnapshot();

    CNode* pNode = nullptr;
    foreach (pNode, m_activeMob->nodes())
    {
        switch (selectParam.type) {
        case eSelectType_Id_range:
        {
            uint id_min = selectParam.param1.toUInt();
            uint id_max = selectParam.param2.toUInt();
            // set equal value if only one defined
            if(id_min!=0 && id_max==0)
                id_max = id_min;
            else if(id_max!=0 && id_min==0)
                id_min = id_max;

            if (id_max < id_min)
            {
                uint temp = id_min;
                id_min = id_max;
                id_max = temp;
            }
            if (pNode->mapId() >= id_min && pNode->mapId() <=id_max)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
                pNode->setState(eDraw);
            break;
        }
        case eSelectType_Map_name:
        {
            auto pObj = dynamic_cast<CObjectBase*>(pNode);
            if (!pObj)
                break;
            bool bAppropriate = selectParam.exactMatch ? pNode->mapName() == selectParam.param1 : pNode->mapName().toLower().contains(selectParam.param1.toLower());
            if (bAppropriate)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
                pNode->setState(eDraw);
            break;
        }
        case eSelectType_Texture_name:
        {
            if (!(pNode->nodeType() & ENodeType::eWorldObject))
                break;
            bool bAppropriate = selectParam.exactMatch ? pNode->textureName() == selectParam.param1 : pNode->textureName().toLower().contains(selectParam.param1.toLower());
            if (bAppropriate)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
                pNode->setState(eDraw);
            break;
        }
        case eSelectType_Model_name:
        {
            if (!(pNode->nodeType() & ENodeType::eWorldObject))
                break;

            bool bAppropriate = selectParam.exactMatch ? pNode->modelName() == selectParam.param1 : pNode->modelName().toLower().contains(selectParam.param1.toLower());
            if (bAppropriate)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
                pNode->setState(eDraw);
            break;
        }
        case eSelectType_Position_circle:
        {
            //TODO. create 'brush' for selecti by circle
            break;
        }
        case eSelectType_Position_rectangle:
        {
            //TODO. create 'brush' for selecti by circle
            break;
        }
        case eSelectType_Diplomacy_group:
        {
            auto pObj = dynamic_cast<CWorldObj*>(pNode);
            if (!pObj)
            {//deselect if we are trying to object diplomacy group with selected particles(and others)
                if(!bAddToSelect && (pNode->nodeState() & eSelect))
                    pNode->setState(eDraw);
                break;
            }

            int group_min = selectParam.param1.toUInt();
            int group_max = selectParam.param2.toUInt();
            if (group_max < group_min)
            {
                uint temp = group_min;
                group_min = group_max;
                group_max = temp;
            }
            if (pObj->dipGroup() >= group_min && pObj->dipGroup() <= group_max )
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
            {
                pNode->setState(eDraw);
            }
            break;
        }
        case eSelectType_Database_name:
        {
            auto pObj = dynamic_cast<CUnit*>(pNode);
            if (!pObj)
            { //deselect if we are trying to find unit database name with selected world object(and others)
                if(!bAddToSelect && (pNode->nodeState() & eSelect))
                    pNode->setState(eDraw);
                break;
            }

            bool bAppropriate = selectParam.exactMatch ? pObj->databaseName() == selectParam.param1 : pObj->databaseName().toLower().contains(selectParam.param1.toLower());
            if (bAppropriate)
            {
                pNode->setState(eSelect);
            }
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
            {
                pNode->setState(eDraw);
            }
            break;
        }
        case eSelectType_Template:
        {
            auto pWo = dynamic_cast<CWorldObj*>(pNode);
            if(nullptr == pWo)
                break;

            QSharedPointer<IPropertyBase> templ;
            pWo->getParam(templ, eObjParam_PARENT_TEMPLATE);
            bool bAppropriate = selectParam.exactMatch ? templ->toString() == selectParam.param1 : templ->toString().toLower().contains(selectParam.param1.toLower());
            if(bAppropriate)
                pNode->setState(ENodeState::eSelect);
            else if (!bAddToSelect && (pNode->nodeState() & eSelect)) //deselect
            {
                pNode->setState(eDraw);
            }

            break;
        }
        case eSelectType_all:
        {
            if(selectParam.param1.toLower()=="logic")
            {
                if(m_activeMob->logicNodes().contains(pNode))
                    pNode->setState(eSelect);
                else
                    pNode->setState(eDraw);
            }
            else
                pNode->setState(eSelect);
            break;
        }
        default:
            break;
        }
    }
    recordSelectionChange(selectionBefore);
    viewParameters();
    emit loadedMapStateChanged();
    return nSelectedNodes();
}

CMob *CView::mob(QString mobName)
{
    const QString wanted = QDir::cleanPath(mobName).toLower();
    for(CMob* pMob : m_aMob)
    {
        if(!pMob)
            continue;
        if(QDir::cleanPath(pMob->filePath().absoluteFilePath()).toLower() == wanted)
            return pMob;

        if(pMob->mobName().toLower() == mobName.toLower())
            return pMob;
    }
    return nullptr;
}

void CView::pickObject(QPoint mousePos, bool bAddToSelect)
{
    QRect rect(mousePos, mousePos);
    pickObject(rect, bAddToSelect);
    return;
}

QDebug operator<<(QDebug stream, const SColor& color)
{
    stream << '{' << color.rgb[0] << ',' << color.rgb[1] << ',' << color.rgb[2];
    if (color.hasAlpha)
        stream << ',' << color.rgb[3];
    stream << '}';
    return stream;
}

void CView::getColorFromRect(const QRect& rect, QVector<SColor>& aColor)
{
    //get colors from rectangle
    const int component = 3; //rgb
    //revert Oy for picking
    const QPoint topLeft (rect.left(), m_height - rect.bottom());
    const QPoint btmRight(rect.right(), m_height - rect.top());
    const QRect inversedRect(topLeft, btmRight);

    const int h = inversedRect.height() == 0 ? 1 : qAbs(inversedRect.height());
    const int w = inversedRect.width() == 0 ? 1 :qAbs(inversedRect.width());
    uint size = component * w * h;
    QByteArray* pixel = new QByteArray();
    pixel->resize(size);
    ei::gl::PixelStorei(GL_PACK_ALIGNMENT, 1); // glReadPixels use 4 component by default. for RGB must define pack alignment
    ei::gl::ReadPixels(inversedRect.left(), inversedRect.bottom(), w, h, GL_RGB, GL_UNSIGNED_BYTE, pixel->data());
    SColor color;
    for(int i(0); i<component * w * h; i+=3)
    {
        color = SColor(pixel->at(i), pixel->at(i+1), pixel->at(i+2));
        if (!color.isBlack() && !aColor.contains(color))
            aColor.append(SColor(color));
    }
    delete pixel;
}

void CView::changeCurrentMob(CMob *pMob)
{
    if(pMob != m_activeMob && m_pOp
       && operationRequiresCancellationForDocumentTransition(
           m_pOp->operationMethod()))
        cancelPendingOperation();
    m_activeMob = pMob;
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataActiveMob, nullptr == pMob ? "" : pMob->mobName());
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, (nullptr == pMob || !pMob->isDirty()) ? "" : "*");
    collectObjectTreeData();
    viewParameters();
    emit loadedMapStateChanged();
}

void CView::changeCurrentMob(QString mobName)
{
    for(auto& mob : m_aMob)
    {
        if(mob->mobName() == mobName)
        {
            changeCurrentMob(mob);
            break;
        }

    }
}

void CView::onParamChangeLogic(CNode *pNode, const QSharedPointer<IPropertyBase>& prop)
{
    if(!pNode)
        return;
    switch(pNode->nodeType())
    {
    case eMagicTrap:
    case eUnit:
    case ePatrolPoint:
    case eLookPoint:
    case eTrapActZone:
    case eTrapCastPoint:
    {
        CChangeLogicParam* pOp = new CChangeLogicParam(this, pNode, prop);
        QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
        m_pUndoStack->push(pOp);
        break;
    }
    default:
    {
        Q_ASSERT(false && "not implemented");
        break;
    }
    }
}

QString stringFromUnsignedChar(const unsigned char *str ){
    QString result = "";
    int lengthOfString = strlen( reinterpret_cast<const char*>(str) );

    // print string in reverse order
    QString s;
    for( int i = 0; i < lengthOfString; i++ ){
        s = QString( "%1" ).arg( str[i], 0, 16 );

        // account for single-digit hex values (always must serialize as two digits)
        if( s.length() == 1 )
            result.append( "0" );

        result.append( s );
    }

    return result;
}

void CView::armOpenGlDiagnostics(const QString& reason, int frameCount)
{
    m_openGlDiagnosticReason = reason;
    m_openGlDiagnosticFramesRemaining = qMax(m_openGlDiagnosticFramesRemaining,
                                               qMax(1, frameCount));
    ei::log(eLogInfo, QStringLiteral(
        "OpenGL diagnostics armed reason:%1 frames:%2")
        .arg(reason).arg(m_openGlDiagnosticFramesRemaining));
}

bool CView::logOpenGlErrors(const QString& stage)
{
    if(m_openGlDiagnosticFramesRemaining <= 0
            && !stage.startsWith(QStringLiteral("initialize/"))
            && !stage.startsWith(QStringLiteral("shader/")))
        return false;
    bool found = false;
    QStringList errors;
    for(int index = 0; index < 32; ++index)
    {
        const GLenum error = ei::gl::GetError();
        if(error == GL_NO_ERROR)
            break;
        found = true;
        errors.append(QStringLiteral("%1(%2)")
            .arg(openGlErrorName(error))
            .arg(QString::number(uint(error), 16)));
    }
    if(found)
        ei::log(eLogWarning, QStringLiteral("OpenGL diagnostic error stage:%1 errors:%2")
            .arg(stage, errors.join(QStringLiteral(","))));
    return found;
}

void CView::logOpenGlDiagnosticState(const QString& stage, bool samplePixel)
{
    if(m_openGlDiagnosticFramesRemaining <= 0
            && !stage.startsWith(QStringLiteral("initialize/")))
        return;

    // Attribute errors to the operation that preceded this checkpoint before
    // the diagnostic queries themselves can affect the GL error register.
    logOpenGlErrors(stage + QStringLiteral("/preceding-operation"));

    GLint viewport[4] = {0, 0, 0, 0};
    GLint framebuffer = 0;
    GLint program = 0;
    GLint arrayBuffer = 0;
    GLint elementBuffer = 0;
    GLint activeTexture = 0;
    GLint texture = 0;
    GLboolean colorMask[4] = {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE};
    GLfloat clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ei::gl::GetIntegerv(GL_VIEWPORT, viewport);
    ei::gl::GetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    ei::gl::GetIntegerv(GL_CURRENT_PROGRAM, &program);
    ei::gl::GetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
    ei::gl::GetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
    ei::gl::GetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    ei::gl::GetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    ei::gl::GetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    ei::gl::GetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
    GLenum framebufferStatus = 0;
    const QGLContext* legacyContext = QGLContext::currentContext();
    if(legacyContext)
    {
        typedef GLenum (APIENTRY *CheckFramebufferStatusProc)(GLenum);
        CheckFramebufferStatusProc checkFramebufferStatus =
            reinterpret_cast<CheckFramebufferStatusProc>(
                legacyContext->getProcAddress(
                    QStringLiteral("glCheckFramebufferStatus")));
        if(checkFramebufferStatus)
            framebufferStatus = checkFramebufferStatus(GL_FRAMEBUFFER);
    }

    QString pixelText = QStringLiteral("not-sampled");
    if(samplePixel && width() > 0 && height() > 0)
    {
        GLubyte pixel[4] = {0, 0, 0, 0};
        ei::gl::ReadPixels(width() / 2, height() / 2, 1, 1,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        pixelText = QStringLiteral("%1,%2,%3,%4")
            .arg(pixel[0]).arg(pixel[1]).arg(pixel[2]).arg(pixel[3]);
    }

    ei::log(eLogInfo, QStringLiteral(
        "OpenGL diagnostic state stage:%1 frame:%2 viewport:%3,%4,%5,%6 "
        "fbo:%7 fbo-status:0x%8 program:%9 array-buffer:%10 element-buffer:%11 "
        "active-texture:%12 texture2d:%13 depth:%14 blend:%15 color-mask:%16%17%18%19 "
        "clear:%20,%21,%22,%23 center-rgba:%24")
        .arg(stage).arg(m_openGlDiagnosticFrame)
        .arg(viewport[0]).arg(viewport[1]).arg(viewport[2]).arg(viewport[3])
        .arg(framebuffer).arg(QString::number(uint(framebufferStatus), 16))
        .arg(program).arg(arrayBuffer).arg(elementBuffer)
        .arg(activeTexture - GL_TEXTURE0).arg(texture)
        .arg(ei::gl::IsEnabled(GL_DEPTH_TEST) ? 1 : 0)
        .arg(ei::gl::IsEnabled(GL_BLEND) ? 1 : 0)
        .arg(colorMask[0] ? 1 : 0).arg(colorMask[1] ? 1 : 0)
        .arg(colorMask[2] ? 1 : 0).arg(colorMask[3] ? 1 : 0)
        .arg(clearColor[0], 0, 'f', 3).arg(clearColor[1], 0, 'f', 3)
        .arg(clearColor[2], 0, 'f', 3).arg(clearColor[3], 0, 'f', 3)
        .arg(pixelText));
    logOpenGlErrors(stage + QStringLiteral("/diagnostic-query"));
}

void CView::logShaderProgramDiagnostics(const QString& name,
                                        const QOpenGLShaderProgram& program,
                                        bool success)
{
    const QString shaderLog = program.log().trimmed();
    ei::log(success ? eLogInfo : eLogWarning, QStringLiteral(
        "OpenGL shader program:%1 success:%2 id:%3 log:%4")
        .arg(name).arg(success ? 1 : 0).arg(program.programId())
        .arg(shaderLog.isEmpty() ? QStringLiteral("<empty>") : shaderLog));
    logOpenGlErrors(QStringLiteral("shader/%1").arg(name));
}

void CView::logOpenGlData()
{
    const GLubyte * version = ei::gl::GetString(GL_VERSION);
    const GLubyte * vendor = ei::gl::GetString(GL_VENDOR);
    const GLubyte * renderer = ei::gl::GetString(GL_RENDERER);
    const QGLContext* legacyContext = QGLContext::currentContext();
    const QGLFormat legacyFormat = legacyContext
        ? legacyContext->format() : QGLFormat();
    ei::log(eLogInfo, QString("OpenGL module:%1 version:%2 vendor:%3 renderer:%4 swap-interval-request:%5 legacy-context:%6 legacy-valid:%7 legacy-format:%8.%9 profile:%10")
            .arg(QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES ? "GLES/ANGLE" : "Desktop")
            .arg(version ? reinterpret_cast<const char*>(version) : "unavailable")
            .arg(vendor ? reinterpret_cast<const char*>(vendor) : "unavailable")
            .arg(renderer ? reinterpret_cast<const char*>(renderer) : "unavailable")
            .arg(format().swapInterval())
            .arg(legacyContext ? 1 : 0)
            .arg(legacyContext && legacyContext->isValid() ? 1 : 0)
            .arg(legacyFormat.majorVersion()).arg(legacyFormat.minorVersion())
            .arg(int(legacyFormat.profile())));
}

CMob* CView::ownerMob(CNode* node) const
{
    if(!node)
        return nullptr;
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        if(mob->nodes().contains(node) || mob->logicNodes().contains(node))
            return mob;
    }
    return nullptr;
}

bool CView::isMobVisible(CMob* mob) const
{
    return mob && !m_hiddenMobs.contains(mob);
}

void CView::setMobVisible(CMob* mob, bool visible)
{
    if(!mob || !m_aMob.contains(mob) || isMobVisible(mob) == visible)
        return;
    if(visible)
        m_hiddenMobs.remove(mob);
    else
    {
        m_hiddenMobs.insert(mob);
        mob->clearSelect(true);
        if(m_pTree)
            m_pTree->clearViewportSelectionMarker();
        viewParameters();
    }
    collectObjectTreeData();
    update();
    emit loadedMapStateChanged();
}

void CView::setWorkspaceVisibility(const QList<CMob*>& mobs,
                                   bool includeLandscape, bool visible)
{
    bool changed = false;
    bool selectionChanged = false;
    if(includeLandscape && m_landscapeVisible != visible)
    {
        m_landscapeVisible = visible;
        changed = true;
        if(!visible)
        {
            m_bPreviewTile = false;
            changeOperation(EButtonOpSelect);
        }
    }
    for(CMob* mob : mobs)
    {
        if(!mob || !m_aMob.contains(mob) || isMobVisible(mob) == visible)
            continue;
        changed = true;
        if(visible)
            m_hiddenMobs.remove(mob);
        else
        {
            m_hiddenMobs.insert(mob);
            for(CNode* node : selectionSnapshot())
                if(ownerMob(node) == mob) selectionChanged = true;
            mob->clearSelect(true);
        }
    }
    if(!changed)
        return;
    if(selectionChanged && m_pTree)
        m_pTree->clearViewportSelectionMarker();
    collectObjectTreeData();
    viewParameters();
    update();
    emit loadedMapStateChanged();
}

void CView::setLandscapeVisible(bool visible)
{
    if(m_landscapeVisible == visible)
        return;
    m_landscapeVisible = visible;
    if(!visible)
    {
        m_bPreviewTile = false;
        changeOperation(EButtonOpSelect);
    }
    update();
    emit loadedMapStateChanged();
}

bool CView::hasEditableSelection() const
{
    if(!m_activeMob || !isMobVisible(m_activeMob))
        return false;
    bool found = false;
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        for(CNode* node : mob->nodes())
            if(node && node->nodeState() == ENodeState::eSelect)
            {
                if(mob != m_activeMob)
                    return false;
                found = true;
            }
        for(CNode* node : mob->logicNodes())
            if(node && node->nodeState() == ENodeState::eSelect)
            {
                if(mob != m_activeMob)
                    return false;
                found = true;
            }
    }
    return found;
}

bool CView::selectNode(CNode* node, bool addToSelection, bool activateOwner)
{
    CMob* owner = ownerMob(node);
    if(!owner || !isMobVisible(owner))
        return false;
    if(activateOwner && owner != m_activeMob)
        changeCurrentMob(owner);
    return selectNodes(QList<CNode*>() << node, addToSelection);
}

bool CView::selectNodes(const QList<CNode*>& nodes, bool addToSelection)
{
    QList<CNode*> selectable;
    for(CNode* node : nodes)
    {
        CMob* owner = ownerMob(node);
        if(node && owner && isMobVisible(owner) && !selectable.contains(node))
            selectable.append(node);
    }
    if(selectable.isEmpty())
        return false;
    const QList<CNode*> before = selectionSnapshot();
    if(!addToSelection)
        for(CMob* mob : m_aMob)
            if(mob) mob->clearSelect(true);
    for(CNode* node : selectable)
    {
        if(addToSelection && node->nodeState() == ENodeState::eSelect)
            node->setState(ENodeState::eDraw);
        else
            node->setState(ENodeState::eSelect);
    }
    recordSelectionChange(before);
    if(m_pTree)
    {
        const QList<CNode*> selected = selectionSnapshot();
        if(selected.isEmpty())
            m_pTree->clearViewportSelectionMarker();
        else
            m_pTree->markNode(selected.back(), false);
    }
    viewParameters();
    update();
    emit loadedMapStateChanged();
    return true;
}

void CView::drawTilePreview(QOpenGLShaderProgram* program)
{
    if(m_pPreviewTile.isNull())
        return;

    m_pLand->glTexture()->bind(0); // todo: update preview tile texture after mpr loaded
    program->setUniformValue("qt_Texture0", 0);
    m_pPreviewTile->draw(program);
}

void CView::updateTileForm(bool bGenerateNewTable)
{
    if(bGenerateNewTable)
        m_pTileForm->fillTable(m_pLand->mapName(), m_pLand->textureNum());
    m_pTileForm->setTileTypes(m_pLand->tileTypes());
    m_pTileForm->setMaterial(m_pLand->materials());
    if(m_landscapeTools)
        m_landscapeTools->setOverlayMaterials(m_pLand->materials());
    m_pTileForm->setAnimTile(m_pLand->animTiles());
}

void CView::drawTerrainPaintOutline(QOpenGLShaderProgram* program)
{
    if(!m_landscapeTools || !m_pLand || !m_pLand->isMprLoad()
       || !m_landscapeTools->editingEnabled() || m_landscapeInteractionSuspended)
        return;
    const int position = program->attributeLocation("a_position");
    if(position < 0)
        return;
    if(m_landscapeTools->paintIssueMarkingEnabled())
    {
        QSet<QPoint> highlighted = m_paintIssueSelectedCells;
        if(m_paintIssueHoverValid)
            highlighted.insert(m_paintIssueHoverCell);
        glDepthFunc(GL_ALWAYS);
        for(const QPoint& cell : highlighted)
        {
            QVector<QVector3D> boundary;
            const QVector<QPointF> corners = {
                QPointF(cell.x() * 2.0, cell.y() * 2.0),
                QPointF((cell.x() + 1) * 2.0, cell.y() * 2.0),
                QPointF((cell.x() + 1) * 2.0, (cell.y() + 1) * 2.0),
                QPointF(cell.x() * 2.0, (cell.y() + 1) * 2.0)
            };
            for(const QPointF& corner : corners)
            {
                QVector3D point(corner.x(), corner.y(), 0.0f);
                if(!m_pLand->projectPt(point, true)) continue;
                point.setZ(point.z() + 0.18f);
                boundary.append(point);
            }
            if(boundary.size() != 4) continue;
            const bool selected = m_paintIssueSelectedCells.contains(cell);
            if(selected)
            {
                program->setUniformValue("customColor",
                                         QVector4D(1.0f, 0.05f, 0.05f, 0.20f));
                program->setAttributeArray(position, boundary.constData());
                glDrawArrays(GL_TRIANGLE_FAN, 0, boundary.size());
            }
            program->setUniformValue("customColor",
                                     QVector4D(1.0f, 0.12f, 0.12f, 0.98f));
            program->setAttributeArray(position, boundary.constData());
            glLineWidth(selected ? 3.0f : 2.0f);
            glDrawArrays(GL_LINE_LOOP, 0, boundary.size());
        }
        glLineWidth(1.0f);
        glDepthFunc(GL_LESS);
        return;
    }
    if(m_landscapeTools->editMode() == ELandscapeEditMode::Sculpt
       || m_landscapeTools->editMode() == ELandscapeEditMode::OverlaySculpt)
    {
        if(!m_sculptOutlineValid)
            return;
        QVector<QVector3D> circle;
        constexpr int segments = 64;
        circle.reserve(segments);
        const float radius = m_landscapeTools->radius();
        for(int segment = 0; segment < segments; ++segment)
        {
            const float angle = float(segment) * float(2.0 * M_PI / segments);
            QVector3D point(m_sculptOutlineCenter.x() + qCos(angle) * radius,
                            m_sculptOutlineCenter.y() + qSin(angle) * radius, 0.0f);
            const bool landSculpt = m_landscapeTools->editMode()
                == ELandscapeEditMode::Sculpt;
            if(!m_pLand->projectPt(point, landSculpt)
               && (landSculpt || !m_pLand->projectPt(point, true)))
                continue;
            point.setZ(point.z() + 0.12f);
            circle.append(point);
        }
        program->setUniformValue("customColor", QVector4D(1.0f, 0.52f, 0.10f, 0.95f));
        program->setAttributeArray(position, circle.constData());
        // Keep the complete footprint legible when an overlay is thinner
        // than the brush. Overlay points are preferred; land is only the
        // terrain-aligned visual fallback outside the active surface.
        glDepthFunc(GL_ALWAYS);
        glLineWidth(2.0f);
        glDrawArrays(GL_LINE_LOOP, 0, circle.size());
        glLineWidth(1.0f);
        glDepthFunc(GL_LESS);
        return;
    }
    if(!m_terrainPaintOutlineValid || m_landscapeTools->manualTileSelectionEnabled())
        return;
    const bool landLayer = m_terrainPaintProjectionLand;
    const int radius = qMax(0, int(qRound(m_landscapeTools->paintRadius())));
    const ETerrainTileBrushShape shape = m_landscapeTools->paintShape();
    const auto included = [this, radius, shape](int x, int y)
    {
        const int dx = qAbs(x - m_terrainPaintOutlineX);
        const int dy = qAbs(y - m_terrainPaintOutlineY);
        return shape == ETerrainTileBrushShape::Square
            ? qMax(dx, dy) <= radius
            : dx * dx + dy * dy <= radius * radius;
    };
    const auto projected = [this, landLayer](float x, float y,
                                             QVector3D* result)
    {
        QVector3D point(x, y, 0.0f);
        if(!m_pLand->projectPt(point, landLayer)
           && (landLayer || !m_pLand->projectPt(point, true)))
            return false;
        point.setZ(point.z() + 0.12f);
        *result = point;
        return true;
    };

    // The solver samples discrete vertices, but the brush itself is a true
    // circle/square centered on the snapped vertex. Half a vertex interval is
    // included so size 1 remains visible around its single selected junction.
    const float centerX = float(m_terrainPaintOutlineX * 2);
    const float centerY = float(m_terrainPaintOutlineY * 2);
    const float footprintRadius = float(radius * 2 + 1);
    QVector<QVector3D> boundary;
    if(shape == ETerrainTileBrushShape::Circle)
    {
        const int segments = qMax(48, 16 + radius * 12);
        for(int segment = 0; segment < segments; ++segment)
        {
            const float angle = float(segment) * float(2.0 * M_PI / segments);
            QVector3D point;
            if(projected(centerX + qCos(angle) * footprintRadius,
                         centerY + qSin(angle) * footprintRadius, &point))
                boundary.append(point);
        }
    }
    else
    {
        const QPointF corners[] = {
            {centerX - footprintRadius, centerY - footprintRadius},
            {centerX + footprintRadius, centerY - footprintRadius},
            {centerX + footprintRadius, centerY + footprintRadius},
            {centerX - footprintRadius, centerY + footprintRadius}
        };
        for(const QPointF& corner : corners)
        {
            QVector3D point;
            if(projected(float(corner.x()), float(corner.y()), &point))
                boundary.append(point);
        }
    }

    QVector<QVector3D> vertices;
    for(int y = m_terrainPaintOutlineY - radius;
        y <= m_terrainPaintOutlineY + radius; ++y)
    {
        for(int x = m_terrainPaintOutlineX - radius;
            x <= m_terrainPaintOutlineX + radius; ++x)
        {
            if(!included(x, y))
                continue;
            QVector3D point;
            if(projected(float(x * 2), float(y * 2), &point))
            {
                point.setZ(point.z() + 0.04f);
                vertices.append(point);
            }
        }
    }
    if(boundary.isEmpty() && vertices.isEmpty())
        return;
    program->setUniformValue("customColor", landLayer
        ? QVector4D(1.0f, 0.72f, 0.08f, 0.95f)
        : QVector4D(0.10f, 0.82f, 1.0f, 0.95f));
    glDepthFunc(GL_ALWAYS);
    if(boundary.size() >= 3)
    {
        program->setAttributeArray(position, boundary.constData());
        glLineWidth(2.0f);
        glDrawArrays(GL_LINE_LOOP, 0, boundary.size());
        glLineWidth(1.0f);
    }
    if(!vertices.isEmpty())
    {
        program->setAttributeArray(position, vertices.constData());
        glPointSize(7.0f);
        glDrawArrays(GL_POINTS, 0, vertices.size());
        glPointSize(1.0f);
    }
    glDepthFunc(GL_LESS);
}

void CView::rebuildTerrainAutoCatalog()
{
    m_workingTerrainAtlas.clear();
    m_workingAtlasProtectedSlots.clear();
    m_terrainAtlasAnalysis = STerrainAtlasAnalysis();
    m_terrainAtlasProfile = STerrainAtlasProfile();
    m_terrainAtlasProfileSource = STerrainAtlasProfileSource();
    m_terrainMaterialRepresentatives.clear();
    m_terrainPaintAudit.clear();
    m_terrainPaintActions.clear();
    m_terrainPaintActionId = 0;
    m_terrainGlobalTransitionSources.clear();
    m_importedTerrainTransitions.clear();
    if(m_landscapeTools)
        m_landscapeTools->setPaintTerrainTypes({}, {}, {});
    QByteArray seedText = m_mprDocument.mapInfo().innerNamespace.toUtf8();
    quint32 seed = 2166136261u;
    for(char byte : seedText)
    {
        seed ^= quint8(byte);
        seed *= 16777619u;
    }
    m_terrainSemanticSeed = seed;
    if(!m_terrainEditAvailable || !m_pTileForm)
        return;
    CValidationReport report;
    const QString mprNamespace = m_mprDocument.mapInfo().innerNamespace;
    const int slotCount = m_pTileForm->tileImages().size();
    const QByteArray atlasHash = CTerrainAtlasProfileInference::pixelSha256(
        m_pTileForm->tileImages());
    const QString profilePath = CTerrainAtlasProfileStore::profilePath(mprNamespace);
    bool reviewedProfileActive = false;
    bool provisionalProfileActive = false;
    bool staleProfile = false;
    bool builtInDefaultActive = false;

    // Shipped presets are immutable defaults. Only a profile explicitly
    // promoted by Material Atlas editing may override one, and its local path
    // remains visible in both Landscape Tools and the review window.
    STerrainAtlasProfile stored;
    QString storedPath;
    CValidationReport storedReport;
    const QString portableProfileRoot = m_pLand
        ? m_pLand->filePath().absolutePath() : QString();
    const bool adjacentPortableProfile = !portableProfileRoot.isEmpty()
        && CTerrainAtlasProfileStore::loadExplicitOverride(
            mprNamespace, atlasHash, slotCount, &stored,
            portableProfileRoot, &storedPath, &storedReport);
    if(adjacentPortableProfile || CTerrainAtlasProfileStore::loadExplicitOverride(
           mprNamespace, atlasHash, slotCount, &stored, QString(),
           &storedPath, &storedReport))
    {
        m_terrainAtlasProfile = stored;
        m_terrainAtlasAnalysis = m_terrainAtlasProfile.usableAnalysis(&storedReport);
        reviewedProfileActive = m_terrainAtlasAnalysis.usable;
        provisionalProfileActive = reviewedProfileActive
            && (!stored.approved || stored.assignedSlotCount() < stored.atlasSlotCount);
        m_terrainAtlasProfileSource.kind = ETerrainAtlasProfileSource::LocalOverride;
        m_terrainAtlasProfileSource.path = storedPath;
        if(!reviewedProfileActive)
        {
            m_terrainAtlasProfile.approved = false;
            staleProfile = true;
            m_terrainAtlasProfile = STerrainAtlasProfile();
            m_terrainAtlasAnalysis = STerrainAtlasAnalysis();
            m_terrainAtlasProfileSource = STerrainAtlasProfileSource();
        }
    }
    else
        staleProfile = QFileInfo::exists(profilePath);
    for(const SValidationFinding& finding : storedReport.findings())
        report.add(finding.code, finding.severity, finding.message, finding.location);

    if(m_terrainAtlasProfile.slotDefinitions.isEmpty())
    {
        m_terrainAtlasProfile = CTerrainAtlasPresetCatalog::exactProfile(
            mprNamespace, atlasHash, slotCount, &report);
        if(!m_terrainAtlasProfile.slotDefinitions.isEmpty())
        {
            m_terrainAtlasAnalysis = m_terrainAtlasProfile.usableAnalysis(&report);
            builtInDefaultActive = m_terrainAtlasAnalysis.usable;
            m_terrainAtlasProfileSource.kind =
                ETerrainAtlasProfileSource::BuiltInDefault;
        }
    }
    if(m_terrainAtlasProfile.slotDefinitions.isEmpty())
    {
        m_terrainAtlasProfile = CTerrainAtlasProfileInference::propose(
            mprNamespace, m_pTileForm->tileImages(),
            m_mprDocument.mapInfo().tileTypes, terrainAtlasUsage(), &report);
        m_terrainAtlasAnalysis =
            m_terrainAtlasProfile.usableAnalysis(&report);
        reviewedProfileActive = m_terrainAtlasAnalysis.usable;
        provisionalProfileActive = reviewedProfileActive;
        m_terrainAtlasProfileSource.kind =
            ETerrainAtlasProfileSource::GeneratedDraft;
    }
    else if(!builtInDefaultActive && !reviewedProfileActive)
        m_terrainAtlasAnalysis = STerrainAtlasAnalysis();
    QVector<STerrainPaintMaterialChoice> landChoices;
    QVector<STerrainPaintMaterialChoice> underwaterChoices;
    QVector<STerrainPaintMaterialChoice> overlayChoices;
    QMap<qint32, int> assignedByFamily;
    QMap<qint32, QString> nameByFamily;
    for(const STerrainAtlasProfileFamily& family : m_terrainAtlasProfile.families)
    {
        assignedByFamily.insert(family.id, family.memberSlots.size());
        nameByFamily.insert(family.id, family.name);
    }
    QSet<QPair<int, qint32>> displayedMaterials;
    for(const STerrainAtlasMaterial& material : m_terrainAtlasAnalysis.materials)
    {
        QVector<STerrainPaintMaterialChoice>& choices = material.layer == ETerrainAtlasLayer::Overlay
            ? overlayChoices : (material.underwater ? underwaterChoices : landChoices);
        const int thumbnailSlot=material.thumbnailSlot>=0
            ?material.thumbnailSlot:material.representativeSlot;
        const CTerrainTransitionCatalog& catalog=material.layer==ETerrainAtlasLayer::Overlay
            ?m_terrainAtlasAnalysis.overlayCatalog:m_terrainAtlasAnalysis.landCatalog;
        const int layerKey=material.layer==ETerrainAtlasLayer::Overlay?1:0;
        if(displayedMaterials.contains(qMakePair(layerKey,material.id)))
            continue;
        int uniformThumbnail=-1;
        for(const STerrainAtlasProfileSlot& definition:m_terrainAtlasProfile.slotDefinitions)
            if(definition.signature.northWest==material.id
               &&definition.signature.northEast==material.id
               &&definition.signature.southEast==material.id
               &&definition.signature.southWest==material.id)
            {
                uniformThumbnail=definition.slot;
                if(definition.slot==material.thumbnailSlot)break;
            }
        // A brush is one vertex-material identity. Transition-only families
        // are atlas evidence, not separate "material on support" brushes.
        if(uniformThumbnail<0||uniformThumbnail>=m_pTileForm->tileImages().size())
            continue;
        displayedMaterials.insert(qMakePair(layerKey,material.id));
        QVector<QPair<qint32, bool>> displayedSupports;
        displayedSupports.append(qMakePair(qint32(0),false));
        for(const QPair<qint32, bool>& displayed : displayedSupports)
        {
            const qint32 support = displayed.first;
            STerrainPaintMaterialChoice choice;
            choice.id = material.id;
            choice.supportId = support;
            choice.name = material.name;
            choice.contextualStamp = displayed.second;
            if(choice.contextualStamp && support != material.id)
                choice.name = tr("%1 — on %2")
                    .arg(material.name, nameByFamily.value(
                        support, tr("material %1").arg(support)));
            choice.assignedSlots = assignedByFamily.value(material.id);
            choice.cornerOnly = false;
            if(!choice.contextualStamp && uniformThumbnail >= 0
               && uniformThumbnail < m_pTileForm->tileImages().size())
                choice.thumbnail = m_pTileForm->tileImages().at(
                    uniformThumbnail);
            else
                choice.thumbnail = terrainVertexJunctionThumbnail(
                    m_pTileForm->tileImages(), catalog, material.id, support,
                    thumbnailSlot);
            choices.append(choice);
        }
        m_terrainMaterialRepresentatives.insert(material.id, material.representativeSlot);
    }
    // The profile loop above remains useful for atlas review metadata, but the
    // authoring palette and vertex field use game-wide full-tile families.
    landChoices.clear();
    underwaterChoices.clear();
    overlayChoices.clear();
    m_terrainMaterialRepresentatives.clear();
    m_terrainGlobalFamilies.clear();
    m_terrainGlobalMaterialSources.clear();
    if(m_terrainAtlasAnalysis.usable)
    {
        m_terrainAtlasAnalysis.landCatalog = globalMaterialCatalog(
            m_terrainAtlasProfile, m_terrainAtlasAnalysis.landCatalog,
            ETerrainAtlasLayer::Land);
        m_terrainAtlasAnalysis.overlayCatalog = globalMaterialCatalog(
            m_terrainAtlasProfile, m_terrainAtlasAnalysis.overlayCatalog,
            ETerrainAtlasLayer::Overlay);
        m_terrainAtlasAnalysis.overlayCompatibleCatalog = globalMaterialCatalog(
            m_terrainAtlasProfile, m_terrainAtlasAnalysis.overlayCompatibleCatalog,
            ETerrainAtlasLayer::Overlay);
        m_terrainGlobalFamilies =
            CTerrainAtlasPresetCatalog::globalUniformFamilies(&report);
        for(const STerrainGlobalTransitionSource& source :
                CTerrainAtlasPresetCatalog::globalAuthoredTransitions(&report))
            for(int rotation = 0; rotation < 4; ++rotation)
                m_terrainGlobalTransitionSources.insert(
                    globalTransitionKey(CTerrainTransitionCatalog::rotated(
                        source.signature, rotation)), source);
        const QSize requiredSize = m_pTileForm->tileImages().isEmpty()
            ? QSize() : m_pTileForm->tileImages().front().size();
        // A reviewed atlas page commonly supplies anchors for many material
        // families. Decode each physical MMP only once per catalog rebuild;
        // the previous per-family decode made map opening spend tens of
        // seconds repeatedly expanding the same 2048x2048 DXT page.
        QMap<QString, QByteArray> encodedGlobalPages;
        QSet<QString> missingGlobalPages;
        for(const STerrainGlobalMaterialFamily& family : m_terrainGlobalFamilies)
        {
            const QImage source = loadGlobalUniformSource(
                family, mprNamespace, requiredSize,
                &encodedGlobalPages, &missingGlobalPages,
                &m_terrainGlobalUniformTileCache);
            if(source.isNull())
            {
                report.add("terrain.global_family_source",
                    EValidationSeverity::Warning,
                    "A game-wide material family has no loadable uniform source in the configured resources",
                    family.name);
                continue;
            }
            m_terrainGlobalMaterialSources.insert(family.id, source);
            STerrainPaintMaterialChoice choice;
            choice.id = family.id;
            choice.name = family.name;
            choice.familyName = family.theme;
            choice.thumbnail = source;
            choice.assignedSlots = family.sources.size();
            if(family.layer == ETerrainAtlasLayer::Land)
            {
                m_terrainAtlasAnalysis.landCatalog.declareMaterial(family.id);
                m_terrainAtlasAnalysis.overlayCompatibleCatalog.declareMaterial(family.id);
                landChoices.append(choice);
            }
            else
            {
                m_terrainAtlasAnalysis.overlayCatalog.declareMaterial(family.id);
                m_terrainAtlasAnalysis.overlayCompatibleCatalog.declareMaterial(family.id);
                overlayChoices.append(choice);
            }
        }
        ei::log(eLogInfo, QString(
            "Game-wide material palette source pages loaded:%1 unavailable:%2 materialTiles:%3")
            .arg(encodedGlobalPages.size()).arg(missingGlobalPages.size())
            .arg(m_terrainGlobalMaterialSources.size()));
    }
    QString status = m_terrainAtlasAnalysis.usable
        ? tr("%1 loaded: %2 land and %3 overlay materials.")
              .arg(m_terrainAtlasAnalysis.templateName)
              .arg(landChoices.size()).arg(overlayChoices.size())
        : tr("No structurally valid material is available for namespace '%1'. Open Material atlas to assign one; manual atlas tile mode remains available.")
              .arg(mprNamespace);
    if(builtInDefaultActive)
        status = tr("Game-wide Material families loaded: %1 ground and %2 overlay families.")
            .arg(landChoices.size()).arg(overlayChoices.size());
    else if(reviewedProfileActive)
        status = m_terrainAtlasProfileSource.kind
                    == ETerrainAtlasProfileSource::LocalOverride
            ? tr("Local Material atlas override loaded: %1 land and %2 overlay materials.")
                .arg(landChoices.size()).arg(overlayChoices.size())
            : (provisionalProfileActive
            ? tr("Generated material atlas ready: %1 land and %2 overlay materials; %3 of %4 atlas slots assigned.")
                .arg(landChoices.size()).arg(overlayChoices.size())
                .arg(m_terrainAtlasProfile.assignedSlotCount())
                .arg(m_terrainAtlasProfile.atlasSlotCount)
            : tr("Saved material atlas loaded: %1 land and %2 overlay materials.")
                .arg(landChoices.size()).arg(overlayChoices.size()));
    else if(staleProfile)
        status += tr(" A saved auto-inferred profile is obsolete or failed its namespace/hash/slot binding; review the new proposal before use.");
    else if(!m_terrainAtlasProfile.slotDefinitions.isEmpty())
        status += tr(" No safe uniform material could be identified; open Material atlas to assign one.");
    QString sourceLabel;
    if(m_terrainAtlasProfileSource.kind == ETerrainAtlasProfileSource::BuiltInDefault)
        sourceLabel = tr("Default atlas");
    else if(m_terrainAtlasProfileSource.kind == ETerrainAtlasProfileSource::LocalOverride)
        sourceLabel = tr("Local override: %1").arg(
            QFileInfo(m_terrainAtlasProfileSource.path).fileName());
    else if(m_terrainAtlasProfileSource.kind == ETerrainAtlasProfileSource::GeneratedDraft)
        sourceLabel = tr("Generated draft");
    m_landscapeTools->setPaintMaterials(
        landChoices, underwaterChoices, overlayChoices,
        m_terrainAtlasProfile.assignedSlotCount(),
        m_terrainAtlasProfile.atlasSlotCount, status, sourceLabel,
        m_terrainAtlasProfileSource.path);
    updateTerrainAtlasViewerAnnotations(status);
    ei::log(report.hasErrors() ? eLogWarning : eLogInfo,
        QString("Material atlas source: %1, %2 land materials, %3 overlay materials, %4/%5 described slots%6")
            .arg(m_terrainAtlasAnalysis.templateName)
            .arg(landChoices.size()).arg(overlayChoices.size())
            .arg(m_terrainAtlasAnalysis.describedSlots).arg(m_terrainAtlasAnalysis.expectedSlots)
            .arg(report.findings().isEmpty() ? QString() : "\n" + report.toText()));
    emit updateMsg(m_terrainAtlasAnalysis.usable
        ? status
        : tr("This atlas layout is not registered; semantic terrain painting is disabled."));

    if(m_terrainAtlasAnalysis.usable)
    {
        if(m_authoredTerrainTileImages.size() != m_pTileForm->tileImages().size())
            m_authoredTerrainTileImages = m_pTileForm->tileImages();
        m_workingTerrainAtlas.reset(new CTerrainWorkingAtlas(
            m_terrainAtlasAnalysis.overlayCatalogWithLandFamilies(),
            m_authoredTerrainTileImages, m_terrainSemanticSeed,
            kMprPackedTerrainAtlasSlotLimit,
            m_terrainGlobalMaterialSources,
            globalMaterialBlendStyles(m_terrainGlobalFamilies),
            m_mprDocument.mapInfo().tileTypes,
            materialRuntimeTypes(m_terrainAtlasProfile,
                                 m_terrainGlobalFamilies)));
        m_terrainEdit.enableWorkingAtlasSlots(
            m_workingTerrainAtlas->capacityLimit());
        if(m_landscapeTools)
            m_landscapeTools->setGeneratedAtlasTileCount(0);
        const QVector<STerrainAtlasSlotUsage> currentUsage = terrainAtlasUsage();
        for(int slot = 0; slot < currentUsage.size(); ++slot)
            if(currentUsage.at(slot).landCount > 0
               || currentUsage.at(slot).overlayCount > 0)
                m_workingAtlasProtectedSlots.insert(slot);
        ei::log(eLogInfo, QString(
            "Modern terrain working atlas ready authoredSlots:%1 authoredPages:%2 capacitySlots:%3 extensionSlots:%4 protectedAuthoredSlots:%5")
            .arg(m_authoredTerrainTileImages.size())
            .arg(m_workingTerrainAtlas->activeTextureCount())
            .arg(m_workingTerrainAtlas->capacityLimit())
            .arg(m_workingTerrainAtlas->capacityLimit()
                 - m_authoredTerrainTileImages.size())
            .arg(m_workingAtlasProtectedSlots.size()));
        refreshTerrainFamilyUsage();
    }
}

void CView::refreshTerrainFamilyUsage()
{
    if(!m_landscapeTools || !m_terrainEditAvailable)
        return;
    QSet<qint32> land;
    QSet<qint32> overlay;
    CTerrainVirtualMaterialField field;
    const auto collectField = [](const CTerrainVirtualMaterialField& source,
                                 QSet<qint32>* used) {
        for(int y = 0; y < source.vertexHeight(); ++y)
            for(int x = 0; x < source.vertexWidth(); ++x)
                used->insert(source.material(x, y));
    };
    if(m_terrainEdit.copyVirtualMaterialField(ETerrainTileLayer::Land, &field))
        collectField(field, &land);
    else
        for(int y = 0; y < m_terrainEdit.tileCountY(); ++y)
            for(int x = 0; x < m_terrainEdit.tileCountX(); ++x)
            {
                bool available = false;
                const quint16 packed = m_terrainEdit.packedTile(
                    x, y, ETerrainTileLayer::Land, &available);
                bool found = false;
                const STerrainTransitionSignature signature = available
                    ? m_terrainAtlasAnalysis.landCatalog.gridSignatureForPacked(
                        packed, &found) : STerrainTransitionSignature();
                if(found)
                    for(qint32 value : {signature.northWest, signature.northEast,
                                        signature.southEast, signature.southWest})
                        land.insert(value);
            }
    if(m_terrainEdit.copyVirtualMaterialField(ETerrainTileLayer::Overlay, &field))
        collectField(field, &overlay);
    else
        for(int y = 0; y < m_terrainEdit.tileCountY(); ++y)
            for(int x = 0; x < m_terrainEdit.tileCountX(); ++x)
            {
                bool available = false;
                const quint16 packed = m_terrainEdit.packedTile(
                    x, y, ETerrainTileLayer::Overlay, &available);
                bool found = false;
                const STerrainTransitionSignature signature = available
                    ? m_terrainAtlasAnalysis.overlayCatalog.gridSignatureForPacked(
                        packed, &found) : STerrainTransitionSignature();
                if(found)
                    for(qint32 value : {signature.northWest, signature.northEast,
                                        signature.southEast, signature.southWest})
                        overlay.insert(value);
            }
    m_landscapeTools->setUsedTerrainFamilies(land, overlay);
}

QVector<STerrainAtlasSlotUsage> CView::terrainAtlasUsage() const
{
    const int slotCount = m_pTileForm ? m_pTileForm->tileImages().size() : 0;
    QVector<STerrainAtlasSlotUsage> usage(slotCount);
    for(int y = 0; y < m_terrainEdit.tileCountY(); ++y)
        for(int x = 0; x < m_terrainEdit.tileCountX(); ++x)
        {
            bool landAvailable = false;
            const SMprPackedTile land = SMprPackedTile::decode(m_terrainEdit.packedTile(
                x, y, ETerrainTileLayer::Land, &landAvailable));
            bool overlayAvailable = false;
            const SMprPackedTile overlay = SMprPackedTile::decode(m_terrainEdit.packedTile(
                x, y, ETerrainTileLayer::Overlay, &overlayAvailable));
            if(landAvailable && land.atlasSlot() >= 0 && land.atlasSlot() < slotCount)
            {
                ++usage[land.atlasSlot()].landCount;
                if(overlayAvailable) ++usage[land.atlasSlot()].landBelowOverlayCount;
            }
            if(overlayAvailable && overlay.atlasSlot() >= 0 && overlay.atlasSlot() < slotCount)
                ++usage[overlay.atlasSlot()].overlayCount;
        }
    return usage;
}

void CView::reviewTerrainAtlasProfile()
{
    if(!m_terrainEditAvailable || !m_pTileForm || m_pTileForm->tileImages().isEmpty())
    {
        emit updateMsg(tr("Load an MPR and its complete terrain atlas before reviewing semantic conversion."));
        return;
    }
    if(m_terrainEdit.virtualMaterialFieldActive(ETerrainTileLayer::Land)
       || m_terrainEdit.virtualMaterialFieldActive(ETerrainTileLayer::Overlay))
    {
        QMessageBox::information(this, tr("Material atlas is in use"),
            tr("Finalize or undo the current semantic terrain edits before changing the Material atlas profile. "
               "This keeps every generated working tile and undo reference bound to the atlas that created it."));
        return;
    }
    const QString mprNamespace = m_mprDocument.mapInfo().innerNamespace;
    const QByteArray hash = CTerrainAtlasProfileInference::pixelSha256(m_pTileForm->tileImages());
    STerrainAtlasProfile profile = m_terrainAtlasProfile;
    STerrainAtlasProfileSource source = m_terrainAtlasProfileSource;
    CValidationReport report;
    if(profile.slotDefinitions.isEmpty()
       || !profile.validateBinding(mprNamespace, hash, m_pTileForm->tileImages().size(), &report))
    {
        profile = CTerrainAtlasPresetCatalog::exactProfile(
            mprNamespace, hash, m_pTileForm->tileImages().size(), &report);
        source.kind = ETerrainAtlasProfileSource::BuiltInDefault;
        source.path.clear();
        if(profile.slotDefinitions.isEmpty())
        {
            profile = CTerrainAtlasProfileInference::propose(
                mprNamespace, m_pTileForm->tileImages(), m_mprDocument.mapInfo().tileTypes,
                terrainAtlasUsage(), &report);
            source.kind = ETerrainAtlasProfileSource::GeneratedDraft;
        }
    }
    CTerrainAtlasReviewDialog dialog(profile, m_pTileForm->tileImages(), source, this);
    if(dialog.exec() != QDialog::Accepted) return;
    if(dialog.shouldResetToDefault())
    {
        CValidationReport removeReport;
        if(!CTerrainAtlasProfileStore::removeLocalOverride(
               mprNamespace, QString(), &removeReport))
        {
            QMessageBox::warning(this, tr("Could not reset Material atlas"),
                                 removeReport.toText());
            return;
        }
        rebuildTerrainAutoCatalog();
        emit updateMsg(tr("Local Material atlas override removed; the built-in default is active."));
        return;
    }
    const STerrainAtlasProfile reviewed = dialog.reviewedProfile();
    if(!dialog.shouldPersistOverride())
    {
        rebuildTerrainAutoCatalog();
        emit updateMsg(tr("Built-in Material atlas remains active; no local override was created."));
        return;
    }
    CValidationReport saveReport;
    if(!CTerrainAtlasProfileStore::save(reviewed, QString(), &saveReport))
    {
        QMessageBox::warning(this, tr("Could not save semantic atlas profile"), saveReport.toText());
        return;
    }
    rebuildTerrainAutoCatalog();
    const QString savedPath = CTerrainAtlasProfileStore::profilePath(mprNamespace);
    emit updateMsg(reviewed.approved
        ? tr("Material atlas applied and saved to %1.")
              .arg(QDir::toNativeSeparators(savedPath))
        : tr("Material atlas applied and saved to %1. Unassigned or unconfirmed items remain excluded from painting.")
              .arg(QDir::toNativeSeparators(savedPath)));
}

void CView::updateTerrainAtlasViewerAnnotations(const QString& status)
{
    if(!m_pTileForm) return;
    QStringList annotations;
    annotations.reserve(m_terrainAtlasProfile.atlasSlotCount);
    QMap<qint32, STerrainAtlasProfileFamily> families;
    for(const STerrainAtlasProfileFamily& family : m_terrainAtlasProfile.families)
        families.insert(family.id, family);
    for(const STerrainAtlasProfileSlot& slot : m_terrainAtlasProfile.slotDefinitions)
    {
        const qint32 ids[] = {slot.signature.northWest, slot.signature.northEast,
                              slot.signature.southEast, slot.signature.southWest};
        QStringList names;
        for(qint32 id : ids)
            names.append(families.contains(id) ? families.value(id).name : QString::number(id));
        annotations.append(tr("Slot %1\nNW / NE / SE / SW: %2\n%3")
            .arg(slot.slot).arg(names.join(QStringLiteral(" / ")))
            .arg(!slot.assigned || slot.ambiguous ? tr("Unassigned") : tr("Assigned")));
    }
    m_pTileForm->setSemanticAnnotations(annotations,
        m_terrainAtlasProfile.approved ? tr("material atlas approved")
        : (m_terrainAtlasAnalysis.usable ? tr("material atlas available")
                                         : tr("no safe material assignments")));
    Q_UNUSED(status);
}

void CView::exportTerrainPaintDiagnostic()
{
    if(!m_pLand->isMprLoad())
    {
        emit updateMsg(tr("Load an MPR before exporting a paint diagnostic."));
        return;
    }
    const QString suggested = m_pLand->filePath().absolutePath() + QDir::separator()
        + m_pLand->filePath().completeBaseName() + "-paint-diagnostic.json";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export terrain paint diagnostic"), suggested, tr("JSON files (*.json)"));
    if(path.isEmpty())
        return;
    QJsonObject root;
    root.insert("schema", "ei-mapper-endurance-paint-diagnostic");
    root.insert("version", 5);
    root.insert("mprFileName", m_pLand->filePath().fileName());
    root.insert("mprNamespace", m_mprDocument.mapInfo().innerNamespace);
    root.insert("sourceMprSha256", QString::fromLatin1(QCryptographicHash::hash(
        m_mprDocument.originalBytes(), QCryptographicHash::Sha256).toHex()));
    root.insert("atlasSlotCount", m_pTileForm ? m_pTileForm->tileImages().size() : 0);
    root.insert("atlasPixelSha256", m_pTileForm
        ? QString::fromLatin1(terrainAtlasPixelSha256(m_pTileForm->tileImages())) : QString());
    root.insert("analysisMode", m_terrainAtlasProfile.approved
        ? QStringLiteral("reviewed-semantic-profile-v1")
        : (m_terrainAtlasAnalysis.usable
            ? QStringLiteral("authored-namespace-template-v1")
            : QStringLiteral("manual-atlas-only")));
    root.insert("semanticProfileApproved", m_terrainAtlasProfile.approved);
    root.insert("semanticProfileProvenance", m_terrainAtlasProfile.provenance);
    root.insert("profileCornerSpace", "tile-preview-nw-ne-se-sw-v1");
    root.insert("paintLookupTransform", "preview-mirror-y-fixed-rotation-v1");
    root.insert("templateName", m_terrainAtlasAnalysis.templateName);
    root.insert("registeredMaterialCount", m_terrainAtlasAnalysis.materials.size());
    root.insert("describedAtlasSlots", m_terrainAtlasAnalysis.describedSlots);
    root.insert("changedMprSavingEnabled", false);
    QJsonArray paintActions;
    for(const STerrainPaintAuditAction& action : m_terrainPaintActions)
    {
        QJsonObject object;
        object.insert("actionId", action.actionId);
        object.insert("strokeId", action.strokeId);
        object.insert("outcome", action.outcome);
        object.insert("validation", action.validation);
        object.insert("layer", action.semantic.layer == ETerrainTileLayer::Land
            ? "land" : "overlay");
        object.insert("requestedMaterialId", action.semantic.requestedMaterialId);
        object.insert("backgroundMaterialId", action.semantic.backgroundMaterialId);
        QJsonObject brush;
        brush.insert("centerX", action.semantic.brush.centerX);
        brush.insert("centerY", action.semantic.brush.centerY);
        brush.insert("radius", action.semantic.brush.radius);
        brush.insert("shape", action.semantic.brush.shape == ETerrainTileBrushShape::Circle
            ? "circle" : "square");
        object.insert("brush", brush);
        QJsonArray path;
        for(qint32 material : action.semantic.materialPath)
            path.append(material);
        object.insert("materialPath", path);
        const auto cellsJson = [](const QVector<QPoint>& cells) {
            QJsonArray result;
            for(const QPoint& cell : cells)
            {
                QJsonObject point;
                point.insert("x", cell.x());
                point.insert("y", cell.y());
                result.append(point);
            }
            return result;
        };
        object.insert("paintedCoreCells", cellsJson(action.semantic.paintedCoreCells));
        object.insert("paintedVertices", cellsJson(action.semantic.paintedVertices));
        object.insert("affectedCells", cellsJson(action.semantic.affectedCells));
        QJsonArray changedCells;
        QJsonArray changedTiles;
        const CTerrainTransitionCatalog& actionCatalog =
            m_terrainAtlasAnalysis.catalog(action.semantic.layer);
        for(const STerrainTileDelta& delta : action.deltas)
        {
            QJsonObject point;
            point.insert("x", delta.globalX);
            point.insert("y", delta.globalY);
            changedCells.append(point);
            QJsonObject tile;
            tile.insert("x", delta.globalX);
            tile.insert("y", delta.globalY);
            tile.insert("before", packedTerrainTileJson(delta.before));
            tile.insert("after", packedTerrainTileJson(delta.after));
            bool beforeFound = false;
            bool afterFound = false;
            const STerrainTransitionSignature beforeSignature =
                actionCatalog.gridSignatureForPacked(delta.before, &beforeFound);
            const STerrainTransitionSignature afterSignature =
                actionCatalog.gridSignatureForPacked(delta.after, &afterFound);
            tile.insert("beforeCatalogRecognized", beforeFound);
            tile.insert("afterCatalogRecognized", afterFound);
            if(beforeFound)
                tile.insert("beforeGridCorners",
                            terrainGridSignatureJson(beforeSignature));
            if(afterFound)
                tile.insert("afterGridCorners",
                            terrainGridSignatureJson(afterSignature));
            changedTiles.append(tile);
        }
        object.insert("changedCells", changedCells);
        object.insert("changedTiles", changedTiles);
        object.insert("solverMode", action.semantic.solverMode);
        object.insert("junctionPadding", action.semantic.junctionPadding);
        object.insert("solverNodes", action.semantic.solverNodes);
        object.insert("solverLimit", action.semantic.solverLimit);
        paintActions.append(object);
    }
    root.insert("paintActions", paintActions);
    QJsonArray operations;
    for(const STerrainTileDelta& delta : m_terrainPaintAudit)
    {
        QJsonObject operation;
        operation.insert("x", delta.globalX);
        operation.insert("y", delta.globalY);
        QJsonObject globalGrid;
        globalGrid.insert("x", delta.globalX);
        globalGrid.insert("y", delta.globalY);
        operation.insert("globalGrid", globalGrid);
        QJsonObject sectorGrid;
        sectorGrid.insert("sectorX", delta.globalX / 16);
        sectorGrid.insert("sectorY", delta.globalY / 16);
        sectorGrid.insert("localColumn", delta.globalX % 16);
        sectorGrid.insert("localRow", delta.globalY % 16);
        operation.insert("sectorGrid", sectorGrid);
        operation.insert("layer", delta.layer == ETerrainTileLayer::Land ? "land" : "overlay");
        operation.insert("beforePacked", int(delta.before));
        operation.insert("afterPacked", int(delta.after));
        operation.insert("before", packedTerrainTileJson(delta.before));
        operation.insert("after", packedTerrainTileJson(delta.after));
        operation.insert("hasSemanticRequest", delta.hasSemanticRequest);
        operation.insert("strokeId", delta.diagnosticStrokeId);
        if(delta.hasSemanticRequest)
        {
            QJsonObject brushCenter;
            brushCenter.insert("x", delta.brushCenterX);
            brushCenter.insert("y", delta.brushCenterY);
            operation.insert("brushCenterGrid", brushCenter);
            QJsonObject brushOffset;
            brushOffset.insert("x", delta.globalX - delta.brushCenterX);
            brushOffset.insert("y", delta.globalY - delta.brushCenterY);
            operation.insert("brushGridOffset", brushOffset);
            operation.insert("requestedMaterialId", delta.requestedMaterialId);
            operation.insert("requestedGridCorners",
                             terrainGridSignatureJson(delta.requestedSignature));
        }
        const CTerrainTransitionCatalog& catalog = m_terrainAtlasAnalysis.catalog(delta.layer);
        bool beforeFound = false;
        bool afterFound = false;
        bool baseFound = false;
        const STerrainTransitionSignature beforeSignature =
            catalog.signatureForPacked(delta.before, &beforeFound);
        const STerrainTransitionSignature afterSignature =
            catalog.signatureForPacked(delta.after, &afterFound);
        SMprPackedTile baseTile = SMprPackedTile::decode(delta.after);
        baseTile.rotation = 0;
        const STerrainTransitionSignature baseSignature =
            catalog.signatureForPacked(baseTile.encode(), &baseFound);
        operation.insert("beforeCatalogRecognized", beforeFound);
        operation.insert("afterCatalogRecognized", afterFound);
        operation.insert("slotRotationZeroCatalogRecognized", baseFound);
        if(beforeFound)
            operation.insert("beforeCatalogGridCorners",
                             terrainGridSignatureJson(beforeSignature));
        if(afterFound)
            operation.insert("afterCatalogGridCorners",
                             terrainGridSignatureJson(afterSignature));
        if(baseFound)
            operation.insert("slotRotationZeroCatalogGridCorners",
                             terrainGridSignatureJson(baseSignature));
        operations.append(operation);
    }
    root.insert("paintDeltas", operations);
    QMap<QString, STerrainTileDelta> snapshotCells;
    for(const STerrainPaintAuditAction& action : m_terrainPaintActions)
    {
        for(const QPoint& affected : action.semantic.affectedCells)
        {
            for(int offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for(int offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    STerrainTileDelta cell;
                    cell.globalX = affected.x() + offsetX;
                    cell.globalY = affected.y() + offsetY;
                    cell.layer = action.semantic.layer;
                    const QString key = QString("%1:%2:%3")
                        .arg(cell.layer == ETerrainTileLayer::Land ? "land" : "overlay")
                        .arg(cell.globalX).arg(cell.globalY);
                    snapshotCells.insert(key, cell);
                }
            }
        }
    }
    for(const STerrainTileDelta& delta : m_terrainPaintAudit)
    {
        for(int offsetY = -1; offsetY <= 1; ++offsetY)
        {
            for(int offsetX = -1; offsetX <= 1; ++offsetX)
            {
                STerrainTileDelta cell;
                cell.globalX = delta.globalX + offsetX;
                cell.globalY = delta.globalY + offsetY;
                cell.layer = delta.layer;
                const QString key = QString("%1:%2:%3")
                    .arg(cell.layer == ETerrainTileLayer::Land ? "land" : "overlay")
                    .arg(cell.globalX).arg(cell.globalY);
                snapshotCells.insert(key, cell);
            }
        }
    }
    QJsonArray currentTiles;
    for(const STerrainTileDelta& cell : snapshotCells)
    {
        bool available = false;
        const quint16 packed = m_terrainEdit.packedTile(
            cell.globalX, cell.globalY, cell.layer, &available);
        if(!available)
            continue;
        QJsonObject current;
        current.insert("x", cell.globalX);
        current.insert("y", cell.globalY);
        current.insert("layer", cell.layer == ETerrainTileLayer::Land ? "land" : "overlay");
        current.insert("packed", packedTerrainTileJson(packed));
        const CTerrainTransitionCatalog& catalog = m_workingTerrainAtlas
            ? m_workingTerrainAtlas->catalog()
            : m_terrainAtlasAnalysis.catalog(cell.layer);
        bool found = false;
        const STerrainTransitionSignature paintSignature =
            catalog.gridSignatureForPacked(packed, &found);
        current.insert("catalogRecognized", found);
        if(found)
        {
            current.insert("paintGridCorners",
                           terrainGridSignatureJson(paintSignature));
            current.insert("previewImageCorners", terrainGridSignatureJson(
                CTerrainTransitionCatalog::mirroredGridY(paintSignature)));
        }
        currentTiles.append(current);
    }
    root.insert("currentTileSnapshot", currentTiles);
    QFile output(path);
    if(!output.open(QIODevice::WriteOnly | QIODevice::Truncate)
       || output.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0)
    {
        QMessageBox::warning(this, tr("Paint diagnostic"),
                             tr("Could not write %1: %2").arg(path, output.errorString()));
        return;
    }
    output.close();
    emit updateMsg(tr("Paint diagnostic exported without writing an MPR: %1").arg(path));
}

QMap<int, quint64> CView::workingTerrainAtlasUsage() const
{
    QMap<int, quint64> result;
    const int capacity = m_workingTerrainAtlas
        ? m_workingTerrainAtlas->capacityLimit() : 0;
    for(int slot = 0; slot < capacity; ++slot)
        result.insert(slot, 0);
    for(int y = 0; y < m_terrainEdit.tileCountY(); ++y)
        for(int x = 0; x < m_terrainEdit.tileCountX(); ++x)
            for(ETerrainTileLayer layer : {ETerrainTileLayer::Land,
                                           ETerrainTileLayer::Overlay})
            {
                bool available = false;
                const SMprPackedTile packed = SMprPackedTile::decode(
                    m_terrainEdit.packedTile(x, y, layer, &available));
                if(available && packed.atlasSlot() < capacity)
                    ++result[packed.atlasSlot()];
            }
    return result;
}

bool CView::realizeWorkingTerrainSignature(
    const STerrainTransitionSignature& signature, int cellX, int cellY,
    QMap<int, quint64>* usage, CValidationReport* report, bool* generated,
    qint32 preferredPaintMaterial)
{
    if(!m_workingTerrainAtlas || !m_workingTerrainAtlas->isValid())
    {
        if(report) report->add("terrain.working_atlas_unavailable",
            EValidationSeverity::Error,
            "The live exact-transition cache is unavailable for this map.");
        return false;
    }
    // Resolution order is user-visible policy: current map exact tuple first,
    // then an exact authored tuple from any configured stock JMV atlas, and
    // only then stock-mask-derived generation. Global pages are decoded on demand so
    // building the palette does not expand every atlas in the installation.
    bool alreadyExact = false;
    m_workingTerrainAtlas->catalog().selectForPaint(
        signature, cellX, cellY, m_terrainSemanticSeed, &alreadyExact);
    bool exactLibraryReady = false;
    if(!alreadyExact)
    {
        const QList<STerrainGlobalTransitionSource> candidates =
            m_terrainGlobalTransitionSources.values(globalTransitionKey(signature));
        const QSize requiredSize = m_authoredTerrainTileImages.isEmpty()
            ? QSize() : m_authoredTerrainTileImages.front().size();
        for(const STerrainGlobalTransitionSource& source : candidates)
        {
            const QImage pixels = loadGlobalTransitionTile(
                source, requiredSize,
                &m_terrainGlobalTransitionEncodedPageCache,
                &m_terrainGlobalTransitionMissingPages,
                &m_terrainGlobalTransitionTileCache);
            if(pixels.isNull())
                continue;
            const QString label = QStringLiteral("%1 slot %2")
                .arg(source.mprNamespace).arg(source.slot);
            CValidationReport importReport;
            if(m_workingTerrainAtlas->addAuthoredLibraryTransition(
                   source.signature, pixels, label, &importReport))
            {
                exactLibraryReady = true;
                m_importedTerrainTransitions.insert(
                    globalTransitionKey(source.signature),
                    qMakePair(source.signature, pixels));
                ei::log(eLogInfo, QString(
                    "Terrain exact transition source: authored global %1")
                    .arg(label));
                break;
            }
        }
    }
    if(!alreadyExact && !exactLibraryReady
       && m_workingTerrainAtlas->stockBlendMaskCountFor(signature) < 3)
    {
        // Exact stock reuse was unavailable. Lazily index a few real JMV
        // silhouettes of the required topology, preferring the requested
        // material schools. This keeps map opening cheap while guaranteeing
        // that the first paint on a default atlas is not refused merely
        // because no unrelated earlier stroke happened to import a mask.
        struct RankedMaskSource
        {
            STerrainGlobalTransitionSource source;
            int score{0};
            QString stableKey;
        };
        const QMap<qint32, ETerrainBlendStyle> styles =
            globalMaterialBlendStyles(m_terrainGlobalFamilies);
        const ETerrainBlendStyle requestedA = styles.value(
            signature.northWest, ETerrainBlendStyle::Organic);
        ETerrainBlendStyle requestedB = requestedA;
        for(qint32 material : {signature.northEast, signature.southEast,
                               signature.southWest})
            if(material != signature.northWest)
            {
                requestedB = styles.value(material,
                    ETerrainBlendStyle::Organic);
                break;
            }
        QVector<RankedMaskSource> ranked;
        QSet<QString> seen;
        const int requestedTopology = terrainTransitionTopology(signature);
        for(auto it = m_terrainGlobalTransitionSources.cbegin();
            it != m_terrainGlobalTransitionSources.cend(); ++it)
        {
            const STerrainGlobalTransitionSource& source = it.value();
            if(terrainTransitionTopology(source.signature) != requestedTopology)
                continue;
            const QString stableKey = QStringLiteral("%1:%2")
                .arg(source.mprNamespace.toCaseFolded()).arg(source.slot);
            if(seen.contains(stableKey))
                continue;
            seen.insert(stableKey);
            const ETerrainBlendStyle sourceA = styles.value(
                source.signature.northWest, ETerrainBlendStyle::Organic);
            ETerrainBlendStyle sourceB = sourceA;
            for(qint32 material : {source.signature.northEast,
                                   source.signature.southEast,
                                   source.signature.southWest})
                if(material != source.signature.northWest)
                {
                    sourceB = styles.value(material,
                        ETerrainBlendStyle::Organic);
                    break;
                }
            const bool exactStyles = (sourceA == requestedA
                && sourceB == requestedB) || (sourceA == requestedB
                && sourceB == requestedA);
            const bool oneStyle = sourceA == requestedA || sourceA == requestedB
                || sourceB == requestedA || sourceB == requestedB;
            RankedMaskSource candidate;
            candidate.source = source;
            candidate.score = exactStyles ? 1000 : oneStyle ? 100 : 0;
            candidate.stableKey = stableKey;
            ranked.append(candidate);
        }
        std::sort(ranked.begin(), ranked.end(),
            [](const RankedMaskSource& left, const RankedMaskSource& right) {
                if(left.score != right.score) return left.score > right.score;
                return left.stableKey < right.stableKey;
            });
        const QSize requiredSize = m_authoredTerrainTileImages.isEmpty()
            ? QSize() : m_authoredTerrainTileImages.front().size();
        for(const RankedMaskSource& candidate : ranked)
        {
            const QImage pixels = loadGlobalTransitionTile(
                candidate.source, requiredSize,
                &m_terrainGlobalTransitionEncodedPageCache,
                &m_terrainGlobalTransitionMissingPages,
                &m_terrainGlobalTransitionTileCache);
            if(pixels.isNull())
                continue;
            const int before =
                m_workingTerrainAtlas->stockBlendMaskCountFor(signature);
            const QString label = QStringLiteral("%1 slot %2")
                .arg(candidate.source.mprNamespace)
                .arg(candidate.source.slot);
            m_workingTerrainAtlas->addStockBlendMaskTemplate(
                candidate.source.signature, pixels, label, nullptr);
            const int after =
                m_workingTerrainAtlas->stockBlendMaskCountFor(signature);
            if(after > before)
                ei::log(eLogInfo, QString(
                    "Terrain stock mask source indexed:%1 topology:%2 score:%3")
                    .arg(label).arg(requestedTopology).arg(candidate.score));
            if(after >= 3)
                break;
        }
    }
    const auto appendFindings = [report](const CValidationReport& source) {
        if(!report) return;
        for(const SValidationFinding& finding : source.findings())
            report->add(finding.code, finding.severity,
                        finding.message, finding.location);
    };
    const auto hasCapacityFinding = [](const CValidationReport& source) {
        for(const SValidationFinding& finding : source.findings())
            if(finding.code == QStringLiteral("terrain.working_atlas_capacity"))
                return true;
        return false;
    };
    CValidationReport attempt;
    quint16 packed = m_workingTerrainAtlas->resolve(
        signature, cellX, cellY, usage, m_workingAtlasProtectedSlots,
        generated, &attempt, preferredPaintMaterial);
    if(attempt.hasErrors() && hasCapacityFinding(attempt) && usage)
    {
        bool reclaimable = false;
        for(int slot = 0; slot < m_workingTerrainAtlas->capacityLimit(); ++slot)
            if(usage->value(slot) == 0 && m_workingAtlasProtectedSlots.contains(slot))
            {
                reclaimable = true;
                break;
            }
        if(reclaimable)
        {
            // Physical packed slots are referenced by old undo commands. They
            // may only be recycled after that history is explicitly retired.
            // The virtual material field/current map remain authoritative.
            if(m_pUndoStack)
                m_pUndoStack->clear();
            m_workingAtlasProtectedSlots.clear();
            for(auto it = usage->cbegin(); it != usage->cend(); ++it)
                if(it.value() != 0)
                    m_workingAtlasProtectedSlots.insert(it.key());
            CValidationReport retry;
            if(generated) *generated = false;
            packed = m_workingTerrainAtlas->resolve(
                signature, cellX, cellY, usage, m_workingAtlasProtectedSlots,
                generated, &retry, preferredPaintMaterial);
            if(!retry.hasErrors())
            {
                m_tileStrokeUndoCheckpointed = true;
                if(report) report->add("terrain.working_atlas_undo_checkpoint",
                    EValidationSeverity::Warning,
                    "The live preview cache was full. The editor checkpointed the current terrain state and cleared prior undo history so unused physical slots could be safely recycled; no material identity was substituted.");
                ei::log(eLogWarning,
                    "Terrain preview cache checkpointed current state and cleared undo history to reclaim unused exact-transition slots.");
            }
            appendFindings(retry);
            if(retry.hasErrors())
                return false;
        }
        else
        {
            appendFindings(attempt);
            return false;
        }
    }
    else
    {
        appendFindings(attempt);
        if(attempt.hasErrors())
            return false;
    }
    const int resolvedSlot = SMprPackedTile::decode(packed).atlasSlot();
    const qint32 runtimeType =
        m_workingTerrainAtlas->runtimeTerrainTypeForSlot(resolvedSlot);
    if(!m_terrainEdit.setWorkingAtlasSlotType(resolvedSlot, runtimeType))
    {
        if(report) report->add("terrain.working_type_install",
            EValidationSeverity::Error,
            "The resolved exact transition could not install its runtime terrain type.");
        return false;
    }
    if(usage)
        ++(*usage)[resolvedSlot];
    return true;
}

bool CView::syncWorkingTerrainAtlas(CValidationReport* report)
{
    if(!m_workingTerrainAtlas)
        return false;
    if(m_landscapeTools)
        m_landscapeTools->setGeneratedAtlasTileCount(
            m_workingTerrainAtlas->generatedTileCount());
    const QSet<int> changed = m_workingTerrainAtlas->takeChangedSlots();
    if(changed.isEmpty())
        return true;
    const QVector<QImage> activeImages = m_workingTerrainAtlas->activeTileImages();
    const QVector<QImage> tableImages = activeImages.mid(
        0, m_authoredTerrainTileImages.size());
    if(tableImages != m_authoredTerrainTileImages)
    {
        if(report) report->add("terrain.working_atlas_authored_mutation",
            EValidationSeverity::Error,
            "The working atlas attempted to change authored terrain pixels. The preview update was refused before upload.");
        return false;
    }
    QElapsedTimer uploadTimer;
    uploadTimer.start();
    makeCurrent();
    const bool installed = m_pLand->previewTerrainAtlas(
        activeImages, m_workingTerrainAtlas->activeTextureCount());
    doneCurrent();
    ei::log(eLogInfo, QString(
        "Working terrain atlas sync timing: changedSlots:%1 activeSlots:%2 pages:%3 elapsedMs:%4")
        .arg(changed.size()).arg(activeImages.size())
        .arg(m_workingTerrainAtlas->activeTextureCount())
        .arg(uploadTimer.elapsed()));
    if(!installed && report)
        report->add("terrain.working_atlas_renderer",
            EValidationSeverity::Error,
            "The generated exact tiles could not be installed in the live terrain renderer.");
    if(installed)
        update();
    return installed;
}

void CView::finalizeVirtualTerrainAtlas()
{
    const bool landActive=m_terrainEdit.virtualMaterialFieldActive(ETerrainTileLayer::Land);
    const bool overlayActive=m_terrainEdit.virtualMaterialFieldActive(ETerrainTileLayer::Overlay);
    if(!m_terrainEditAvailable||!m_pTileForm||m_pTileForm->tileImages().isEmpty()
       ||(!landActive&&!overlayActive))
    {
        QMessageBox::information(this,tr("No virtual atlas changes"),
            tr("Paint at least one semantic terrain material before finalizing a map-specific atlas."));
        return;
    }
    if(landActive&&overlayActive)
    {
        QMessageBox::warning(this,tr("Capacity review required"),
            tr("This MVP can finalize one changed terrain layer per isolated package. "
               "Both Ground and Overlay contain virtual material edits. Undo or save one layer "
               "as a separate editing session; no identities were substituted."));
        return;
    }
    const ETerrainTileLayer layer=landActive?ETerrainTileLayer::Land:ETerrainTileLayer::Overlay;
    QProgressDialog finalizeProgress(
        tr("Preparing map-specific terrain atlas..."),QString(),0,0,this);
    finalizeProgress.setWindowTitle(tr("Finalizing terrain atlas"));
    finalizeProgress.setCancelButton(nullptr);
    finalizeProgress.setWindowModality(Qt::WindowModal);
    finalizeProgress.setMinimumDuration(0);
    finalizeProgress.show();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    CTerrainVirtualMaterialField field;
    if(!m_terrainEdit.copyVirtualMaterialField(layer,&field))
    {
        QMessageBox::critical(this,tr("Atlas finalization refused"),
            tr("The virtual material field is incomplete; no package was written."));
        return;
    }
    const CTerrainTransitionCatalog& catalog=layer==ETerrainTileLayer::Land
        ?m_terrainAtlasAnalysis.landCatalog:m_terrainAtlasAnalysis.overlayCatalog;
    QVector<quint16> pinned;
    const ETerrainTileLayer preserved=layer==ETerrainTileLayer::Land
        ?ETerrainTileLayer::Overlay:ETerrainTileLayer::Land;
    CTerrainEditModel originalTerrain;
    CValidationReport originalReport;
    finalizeProgress.setLabelText(tr(
        "Auditing preserved terrain references and unused slots..."));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    if(!originalTerrain.load(m_mprDocument,&originalReport))
    {
        QMessageBox::critical(this,tr("Atlas finalization refused"),
            tr("The preserved layer could not be reconstructed from the original MPR.\n\n%1")
                .arg(originalReport.toText()));
        return;
    }
    for(int y=0;y<m_terrainEdit.tileCountY();++y)
        for(int x=0;x<m_terrainEdit.tileCountX();++x)
        {
            bool available=false;
            const quint16 packed=originalTerrain.packedTile(x,y,preserved,&available);
            // 0xffff is the inactive-overlay sentinel, not atlas slot 16383.
            // Pinning it caused valid land finalization to fail with the
            // misleading "slot 16383 was not pinned" error.
            if(available && !(preserved==ETerrainTileLayer::Overlay
                              && packed==0xffffu))
                pinned.append(packed);
        }
    CValidationReport report;
    const QVector<QImage>& authoredImages=m_authoredTerrainTileImages.isEmpty()
        ?m_pTileForm->tileImages():m_authoredTerrainTileImages;
    CTerrainVirtualAtlas atlas(catalog, authoredImages, m_terrainSemanticSeed,
                               m_terrainGlobalMaterialSources,
                               globalMaterialBlendStyles(m_terrainGlobalFamilies),
                               layer == ETerrainTileLayer::Land,
                               m_mprDocument.mapInfo().tileTypes,
                               materialRuntimeTypes(m_terrainAtlasProfile,
                                                    m_terrainGlobalFamilies));
    finalizeProgress.setLabelText(tr(
        "Loading exact stock transitions used by this map..."));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    for(auto imported = m_importedTerrainTransitions.cbegin();
        imported != m_importedTerrainTransitions.cend(); ++imported)
        if(!atlas.addAuthoredLibraryTransition(
               imported.value().first, imported.value().second,
               QStringLiteral("stock-jmv-finalization"), &report))
        {
            QMessageBox::critical(this,tr("Atlas finalization refused"),
                tr("An authored stock transition used by this map could not be imported into the isolated final atlas.\n\n%1")
                    .arg(report.toText()));
            return;
        }
    finalizeProgress.setLabelText(tr(
        "Resolving, evicting unused tiles, and rebuilding physical pages..."));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QVector<qint32> placedCellTypes;
    placedCellTypes.reserve(field.tileWidth()*field.tileHeight());
    for(int y=0;y<field.tileHeight();++y)
        for(int x=0;x<field.tileWidth();++x)
        {
            bool available=false;
            const quint16 packed=m_terrainEdit.packedTile(x,y,layer,&available);
            bool typeFound=false;
            const qint32 type=available
                ?m_terrainEdit.tileType(packed,&typeFound):-1;
            placedCellTypes.append(typeFound?type:-1);
        }
    const STerrainAtlasFinalization finalized=atlas.finalize(
        field,m_mprDocument.mapInfo().innerNamespace,
        kMprPackedTerrainAtlasSlotLimit,
        m_mprDocument.mapInfo().tileTypes,&report,pinned,placedCellTypes);
    if(!finalized.success)
    {
        const QString findings=report.findings().isEmpty()
            ?QString():QStringLiteral("\n\n")+report.toText();
        QMessageBox::critical(this,tr("Atlas finalization refused"),
            finalized.error+findings);
        return;
    }
    int generatedCount=0;
    int importedStockCount=0;
    for(const STerrainFinalizedTile& tile:finalized.tiles)
    {
        generatedCount+=!tile.authored;
        importedStockCount+=tile.authored&&tile.sourceSlot<0;
    }
    const int retainedAuthored=finalized.authoredSlotRemap.size();
    const int evictedAuthored=qMax(0,authoredImages.size()-retainedAuthored);
    QDialog preview(this);
    preview.setWindowTitle(tr("Review finalized map atlas"));
    preview.resize(900,700);
    QVBoxLayout* previewLayout=new QVBoxLayout(&preview);
    QLabel* summary=new QLabel(tr(
        "Final atlas: %1 slots (%2 pages)\n"
        "Retained source tiles: %3  |  Evicted unused source tiles: %4\n"
        "Added stock JMV blends: %5  |  Newly generated exact blends: %6")
        .arg(finalized.requiredSlots).arg(finalized.textureCount)
        .arg(retainedAuthored).arg(evictedAuthored)
        .arg(importedStockCount).arg(generatedCount),&preview);
    summary->setWordWrap(true);
    previewLayout->addWidget(summary);
    QLabel* saveExplanation=new QLabel(tr(
        "Save result: one edited .mpr plus its required map-specific .res atlas "
        "and review manifest, stored together. Open or stage the new .mpr; do not "
        "save the original map over it afterward. The .res is texture data used by "
        "that MPR, not a second map."),&preview);
    saveExplanation->setWordWrap(true);
    saveExplanation->setStyleSheet(QStringLiteral(
        "QLabel { background: #263746; border: 1px solid #4d718c; padding: 7px; }"));
    previewLayout->addWidget(saveExplanation);
    QProgressBar* capacity=new QProgressBar(&preview);
    capacity->setRange(0,kMprPackedTerrainAtlasSlotLimit);
    capacity->setValue(finalized.requiredSlots);
    QString capacityColor;
    QString capacityTier;
    if(finalized.requiredSlots<=512)
    {capacityColor=QStringLiteral("#35a853");capacityTier=tr("shipped-size range");}
    else if(finalized.requiredSlots<=2048)
    {capacityColor=QStringLiteral("#ebbe23");capacityTier=tr("expanded range");}
    else if(finalized.requiredSlots<=4096)
    {capacityColor=QStringLiteral("#e87e20");capacityTier=tr("high-memory range");}
    else
    {capacityColor=QStringLiteral("#cd3737");capacityTier=tr("potentially unstable");}
    capacity->setFormat(tr("%1 / 16384 slots - %2")
        .arg(finalized.requiredSlots).arg(capacityTier));
    capacity->setStyleSheet(QStringLiteral(
        "QProgressBar { text-align: center; } "
        "QProgressBar::chunk { background-color: %1; }").arg(capacityColor));
    previewLayout->addWidget(capacity);
    if(finalized.requiredSlots>4096)
    {
        QLabel* warning=new QLabel(tr(
            "Potential instability: this atlas exceeds 4,096 slots. Runtime memory "
            "headroom depends on the rest of the map. The package may still be "
            "created and staged for controlled testing."),&preview);
        warning->setWordWrap(true);
        warning->setStyleSheet(QStringLiteral(
            "QLabel { color: #c73737; font-weight: bold; }"));
        previewLayout->addWidget(warning);
    }
    QLabel* addedHeading=new QLabel(tr(
        "Tiles added to this isolated atlas (stock reuse first, generated only when missing):"),
        &preview);
    addedHeading->setWordWrap(true);
    previewLayout->addWidget(addedHeading);
    QLabel* addedLegend=new QLabel(tr(
        "Blue frame: existing stock JMV blend added to this map.  "
        "Orange frame: newly generated exact blend."),&preview);
    addedLegend->setStyleSheet(QStringLiteral(
        "QLabel { color: #b9c4d0; padding-bottom: 4px; }"));
    previewLayout->addWidget(addedLegend);
    QScrollArea* addedScroll=new QScrollArea(&preview);
    addedScroll->setWidgetResizable(true);
    QWidget* addedPage=new QWidget(addedScroll);
    QGridLayout* addedGrid=new QGridLayout(addedPage);
    QMap<qint32,QString> materialNames;
    for(const STerrainGlobalMaterialFamily& family:m_terrainGlobalFamilies)
        materialNames.insert(family.id,family.name);
    const auto materialLabel=[&materialNames](qint32 id)
    {
        return materialNames.contains(id)
            ?materialNames.value(id)
            :QStringLiteral("Material %1").arg(id);
    };
    int addedIndex=0;
    for(int slot=0;slot<finalized.tiles.size();++slot)
    {
        const STerrainFinalizedTile& tile=finalized.tiles.at(slot);
        if(tile.sourceSlot>=0)continue;
        QToolButton* item=new QToolButton(addedPage);
        item->setIcon(QPixmap::fromImage(tile.pixels));
        item->setIconSize(QSize(96,96));
        item->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        item->setStyleSheet(tile.authored
            ?QStringLiteral("QToolButton { border: 3px solid #3f8dcc; padding: 3px; }")
            :QStringLiteral("QToolButton { border: 3px solid #e88b2b; padding: 3px; }"));
        item->setText(tr("Slot %1 - %2").arg(slot)
            .arg(tile.authored?tr("stock JMV"):tr("generated")));
        QStringList usedFamilies;
        for(qint32 id:{tile.canonicalSignature.northWest,
                       tile.canonicalSignature.northEast,
                       tile.canonicalSignature.southEast,
                       tile.canonicalSignature.southWest})
            if(!usedFamilies.contains(materialLabel(id)))
                usedFamilies.append(materialLabel(id));
        item->setToolTip(tr("Used for: %1\nCorners: %2 | %3 | %4 | %5\nPixel SHA-256: %6\nBlend source: %7")
            .arg(usedFamilies.join(QStringLiteral(" + ")))
            .arg(materialLabel(tile.canonicalSignature.northWest))
            .arg(materialLabel(tile.canonicalSignature.northEast))
            .arg(materialLabel(tile.canonicalSignature.southEast))
            .arg(materialLabel(tile.canonicalSignature.southWest))
            .arg(QString::fromLatin1(tile.pixelSha256))
            .arg(tile.blendProvenance.isEmpty()
                ?tr("original map atlas"):tile.blendProvenance));
        addedGrid->addWidget(item,addedIndex/6,addedIndex%6);
        ++addedIndex;
    }
    if(addedIndex==0)
        addedGrid->addWidget(new QLabel(tr("No tiles are added; this finalization only compacts existing usage."),addedPage),0,0);
    addedGrid->setAlignment(Qt::AlignTop);
    addedScroll->setWidget(addedPage);
    previewLayout->addWidget(addedScroll,1);
    QDialogButtonBox* buttons=new QDialogButtonBox(
        QDialogButtonBox::Save|QDialogButtonBox::Cancel,&preview);
    buttons->button(QDialogButtonBox::Save)->setText(tr(
        "Save edited MPR with atlas resources..."));
    connect(buttons,&QDialogButtonBox::accepted,&preview,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&preview,&QDialog::reject);
    previewLayout->addWidget(buttons);
    finalizeProgress.hide();
    if(preview.exec()!=QDialog::Accepted)return;
    const STerrainPortableAtlasPackage package=CTerrainVirtualAtlas::buildPortablePackage(
        m_mprDocument,finalized,layer,&report);
    if(!package.success)
    {
        const QString findings=report.findings().isEmpty()
            ?QString():QStringLiteral("\n\n")+report.toText();
        QMessageBox::critical(this,tr("Atlas package validation failed"),
            package.error+findings);
        return;
    }
    const QString parentPath=QFileDialog::getExistingDirectory(
        this,tr("Choose parent folder for portable atlas package"),
        m_pLand?m_pLand->filePath().absolutePath():QDir::homePath());
    if(parentPath.isEmpty())return;
    const QString folderName=QStringLiteral("%1-modern-atlas-%2")
        .arg(finalized.isolatedNamespace,
             QString::fromLatin1(finalized.stableSha256.left(8)));
    QDir parent(parentPath);
    const QString folderPath=parent.filePath(folderName);
    if(QFileInfo::exists(folderPath)||!parent.mkdir(folderName))
    {
        QMessageBox::critical(this,tr("Atlas package not written"),
            tr("The deterministic package folder already exists or could not be created:\n%1\n\n"
               "Existing packages are never replaced silently.").arg(QDir::toNativeSeparators(folderPath)));
        return;
    }
    QDir folder(folderPath);
    const QString mprPath=folder.filePath(finalized.isolatedNamespace+QStringLiteral(".mpr"));
    const QString resPath=folder.filePath(finalized.isolatedNamespace+QStringLiteral("-atlas.res"));
    const QString manifestPath=folder.filePath(QStringLiteral("atlas-package.json"));
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema"),QStringLiteral("ei-mapper-modern-atlas-v1"));
    manifest.insert(QStringLiteral("namespace"),finalized.isolatedNamespace);
    manifest.insert(QStringLiteral("stableSha256"),QString::fromLatin1(finalized.stableSha256));
    manifest.insert(QStringLiteral("capacityLimit"),finalized.capacityLimit);
    manifest.insert(QStringLiteral("usedSlots"),finalized.requiredSlots);
    manifest.insert(QStringLiteral("texturePages"),finalized.textureCount);
    manifest.insert(QStringLiteral("retainedAuthoredTiles"),retainedAuthored);
    manifest.insert(QStringLiteral("evictedUnusedAuthoredTiles"),evictedAuthored);
    manifest.insert(QStringLiteral("addedStockJmvBlends"),importedStockCount);
    manifest.insert(QStringLiteral("addedGeneratedExactBlends"),generatedCount);
    manifest.insert(QStringLiteral("runtimeStabilityTier"),capacityTier);
    manifest.insert(QStringLiteral("layer"),layer==ETerrainTileLayer::Land
        ?QStringLiteral("land"):QStringLiteral("overlay"));
    QJsonArray members;for(const QString& member:package.resourceMembers)members.append(member);
    manifest.insert(QStringLiteral("resourceMembers"),members);
    QJsonArray tiles;
    for(int slot=0;slot<finalized.tiles.size();++slot)
    {
        const STerrainFinalizedTile& tile=finalized.tiles.at(slot);
        QJsonObject item;item.insert(QStringLiteral("slot"),slot);
        item.insert(QStringLiteral("authored"),tile.authored);
        item.insert(QStringLiteral("sourceSlot"),tile.sourceSlot);
        item.insert(QStringLiteral("pixelSha256"),QString::fromLatin1(tile.pixelSha256));
        item.insert(QStringLiteral("stockDerivedBlend"),tile.stockDerivedBlend);
        item.insert(QStringLiteral("origin"),tile.sourceSlot>=0
            ?QStringLiteral("original-map")
            :(tile.authored?QStringLiteral("stock-jmv")
                           :QStringLiteral("generated-exact")));
        item.insert(QStringLiteral("blendProvenance"),tile.blendProvenance);
        QJsonArray families;
        QStringList familyLabels;
        for(qint32 id:{tile.canonicalSignature.northWest,
                       tile.canonicalSignature.northEast,
                       tile.canonicalSignature.southEast,
                       tile.canonicalSignature.southWest})
        {
            const QString label=materialNames.contains(id)
                ?materialNames.value(id):QStringLiteral("Material %1").arg(id);
            if(!familyLabels.contains(label))familyLabels.append(label);
        }
        for(const QString& label:familyLabels)families.append(label);
        item.insert(QStringLiteral("materialFamilies"),families);
        QJsonArray corners;corners.append(tile.canonicalSignature.northWest);
        corners.append(tile.canonicalSignature.northEast);
        corners.append(tile.canonicalSignature.southEast);
        corners.append(tile.canonicalSignature.southWest);
        item.insert(QStringLiteral("canonicalGridCorners"),corners);tiles.append(item);
    }
    manifest.insert(QStringLiteral("tiles"),tiles);
    // The isolated atlas has a new namespace, pixel hash, slot order, and
    // generated signatures. Persist its exact semantic contract beside the
    // MPR; otherwise reopening correctly finds the 704 (etc.) texture slots
    // but has zero assignments because only the original namespace profile is
    // known to the editor.
    CValidationReport portableProfileReport;
    QVector<QImage> physicalTiles;
    if(!CTerrainVirtualAtlas::decodePhysicalTileImages(
           finalized,&physicalTiles,&portableProfileReport))
    {
        QMessageBox::critical(this,tr("Atlas package not written"),
            tr("The isolated physical atlas could not be decoded.\n\n%1")
                .arg(portableProfileReport.toText()));
        parent.rmdir(folderName);
        return;
    }
    STerrainAtlasProfile portableProfile=
        CTerrainAtlasProfileStore::portableFinalizedProfile(
            finalized,physicalTiles,m_terrainAtlasProfile,
            layer==ETerrainTileLayer::Land?ETerrainAtlasLayer::Land
                                          :ETerrainAtlasLayer::Overlay,
            materialNames,&portableProfileReport);
    if(portableProfile.assignedSlotCount()<=0)
    {
        QMessageBox::critical(this,tr("Atlas package not written"),
            tr("The isolated material profile could not be constructed.\n\n%1")
                .arg(portableProfileReport.toText()));
        parent.rmdir(folderName);
        return;
    }
    const QByteArray profileBytes=QJsonDocument(
        portableProfile.toJson()).toJson(QJsonDocument::Indented);
    const QString profilePath=CTerrainAtlasProfileStore::profilePath(
        finalized.isolatedNamespace,folderPath);
    manifest.insert(QStringLiteral("semanticProfile"),
        QDir(folderPath).relativeFilePath(profilePath));
    const QByteArray manifestBytes=QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    const auto writeFile=[](const QString& path,const QByteArray& bytes,QString* error)
    {
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())
        {if(error)*error=file.errorString();return false;}
        return true;
    };
    QString error;
    if(!QDir().mkpath(QFileInfo(profilePath).absolutePath())
       ||!writeFile(mprPath,package.mprBytes,&error)
       ||!writeFile(resPath,package.resourceArchiveBytes,&error)
       ||!writeFile(profilePath,profileBytes,&error)
       ||!writeFile(manifestPath,manifestBytes,&error))
    {
        QFile::remove(mprPath);QFile::remove(resPath);QFile::remove(profilePath);
        QFile::remove(manifestPath);
        QDir(folderPath).rmdir(QStringLiteral("terrain-atlas-profiles"));
        parent.rmdir(folderName);
        QMessageBox::critical(this,tr("Atlas package not written"),error);
        return;
    }
    CMprDocument reopenedMpr;CValidationReport reopenReport;
    CResArchiveDocument reopenedRes;QStringList errors;
    QFile mprFile(mprPath),resFile(resPath);
    if(!mprFile.open(QIODevice::ReadOnly)||!resFile.open(QIODevice::ReadOnly)
       ||!reopenedMpr.loadBytes(mprFile.readAll(),mprPath,&reopenReport)
       ||reopenReport.hasErrors()
       ||!reopenedRes.loadBytes(resFile.readAll(),resPath,&errors))
    {
        QMessageBox::critical(this,tr("Atlas package reopen failed"),
            tr("The package was written but failed the final disk reopen. Do not install it.\n%1")
                .arg(reopenReport.toText()+errors.join(QStringLiteral("\n"))));
        return;
    }
    STerrainAtlasProfile reopenedProfile;
    QString reopenedProfilePath;
    if(!CTerrainAtlasProfileStore::loadExplicitOverride(
           finalized.isolatedNamespace,portableProfile.atlasPixelSha256,
           physicalTiles.size(),&reopenedProfile,folderPath,
           &reopenedProfilePath,&reopenReport)
       ||reopenedProfile.assignedSlotCount()!=portableProfile.assignedSlotCount())
    {
        QMessageBox::critical(this,tr("Atlas package reopen failed"),
            tr("The package textures reopened, but its material assignments did not. Do not install it.\n%1")
                .arg(reopenReport.toText()));
        return;
    }
    emit updateMsg(tr("Validated map-specific atlas package written: %1")
        .arg(QDir::toNativeSeparators(folderPath)));
    QMessageBox::information(this,tr("Edited MPR and atlas ready"),tr(
        "The edited map and its isolated terrain atlas were saved together.\n\n"
        "MPR to open or stage:\n%1\n\nRequired atlas resource:\n%2\n\n"
        "Namespace: %3\nUsed tiles: %4 / %5\nTexture pages: %6\n\n"
        "Keep the MPR and RES together. The current editor tab still represents "
        "the original editing session; reopen the MPR above to continue from the finalized package. "
        "The original MPR and shared atlas resources were not modified.")
        .arg(QDir::toNativeSeparators(mprPath),
             QDir::toNativeSeparators(resPath),finalized.isolatedNamespace)
        .arg(finalized.requiredSlots).arg(finalized.capacityLimit)
        .arg(finalized.textureCount));
}

void CView::onChangeCursorTile(QPixmap ico)
{
    if(m_pOp->operationMethod() != eOperationMethodTileBrush)
        return;
    // Создаем базовое изображение для курсора (например, простой квадрат)
    QPixmap baseCursorPixmap(42, 42);
    baseCursorPixmap.fill(Qt::transparent);  // Прозрачный фон

    QPainter painter(&baseCursorPixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Устанавливаем цвет и толщину пера для стрелки
    QPen pen(CScene::getInstance()->isLandTileEditMode() ? Qt::red : Qt::green);
    pen.setWidth(2);
    painter.setPen(pen);

    // Рисуем стрелку в левом верхнем углу
    painter.drawLine(2, 2, 10, 10);   // Линия тела стрелки
    painter.drawLine(2, 2, 2, 6);    // Линия нижней части стрелки
    painter.drawLine(2, 2, 6, 2);    // Линия верхней части стрелки

    painter.end();

    // Загружаем дополнительное изображение, которое нужно добавить к курсору
    //QPixmap additionalPixmap(":/images/icon.png");

    // Совмещаем оба изображения: рисуем дополнительное поверх базового
    QPixmap combinedPixmap(baseCursorPixmap.size());
    combinedPixmap.fill(Qt::transparent);

    painter.begin(&combinedPixmap);
    painter.drawPixmap(0, 0, baseCursorPixmap);  // Базовый курсор
    painter.drawPixmap(10, 10, ico); // Дополнительное изображение
    painter.end();

    // Создаем новый курсор из объединенного изображения
    QCursor customCursor(combinedPixmap, 0, 0);

    // Устанавливаем новый курсор для виджета
    setCursor(customCursor);

//    QCursor curs(ico, 0, 0);
    //    setCursor(curs);
}

void CView::onMapMaterialUpdate()
{
    if(!m_pLand->isMprLoad())
        return;
    QMessageBox::information(this, tr("MPR metadata"),
        tr("Direct atlas-category and liquid-material mutation is disabled until it uses the canonical staged MPR transaction path."));
}

void CView::onRestoreTileData()
{

}

void CView::checkNewLandVersion()
{
    return; //todo: set option to auto-check changes in mpr. Dont re-upload when save by ourselfes
    // Dont use QFileSystemWatcher because it spams signals several time for 1 MapEd save
    QFileInfo landPath(m_pLand->filePath());
    landPath.refresh();
    QDateTime lastM = landPath.lastModified();
    if (lastM > m_lastModifiedLand)
    {
        unloadLand();
        loadLandscape(landPath);
        m_lastModifiedLand = lastM;
    }
}

void CView::drawSelectFrame(QRect &rect)
{
    //convert frame to [(-1, 1), (-1, 1)]
    float leftX = rect.bottomLeft().x()/(float)m_width*2-1.0f;
    float leftY = rect.bottomLeft().y()/(float)m_height*2-1.0f;
    float rightX = rect.topRight().x()/(float)m_width*2-1.0f;
    float rightY = rect.topRight().y()/(float)m_height*2-1.0f;
    QPointF bottomLeft(leftX, -leftY);
    QPointF topRight(rightX, -rightY);
    m_selectFrame->updateFrame(bottomLeft, topRight);
}

void CView::pickObject(const QRect &rect, bool bAddToSelect)
{
    //TODO: use different buffer => save picking in image
    // Bind shader pipeline for use
    if(nullptr == m_activeMob)
    {
        m_selectFrame->reset();
        return;
    }

    const QList<CNode*> selectionBefore = selectionSnapshot();

    if (!m_selectProgram.bind())
        close();

    const QMatrix4x4 viewMatrix = m_cam->viewMatrix();
    m_selectProgram.setUniformValue("u_projMmatrix", m_projection);
    m_selectProgram.setUniformValue("u_viewMmatrix", viewMatrix);
    ei::gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const COptBool* cullingOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("frustumCulling")) : nullptr;
    const COptBool* drawLogicOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("drawLogic")) : nullptr;
    const bool cullingEnabled = (!cullingOption || cullingOption->value())
        && (!drawLogicOption || !drawLogicOption->value())
        && CScene::getInstance()->getMode() != eEditModeLogic;
    const CViewFrustum frustum(m_projection * viewMatrix);

    QList<CNode*> pickableNodes;
    const bool logicPicking = CScene::getInstance()->getMode() == eEditModeLogic;
    if(!logicPicking)
        RenderQueueContext::instance().beginSelection(&m_selectProgram);
    for(CMob* mob : m_aMob)
        if(isMobVisible(mob))
        {
            const QVector<CNode*> spatialCandidates = logicPicking
                ? QVector<CNode*>()
                : spatiallyVisibleNodes(mob, cullingEnabled ? &frustum : nullptr, nullptr);
            const QList<CNode*>& legacyCandidates = logicPicking
                ? mob->logicNodes() : mob->nodes();
            if(logicPicking)
                for(CNode* node : legacyCandidates)
                {
                    if(node && !pickableNodes.contains(node))
                    {
                        pickableNodes.append(node);
                        node->drawSelect(&m_selectProgram);
                    }
                }
            else
                for(CNode* node : spatialCandidates)
                {
                    if(node && !pickableNodes.contains(node))
                    {
                        pickableNodes.append(node);
                        node->drawSelect(&m_selectProgram);
                    }
                }
        }
    if(!logicPicking)
        RenderQueueContext::instance().end();

    QVector<SColor> aColor;
    getColorFromRect(rect, aColor);

    //if(m_operationType == EOperationTypeObjects)

    if (!bAddToSelect) // clear selection buffer if we click out of objects in single selection mode
        for(CMob* mob : m_aMob)
            if(mob) mob->clearSelect(true);

    CNode* lastPickedNode = nullptr;
    if (!aColor.isEmpty())
        foreach (CNode* node, pickableNodes)
            foreach (const auto& color, aColor)
            {
                if (node->isColorSuitable(color))
                {
                    lastPickedNode = node;
                    if (bAddToSelect)
                    {//shift pressed

                        if (node->nodeState() & ENodeState::eSelect)
                        {//remove from select
                            node->setState(ENodeState::eDraw);
                        }
                        else
                        {// add to select
                            node->setState(ENodeState::eSelect);
                        }
                        break;
                    }
                    else
                    {//shift not pressed
                        node->setState(ENodeState::eSelect);
                        break;
                    }
                    break;
                }
            }

    m_selectFrame->reset();
    recordSelectionChange(selectionBefore);
    if(m_pTree)
    {
        if(lastPickedNode && lastPickedNode->nodeState() == ENodeState::eSelect)
            m_pTree->revealNode(lastPickedNode);
        else
        {
            const QList<CNode*> selected = selectionSnapshot();
            if(selected.isEmpty())
                m_pTree->clearViewportSelectionMarker();
            else
                m_pTree->revealNode(selected.back());
        }
    }
    emit loadedMapStateChanged();
}

QVector3D CView::getTerrainPos(bool bLand)
{
    QPoint globalCursorPos = QCursor::pos();
    QPoint localCursorPos = mapFromGlobal(globalCursorPos);
    return getTerrainPos(localCursorPos.x(), localCursorPos.y(), bLand);
}

QVector3D CView::getTerrainBrushPos(bool* hit)
{
    const QPoint localCursorPos = mapFromGlobal(QCursor::pos());
    return getTerrainBrushPos(localCursorPos.x(), localCursorPos.y(), hit);
}

QVector3D CView::getTerrainBrushPos(const int cursorPosX, const int cursorPosY,
                                    bool* hit)
{
    if(hit)
        *hit = false;
    const bool addingSurface = m_landscapeTools
        && m_landscapeTools->editMode() == ELandscapeEditMode::OverlaySurface
        && m_landscapeTools->overlaySurfaceOperation()
            == EOverlaySurfaceOperation::Activate;
    if(addingSurface)
    {
        bool overlayHit = false;
        const QVector3D overlay = getTerrainPos(
            cursorPosX, cursorPosY, false, &overlayHit);
        if(overlayHit)
        {
            m_terrainPaintProjectionLand = false;
            if(hit)
                *hit = true;
            return overlay;
        }
    }
    m_terrainPaintProjectionLand = terrainBrushProjectsToLand();
    return getTerrainPos(cursorPosX, cursorPosY,
                         m_terrainPaintProjectionLand, hit);
}

bool CView::loadMob(QFileInfo &filePath)
{
    const auto normalizedPath = [](const QFileInfo& info)
    {
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical)
            .toCaseFolded();
    };
    const QString requestedPath = normalizedPath(filePath);
    for(CMob* loaded : m_aMob)
    {
        if(loaded && normalizedPath(loaded->filePath()) == requestedPath)
        {
            changeCurrentMob(loaded);
            ei::log(eLogInfo, QString("MOB open reused the already loaded document path:%1")
                .arg(filePath.absoluteFilePath()));
            emit updateMsg(tr("%1 is already loaded; its existing document was activated.")
                .arg(filePath.fileName()));
            return true;
        }
    }

    m_pProgress->reset();
    CMob* pMob = new CMob;
    pMob->attach(this, m_pProgress);
    const int result = pMob->readMob(filePath);
    if(result != 0)
    {
        ei::log(eLogWarning, QString("MOB load was rejected without registering a partial document path:%1 result:%2")
            .arg(filePath.absoluteFilePath()).arg(result));
        delete pMob;
        return false;
    }
    QElapsedTimer preloadTimer;
    preloadTimer.start();
    int preloadedUnits = 0;
    int unitCount = 0;
    for(CNode* node : pMob->nodes())
        if(dynamic_cast<CUnit*>(node))
            ++unitCount;
    if(unitCount > 0)
        m_pProgress->reset();
    // Keep the UI responsive during large MOB loads without forcing an event
    // pump and GL-context reacquisition for every unit. Twenty evenly spaced
    // updates keeps the longest zone18 preload interval below a second.
    const int progressStride = qMax(1, unitCount / 20);
    int unitsSinceProgress = 0;
    makeCurrent();
    for(CNode* node : pMob->nodes())
        if(CUnit* unit = dynamic_cast<CUnit*>(node))
        {
            // Progress updates process the Qt event queue. A paint or window
            // event may change/release the thread's current GL context, so a
            // single makeCurrent() before the loop is not a valid guarantee
            // for the following unit's buffer/texture uploads.
            ei::setCrashContext(QString("mob/unit-preload:%1").arg(unit->mapId()));
            if(!QOpenGLContext::currentContext())
            {
                ei::log(eLogFatal, QString(
                    "Unit visual preload stopped because no OpenGL context could be made current; unit:%1")
                    .arg(unit->mapId()));
                break;
            }
            unit->preloadVisualResources();
            ++preloadedUnits;
            ++unitsSinceProgress;
            if(unitsSinceProgress >= progressStride || preloadedUnits == unitCount)
            {
                m_pProgress->update(100.0 * unitsSinceProgress / unitCount);
                unitsSinceProgress = 0;
                // Progress processing may allow another widget to change the
                // current context. Reacquire only after that event boundary,
                // rather than once for every unit.
                makeCurrent();
            }
        }
    makeCurrent();
    doneCurrent();
    ei::setCrashContext("idle after MOB preload");
    if(unitCount > 0)
        m_pProgress->finish();
    ei::log(eLogInfo, QString("Preloaded visual resources for %1 units during MOB load in %2 ms")
            .arg(preloadedUnits).arg(preloadTimer.elapsed()));
    m_aMob.append(pMob);
    armOpenGlDiagnostics(QStringLiteral("MOB loaded: %1")
        .arg(filePath.fileName()), 6);
    if(!m_environmentMob)
        m_environmentMob = pMob;
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;
    m_scriptWorkspaces->registerMob(pMob);
    changeCurrentMob(pMob);
    refreshAiGraphPreviewSource();
    emit mobStateChanged();
    return true;
}

void CView::saveMobAs()
{
    if(nullptr == m_activeMob)
        return;

    const QFileInfo fileName = QFileDialog::getSaveFileName(
            this, "Save " + m_activeMob->mobName() + " as... ",
            m_activeMob->filePath().absoluteFilePath(),
            tr("Map objects (*.mob);;Mob as JSON(*.json)"));
    if(fileName.filePath().isEmpty())
        return;

    if (fileName.fileName().endsWith(".json"))
        m_activeMob->serializeJson(fileName);
    else if (fileName.fileName().endsWith(".mob"))
    {
        if(!saveMobDocument(m_activeMob, fileName))
            return;
        m_activeMob->setFileName(fileName);
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataActiveMob, fileName.baseName());
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, "");
    }
}

void CView::saveActiveMob()
{
    if(nullptr == m_activeMob)
        return;

    if(saveMobDocument(m_activeMob, m_activeMob->filePath()))
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, "");
}

void CView::saveAllMob()
{
    QList<CMob*> dirtyMobs;
    for(CMob* mob : m_aMob)
        if(mob && mob->isDirty())
            dirtyMobs.append(mob);
    if(dirtyMobs.isEmpty())
        return;

    QProgressDialog progress(tr("Preparing safe MOB saves..."), QString(),
                             0, dirtyMobs.size() * 5, this);
    progress.setWindowTitle(tr("Saving all MOBs"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(250);
    progress.setWindowFlags(progress.windowFlags() & ~Qt::WindowContextHelpButtonHint
                             & ~Qt::WindowCloseButtonHint);
    for(int index = 0; index < dirtyMobs.size(); ++index)
    {
        CMob* mob = dirtyMobs.at(index);
        const auto updateProgress = [&progress, index, mob](const QString& label,
                                                            int value, int maximum)
        {
            Q_UNUSED(maximum);
            progress.setLabelText(QString("%1: %2").arg(mob->mobName(), label));
            progress.setValue(index * 5 + value);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        };
        if(!mob->save(updateProgress))
            break;
    }
    progress.setValue(progress.maximum());
    if(m_activeMob && !m_activeMob->isDirty())
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, "");
}

bool CView::saveMobDocument(CMob* mob, const QFileInfo& destination)
{
    if(!mob)
        return false;
    QProgressDialog progress(tr("Preparing safe MOB save..."), QString(), 0, 5, this);
    progress.setWindowTitle(tr("Saving %1").arg(mob->mobName()));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(250);
    progress.setWindowFlags(progress.windowFlags() & ~Qt::WindowContextHelpButtonHint
                             & ~Qt::WindowCloseButtonHint);
    const auto updateProgress = [&progress](const QString& label, int value, int maximum)
    {
        progress.setMaximum(maximum);
        progress.setLabelText(label);
        progress.setValue(value);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    const bool saved = mob->saveAs(destination, updateProgress);
    progress.setValue(progress.maximum());
    return saved;
}

void CView::unloadActiveMob()
{
    unloadMobDocument(m_activeMob);
}

bool CView::unloadMobDocument(CMob* mob, bool promptForSave)
{
    if(!mob || !m_aMob.contains(mob))
        return false;
    prepareDocumentTransition();
    if(!closeMobParameterWindow(mob))
        return false;
    if(!closeMobScriptWindow(mob))
        return false;
    if(promptForSave && mob->isDirty())
    {
        const QMessageBox::StandardButton reply = QMessageBox::question(this,
            tr("Unload MOB"),
            tr("%1 has unsaved changes.\nDo you want to save changes?").arg(mob->mobName()),
            QMessageBox::Save | QMessageBox::No | QMessageBox::Cancel);
        if(reply == QMessageBox::Save && !saveMobDocument(mob, mob->filePath()))
            return false;
        if(reply == QMessageBox::Cancel)
            return false;
    }
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;

    const int removedIndex = m_aMob.indexOf(mob);
    const bool removedActive = mob == m_activeMob;
    const bool removedEnvironment = mob == m_environmentMob;
    ei::log(eLogInfo, "unloading mob " + mob->mobName());
    if(m_pUndoStack)
        m_pUndoStack->clear();
    m_hiddenMobs.remove(mob);
    m_tendriEmitters.clear();
    m_vulcanSmokeEmitters.clear();
    m_fireballEmitters.clear();
    m_fireBlastEmitters.clear();
    m_healingEmitters.clear();
    m_poisonFogEmitters.clear();
    m_geyserEmitters.clear();
    m_mushroomEmitters.clear();
    m_blood1Emitters.clear();
    m_a4BloodEmitters.clear();
    m_zoneExitEmitters.clear();
    m_pathEmitters.clear();
    m_pathDestinationEmitters.clear();
    m_moshkaEmitters.clear();
    m_portalStarEmitters.clear();
    m_portalEmitters.clear();
    m_cylinderEmitters.clear();
    m_starFlashEmitters.clear();
    m_sparksEmitters.clear();
    m_visionStarEmitters.clear();
    m_regenerationEmitters.clear();
    m_silenceEmitters.clear();
    m_feeblemindEmitters.clear();
    m_feetCloudEmitters.clear();
    m_ballStarsEmitters.clear();
    m_curseStarsEmitters.clear();
    m_startTransEmitters.clear();
    m_castingGatherEmitters.clear();
    m_castingRoundEmitters.clear();
    m_castingFailedEmitters.clear();
    m_fireArrowEmitters.clear();
    m_acidRayEmitters.clear();
    m_blueGasEmitters.clear();
    m_tornadoEmitters.clear();
    m_lightningBlastEmitters.clear();
    m_castingEmitters.clear();
    m_nukeEmitters.clear();
    m_bansheeCastingEmitters.clear();
    m_aMob.removeAt(removedIndex);
    if(removedEnvironment)
        m_environmentMob = m_aMob.isEmpty() ? nullptr : m_aMob.first();
    m_scriptWorkspaces->unregisterMob(mob);
    emit unloadMob(mob);
    delete mob;

    if(removedActive)
    {
        CMob* next = nullptr;
        if(!m_aMob.isEmpty())
            next = m_aMob.at(qMin(removedIndex, m_aMob.size() - 1));
        changeCurrentMob(next);
    }
    else
    {
        collectObjectTreeData();
        viewParameters();
        emit loadedMapStateChanged();
    }
    if(m_aMob.isEmpty())
    {
        m_caveEnvironment = false;
        m_shadowInitialized = false;
        m_staticShadowFingerprint.clear();
        m_farStaticShadowFingerprint.clear();
        makeCurrent();
        CTextureList::getInstance()->clearTransientVisualCache();
    }
    ei::log(eLogInfo, "Mob unloaded");
    refreshAiGraphPreviewSource();
    emit mobStateChanged();
    return true;
}

void CView::openActiveMobEditParams()
{
    if(nullptr == m_activeMob)
        return;

    CMobParameters* pParamWindow = nullptr;

    foreach(pParamWindow, m_arrParamWindow)
    {
        if(m_activeMob == pParamWindow->mob())
        {
            qDebug() << "mob already opened";
            return;
        }
    }

    auto pParam = new CMobParameters(this, m_activeMob, this);
    QObject::connect(this, SIGNAL(unloadMob(CMob*)), pParam, SLOT(onMobUnload(CMob*)));
    QObject::connect(pParam, SIGNAL(editFinishedSignal(CMobParameters*)), this, SLOT(onMobParamEditFinished(CMobParameters*)));
    m_arrParamWindow.append(pParam);
    pParam->show();
}

void CView::openActiveMobScript()
{
    if(!m_activeMob)
        return;
    QPointer<CMobScriptWindow> existing = m_scriptWindows.value(m_activeMob);
    if(existing)
    {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    CMob* mob = m_activeMob;
    CMobScriptWindow* window = new CMobScriptWindow(mob, this, this);
    m_scriptWindows.insert(mob, window);
    connect(this, &CView::mobScriptChanged, window, &CMobScriptWindow::onMobScriptChanged);
    connect(this, &CView::externalMobScriptImported,
            window, &CMobScriptWindow::onExternalMobScriptImported);
    connect(window, &QObject::destroyed, this, [this, mob]() { m_scriptWindows.remove(mob); });
    window->show();
    window->raise();
    window->activateWindow();
}

void CView::openExternalMobScript(CMob* mob)
{
    if(mob)
    {
        QString editorExecutable;
        if(m_pSettings)
        {
            if(COptString* option = dynamic_cast<COptString*>(m_pSettings->opt("externalEditorPath")))
                editorExecutable = option->value();
        }
        m_scriptWorkspaces->openEditor(mob, editorExecutable);
    }
}

void CView::commitMobParameterEdit(CMob* mob, const SMobParameterState& before,
                                   const SMobParameterState& after)
{
    if(!mob || before == after)
    {
        setDurty(mob);
        return;
    }
    m_pUndoStack->push(new CChangeMobParametersCommand(this, mob, before, after));
}

void CView::commitMobScriptEdit(CMob* mob, const QString& storedScript,
                                const QString& commandText)
{
    if(!mob || mob->script() == storedScript || !m_pUndoStack)
        return;
    m_pUndoStack->push(new CChangeMobScriptCommand(
        this, mob, mob->script(), storedScript, commandText));
}

void CView::notifyMobScriptChanged(CMob* mob)
{
    if(!mob)
        return;
    QTextCodec* cp1251 = QTextCodec::codecForName("Windows-1251");
    emit mobScriptChanged(mob, cp1251->toUnicode(mob->script().toLatin1()));
}

void CView::importExternalMobScript(CMob* mob, const QString& storedScript,
                                    const QString& displayScript)
{
    commitMobScriptEdit(mob, storedScript, tr("Import external MOB script"));
    // An accepted import is authoritative. If the internal editor happens to
    // be open (for example because the managed file was edited directly), show
    // the imported document state instead of retaining a stale local draft.
    if(mob && mob->script() == storedScript)
        emit externalMobScriptImported(mob, displayScript);
}

bool CView::closeMobScriptWindow(CMob* mob)
{
    QPointer<CMobScriptWindow> window = m_scriptWindows.value(mob);
    return !window || window->requestClose();
}

bool CView::closeMobParameterWindow(CMob* mob)
{
    const QList<CMobParameters*> windows = m_arrParamWindow;
    for(CMobParameters* window : windows)
    {
        if(window && window->mob() == mob && !window->close())
            return false;
    }
    return true;
}

bool CView::closeAllMobParameterWindows()
{
    const QList<CMobParameters*> windows = m_arrParamWindow;
    for(CMobParameters* window : windows)
    {
        if(window && !window->close())
            return false;
    }
    return true;
}

bool CView::closeAllMobScriptWindows()
{
    const QList<QPointer<CMobScriptWindow>> windows = m_scriptWindows.values();
    for(const QPointer<CMobScriptWindow>& window : windows)
    {
        if(window && !window->requestClose())
            return false;
    }
    return true;
}

void CView::unloadMob(QString mobName)
{
    if(m_aMob.isEmpty())
        return;

    prepareDocumentTransition();
    if(mobName.isEmpty())
    {
        if(!closeAllMobParameterWindows())
            return;
        if(!closeAllMobScriptWindows())
            return;
    }
    else
    {
        CMob* closingMob = mob(mobName);
        if(closingMob && !closeMobParameterWindow(closingMob))
            return;
        if(closingMob && !closeMobScriptWindow(closingMob))
            return;
    }
    if(m_pUndoStack)
        m_pUndoStack->clear();
    m_tendriEmitters.clear();
    m_vulcanSmokeEmitters.clear();
    m_fireballEmitters.clear();
    m_fireBlastEmitters.clear();
    m_healingEmitters.clear();
    m_poisonFogEmitters.clear();
    m_geyserEmitters.clear();
    m_mushroomEmitters.clear();
    m_blood1Emitters.clear();
    m_a4BloodEmitters.clear();
    m_zoneExitEmitters.clear();
    m_pathEmitters.clear();
    m_pathDestinationEmitters.clear();
    m_moshkaEmitters.clear();
    m_portalStarEmitters.clear();
    m_portalEmitters.clear();
    m_cylinderEmitters.clear();
    m_starFlashEmitters.clear();
    m_sparksEmitters.clear();
    m_visionStarEmitters.clear();
    m_regenerationEmitters.clear();
    m_silenceEmitters.clear();
    m_feeblemindEmitters.clear();
    m_feetCloudEmitters.clear();
    m_ballStarsEmitters.clear();
    m_curseStarsEmitters.clear();
    m_startTransEmitters.clear();
    m_castingGatherEmitters.clear();
    m_castingRoundEmitters.clear();
    m_castingFailedEmitters.clear();
    m_fireArrowEmitters.clear();
    m_acidRayEmitters.clear();
    m_blueGasEmitters.clear();
    m_tornadoEmitters.clear();
    m_lightningBlastEmitters.clear();
    m_castingEmitters.clear();
    m_nukeEmitters.clear();
    m_bansheeCastingEmitters.clear();
    ei::log(eLogInfo, "unloading mob " + mobName);
    CMob* pMob = nullptr;
    if(mobName.isEmpty())
    {
        foreach(pMob, m_aMob)
        {
            m_hiddenMobs.remove(pMob);
            m_scriptWorkspaces->unregisterMob(pMob);
            emit unloadMob(pMob);
            delete pMob;
        }
        m_aMob.clear();
        m_activeMob = nullptr;
        m_environmentMob = nullptr;
        m_caveEnvironment = false;
        m_shadowInitialized = false;
        m_staticShadowFingerprint.clear();
        m_farStaticShadowFingerprint.clear();
    }
    else
    {
        foreach(pMob, m_aMob)
        {
            if (pMob->mobName().toLower() == mobName.toLower())
            {
                const bool removedEnvironment = pMob == m_environmentMob;
                m_scriptWorkspaces->unregisterMob(pMob);
                m_hiddenMobs.remove(pMob);
                emit unloadMob(pMob);
                m_aMob.removeOne(pMob);
                delete pMob;
                if(removedEnvironment)
                    m_environmentMob = m_aMob.isEmpty() ? nullptr : m_aMob.first();
                break;
            }
        }
    }
    m_spatialIndexDirty = true;
    m_sceneFingerprintDirty = true;

    if(!m_aMob.isEmpty())
        changeCurrentMob(m_aMob.back());
    else
    {
        changeCurrentMob(nullptr);
        m_caveEnvironment = false;
        m_shadowInitialized = false;
        m_staticShadowFingerprint.clear();
        m_farStaticShadowFingerprint.clear();
        makeCurrent();
        CTextureList::getInstance()->clearTransientVisualCache();
        // Keep the view context current for the next map's terrain/resource
        // uploads; this matches the renderer's existing context lifetime.
    }

    ei::log(eLogInfo, "Mob unloaded");
    refreshAiGraphPreviewSource();
    viewParameters();
    emit mobStateChanged();
    emit selectionChanged();
}

int CView::nSelectedNodes()
{
    int n = 0;
    for(const auto& mob: m_aMob)
    {
        QList<CNode*> counted;
        for(const auto& node: mob->nodes())
            if(node->nodeState() == ENodeState::eSelect && !counted.contains(node))
            {
                counted.append(node);
                ++n;
            }
        for(const auto& node: mob->logicNodes())
            if(node->nodeState() == ENodeState::eSelect && !counted.contains(node))
            {
                counted.append(node);
                ++n;
            }
    }
    return n;
}

QList<CNode *> CView::selectedNodes()
{
    QList<CNode*> arrNode;
    for(const auto& mob: m_aMob)
    {
        for(const auto& node: mob->nodes())
            if(node->nodeState() == ENodeState::eSelect)
                arrNode.append(node);
    }
    return arrNode;
}

QList<CNode*> CView::selectionSnapshot()
{
    QList<CNode*> selection;
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        for(CNode* node : mob->nodes())
        {
            if(node && node->nodeState() == ENodeState::eSelect && !selection.contains(node))
                selection.append(node);
        }
        for(CNode* node : mob->logicNodes())
        {
            if(node && node->nodeState() == ENodeState::eSelect && !selection.contains(node))
                selection.append(node);
        }
    }
    return selection;
}

void CView::restoreSelection(const QList<CNode*>& selection)
{
    QList<CNode*> available;
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        mob->clearSelect(true);
        for(CNode* node : mob->nodes())
        {
            if(node && !available.contains(node))
                available.append(node);
        }
        for(CNode* node : mob->logicNodes())
        {
            if(node && !available.contains(node))
                available.append(node);
        }
    }

    for(CNode* node : selection)
    {
        if(node && available.contains(node) && isMobVisible(ownerMob(node)))
            node->setState(ENodeState::eSelect);
    }
    if(m_pTree)
    {
        if(selection.isEmpty())
            m_pTree->clearViewportSelectionMarker();
        else
            m_pTree->revealNode(selection.back());
    }
    viewParameters();
    update();
    emit loadedMapStateChanged();
    emit selectionChanged();
}

bool CView::selectObjectByProvenance(const QString& mobPath, quint32 nid,
                                     const QString& sectionPath)
{
    const QString wantedPath = QDir::cleanPath(QFileInfo(mobPath).absoluteFilePath());
    CMob* targetMob = nullptr;
    for(CMob* mob : m_aMob)
    {
        if(mob && QDir::cleanPath(mob->filePath().absoluteFilePath()).compare(
               wantedPath, Qt::CaseInsensitive) == 0)
        {
            targetMob = mob;
            break;
        }
    }
    if(!targetMob)
        return false;

    CNode* target = nullptr;
    QList<CNode*> nidMatches;
    for(CNode* node : targetMob->nodes())
    {
        if(!node || node->mapId() != nid)
            continue;
        nidMatches.append(node);
        if(!sectionPath.isEmpty() && node->rawSourceObjectPath() == sectionPath)
            target = node;
    }
    if(!target && nidMatches.size() == 1)
        target = nidMatches.first();
    if(!target)
        return false;

    const QList<CNode*> before = selectionSnapshot();
    changeCurrentMob(targetMob);
    restoreSelection(QList<CNode*>() << target);
    recordSelectionChange(before, tr("Select object from validation"));
    return true;
}

void CView::clearObjectSelection()
{
    if(m_aMob.isEmpty())
        return;
    const QList<CNode*> before = selectionSnapshot();
    if(before.isEmpty())
    {
        if(m_pTree)
            m_pTree->clearViewportSelectionMarker();
        return;
    }
    for(CMob* mob : m_aMob)
        if(mob) mob->clearSelect(true);
    recordSelectionChange(before, "Clear selection");
    if(m_pTree)
        m_pTree->clearViewportSelectionMarker();
    viewParameters();
    update();
    emit loadedMapStateChanged();
}

void CView::recordSelectionChange(const QList<CNode*>& before, const QString& text)
{
    const QList<CNode*> after = selectionSnapshot();
    QSet<CNode*> beforeSet;
    QSet<CNode*> afterSet;
    for(CNode* node : before)
        beforeSet.insert(node);
    for(CNode* node : after)
        afterSet.insert(node);
    if(beforeSet == afterSet)
        return;

    m_pUndoStack->push(new CSelectionCommand(this, before, after, text));
    emit selectionChanged();
}

void CView::setRandomComplection(const EObjParam param, const float min, const float max)
{
    if(nullptr == m_activeMob)
        return;

    QList<CNode*> targets;
    for(CNode* node : m_activeMob->nodes())
    {
        if(node && node->nodeState() == ENodeState::eSelect)
            targets.append(node);
    }
    if(targets.isEmpty())
        return;
    const bool grouped = targets.size() > 1;
    if(grouped)
        m_pUndoStack->beginMacro(QString("Randomize %1 objects").arg(targets.size()));

    CNode* pNode = nullptr;
    foreach(pNode, targets)
    {
        float value = util::randomFloat(min ,max);
        QSharedPointer<propFloat> complexity(new propFloat(param, value));
        CChangeProp* pChanger = new CChangeProp(this, pNode, complexity);
        QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
        //QObject::connect(pChanger, SIGNAL(updatePosOnLand(CNode*)), this, SLOT(landPositionUpdate(CNode*)));
        m_pUndoStack->push(pChanger);
    }
    if(grouped)
        m_pUndoStack->endMacro();
    updateParameter(param);
}

void CView::resetSelectedId()
{
    if(nullptr == m_activeMob)
        return;

    // get MOB ranges - is a high priority. Chck if valid data (10+)
    QVector<SRange> arrRange = m_activeMob->ranges(!m_activeMob->isQuestMob());
    if(arrRange.isEmpty())
    {
        ei::log(eLogWarning, "not found available MOB ID range to reset IDs");
        QMessageBox::warning(this, "Warning", "Not found available MOB ID range to reset IDs. Check MOB parameters,");
        return;
    }
    for(auto& range: arrRange)
    {
        if(range.minRange < 10)
            range.minRange = 10;
        if(range.maxRange <= range.minRange)
        {
            ei::log(eLogWarning, "Incorrect MOB ranges:"+QString::number(range.minRange) + ":" + QString::number(range.maxRange));
            break;
        }
    }
    // collect busy IDs (skip ID from selected objects)
    QVector<uint> arrBusyId;
    QVector<uint> arrNodes;
    CNode* pNode = nullptr;
    foreach(pNode, m_activeMob->nodes())
    {
        if (pNode->nodeState() == ENodeState::eSelect)
            arrNodes.append(pNode->mapId());
        else
        {
            arrBusyId.append(pNode->mapId());
        }
    }
    if(arrNodes.isEmpty())
        return;
    if(!confirmScriptReferencedObjectIds(arrNodes, tr("reset these object IDs")))
        return;
    // find range of free IDs
    std::sort(arrBusyId.begin(), arrBusyId.end());
    uint min;
    uint max;
    uint startId(0);
    const auto nElem = arrNodes.size();
    bool bFound = false;
    for(auto& range: arrRange)
    {
        if(nElem > int(range.maxRange-range.minRange))
            continue; // skip too small ranges
        min = range.minRange;
        max = min;
        for(int i(0); i<arrBusyId.size(); ++i)
        {
            if(arrBusyId[i]<min)
                continue; //filter left-side value
            if(arrBusyId[i]==min)
            {// filter conflict value
                ++min;
                max = min;
                continue;
            }
            if(arrBusyId[i]>=range.maxRange)
                break; //filter right-side value
            if(arrBusyId[i] > max)
            { // found value in range
                max = arrBusyId[i];
            }
            else
            {
                continue;
            }
            if(int(max-min) >= nElem)
            {
                startId = min;
                bFound = true;
                break;
            }
            else
            {
                min = arrBusyId[i]+1;
                max = min;
            }
        }
        if(bFound)
            break;
        //not found between busy ids
        if(int(range.maxRange - max) >= nElem)
        {
            startId = max;
            break;
        }
    }

    QMap<uint, uint> reconnectId;
    for(int i(0); i<nElem; ++i)
    {
        reconnectId[arrNodes[i]] = startId;
        ++startId;
    }
    for(auto pair: reconnectId.toStdMap())
        qDebug() << pair.first << ": " << pair.second;
    CResetIdCommand* pReset = new CResetIdCommand(this, reconnectId);
    QObject::connect(pReset, SIGNAL(updateParam()), this, SLOT(viewParameters()));
    m_pUndoStack->push(pReset);
    m_activeMob->setDirty();
    // update tree view?
}


void CView::openMapParameters()
{
    if(m_mprSettingsDock)
    {
        refreshMprSettingsDock();
        m_mprSettingsDock->show();
        m_mprSettingsDock->raise();
        return;
    }

    QMainWindow* host = qobject_cast<QMainWindow*>(window());
    if(!host)
        return;

    QDockWidget* dock = new QDockWidget(tr("MPR Settings"), host);
    m_mprSettingsDock = dock;
    dock->setObjectName(QStringLiteral("mprSettingsDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->setWindowFlags(dock->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    QWidget* container = new QWidget(dock);
    dock->setWidget(container);
    QVBoxLayout* containerLayout = new QVBoxLayout(container);
    QStackedWidget* statePages = new QStackedWidget(container);
    containerLayout->addWidget(statePages);
    QLabel* noMpr = new QLabel(tr("No MPR loaded."), statePages);
    noMpr->setAlignment(Qt::AlignCenter);
    noMpr->setWordWrap(true);
    statePages->addWidget(noMpr);
    QWidget* panel = new QWidget(statePages);
    statePages->addWidget(panel);
    QVBoxLayout* layout = new QVBoxLayout(panel);
    QTabWidget* tabs = new QTabWidget(panel);
    layout->addWidget(tabs, 1);

    QWidget* summaryPage = new QWidget(tabs);
    QFormLayout* summaryForm = new QFormLayout(summaryPage);
    QLabel* sourceValue = new QLabel(summaryPage);
    sourceValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourceValue->setWordWrap(true);
    QLabel* namespaceValue = new QLabel(summaryPage);
    namespaceValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QLabel* dimensionsValue = new QLabel(summaryPage);
    QLabel* rangeValue = new QLabel(summaryPage);
    QLabel* scaleValue = new QLabel(summaryPage);
    QLabel* materialsValue = new QLabel(summaryPage);
    summaryForm->addRow(tr("Source MPR"), sourceValue);
    summaryForm->addRow(tr("Embedded namespace"), namespaceValue);
    summaryForm->addRow(tr("Dimensions"), dimensionsValue);
    summaryForm->addRow(tr("Height range"), rangeValue);
    summaryForm->addRow(tr("Vertical scale (maxZ)"), scaleValue);
    summaryForm->addRow(tr("Atlas / liquid materials"), materialsValue);
    QLabel* summaryScaleExplanation = new QLabel(tr(
        "maxZ maps the complete 0-65535 raw range into world height. Changing it either "
        "scales the landscape or requantizes every height. Use Vertical scale to choose "
        "the migration policy explicitly."), summaryPage);
    summaryScaleExplanation->setWordWrap(true);
    summaryForm->addRow(summaryScaleExplanation);
    tabs->addTab(summaryPage, tr("Summary"));

    QWidget* heightsPage = new QWidget(tabs);
    QVBoxLayout* heightsLayout = new QVBoxLayout(heightsPage);
    QFormLayout* operationForm = new QFormLayout;
    QComboBox* operation = new QComboBox(heightsPage);
    operation->addItem(tr("Offset all heights"));
    operation->addItem(tr("Flatten whole map"));
    operation->addItem(tr("Clamp to range"));
    operation->addItem(tr("Normalize / remap range"));
    operationForm->addRow(tr("Operation"), operation);

    QStackedWidget* parameters = new QStackedWidget(heightsPage);
    QSpinBox* offset = new QSpinBox(parameters);
    offset->setRange(-65535, 65535);
    offset->setSingleStep(100);
    QWidget* offsetPage = new QWidget(parameters);
    QFormLayout* offsetForm = new QFormLayout(offsetPage);
    offsetForm->addRow(tr("Raw height delta"), offset);
    parameters->addWidget(offsetPage);

    QSpinBox* flatten = new QSpinBox(parameters);
    flatten->setRange(0, 65535);
    QWidget* flattenPage = new QWidget(parameters);
    QFormLayout* flattenForm = new QFormLayout(flattenPage);
    flattenForm->addRow(tr("Target raw height"), flatten);
    parameters->addWidget(flattenPage);

    QSpinBox* clampMinimum = new QSpinBox(parameters);
    QSpinBox* clampMaximum = new QSpinBox(parameters);
    clampMinimum->setRange(0, 65535);
    clampMaximum->setRange(0, 65535);
    QWidget* clampPage = new QWidget(parameters);
    QFormLayout* clampForm = new QFormLayout(clampPage);
    clampForm->addRow(tr("Minimum raw height"), clampMinimum);
    clampForm->addRow(tr("Maximum raw height"), clampMaximum);
    parameters->addWidget(clampPage);

    QSpinBox* normalizeMinimum = new QSpinBox(parameters);
    QSpinBox* normalizeMaximum = new QSpinBox(parameters);
    normalizeMinimum->setRange(0, 65535);
    normalizeMaximum->setRange(0, 65535);
    QWidget* normalizePage = new QWidget(parameters);
    QFormLayout* normalizeForm = new QFormLayout(normalizePage);
    normalizeForm->addRow(tr("Output minimum"), normalizeMinimum);
    normalizeForm->addRow(tr("Output maximum"), normalizeMaximum);
    parameters->addWidget(normalizePage);
    operationForm->addRow(parameters);
    heightsLayout->addLayout(operationForm);

    QLabel* operationExplanation = new QLabel(tr(
        "These operations retain the current sector dimensions and vertex count. "
        "They are applied as one undoable canonical terrain command and saved through "
        "the validated MPR transaction."), heightsPage);
    operationExplanation->setWordWrap(true);
    heightsLayout->addWidget(operationExplanation);
    QLabel* preview = new QLabel(heightsPage);
    preview->setFrameShape(QFrame::StyledPanel);
    preview->setWordWrap(true);
    preview->setMinimumHeight(105);
    heightsLayout->addWidget(preview);
    QPushButton* apply = new QPushButton(tr("Apply undoable operation"), heightsPage);
    heightsLayout->addWidget(apply, 0, Qt::AlignRight);
    heightsLayout->addStretch(1);
    tabs->addTab(heightsPage, tr("Height operations"));

    QWidget* scalePage = new QWidget(tabs);
    QVBoxLayout* scaleLayout = new QVBoxLayout(scalePage);
    QFormLayout* scaleForm = new QFormLayout;
    QDoubleSpinBox* scaleTarget = new QDoubleSpinBox(scalePage);
    scaleTarget->setDecimals(6);
    scaleTarget->setRange(0.000001, 1000000000.0);
    scaleTarget->setSingleStep(1.0);
    QComboBox* scalePolicy = new QComboBox(scalePage);
    scalePolicy->addItem(tr("Scale terrain with vertical range"));
    scalePolicy->addItem(tr("Preserve world heights by requantizing"));
    QCheckBox* acceptScaleClamp = new QCheckBox(
        tr("Allow vertices above the new range to clamp"), scalePage);
    scaleForm->addRow(tr("New maxZ"), scaleTarget);
    scaleForm->addRow(tr("Policy"), scalePolicy);
    scaleForm->addRow(acceptScaleClamp);
    scaleLayout->addLayout(scaleForm);
    QLabel* scaleExplanation = new QLabel(tr(
        "Scale terrain keeps every raw sample and changes world-space heights. "
        "Preserve world heights requantizes raw samples and may introduce rounding. "
        "Clamping is never accepted silently."), scalePage);
    scaleExplanation->setWordWrap(true);
    scaleLayout->addWidget(scaleExplanation);
    QLabel* scalePreview = new QLabel(scalePage);
    scalePreview->setFrameShape(QFrame::StyledPanel);
    scalePreview->setWordWrap(true);
    scalePreview->setMinimumHeight(110);
    scaleLayout->addWidget(scalePreview);
    QPushButton* applyScale = new QPushButton(tr("Apply undoable scale migration"), scalePage);
    scaleLayout->addWidget(applyScale, 0, Qt::AlignRight);
    QLabel* resizeDeferred = new QLabel(tr(
        "Crop/expand remains disabled: resizing changes SEC members and archive hash "
        "chains and will be introduced as a separate Save As-only transaction."), scalePage);
    resizeDeferred->setWordWrap(true);
    scaleLayout->addWidget(resizeDeferred);
    scaleLayout->addStretch(1);
    tabs->addTab(scalePage, tr("Vertical scale"));

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, panel);
    QPushButton* atlas = buttons->addButton(tr("Open Atlas Tiles..."),
                                            QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dock, &QDockWidget::hide);
    connect(atlas, &QPushButton::clicked, dock, [this]() { openAtlasTiles(); });

    const auto refreshSummary = [this, dimensionsValue, rangeValue, scaleValue,
                                 materialsValue, flatten, clampMinimum,
                                 clampMaximum, normalizeMinimum,
                                 normalizeMaximum]() {
        const SMprMapInfo& map = m_mprDocument.mapInfo();
        const STerrainHeightRange range = m_terrainEdit.heightRange();
        dimensionsValue->setText(tr("%1 x %2 sectors; %3 x %4 tiles; %5 x %6 vertices")
            .arg(map.sectorCountX).arg(map.sectorCountY)
            .arg(map.sectorCountX * 16).arg(map.sectorCountY * 16)
            .arg(m_terrainEdit.vertexCountX()).arg(m_terrainEdit.vertexCountY()));
        rangeValue->setText(tr("raw %1 to %2; world %3 to %4")
            .arg(range.minimumRaw).arg(range.maximumRaw)
            .arg(range.minimumWorld, 0, 'f', 4)
            .arg(range.maximumWorld, 0, 'f', 4));
        scaleValue->setText(tr("%1 world units; %2 per raw step")
            .arg(m_terrainEdit.maxZ(), 0, 'g', 8)
            .arg(range.worldUnitsPerRawStep, 0, 'g', 8));
        materialsValue->setText(tr("%1 atlas slots; %2 liquid material record(s)")
            .arg(map.tileCount).arg(map.materialCount));
        flatten->setValue(range.minimumRaw);
        clampMinimum->setValue(range.minimumRaw);
        clampMaximum->setValue(range.maximumRaw);
        normalizeMinimum->setValue(range.minimumRaw);
        normalizeMaximum->setValue(range.maximumRaw);
    };

    const auto makeScaleCommand = [this, scaleTarget, scalePolicy,
                                   acceptScaleClamp](CValidationReport* report) {
        const ETerrainVerticalScalePolicy policy = scalePolicy->currentIndex() == 0
            ? ETerrainVerticalScalePolicy::ScaleTerrain
            : ETerrainVerticalScalePolicy::PreserveWorldHeights;
        return m_terrainEdit.makeVerticalScale(float(scaleTarget->value()), policy,
                                               acceptScaleClamp->isChecked(), report);
    };
    const auto refreshScalePreview = [this, makeScaleCommand, scalePreview,
                                      applyScale, acceptScaleClamp, scalePolicy]() {
        acceptScaleClamp->setEnabled(scalePolicy->currentIndex() == 1);
        CValidationReport report;
        const STerrainVerticalScaleCommand command = makeScaleCommand(&report);
        if(!command.valid)
        {
            scalePreview->setText(report.toText().trimmed().isEmpty()
                ? tr("The requested migration is invalid.")
                : report.toText().trimmed());
            applyScale->setEnabled(false);
            return;
        }
        if(command.isEmpty())
        {
            scalePreview->setText(tr("No change: maxZ already has this value."));
            applyScale->setEnabled(false);
            return;
        }
        const STerrainHeightRange before = m_terrainEdit.heightRange();
        if(command.policy == ETerrainVerticalScalePolicy::ScaleTerrain)
        {
            scalePreview->setText(tr(
                "Raw height samples remain unchanged; packed normals are rebuilt in every SEC.\n"
                "World height range: %1-%2 -> %3-%4.\n"
                "The MP maxZ field and derived SEC normals change; terrain and navigation scale vertically.")
                .arg(before.minimumWorld, 0, 'f', 4)
                .arg(before.maximumWorld, 0, 'f', 4)
                .arg(float(before.minimumRaw) * command.afterMaxZ / 65535.0f, 0, 'f', 4)
                .arg(float(before.maximumRaw) * command.afterMaxZ / 65535.0f, 0, 'f', 4));
        }
        else
        {
            scalePreview->setText(tr(
                "%1 vertex/vertices will be requantized across the affected SEC members.\n"
                "%2 value(s) round; %3 value(s) clamp. Maximum world-height error: %4.")
                .arg(command.heights.deltas.size()).arg(command.roundedVertexCount)
                .arg(command.clampedVertexCount)
                .arg(command.maximumWorldError, 0, 'g', 8));
        }
        applyScale->setEnabled(true);
    };

    const auto makeCommand = [this, operation, offset, flatten, clampMinimum,
                              clampMaximum, normalizeMinimum,
                              normalizeMaximum](CValidationReport* report) {
        switch(operation->currentIndex())
        {
        case 0: return m_terrainEdit.makeGlobalOffset(float(offset->value()), report);
        case 1: return m_terrainEdit.makeGlobalFlatten(quint16(flatten->value()));
        case 2: return m_terrainEdit.makeGlobalClamp(
                    quint16(clampMinimum->value()), quint16(clampMaximum->value()), report);
        default: return m_terrainEdit.makeGlobalNormalize(
                    quint16(normalizeMinimum->value()), quint16(normalizeMaximum->value()), report);
        }
    };
    const auto refreshPreview = [this, makeCommand, preview, apply]() {
        CValidationReport report;
        const STerrainHeightCommand command = makeCommand(&report);
        if(report.hasErrors())
        {
            preview->setText(report.toText());
            apply->setEnabled(false);
            return;
        }
        if(command.isEmpty())
        {
            preview->setText(tr("No vertices would change. The MPR will remain clean."));
            apply->setEnabled(false);
            return;
        }
        const STerrainEditDiagnostics diagnostics = m_terrainEdit.diagnose(command);
        if(!diagnostics.valid || command.requiresMaxZDecision())
        {
            preview->setText(tr(
                "The requested operation would exceed the current raw range. "
                "It is refused until you choose an explicit clamp or maxZ migration policy."));
            apply->setEnabled(false);
            return;
        }
        preview->setText(tr(
            "%1 unique vertices across %2 sector(s) would change.\n"
            "Changed values: raw %3-%4 -> %5-%6.\n"
            "The vertex count remains %7 and the operation is undoable.")
            .arg(diagnostics.changedVertexCount).arg(diagnostics.affectedSectorCount)
            .arg(diagnostics.minimumBefore).arg(diagnostics.maximumBefore)
            .arg(diagnostics.minimumAfter).arg(diagnostics.maximumAfter)
            .arg(m_terrainEdit.vertexCountX() * m_terrainEdit.vertexCountY()));
        apply->setEnabled(true);
    };
    connect(operation, QOverload<int>::of(&QComboBox::currentIndexChanged),
            parameters, &QStackedWidget::setCurrentIndex);
    connect(operation, QOverload<int>::of(&QComboBox::currentIndexChanged),
            dock, [refreshPreview](int) { refreshPreview(); });
    const QList<QSpinBox*> editors = {offset, flatten, clampMinimum, clampMaximum,
                                      normalizeMinimum, normalizeMaximum};
    for(QSpinBox* editor : editors)
        connect(editor, QOverload<int>::of(&QSpinBox::valueChanged),
                dock, [refreshPreview](int) { refreshPreview(); });
    connect(apply, &QPushButton::clicked, dock,
            [this, dock, makeCommand, refreshPreview, refreshSummary]() {
        if(!m_pLand->isMprLoad() || !m_terrainEditAvailable)
            return;
        CValidationReport report;
        const STerrainHeightCommand command = makeCommand(&report);
        const STerrainEditDiagnostics diagnostics = m_terrainEdit.diagnose(command);
        if(report.hasErrors() || command.isEmpty() || !diagnostics.valid
           || command.requiresMaxZDecision())
        {
            refreshPreview();
            return;
        }
        if(QMessageBox::question(dock, tr("Apply MPR height operation"),
            tr("Apply %1 to %2 vertices in %3 sector(s)?\n\n"
               "This changes the terrain preview immediately and is one undoable action.")
                .arg(command.description)
                .arg(diagnostics.changedVertexCount)
                .arg(diagnostics.affectedSectorCount)) != QMessageBox::Yes)
            return;
        m_pUndoStack->push(new CTerrainHeightPreviewCommand(this, command));
        refreshSummary();
        refreshPreview();
    });
    connect(scaleTarget, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            dock, [refreshScalePreview](double) { refreshScalePreview(); });
    connect(scalePolicy, QOverload<int>::of(&QComboBox::currentIndexChanged),
            dock, [refreshScalePreview](int) { refreshScalePreview(); });
    connect(acceptScaleClamp, &QCheckBox::toggled,
            dock, [refreshScalePreview](bool) { refreshScalePreview(); });
    connect(applyScale, &QPushButton::clicked, dock,
            [this, dock, makeScaleCommand, refreshScalePreview]() {
        CValidationReport report;
        const STerrainVerticalScaleCommand command = makeScaleCommand(&report);
        if(!command.valid || command.isEmpty())
        {
            refreshScalePreview();
            return;
        }
        const QString detail = command.policy == ETerrainVerticalScalePolicy::ScaleTerrain
            ? tr("Raw height samples remain unchanged; world heights scale with maxZ and derived SEC normals are rebuilt.")
            : tr("%1 vertices will be requantized; %2 will clamp; maximum world-height error is %3.")
                .arg(command.heights.deltas.size()).arg(command.clampedVertexCount)
                .arg(command.maximumWorldError, 0, 'g', 8);
        if(QMessageBox::question(dock, tr("Apply vertical-scale migration"),
            tr("Change maxZ from %1 to %2?\n\n%3\n\n"
               "This is one undoable action and will be saved through staged MPR validation.")
                .arg(command.beforeMaxZ, 0, 'g', 8)
                .arg(command.afterMaxZ, 0, 'g', 8).arg(detail)) != QMessageBox::Yes)
            return;
        m_pUndoStack->push(new CTerrainVerticalScalePreviewCommand(this, command));
    });
    m_refreshMprSettingsDock = [this, statePages, panel, sourceValue,
                                namespaceValue, refreshSummary, refreshPreview,
                                scaleTarget, refreshScalePreview]() {
        const bool loaded = m_pLand->isMprLoad() && m_terrainEditAvailable;
        statePages->setCurrentIndex(loaded ? 1 : 0);
        panel->setEnabled(loaded);
        if(!loaded)
            return;
        sourceValue->setText(m_pLand->filePath().absoluteFilePath());
        namespaceValue->setText(m_mprDocument.mapInfo().innerNamespace);
        refreshSummary();
        refreshPreview();
        {
            const QSignalBlocker blocker(scaleTarget);
            scaleTarget->setValue(m_terrainEdit.maxZ());
        }
        refreshScalePreview();
    };
    connect(m_pUndoStack, &QUndoStack::indexChanged, dock,
            [this](int) { refreshMprSettingsDock(); });
    refreshMprSettingsDock();
    host->addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->resize(620, 520);
    dock->show();
    dock->raise();
}

void CView::closeMprSettingsDock()
{
    m_refreshMprSettingsDock = {};
    if(!m_mprSettingsDock)
        return;
    delete m_mprSettingsDock.data();
    m_mprSettingsDock = nullptr;
}

void CView::refreshMprSettingsDock()
{
    if(m_refreshMprSettingsDock)
        m_refreshMprSettingsDock();
}

void CView::openAtlasTiles()
{
    if(!m_pLand->isMprLoad())
        return;

    if(m_pTileForm->isVisible())
    {
        m_pTileForm->show();
        return; // dont update params if window already opened
    }

    updateTileForm();
    m_pTileForm->show();
}

void CView::pickTile(QVector3D posOnLand, bool bLand)
{
    if(!m_pLand->isMprLoad())
        return;

    CTile* pTile = nullptr;
    STileLocation tileLoc;
    const bool pickLand = bLand || (!bLand && m_terrainPaintProjectionLand);
    if(!m_pLand->pickTile(posOnLand, pTile, tileLoc, pickLand))
        return;

    m_pTileForm->selectTile(pTile->tileIndex());
    m_pTileForm->setTileRotation(pTile->tileRotation());
    if(m_landscapeTools && m_terrainEditAvailable)
    {
        const int globalX = tileLoc.xSec * 16 + tileLoc.col;
        const int globalY = tileLoc.ySec * 16 + tileLoc.row;
        bool found = false;
        const quint16 packed = m_terrainEdit.packedTile(globalX, globalY,
            bLand ? ETerrainTileLayer::Land : ETerrainTileLayer::Overlay, &found);
        const ETerrainTileLayer layer = bLand
            ? ETerrainTileLayer::Land : ETerrainTileLayer::Overlay;
        const CTerrainTransitionCatalog& catalog = !bLand
            ? m_terrainAtlasAnalysis.overlayCatalogWithLandFamilies()
            : m_terrainAtlasAnalysis.catalog(layer);
        const STerrainTransitionSignature signature = found
            ? catalog.signatureForPacked(packed, &found)
            : STerrainTransitionSignature();
        if(found && signature.northWest == signature.northEast
           && signature.northWest == signature.southEast
           && signature.northWest == signature.southWest)
        {
            if(m_landscapeTools->selectPaintTerrainType(signature.northWest))
                emit updateMsg(tr("Sampled visual terrain material %1.").arg(signature.northWest));
            else
                emit updateMsg(tr("This atlas family is reserved for underwater or internal transitions and is not a land brush."));
        }
        else if(found)
            emit updateMsg(tr("This is a transition tile; sample a uniform tile to choose one material."));
    }
    if(!bLand)
        m_pTileForm->setActiveMatIndex(pTile->materialIndex());

    m_pPreviewTile->updateTile(*pTile, pTile->tileIndex(), pTile->tileRotation(), pTile->materialIndex(), tileLoc.xSec, tileLoc.ySec);
}

void CView::setTile(QVector3D posOnLand, bool bLand)
{
    if(!m_pLand->isMprLoad() || !m_terrainEditAvailable || !m_landscapeTools)
        return;

    CTile* pTile = nullptr;
    STileLocation tileLoc;
    // Coverage activation always resolves the logical cell from canonical
    // land. The hover/outline may sit on an existing overlay, but choosing the
    // cell through that thin/disconnected surface would make interpolated add
    // strokes skip land gaps.
    const bool pickLand = bLand || (!bLand
        && m_landscapeTools->overlaySurfaceOperation()
            == EOverlaySurfaceOperation::Activate);
    if(!m_pLand->pickTile(posOnLand, pTile, tileLoc, pickLand))
        return;

    STerrainTileBrush brush;
    brush.centerX = tileLoc.xSec * 16 + tileLoc.col;
    brush.centerY = tileLoc.ySec * 16 + tileLoc.row;
    bool preselectedMaterial = false;
    qint32 preselectedTerrainType = -1;
    const bool semanticPaint = !m_landscapeTools->manualTileSelectionEnabled()
        && (bLand || m_landscapeTools->overlaySurfaceOperation()
                        == EOverlaySurfaceOperation::PaintExisting);
    if(semanticPaint)
        preselectedTerrainType = m_landscapeTools->paintTerrainType(&preselectedMaterial);
    const QPoint detailVertex(
        tileLoc.xSec * 16 + qBound(0, qRound(posOnLand.x() / 2.0f), 16),
        tileLoc.ySec * 16 + qBound(0, qRound(posOnLand.y() / 2.0f), 16));
    const QPoint brushCenter = semanticPaint
        ? detailVertex : QPoint(qRound(brush.centerX), qRound(brush.centerY));
    if(m_tilePaintStrokeActive && m_lastTerrainPaintCenterValid
       && m_lastTerrainPaintCenter == brushCenter
       && m_lastTerrainPaintLayerLand == bLand)
        return;
    m_lastTerrainPaintCenter = brushCenter;
    m_lastTerrainPaintCenterValid = true;
    m_lastTerrainPaintLayerLand = bLand;
    brush.radius = m_landscapeTools->paintRadius();
    brush.shape = m_landscapeTools->paintShape();
    CValidationReport report;
    if(!bLand && m_landscapeTools->overlaySurfaceOperation()
                     != EOverlaySurfaceOperation::PaintExisting)
    {
        SOverlaySurfaceCommand surfaceCommand;
        if(m_landscapeTools->overlaySurfaceOperation()
           == EOverlaySurfaceOperation::Deactivate)
        {
            surfaceCommand = m_terrainEdit.makeOverlayDeactivation(brush, &report);
        }
        else
        {
            bool materialSelected = false;
            const qint16 material = m_landscapeTools->overlayMaterialIndex(&materialSelected);
            int selectedIndex = -1;
            int selectedRotation = 0;
            if(m_landscapeTools->manualTileSelectionEnabled())
                m_pTileForm->getSelectedTile(selectedIndex, selectedRotation);
            else
            {
                bool terrainSelected = false;
                const qint32 terrainType = m_landscapeTools->paintTerrainType(&terrainSelected);
                if(terrainSelected)
                    selectedIndex = m_terrainMaterialRepresentatives.value(terrainType, -1);
            }
            if(!materialSelected || selectedIndex < 0)
            {
                emit updateMsg(tr("Adding an overlay requires an existing liquid material and atlas tile family."));
                return;
            }
            surfaceCommand = m_terrainEdit.makeOverlayActivation(
                brush, material, quint8(selectedIndex / 64), quint8(selectedIndex % 64),
                quint8(selectedRotation & 3), &report);
        }
        ++m_tileStrokeActions;
        if(surfaceCommand.isEmpty())
        {
            if(report.hasErrors())
            {
                ++m_tileStrokeRefused;
                emit updateMsg(report.toText());
            }
            else
                ++m_tileStrokeNoOps;
            return;
        }
        if(!applyOverlaySurfacePreview(surfaceCommand, true))
        {
            ++m_tileStrokeRefused;
            emit updateMsg(tr("Overlay surface edit was refused; no undo entry was created."));
            return;
        }
        ++m_tileStrokeApplied;
        m_tileStrokeChangedTiles += surfaceCommand.sectors.size();
        appendOverlaySurfaceCommand(&m_overlaySurfaceStrokeCommand, surfaceCommand);
        if(surfaceCommand.heightClampedVertexCount > 0)
            emit updateMsg(tr("Added overlay surface; %1 vertex/vertices were clamped to MPR maxZ and may overlap the land until the terrain or maxZ is adjusted.")
                               .arg(surfaceCommand.heightClampedVertexCount));
        return;
    }
    const ETerrainTileLayer layer = bLand
        ? ETerrainTileLayer::Land : ETerrainTileLayer::Overlay;
    const CTerrainTransitionCatalog& manualCatalog = !bLand
            && m_landscapeTools->landFamiliesAllowedInOverlay()
        ? m_terrainAtlasAnalysis.overlayCatalogWithLandFamilies()
        : m_terrainAtlasAnalysis.catalog(layer);
    const CTerrainTransitionCatalog& semanticCatalog = m_workingTerrainAtlas
        ? m_workingTerrainAtlas->catalog()
        : (!bLand ? m_terrainAtlasAnalysis.overlayCatalogWithLandFamilies()
                  : m_terrainAtlasAnalysis.catalog(layer));
    const bool manual = m_landscapeTools->manualTileSelectionEnabled();
    qint32 terrainType = -1;
    STerrainTileCommand command;
    if(manual)
    {
        int selectedIndex = -1;
        int selectedRotation = 0;
        m_pTileForm->getSelectedTile(selectedIndex, selectedRotation);
        if(selectedIndex < 0)
        {
            emit updateMsg(tr("Choose an atlas tile before painting in manual mode."));
            return;
        }
        SMprPackedTile selected;
        selected.atlasIndex = quint8(selectedIndex / 64);
        selected.tileIndex = quint8(selectedIndex % 64);
        selected.rotation = quint8(selectedRotation & 3);
        bool layerCompatible = !m_terrainAtlasAnalysis.usable;
        if(m_terrainAtlasAnalysis.usable)
            manualCatalog.signatureForPacked(selected.encode(), &layerCompatible);
        if(!layerCompatible)
        {
            emit updateMsg(layer == ETerrainTileLayer::Land
                ? tr("That atlas slot is not a land-family tile.")
                : tr("That atlas slot is not enabled for overlay painting."));
            return;
        }
        command = m_terrainEdit.makeTilePaint(
            brush, layer, selected.atlasIndex, selected.tileIndex,
            selected.rotation);
        command.description = tr("Paint manual atlas tiles");
        command.semanticAudit.valid = true;
        command.semanticAudit.brush = brush;
        command.semanticAudit.layer = layer;
        command.semanticAudit.requestedMaterialId = -1;
        command.semanticAudit.solverMode = QStringLiteral("manual-atlas");
        for(const STerrainTileDelta& delta : command.deltas)
        {
            const QPoint point(delta.globalX, delta.globalY);
            command.semanticAudit.paintedCoreCells.append(point);
            command.semanticAudit.affectedCells.append(point);
        }
    }
    else
    {
        const bool materialSelected = preselectedMaterial;
        terrainType = preselectedTerrainType;
        if(!materialSelected)
        {
            emit updateMsg(m_terrainAtlasAnalysis.usable
                ? tr("Choose a terrain material before painting.")
                : tr("No authored slot template is registered for this atlas namespace."));
            return;
        }
        STerrainTileBrush vertexBrush = brush;
        vertexBrush.centerX = detailVertex.x();
        vertexBrush.centerY = detailVertex.y();
        STerrainTransitionSignature pure;
        pure.northWest = pure.northEast = pure.southEast = pure.southWest = terrainType;
        QMap<int, quint64> usage = workingTerrainAtlasUsage();
        if(!realizeWorkingTerrainSignature(pure, qRound(vertexBrush.centerX),
             qRound(vertexBrush.centerY), &usage, &report, nullptr,
             terrainType)
           || !syncWorkingTerrainAtlas(&report))
        {
            ++m_tileStrokeRefused;
            emit updateMsg(report.toText());
            return;
        }
        command = m_terrainEdit.makeVirtualMaterialPaint(
            qRound(brush.centerX), qRound(brush.centerY), vertexBrush,
            layer, terrainType, semanticCatalog,
            m_tileStrokeDistributionSeed, &report);
    }
    STerrainPaintAuditAction audit;
    audit.actionId = m_terrainPaintActionId++;
    audit.strokeId = m_tileBrushCommandId;
    audit.validation = report.toText();
    audit.semantic = command.semanticAudit;
    audit.deltas = command.deltas;
    ++m_tileStrokeActions;
    m_tileStrokeSolverNodes += audit.semantic.solverNodes;
    if(command.isEmpty())
    {
        audit.outcome = report.hasErrors()
            ? QStringLiteral("refused") : QStringLiteral("no-op");
        m_terrainPaintActions.append(audit);
        if(audit.outcome == QStringLiteral("refused"))
            ++m_tileStrokeRefused;
        else
            ++m_tileStrokeNoOps;
        if(report.hasErrors())
            ei::log(eLogWarning, "Semantic terrain paint refused:\n" + report.toText());
        if(!m_tilePaintStrokeActive || report.hasErrors())
            emit updateMsg(report.findings().isEmpty()
                ? tr("The terrain material is already resolved at this brush position.")
                : report.toText());
    }
    else
    {
        for(STerrainTileDelta& delta : command.deltas)
            delta.diagnosticStrokeId = m_tileBrushCommandId;
        audit.deltas = command.deltas;
        if(!applyTerrainTilePreview(command, true))
        {
            audit.outcome = QStringLiteral("refused");
            audit.validation += QStringLiteral(
                "ERROR terrain.preview_apply: The canonical preview command was not applied\n");
            m_terrainPaintActions.append(audit);
            ++m_tileStrokeRefused;
            emit updateMsg(tr("Terrain preview was refused; no undo entry was created."));
        }
        else
        {
            audit.outcome = QStringLiteral("applied");
            m_terrainPaintActions.append(audit);
            ++m_tileStrokeApplied;
            m_tileStrokeChangedTiles += audit.deltas.size();
            m_terrainPaintAudit += command.deltas;
            // Compose all stamps into one before/after command. Live preview
            // remains incremental, but undo/redo applies each final tile once
            // and rebuilds each touched sector once.
            appendTerrainTileCommand(&m_tileStrokeCommand, command);
        }
    }
    const QString actionLog = QString("Terrain paint action %1 stroke %2: %3, layer=%4 material=%5 center=%6,%7 radius=%8 core=%9 vertices=%10 affected=%11 changed=%12 solver=%13 nodes=%14")
            .arg(audit.actionId).arg(audit.strokeId).arg(audit.outcome)
            .arg(bLand ? "land" : "overlay")
            .arg(manual ? QStringLiteral("manual") : QString::number(terrainType))
            .arg(brush.centerX).arg(brush.centerY).arg(brush.radius)
            .arg(audit.semantic.paintedCoreCells.size())
            .arg(audit.semantic.paintedVertices.size())
            .arg(audit.semantic.affectedCells.size()).arg(audit.deltas.size())
            .arg(audit.semantic.solverMode).arg(audit.semantic.solverNodes);
    // The exported diagnostic retains every action. Keep synchronous workflow
    // logging off the pointer hot path and emit one summary on release.
    if(!m_tilePaintStrokeActive || audit.outcome == QStringLiteral("refused"))
        ei::log(audit.outcome == QStringLiteral("refused") ? eLogWarning : eLogInfo,
                actionLog);
}

void CView::setTile(QMap<STileLocation, STileInfo>& arrTileData)
{
    if(!m_pLand->isMprLoad() || !m_terrainEditAvailable)
        return;

    STerrainTileCommand command;
    command.description = tr("Paint terrain tiles");
    for(auto entry = arrTileData.constBegin(); entry != arrTileData.constEnd(); ++entry)
    {
        const STileLocation& location = entry.key();
        const STileInfo& requested = entry.value();
        const int globalX = location.xSec * 16 + location.col;
        const int globalY = location.ySec * 16 + location.row;
        const ETerrainTileLayer layer = location.bLand
            ? ETerrainTileLayer::Land : ETerrainTileLayer::Overlay;
        bool available = false;
        const quint16 before = m_terrainEdit.packedTile(globalX, globalY, layer, &available);
        if(!available || requested.index < 0)
            continue;
        SMprPackedTile packed;
        packed.atlasIndex = quint8(requested.index / 64);
        packed.tileIndex = quint8(requested.index % 64);
        packed.rotation = quint8(requested.rotNum & 3);
        const quint16 after = packed.encode();
        if(before == after)
            continue;
        STerrainTileDelta delta;
        delta.globalX = globalX;
        delta.globalY = globalY;
        delta.layer = layer;
        delta.before = before;
        delta.after = after;
        command.deltas.append(delta);
    }
    if(command.isEmpty())
        return;
    CValidationReport report;
    if(!m_terrainEdit.apply(command, true, &report))
    {
        ei::log(eLogWarning, "Canonical tile preview refused:\n" + report.toText());
        return;
    }
    for(const STerrainTileDelta& delta : command.deltas)
        m_pLand->previewCanonicalTile(delta.globalX, delta.globalY,
            delta.layer == ETerrainTileLayer::Land, delta.after);
    m_pLand->setDirty(m_terrainEdit.isDirty());
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag,
                              m_terrainEdit.isDirty() ? "*" : "");
    update();
}

void CView::updatePreviewTile(QVector3D posOnLand, bool bLand)
{
    if(!m_pLand->isMprLoad())
        return;

    CTile* pTile = nullptr;
    STileLocation tileLoc;
    const bool pickLand = bLand || (!bLand && m_terrainPaintProjectionLand);
    if(!m_pLand->pickTile(posOnLand, pTile, tileLoc, pickLand))
    {
        m_terrainPaintOutlineValid = false;
        return;
    }

    if(!manualTilePaintingEnabled())
    {
        // pickTile converts the point to sector-local coordinates. Match the
        // paint path by snapping that hit to the nearest global terrain vertex.
        m_terrainPaintOutlineX = tileLoc.xSec * 16
            + qBound(0, qRound(posOnLand.x() / 2.0f), 16);
        m_terrainPaintOutlineY = tileLoc.ySec * 16
            + qBound(0, qRound(posOnLand.y() / 2.0f), 16);
        m_terrainPaintOutlineValid = true;
        setPreviewTile(false);
        unsetCursor();
        update();
        return;
    }
    m_terrainPaintOutlineValid = false;

    int index = -1;
    int rotNum = 0;
    m_pTileForm->getSelectedTile(index, rotNum);
    if(index < 0)
    {
        setPreviewTile(false);
        unsetCursor();
        return;
    }

    onChangeCursorTile(m_pTileForm->tileWithRot(index));
    int matIndex;

    matIndex = m_pTileForm->activeMaterialindex();
    m_pPreviewTile->updateTile(*pTile, index, rotNum, matIndex, tileLoc.xSec, tileLoc.ySec);
    setPreviewTile(true);
}

void CView::addTileRotation(int step)
{
    int curRot = (m_pTileForm->tileRotation() + step)%4;
    if(curRot < 0)
        curRot = 3;
    m_pTileForm->setTileRotation(curRot);
}

void CView::adjustTerrainPaintSize(int steps)
{
    if(!m_landscapeTools)
        return;
    m_landscapeTools->adjustPaintSize(steps);
    update();
}

void CView::adjustTerrainPaintMaterial(int steps)
{
    if(!m_landscapeTools || manualTilePaintingEnabled())
        return;
    m_landscapeTools->adjustPaintTerrainType(steps);
    update();
}

bool CView::manualTilePaintingEnabled() const
{
    return m_landscapeTools && m_landscapeTools->manualTileSelectionEnabled();
}

float CView::terrainPaintStampSpacing() const
{
    if(!m_landscapeTools || manualTilePaintingEnabled())
        return 2.0f;
    // Terrain cells are two world units wide. Larger semantic brushes overlap
    // heavily, so proportional spacing avoids resolving the same seam network
    // for every crossed cell while retaining continuous core coverage.
    return qMax(2.0f, float(m_landscapeTools->paintRadius()) * 2.0f);
}

void CView::onLandscapeToolsShown()
{
    if(!m_terrainEditAvailable || !m_pLand || !m_pLand->isMprLoad()
       || !m_pTileForm || m_pTileForm->tileImages().isEmpty()
       || m_terrainAtlasProfile.provenance.startsWith(QStringLiteral("authored-template:"))
       || m_terrainAtlasProfile.provenance.startsWith(QStringLiteral("authored-preset:"))
       || m_terrainAtlasProfile.approved)
        return;
    const QString binding = m_mprDocument.mapInfo().innerNamespace
        + QLatin1Char(':')
        + QString::fromLatin1(CTerrainAtlasProfileInference::pixelSha256(
              m_pTileForm->tileImages()).toHex());
    if(binding == m_lastAtlasReviewPromptBinding)
        return;
    m_lastAtlasReviewPromptBinding = binding;
    const int assigned = m_terrainAtlasProfile.assignedSlotCount();
    const int total = m_terrainAtlasProfile.atlasSlotCount;
    const QString result = m_terrainAtlasAnalysis.usable
        ? tr("Endurance identified usable materials in this map's atlas. Painting is already available for the resolved tiles.")
        : tr("Endurance could not prove a safe uniform material in this atlas. This usually indicates an unusual custom tileset or a parser gap; manual tile painting remains available.");
    if(QMessageBox::question(this, tr("Review generated Material atlas"), tr(
        "%1\n\n%2 of %3 atlas tiles are assigned; %4 unusual tile(s) remain unassigned. "
        "Open Material atlas now to review names, thumbnails, and assignments? This does not modify the MPR or texture resources.")
            .arg(result).arg(assigned).arg(total).arg(qMax(0, total - assigned)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
        reviewTerrainAtlasProfile();
}

bool CView::terrainBrushProjectsToLand() const
{
    if(CScene::getInstance()->isLandTileEditMode())
        return true;
    return m_landscapeTools
        && m_landscapeTools->editMode() == ELandscapeEditMode::OverlaySurface
        && m_landscapeTools->overlaySurfaceOperation()
            == EOverlaySurfaceOperation::Activate;
}

void CView::showOutliner(bool bShow)
{
    emit showOutlinerSignal(bShow);
}

void CView::pickQuickAccessTile(int index)
{
    if(!m_pLand->isMprLoad())
        return;

    m_pTileForm->selectQuickAccessTile(index);
}

void CView::endTileBrushGroup()
{
    if(m_tilePaintStrokeActive)
    {
        if(!m_tileStrokeCommand.virtualVertices.isEmpty())
        {
            const ETerrainTileLayer layer=m_tileStrokeCommand.virtualVertices.front().layer;
            CValidationReport releaseReport;
            CTerrainVirtualMaterialField field;
            QSet<int> affected;
            QElapsedTimer releaseTimer;
            releaseTimer.start();
            qint64 resolveElapsed = 0;
            qint64 uploadElapsed = 0;
            if(!m_terrainEdit.copyVirtualMaterialField(layer,&field,false))
                releaseReport.add("terrain.virtual_stroke_field",
                    EValidationSeverity::Error,
                    "The completed stroke has no complete vertex-material field.");
            else
            {
                for(const auto& vertex:m_tileStrokeCommand.virtualVertices)
                    for(int y:{vertex.globalY-1,vertex.globalY})
                        for(int x:{vertex.globalX-1,vertex.globalX})
                            if(x>=0&&x<field.tileWidth()&&y>=0&&y<field.tileHeight())
                                affected.insert(y*field.tileWidth()+x);
                QVector<int> cells=affected.values().toVector();
                std::sort(cells.begin(),cells.end());
                struct SMissingSignature
                {
                    STerrainTransitionSignature signature;
                    int x{0};
                    int y{0};
                };
                QMap<QByteArray,SMissingSignature> missing;
                for(int cell:cells)
                {
                    const int x=cell%field.tileWidth(),y=cell/field.tileWidth();
                    bool found=false,exactAvailable=false;
                    const STerrainTransitionSignature signature=
                        field.cellSignature(x,y,&found);
                    if(!found)continue;
                    m_workingTerrainAtlas->catalog().selectForPaint(
                        signature,x,y,m_terrainSemanticSeed,&exactAvailable);
                    if(!exactAvailable)
                        missing.insert(globalTransitionKey(signature),
                                       {signature,x,y});
                }
                QProgressDialog progress(
                    tr("Resolving exact terrain blends..."), QString(),
                    0, missing.size() + 1, this);
                progress.setWindowTitle(tr("Finishing terrain stroke"));
                progress.setCancelButton(nullptr);
                progress.setWindowModality(Qt::WindowModal);
                progress.setMinimumDuration(250);
                QElapsedTimer resolveTimer;
                resolveTimer.start();
                int resolvedCells = 0;
                QMap<int,quint64> usage;
                qint64 usageScanMs=0;
                if(!missing.isEmpty())
                {
                    QElapsedTimer usageTimer;usageTimer.start();
                    usage=workingTerrainAtlasUsage();
                    usageScanMs=usageTimer.elapsed();
                }
                for(const SMissingSignature& item:missing)
                {
                    progress.setValue(resolvedCells);
                    progress.setLabelText(tr(
                        "Resolving exact terrain blends (%1 / %2)...")
                        .arg(resolvedCells + 1).arg(missing.size()));
                    QCoreApplication::processEvents(
                        QEventLoop::ExcludeUserInputEvents);
                    if(!realizeWorkingTerrainSignature(
                           item.signature,item.x,item.y,&usage,&releaseReport,
                           nullptr,
                           m_tileStrokeCommand.semanticAudit.requestedMaterialId))
                        break;
                    ++resolvedCells;
                }
                resolveElapsed = resolveTimer.elapsed();
                if(!releaseReport.hasErrors()&&!missing.isEmpty())
                {
                    progress.setValue(missing.size());
                    progress.setLabelText(tr(
                        "Uploading the resolved terrain atlas..."));
                    QCoreApplication::processEvents(
                        QEventLoop::ExcludeUserInputEvents);
                    QElapsedTimer uploadTimer;
                    uploadTimer.start();
                    if(!syncWorkingTerrainAtlas(&releaseReport))
                        releaseReport.add("terrain.working_atlas_sync",
                            EValidationSeverity::Error,
                            "The completed stroke could not install its generated exact transitions.");
                    uploadElapsed = uploadTimer.elapsed();
                    progress.setValue(missing.size() + 1);
                }
                ei::log(eLogInfo,QString(
                    "Terrain exact cache plan: affected:%1 uniqueMissing:%2 usageScanMs:%3")
                    .arg(cells.size()).arg(missing.size()).arg(usageScanMs));
            }
            ei::log(eLogInfo, QString(
                "Terrain stroke release timing: affectedCells:%1 resolveMs:%2 uploadMs:%3 totalMs:%4")
                .arg(affected.size()).arg(resolveElapsed).arg(uploadElapsed)
                .arg(releaseTimer.elapsed()));
            if(releaseReport.hasErrors())
            {
                applyTerrainTilePreview(m_tileStrokeCommand,false);
                m_tileStrokeCommand=STerrainTileCommand();
                ++m_tileStrokeRefused;
                emit updateMsg(tr("Terrain stroke refused without substituting material identities:\n%1")
                    .arg(releaseReport.toText()));
            }
            else
            {
                const CTerrainTransitionCatalog& catalog=m_workingTerrainAtlas
                    ?m_workingTerrainAtlas->catalog()
                    :(layer==ETerrainTileLayer::Land
                        ?m_terrainAtlasAnalysis.landCatalog:m_terrainAtlasAnalysis.overlayCatalog);
                STerrainTileCommand resolution=m_terrainEdit.makeVirtualStrokeResolution(
                    m_tileStrokeCommand,catalog,m_tileStrokeDistributionSeed,&releaseReport);
                if(releaseReport.hasErrors())
                {
                    applyTerrainTilePreview(m_tileStrokeCommand,false);
                    m_tileStrokeCommand=STerrainTileCommand();
                    ++m_tileStrokeRefused;
                    emit updateMsg(tr("Terrain stroke refused without substituting material identities:\n%1")
                        .arg(releaseReport.toText()));
                }
                else if(!resolution.isEmpty()&&applyTerrainTilePreview(resolution,true))
                {
                    m_tileStrokeChangedTiles+=resolution.deltas.size();
                    appendTerrainTileCommand(&m_tileStrokeCommand,resolution);
                }
            }
            if(!releaseReport.findings().isEmpty())
                ei::log(releaseReport.hasErrors()?eLogWarning:eLogInfo,
                    "Virtual terrain release resolution:\n"+releaseReport.toText());
        }
        m_tilePaintStrokeActive = false;
        refreshTerrainFamilyUsage();
        if(m_pUndoStack && !m_overlaySurfaceStrokeCommand.isEmpty())
            m_pUndoStack->push(new COverlaySurfacePreviewCommand(
                this, m_overlaySurfaceStrokeCommand, true));
        else if(m_pUndoStack && !m_tileStrokeCommand.isEmpty())
        {
            m_pUndoStack->push(new CTerrainTilePreviewCommand(
                this, m_tileStrokeCommand, true));
            // Loaded authored graph data remains an explicit static override;
            // only an already-generated preview is invalidated/rebuilt.
            scheduleAiGraphPreviewRegeneration();
        }
        m_pLand->setDirty(m_terrainEdit.isDirty());
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag,
                                  m_terrainEdit.isDirty() ? "*" : "");
        emit updateMsg(tr("Terrain stroke: %1 stamp(s), %2 changed tile(s), %3 no-op, %4 refused.%5")
            .arg(m_tileStrokeActions).arg(m_tileStrokeChangedTiles)
            .arg(m_tileStrokeNoOps).arg(m_tileStrokeRefused)
            .arg(m_tileStrokeUndoCheckpointed
                ? tr(" Preview cache checkpointed; earlier undo history was cleared.")
                : QString()));
        ei::log(m_tileStrokeRefused > 0 ? eLogWarning : eLogInfo,
            QString("Terrain paint stroke %1 summary: actions=%2 applied=%3 no-op=%4 refused=%5 changed=%6 solverNodes=%7")
                .arg(m_tileBrushCommandId).arg(m_tileStrokeActions)
                .arg(m_tileStrokeApplied).arg(m_tileStrokeNoOps)
                .arg(m_tileStrokeRefused).arg(m_tileStrokeChangedTiles)
                .arg(m_tileStrokeSolverNodes));
        // Resolution happens after mouse release. Present the completed atlas
        // texture and rebuilt sector VBOs immediately instead of waiting for
        // a later camera/input event to trigger another frame.
        if(isValid())
            updateGL();
        else
            update();
    }
    m_lastTerrainPaintCenterValid = false;
    ++m_tileBrushCommandId;
}

void CView::beginTileBrushGroup()
{
    if(m_tilePaintStrokeActive || !m_pUndoStack)
        return;
    m_tilePaintStrokeActive = true;
    // Uniform authored variations are chosen once per stroke. The seed is
    // deterministic for undo/replay while adjacent strokes need not repeat the
    // same texture variation.
    m_tileStrokeDistributionSeed = m_terrainSemanticSeed
        ^ (quint32(m_tileBrushCommandId + 1) * 0x9e3779b9u);
    m_tileStrokeCommand = STerrainTileCommand();
    m_tileStrokeCommand.description = tr("Paint terrain tiles");
    m_overlaySurfaceStrokeCommand = SOverlaySurfaceCommand();
    m_lastTerrainPaintCenterValid = false;
    m_tileStrokeActions = 0;
    m_tileStrokeApplied = 0;
    m_tileStrokeNoOps = 0;
    m_tileStrokeRefused = 0;
    m_tileStrokeChangedTiles = 0;
    m_tileStrokeSolverNodes = 0;
    m_tileStrokeUndoCheckpointed = false;
}

bool CView::applyTerrainTilePreview(const STerrainTileCommand& command, bool forward)
{
    if(!m_terrainEditAvailable)
        return false;
    CValidationReport report;
    if(!m_terrainEdit.apply(command, forward, &report))
    {
        ei::log(eLogWarning, "Semantic terrain preview command refused:\n" + report.toText());
        return false;
    }
    for(const STerrainTileDelta& delta:command.deltas)
    {
        m_workingAtlasProtectedSlots.insert(
            SMprPackedTile::decode(delta.before).atlasSlot());
        m_workingAtlasProtectedSlots.insert(
            SMprPackedTile::decode(delta.after).atlasSlot());
    }
    QSet<QPair<int, int>> sectors;
    for(const STerrainTileDelta& delta : command.deltas)
    {
        m_pLand->previewCanonicalTile(delta.globalX, delta.globalY,
            delta.layer == ETerrainTileLayer::Land,
            forward ? delta.after : delta.before, false);
        sectors.insert(qMakePair(delta.globalX / 16, delta.globalY / 16));
    }
    // Tile painting is never part of the sculpt/overlay preview batch. Rebuild
    // every touched sector immediately so a live stroke and its later
    // undo/redo use the same visible update path.
    // A generated exact tile can upload a replacement atlas immediately before
    // this command.  That upload deliberately releases the widget context, but
    // updateSectorDrawData rebuilds OpenGL buffers and therefore cannot be left
    // to run context-free.  Otherwise the CPU tile changes on button release
    // while the old four-corner VBO remains visible until the next interaction.
    const bool acquireContext = isValid()
        && QGLContext::currentContext() != context();
    if(acquireContext)
        makeCurrent();
    for(const QPair<int, int>& sector : sectors)
        m_pLand->updateSectorDrawData(sector.first, sector.second);
    if(acquireContext)
        doneCurrent();
    m_pLand->setDirty(m_terrainEdit.isDirty());
    if(!m_tilePaintStrokeActive)
    {
        refreshTerrainFamilyUsage();
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag,
                                  m_terrainEdit.isDirty() ? "*" : "");
        emit updateMsg(command.semanticAudit.solverMode == QStringLiteral("manual-atlas")
            ? tr("Painted %1 atlas tile(s) manually.").arg(command.deltas.size())
            : tr("Painted %1 atlas tile(s), including bounded seam updates.")
                  .arg(command.deltas.size()));
    }
    update();
    return true;
}

bool CView::applyOverlaySurfacePreview(const SOverlaySurfaceCommand& command,
                                       bool forward)
{
    if(!m_terrainEditAvailable)
        return false;
    CValidationReport report;
    if(!m_terrainEdit.apply(command, forward, &report))
    {
        ei::log(eLogWarning, "Overlay surface preview command refused:\n" + report.toText());
        return false;
    }
    for(const SOverlaySectorDelta& delta : command.sectors)
    {
        const QPair<int, int> sector(delta.sectorX, delta.sectorY);
        if(m_terrainPreviewBatchActive)
        {
            m_pendingTerrainPreviewSectors.insert(sector);
            continue;
        }
        if(!m_pLand->previewCanonicalOverlaySector(delta.sectorX, delta.sectorY,
                m_terrainEdit.sectorPayload(delta.sectorX, delta.sectorY)))
            ei::log(eLogWarning, QString(
                "Overlay model changed but preview renderer rejected sector %1,%2")
                    .arg(delta.sectorX).arg(delta.sectorY));
    }
    m_pLand->setDirty(m_terrainEdit.isDirty());
    m_aiGraphOverlayDirty = true;
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag,
                              m_terrainEdit.isDirty() ? "*" : "");
    update();
    return true;
}

void CView::beginTerrainPreviewBatch()
{
    m_terrainPreviewBatchActive = true;
}

void CView::endTerrainPreviewBatch()
{
    if(!m_terrainPreviewBatchActive)
        return;
    m_terrainPreviewBatchActive = false;
    const QSet<QPair<int, int>> sectors = m_pendingTerrainPreviewSectors;
    m_pendingTerrainPreviewSectors.clear();
    for(const QPair<int, int>& sector : sectors)
        if(!m_pLand->previewCanonicalOverlaySector(
            sector.first, sector.second,
            m_terrainEdit.sectorPayload(sector.first, sector.second)))
            ei::log(eLogWarning, QString(
                "Batched terrain preview rejected sector %1,%2")
                    .arg(sector.first).arg(sector.second));
    if(!sectors.isEmpty())
        update();
}

bool CView::onExit()
{
    if(!closeAllMobScriptWindows())
        return false;
    bool bCloseAllowed = true;
    CMob* pMob = nullptr;
    foreach(pMob, m_aMob)
    {
        if(!pMob->isDirty())
            continue;

        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Exit", pMob->mobName() + " has unsaved changes.\nDo you want to save changes?", QMessageBox::Save|QMessageBox::No|QMessageBox::Cancel);
        if(reply == QMessageBox::Save)
        {
            if(!saveMobDocument(pMob, pMob->filePath()))
            {
                bCloseAllowed = false;
                break;
            }
        }
        else if(reply == QMessageBox::Cancel)
        {
            bCloseAllowed = false;
            break;
        }
    }
    if(!bCloseAllowed)
        return bCloseAllowed;

    if(m_pLand && m_pLand->isDirty())
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Exit", m_pLand->filePath().fileName() + " has unsaved changes.\nDo you want to save changes?", QMessageBox::Save|QMessageBox::No|QMessageBox::Cancel);
        if(reply == QMessageBox::Save)
        {
            if(!saveLandscapeDocument(m_pLand->filePath()))
                bCloseAllowed = false;
        }
        else if(reply == QMessageBox::Cancel)
        {
            bCloseAllowed = false;
        }
    }
    if(bCloseAllowed)
    {
        saveSession();
    }
    return bCloseAllowed;
}

void CView::updateParameter(EObjParam propType)
{
    return; // not all prop types can be updated for each node. Use direct updating for each operation separately.

    if(nullptr == m_activeMob)
        return;

    QSharedPointer<IPropertyBase> valueToUpdate;
    QSharedPointer<IPropertyBase> value;
    for (const auto& node : CScene::getInstance()->getMode() == eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if (node->nodeState() != ENodeState::eSelect)
            continue;

        if(node->nodeType() == eTrapActZone || node->nodeType() == eTrapCastPoint || node->nodeType() == eLookPoint || node->nodeType() == ePatrolPoint)
            node->getLogicParam(value, propType);
        else
            node->getParam(value, propType);

        if(!valueToUpdate.isNull() && !valueToUpdate->isEqual(value.get()))
        {
            valueToUpdate.reset();
            break;
        }
        valueToUpdate.reset(value->clone()); //same values will be copied every time
    }
    //todo: pipe to direct update value ot x,y,z, in cell
    //m_tableManager->updateParam(param, valueToUpdate);
}

void CView::viewParameters()
{
    if(m_aMob.isEmpty())
    {
        m_tableManager->reset(); //clear table data if empty current mob
        m_tableManager->setReadOnly(false);
        emit selectionStateChanged(false);
        return;
    }

    QList<CNode*> selected;
    for(CMob* mob : m_aMob)
        if(mob && isMobVisible(mob))
            for(CNode* node : CScene::getInstance()->getMode() == eEditModeLogic
                    ? mob->logicNodes() : mob->nodes())
                if(node && node->nodeState() == ENodeState::eSelect
                   && !selected.contains(node))
                    selected.append(node);

    QSet<ENodeType> aType;
    //find unique selected node types
    //CMob* pMob = nullptr;
    //foreach(pMob, m_aMob)
    CNode* pNode = nullptr;
    foreach(pNode, selected)
        aType.insert(pNode->nodeType());

    if (aType.isEmpty())
    {//reset parameters if not selected objects
        m_tableManager->reset();
        m_tableManager->setReadOnly(false);
        emit selectionStateChanged(false);
        return;
    }

    //define common type for parameter display
    int ttt(aType.toList()[0]);
    foreach (const ENodeType& type, aType)
        ttt &= type;
    const ENodeType commonType = ENodeType(ttt);

    QList<QSharedPointer<IPropertyBase>> aProp;
    foreach(pNode, selected)
    {
        if(CScene::getInstance()->getMode() == eEditModeLogic)
            pNode->collectlogicParams(aProp, commonType);
        else
            pNode->collectParams(aProp, commonType);
    }

    m_tableManager->setNewData(aProp);
    m_tableManager->setReadOnly(!hasEditableSelection());
    emit selectionStateChanged(true);
}

void CView::onParamChange(const QSharedPointer<IPropertyBase>& prop)
{
    if(nullptr == m_activeMob)
        return;

    QList<CNode*> targets;
    for(CNode* node : CScene::getInstance()->getMode() == eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if(node && node->nodeState() == ENodeState::eSelect)
            targets.append(node);
    }
    if(targets.isEmpty())
        return;
    if(prop->type() == eObjParam_NID && targets.size() > 1)
    {
        ei::log(eLogWarning, "Cannot assign one identifier to multiple objects");
        QMessageBox::warning(this, "Operation Error",
                             "One identifier cannot be assigned to multiple objects.");
        return;
    }

    const bool grouped = targets.size() > 1;
    if(grouped)
        m_pUndoStack->beginMacro(QString("Change %1 objects").arg(targets.size()));

    CNode* pNode = nullptr;
    foreach(pNode, targets)
    {
        switch (prop->type()) {

        case eObjParam_ROTATION: //todo: need recalc landscape position for objects?
        case eObjParam_COMPLECTION:
        {
            CChangeProp* pChanger = new CChangeProp(this, pNode, prop);
            QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            QObject::connect(pChanger, SIGNAL(updateParam()), m_pTree, SLOT(applyFilter()));
            //QObject::connect(pChanger, SIGNAL(updatePosOnLand(CNode*)), this, SLOT(landPositionUpdate(CNode*)));
            m_pUndoStack->push(pChanger);
            break;
        }
        case eObjParam_TEMPLATE:
        {
            CChangeProp* pChanger = new CChangeProp(this, pNode, prop); // #todo: update node pos after undo-redo
            QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            QObject::connect(pChanger, SIGNAL(updateParam()), m_pTree, SLOT(applyFilter()));
            //QObject::connect(pChanger, SIGNAL(updatePosOnLand(CNode*)), this, SLOT(landPositionUpdate(CNode*)));
            m_pUndoStack->push(pChanger);
            break;
        }
        case eObjParam_NID:
        {
            auto pId = dynamic_cast<propUint*>(prop.get());
            if(!pId || pId->value() == pNode->mapId())
                break;
            emit nidChangeProposed(pId->value(), m_activeMob->filePath().absoluteFilePath());
            if(!confirmScriptReferencedObjectIds({pNode->mapId()},
                                                 tr("change this object ID")))
                return;
            CChangeProp* pChanger = new CChangeProp(this, pNode, prop);
            QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            QObject::connect(pChanger, SIGNAL(updateParam()), m_pTree, SLOT(applyFilter()));
            QObject::connect(pChanger, SIGNAL(changeIdSignal(uint,uint)), m_pTree, SLOT(onChangeNodeId(uint,uint)));
            QObject::connect(pChanger, SIGNAL(changeTreeName(CNode*)), m_pTree, SLOT(onChangeObjectName(CNode*)));
            m_pUndoStack->push(pChanger);
            break;
        }
        default:
        {
            if(CScene::getInstance()->getMode() == eEditModeLogic)
            {
                onParamChangeLogic(pNode, prop);
            }
            else
            {
                CChangeProp* pChanger = new CChangeProp(this, pNode, prop);
                QObject::connect(pChanger, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                QObject::connect(pChanger, SIGNAL(updateParam()), m_pTree, SLOT(applyFilter()));
                QObject::connect(pChanger, SIGNAL(changeIdSignal(uint,uint)), m_pTree, SLOT(onChangeNodeId(uint,uint)));
                QObject::connect(pChanger, SIGNAL(changeTreeName(CNode*)), m_pTree, SLOT(onChangeObjectName(CNode*)));
                m_pUndoStack->push(pChanger);
            }
            break;
        }
        }
    }

    if(grouped)
        m_pUndoStack->endMacro();
}

bool CView::applyFamilyNidRepairs(const QVector<SNidRepair>& repairs, QString* error)
{
    struct ResolvedRepair { CMob* mob; CNode* node; SNidRepair repair; };
    QVector<ResolvedRepair> resolved;
    QSet<quint32> replacements;
    for(const SNidRepair& repair : repairs)
    {
        if(repair.oldNid == repair.newNid || replacements.contains(repair.newNid))
        {
            if(error) *error = tr("The repair contains a duplicate or unchanged replacement ID.");
            return false;
        }
        CMob* targetMob = nullptr;
        const QString wanted = QDir::cleanPath(QFileInfo(repair.mobPath).absoluteFilePath());
        for(CMob* mob : m_aMob)
            if(mob && QDir::cleanPath(mob->filePath().absoluteFilePath()).compare(
                   wanted, Qt::CaseInsensitive) == 0)
            {
                targetMob = mob;
                break;
            }
        if(!targetMob)
        {
            if(error) *error = tr("Repair target is not loaded: %1").arg(repair.mobPath);
            return false;
        }
        CNode* targetNode = nullptr;
        for(CNode* node : targetMob->nodes())
            if(node && node->mapId() == repair.oldNid
               && node->rawSourceObjectPath() == repair.sectionPath)
            {
                targetNode = node;
                break;
            }
        if(!targetNode)
        {
            if(error) *error = tr("Could not map NID %1 at %2 onto a loaded raw object.")
                .arg(repair.oldNid).arg(repair.sectionPath);
            return false;
        }
        replacements.insert(repair.newNid);
        resolved.append({targetMob, targetNode, repair});
    }
    if(resolved.isEmpty())
    {
        if(error) *error = tr("No repair rows were selected.");
        return false;
    }

    m_pUndoStack->beginMacro(tr("Repair %1 family NID collision(s)").arg(resolved.size()));
    for(const ResolvedRepair& item : resolved)
    {
        QSharedPointer<IPropertyBase> value(new propUint(eObjParam_NID, item.repair.newNid));
        CChangeProp* command = new CChangeProp(this, item.mob, item.node, value);
        connect(command, SIGNAL(updateParam()), this, SLOT(viewParameters()));
        connect(command, SIGNAL(changeIdSignal(uint,uint)), m_pTree, SLOT(onChangeNodeId(uint,uint)));
        m_pUndoStack->push(command);
    }
    m_pUndoStack->endMacro();
    emit mobStateChanged();
    return true;
}

quint32 CView::suggestFamilyNid(CMob* mob, quint32 localSuggestion)
{
    quint32 suggestion = localSuggestion;
    if(mob)
        emit nidAllocationRequested(mob->filePath().absoluteFilePath(), &suggestion);
    return suggestion;
}

void CView::onPropertiesChange(const QList<QSharedPointer<IPropertyBase>>& properties)
{
    if(properties.isEmpty() || !m_pUndoStack)
        return;
    if(properties.size() > 1)
        m_pUndoStack->beginMacro(QString("Paste %1 properties").arg(properties.size()));
    for(const QSharedPointer<IPropertyBase>& property : properties)
        onParamChange(property);
    if(properties.size() > 1)
        m_pUndoStack->endMacro();
}



void CView::collectObjectTreeData()
{
    m_pTree->prepareForRebuild();
    m_pTree->clear();
    if(m_aMob.isEmpty())
    {
        m_pTree->applyFilter();
        return;
    }

    m_pTree->setColumnCount(2);
    QStringList labels;
    labels << "Objects" << "Count";
    m_pTree->setHeaderLabels(labels);
    m_pTree->header()->setStretchLastSection(false);
    m_pTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pTree->resizeColumnToContents(1);
    for(CMob* mob : m_aMob)
    {
        if(!mob)
            continue;
        const bool visible = isMobVisible(mob);
        auto mobItem = new CTreeObject(m_pTree);
        QString mobLabel = mob->mobName() + (mob->isDirty() ? " *" : "");
        if(mob == m_activeMob)
            mobLabel += tr(" [Active]");
        else
            mobLabel += tr(" [Locked]");
        if(!visible)
            mobLabel += tr(" [Hidden]");
        mobItem->setText(0, mobLabel);
        mobItem->setText(1, QString::number(mob->nodes().size()));
        mobItem->setData(0, Qt::UserRole + 1,
                         QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(mob)));
        mobItem->setExpanded(true);
        m_pTree->addTopLevelItem(mobItem);

        QMap<QString, QMap<QString, QList<CNode*>>> objects;
        for(CNode* node : mob->nodes())
        {
            if(!node)
                continue;
            const QString typeName = m_pTree->objectNameByType(node->nodeType());
            if(typeName.isEmpty())
                continue;
            CUnit* unit = dynamic_cast<CUnit*>(node);
            const QString group = unit ? unit->databaseName() : node->mapName();
            objects[typeName][group].append(node);
        }

        for(auto typeIt = objects.constBegin(); typeIt != objects.constEnd(); ++typeIt)
        {
            auto typeItem = new CTreeObject(mobItem, nullptr);
            typeItem->setText(0, typeIt.key());
            int typeCount = 0;
            for(const QList<CNode*>& nodes : typeIt.value())
                typeCount += nodes.size();
            typeItem->setText(1, QString::number(typeCount));
            for(auto groupIt = typeIt.value().constBegin(); groupIt != typeIt.value().constEnd(); ++groupIt)
            {
                auto groupItem = new CTreeObject(typeItem, nullptr);
                groupItem->setText(0, groupIt.key());
                groupItem->setText(1, QString::number(groupIt.value().size()));
                if(groupIt.value().size() == 1)
                {
                    groupItem->setNode(groupIt.value().front());
                    groupItem->setToolTip(0, mob == m_activeMob
                        ? tr("Belongs to active MOB %1").arg(mob->mobName())
                        : tr("Belongs to locked MOB %1. Double-click to make it active.")
                            .arg(mob->mobName()));
                }
                else
                    for(CNode* node : groupIt.value())
                    {
                        auto objectItem = new CTreeObject(groupItem, node);
                        objectItem->setText(0, QString::number(node->mapId()));
                        objectItem->setToolTip(0, mob == m_activeMob
                            ? tr("Belongs to active MOB %1").arg(mob->mobName())
                            : tr("Belongs to locked MOB %1. Double-click to make it active.")
                                .arg(mob->mobName()));
                        if(!visible)
                            objectItem->setFlags(objectItem->flags() & ~Qt::ItemIsSelectable);
                    }
                if(!visible)
                    groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsSelectable);
            }
        }

        if(!visible)
        {
            const QBrush hiddenTextBrush(QColor(128, 128, 128));
            QList<QTreeWidgetItem*> pendingItems;
            pendingItems.append(mobItem);
            while(!pendingItems.isEmpty())
            {
                QTreeWidgetItem* item = pendingItems.takeLast();
                for(int column = 0; column < item->columnCount(); ++column)
                    item->setForeground(column, hiddenTextBrush);
                for(int childIndex = 0; childIndex < item->childCount(); ++childIndex)
                    pendingItems.append(item->child(childIndex));
            }
        }
    }
    m_pTree->applyFilter();
    const QList<CNode*> selected = selectionSnapshot();
    if(!selected.isEmpty())
        m_pTree->markNode(selected.back(), false);
}

void CView::onRestoreCursor()
{
    unsetCursor();
}

void CView::moveCamToSelectedObjects()
{
    CNode* pNode = nullptr;
    CBox box;
    foreach(pNode, m_activeMob->nodes())
    {
        if (pNode->nodeState() != ENodeState::eSelect)
            continue;
        // calculate world position
        CBox nodeBox = pNode->getBBox();
        nodeBox.move(pNode->drawPosition());
        box.expand(nodeBox);
    }

    if(box.isInit())
    {
        m_cam->moveTo(box.center());
        m_cam->moveAwayOn(box.radius()*2.5f); // too much for multi large objects
    }
}

bool CView::terrainCellAtViewport(const QPoint& viewportPosition, QPoint* cell)
{
    if(!cell || !m_pLand || !m_pLand->isMprLoad()) return false;
    bool hit = false;
    QVector3D terrainPosition = getTerrainPos(
        viewportPosition.x(), viewportPosition.y(), true, &hit);
    if(!hit) return false;
    CTile* tile = nullptr;
    STileLocation location;
    if(!m_pLand->pickTile(terrainPosition, tile, location, true)) return false;
    *cell = QPoint(location.xSec * 16 + location.col,
                   location.ySec * 16 + location.row);
    return true;
}

void CView::appendMarkedTerrainPaintIssue(const QSet<QPoint>& cells,
                                          const QString& category)
{
    if(cells.isEmpty() || !m_terrainEditAvailable
       || !m_pLand || !m_pLand->isMprLoad()) return;
    int minimumX = std::numeric_limits<int>::max();
    int minimumY = std::numeric_limits<int>::max();
    int maximumX = std::numeric_limits<int>::min();
    int maximumY = std::numeric_limits<int>::min();
    for(const QPoint& cell : cells)
    {
        minimumX = qMin(minimumX, cell.x()); minimumY = qMin(minimumY, cell.y());
        maximumX = qMax(maximumX, cell.x()); maximumY = qMax(maximumY, cell.y());
    }
    const QRect markedBounds(QPoint(minimumX, minimumY),
                             QPoint(maximumX, maximumY));
    const QString directoryPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("diagnostics/terrain-paint"));
    if(!QDir().mkpath(directoryPath))
    {
        emit updateMsg(tr("Could not create the terrain-paint diagnostic folder."));
        return;
    }
    const QString namespaceName = m_mprDocument.mapInfo().innerNamespace;
    QString safeName = namespaceName.isEmpty()
        ? QStringLiteral("terrain") : namespaceName.toLower();
    safeName.replace(QRegExp(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("-"));
    if(safeName.isEmpty()) safeName = QStringLiteral("terrain");
    const QString path = QDir(directoryPath).filePath(
        safeName + QStringLiteral("-paint-issues.json"));
    QJsonObject root;
    QFile existing(path);
    if(existing.open(QIODevice::ReadOnly))
    {
        const QJsonDocument parsed = QJsonDocument::fromJson(existing.readAll());
        if(parsed.isObject()) root = parsed.object();
    }
    const QString sourceHash = QString::fromLatin1(QCryptographicHash::hash(
        m_mprDocument.originalBytes(), QCryptographicHash::Sha256).toHex());
    if(root.value(QStringLiteral("schema")).toString()
            != QStringLiteral("ei-mapper-endurance-paint-issue-marks")
       || root.value(QStringLiteral("sourceMprSha256")).toString() != sourceHash)
        root = QJsonObject();
    root.insert(QStringLiteral("schema"),
                QStringLiteral("ei-mapper-endurance-paint-issue-marks"));
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("mprFileName"), m_pLand->filePath().fileName());
    const QString applicationPath = QDir::cleanPath(
        QCoreApplication::applicationDirPath());
    const QString sourcePath = QDir::cleanPath(m_pLand->filePath().absoluteFilePath());
    if(sourcePath.startsWith(applicationPath + QDir::separator(), Qt::CaseInsensitive))
        root.insert(QStringLiteral("fixtureRelativePath"),
            QDir(applicationPath).relativeFilePath(sourcePath).replace('\\', '/'));
    root.insert(QStringLiteral("mprNamespace"), namespaceName);
    root.insert(QStringLiteral("sourceMprSha256"), sourceHash);
    root.insert(QStringLiteral("atlasPixelSha256"), m_pTileForm
        ? QString::fromLatin1(terrainAtlasPixelSha256(m_pTileForm->tileImages()))
        : QString());
    root.insert(QStringLiteral("semanticProfileProvenance"),
                m_terrainAtlasProfile.provenance);
    const bool overlay = m_landscapeTools
        && (m_landscapeTools->editMode() == ELandscapeEditMode::OverlayPaint
            || m_landscapeTools->editMode() == ELandscapeEditMode::OverlaySurface);
    const ETerrainTileLayer layer = overlay
        ? ETerrainTileLayer::Overlay : ETerrainTileLayer::Land;
    const CTerrainTransitionCatalog& catalog = overlay
        ? m_terrainAtlasAnalysis.overlayCatalogWithLandFamilies()
        : (m_workingTerrainAtlas ? m_workingTerrainAtlas->catalog()
                                 : m_terrainAtlasAnalysis.landCatalog);
    bool materialSelected = false;
    const qint32 selectedMaterial = m_landscapeTools
        ? m_landscapeTools->paintTerrainType(&materialSelected) : -1;
    QString selectedMaterialName;
    for(const STerrainAtlasMaterial& material : m_terrainAtlasAnalysis.materials)
        if(material.id == selectedMaterial
           && ((material.layer == ETerrainAtlasLayer::Overlay) == overlay))
        {
            selectedMaterialName = material.name;
            break;
        }
    QJsonObject mark;
    mark.insert(QStringLiteral("id"), root.value(QStringLiteral("marks")).toArray().size());
    mark.insert(QStringLiteral("timestampUtc"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    mark.insert(QStringLiteral("category"), category);
    mark.insert(QStringLiteral("layer"), overlay ? QStringLiteral("overlay")
                                                  : QStringLiteral("land"));
    mark.insert(QStringLiteral("selectedMaterialId"),
                materialSelected ? selectedMaterial : -1);
    mark.insert(QStringLiteral("selectedMaterial"), selectedMaterialName);
    QJsonObject boundsJson;
    boundsJson.insert(QStringLiteral("x0"), markedBounds.left());
    boundsJson.insert(QStringLiteral("y0"), markedBounds.top());
    boundsJson.insert(QStringLiteral("x1"), markedBounds.right());
    boundsJson.insert(QStringLiteral("y1"), markedBounds.bottom());
    mark.insert(QStringLiteral("markedCells"), boundsJson);
    mark.insert(QStringLiteral("markedTileCount"), cells.size());
    QVector<QPoint> orderedMarkedCells = cells.values().toVector();
    std::sort(orderedMarkedCells.begin(), orderedMarkedCells.end(),
              [](const QPoint& lhs, const QPoint& rhs) {
                  return lhs.y() != rhs.y() ? lhs.y() < rhs.y() : lhs.x() < rhs.x();
              });
    QJsonArray selectedCells;
    for(const QPoint& cell : orderedMarkedCells)
    {
        QJsonObject selected;
        selected.insert(QStringLiteral("x"), cell.x());
        selected.insert(QStringLiteral("y"), cell.y());
        selectedCells.append(selected);
    }
    mark.insert(QStringLiteral("selectedCells"), selectedCells);
    QJsonArray tiles;
    const QRect snapshot = markedBounds.adjusted(-1, -1, 1, 1).intersected(
        QRect(0, 0, m_terrainEdit.tileCountX(), m_terrainEdit.tileCountY()));
    for(int y = snapshot.top(); y <= snapshot.bottom(); ++y)
        for(int x = snapshot.left(); x <= snapshot.right(); ++x)
        {
            bool available = false;
            const quint16 packed = m_terrainEdit.packedTile(x, y, layer, &available);
            if(!available) continue;
            bool recognized = false;
            const STerrainTransitionSignature signature =
                catalog.gridSignatureForPacked(packed, &recognized);
            QJsonObject tile;
            tile.insert(QStringLiteral("x"), x);
            tile.insert(QStringLiteral("y"), y);
            tile.insert(QStringLiteral("insideMark"), cells.contains(QPoint(x, y)));
            tile.insert(QStringLiteral("packed"), packedTerrainTileJson(packed));
            tile.insert(QStringLiteral("catalogRecognized"), recognized);
            if(recognized)
                tile.insert(QStringLiteral("gridCorners"),
                            terrainGridSignatureJson(signature));
            tiles.append(tile);
        }
    mark.insert(QStringLiteral("tileSnapshot"), tiles);
    QJsonArray relatedActions;
    const QRect related = markedBounds.adjusted(-2, -2, 2, 2);
    for(const STerrainPaintAuditAction& action : m_terrainPaintActions)
    {
        bool intersects = false;
        for(const QPoint& point : action.semantic.affectedCells)
            if(related.contains(point)) { intersects = true; break; }
        if(!intersects) continue;
        QJsonObject item;
        item.insert(QStringLiteral("actionId"), action.actionId);
        item.insert(QStringLiteral("strokeId"), action.strokeId);
        item.insert(QStringLiteral("outcome"), action.outcome);
        item.insert(QStringLiteral("requestedMaterialId"),
                    action.semantic.requestedMaterialId);
        item.insert(QStringLiteral("backgroundMaterialId"),
                    action.semantic.backgroundMaterialId);
        item.insert(QStringLiteral("solverMode"), action.semantic.solverMode);
        item.insert(QStringLiteral("solverNodes"), action.semantic.solverNodes);
        item.insert(QStringLiteral("validation"), action.validation);
        item.insert(QStringLiteral("changedTileCount"), action.deltas.size());
        QJsonArray paintedVertices;
        for(const QPoint& point : action.semantic.paintedVertices)
        {
            QJsonObject vertex;
            vertex.insert(QStringLiteral("x"), point.x());
            vertex.insert(QStringLiteral("y"), point.y());
            paintedVertices.append(vertex);
        }
        item.insert(QStringLiteral("paintedVertices"), paintedVertices);
        relatedActions.append(item);
    }
    mark.insert(QStringLiteral("relatedPaintActions"), relatedActions);
    QJsonArray marks = root.value(QStringLiteral("marks")).toArray();
    marks.append(mark);
    root.insert(QStringLiteral("marks"), marks);
    QString writeError;
    if(!ei::writePortableDiagnostic(path,
           QJsonDocument(root).toJson(QJsonDocument::Indented), &writeError))
    {
        emit updateMsg(tr("Could not write paint issue diagnostic: %1 (%2)")
                       .arg(QDir::toNativeSeparators(path), writeError));
        return;
    }
    ei::log(eLogInfo, QString(
        "Terrain paint issue marked category:%1 tiles:%2 bounds:%3,%4-%5,%6 output:%7")
        .arg(category).arg(cells.size()).arg(markedBounds.left()).arg(markedBounds.top())
        .arg(markedBounds.right()).arg(markedBounds.bottom()).arg(path));
    emit updateMsg(tr("Paint issue marked. Send this file: %1")
                   .arg(QDir::toNativeSeparators(path)));
}

void CView::mousePressEvent(QMouseEvent* event)
{
    m_lastViewportMousePos = event->pos();
    m_hasViewportMousePos = rect().contains(event->pos());
    if(m_landscapeInteractionSuspended)
    {
        event->accept();
        return;
    }
    if(m_landscapeTools && m_landscapeTools->paintIssueMarkingEnabled()
       && event->button() == Qt::LeftButton)
    {
        QPoint cell;
        if(terrainCellAtViewport(event->pos(), &cell))
        {
            m_paintIssueDragActive = true;
            m_paintIssueHoverValid = true;
            m_paintIssueHoverCell = cell;
            m_paintIssueSelectedCells.clear();
            m_paintIssueSelectedCells.insert(cell);
            update();
        }
        event->accept();
        return;
    }
    if(m_landscapeTools && m_landscapeTools->sculptEnabled()
        && m_terrainEditAvailable && event->button() == Qt::LeftButton)
    {
        const bool overlaySculpt = m_landscapeTools->overlaySculptEnabled();
        m_sculptStrokeActive = true;
        m_lastSculptPoint = getTerrainPos(event->x(), event->y(), !overlaySculpt);
        m_sculptOutlineCenter = m_lastSculptPoint;
        m_sculptOutlineValid = true;
        m_lastSculptStampPoint = m_lastSculptPoint;
        m_sculptStrokeChanged = false;
        m_sculptStrokeStamps = 0;
        m_sculptStrokeVertices = 0;
        m_sculptStrokeCommand = STerrainHeightCommand();
        m_sculptStrokeCommand.description = tr("Sculpt terrain");
        m_overlaySurfaceStrokeCommand = SOverlaySurfaceCommand();
        m_overlaySurfaceStrokeCommand.description = tr("Sculpt overlay height");
        sculptTerrainAt(m_lastSculptPoint);
        m_sculptRepeatTimer->start();
        event->accept();
        return;
    }
    m_pOp->mousePressEvent(event);
}

void CView::sculptTerrainAt(const QVector3D& position)
{
    if(!m_terrainEditAvailable || !m_landscapeTools)
        return;

    STerrainBrush brush;
    brush.centerX = position.x();
    brush.centerY = position.y();
    brush.radius = m_landscapeTools->radius();
    brush.falloff = ETerrainBrushFalloff::Smooth;

    if(m_landscapeTools->overlaySculptEnabled())
    {
        CValidationReport report;
        SOverlaySurfaceCommand overlayCommand;
        switch(m_landscapeTools->sculptTool())
        {
        case ELandscapeSculptTool::Raise:
            overlayCommand = m_terrainEdit.makeOverlayRaiseLower(
                brush, m_landscapeTools->rawDelta() * m_landscapeTools->strength(),
                &report);
            break;
        case ELandscapeSculptTool::Lower:
            overlayCommand = m_terrainEdit.makeOverlayRaiseLower(
                brush, -m_landscapeTools->rawDelta() * m_landscapeTools->strength(),
                &report);
            break;
        case ELandscapeSculptTool::Flatten:
            overlayCommand = m_terrainEdit.makeOverlayFlatten(
                brush, m_landscapeTools->flattenHeight(),
                m_landscapeTools->strength(), &report);
            break;
        case ELandscapeSculptTool::Smooth:
            overlayCommand = m_terrainEdit.makeOverlaySmooth(
                brush, m_landscapeTools->strength(), &report);
            break;
        }
        if(overlayCommand.isEmpty())
        {
            if(report.hasErrors())
                emit updateMsg(report.toText());
            return;
        }
        if(applyOverlaySurfacePreview(overlayCommand, true))
        {
            appendOverlaySurfaceCommand(&m_overlaySurfaceStrokeCommand, overlayCommand);
            m_sculptStrokeChanged = true;
            ++m_sculptStrokeStamps;
            m_sculptStrokeVertices += overlayCommand.sectors.size();
        }
        return;
    }

    STerrainHeightCommand command;
    switch(m_landscapeTools->sculptTool())
    {
    case ELandscapeSculptTool::Raise:
        command = m_terrainEdit.makeRaiseLower(
            brush, m_landscapeTools->rawDelta() * m_landscapeTools->strength());
        break;
    case ELandscapeSculptTool::Lower:
        command = m_terrainEdit.makeRaiseLower(
            brush, -m_landscapeTools->rawDelta() * m_landscapeTools->strength());
        break;
    case ELandscapeSculptTool::Flatten:
        command = m_terrainEdit.makeFlatten(
            brush, m_landscapeTools->flattenHeight(), m_landscapeTools->strength());
        break;
    case ELandscapeSculptTool::Smooth:
        command = m_terrainEdit.makeSmooth(brush, m_landscapeTools->strength());
        break;
    }
    if(command.isEmpty())
        return;
    if(command.requiresMaxZDecision())
    {
        emit updateMsg(tr("Sculpt stamp refused: it would clamp the MPR raw-height range."));
        return;
    }
    if(m_sculptStrokeActive)
    {
        if(applyTerrainHeightPreview(command, true))
            appendTerrainHeightCommand(&m_sculptStrokeCommand, command);
        return;
    }
    m_pUndoStack->push(new CTerrainHeightPreviewCommand(this, command));
}

void CView::sculptTerrainTowardLatestPoint()
{
    if(!m_sculptStrokeActive || !m_landscapeTools)
        return;
    const float distance = m_lastSculptPoint.distanceToPoint(m_lastSculptStampPoint);
    const float spacing = qMax(1.0f, m_landscapeTools->radius() * 0.5f);
    // Preserve a continuous moving stroke without letting a burst of mouse
    // events enqueue unbounded work. Stationary holds still receive one stamp.
    const int steps = distance > spacing
        ? qMin(8, qMax(1, int(std::ceil(distance / spacing)))) : 1;
    const QVector3D start = m_lastSculptStampPoint;
    beginTerrainPreviewBatch();
    for(int step = 1; step <= steps; ++step)
    {
        const float ratio = float(step) / float(steps);
        sculptTerrainAt(start * (1.0f - ratio) + m_lastSculptPoint * ratio);
    }
    endTerrainPreviewBatch();
    m_lastSculptStampPoint = m_lastSculptPoint;
}

bool CView::applyTerrainHeightPreview(const STerrainHeightCommand& command, bool forward)
{
    if(!m_terrainEditAvailable)
        return false;
    CValidationReport report;
    if(!m_terrainEdit.apply(command, forward, &report, false))
    {
        ei::log(eLogWarning, "Terrain preview command refused:\n" + report.toText());
        return false;
    }

    QSet<QPair<int, int>> sectors;
    const int sectorCountX = int(m_mprDocument.mapInfo().sectorCountX);
    const int sectorCountY = int(m_mprDocument.mapInfo().sectorCountY);
    for(const STerrainHeightDelta& delta : command.deltas)
    {
        QVector<int> xs;
        QVector<int> ys;
        int sx = qMin(delta.globalX / 32, sectorCountX - 1);
        int sy = qMin(delta.globalY / 32, sectorCountY - 1);
        xs.append(sx);
        ys.append(sy);
        if(delta.globalX > 0 && delta.globalX % 32 == 0)
            xs.append(sx - 1);
        if(delta.globalY > 0 && delta.globalY % 32 == 0)
            ys.append(sy - 1);
        for(int x : xs)
            for(int y : ys)
                if(x >= 0 && y >= 0)
                    sectors.insert(qMakePair(x, y));
    }
    if(m_terrainPreviewBatchActive)
        m_pendingTerrainPreviewSectors.unite(sectors);
    else
        for(const QPair<int, int>& sector : sectors)
        {
            if(!m_pLand->previewCanonicalSector(
                sector.first, sector.second,
                m_terrainEdit.sectorPayload(sector.first, sector.second)))
            {
                ei::log(eLogWarning, QString("Terrain preview renderer rejected sector %1,%2")
                    .arg(sector.first).arg(sector.second));
            }
        }
    if(m_sculptStrokeActive)
    {
        m_sculptStrokeChanged = true;
        ++m_sculptStrokeStamps;
        m_sculptStrokeVertices += command.deltas.size();
    }
    else
    {
        for(CMob* mob : m_aMob)
            mob->reprojectForCurrentTerrain();
    }
    m_pLand->setDirty(m_terrainEdit.isDirty());
    m_aiGraphOverlayDirty = true;
    if(!m_sculptStrokeActive)
    {
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag,
                                  m_terrainEdit.isDirty() ? "*" : "");
        emit updateMsg(tr("Terrain preview changed %1 vertices in %2 sector(s); navigation data may need review.")
            .arg(command.deltas.size()).arg(sectors.size()));
    }
    update();
    return true;
}

bool CView::applyTerrainVerticalScalePreview(
    const STerrainVerticalScaleCommand& command, bool forward)
{
    if(!m_terrainEditAvailable)
        return false;
    CValidationReport report;
    if(!m_terrainEdit.apply(command, forward, &report))
    {
        ei::log(eLogWarning, "Vertical-scale preview command refused:\n" + report.toText());
        return false;
    }
    if(!m_pLand->previewCanonicalMaxZ(m_terrainEdit.maxZ()))
    {
        m_terrainEdit.apply(command, !forward, nullptr);
        ei::log(eLogWarning, "Vertical-scale preview renderer refused maxZ");
        return false;
    }

    QSet<QPair<int, int>> sectors;
    const int sectorCountX = int(m_mprDocument.mapInfo().sectorCountX);
    const int sectorCountY = int(m_mprDocument.mapInfo().sectorCountY);
    for(const STerrainHeightDelta& delta : command.heights.deltas)
    {
        QVector<int> xs{qMin(delta.globalX / 32, sectorCountX - 1)};
        QVector<int> ys{qMin(delta.globalY / 32, sectorCountY - 1)};
        if(delta.globalX > 0 && delta.globalX % 32 == 0)
            xs.append(xs.front() - 1);
        if(delta.globalY > 0 && delta.globalY % 32 == 0)
            ys.append(ys.front() - 1);
        for(int x : xs)
            for(int y : ys)
                if(x >= 0 && y >= 0)
                    sectors.insert(qMakePair(x, y));
    }
    for(const QPair<int, int>& sector : sectors)
        m_pLand->previewCanonicalSector(sector.first, sector.second,
            m_terrainEdit.sectorPayload(sector.first, sector.second));

    for(CMob* mob : m_aMob)
        mob->reprojectForCurrentTerrain();
    m_pLand->setDirty(m_terrainEdit.isDirty());
    m_aiGraphOverlayDirty = true;
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag,
                              m_terrainEdit.isDirty() ? "*" : "");
    emit updateMsg(tr("Vertical range changed to %1; %2 vertex/vertices were requantized and navigation data may need review.")
        .arg(m_terrainEdit.maxZ(), 0, 'g', 8)
        .arg(command.heights.deltas.size()));
    refreshMprSettingsDock();
    update();
    return true;
}

void CView::importTerrainHeightmap()
{
    if(!m_terrainEditAvailable)
    {
        QMessageBox::information(this, tr("Heightmap import"),
            tr("Open a supported MPR before importing a heightmap."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("Import grayscale heightmap"),
        QString(), tr("Heightmaps (*.png *.bmp *.tif *.tiff);;Images (*)"));
    if(path.isEmpty())
        return;
    QImage image(path);
    if(image.isNull())
    {
        QMessageBox::warning(this, tr("Heightmap import"), tr("The image could not be decoded."));
        return;
    }
    bool accepted = false;
    const double minimum = QInputDialog::getDouble(this, tr("Heightmap minimum"),
        tr("Raw MPR height for black (0-65535):"), 0.0, 0.0, 65535.0, 0, &accepted);
    if(!accepted)
        return;
    const double maximum = QInputDialog::getDouble(this, tr("Heightmap maximum"),
        tr("Raw MPR height for white (0-65535):"), 65535.0, 0.0, 65535.0, 0, &accepted);
    if(!accepted || maximum < minimum)
    {
        if(accepted)
            QMessageBox::warning(this, tr("Heightmap import"), tr("Maximum height must not be below minimum height."));
        return;
    }

    STerrainHeightmapGrid grid;
    grid.width = image.width();
    grid.height = image.height();
    grid.samples.reserve(grid.width * grid.height);
    const bool flip = m_landscapeTools && m_landscapeTools->flipHeightmapVertically();
    for(int outputY = 0; outputY < grid.height; ++outputY)
    {
        const int imageY = flip ? grid.height - 1 - outputY : outputY;
        for(int x = 0; x < grid.width; ++x)
            grid.samples.append(float(qGray(image.pixel(x, imageY))) / 255.0f);
    }
    STerrainHeightmapImportSettings settings;
    settings.placement = ETerrainHeightmapPlacement::StretchToTerrain;
    settings.minimumRawHeight = float(minimum);
    settings.maximumRawHeight = float(maximum);
    settings.strength = 1.0f;
    CValidationReport report;
    const STerrainHeightCommand command = m_terrainEdit.makeHeightmapImport(grid, settings, &report);
    if(command.isEmpty() || report.hasErrors() || command.requiresMaxZDecision())
    {
        QMessageBox::warning(this, tr("Heightmap import"),
            tr("The heightmap preview was refused.\n\n%1").arg(report.toText()));
        return;
    }
    const QString orientation = flip ? tr("bottom row first") : tr("top row first");
    if(QMessageBox::question(this, tr("Confirm heightmap preview"),
        tr("Stretch %1 x %2 samples across %3 x %4 terrain vertices?\n"
           "Orientation: %5\nRaw range: %6 to %7\n\n"
           "This is undoable and can be committed through the validated MPR save path.")
            .arg(grid.width).arg(grid.height)
            .arg(m_terrainEdit.vertexCountX()).arg(m_terrainEdit.vertexCountY())
            .arg(orientation).arg(minimum).arg(maximum)) != QMessageBox::Yes)
        return;
    m_pUndoStack->push(new CTerrainHeightPreviewCommand(this, command));
}

void CView::mouseMoveEvent(QMouseEvent* event)
{
    m_lastViewportMousePos = event->pos();
    m_hasViewportMousePos = rect().contains(event->pos());
    if(m_landscapeInteractionSuspended)
    {
        event->accept();
        return;
    }
    if(m_landscapeTools && m_landscapeTools->paintIssueMarkingEnabled())
    {
        QPoint cell;
        if(terrainCellAtViewport(event->pos(), &cell))
        {
            const QPoint previous = m_paintIssueHoverValid
                ? m_paintIssueHoverCell : cell;
            m_paintIssueHoverCell = cell;
            m_paintIssueHoverValid = true;
            if(m_paintIssueDragActive && (event->buttons() & Qt::LeftButton))
            {
                const int dx = cell.x() - previous.x();
                const int dy = cell.y() - previous.y();
                const int steps = qMax(qAbs(dx), qAbs(dy));
                for(int step = 0; step <= steps; ++step)
                {
                    const int x = steps == 0 ? cell.x()
                        : previous.x() + qRound(double(dx) * step / steps);
                    const int y = steps == 0 ? cell.y()
                        : previous.y() + qRound(double(dy) * step / steps);
                    m_paintIssueSelectedCells.insert(QPoint(x, y));
                }
            }
            QToolTip::showText(mapToGlobal(event->pos() + QPoint(18, 18)),
                tr("MARK: %1").arg(paintIssueCategoryLabel(
                    m_landscapeTools->paintIssueCategory())),
                this, rect(), 60000);
            update();
        }
        else
        {
            m_paintIssueHoverValid = false;
            QToolTip::hideText();
            update();
        }
        event->accept();
        return;
    }
    if(m_landscapeTools && m_landscapeTools->sculptEnabled()
       && m_terrainEditAvailable)
    {
        const bool overlaySculpt = m_landscapeTools->overlaySculptEnabled();
        m_sculptOutlineCenter = getTerrainPos(event->x(), event->y(), !overlaySculpt);
        m_sculptOutlineValid = true;
        update();
    }
    if(m_sculptStrokeActive && (event->buttons() & Qt::LeftButton)
        && m_landscapeTools && m_terrainEditAvailable)
    {
        const bool overlaySculpt = m_landscapeTools->overlaySculptEnabled();
        const QVector3D position = getTerrainPos(event->x(), event->y(), !overlaySculpt);
        m_lastSculptPoint = position;
        // The repeat timer applies the latest point. This keeps pointer input
        // responsive and caps canonical preview work to one stamp per tick.
        event->accept();
        return;
    }
    m_pOp->mouseMoveEvent(event);
}

void CView::mouseReleaseEvent(QMouseEvent *event)
{
    m_lastViewportMousePos = event->pos();
    m_hasViewportMousePos = rect().contains(event->pos());
    if(m_landscapeInteractionSuspended)
    {
        event->accept();
        return;
    }
    if(m_paintIssueDragActive && event->button() == Qt::LeftButton)
    {
        QPoint cell;
        if(terrainCellAtViewport(event->pos(), &cell))
        {
            m_paintIssueHoverValid = true;
            m_paintIssueHoverCell = cell;
            m_paintIssueSelectedCells.insert(cell);
        }
        m_paintIssueDragActive = false;
        appendMarkedTerrainPaintIssue(m_paintIssueSelectedCells,
            m_landscapeTools ? m_landscapeTools->paintIssueCategory()
                             : QStringLiteral("no-blend"));
        update();
        event->accept();
        return;
    }
    if(m_sculptStrokeActive && event->button() == Qt::LeftButton)
    {
        if(m_lastSculptPoint.distanceToPoint(m_lastSculptStampPoint) > 0.01f)
            sculptTerrainTowardLatestPoint();
        finishSculptStroke();
        event->accept();
        return;
    }
    m_pOp->mouseReleaseEvent(event);
}

QPoint CView::operationAnchor() const
{
    if(m_hasViewportMousePos && rect().contains(m_lastViewportMousePos))
        return m_lastViewportMousePos;
    return rect().center();
}

void CView::setTestSpawnMarker(const QVector3D& position, bool adjusted)
{
    m_testSpawnMarker = position;
    m_testSpawnMarkerValid = true;
    m_testSpawnMarkerAdjusted = adjusted;
    update();
}

void CView::setTestSpawnMarkerSelected(bool selected)
{
    m_testSpawnMarkerSelected = m_testSpawnMarkerValid && selected;
    update();
}

bool CView::testSpawnMarkerAt(const QPoint& viewportPosition, int padding) const
{
    if(!m_testSpawnMarkerValid || !m_cam || width() <= 0 || height() <= 0)
        return false;

    const QRect viewport(0, 0, width(), height());
    const auto projectToWidget = [&](const QVector3D& world, QPointF& result) {
        const QVector3D projected = world.project(
            m_cam->viewMatrix(), m_projection, viewport);
        if(projected.z() < 0.0f || projected.z() > 1.0f)
            return false;
        // QVector3D::project uses the OpenGL bottom-left viewport origin,
        // while QMouseEvent positions use Qt's top-left widget origin.
        result = QPointF(projected.x(), height() - projected.y());
        return true;
    };

    const float baseZ = m_testSpawnMarker.z() + 0.08f;
    QPointF capsuleBottom;
    QPointF capsuleTop;
    if(!projectToWidget(QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(), baseZ),
                        capsuleBottom)
       || !projectToWidget(QVector3D(m_testSpawnMarker.x(), m_testSpawnMarker.y(),
                                     baseZ + 1.93f), capsuleTop))
        return false;

    const QPointF capsuleAxis = capsuleTop - capsuleBottom;
    const double axisLengthSquared = QPointF::dotProduct(capsuleAxis, capsuleAxis);
    const auto distanceSquaredToCapsuleAxis = [&](const QPointF& point) {
        if(axisLengthSquared <= 0.0001)
        {
            const QPointF delta = point - capsuleBottom;
            return QPointF::dotProduct(delta, delta);
        }
        const QPointF fromBottom = point - capsuleBottom;
        const double fraction = qBound(
            0.0, QPointF::dotProduct(fromBottom, capsuleAxis) / axisLengthSquared, 1.0);
        const QPointF delta = point - (capsuleBottom + capsuleAxis * fraction);
        return QPointF::dotProduct(delta, delta);
    };

    // Fit the clickable capsule to the same ground and head rings that are
    // drawn for the marker. This covers the complete hero silhouette at every
    // camera angle instead of approximating it with a small circle at its
    // centre. A little screen-space padding keeps the outline easy to pick.
    double radiusSquared = 0.0;
    const int segments = 24;
    for(int index = 0; index < segments; ++index)
    {
        const float angle = float(index) * 2.0f * float(M_PI) / float(segments);
        QPointF projected;
        if(projectToWidget(QVector3D(m_testSpawnMarker.x() + qCos(angle) * 0.55f,
                                     m_testSpawnMarker.y() + qSin(angle) * 0.55f,
                                     baseZ), projected))
            radiusSquared = qMax(radiusSquared, distanceSquaredToCapsuleAxis(projected));
        if(projectToWidget(QVector3D(m_testSpawnMarker.x() + qCos(angle) * 0.24f,
                                     m_testSpawnMarker.y(),
                                     baseZ + 1.93f + qSin(angle) * 0.24f), projected))
            radiusSquared = qMax(radiusSquared, distanceSquaredToCapsuleAxis(projected));
    }

    const double capsuleRadius = qMax(12.0, qSqrt(radiusSquared))
        + qMax(0, padding);
    return distanceSquaredToCapsuleAxis(QPointF(viewportPosition))
        <= capsuleRadius * capsuleRadius;
}

void CView::clearTestSpawnMarker()
{
    m_testSpawnMarkerValid = false;
    m_testSpawnMarkerSelected = false;
    update();
}

void CView::setMapSkyType(const QString& skyType, const QString& region)
{
    const bool cave = skyType.compare(QStringLiteral("cave"),
                                      Qt::CaseInsensitive) == 0;
    const QString normalizedRegion = region.compare(QStringLiteral("ingos"),
        Qt::CaseInsensitive) == 0 ? QStringLiteral("ingos") : QStringLiteral("gipat");
    if(m_caveEnvironment == cave && m_weatherRegion == normalizedRegion)
        return;
    m_caveEnvironment = cave;
    m_weatherRegion = normalizedRegion;
    m_shadowInitialized = false;
    m_staticShadowFingerprint.clear();
    m_farStaticShadowFingerprint.clear();
    update();
}

void CView::setTestEntranceOverlays(const QVector<QRectF>& rectangles,
                                    const QStringList& labels,
                                    int selectedIndex)
{
    m_testEntranceRectangles = rectangles;
    m_testEntranceLabels = labels;
    m_testEntranceLabelImages.clear();
    const QFont labelFont = font();
    const QFontMetrics metrics(labelFont);
    for(const QString& label : labels)
    {
        QRect bounds = metrics.boundingRect(QRect(0, 0, 240, 96),
                                            Qt::AlignCenter | Qt::TextWordWrap,
                                            label);
        bounds.setSize(bounds.size().expandedTo(QSize(36, metrics.height())));
        QImage image(bounds.width() + 12, bounds.height() + 8,
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 20, 180));
        painter.drawRoundedRect(image.rect().adjusted(0, 0, -1, -1), 4, 4);
        painter.setFont(labelFont);
        painter.setPen(QColor(255, 245, 180));
        painter.drawText(image.rect().adjusted(6, 4, -6, -4),
                         Qt::AlignCenter | Qt::TextWordWrap, label);
        m_testEntranceLabelImages.append(image);
    }
    m_testEntranceLabelTexturesDirty = true;
    m_selectedTestEntrance = selectedIndex;
    update();
}

void CView::moveCameraToPoint(const QVector3D& position)
{
    QVector3D terrainPosition(position.x(), position.y(), 0.0f);
    if(CLandscape::getInstance()->projectPt(terrainPosition))
    {
        m_cam->moveTo(terrainPosition);
        update();
    }
}

bool CView::terrainPositionAt(const QPoint& viewportPosition, QVector3D* position)
{
    if(!position || !m_pLand || !m_pLand->isMprLoad()
       || !rect().contains(viewportPosition) || width() <= 0 || height() <= 0)
        return false;

    const QRect viewport(0, 0, width(), height());
    const float screenY = float(height() - viewportPosition.y());
    const QMatrix4x4 view = m_cam->viewMatrix();
    const QVector3D rayStart = QVector3D(float(viewportPosition.x()), screenY, 0.0f)
        .unproject(view, m_projection, viewport);
    const QVector3D rayEnd = QVector3D(float(viewportPosition.x()), screenY, 1.0f)
        .unproject(view, m_projection, viewport);

    bool havePrevious = false;
    float previousT = 0.0f;
    float previousDelta = 0.0f;
    constexpr int steps = 512;
    for(int step = 0; step <= steps; ++step)
    {
        const float t = float(step) / float(steps);
        const QVector3D rayPoint = rayStart + (rayEnd - rayStart) * t;
        QVector3D terrain(rayPoint.x(), rayPoint.y(), 0.0f);
        if(!CLandscape::getInstance()->projectPt(terrain))
        {
            havePrevious = false;
            continue;
        }
        const float delta = rayPoint.z() - terrain.z();
        if(havePrevious && previousDelta >= 0.0f && delta <= 0.0f)
        {
            float low = previousT;
            float high = t;
            QVector3D resolved = terrain;
            for(int iteration = 0; iteration < 14; ++iteration)
            {
                const float middle = (low + high) * 0.5f;
                const QVector3D sample = rayStart + (rayEnd - rayStart) * middle;
                QVector3D ground(sample.x(), sample.y(), 0.0f);
                if(!CLandscape::getInstance()->projectPt(ground))
                {
                    low = middle;
                    continue;
                }
                resolved = ground;
                if(sample.z() - ground.z() > 0.0f)
                    low = middle;
                else
                    high = middle;
            }
            *position = resolved;
            return true;
        }
        havePrevious = true;
        previousT = t;
        previousDelta = delta;
    }
    return false;
}

void CView::anchorCameraRotationToViewportCenter()
{
    if(!m_cam || width() <= 0 || height() <= 0)
        return;
    bool hit = false;
    const QVector3D center = getTerrainPos(width() / 2, height() / 2, true, &hit);
    if(hit)
        m_cam->setOrbitPivotPreservingView(center);
}

void CView::wheelEvent(QWheelEvent* event)
{
    const bool landscapeModifier = event->modifiers()
        & (Qt::AltModifier | Qt::ShiftModifier);
    if(m_landscapeTools && m_landscapeTools->editingEnabled()
       && landscapeModifier)
    {
        if(m_landscapeTools->sculptEnabled())
        {
            const int step = landscapeWheelStep(event->angleDelta().y(),
                                                event->delta(), event->pixelDelta().y());
            if(step == 0)
                return;
            if(event->modifiers() & Qt::ShiftModifier)
            {
                m_landscapeTools->adjustSculptStrength(step);
                emit updateMsg(tr("Sculpt strength: %1").arg(
                    m_landscapeTools->strength(), 0, 'f', 2));
            }
            else
            {
                m_landscapeTools->adjustSculptRadius(step);
                emit updateMsg(tr("Sculpt radius: %1 vertices").arg(
                    m_landscapeTools->radius(), 0, 'f', 1));
            }
            update();
            event->accept();
            return;
        }
        if(m_landscapeTools->tilePaintEnabled()
           || m_landscapeTools->overlayPaintEnabled())
        {
            m_pOp->wheelEvent(event);
            if(event->isAccepted())
                return;
        }
    }

    QString gesture = "Ctrl+Wheel";
    if(m_pSettings)
        if(const COptString* option = dynamic_cast<const COptString*>(
               m_pSettings->opt(eOptSetKeyBinding, "cameraSpeedWheelGesture")))
            gesture = option->value();
    Qt::KeyboardModifier modifier = Qt::ControlModifier;
    if(gesture.startsWith("Alt", Qt::CaseInsensitive)) modifier = Qt::AltModifier;
    else if(gesture.startsWith("Shift", Qt::CaseInsensitive)) modifier = Qt::ShiftModifier;
    else if(gesture.startsWith("Meta", Qt::CaseInsensitive)) modifier = Qt::MetaModifier;
    if(event->modifiers() & modifier)
    {
        const double direction = event->angleDelta().y() >= 0 ? 0.1 : -0.1;
        setCameraSpeedMultiplier(cameraSpeedMultiplier() + direction);
        event->accept();
        return;
    }
    m_pOp->wheelEvent(event);
}

void CView::focusOutEvent(QFocusEvent *event)
{
    Q_UNUSED(event);
    finishSculptStroke();
    auto keySet = m_pOp->keyManager()->keys();
    for(auto& key : keySet)
    {
        m_pOp->keyRelease(key);
    }
}

// draw objects without light and textures, only colored triangles; find suitable object
// x,y - mouse position
CNode* CView::pickObject(QList<CNode*>& aNode, int x, int y)
{
    // Bind shader pipeline for use
    if (!m_selectProgram.bind())
        close();

    const QMatrix4x4 viewMatrix = m_cam->viewMatrix();
    m_selectProgram.setUniformValue("u_projMmatrix", m_projection);
    m_selectProgram.setUniformValue("u_viewMmatrix", viewMatrix);
    ei::gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const COptBool* cullingOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("frustumCulling")) : nullptr;
    const COptBool* drawLogicOption = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("drawLogic")) : nullptr;
    const bool cullingEnabled = (!cullingOption || cullingOption->value())
        && (!drawLogicOption || !drawLogicOption->value())
        && CScene::getInstance()->getMode() != eEditModeLogic;
    const CViewFrustum frustum(m_projection * viewMatrix);

    for (auto node: aNode)
    {
        bool drawNode = true;
        if(cullingEnabled && node && node->nodeState() != ENodeState::eSelect)
        {
            CBox bounds = node->getBBox();
            drawNode = !bounds.isInit() || frustum.intersectsSphere(
                bounds.center() + node->drawPosition(), bounds.radius());
        }
        if(drawNode)
            node->drawSelect(&m_selectProgram);
    }

    GLubyte pixel[3];
    ei::gl::ReadPixels(x, m_height-y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel);
    SColor pickedColor(pixel[0], pixel[1], pixel[2]);

    for (auto& node: aNode)
    {
        if (node->isColorSuitable(pickedColor))
            return node;
    }

    return nullptr;
}

//clear buffer, draw only landscape, project point and return vector3D
QVector3D CView::getTerrainPos(const int cursorPosX, const int cursorPosY,
                              bool bLand, bool* hit)
{
    QVector3D point(0,0,0);
    if(hit)
        *hit = false;
    if(!m_pLand || !m_pLand->isMprLoad())
        return point;

    QMatrix4x4 camMatrix = m_cam->viewMatrix();
    // Set modelview-projection matrix

    // Bind shader pipeline for use
    if (!m_landProgram.bind())
        close();

    // Set modelview-projection matrix
    m_landProgram.setUniformValue("u_projMmatrix", m_projection);
    m_landProgram.setUniformValue("u_viewMmatrix", camMatrix);
    ei::gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if(bLand)
        m_pLand->draw(&m_landProgram);
    else
    {
        m_pLand->drawWater(&m_landProgram);
    }
    const int posY (height() - cursorPosY);
    float z;
    ei::gl::ReadPixels(cursorPosX, posY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z );
    if(hit)
        *hit = qIsFinite(z) && z >= 0.0f && z < 1.0f;
    //QVector3D point (cursorPosX, posY, z);
    point.setX(cursorPosX);
    point.setY(posY);
    point.setZ(z);

    QMatrix4x4 view = m_cam->viewMatrix();
    GLint viewPort[4];
    ei::gl::GetIntegerv(GL_VIEWPORT, viewPort);
    QRect viewPortRect(viewPort[0], viewPort[1], viewPort[2], viewPort[3]);
    return point.unproject(view, m_projection, viewPortRect); //return Vector3D here
}

void CView::changeOperation(EButtonOp type, bool preserveLandscapeEditing)
{
    cancelPendingOperation();
    const bool landscapePaintMode = m_landscapeTools
        && (m_landscapeTools->editMode() == ELandscapeEditMode::TilePaint
            || m_landscapeTools->editMode() == ELandscapeEditMode::OverlaySurface
            || m_landscapeTools->editMode() == ELandscapeEditMode::OverlayPaint);
    if(m_landscapeTools && landscapeOperationDisablesEditing(
           type, landscapePaintMode, preserveLandscapeEditing))
        m_landscapeTools->setEditingEnabled(false, false);
    switch (type) {
    case EButtonOpSelect:
        m_pOp->changeState(new CSelect(this)); break;
    case EButtonOpMove:
        m_pOp->changeState(new CMoveAxis(this, EOperateAxisXY)); break;
    case EButtonOpRotate:
        m_pOp->changeState(new CRotateAxis(this, EOperateAxisZ)); break;
    case EButtonOpScale:
        m_pOp->changeState(new CScaleAxis(this, EOperateAxisZ)); break;
    case EButtonOpTilebrush:
        m_pOp->changeState(new CTileBrush(this)); break;
    }
}

bool CView::cancelPendingOperation()
{
    if(!m_pOp)
        return false;

    const EOperationMethod method = m_pOp->operationMethod();
    const bool cancelPastePreview = m_pendingOperationOwnsPaste;
    const bool cancelLogicPlacement = m_pendingLogicPlacementMacro;
    CMob* pasteMob = m_pendingPasteMob;
    const QList<CNode*> pasteNodes = m_pendingPasteNodes;
    const QList<CNode*> originalSelection = m_pendingPasteOriginalSelection;
    QString methodName;
    switch(method)
    {
    case eOperationMethodMoveAxis:
        methodName = "move";
        operationRevert(EOperationAxisType::eMove);
        break;
    case eOperationMethodRotateAxis:
        methodName = "rotate";
        operationRevert(EOperationAxisType::eRotate);
        break;
    case eOperationMethodScaleAxis:
        methodName = "scale";
        operationRevert(EOperationAxisType::eScale);
        break;
    case eOperationMethodTileBrush:
        methodName = "tile-brush";
        endTileBrushGroup();
        setPreviewTile(false);
        unsetCursor();
        showOutliner(true);
        break;
    case eOperationMethodSelect:
        return false;
    }

    const int trackedObjects = m_operationBackup.size();
    m_operationBackup.clear();
    m_unitMovementBackup.clear();
    m_pendingOperationOwnsPaste = false;
    m_pendingLogicPlacementMacro = false;
    m_pendingPasteMob = nullptr;
    m_pendingPasteNodes.clear();
    m_pendingPasteOriginalSelection.clear();

    if(cancelPastePreview && pasteMob)
    {
        for(CNode* node : pasteNodes)
        {
            if(!node || !pasteMob->nodes().contains(node))
                continue;
            const uint nodeId = node->mapId();
            pasteMob->discardUncommittedNode(node);
            if(m_pTree)
                m_pTree->onNodeDelete(nodeId);
        }
        restoreSelection(originalSelection);
        setDurty(pasteMob);
    }
    if(cancelLogicPlacement && m_pUndoStack)
    {
        m_pUndoStack->endMacro();
        m_pUndoStack->undo();
    }
    m_pOp->changeState(new CSelect(this));
    ei::log(eLogInfo, QString("Cancelled pending viewport operation method:%1 trackedObjects:%2")
            .arg(methodName).arg(trackedObjects));
    return true;
}

bool CView::hasPendingOperation() const
{
    return m_pOp && m_pOp->operationMethod() != eOperationMethodSelect;
}

bool CView::prepareDocumentTransition()
{
    finishSculptStroke();
    if(!m_pOp)
        return false;
    const EOperationMethod method = m_pOp->operationMethod();
    if(method == eOperationMethodTileBrush)
    {
        // Test staging is modal, but tile painting is the selected persistent
        // tool rather than a pending object transform. End the current stroke
        // and clear transient feedback without switching the operation to
        // Select; the next viewport move/click can immediately paint again.
        endTileBrushGroup();
        setPreviewTile(false);
        m_terrainPaintOutlineValid = false;
        m_sculptOutlineValid = false;
        unsetCursor();
        update();
        return false;
    }
    return operationRequiresCancellationForDocumentTransition(method)
        ? cancelPendingOperation() : false;
}

bool CView::prepareUndo()
{
    return prepareHistoryNavigation();
}

bool CView::prepareHistoryNavigation()
{
    if(!m_pOp || operationAllowsHistoryNavigation(m_pOp->operationMethod()))
        return true;

    // Unfinished transforms are previews, not committed history entries.
    // Cancelling a paste preview also removes its temporary objects.
    // Tile painting is a persistent tool, so Undo and Redo operate on history
    // without cancelling the brush or consuming a key press first.
    cancelPendingOperation();
    return false;
}

void CView::operationSetBackup(EOperationAxisType operationType)
{
    if(nullptr == m_activeMob)
        return;

    m_operationBackup.clear();
    m_unitMovementBackup.clear();
    for (auto& node : CScene::getInstance()->getMode() == eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if (node->nodeState() != ENodeState::eSelect)
            continue;
        if(!node->isOperationAxisAllow(operationType))
            continue;

        switch (operationType)
        {
        case EOperationAxisType::eMove:
            m_operationBackup[node] = node->position();
            if(CUnit* unit = dynamic_cast<CUnit*>(node))
                m_unitMovementBackup.insert(unit, unit->movementState());
            break;
        case EOperationAxisType::eRotate:
            m_operationBackup[node] = node->getEulerRotation(); break;
        case EOperationAxisType::eScale:
            m_operationBackup[node] = node->constitution(); break;
        }
    }
}

void CView::operationRevert(EOperationAxisType operationType)
{
    QQuaternion quat;
    if(m_operationBackup.isEmpty())
        return;

    for(auto& pair : m_operationBackup.toStdMap())
        switch (operationType)
        {
        case EOperationAxisType::eMove:
        {
            CUnit* unit = dynamic_cast<CUnit*>(pair.first);
            if(unit && m_unitMovementBackup.contains(unit))
                unit->restoreMovementState(m_unitMovementBackup.value(unit));
            else
                pair.first->updatePos(pair.second);
            break;
        }
        case EOperationAxisType::eRotate:
        {
            quat = QQuaternion::fromEulerAngles(pair.second);
            pair.first->setRot(quat);
            break;
        }
        case EOperationAxisType::eScale:
            pair.first->setConstitution(pair.second); break;
        }

}

void CView::operationApply(EOperationAxisType operationType)
{
    const bool committingPaste = m_pendingOperationOwnsPaste
            && m_pendingPasteMob && !m_pendingPasteNodes.isEmpty();
    if(committingPaste)
    {
        m_pUndoStack->beginMacro(QString("Paste %1 object%2")
                .arg(m_pendingPasteNodes.size())
                .arg(m_pendingPasteNodes.size() == 1 ? "" : "s"));
        for(CNode* node : m_pendingPasteNodes)
        {
            CCreateNodeCommand* create = new CCreateNodeCommand(this, m_pendingPasteMob, node);
            QObject::connect(create, SIGNAL(addNodeSignal(CNode*)), m_pTree, SLOT(addNodeToTree(CNode*)));
            QObject::connect(create, SIGNAL(undo_addNodeSignal(uint)), m_pTree, SLOT(onNodeDelete(uint)));
            m_pUndoStack->push(create);
        }
        m_pUndoStack->push(new CSelectionCommand(this, m_pendingPasteOriginalSelection,
                                                  m_pendingPasteNodes,
                                                  "Select pasted objects"));
    }

    QQuaternion quat;
    QVector3D rot;
    // A paste and its automatically-started placement transform are one user
    // action. Keep the Paste macro open through the transform commands so one
    // Undo removes the pasted objects instead of merely moving them back.
    const bool grouped = !committingPaste && !m_pendingLogicPlacementMacro
            && m_operationBackup.size() > 1;
    if(grouped)
    {
        QString action = "Transform";
        if(operationType == EOperationAxisType::eMove)
            action = "Move";
        else if(operationType == EOperationAxisType::eRotate)
            action = "Rotate";
        else if(operationType == EOperationAxisType::eScale)
            action = "Scale";
        m_pUndoStack->beginMacro(QString("%1 %2 objects").arg(action).arg(m_operationBackup.size()));
    }
    for(auto& pair : m_operationBackup.toStdMap())
    {
        switch (operationType)
        {
        case EOperationAxisType::eMove:
        {
            if(CScene::getInstance()->getMode() == eEditModeLogic)
            {
                ENodeType type = pair.first->nodeType();
                switch(type)
                {
                case eUnit:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeProp* pOp = new CChangeProp(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]);
                    m_pUndoStack->push(pOp);
                    break;
                }
                case ePatrolPoint:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeLogicParam* pOp = new CChangeLogicParam(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]); //revert position to start (it will apply in 'push' operation
                    m_pUndoStack->push(pOp);
                    break;
                }
                case eLookPoint:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeLogicParam* pOp = new CChangeLogicParam(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]); //revert position to start (it will apply in 'push' operation
                    m_pUndoStack->push(pOp);
                    break;
                }
                case eTrapActZone:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeLogicParam* pOp = new CChangeLogicParam(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]); //revert position to start (it will apply in 'push' operation
                    m_pUndoStack->push(pOp);
                    break;
                }
                case eTrapCastPoint:
                {
                    QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                    CChangeLogicParam* pOp = new CChangeLogicParam(this, pair.first, pos);
                    QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                    pair.first->updatePos(m_operationBackup[pair.first]); //revert position to start (it will apply in 'push' operation
                    m_pUndoStack->push(pOp);
                    break;
                }
                default:
                    break;
                }


            }
            else
            {
                QSharedPointer<IPropertyBase> pos (new prop3D(eObjParam_POSITION, pair.first->position()));
                CChangeProp* pOp = new CChangeProp(this, pair.first, pos);
                QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
                CUnit* unit = dynamic_cast<CUnit*>(pair.first);
                if(unit && m_unitMovementBackup.contains(unit))
                    unit->restoreMovementState(m_unitMovementBackup.value(unit));
                else
                    pair.first->updatePos(m_operationBackup[pair.first]);
                m_pUndoStack->push(pOp);
            }

            break;
        }
        case EOperationAxisType::eRotate:
        {
            rot = pair.first->getEulerRotation();
            QSharedPointer<IPropertyBase> rotation (new prop3D(eObjParam_ROTATION, rot));
            CChangeProp* pOp = new CChangeProp(this, pair.first, rotation);
            QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            quat = QQuaternion::fromEulerAngles(m_operationBackup[pair.first]);
            pair.first->setRot(quat);
            m_pUndoStack->push(pOp);
            break;
        }
        case EOperationAxisType::eScale:
        {
            QSharedPointer<IPropertyBase> complection (new prop3D(eObjParam_COMPLECTION, pair.first->constitution()));
            CChangeProp* pOp = new CChangeProp(this, pair.first, complection);
            QObject::connect(pOp, SIGNAL(updateParam()), this, SLOT(viewParameters()));
            pair.first->setConstitution(m_operationBackup[pair.first]);
            m_pUndoStack->push(pOp);
            break;
        }
        }

    }
    if(grouped)
        m_pUndoStack->endMacro();
    if(committingPaste)
        m_pUndoStack->endMacro();
    if(m_pendingLogicPlacementMacro)
        m_pUndoStack->endMacro();
    m_operationBackup.clear();
    m_unitMovementBackup.clear();
    m_pendingOperationOwnsPaste = false;
    m_pendingLogicPlacementMacro = false;
    m_pendingPasteMob = nullptr;
    m_pendingPasteNodes.clear();
    m_pendingPasteOriginalSelection.clear();
}

void CView::moveTo(QVector3D &dir)
{
    if(nullptr == m_activeMob)
        return;

    QVector3D pos;
    for (auto& node : CScene::getInstance()->getMode() == eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if (node->nodeState() != ENodeState::eSelect)
            continue;

        pos = node->position() + dir;
        node->updatePos(pos);
    }

    updateParameter(EObjParam::eObjParam_POSITION);
}

void CView::rotateTo(QVector3D &rot)
{
    const float gimbalAvoidStep = 0.001f;
    QVector3D rotation; //this rotation will be tested
    QQuaternion quat; //this rotation as quat be applied to object
    QVector3D eulerRot; // temp variable
    const auto setNotNan = [&quat, &eulerRot, &gimbalAvoidStep](QVector3D& ch_rot)
    {
        for(int i(0); i< 100; ++i)
        {
            quat = QQuaternion::fromEulerAngles(ch_rot);
            eulerRot = quat.toEulerAngles();
            if(eulerRot.x() != eulerRot.x())
            {
                ch_rot.setX(ch_rot.x() + gimbalAvoidStep);
            }
            else if(eulerRot.y() != eulerRot.y())
            {
                ch_rot.setY(ch_rot.y() + gimbalAvoidStep);
            }
            else if(eulerRot.z() != eulerRot.z())
            {
                ch_rot.setZ(ch_rot.z() + gimbalAvoidStep);
            }
            else
                return;
        }
        return;
    };


    for(auto& node : m_operationBackup.keys())
    {
        rotation = node->getEulerRotation() + rot;
        setNotNan(rotation);
        node->setRot(quat);
    }

    updateParameter(EObjParam::eObjParam_ROTATION);
}

void CView::scaleTo(QVector3D &scale)
{
    if(nullptr == m_activeMob)
        return;

    QVector3D constitution;
    for (auto& node : m_activeMob->nodes())
    {
        if (node->nodeState() != ENodeState::eSelect)
            continue;

        constitution = node->constitution() + scale;
        node->setConstitution(constitution);
    }

    updateParameter(EObjParam::eObjParam_COMPLECTION);
}

void CView::resetUnitLogicPaths()
{
    if(nullptr == m_activeMob)
        return;

    //todo: make undo for this operation
    CNode* pNode = nullptr;
    foreach(pNode, m_activeMob->logicNodes())
    {
        if(pNode->nodeState() != ENodeState::eSelect)
            continue;

        if(pNode->nodeType() != ENodeType::eUnit)
            continue;

        auto pUnit = dynamic_cast<CUnit*>(pNode);
        pUnit->clearPaths();
    }
    m_activeMob->logicNodesUpdate();
    m_activeMob->setDirty();
}

void CView::deleteSelectedNodes()
{
    if(!m_activeMob)
        return;

    QList<CNode*> targets;
    for(CNode* node : CScene::getInstance()->getMode()==eEditModeLogic ? m_activeMob->logicNodes() : m_activeMob->nodes())
    {
        if(node && node->nodeState() == ENodeState::eSelect)
            targets.append(node);
    }
    if(targets.isEmpty())
        return;

    QVector<uint> objectIds;
    for(CNode* node : targets)
    {
        switch(node->nodeType())
        {
        case ePatrolPoint:
        case eLookPoint:
        case eTrapActZone:
        case eTrapCastPoint:
            break;
        default:
            objectIds.append(node->mapId());
            break;
        }
    }
    if(!confirmScriptReferencedObjectIds(objectIds, tr("delete these objects")))
        return;

    const bool grouped = targets.size() > 1;
    if(grouped)
        m_pUndoStack->beginMacro(QString("Delete %1 objects").arg(targets.size()));

    QVector<uint> arrMapId;
    CNode* pNode = nullptr;
    foreach(pNode, targets)
    {
        auto type = pNode->nodeType();
        switch(type)
        {
        case ePatrolPoint:
        {
            auto pPoint = dynamic_cast<CPatrolPoint*>(pNode);
            int unitId(0);
            int patrolId(0);
            m_activeMob->getPatrolHash(unitId, patrolId, pPoint);
            QString hash = QString("%1.%2").arg(unitId).arg(patrolId);
            CDeleteLogicPoint* pOp = new CDeleteLogicPoint(this, hash);
            m_pUndoStack->push(pOp);
            break;
        }
        case eLookPoint:
        {
            auto pPoint = dynamic_cast<CLookPoint*>(pNode);
            int unitId(0);
            int patrolId(0);
            int viewId(0);
            m_activeMob->getViewHash(unitId, patrolId, viewId, pPoint);
            QString hash = QString("%1.%2.%3").arg(unitId).arg(patrolId).arg(viewId);
            CDeleteLogicPoint* pOp = new CDeleteLogicPoint(this, hash);
            m_pUndoStack->push(pOp);
            break;
        }
        case eTrapActZone:
        {
            auto pZone = dynamic_cast<CActivationZone*>(pNode);
            int unitId(0);
            int patrolId(-2);
            int viewId(-2);
            int zoneId(0);
            m_activeMob->getTrapZoneHash(unitId, zoneId, pZone);
            QString hash = QString("%1.%2.%3.%4").arg(unitId).arg(patrolId).arg(viewId).arg(zoneId);
            CDeleteLogicPoint* pOp = new CDeleteLogicPoint(this, hash);
            m_pUndoStack->push(pOp);
            break;
        }
        case eTrapCastPoint:
        {
            auto pCast = dynamic_cast<CTrapCastPoint*>(pNode);
            int unitId(0);
            int patrolId(-2);
            int viewId(-2);
            int zoneId(-2);
            int castPointId(0);
            m_activeMob->getTrapCastHash(unitId, castPointId, pCast);
            QString hash = QString("%1.%2.%3.%4.%5").arg(unitId).arg(patrolId).arg(viewId).arg(zoneId).arg(castPointId);
            CDeleteLogicPoint* pOp = new CDeleteLogicPoint(this, hash);
            m_pUndoStack->push(pOp);
            break;
        }
        default:
        {
            arrMapId.append(pNode->mapId());
            break;
        }
        }
    }

    for(auto& id : arrMapId)
    {
        CDeleteNodeCommand* pUndo = new CDeleteNodeCommand(this, id);
        QObject::connect(pUndo, SIGNAL(deleteNodeSignal(uint)), m_pTree, SLOT(onNodeDelete(uint)));
        QObject::connect(pUndo, SIGNAL(undo_deleteNodeSignal(CNode*)), m_pTree, SLOT(addNodeToTree(CNode*)));
        m_pUndoStack->push(pUndo);
    }

    if(grouped)
        m_pUndoStack->endMacro();
    viewParameters();
    //updateViewTree();
}

void CView::selectedObjectToClipboardBuffer()
{
    if(nullptr == m_activeMob)
        return;

    QJsonArray arrObj;
    for(CNode* pNode : selectedNodes())
        if (pNode->nodeState() & ENodeState::eSelect)
        {
            QJsonObject nodeData = pNode->toJson();
            if(pNode->hasRawObjectProvenance())
            {
                QJsonObject provenance;
                provenance.insert("sourceDocumentSha256",
                    QString::fromLatin1(pNode->rawSourceDocumentSha256().toHex()));
                provenance.insert("sourceObjectPath", pNode->rawSourceObjectPath());
                provenance.insert("sourceSubtreeSha256",
                    QString::fromLatin1(pNode->rawSourceSubtreeSha256().toHex()));
                QByteArray sourceSubtree;
                QByteArray cleanSubtree;
                CMob* sourceMob = ownerMob(pNode);
                if(sourceMob && sourceMob->rawObjectTemplateBytes(pNode, &sourceSubtree, &cleanSubtree))
                {
                    provenance.insert("sourceSubtreeBase64",
                        QString::fromLatin1(sourceSubtree.toBase64()));
                    provenance.insert("cleanSubtreeBase64",
                        QString::fromLatin1(cleanSubtree.toBase64()));
                }
                nodeData.insert("_Endurance raw provenance", provenance);
            }
            arrObj.append(nodeData);
        }

    QJsonObject obj;
    obj.insert("Version", 2);
    auto pos = QWidget::mapFromGlobal(QCursor::pos());
    auto posOnLand = getTerrainPos(pos.x(), pos.y());
    QJsonArray point;
    point.append(QJsonValue::fromVariant(posOnLand.x()));
    point.append(QJsonValue::fromVariant(posOnLand.y()));
    point.append(QJsonValue::fromVariant(posOnLand.z()));
    obj.insert("Mouse position", point);
    obj.insert("Data", arrObj);

    QJsonDocument doc(obj);

    if (!m_clipboard_buffer_file.open(QIODevice::WriteOnly))
    {
        Q_ASSERT("Couldn't open option file." && false);
    }
    else
    {
        m_clipboard_buffer_file.write(doc.toJson(QJsonDocument::JsonFormat::Indented));
        m_clipboard_buffer_file.close();
    }

}

void CView::clipboradObjectsToScene()
{
    if(!m_activeMob)
        return;

    if (!m_clipboard_buffer_file.open(QIODevice::ReadOnly))
    {
        Q_ASSERT("Couldn't open option file." && false);
        return;
    }

    if (m_clipboard_buffer_file.size() == 0)
    {
        qDebug() << "empty copypasteBuffer file";
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(m_clipboard_buffer_file.readAll(), &parseError);
    m_clipboard_buffer_file.close();
    //todo: check if has no error
    QJsonObject obj = doc.object();
    if (obj["Version"].toInt() != 2)
        return;

    auto array = obj["Data"].toArray();
    if(array.isEmpty())
    {
        ei::log(eLogInfo, "clipboard empty");
        return;
    }

    const QList<CNode*> originalSelection = selectionSnapshot();
    QList<CNode*> previewNodes;
    for(CMob* mob : m_aMob)
        if(mob) mob->clearSelect(true);
    for(auto it = array.begin(); it < array.end(); ++it)
    {
        CNode* node = m_activeMob->createNode(it->toObject());
        if(!node)
            continue;
        previewNodes.append(node);
        m_pTree->addNodeToTree(node);
    }
    if(previewNodes.isEmpty())
    {
        restoreSelection(originalSelection);
        return;
    }
    //move copyed nodes to new mouse position
    QVector3D oldMousePos;
    QJsonArray arrPoint = obj["Mouse position"].toArray();
    if(arrPoint.size()==3)
        oldMousePos = QVector3D(arrPoint[0].toVariant().toFloat(), arrPoint[1].toVariant().toFloat(), arrPoint[2].toVariant().toFloat());

    auto pos = QWidget::mapFromGlobal(QCursor::pos());
    auto posOnLand = getTerrainPos(pos.x(), pos.y());
    auto mouseDif = posOnLand - oldMousePos;
    mouseDif.setZ(0.0f);
    m_pendingOperationOwnsPaste = true;
    m_pendingPasteMob = m_activeMob;
    m_pendingPasteNodes = previewNodes;
    m_pendingPasteOriginalSelection = originalSelection;
    m_pOp->changeState(new CMoveAxis(this, EOperateAxisXY));
    moveTo(mouseDif);

    viewParameters();
    emit loadedMapStateChanged();
}

void CView::hideSelectedNodes()
{
    CMob* pMob = nullptr;
    CNode* pNode = nullptr;
    foreach (pMob, m_aMob)
    {
        foreach(pNode, pMob->nodes())
            if(pNode->nodeState() == ENodeState::eSelect)
                pNode->setState(ENodeState::eHidden);
    }
    viewParameters();
}

void CView::unHideAll()
{
    CMob* pMob = nullptr;
    CNode* pNode = nullptr;
    foreach (pMob, m_aMob)
    {
        foreach(pNode, pMob->nodes())
        if(pNode->nodeState() == ENodeState::eHidden)
            pNode->setState(ENodeState::eDraw);
    }

}

void CView::setDirtyMpr()
{
    const bool dirty = m_terrainEditAvailable ? m_terrainEdit.isDirty() : true;
    m_pLand->setDirty(dirty);
    m_staticShadowFingerprint.clear();
    m_farStaticShadowFingerprint.clear();
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag, dirty ? "*" : "");
}

void CView::finishSculptStroke()
{
    if(!m_sculptStrokeActive)
        return;
    m_sculptRepeatTimer->stop();
    m_sculptStrokeActive = false;
    const bool overlayStroke = !m_overlaySurfaceStrokeCommand.isEmpty();
    if(m_pUndoStack && overlayStroke)
        m_pUndoStack->push(new COverlaySurfacePreviewCommand(
            this, m_overlaySurfaceStrokeCommand, true));
    else if(m_pUndoStack && !m_sculptStrokeCommand.isEmpty())
        m_pUndoStack->push(new CTerrainHeightPreviewCommand(
            this, m_sculptStrokeCommand, true));
    if(m_sculptStrokeChanged)
    {
        if(!overlayStroke)
            for(CMob* mob : m_aMob)
                mob->reprojectForCurrentTerrain();
        m_pLand->setDirty(m_terrainEdit.isDirty());
        m_staticShadowFingerprint.clear();
        m_farStaticShadowFingerprint.clear();
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataMprDirtyFlag,
                                  m_terrainEdit.isDirty() ? "*" : "");
        emit updateMsg(overlayStroke
            ? tr("Overlay sculpt stroke: %1 stamp(s), %2 sector update(s); liquid navigation may need review.")
                .arg(m_sculptStrokeStamps).arg(m_sculptStrokeVertices)
            : tr("Sculpt stroke: %1 stamp(s), %2 vertex updates; MOB placement and navigation may need review.")
                .arg(m_sculptStrokeStamps).arg(m_sculptStrokeVertices));
    }
}

void CView::setDurty(CMob* pMob)
{
    CMob* changedMob = pMob ? pMob : m_activeMob;
    if(!changedMob)
        return;

    changedMob->refreshDirtyFromProjection();
    // Undo/redo commands retain their owning MOB. If the user switched the
    // active destination after creating a command, reveal and unlock that
    // owner as soon as the command applies instead of changing a locked MOB
    // invisibly in the background.
    if(changedMob != m_activeMob && m_aMob.contains(changedMob))
        changeCurrentMob(changedMob);
    else
        emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag, changedMob->isDirty() ? "*" : "");
    emit mobStateChanged();
}

bool CView::confirmScriptReferencedObjectIds(const QVector<uint>& objectIds,
                                             const QString& operation)
{
    if(!m_activeMob || objectIds.isEmpty())
        return true;
    const QVector<SMobScriptReference> references =
        CScriptReferenceAnalyzer::findObjectIds(m_activeMob->script(), objectIds);
    if(references.isEmpty())
        return true;

    QStringList preview;
    const int previewLimit = qMin(10, references.size());
    for(int index = 0; index < references.size(); ++index)
    {
        const SMobScriptReference& reference = references.at(index);
        const QString detail = QString("ID %1, line %2:%3: %4")
            .arg(reference.objectId).arg(reference.line).arg(reference.column)
            .arg(reference.context);
        ei::log(eLogWarning, QString("Script reference before object edit (%1): %2")
            .arg(operation, detail));
        if(index < previewLimit)
            preview.append(detail);
    }
    if(references.size() > previewLimit)
        preview.append(tr("...and %1 more reference(s); see the workflow log.")
            .arg(references.size() - previewLimit));

    QMessageBox warning(QMessageBox::Warning, tr("Object referenced by MOB script"),
        tr("The active MOB script contains %1 possible reference(s) to the selected object ID(s).\n\n"
           "Continuing may break script logic. Endurance will not rewrite the script automatically.\n\n%2\n\n"
           "Do you want to %3?")
            .arg(references.size()).arg(preview.join('\n'), operation),
        QMessageBox::Yes | QMessageBox::Cancel, this);
    warning.setDefaultButton(QMessageBox::Cancel);
    return warning.exec() == QMessageBox::Yes;
}

void CView::duplicateSelectedObjects()
{
    // Logic points have their own P/L creation workflow and must not cause
    // Ctrl+D to replay a stale normal-object clipboard.
    if(!m_activeMob || selectedNodes().isEmpty())
        return;
    selectedObjectToClipboardBuffer();
    clipboradObjectsToScene();
}

bool CView::canConvertSelectedWorldObjectToLever() const
{
    if(!m_activeMob || CScene::getInstance()->getMode() != eEditModeObjects)
        return false;
    CNode* candidate = nullptr;
    for(CMob* mob : m_aMob)
        if(mob)
            for(CNode* node : mob->nodes())
                if(node && node->nodeState() == ENodeState::eSelect)
                {
                    if(candidate || mob != m_activeMob || node->nodeType() != eWorldObject)
                        return false;
                    candidate = node;
                }
    return candidate && !hasPendingOperation();
}

void CView::convertSelectedWorldObjectToLever()
{
    if(!canConvertSelectedWorldObjectToLever() || !m_pUndoStack)
        return;
    CWorldObj* worldObject = nullptr;
    for(CNode* node : m_activeMob->nodes())
        if(node && node->nodeState() == ENodeState::eSelect)
        {
            worldObject = dynamic_cast<CWorldObj*>(node);
            break;
        }
    if(!worldObject || !worldObject->hasRawObjectProvenance())
        return;
    if(!confirmScriptReferencedObjectIds({worldObject->mapId()},
                                         tr("convert this world object to a lever")))
        return;

    QDialog conversion(this);
    conversion.setWindowTitle(tr("Convert World Object to Lever"));
    QVBoxLayout* layout = new QVBoxLayout(&conversion);
    QLabel* explanation = new QLabel(tr(
        "Convert object ID %1 (%2) using the proven Base/JMV lever layout. "
        "The original common and opaque object records remain byte-identical. "
        "The conversion is one undoable edit.")
        .arg(worldObject->mapId()).arg(worldObject->mapName()), &conversion);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    QFormLayout* form = new QFormLayout;
    QComboBox* openMode = new QComboBox(&conversion);
    openMode->addItem(tr("Enabled (free open)"), 1);
    openMode->addItem(tr("Disabled"), 0);
    openMode->addItem(tr("Hands skill required"), 5);
    openMode->addItem(tr("Key required"), 8);
    QSpinBox* keyId = new QSpinBox(&conversion);
    keyId->setRange(0, std::numeric_limits<int>::max());
    QSpinBox* handsSkill = new QSpinBox(&conversion);
    handsSkill->setRange(0, std::numeric_limits<int>::max());
    QSpinBox* currentState = new QSpinBox(&conversion);
    currentState->setRange(0, 255);
    QSpinBox* totalStates = new QSpinBox(&conversion);
    totalStates->setRange(1, 255);
    totalStates->setValue(2);
    QCheckBox* cycled = new QCheckBox(tr("Cycle through states"), &conversion);
    QCheckBox* door = new QCheckBox(tr("Treat as door"), &conversion);
    QCheckBox* recalculateGraph = new QCheckBox(tr("Recalculate AI graph"), &conversion);
    form->addRow(tr("Open mode"), openMode);
    form->addRow(tr("Key ID"), keyId);
    form->addRow(tr("Hands skill"), handsSkill);
    form->addRow(tr("Current state"), currentState);
    form->addRow(tr("State count"), totalStates);
    form->addRow(QString(), cycled);
    form->addRow(QString(), door);
    form->addRow(QString(), recalculateGraph);
    layout->addLayout(form);
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &conversion);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Convert"));
    connect(buttons, &QDialogButtonBox::accepted, &conversion, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &conversion, &QDialog::reject);
    layout->addWidget(buttons);
    if(conversion.exec() != QDialog::Accepted)
        return;

    QByteArray sourceObject;
    QByteArray cleanObject;
    if(!m_activeMob->rawObjectTemplateBytes(worldObject, &sourceObject, &cleanObject))
    {
        QMessageBox::warning(this, tr("Conversion unavailable"),
            tr("Endurance could not verify the selected object's lossless raw provenance."));
        return;
    }
    CMobDocument::SLeverConversionValues defaults;
    defaults.openType = openMode->currentData().toUInt();
    defaults.keyId = quint32(keyId->value());
    defaults.handsSkill = quint32(handsSkill->value());
    defaults.currentState = quint8(currentState->value());
    defaults.totalStates = quint8(totalStates->value());
    defaults.cycled = cycled->isChecked();
    defaults.door = door->isChecked();
    defaults.recalculateGraph = recalculateGraph->isChecked();
    QByteArray sourceLever;
    QByteArray cleanLever;
    CValidationReport report;
    if(!CMobDocument::convertWorldObjectSubtreeToLever(sourceObject, defaults,
                                                       &sourceLever, &report) ||
       !CMobDocument::convertWorldObjectSubtreeToLever(cleanObject, defaults,
                                                       &cleanLever, &report))
    {
        ei::log(eLogWarning, QString("World object to lever conversion rejected: %1")
                .arg(report.toText()));
        QMessageBox::warning(this, tr("Conversion unavailable"),
            tr("The selected object's raw structure could not be converted safely. See the workflow log."));
        return;
    }

    QJsonObject leverData;
    leverData.insert("World object", worldObject->toJson());
    leverData.insert("State", int(defaults.currentState));
    leverData.insert("State numbers", int(defaults.totalStates));
    leverData.insert("Is cycled?", defaults.cycled);
    leverData.insert("Is cast once?", false);
    QJsonArray scienceStats;
    scienceStats.append(int(defaults.openType));
    scienceStats.append(int(defaults.keyId));
    scienceStats.append(int(defaults.handsSkill));
    leverData.insert("Science stats", scienceStats);
    leverData.insert("Is door?", defaults.door);
    leverData.insert("Is recalculate graph?", defaults.recalculateGraph);
    CLever* lever = new CLever(leverData);
    lever->loadFigure();
    lever->loadTexture();
    lever->setRawObjectProvenance(worldObject->rawSourceDocumentSha256(),
                                  worldObject->rawSourceObjectPath(),
                                  QCryptographicHash::hash(sourceLever, QCryptographicHash::Sha256));
    lever->setRawStructuralReplacementBytes(sourceLever, cleanLever);
    lever->setState(worldObject->nodeState());
    m_pUndoStack->push(new CConvertWorldObjectToLeverCommand(
        this, m_activeMob, worldObject, lever));
}

void CView::refreshMobDirtyStates()
{
    for(CMob* mob : m_aMob)
    {
        if(mob)
            mob->refreshDirtyFromProjection();
    }
    emit updateMainWindowTitle(eTitleTypeData::eTitleTypeDataDirtyFlag,
            (m_activeMob && m_activeMob->isDirty()) ? "*" : "");
}

void CView::resetCamPosition()
{
    m_cam->reset();
}

double CView::cameraSpeedMultiplier() const
{
    return m_cam ? double(m_cam->speedMultiplier()) : 1.0;
}

void CView::setCameraSpeedMultiplier(double multiplier)
{
    if(!m_cam)
        return;
    const double bounded = qBound(0.1, multiplier, 10.0);
    const double rounded = qRound(bounded * 10.0) / 10.0;
    if(qAbs(cameraSpeedMultiplier() - rounded) < 0.001)
        return;
    m_cam->setSpeedMultiplier(float(rounded));
    emit cameraSpeedMultiplierChanged(rounded);
    update();
}

bool CView::overridesWorldTime() const
{
    const COptBool* option = m_pSettings
        ? dynamic_cast<const COptBool*>(m_pSettings->opt("overrideWorldTime")) : nullptr;
    return option && option->value();
}

double CView::effectiveWorldTimeHours() const
{
    const COptInt* timeOption = m_pSettings
        ? dynamic_cast<const COptInt*>(m_pSettings->opt("renderTimeOfDay")) : nullptr;
    const float selectedTime = timeOption
        ? ei::clockValueToHours(timeOption->value()) : 12.0f;
    // Cave lighting deliberately ignores the preview clock, but runtime
    // scripts and gameplay still need the time explicitly chosen by the user.
    if(overridesWorldTime())
        return double(selectedTime);
    const CWorldSet* worldSet = m_environmentMob
        ? &m_environmentMob->worldSet() : nullptr;
    return double(ei::renderEnvironment(
        worldSet, false, selectedTime, m_caveEnvironment).timeHours);
}

void CView::addLogicPoint(bool bLookPoint)
{
    if(nullptr == m_activeMob)
        return;

    QList<CNode*> targets;
    for(CNode* node : m_activeMob->logicNodes())
    {
        if(node && node->nodeState() == ENodeState::eSelect)
            targets.append(node);
    }
    if(targets.isEmpty())
        return;
    bool bChangeToMove = false;
    int createdCount = 0;
    const auto beginPlacement = [this, &createdCount, &targets]()
    {
        if(createdCount == 0)
            m_pUndoStack->beginMacro(targets.size() > 1
                ? QString("Create and place logic points on %1 objects").arg(targets.size())
                : QString("Create and place logic point"));
        ++createdCount;
    };

    CNode* pNode = nullptr;
    foreach(pNode, targets)
    {
        ENodeType type = pNode->nodeType();
        switch(type)
        {
        case eLookPoint:
        {
            if(!bLookPoint)
                break; //dont create patrol point for view point

            int unitId(0); //parent unit map id
            int patrolId(0); //patrol parent id
            int viewId(0); //parent view id
            auto pLook = dynamic_cast<CLookPoint*>(pNode);
            m_activeMob->getViewHash(unitId, patrolId, viewId, pLook);
            QString hash = QString("%1.%2.%3").arg(unitId).arg(patrolId).arg(viewId);
            beginPlacement();
            CCreatePatrolCommand* pUndo = new CCreatePatrolCommand(this, hash);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case ePatrolPoint:
        {
            int unitId(0); //parent unit map id
            int patrolId(0); //patrol parent id
            int viewId(-1); //parent view id
            auto pPoint = dynamic_cast<CPatrolPoint*>(pNode);
            QString hash;
            m_activeMob->getPatrolHash(unitId, patrolId, pPoint);
            if(bLookPoint)
                hash = QString("%1.%2.%3").arg(unitId).arg(patrolId).arg(viewId); // for creating first looking point
            else
                hash = QString("%1.%2").arg(unitId).arg(patrolId); // for creating patrol point

            beginPlacement();
            CCreatePatrolCommand* pUndo = new CCreatePatrolCommand(this, hash);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case eUnit:
        {
            if(bLookPoint)
                break; //dont create look point for unit

            QString hash = QString("%1.%2").arg(pNode->mapId()).arg(-1);
            beginPlacement();
            CCreatePatrolCommand* pUndo = new CCreatePatrolCommand(this, hash);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case eMagicTrap:
        {
            beginPlacement();
            CCreateTrapPointCommand* pUndo = new CCreateTrapPointCommand(this, pNode->mapId(), !bLookPoint);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case eTrapActZone:
        {
            uint trapId = m_activeMob->trapIdByPoint(dynamic_cast<CActivationZone*>(pNode));
            beginPlacement();
            CCreateTrapPointCommand* pUndo = new CCreateTrapPointCommand(this, trapId, !bLookPoint);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        case eTrapCastPoint:
        {
            uint trapId = m_activeMob->trapIdByPoint(dynamic_cast<CTrapCastPoint*>(pNode));
            beginPlacement();
            CCreateTrapPointCommand* pUndo = new CCreateTrapPointCommand(this, trapId, !bLookPoint);
            m_pUndoStack->push(pUndo);
            bChangeToMove = true;
            break;
        }
        default:
            break;
        }

        pNode->setState(ENodeState::eDraw);

    }

    if(bChangeToMove)
    {
        m_pendingLogicPlacementMacro = true;
        m_pOp->changeState(new CMoveAxis(this, EOperateAxisXY));
    }
}

void CView::copySelectedIDsToClipboard()
{
    QVector<uint> arrId;
    CNode* pNode = nullptr;
    foreach(pNode, m_activeMob->nodes())
    {
        if(pNode->nodeState() != eSelect)
            continue;
        arrId.append(pNode->mapId());
    }
    std::sort(arrId.begin(), arrId.end());
    QString text;
    for(auto id : arrId)
    {
        text += QString::number(id) + "\n";
    }
    QClipboard* pClipboard = QGuiApplication::clipboard();
    pClipboard->setText(text);
}

void CView::execMobSwitch()
{
    if(nullptr != m_activeMob && m_aMob.size() == 1) //dont switch beetwen 1 mob
        return;

    CRoundMobCommand* pUndo = new CRoundMobCommand(this);
    m_pUndoStack->push(pUndo);
}

void CView::clearHistory()
{
    m_pUndoStack->clear();
}

void CView::onMobParamEditFinished(CMobParameters *pMob)
{
    m_arrParamWindow.removeOne(pMob);
}

void CView::roundActiveMob()
{
    if(m_aMob.isEmpty())
        return;

    if(nullptr == m_activeMob)
    {
        changeCurrentMob(m_aMob.front());
        return;
    }


    m_activeMob->clearSelect();
    int actIndex = m_aMob.indexOf(m_activeMob);
    if(actIndex == m_aMob.size()-1)
        actIndex = 0;
    else
        ++actIndex;

    changeCurrentMob(m_aMob[actIndex]);
}

void CView::undo_roundActiveMob()
{
    if(m_aMob.size() <1)
        return;

    if(m_aMob.size() == 1)
    {
        if(nullptr != m_activeMob)
            changeCurrentMob(nullptr);

        return;
    }

    m_activeMob->clearSelect();
    int actIndex = m_aMob.indexOf(m_activeMob);
    if(actIndex == 0)
        actIndex = m_aMob.size()-1;
    else
        --actIndex;

    changeCurrentMob(m_aMob[actIndex]);
}

void CView::execUnloadCommand()
{
    if(!m_pLand->isMprLoad() && m_activeMob == nullptr)
        return;

    if(m_activeMob == nullptr)
    {
        cancelPendingOperation();
        m_pUndoStack->clear();
        unloadLand();
        return;
    }

    cancelPendingOperation();
    m_pUndoStack->clear();
    unloadActiveMob();
}

void CView::iterateRoundMob()
{
    if(nullptr == m_activeMob)
        return;

    if(nullptr == m_pRoundForm)
    {
        QList<QString> arrMob;
        arrMob.append(m_activeMob->mobName());
        for(auto& mob: m_aMob)
        {
            if(mob == m_activeMob)
                continue;
            arrMob.append(mob->mobName());
        }
        m_pRoundForm = new CRoundMobForm(this);
        m_pRoundForm->initMobList(arrMob);
        m_pRoundForm->show();
        return;
    }
    m_pRoundForm->round();
}

void CView::applyRoundMob()
{
    if(nullptr == m_pRoundForm)
        return;

    QString newMobName = m_pRoundForm->selectedMob();
    delete m_pRoundForm;
    m_pRoundForm = nullptr;
    if (newMobName == m_activeMob->mobName())
        return;

    auto pChangeMobCommand = new CChangeActiveMobCommand(this, newMobName);
    m_pUndoStack->push(pChangeMobCommand);
}

void CView::saveSession()
{
    if(!m_pLand->isMprLoad())
        return;

    QString mprPath(m_pLand->filePath().absoluteFilePath());
    QVector<QString> arrMobPath;

    for(auto& mob: m_aMob)
    {
        if(mob == m_activeMob)
            continue;
        arrMobPath.append(mob->filePath().absoluteFilePath());
    }
    if(m_activeMob)
        arrMobPath.append(m_activeMob->filePath().absoluteFilePath());

    auto pManager = CSessionDataManager::getInstance();
    pManager->reset();
    pManager->addZoneData(mprPath, arrMobPath);
    pManager->addWorkspaceIdentity(m_sessionWorkspaceKind,
                                   m_sessionWorkspaceIdentifier,
                                   m_sessionWorkspaceDisplayName,
                                   m_sessionWorkspaceMapZoneId);
    QVector3D pos, pivot, rot;
    m_cam->getLocation(pos, pivot, rot);
    pManager->addCameraData(pos, pivot, rot);
    pManager->addQuickTileIndices(m_pTileForm->quickTileSet());
    pManager->saveSession();
}

void CView::loadSession()
{
    auto pManager = CSessionDataManager::getInstance();
    pManager->reset();
    pManager->loadSession();
    pManager->getWorkspaceIdentity(m_sessionWorkspaceKind,
                                   m_sessionWorkspaceIdentifier,
                                   m_sessionWorkspaceDisplayName,
                                   m_sessionWorkspaceMapZoneId);

    QString mprPath;
    QVector<QString> arrMobPath;
    if(!pManager->getZoneData(mprPath, arrMobPath))
        return;

    if(mprPath.isEmpty())
        return;

    QFileInfo mprFile(mprPath);
    if(!mprFile.exists())
    {
        ei::log(eLogWarning, "Cannot find MPR file: " + mprFile.absoluteFilePath());
        return;
    }
    prepareDocumentTransition();
    m_pUndoStack->clear();
    loadLandscape(mprFile);

    for(const auto& mobPath: arrMobPath)
    {
        QFileInfo mobFile(mobPath);
        if(!mobFile.exists())
        {
            ei::log(eLogWarning, "Cannot find MOB file: " + mobFile.absoluteFilePath());
            continue;
        }
        loadMob(mobFile);
        roundActiveMob();
    }

    QVector3D pos, pivot, rot;
    if(pManager->getCameraData(pos, pivot, rot))
    {
        m_cam->moveTo(pos);
        m_cam->setLocation(pos, pivot, rot);
    }

    QVector<int> arrQuickInd;
    if(pManager->getdQuickTileIndices(arrQuickInd))
    {
        m_pTileForm->setQuickTileSet(arrQuickInd);
    }
}

void CView::setSessionWorkspaceIdentity(const QString& kind, const QString& identifier,
                                        const QString& displayName, const QString& mapZoneId)
{
    m_sessionWorkspaceKind = kind;
    m_sessionWorkspaceIdentifier = identifier;
    m_sessionWorkspaceDisplayName = displayName;
    m_sessionWorkspaceMapZoneId = mapZoneId;
}

bool CView::isRecentAvailable()
{
    return m_recentOpenedFile_file.exists();
}

int CView::renameActiveMobUnits(QMap<QString, QString> &mapName)
{
    ei::log(ELogMessageType::eLogInfo, "processing map " + m_activeMob->mobName());
    int n=0;
    CUnit* pUnit = nullptr;
    QString sourceName;
    for(auto& node:m_activeMob->nodes())
    {
        if(node->nodeType() != ENodeType::eUnit)
            continue;
        pUnit = dynamic_cast<CUnit*>(node);
        sourceName = pUnit->databaseName();
        if(mapName.contains(sourceName))
        {
            QSharedPointer<IPropertyBase> newName(new propStr(eObjParam_UNIT_PROTOTYPE, mapName[sourceName]));
            pUnit->applyParam(newName);
            ++n;
            ei::log(eLogInfo, "Unit:" + QString::number(pUnit->mapId()) + " last name:" + sourceName + " new name:" + newName->toString());
        }
        else
            ei::log(ELogMessageType::eLogFatal, "not found:" + sourceName);
    }
    ei::log(ELogMessageType::eLogInfo, "processing map finished:" + m_activeMob->mobName());
    return n;
}
