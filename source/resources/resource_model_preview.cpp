#include "resource_model_preview.h"
#include "../render/figure_format.h"

#include <QDataStream>
#include <QMap>
#include <QPainter>
#include <QSet>
#include <QTextCodec>
#include <QVector2D>
#include <QVector3D>
#include <QtMath>
#include <QtEndian>

#include <cmath>
#include <functional>
#include <limits>

namespace ei {
namespace {
constexpr quint32 kResSignature = 0x019CE23C;
constexpr quint64 kResEntrySize = 22;
constexpr quint32 kMaxArrayItems = 3000000;
constexpr int kMaxNestedMembers = 10000;
constexpr int kMaxPreviewTriangles = 50000;
constexpr int kMaxAssemblyTriangles = 300000;

bool isBaseAnatomyPart(const QString& partName)
{
    static const QSet<QString> anatomy = {
        QStringLiteral("hp"), QStringLiteral("bd"), QStringLiteral("hd"),
        QStringLiteral("hr"), QStringLiteral("lh1"), QStringLiteral("rh1"),
        QStringLiteral("lh2"), QStringLiteral("rh2"), QStringLiteral("lh3"),
        QStringLiteral("rh3"), QStringLiteral("ll1"), QStringLiteral("rl1"),
        QStringLiteral("ll2"), QStringLiteral("rl2"), QStringLiteral("ll3"),
        QStringLiteral("rl3")
    };
    return anatomy.contains(partName.trimmed().toLower());
}

struct Triangle
{
    QVector3D a; QVector3D b; QVector3D c;
    int sourceA{-1}; int sourceB{-1}; int sourceC{-1};
    QVector2D uvA, uvB, uvC;
};

quint16 u16(const QByteArray& bytes, quint64 offset)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

quint32 u32(const QByteArray& bytes, quint64 offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

bool isFigureData(const QByteArray& bytes)
{
    return bytes.size() >= 4 && ei::figureFormCount(u32(bytes, 0)) != 0;
}

bool rangeFits(const QByteArray& bytes, quint64 offset, quint64 size)
{
    return offset <= quint64(bytes.size()) && size <= quint64(bytes.size()) - offset;
}

QMap<QString, QByteArray> resMembers(const QByteArray& bytes, QStringList* diagnostics)
{
    QMap<QString, QByteArray> result;
    if(bytes.size() < 16 || u32(bytes, 0) != kResSignature)
    {
        if(diagnostics) diagnostics->append(QStringLiteral("Nested RES signature is invalid."));
        return result;
    }
    const quint32 count = u32(bytes, 4);
    const quint32 tableOffset = u32(bytes, 8);
    const quint32 namesLength = u32(bytes, 12);
    if(count == 0 || count > kMaxNestedMembers
       || !rangeFits(bytes, tableOffset, quint64(count) * kResEntrySize + namesLength))
    {
        if(diagnostics) diagnostics->append(QStringLiteral("Nested RES table is out of bounds."));
        return result;
    }
    const quint64 namesOffset = quint64(tableOffset) + quint64(count) * kResEntrySize;
    QTextCodec* codec = QTextCodec::codecForName("CP1251");
    for(quint32 slot = 0; slot < count; ++slot)
    {
        const quint64 entry = quint64(tableOffset) + quint64(slot) * kResEntrySize;
        const quint32 size = u32(bytes, entry + 4);
        const quint32 offset = u32(bytes, entry + 8);
        const quint16 nameLength = u16(bytes, entry + 16);
        const quint32 nameOffset = u32(bytes, entry + 18);
        if(!rangeFits(bytes, offset, size) || quint64(nameOffset) + nameLength > namesLength)
        {
            if(diagnostics) diagnostics->append(
                QStringLiteral("Nested RES member %1 is out of bounds.").arg(slot));
            return {};
        }
        const QByteArray encoded = bytes.mid(int(namesOffset + nameOffset), nameLength);
        const QString name = (codec ? codec->toUnicode(encoded)
                                    : QString::fromLatin1(encoded)).toLower();
        if(result.contains(name))
        {
            if(diagnostics) diagnostics->append(
                QStringLiteral("Nested RES member name is ambiguous: %1").arg(name));
            return {};
        }
        result.insert(name, bytes.mid(int(offset), int(size)));
    }
    return result;
}

bool parseFig(const QByteArray& bytes, QVector<Triangle>* triangles, QString* diagnostic,
              const QVector3D& offset = QVector3D())
{
    if(!triangles) return false;
    const int formCount = bytes.size() >= 4 ? figureFormCount(u32(bytes, 0)) : 0;
    if(bytes.size() < 40 || !formCount)
    {
        if(diagnostic) *diagnostic = QStringLiteral("FIG1/FIG8 signature is invalid.");
        return false;
    }
    const quint32 vertexBlocks = u32(bytes, 4);
    const quint32 normalBlocks = u32(bytes, 8);
    const quint32 uvCount = u32(bytes, 12);
    const quint32 indexCount = u32(bytes, 16);
    const quint32 componentCount = u32(bytes, 20);
    const quint32 morphCount = u32(bytes, 24);
    if(vertexBlocks == 0 || vertexBlocks > kMaxArrayItems || normalBlocks > kMaxArrayItems
       || uvCount > kMaxArrayItems || indexCount > kMaxArrayItems
       || componentCount > kMaxArrayItems || morphCount > kMaxArrayItems)
    {
        if(diagnostic) *diagnostic = QStringLiteral("FIG array count exceeds the safety bound.");
        return false;
    }
    const quint64 tail = quint64(formCount) * 10 * sizeof(float);
    const quint64 vertexBytes = quint64(vertexBlocks) * 3 * formCount * 4 * sizeof(float);
    const quint64 normalBytes = quint64(normalBlocks) * 4 * 4 * sizeof(float);
    const quint64 uvBytes = quint64(uvCount) * 2 * sizeof(float);
    const quint64 indexBytes = quint64(indexCount) * sizeof(quint16);
    const quint64 componentBytes = quint64(componentCount) * 3 * sizeof(quint16);
    const quint64 morphBytes = quint64(morphCount) * sizeof(quint32);
    const quint64 expected = 40 + tail + vertexBytes + normalBytes + uvBytes
        + indexBytes + componentBytes + morphBytes;
    if(expected > quint64(bytes.size()))
    {
        if(diagnostic) *diagnostic = QStringLiteral("FIG arrays are truncated.");
        return false;
    }
    QDataStream stream(bytes);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    if(!stream.device()->seek(40 + qint64(tail))) return false;
    QVector<QVector3D> vertices(int(vertexBlocks * 4));
    for(quint32 block = 0; block < vertexBlocks; ++block)
        for(int axis = 0; axis < 3; ++axis)
            for(int morph = 0; morph < formCount; ++morph)
                for(int lane = 0; lane < 4; ++lane)
                {
                    float value = 0.0f;
                    stream >> value;
                    if(morph == 0) vertices[int(block * 4 + lane)][axis] = value;
                }
    stream.skipRawData(int(normalBytes));
    QVector<QVector2D> uvs;uvs.resize(int(uvCount));
    for(auto& uv:uvs) {float u,v;stream>>u>>v;uv=QVector2D(u,v);}
    QVector<quint16> indices;
    indices.resize(int(indexCount));
    for(quint16& value : indices) stream >> value;
    QVector<quint16> componentUvs;componentUvs.resize(int(componentCount));
    QVector<quint16> componentVertices;
    componentVertices.resize(int(componentCount));
    for(quint32 component = 0; component < componentCount; ++component)
    {
        quint16 vertex = 0, normal = 0, uv = 0;
        stream >> vertex >> normal >> uv;
        Q_UNUSED(normal);
        componentUvs[int(component)] = uv;
        componentVertices[int(component)] = vertex;
    }
    if(stream.status() != QDataStream::Ok)
    {
        if(diagnostic) *diagnostic = QStringLiteral("FIG arrays could not be read.");
        return false;
    }
    const int complete = qMin(indices.size() - (indices.size() % 3),
                              kMaxPreviewTriangles * 3);
    triangles->reserve(triangles->size() + complete / 3);
    for(int index = 0; index < complete; index += 3)
    {
        const quint16 ia = indices.at(index);
        const quint16 ib = indices.at(index + 1);
        const quint16 ic = indices.at(index + 2);
        if(ia >= componentVertices.size() || ib >= componentVertices.size()
           || ic >= componentVertices.size()) continue;
        const quint16 va = componentVertices.at(ia);
        const quint16 vb = componentVertices.at(ib);
        const quint16 vc = componentVertices.at(ic);
        if(va >= vertices.size() || vb >= vertices.size() || vc >= vertices.size()) continue;
        Triangle triangle;
        triangle.a = vertices.at(va) + offset;
        triangle.b = vertices.at(vb) + offset;
        triangle.c = vertices.at(vc) + offset;
        triangle.sourceA = va;
        triangle.sourceB = vb;
        triangle.sourceC = vc;
        if(componentUvs[ia]<uvs.size())triangle.uvA=uvs[componentUvs[ia]];
        if(componentUvs[ib]<uvs.size())triangle.uvB=uvs[componentUvs[ib]];
        if(componentUvs[ic]<uvs.size())triangle.uvC=uvs[componentUvs[ic]];
        triangles->append(triangle);
    }
    return !triangles->isEmpty();
}

QImage renderTriangles(const QVector<Triangle>& triangles, const QSize& requested)
{
    const QSize size(qMax(240, requested.width()), qMax(180, requested.height()));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(30, 33, 39));
    if(triangles.isEmpty()) return image;
    struct DrawTriangle { QPolygonF polygon; float depth; QColor color; };
    QVector<QVector3D> projected;
    projected.reserve(triangles.size() * 3);
    const float yaw = 0.72f;
    const float pitch = 0.42f;
    auto project = [yaw, pitch](const QVector3D& value) {
        const float x = std::cos(yaw) * value.x() - std::sin(yaw) * value.y();
        const float y0 = std::sin(yaw) * value.x() + std::cos(yaw) * value.y();
        const float y = std::cos(pitch) * value.z() - std::sin(pitch) * y0;
        const float z = std::sin(pitch) * value.z() + std::cos(pitch) * y0;
        return QVector3D(x, y, z);
    };
    QVector3D minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), 0);
    QVector3D maximum(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), 0);
    for(const Triangle& triangle : triangles)
        for(const QVector3D& point : {triangle.a, triangle.b, triangle.c})
        {
            const QVector3D value = project(point);
            projected.append(value);
            minimum.setX(qMin(minimum.x(), value.x())); minimum.setY(qMin(minimum.y(), value.y()));
            maximum.setX(qMax(maximum.x(), value.x())); maximum.setY(qMax(maximum.y(), value.y()));
        }
    const float width = qMax(1.0e-5f, maximum.x() - minimum.x());
    const float height = qMax(1.0e-5f, maximum.y() - minimum.y());
    const float scale = 0.86f * qMin(size.width() / width, size.height() / height);
    const QPointF center(size.width() * 0.5, size.height() * 0.5);
    const QVector2D boundsCenter((minimum.x() + maximum.x()) * 0.5f,
                                 (minimum.y() + maximum.y()) * 0.5f);
    QVector<DrawTriangle> draw;
    draw.reserve(triangles.size());
    for(int triangle = 0; triangle < triangles.size(); ++triangle)
    {
        const QVector3D a = projected.at(triangle * 3);
        const QVector3D b = projected.at(triangle * 3 + 1);
        const QVector3D c = projected.at(triangle * 3 + 2);
        QPolygonF polygon;
        for(const QVector3D& point : {a, b, c})
            polygon << center + QPointF((point.x() - boundsCenter.x()) * scale,
                                        -(point.y() - boundsCenter.y()) * scale);
        QVector3D normal = QVector3D::crossProduct(b - a, c - a).normalized();
        const int light = qBound(42, int(105 + 105 * qAbs(normal.z())), 215);
        draw.append({polygon, (a.z() + b.z() + c.z()) / 3.0f,
                     QColor(light * 0.65, light * 0.78, light)});
    }
    std::sort(draw.begin(), draw.end(), [](const DrawTriangle& a, const DrawTriangle& b) {
        return a.depth < b.depth;
    });
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for(const DrawTriangle& triangle : draw)
    {
        painter.setBrush(triangle.color);
        painter.setPen(QPen(QColor(15, 18, 23, 150), 0.6));
        painter.drawPolygon(triangle.polygon);
    }
    return image;
}

