#pragma once
#include <QImage>
#include <QMatrix4x4>
#include <QRect>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include "terrain_edit_model.h"
#include "ai_graph_document.h"
#include <memory>

// Value-only GUI snapshot. No QObject, CNode, GL resource or document is
// referenced after publication. Geometry preparation and raster run off Qt.
struct CNativeEditorService {
    enum Primitive {Lines,Loop,Points,Triangles,Fan,Ring,Box,Strip};
    struct Batch {QVector<QVector3D> vertices;QVector4D color;Primitive primitive{Lines};float width{1};bool depth{true};QMatrix4x4 model;};
    struct Label {QVector3D position;QImage image;};
    std::shared_ptr<const CTerrainEditModel> grid;
    QByteArray terrainBytes;
    quint64 generation{0};
    CAiGraphDocument graph;int graphLayer{3};QVector<float> graphHeights;std::shared_ptr<const CTerrainEditModel> graphTerrain;
    QVector<Batch> batches;
    QVector<Label> labels;
    QString legend;
    QRect legendRect,selectionRect;
    QMatrix4x4 clip;
    QSize logicalSize;
    void add(const QVector<QVector3D>& vertices,const QVector4D& color,Primitive primitive=Lines,float width=1,bool depth=true,const QMatrix4x4& model={}) {
        if(!vertices.isEmpty())batches.append({vertices,color,primitive,width,depth,model});
    }
    QImage raster(const QSize& physicalSize,const QVector<float>& nativeDepth) const;
};
