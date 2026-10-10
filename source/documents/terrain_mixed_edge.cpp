#include "terrain_mixed_edge.h"
#include "endurance_document.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <algorithm>

namespace {
QVector<qint32> corners(const STerrainTransitionSignature& s)
{ return {s.northWest, s.northEast, s.southEast, s.southWest}; }
bool uniform(const STerrainTransitionSignature& s)
{ return s.northWest == s.northEast && s.northWest == s.southEast
    && s.northWest == s.southWest; }
bool opaque(const QImage& image)
{
    if(image.isNull() || image.width()!=image.height()) return false;
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
        if(qAlpha(image.pixel(x,y))!=255) return false;
    return true;
}
}

STerrainTileCommand CTerrainMixedEdge::resolve(const CTerrainEditModel& model,
    CTerrainWorkingAtlas* atlas, const QSet<int>& affectedCells,
    const QMap<qint32, qint32>& materialTypes,
    const QVector<SMprAnimatedTileRecord>& animations,
    const QPoint& origin, qint32 paintedMaterial, quint32 uniformSeed,
    bool randomizeUniformRotation, CValidationReport* report, int slotLimit)
{
    STerrainTileCommand command;
    command.description=QStringLiteral("Resolve mixed-edge terrain");
    command.semanticAudit.layer=ETerrainTileLayer::Land;
    command.semanticAudit.valid=true;
    command.semanticAudit.solverMode=QStringLiteral("mixed-edge-b2");
    auto refuse=[&](const QString& reason) {
        if(report) report->add("terrain.mixed_edge.refused", EValidationSeverity::Error, reason);
        return STerrainTileCommand();
    };
    CTerrainVirtualMaterialField field;
    if(!atlas || !atlas->isValid()
       || !model.copyVirtualMaterialField(ETerrainTileLayer::Land,&field,false))
        return refuse(QCoreApplication::translate("CTerrainMixedEdge", "Mixed-edge requires a complete Ground material field and a valid working atlas."));

    if(paintedMaterial>0 && !materialTypes.contains(paintedMaterial))
        return refuse(QCoreApplication::translate("CTerrainMixedEdge", "Mixed-edge has no opaque non-animated uniform source or reviewed runtime class for material %1 in this map's atlas/profile.").arg(paintedMaterial));

    if(atlas->activeTileCount()>slotLimit)
        return refuse(QCoreApplication::translate("CTerrainMixedEdge", "Mixed-edge atlas budget exceeded: %1 / %2 slots. No material was substituted; extended 8192 is not enabled.").arg(atlas->activeTileCount()).arg(slotLimit));

    // Follow the connected transition ring: leaving an old resolver tile on
    // the other side of a mixed shared edge would break the new edge contract.
    QSet<int> cells=affectedCells;
    QVector<int> queue=cells.values().toVector();
    const int w=field.tileWidth(), h=field.tileHeight();
    for(int i=0;i<queue.size();++i)
    {
        const int cell=queue[i], x=cell%w, y=cell/w;
        if(cell<0 || cell>=w*h) return refuse(QCoreApplication::translate("CTerrainMixedEdge", "Mixed-edge footprint is outside this map."));
        const auto s=field.cellSignature(x,y);
        const bool mixedEdges[]={s.northWest!=s.northEast,s.northEast!=s.southEast,
            s.southWest!=s.southEast,s.northWest!=s.southWest};
        const QPoint neighbors[]={QPoint(x,y-1),QPoint(x+1,y),QPoint(x,y+1),QPoint(x-1,y)};
        for(int edge=0;edge<4;++edge)
        {
            const auto p=neighbors[edge];
            if(!mixedEdges[edge] || p.x()<0 || p.y()<0 || p.x()>=w || p.y()>=h) continue;
            const int next=p.y()*w+p.x();
            if(!cells.contains(next)) { cells.insert(next); queue.append(next); }
        }
    }
    QSet<int> animatedSlots;
    for(const auto& animation:animations)
        for(int i=0;i<animation.frameCount;++i) animatedSlots.insert(animation.tileIndex+i);
    const auto& images=atlas->tileImages();
    const auto& catalog=atlas->catalog();
    QMap<qint32,QVector<int>> uniforms;
    QMap<QPair<qint32,qint32>,QVector<int>> stock;
    QMap<int,STerrainTransitionSignature> sourceSignatures;
    for(int slot=0;slot<atlas->authoredSlotCount();++slot)
    {
        if(animatedSlots.contains(slot)) continue;
        STerrainWorkingTileIdentity identity;
        if(atlas->tileIdentityForSlot(slot,&identity)
           && identity.blendProvenance.startsWith(QStringLiteral("mixed-edge-b2:"))) continue;
        SMprPackedTile packed; packed.atlasIndex=quint8(slot/64); packed.tileIndex=quint8(slot%64);
        bool known=false;
        const auto s=catalog.gridSignatureForPacked(packed.encode(),&known);
        if(!known || !opaque(images[slot])) continue;
        sourceSignatures.insert(slot,s);
        QSet<qint32> ids; for(qint32 id:corners(s)) ids.insert(id);
        if(ids.size()==1) uniforms[*ids.cbegin()].append(slot);
        else if(ids.size()==2) {
            auto sorted=ids.values(); std::sort(sorted.begin(),sorted.end());
            stock[qMakePair(sorted[0],sorted[1])].append(slot);
        }
    }
    QVector<int> ordered=cells.values().toVector(); std::sort(ordered.begin(),ordered.end());
    QSet<qint32> required;
    if(paintedMaterial>0) required.insert(paintedMaterial);
    QSet<QPair<qint32,qint32>> pairs;
    for(int cell:ordered)
    {
        const auto s=field.cellSignature(cell%w,cell/w);
        bool available=false;
        const quint16 before=model.packedTile(cell%w,cell/w,ETerrainTileLayer::Land,&available);
        if(!available || animatedSlots.contains(SMprPackedTile::decode(before).atlasSlot()))
            return refuse(QCoreApplication::translate("CTerrainMixedEdge", "Mixed-edge does not support animated Ground cells (%1,%2). Use the previous Modern path.").arg(cell%w).arg(cell/w));
        if(uniform(s)) continue;
        QSet<qint32> ids; for(qint32 id:corners(s)) { required.insert(id); ids.insert(id); }
        auto sorted=ids.values(); std::sort(sorted.begin(),sorted.end());
        for(int i=0;i<sorted.size();++i) for(int j=i+1;j<sorted.size();++j)
            pairs.insert(qMakePair(sorted[i],sorted[j]));
    }
    QMap<qint32,STerrainRecipeMaterial> materials;
    for(qint32 id:required)
    {
        if(uniforms.value(id).isEmpty() || materialTypes.value(id,-1)<0 || materialTypes.value(id,-1)>15)
            return refuse(QCoreApplication::translate("CTerrainMixedEdge", "Mixed-edge has no opaque non-animated uniform source or reviewed runtime class for material %1 in this map's atlas/profile.").arg(id));
        STerrainRecipeMaterial material;
        material.materialId=id; material.runtimeTerrainType=materialTypes[id];
        // Physical slot order is stable across strokes. Keep one source per
        // material: unrelated new recipes cannot reassign interior variants.
        material.uniformTiles.append(images[uniforms[id].first()]);
        materials.insert(id,material);
    }
    QMap<QPair<qint32,qint32>,STerrainRecipeBrush> brushes;
    auto orderedPairs=pairs.values(); std::sort(orderedPairs.begin(),orderedPairs.end());
    for(const auto& pair:orderedPairs)
    {
        QVector<QVector<float>> strips;
        QString lastError;
        // No generic replacement mask. Every bank is extracted from opaque
        // reviewed physical two-material stock in the current map's atlas.
        for(int slot:stock.value(pair))
        {
            QVector<float> strip; QString error;
            const QImage transition=images[slot].scaled(128,128,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            QImage bestFirst,bestSecond; double bestResidual=14;
            for(int a:uniforms[pair.first]) for(int b:uniforms[pair.second])
            {
                const QImage first=images[a].scaled(128,128,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                const QImage second=images[b].scaled(128,128,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
                QVector<float> ownership; double residual=0;
                if(CTerrainRecipeBaker::estimateStockOwnership(transition,first,second,&ownership,&residual,&error)
                   && residual<bestResidual)
                { bestResidual=residual; bestFirst=first; bestSecond=second; }
            }
            if(!bestFirst.isNull() && CTerrainRecipeBaker::extractStockStrip(transition,
                bestFirst,bestSecond,
                pair.first,pair.second,
                CTerrainTransitionCatalog::mirroredGridY(sourceSignatures[slot]),&strip,&error))
            {
                strips.append(strip);
                if(strips.size()==4) break;
            }
            else lastError=error.isEmpty()?QStringLiteral("No trusted RGB ownership fit."):error;
        }
        STerrainRecipeBrush brush; QString error;
        if(strips.isEmpty() || !CTerrainRecipeBaker::makeBrush(pair.first,pair.second,strips,&brush,&error))
            return refuse(QCoreApplication::translate("CTerrainMixedEdge", "Mixed-edge has no usable stock edge for materials %1 / %2 in this map's atlas/profile. %3").arg(pair.first).arg(pair.second).arg(error.isEmpty()?lastError:error));
        brushes.insert(pair,brush);
    }

    for(int cell:ordered)
    {
        const int x=cell%w,y=cell/w;
        const auto s=field.cellSignature(x,y);
        if(uniform(s)) continue;
        bool available=false;
        const quint16 before=model.packedTile(x,y,ETerrainTileLayer::Land,&available);
        STerrainRecipeBakeInput input; input.tileWidth=input.tileHeight=1;
        input.vertexMaterials={s.northWest,s.northEast,s.southWest,s.southEast};
        input.preferredMaterials={paintedMaterial};
        input.preferredRuntimeTypes={atlas->runtimeTerrainTypeForSlot(SMprPackedTile::decode(before).atlasSlot())};
        QSet<qint32> ids; for(qint32 id:corners(s)) ids.insert(id);
        auto sorted=ids.values(); std::sort(sorted.begin(),sorted.end());
        QByteArray key;
        for(qint32 id:corners(s)) key+=QByteArray::number(id)+',';
        for(qint32 id:sorted) input.materials.append(materials[id]);
        for(int i=0;i<sorted.size();++i) for(int j=i+1;j<sorted.size();++j) {
            const auto pair=qMakePair(sorted[i],sorted[j]);
            input.brushes.append(brushes[pair]);
        }
        STerrainRecipeBakeOptions options;
        options.tileSize=images.first().width(); options.origin=origin+QPoint(x,y);
        options.stableCoordinateVariants=true;
        options.preserveReviewedCellRuntimeTypes=paintedMaterial<=0;
        // Bake only one cell at a time, keeping peak raster memory bounded.
        // Atlas insertion checks the actual typed-dedup budget afterward.
        auto baked=CTerrainRecipeBaker::bake(input,options);
        if(!baked.success) return refuse(baked.diagnostics.join(QLatin1Char('\n')));
        const auto& recipe=baked.recipes.first();
        const auto& tile=baked.tiles[baked.tileByCell.first()];
        key+='|'+QByteArray::number(recipe.variant)+'|'+QByteArray::number(recipe.runtimeTerrainType);
        const QString recipeKey=QStringLiteral("mixed-edge-b2:")+QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex());
        int slot=atlas->mixedEdgeSlot(recipeKey);
        if(slot<0) slot=atlas->importMixedEdgeTile(tile.pixels,tile.runtimeTerrainType,s,recipeKey,report);
        if(slot<0 || atlas->activeTileCount()>slotLimit)
            return refuse(QCoreApplication::translate("CTerrainMixedEdge", "Mixed-edge atlas budget exceeded: %1 / %2 slots. No material was substituted; extended 8192 is not enabled.").arg(atlas->activeTileCount()).arg(slotLimit));
        SMprPackedTile packed; packed.atlasIndex=quint8(slot/64); packed.tileIndex=quint8(slot%64);
        const quint16 after=packed.encode();
        command.semanticAudit.affectedCells.append(QPoint(x,y));
        if(before!=after) {
            STerrainTileDelta delta; delta.globalX=x; delta.globalY=y;
            delta.before=before; delta.after=after; delta.hasSemanticRequest=true;
            delta.requestedSignature=s; delta.requestedMaterialId=paintedMaterial;
            command.deltas.append(delta);
        }
    }
    // Uniform cells keep the existing Modern selection and artwork exactly.
    STerrainTileCommand footprint; footprint.semanticAudit.layer=ETerrainTileLayer::Land;
    for(int cell:ordered) if(uniform(field.cellSignature(cell%w,cell/w)))
        footprint.semanticAudit.paintedCoreCells.append(QPoint(cell%w,cell/w));
    // makeVirtualStrokeResolution requires a nonempty vertex footprint.
    for(const QPoint& p:footprint.semanticAudit.paintedCoreCells)
        footprint.semanticAudit.paintedVertices.append(p);
    CValidationReport uniformReport;
    auto uniformCommand=model.makeVirtualStrokeResolution(footprint,catalog,uniformSeed,
        &uniformReport,randomizeUniformRotation,paintedMaterial);
    if(uniformReport.hasErrors()) return refuse(uniformReport.toText());
    for(const auto& delta:uniformCommand.deltas)
        if(uniform(delta.requestedSignature))
        {
            bool known=false;
            const auto placed=catalog.gridSignatureForPacked(delta.before,&known);
            // Compaction renumbers variants. Keep already matching uniform
            // artwork, including its existing Modern rotation.
            if(!known || !(placed==delta.requestedSignature)) command.deltas.append(delta);
        }

    return command;
}