QImage renderInteractiveTriangles(const QVector<Triangle>& triangles,
                                  const QVector3D& sceneCenter, float sceneRadius,
                                  float zoom, const QVector3D& pan,
                                  const QSize& requested)
{
    const QSize size(qMax(240, requested.width()), qMax(180, requested.height()));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(30, 33, 39));
    if(triangles.isEmpty()) return image;
    struct DrawTriangle { QPolygonF polygon; float depth; QColor color; };
    const float scale = 0.42f * qMin(size.width(), size.height())
        * qMax(0.05f, zoom) / qMax(1.0e-4f, sceneRadius);
    const QPointF center(size.width() * 0.5 + pan.x() * qMin(size.width(), size.height()),
                         size.height() * 0.5 - pan.z() * qMin(size.width(), size.height()));
    QVector<DrawTriangle> draw;
    draw.reserve(triangles.size());
    for(const Triangle& triangle : triangles)
    {
        const QVector3D points[3] = {triangle.a - sceneCenter, triangle.b - sceneCenter,
                                     triangle.c - sceneCenter};
        QPolygonF polygon;
        for(const QVector3D& point : points)
            polygon << center + QPointF(point.x() * scale, -point.z() * scale);
        const QVector3D normal = QVector3D::crossProduct(points[1] - points[0],
                                                         points[2] - points[0]).normalized();
        const int light = qBound(42, int(105 + 105 * qAbs(normal.y())), 215);
        draw.append({polygon, (points[0].y() + points[1].y() + points[2].y()) / 3.0f,
                     QColor(light * 0.65, light * 0.78, light)});
    }
    std::sort(draw.begin(), draw.end(), [](const DrawTriangle& left, const DrawTriangle& right) {
        return left.depth < right.depth;
    });
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for(const DrawTriangle& triangle : draw)
    {
        painter.setBrush(triangle.color);
        painter.setPen(QPen(QColor(15, 18, 23, 150), 0.6));
        painter.drawPolygon(triangle.polygon);
    }
    return image;
}

