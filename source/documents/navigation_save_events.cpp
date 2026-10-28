#include "navigation_save_events.h"
#include "mpr_document.h"
#include "endurance_document.h"
#include <QFileInfo>
#include <QDataStream>
#include <QtEndian>
#include <algorithm>
#include <functional>

namespace {
void staticObjects(const QVector<SMobSection>& sections, QVector<QByteArray>* result)
{
    for(const auto& section : sections) {
        // These are the same record kinds loaded as WorldObj, Lever and Torch.
        // UNIT and MAGIC_TRAP containers (including their nested OBJECT) do not
        // contribute static navigation collision.
        if(section.type==0xBBBB0000 || section.type==0xBBAB0000)continue;
        if(section.type==0xB001 || section.type==0xBBAC0000 || section.type==0xBBBF0000) {
            QByteArray geometry; QDataStream out(&geometry,QIODevice::WriteOnly);
            out<<section.type;
            std::function<void(const QVector<SMobSection>&)> fields;
            fields=[&](const QVector<SMobSection>& children) {
                for(const auto& field:children) {
                    switch(field.type) {
                    case 0xB002: case 0xB003: case 0xB006: case 0xB009:
                    case 0xB00A: case 0xB00C: case 0xB00D: case 0xB00E:
                    case 0xB012: out<<field.type<<field.rawBytes; break;
                    default: if(field.container)fields(field.children); break;
                    }
                }
            };
            fields(section.children);result->append(geometry);
        } else staticObjects(section.children,result);
    }
}
bool mobGeometry(const QByteArray& bytes,QVector<QByteArray>* objects)
{
    if(bytes.isEmpty())return true;
    CMobDocument document;CValidationReport report;
    if(!document.loadBytes(bytes,"navigation-save.mob",&report)||report.hasErrors())return false;
    staticObjects(document.sections(),objects);
    std::sort(objects->begin(),objects->end());return true;
}
bool terrainGeometry(const QByteArray& bytes,QByteArray* geometry)
{
    if(bytes.isEmpty())return true;
    CMprDocument document;CValidationReport report;
    if(!document.loadBytes(bytes,"navigation-save.mpr",&report)||report.hasErrors())return false;
    const auto& map=document.mapInfo();QDataStream out(geometry,QIODevice::WriteOnly);
    out<<map.maxZ<<map.sectorCountX<<map.sectorCountY;
    auto sectors=document.sectors();
    std::sort(sectors.begin(),sectors.end(),[](const SMprSectorInfo& a,const SMprSectorInfo& b){return qMakePair(a.sectorY,a.sectorX)<qMakePair(b.sectorY,b.sectorX);});
    for(const auto& sector:sectors) {
        const auto& payload=document.members()[sector.memberIndex].payload;
        out<<sector.sectorX<<sector.sectorY<<sector.hasOverlay;
        // XY offsets and heights; packed lighting normals are visual only.
        for(int i=0;i<33*33;++i)out<<payload.mid(int(sector.landVertexOffset)+i*8,4);
        if(sector.hasOverlay)for(int i=0;i<33*33;++i)out<<payload.mid(int(sector.overlayVertexOffset)+i*8,4);
        for(int i=0;i<16*16;++i) {
            const auto tileType=[&](quint64 offset) {
                const auto packed=qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(payload.constData()+int(offset)+i*2));
                return map.tileType(SMprPackedTile::decode(packed).atlasSlot());
            };
            out<<tileType(sector.landTileOffset);
            if(sector.hasOverlay) {
                const qint16 material=sector.overlayMaterialIndices.value(i,-1);
                out<<(material!=-1); // overlay coverage
                if(material!=-1)out<<tileType(sector.overlayTileOffset)<<map.materials.value(material).type;
            }
        }
    }
    return true;
}
}
CNavigationSaveEvents* CNavigationSaveEvents::instance()
{
    static CNavigationSaveEvents events;return &events;
}
bool CNavigationSaveEvents::geometryChanged(const QString& path,const QByteArray& before,const QByteArray& after)
{
    if(before==after)return false;
    const auto suffix=QFileInfo(path).suffix().toLower();
    if(suffix=="navsrc")return true;
    if(suffix=="mob") {
        QVector<QByteArray> a,b;
        return !mobGeometry(before,&a)||!mobGeometry(after,&b)||a!=b;
    }
    if(suffix=="mpr") {
        QByteArray a,b;
        return !terrainGeometry(before,&a)||!terrainGeometry(after,&b)||a!=b;
    }
    return false;
}
void CNavigationSaveEvents::committed(const QString& path,const QByteArray& before,const QByteArray& after)
{
    const auto suffix=QFileInfo(path).suffix().toLower();
    if(suffix=="mpr"||suffix=="mob"||suffix=="navsrc")
        emit instance()->mapSaved(QFileInfo(path).absoluteFilePath(),geometryChanged(path,before,after));
}
