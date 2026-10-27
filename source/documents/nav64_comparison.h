#pragma once
#include "nav64_document.h"
#include "ai_graph_preview_builder.h"
#include <QRect>
#include <atomic>
struct SNav64DifferenceRegion {
    bool lost=false;
    int cells=0;
    QVector3D center;
    QRect bounds;
};
struct SNav64Comparison {
    bool valid=false;
    QString error;
    int width=0,height=0,layer=0,lost=0,gained=0;
    qint64 milliseconds=0;
    QVector<quint8> cells;
    QVector<int> regions;
    QVector<float> heights;
    QVector<SNav64DifferenceRegion> areas;
    QVector<SNav64RenderTile> renderTiles;
};
int nav64LegacyLayer(const QString& profile);
QString nav64LegacyLayerLabel(int layer);
SNav64Comparison compareNav64(const SNav64Document& mesh,const SAiGraphPreviewBuildInput& input,
                             int layer,const std::shared_ptr<std::atomic_bool>& cancelled);