QVector<Triangle> posedTriangles(const SResourceModelScene& scene,
                                  const SResourceModelView& view)
{
    QVector<Triangle> result;
    for(const SResourceModelPart& part : scene.parts)
    {
        if(view.filterParts && !view.visibleParts.contains(part.name.toLower()))
            continue;
        const QMatrix4x4 pose = view.poses.value(part.name.toLower());
        const QVector<QVector3D> morph = view.morphDeltas.value(part.name.toLower());
        for(const SResourceModelTriangle& source : part.triangles)
        {
            Triangle triangle;
            const auto point = [&](const SResourceModelVertex& vertex) {
                QVector3D value = vertex.position;
                if(vertex.sourceIndex >= 0 && vertex.sourceIndex < morph.size())
                    value += morph.at(vertex.sourceIndex);
                return pose * value;
            };
            triangle.a = point(source.vertices[0]);
            triangle.b = point(source.vertices[1]);
            triangle.c = point(source.vertices[2]);
            result.append(triangle);
        }
    }
    return result;
}
}

float resourceModelDefaultElevationDegrees()
{
    return 0.0f;
}

float resourceModelDefaultAzimuthDegrees()
{
    return 0.0f;
}

bool isHumanoidAssemblyStem(const QString& modelStem)
{
    QString stem = modelStem.trimmed().toLower();
    if(stem.endsWith(QStringLiteral(".mod"))) stem.chop(4);
    static const QSet<QString> humanoids = {
        QStringLiteral("unhuma"), QStringLiteral("unhufe"),
        QStringLiteral("unhuli"),
        QStringLiteral("unorma"), QStringLiteral("unorfe"),
        QStringLiteral("unhusk"), QStringLiteral("unhuzm"),
        QStringLiteral("unhuzf")
    };
    return humanoids.contains(stem);
}

