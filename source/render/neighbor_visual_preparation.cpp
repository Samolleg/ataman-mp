#include "neighbor_visual_preparation.h"
#include "resourcemanager.h"
#include "res_archive_index.h"
#include "res_file.h"
#include "resource_name.h"
#include <QJsonArray>
#include <QJsonDocument>

void prepareNeighborVisuals(SNeighborSceneData& scene, const QStringList& archives,
                           const std::function<bool()>& current)
{
    QVector<CResArchiveIndex> sources;
    for(const auto& path : archives)
    {
        if(!current()) return;
        CResArchiveIndex index;
        if(index.open(path)) sources.append(index);
    }
    CResFile auxiliary(QStringLiteral(":/auxData.res"));
    const auto aux=auxiliary.bufferOfFiles();
    const auto read=[&](const QString& name) {
        for(const auto& index : sources)
            if(index.memberIndex(name)>=0) return index.readMember(name);
        return aux.value(name);
    };
    QHash<QString,QVector<SPreparedStaticPart>> geometry;
    QHash<QString,SPreparedStaticTexture> textures;
    QHash<QString,QSharedPointer<const SPreparedStaticVisual>> cache;
    for(const auto& object : scene.staticObjects)
    {
        if(!current()) return;
        const QString model=object.value("Model name").toString().toLower();
        const QString texture=object.value("Primary texture").toString().toLower();
        const auto base=object.value("Base object").toObject();
        const auto constitution=base.value("Complection").toArray();
        const auto parts=object.value("Object parts").toArray();
        const QString geometryKey=model+'|'+QString::fromUtf8(QJsonDocument(constitution).toJson(QJsonDocument::Compact))
            +'|'+QString::fromUtf8(QJsonDocument(parts).toJson(QJsonDocument::Compact));
        const QString textureKey=ei::textureCacheKey(texture,model);
        const QString visualKey=geometryKey+'|'+textureKey;
        if(!cache.contains(visualKey))
        {
            // Humanoid assemblies and inventory figures have additional UV /
            // appearance rules. Retain that established path rather than
            // silently replacing them by ordinary scenery geometry.
            if((model.startsWith("unhuma") && texture.startsWith("unhuma") && !parts.isEmpty())
                || (model.startsWith("init") && (model.endsWith("weapon") || model.endsWith("armor") || model.endsWith("item"))))
            {
                QSharedPointer<SPreparedStaticVisual> visual(new SPreparedStaticVisual);
                visual->key=visualKey; visual->specializedVisual=true;
                cache.insert(visualKey,visual); scene.visuals.append(visual); continue;
            }
            if(!geometry.contains(geometryKey))
            {
                QStringList names; for(const auto& p:parts) names.append(p.toString());
                QMap<QString,QByteArray> files;
                files.insert(model+".mod",read(model+".mod"));
                files.insert(model+".bon",read(model+".bon"));
                QVector3D complexion(1,1,1);
                if(constitution.size()==3) complexion=QVector3D(float(constitution[0].toDouble()),float(constitution[1].toDouble()),float(constitution[2].toDouble()));
                auto prepared=CObjectList::prepareStaticAssembly(files,model,complexion,names);
                if(prepared.isEmpty())
                {
                    scene.diagnostics.append("Neighbor model unavailable: "+model);
                    prepared=CObjectList::prepareStaticAssembly(aux,"cannotdisplay",QVector3D(1,1,1),{});
                }
                geometry.insert(geometryKey,prepared);
            }
            if(!textures.contains(textureKey))
            {
                SPreparedStaticTexture prepared;
                for(const auto& candidate:ei::textureResourceCandidates(texture,model))
                {
                    const auto bytes=read(candidate);
                    if(bytes.isEmpty()) continue;
                    prepared=CTextureList::prepareStaticTexture(bytes);
                    if(prepared.valid()) break;
                }
                if(!prepared.valid()) scene.diagnostics.append("Neighbor texture unavailable: "+texture+" model:"+model);
                textures.insert(textureKey,prepared);
            }
            QSharedPointer<SPreparedStaticVisual> visual(new SPreparedStaticVisual);
            visual->key=visualKey;
            visual->parts=geometry.value(geometryKey);
            visual->texture=textures.value(textureKey);
            cache.insert(visualKey,visual);
        }
        scene.visuals.append(cache.value(visualKey));
    }
}
