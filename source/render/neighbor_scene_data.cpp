#include "neighbor_scene_data.h"
#include "endurance_document.h"
#include "mpr_document.h"
#include <QHash>
#include <QJsonArray>
#include <QTextCodec>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace {
void indexSections(const QVector<SMobSection>& sections,
                   QHash<QString, const SMobSection*>& index)
{
    for(const auto& section : sections)
    {
        index.insert(section.path, &section);
        indexSections(section.children, index);
    }
}
QByteArray leaf(const SMobSection& object, quint32 type)
{
    for(const auto& field : object.children)
        if(field.type == type) return field.rawBytes.mid(8);
    return {};
}
QString text(const QByteArray& bytes)
{
    QByteArray value = bytes;
    while(value.endsWith('\0')) value.chop(1);
    return QTextCodec::codecForName("Windows-1251")->toUnicode(value);
}
QJsonArray floats(const QByteArray& bytes, int count, const QJsonArray& fallback)
{
    if(bytes.size() != count * 4) return fallback;
    QJsonArray result;
    for(int i=0; i<count; ++i)
    {
        const quint32 bits=qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(bytes.constData()+4*i));
        float value; std::memcpy(&value,&bits,4);
        if(!std::isfinite(value)) return fallback;
        result.append(double(value));
    }
    return result;
}
}

QVector<QJsonObject> neighborStaticObjects(const CMobDocument& document)
{
    QHash<QString, const SMobSection*> sections;
    indexSections(document.sections(), sections);
    QVector<QJsonObject> result;
    for(const auto& object : document.objects())
    {
        // Ordinary world objects contain buildings, trees and rocks. Units,
        // lights, sounds, particles and gameplay helpers are not background geometry.
        if(object.unit || object.sectionType != 0xB001 || !object.hasPosition
           || object.model.isEmpty()) continue;
        const auto* section=sections.value(object.sectionPath,nullptr);
        if(!section) continue;
        const QJsonArray position=floats(leaf(*section,0xB009),3,{});
        if(position.size()!=3) continue;
        QJsonObject base;
        base["Map name"]=object.name;
        base["Position"]=position;
        base["Complection"]=floats(leaf(*section,0xB00C),3,{1,1,1});
        // MOB stores scalar first (w,x,y,z); object JSON / QVector4D stores
        // scalar last (x,y,z,w), as in CMobParser::readQuaternion.
        const auto rotation=floats(leaf(*section,0xB00A),4,{1,0,0,0});
        base["Rotation"]=QJsonArray{rotation[1],rotation[2],rotation[3],rotation[0]};
        QJsonObject visual;
        visual["Base object"]=base;
        visual["Model name"]=object.model;
        visual["Primary texture"]=text(leaf(*section,0xB007));
        visual["Secondary texture"]=text(leaf(*section,0xB008));
        visual["Object parts"]=QJsonArray::fromStringList(object.bodyParts);
        const QByteArray shadow=leaf(*section,0xB014);
        visual["Is shadow?"]=shadow.isEmpty() || shadow.at(0)!=0;
        result.append(visual);
    }
    return result;
}

SNeighborSceneData prepareNeighborScene(const SNeighborMapPlacement& placement,
                                        bool loadStaticObjects, bool loadTerrain)
{
    SNeighborSceneData result; result.placement=placement;
    if(loadTerrain)
    {
        CMprDocument terrain; CValidationReport report;
        if(!terrain.load(QFileInfo(placement.mprPath),&report) || report.hasErrors()
           || int(terrain.mapInfo().sectorCountX)*32 != qRound(placement.size.width())
           || int(terrain.mapInfo().sectorCountY)*32 != qRound(placement.size.height()))
        {
            result.diagnostics.append("Neighbor terrain rejected: "+placement.zoneId+" "+report.toText());
            return result;
        }
        for(const auto& member : terrain.members())
            result.terrainMembers.insert(member.name,member.payload);
    }
    result.valid=true;
    if(loadStaticObjects && !placement.mobPath.isEmpty())
    {
        CMobDocument mob; CValidationReport mobReport;
        if(mob.load(QFileInfo(placement.mobPath),&mobReport) && !mobReport.hasErrors())
            result.staticObjects=neighborStaticObjects(mob);
        else result.diagnostics.append("Neighbor objects rejected: "+placement.zoneId+" "+mobReport.toText());
    }
    return result;
}