QSet<QString> defaultVisibleModelParts(const SResourceModelScene& scene,
                                       const QString& modelStem)
{
    QSet<QString> visible;
    if(!isHumanoidAssemblyStem(modelStem))
    {
        for(const SResourceModelPart& part : scene.parts)
            visible.insert(part.name.toLower());
        return visible;
    }
    static const QSet<QString> anatomy = {
        QStringLiteral("hp"), QStringLiteral("bd"), QStringLiteral("hd"),
        QStringLiteral("hr"), QStringLiteral("lh1"), QStringLiteral("rh1"),
        QStringLiteral("lh2"), QStringLiteral("rh2"), QStringLiteral("lh3"),
        QStringLiteral("rh3"), QStringLiteral("ll1"), QStringLiteral("rl1"),
        QStringLiteral("ll2"), QStringLiteral("rl2"), QStringLiteral("ll3"),
        QStringLiteral("rl3")
    };
    for(const SResourceModelPart& part : scene.parts)
    {
        const QString name = part.name.toLower();
        const QString root = name.section('.', 0, 0);
        if(anatomy.contains(root) && name == root)
            visible.insert(name);
    }
    return visible;
}

int inferredAnimationLogicalLength(const SAnimationClip& clip)
{
    int samples = 0;
    for(auto it = clip.channels.constBegin(); it != clip.channels.constEnd(); ++it)
        samples = qMax(samples, qMax(it->rotations.size(),
            qMax(it->translations.size(), it->morphations.size())));
    // Authored clips carry a closing loop sample; logicalLength excludes it.
    return qMax(1, samples - 1);
}

