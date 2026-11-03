// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ataman contributors
#include "quest_map_dialog.h"
#include "view.h"
#include "mob.h"
#include "landscape.h"
#include "resourcemanager.h"
#include "mmp_inspector.h"
#include "map_table_document.h"
#include "res_archive_index.h"
#include <QDir>
#include <cmath>
#include <memory>

bool CQuestMapSource::snapshot(CView* view,const SMapCatalogEntry& entry,SQuestMapSource* out,QString* error) {
    if(!view||!out||!view->m_terrainEditAvailable||!CLandscape::getInstance()->isMprLoad()) {
        if(error)*error=QObject::tr("Load an MPR and its MOB family before generating a quest map.");return false;
    }
    SQuestMapSource source;source.entry=entry;
    // Owning value snapshot includes unsaved terrain and water edits. The
    // dialog stays bound to this map if another map is opened in the editor.
    auto terrain=std::make_shared<CTerrainEditModel>(view->m_terrainEdit);
    auto materials=CLandscape::getInstance()->materials();
    source.terrain.sectorsX=terrain->tileCountX()/16;source.terrain.sectorsY=terrain->tileCountY()/16;
    source.terrain.heightAt=[terrain](float x,float y){return terrain->terrainSurfaceHeight(x,y);};
    source.terrain.waterKind=[terrain,materials](float x,float y){
        if(x<0||y<0||x>=terrain->tileCountX()*2||y>=terrain->tileCountY()*2)return 0;
        bool available=false;const int material=terrain->overlayMaterialIndex(int(x/2),int(y/2),&available);
        if(!available||material<0||material>=materials.size())return 0;
        const float z=terrain->navigationHeightSample(int(x*2),int(y*2),ETerrainTileLayer::Overlay,&available,true);
        if(!available||z<=terrain->terrainSurfaceHeight(x,y)+0.02f)return 0;
        return materials[material].G>materials[material].B?2:1;
    };
    QSet<CMob*> seen;
    for(auto* mob:view->mobs()) {
        if(!mob||seen.contains(mob))continue;seen.insert(mob);
        for(auto* node:mob->nodes()) {
            if(!node||node->isMarkDeleted()||node->nodeType()==eUnit)continue;
            const auto type=node->nodeType();if(type!=eWorldObject&&type!=eLever&&type!=eTorch)continue;
            questmap::Kind kind;auto name=node->modelName().toLower();
            if(!questmap::Classify(name.toStdString(),kind))continue;
            const auto pos=node->position();const auto dir=node->rotation().rotatedVector(QVector3D(1,0,0));
            source.markers.push_back({kind,pos.x(),pos.y(),float(std::atan2(dir.y(),dir.x()))});
        }
    }
    // Exit areas are represented losslessly by map.txt, including #remove.
    // Catalog's travel rectangles are deploy areas only; use the source
    // document to avoid drawing an entrance in place of the actual exit.
    if(!entry.sourceArchive.isEmpty()) {
        CResArchiveIndex archive;CMapTableDocument table;
        if(archive.open(entry.sourceArchive)&&table.loadBytes(archive.readMember("map.txt")))
            for(const auto& zone:table.zones())if(zone.zoneId.compare(entry.zoneId,Qt::CaseInsensitive)==0)
                for(const auto& exit:zone.exits)if(exit.hasEffectiveRemove()) {
                    const auto pos=exit.effectiveRemove().center();source.markers.push_back({questmap::Kind::Exit,float(pos.x()),float(pos.y()),0});
                }
    }
    auto loader=[](const QString& name){QByteArray bytes;CTextureList::getInstance()->terrainMmpPayload(name,&bytes);return bytes;};
    const auto archives=CTextureList::getInstance()->configuredResourceArchivePaths();
    auto iconLoader=[archives](const QString& name) {
        for(const auto& path:archives) {
            CResArchiveIndex archive;if(!archive.open(path)||archive.memberIndex(name)<0)continue;
            const auto bytes=archive.readMember(name);const auto info=ei::inspectMmp(bytes);
            if(info.valid&&info.width==256&&info.height==256)return bytes;
        }
        return QByteArray();
    };
    QString cache;source.notes=questmap::loadRuntimeIcons(iconLoader,&cache);
    source.icons=questmap::Icons();
    if(!cache.isEmpty())source.notes<<QObject::tr("Icon cache: %1").arg(QDir::toNativeSeparators(cache));
    if(!entry.objectivesTexture.isEmpty()) {
        QString member=entry.objectivesTexture;if(!member.endsWith(".mmp",Qt::CaseInsensitive))member+=".mmp";
        auto info=ei::inspectMmp(loader(member));if(info.valid){source.original=info.baseImage;source.originalFormat=info.format;}
    }
    *out=source;return true;
}
