#include "terrain_mixed_edge.h"
#include "endurance_document.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <algorithm>
#include <cmath>

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
int stockFragments(const QVector<float>& strip)
{
    QVector<bool> seen(strip.size(),false);
    QVector<int> queue;
    int fragments=0;
    for(int start=0;start<strip.size();++start)
    {
        if(seen[start]) continue;
        queue.clear(); queue.append(start); seen[start]=true;
        const bool side=strip[start]>=.5f;
        for(int i=0;i<queue.size();++i)
        {
            const int p=queue[i],x=p%65;
            const int next[]={x>0?p-1:-1,x<64?p+1:-1,p-65,p+65};
            for(int q:next) if(q>=0 && q<strip.size() && !seen[q] && (strip[q]>=.5f)==side)
            { seen[q]=true; queue.append(q); }
        }
        if(queue.size()>=2 && queue.size()<=64) ++fragments;
    }
    return fragments;
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
    if(!buildStockBrushes(images,uniforms,stock,sourceSignatures,pairs,&brushes,report))return {};

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
        qint32 preferredType=atlas->runtimeTerrainTypeForSlot(SMprPackedTile::decode(before).atlasSlot());
        if(paintedMaterial>0)
        {
            QMap<qint32,int> coverage;
            int maximum=0;
            for(qint32 id:corners(s)) maximum=qMax(maximum,++coverage[materials[id].runtimeTerrainType]);
            const qint32 paintedType=materialTypes[paintedMaterial];
            // A painted class among the maximal coverages wins a tie. The
            // predecessor remains the preference on connected unpainted rings;
            // it cannot override the material just painted with an unrelated
            // old SEC class. Repair still preserves the reviewed predecessor.
            if(coverage.value(paintedType)==maximum) preferredType=paintedType;
        }
        input.preferredRuntimeTypes={preferredType};
        QSet<qint32> ids; for(qint32 id:corners(s)) ids.insert(id);
        auto sorted=ids.values(); std::sort(sorted.begin(),sorted.end());
        // Invalidate soft B2 recipes without discarding their saved slots or
        // Undo history. New crisp recipes are reused across fresh reopen too.
        QByteArray key("crisp-stock-v1|");
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

bool CTerrainMixedEdge::buildStockBrushes(const QVector<QImage>& images,
    const QMap<qint32,QVector<int>>& uniforms,
    const QMap<QPair<qint32,qint32>,QVector<int>>& stock,
    const QMap<int,STerrainTransitionSignature>& sourceSignatures,
    const QSet<QPair<qint32,qint32>>& pairs,
    QMap<QPair<qint32,qint32>,STerrainRecipeBrush>* output,
    CValidationReport* report)
{
    if(!output)return false;
    auto refuseBrush=[&](const QString& message) {
        if(report)report->add("terrain.mixed_edge.refused",EValidationSeverity::Error,message);
        return false;
    };
    QMap<QPair<qint32,qint32>,STerrainRecipeBrush> brushes;
    auto orderedPairs=pairs.values(); std::sort(orderedPairs.begin(),orderedPairs.end());
    for(const auto& pair:orderedPairs)
    {
        struct SStockCandidate { int slot, fragments; double score, residual, contrast; QVector<float> strip; };
        QVector<SStockCandidate> candidates;
        QString lastError;
        // No generic replacement mask. Every bank is extracted from opaque
        // reviewed physical two-material stock in the current map's atlas.
        for(int slot:stock.value(pair))
        {
            QVector<float> strip; QString error;
            const QImage transition=images[slot].scaled(128,128,Qt::IgnoreAspectRatio,Qt::FastTransformation);
            QImage bestFirst,bestSecond; double bestResidual=14;
            for(int a:uniforms[pair.first]) for(int b:uniforms[pair.second])
            {
                const QImage first=images[a].scaled(128,128,Qt::IgnoreAspectRatio,Qt::FastTransformation);
                const QImage second=images[b].scaled(128,128,Qt::IgnoreAspectRatio,Qt::FastTransformation);
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
                double contrast=0;
                for(int y=0;y<128;++y) for(int x=0;x<128;++x)
                {
                    const QRgb a=bestFirst.pixel(x,y),b=bestSecond.pixel(x,y);
                    contrast+=std::pow(qRed(a)-qRed(b),2)+std::pow(qGreen(a)-qGreen(b),2)
                        +std::pow(qBlue(a)-qBlue(b),2);
                }
                contrast=std::sqrt(contrast/(128*128*3.0));
                // Absolute RGB residual alone rewards uncertain low-contrast
                // fits. Rank every local source by error relative to contrast,
                // then by recovered fractional coverage. Never stop at the
                // first four catalog entries (which can be soft legacy art).
                double intermediate=0;
                for(float v:strip) intermediate+=v>.1f && v<.9f;
                intermediate/=strip.size();
                const double relative=bestResidual/qMax(contrast,1.0);
                const int fragments=stockFragments(strip);
                if(contrast>1 && relative<.2)
                    candidates.append({slot,fragments,relative+intermediate*.03-qMin(.15,fragments*.002),
                        bestResidual,contrast,strip});
                else lastError=QCoreApplication::translate("CTerrainMixedEdge", "Stock ownership is uncertain relative to material contrast.");
            }
            else lastError=error.isEmpty()?QStringLiteral("No trusted RGB ownership fit."):error;
        }
        std::sort(candidates.begin(),candidates.end(),[](const SStockCandidate& a,const SStockCandidate& b) {
            return a.score==b.score ? a.slot<b.slot : a.score<b.score;
        });
        // Stage1b's art choice: prefer genuine stock flecks over a clean HD
        // outline, but only among confident normalized RGB fits. A simple
        // authored straight edge remains usable when no ragged source exists.
        const bool ragged=std::any_of(candidates.cbegin(),candidates.cend(),[](const SStockCandidate& c) {
            return c.fragments>=16 && c.residual/c.contrast<.15;
        });
        QVector<QVector<float>> strips;
        for(const auto& c:candidates)
        {
            if(ragged && (c.fragments<16 || c.residual/c.contrast>=.15)) continue;
            strips.append(c.strip);
            if(report) report->add("terrain.mixed_edge.stock",EValidationSeverity::Info,
                QStringLiteral("Crisp stock %1 / %2: slot %3, RGB RMSE %4, contrast RMS %5, relative score %6, fragments %7.")
                    .arg(pair.first).arg(pair.second).arg(c.slot).arg(c.residual,0,'f',6)
                    .arg(c.contrast,0,'f',6).arg(c.score,0,'f',6).arg(c.fragments));
            if(strips.size()==4) break;
        }
        STerrainRecipeBrush brush; QString error;
        if(strips.isEmpty() || !CTerrainRecipeBaker::makeBrush(pair.first,pair.second,strips,&brush,&error))
            return refuseBrush(QCoreApplication::translate("CTerrainMixedEdge", "Mixed-edge has no usable stock edge for materials %1 / %2 in this map's atlas/profile. %3").arg(pair.first).arg(pair.second).arg(error.isEmpty()?lastError:error));
        CTerrainRecipeBaker::sharpenStockBrush(&brush);
        brushes.insert(pair,brush);
    }

    *output=brushes;return true;
}