int resourceModelPreviewTriangleBudget(const QString& partName, int authoredTriangles,
                                       int partCount, int anatomyTriangles)
{
    if(partCount <= 0) return 1;
    if(anatomyTriangles <= 0)
        return qMax(1, kMaxPreviewTriangles / partCount);
    if(isBaseAnatomyPart(partName))
        return qMin(qMax(1, authoredTriangles), qMax(1,
            int(qint64(kMaxPreviewTriangles) * authoredTriangles / anatomyTriangles)));
    const int optionalCount = qMax(1, partCount - 16);
    const int optionalPool = kMaxAssemblyTriangles - kMaxPreviewTriangles;
    return qMax(250, optionalPool / optionalCount);
}

bool resourcePayloadLooksText(const QByteArray& bytes)
{
    if(bytes.isEmpty()) return true;
    if(bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) return true;
    const int sampleSize = qMin(bytes.size(), 8192);
    int controls = 0;
    for(int index = 0; index < sampleSize; ++index)
    {
        const uchar value = uchar(bytes.at(index));
        if(value == 0) return false;
        if(value < 0x20 && value != '\t' && value != '\r' && value != '\n') ++controls;
    }
    return controls * 100 <= sampleSize;
}

SResourceModelScene buildFig8Scene(const QByteArray& bytes)
{
    SResourceModelScene scene;
    QVector<Triangle> parsed;
    QString diagnostic;
    if(!parseFig(bytes, &parsed, &diagnostic))
    {
        scene.diagnostics.append(diagnostic);
        return scene;
    }
    SResourceModelPart part;
    part.name = QStringLiteral("figure");
    for(const Triangle& triangle : parsed)
    {
        SResourceModelTriangle output;
        output.vertices[0].position = triangle.a; output.vertices[0].sourceIndex = triangle.sourceA;
        output.vertices[1].position = triangle.b; output.vertices[1].sourceIndex = triangle.sourceB;
        output.vertices[2].position = triangle.c; output.vertices[2].sourceIndex = triangle.sourceC;
        output.vertices[0].uv=triangle.uvA; output.vertices[1].uv=triangle.uvB; output.vertices[2].uv=triangle.uvC;
        part.triangles.append(output);
    }
    scene.parts.append(part);
    scene.triangleCount = parsed.size();
    scene.kind = QString::fromLatin1(bytes.constData(), 4);
    scene.valid = true;
    return scene;
}

