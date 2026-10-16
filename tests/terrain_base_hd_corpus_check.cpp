#include <QtWidgets>
#include "settings.h"
#include "resourcemanager.h"
#include "mpr_document.h"
#include "terrain_atlas_profile.h"
#include "terrain_atlas_archive_binding.h"
#include "res_archive_index.h"
#include "endurance_document.h"
#include "terrain_base_hd.h"
#include "mmp_codec.h"
#include "terrain_base_hd_seams.h"

int runScenarioLayerCheck()
{
    QTextStream out(stdout);out.setCodec("UTF-8");
    {
        QImage black(256,256,QImage::Format_RGBA8888);black.fill(Qt::black);
        QImage gutter=black;for(int y=0;y<8;++y)for(int x=0;x<256;++x)gutter.setPixelColor(x,y,Qt::white);
        QImage stripe=black;for(int y=64;y<192;++y)for(int x=15;x<=16;++x)stripe.setPixelColor(x,y,Qt::white);
        CBaseHdSeams check({black,black},{black,gutter,stripe});
        SMprPackedTile rotated;rotated.tileIndex=2;rotated.rotation=1;
        if(!check.safe(0,0,0,1,0)||!check.boundarySafe(0,rotated.encode(),0))return 3;
        rotated.rotation=3;
        if(check.boundarySafe(0,rotated.encode(),0)||check.safe(0,0,rotated.encode(),0,0))return 4;
        out<<"PASS synthetic gutter, visible inset, rotation and local discontinuity\n";
    }
    const QString output=qEnvironmentVariable("EI_HD_AUDIT_OUTPUT");QDir().mkpath(output);
    QTemporaryDir scratch;QDir::setCurrent(scratch.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,scratch.path());
    auto* settings=new CSettings;CTextureList::getInstance()->attachSettings(settings);
    const QString game=qEnvironmentVariable("EI_HD_GAME_ROOT"); if(game.isEmpty())return 2;
    const QString library=qEnvironmentVariable("EI_HD_LIBRARY");
    QJsonArray results;QMap<QString,QVector<QImage>> pageCache;
    const QStringList profiles={"DejaVu"};
    for(const auto& profileId:profiles){
        const QString base=profileId=="Base"?game+"/Base":game+"/MODS/"+profileId;
        QVector<QString> roots={base+"/res"};if(profileId!="Base")roots.append(game+"/Base/Res");
        dynamic_cast<COptStringList*>(settings->opt(eOptSetResource,"resourceRoots"))->setValue(roots);
        CTextureList::getInstance()->setMapLocalTerrainArchive(QString());
        const auto maps=QDir(base+"/Maps").entryInfoList({"*.mpr"},QDir::Files,QDir::Name);
        for(const auto& path:maps){
            CValidationReport report;CMprDocument doc;QJsonObject row{{"profile",profileId},{"map",path.fileName()}};
            if(!doc.load(path,&report)){row["error"]=report.toText();results.append(row);continue;}
            const auto info=doc.mapInfo();row["namespace"]=info.innerNamespace;
            CTerrainEditModel terrain;if(!terrain.load(doc,&report)){row["error"]=report.toText();results.append(row);continue;}
            QVector<QImage> images;QStringList archives;QVector<QByteArray> originalPages;
            for(int page=0;page<int(info.textureCount);++page){
                const QString name=QStringLiteral("%1%2.mmp").arg(info.innerNamespace).arg(page,3,10,QLatin1Char('0'));
                QByteArray payload;QString archive;
                if(!CTextureList::getInstance()->terrainMmpPayload(name,&payload,&archive)){row["error"]="Missing page "+name;break;}
                originalPages.append(payload);
                if(!archives.contains(archive))archives.append(archive);
                const QString key=QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex());
                QVector<QImage> tiles;
                if(pageCache.contains(key))tiles=pageCache.value(key);
                else {for(int local=0;local<64;++local){QImage image;QString error;
                    if(!ei::decodeMmpDxt1AtlasTile(payload,local,&image,&error)){row["error"]=error;break;}
                    tiles.append(image.convertToFormat(QImage::Format_RGBA8888));}
                    if(pageCache.size()>12)pageCache.clear();pageCache.insert(key,tiles);}
                images+=tiles;
            }
            if(row.contains("error")){results.append(row);continue;}
            const auto digest=CTerrainAtlasProfileInference::pixelSha256(images);
            STerrainAtlasProfile profile;
            for(const auto& path:archives){CResArchiveIndex index;QString error;if(!index.open(path,&error))continue;
                const auto member=index.readMember(CTerrainAtlasProfileStore::embeddedMemberName(info.innerNamespace),&error);
                STerrainAtlasProfile candidate;if(STerrainAtlasProfile::fromBytes(member,&candidate)
                    && candidate.validateBinding(info.innerNamespace,digest,images.size())){profile=candidate;break;}}
            if(profile.slotDefinitions.isEmpty())profile=CTerrainAtlasPresetCatalog::exactProfile(info.innerNamespace,digest,images.size());
            QVector<STerrainAtlasSlotUsage> usage(images.size());
            for(int y=0;y<terrain.tileCountY();++y)for(int x=0;x<terrain.tileCountX();++x){
                bool ok=false;const auto land=SMprPackedTile::decode(terrain.packedTile(x,y,ETerrainTileLayer::Land,&ok));
                if(ok&&land.atlasSlot()<usage.size())++usage[land.atlasSlot()].landCount;
                const auto overlay=terrain.packedTile(x,y,ETerrainTileLayer::Overlay,&ok);
                if(ok&&overlay!=0xffff&&SMprPackedTile::decode(overlay).atlasSlot()<usage.size()){
                    ++usage[SMprPackedTile::decode(overlay).atlasSlot()].overlayCount;
                    if(land.atlasSlot()<usage.size())++usage[land.atlasSlot()].landBelowOverlayCount;}}
            if(profile.slotDefinitions.isEmpty()){
                auto types=info.tileTypes;while(types.size()<images.size())types.append(-1);
                profile=CTerrainAtlasProfileInference::propose(info.innerNamespace,images,types,usage,&report);}
            row["provenance"]=profile.provenance;row["families"]=profile.families.size();
            const auto analysis=profile.usableAnalysis();
            row["catalogUsable"]=analysis.usable;row["landCells"]=terrain.tileCountX()*terrain.tileCountY();
            CTerrainWorkingAtlas atlas(analysis.overlayCatalogWithLandFamilies(),images,192,16384,{}, {},info.tileTypes);
            terrain.enableWorkingAtlasSlots(atlas.capacityLimit());

            const auto after=prepareBaseTerrainHd(library,atlas,terrain,profile,info.animatedTiles,192,nullptr,originalPages);
            const auto& before=after; row["error"]=after.error;row["eligible"]=after.eligibleCells;
            row["unmatched"]=after.unmatchedCells;
            row["seamRejected"]=after.seamRejectedCells;

            row["unmatchedSources"]=QJsonArray::fromStringList(after.unmatchedSources);
            auto require=[&](bool ok,const QString& what){if(!ok)throw QString(path.fileName()+": "+what+" "+report.toText());};
            require(after.error.isEmpty()&&after.unmatchedCells==0,"complete recognized base coverage");
            require(after.command.deltas.size()+after.seamRejectedCells==after.eligibleCells,"all eligible cells accounted for");
            auto changed=terrain;
            require(changed.apply(after.command,true,&report),"apply");
            const auto verifySeams=[&](const QVector<QImage>& candidate){
                CBaseHdSeams seams(images,candidate);const int dx[4]={0,1,0,-1},dy[4]={-1,0,1,0};
                for(const auto& delta:after.command.deltas)for(int side=0;side<4;++side){
                    require(seams.boundarySafe(delta.before,delta.after,side),"old visible rim exactly preserved");
                    const int x=delta.globalX+dx[side],y=delta.globalY+dy[side];
                    if(x<0||y<0||x>=terrain.tileCountX()||y>=terrain.tileCountY())
                        require(seams.boundarySafe(delta.before,delta.after,side),"external map boundary unchanged");
                    else require(seams.safe(delta.before,terrain.packedTile(x,y,ETerrainTileLayer::Land),delta.after,changed.packedTile(x,y,ETerrainTileLayer::Land),side),"every final neighbor seam nonworsening");
                }
            };
            verifySeams(after.atlas->activeTileImages());
            QSet<int> protectedSlots;
            for(const auto& a:info.animatedTiles)for(int i=0;i<qMax(1,int(a.frameCount));++i)protectedSlots.insert(a.tileIndex+i);
            for(int y=0;y<terrain.tileCountY();++y)for(int x=0;x<terrain.tileCountX();++x){
                bool a=false,b=false;
                const auto oldOverlay=terrain.packedTile(x,y,ETerrainTileLayer::Overlay,&a);
                const auto newOverlay=changed.packedTile(x,y,ETerrainTileLayer::Overlay,&b);
                require(a==b&&oldOverlay==newOverlay,"overlay unchanged");
                if(a&&oldOverlay!=0xffff)protectedSlots.insert(SMprPackedTile::decode(oldOverlay).atlasSlot());
            }
            for(const auto& delta:after.command.deltas){
                const int oldSlot=SMprPackedTile::decode(delta.before).atlasSlot();
                const int newSlot=SMprPackedTile::decode(delta.after).atlasSlot();
                require(SMprPackedTile::decode(delta.before).rotation==SMprPackedTile::decode(delta.after).rotation,"random rotation never rotates the preserved rim");
                bool known=false;auto signature=atlas.catalog().gridSignatureForPacked(delta.before,&known);
                require(known&&signature.northWest&&signature.northWest==signature.northEast&&signature.northWest==signature.southEast&&signature.northWest==signature.southWest&&!protectedSlots.contains(oldSlot),"only pure nonanimated base changed");
                require(info.tileTypes.value(oldSlot,-1)!=6,"water class unchanged");
                require(atlas.runtimeTerrainTypeForSlot(oldSlot)==after.atlas->runtimeTerrainTypeForSlot(newSlot),"physical class preserved");
            }
            auto sectors=[&](const CTerrainEditModel& model){QByteArray bytes;for(int sy=0;sy<int(info.sectorCountY);++sy)for(int sx=0;sx<int(info.sectorCountX);++sx)bytes+=model.sectorPayload(sx,sy);return bytes;};
            const auto modified=sectors(changed);
            require(changed.apply(after.command,false,&report)&&sectors(changed)==sectors(terrain),"Undo exact");
            require(changed.apply(after.command,true,&report)&&sectors(changed)==modified,"Redo exact");
            CMprEditTransaction edits;QByteArray editedBytes;
            require(changed.appendSectorEdits(&edits,&report)&&doc.applyTransaction(edits,&editedBytes,&report),"serialize edited sectors");
            CMprDocument edited;require(edited.loadBytes(editedBytes,path.fileName(),&report),"edited document");
            const auto package=CTerrainVirtualAtlas::buildWorkingPreviewPackage(edited,after.atlas->activeTileImages(),after.atlas->activeTileTypes(),originalPages,info.materials,info.animatedTiles,&report,after.atlas->preservedDxtTiles());
            require(package.success,"native-page save package");
            CMprDocument reopened;require(reopened.loadBytes(package.mprBytes,path.fileName(),&report),"reopen saved MPR");
            CTerrainEditModel reopenedTerrain;require(reopenedTerrain.load(reopened,&report)&&sectors(reopenedTerrain)==modified,"reopened sectors exact");
            require(reopened.mapInfo().materials==info.materials&&reopened.mapInfo().animatedTiles==info.animatedTiles,"water materials and animations exact");
            QTemporaryFile res(scratch.path()+"/saved-XXXXXX.res");require(res.open()&&res.write(package.resourceArchiveBytes)==package.resourceArchiveBytes.size()&&res.flush(),"private saved RES");
            CResArchiveIndex saved;QString saveError;require(saved.open(res.fileName(),&saveError),"reopen saved RES");
            for(int page=0;page<originalPages.size();++page)require(saved.readMember(QStringLiteral("%1%2.mmp").arg(info.innerNamespace).arg(page,3,10,QLatin1Char('0')),&saveError)==originalPages[page],"original page byte exact");
            auto savedImages=after.atlas->activeTileImages();
            for(int page=originalPages.size();page<savedImages.size()/64;++page){
                const auto bytes=saved.readMember(QStringLiteral("%1%2.mmp").arg(info.innerNamespace).arg(page,3,10,QLatin1Char('0')),&saveError);
                for(int local=0;local<64;++local)require(ei::decodeMmpDxt1AtlasTile(bytes,local,&savedImages[page*64+local],&saveError),"decode saved HD tile");
            }
            verifySeams(savedImages);row["previewAndSavedSeams"]=true;
            row["invariantsUndoRedoSaveReopen"]=true;row["convertedCells"]=after.command.deltas.size();
            QJsonArray used;QMap<qint32,QString> names;
            for(const auto& f:profile.families)names[f.id]=f.name;
            for(const auto& d:profile.slotDefinitions)if(d.assigned&&d.slot>=0&&d.slot<usage.size()&&usage[d.slot].landCount>0
               &&d.signature.northWest==d.signature.northEast&&d.signature.northWest==d.signature.southEast&&d.signature.northWest==d.signature.southWest){
                used.append(QJsonObject{{"slot",d.slot},{"material",names.value(d.signature.northWest)},
                    {"cells",double(usage[d.slot].landCount)},{"type",info.tileTypes.value(d.slot,-1)}});
                const QString file=profileId+"-"+path.completeBaseName()+"-"+QString::number(d.slot)+".png";
                if(profileId=="DejaVu"&&path.completeBaseName()=="zone19")images[d.slot].save(output+"/"+file);
            }
            row["usedUniform"]=used;results.append(row);
            QFile file(output+"/audit.json");file.open(QIODevice::WriteOnly);file.write(QJsonDocument(results).toJson());
            out<<profileId<<"/"<<path.fileName()<<" eligible="<<before.eligibleCells<<" unmatched="<<after.unmatchedCells<<" error="<<row["error"].toString()<<"\n";out.flush();
        }
    }
    if(results.isEmpty())return 2;
    for(const auto& value:results){
        const auto row=value.toObject();
        if(!row.value("error").toString().isEmpty()||!row.value("invariantsUndoRedoSaveReopen").toBool())return 1;
    }
    return 0;
}