SResourceModelScene buildModScene(const QByteArray& bytes, const QByteArray& companionBon)
{
    SResourceModelScene scene;
    const QMap<QString, QByteArray> members = resMembers(bytes, &scene.diagnostics);
    if(members.isEmpty()) return scene;
    QMap<QString, QString> parents;
    int figurePartCount = 0;
    int anatomyTriangleCount = 0;
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
        if(isFigureData(it.value()))
        {
            ++figurePartCount;
            if(isBaseAnatomyPart(it.key()) && it.value().size() >= 20)
                anatomyTriangleCount += int(u32(it.value(), 16) / 3);
        }
    bool sampled = false;
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
    {
        if(isFigureData(it.value())) continue;
        QDataStream links(it.value());
        links.setByteOrder(QDataStream::LittleEndian);
        int count = 0; links >> count;
        if(count <= 0 || count > kMaxNestedMembers) continue;
        QMap<QString, QString> candidate;
        bool valid = true;
        for(int index = 0; index < count; ++index)
        {
            auto readName = [&links](QString* name, bool allowEmpty) {
                int length = 0; links >> length;
                if(length < 0 || length > 4096 || (!allowEmpty && length == 0)) return false;
                QByteArray raw(length, Qt::Uninitialized);
                if(length && links.readRawData(raw.data(), length) != length) return false;
                while(raw.endsWith('\0')) raw.chop(1);
                *name = QString::fromLatin1(raw).toLower();
                return allowEmpty || !name->isEmpty();
            };
            QString component, parent;
            if(!readName(&component, false) || !readName(&parent, true)) { valid = false; break; }
            candidate.insert(component, parent);
        }
        if(valid && links.status() == QDataStream::Ok) { parents = candidate; break; }
    }
    QMap<QString, QVector3D> offsets;
    if(!companionBon.isEmpty())
    {
        const QMap<QString, QByteArray> bon = resMembers(companionBon, &scene.diagnostics);
        for(auto it = bon.constBegin(); it != bon.constEnd(); ++it)
        {
            if(it.value().size() < 3 * int(sizeof(float))) continue;
            QDataStream stream(it.value());
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            QVector3D value; stream >> value;
            if(stream.status() == QDataStream::Ok) offsets.insert(it.key(), value);
        }
    }
    QMap<QString, QVector3D> accumulated;
    std::function<QVector3D(const QString&, QSet<QString>)> resolve =
        [&](const QString& name, QSet<QString> visiting) -> QVector3D {
        if(accumulated.contains(name)) return accumulated.value(name);
        if(visiting.contains(name)) return offsets.value(name);
        visiting.insert(name);
        const QString parent = parents.value(name);
        const QVector3D value = offsets.value(name)
            + (parent.isEmpty() ? QVector3D() : resolve(parent, visiting));
        accumulated.insert(name, value);
        return value;
    };
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
    {
        if(!isFigureData(it.value())) continue;
        QVector<Triangle> parsed;
        QString diagnostic;
        if(!parseFig(it.value(), &parsed, &diagnostic, resolve(it.key(), {})))
        {
            if(!diagnostic.isEmpty()) scene.diagnostics.append(it.key() + ": " + diagnostic);
            continue;
        }
        SResourceModelPart part; part.name = it.key();
        const int authoredTriangles = it.value().size() >= 20
            ? int(u32(it.value(), 16) / 3) : parsed.size();
        const int perPartTriangleBudget = resourceModelPreviewTriangleBudget(
            it.key(), authoredTriangles, figurePartCount, anatomyTriangleCount);
        if(parsed.size() > perPartTriangleBudget)
        {
            parsed.resize(perPartTriangleBudget);
            sampled = true;
        }
        for(const Triangle& triangle : parsed)
        {
            SResourceModelTriangle output;
            output.vertices[0].position = triangle.a; output.vertices[0].sourceIndex = triangle.sourceA;
            output.vertices[1].position = triangle.b; output.vertices[1].sourceIndex = triangle.sourceB;
            output.vertices[2].position = triangle.c; output.vertices[2].sourceIndex = triangle.sourceC;
        output.vertices[0].uv=triangle.uvA; output.vertices[1].uv=triangle.uvB; output.vertices[2].uv=triangle.uvC;
            part.triangles.append(output);
        }
        scene.parts.append(part);
        scene.triangleCount += parsed.size();
        scene.skeleton.parents.insert(it.key(), parents.value(it.key()));
        scene.skeleton.restOffsets.insert(it.key(), resolve(it.key(), {}));
    }
    if(sampled)
        scene.diagnostics.append(QStringLiteral(
            "Interactive preview sampled assembly variants within a %1-triangle safety ceiling while preserving base anatomy detail.")
            .arg(kMaxAssemblyTriangles));
    scene.kind = QStringLiteral("MOD assembly");
    scene.valid = !scene.parts.isEmpty();
    return scene;
}

QImage renderResourceModelScene(const SResourceModelScene& scene,
                                const SResourceModelView& view, const QSize& size)
{
    QVector3D minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max());
    QVector3D maximum(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                      -std::numeric_limits<float>::max());
    for(const SResourceModelPart& part : scene.parts)
        for(const SResourceModelTriangle& triangle : part.triangles)
            for(const SResourceModelVertex& vertex : triangle.vertices)
            {
                minimum.setX(qMin(minimum.x(), vertex.position.x()));
                minimum.setY(qMin(minimum.y(), vertex.position.y()));
                minimum.setZ(qMin(minimum.z(), vertex.position.z()));
                maximum.setX(qMax(maximum.x(), vertex.position.x()));
                maximum.setY(qMax(maximum.y(), vertex.position.y()));
                maximum.setZ(qMax(maximum.z(), vertex.position.z()));
            }
    const QVector3D center = (minimum + maximum) * 0.5f;
    float radius = 0.0f;
    for(const SResourceModelPart& part : scene.parts)
        for(const SResourceModelTriangle& triangle : part.triangles)
            for(const SResourceModelVertex& vertex : triangle.vertices)
                radius = qMax(radius, (vertex.position - center).length());
    QVector<Triangle> triangles = posedTriangles(scene, view);
    QMatrix4x4 model;
    model.setToIdentity();
    model.translate(center);
    model.rotate(view.modelYawDegrees, 0.0f, 0.0f, 1.0f);
    model.rotate(view.modelPitchDegrees, 1.0f, 0.0f, 0.0f);
    model.translate(-center);
    const float yaw = qDegreesToRadians(view.azimuthDegrees);
    const float elevation = qDegreesToRadians(view.elevationDegrees);
    const QVector3D right(std::cos(yaw), std::sin(yaw), 0.0f);
    const QVector3D depth(std::sin(yaw) * std::cos(elevation),
                          -std::cos(yaw) * std::cos(elevation), std::sin(elevation));
    const QVector3D up = QVector3D::crossProduct(depth, right).normalized();
    const auto orbit = [&](const QVector3D& point) {
        const QVector3D relative = model * point - center;
        return center + QVector3D(QVector3D::dotProduct(relative, right),
                                  QVector3D::dotProduct(relative, depth),
                                  QVector3D::dotProduct(relative, up));
    };
    for(Triangle& triangle : triangles)
    {
        triangle.a = orbit(triangle.a);
        triangle.b = orbit(triangle.b);
        triangle.c = orbit(triangle.c);
    }
    return renderInteractiveTriangles(triangles, center, radius, view.zoom, view.pan, size);
}

SResourceModelPreview previewFig8(const QByteArray& bytes, const QSize& size)
{
    SResourceModelPreview result;
    QVector<Triangle> triangles;
    QString diagnostic;
    if(!parseFig(bytes, &triangles, &diagnostic))
    {
        result.diagnostics.append(diagnostic);
        return result;
    }
    result.valid = true;
    result.componentCount = 1;
    result.triangleCount = triangles.size();
    result.image = renderTriangles(triangles, size);
    result.details = QStringLiteral("%1 | %2 triangle(s)")
        .arg(QString::fromLatin1(bytes.constData(), 4)).arg(result.triangleCount);
    return result;
}

SResourceModelPreview previewModAssembly(const QByteArray& bytes,
                                         const QByteArray& companionBon,
                                         const QSize& size)
{
    SResourceModelPreview result;
    const QMap<QString, QByteArray> members = resMembers(bytes, &result.diagnostics);
    if(members.isEmpty()) return result;
    QMap<QString, QString> parents;
    QStringList orderedComponents;
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
    {
        if(isFigureData(it.value())) continue;
        QDataStream links(it.value());
        links.setByteOrder(QDataStream::LittleEndian);
        int count = 0;
        links >> count;
        if(count <= 0 || count > kMaxNestedMembers) continue;
        QMap<QString, QString> candidateParents;
        QStringList candidateOrder;
        bool valid = true;
        for(int index = 0; index < count; ++index)
        {
            auto readName = [&links](QString* name, bool allowEmpty) {
                int length = 0;
                links >> length;
                if(length < 0 || length > 4096 || (!allowEmpty && length == 0)) return false;
                QByteArray raw(length, Qt::Uninitialized);
                if(length && links.readRawData(raw.data(), length) != length) return false;
                while(raw.endsWith('\0')) raw.chop(1);
                *name = QString::fromLatin1(raw).toLower();
                return allowEmpty || !name->isEmpty();
            };
            QString component, parent;
            if(!readName(&component, false) || !readName(&parent, true)) { valid = false; break; }
            candidateParents.insert(component, parent);
            candidateOrder.append(component);
        }
        if(valid && links.status() == QDataStream::Ok)
        {
            parents = candidateParents;
            orderedComponents = candidateOrder;
            break;
        }
    }
    if(parents.isEmpty())
        result.diagnostics.append(QStringLiteral("MOD LNK hierarchy is missing or invalid; components are shown at authored origins."));

    QMap<QString, QVector3D> localOffsets;
    if(!companionBon.isEmpty())
    {
        QStringList bonDiagnostics;
        const QMap<QString, QByteArray> offsets = resMembers(companionBon, &bonDiagnostics);
        for(auto it = offsets.constBegin(); it != offsets.constEnd(); ++it)
        {
            if(it.value().size() < 3 * int(sizeof(float))) continue;
            QDataStream stream(it.value());
            stream.setByteOrder(QDataStream::LittleEndian);
            stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            QVector3D offset;
            stream >> offset;
            if(stream.status() == QDataStream::Ok) localOffsets.insert(it.key(), offset);
        }
        result.diagnostics.append(bonDiagnostics);
    }
    QMap<QString, QVector3D> accumulatedOffsets;
    std::function<QVector3D(const QString&, QSet<QString>)> accumulated =
        [&](const QString& component, QSet<QString> visiting) -> QVector3D {
        if(accumulatedOffsets.contains(component)) return accumulatedOffsets.value(component);
        if(visiting.contains(component)) return localOffsets.value(component);
        visiting.insert(component);
        const QString parent = parents.value(component);
        const QVector3D value = localOffsets.value(component)
            + (parent.isEmpty() ? QVector3D() : accumulated(parent, visiting));
        accumulatedOffsets.insert(component, value);
        return value;
    };

    QVector<Triangle> triangles;
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
    {
        if(!it.key().endsWith(QStringLiteral(".fig")) && !isFigureData(it.value())) continue;
        QString diagnostic;
        const int before = triangles.size();
        if(parseFig(it.value(), &triangles, &diagnostic, accumulated(it.key(), {})))
            ++result.componentCount;
        else if(!diagnostic.isEmpty())
            result.diagnostics.append(it.key() + QStringLiteral(": ") + diagnostic);
        if(triangles.size() == before && result.diagnostics.size() > 20) break;
    }
    if(triangles.isEmpty())
    {
        result.diagnostics.append(QStringLiteral("MOD contains no renderable FIG1/FIG8 components."));
        return result;
    }
    result.valid = true;
    result.triangleCount = triangles.size();
    result.image = renderTriangles(triangles, size);
    result.details = QStringLiteral("MOD assembly | %1 component(s) | %2 triangle(s)%3")
        .arg(result.componentCount).arg(result.triangleCount);
    result.details = result.details.arg(companionBon.isEmpty()
        ? QStringLiteral(" | no companion BON offsets") : QStringLiteral(" | companion BON offsets applied"));
    return result;
}

QStringList inspectAnimationArchive(const QByteArray& bytes, int* actionCount, int* channelCount)
{
    QStringList diagnostics;
    const QMap<QString, QByteArray> actions = resMembers(bytes, &diagnostics);
    int channels = 0;
    QStringList result;
    for(auto it = actions.constBegin(); it != actions.constEnd(); ++it)
    {
        QStringList nestedDiagnostics;
        const QMap<QString, QByteArray> nested = resMembers(it.value(), &nestedDiagnostics);
        channels += nested.size();
        result.append(QStringLiteral("%1 — %2 channel(s)").arg(it.key()).arg(nested.size()));
        if(!nestedDiagnostics.isEmpty())
            result.append(QStringLiteral("  %1").arg(nestedDiagnostics.join(QStringLiteral("; "))));
    }
    if(actionCount) *actionCount = actions.size();
    if(channelCount) *channelCount = channels;
    if(actions.isEmpty()) return diagnostics;
    return result;
}

QStringList inspectBoneArchive(const QByteArray& bytes, int* componentCount)
{
    QStringList diagnostics;
    const QMap<QString, QByteArray> members = resMembers(bytes, &diagnostics);
    QStringList result;
    for(auto it = members.constBegin(); it != members.constEnd(); ++it)
    {
        if(it.value().size() < 3 * int(sizeof(float)))
        { result.append(QStringLiteral("%1 — truncated").arg(it.key())); continue; }
        QDataStream stream(it.value());
        stream.setByteOrder(QDataStream::LittleEndian);
        stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
        QVector3D offset; stream >> offset;
        result.append(QStringLiteral("%1 — rest offset [%2, %3, %4]")
            .arg(it.key()).arg(offset.x(), 0, 'g', 6).arg(offset.y(), 0, 'g', 6)
            .arg(offset.z(), 0, 'g', 6));
    }
    if(componentCount) *componentCount = members.size();
    return members.isEmpty() ? diagnostics : result;
}

}
