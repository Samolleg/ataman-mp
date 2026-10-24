#include "new_terrain_canvas.h"
#include "process_edit_lock.h"
#include "mpr_document.h"
#include "endurance_document.h"
#include "terrain_atlas_archive_binding.h"
#include "terrain_atlas_profile.h"
#include "resource_name.h"
#include "mmp_codec.h"
#include "res_file.h"
#include "res_archive_document.h"
#include "res_archive_index.h"
#include "terrain_atlas_compact.h"
#include "terrain_virtual_atlas.h"
#include "terrain_save_flow_policy.h"
#include <QTemporaryDir>
#include <QDir>
#include <QFile>

bool buildNewTerrainAtlas(const QFileInfo& terrainTemplate,
                          const QByteArray& candidateMpr,
                          const QStringList& resourceArchives,
                          QByteArray* atlas, QString* error)
{
    if(atlas) atlas->clear();
    const auto fail = [&](const QString& message) {
        if(error) *error = message;
        return false;
    };
    CMprDocument source, target;
    CValidationReport report;
    if(!atlas || !source.load(terrainTemplate, &report) || report.hasErrors()
       || !target.loadBytes(candidateMpr, "new-canvas.mpr", &report)
       || report.hasErrors())
        return fail("Invalid terrain template or candidate MPR.\n" + report.toText());
    // Dimensions/maxZ may differ; every atlas/material/animation byte must
    // still describe the very same physical slots as the template.
    if(source.memberPayload(source.mapInfo().memberName).mid(16)
       != target.memberPayload(target.mapInfo().memberName).mid(16))
        return fail("The candidate's terrain metadata does not match its template.");
    const QString sourceNamespace = source.mapInfo().innerNamespace;
    QStringList archives;
    archives << terrainTemplate.dir().filePath(
        terrainTemplate.completeBaseName() + "-atlas.res");
    archives << terrainTemplate.dir().filePath("textures.res");
    archives << resourceArchives;
    archives.removeDuplicates();
    const auto embedded = CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
        archives, sourceNamespace);
    if(embedded.success())
        archives = QStringList{embedded.archivePath};
    const QStringList names = ei::terrainTextureResourceNames(
        sourceNamespace, int(source.mapInfo().textureCount));
    const QStringList targetNames = ei::terrainTextureResourceNames(
        target.mapInfo().innerNamespace, int(target.mapInfo().textureCount));
    if(names.isEmpty() || names.size() != targetNames.size())
        return fail("The template has no complete terrain page set.");
    for(const QString& path : archives)
    {
        if(!QFileInfo(path).isFile()) continue;
        QMap<QString, QByteArray> pages;
        if(!CTerrainAtlasArchiveBindingResolver::readArchiveMembers(path, names, &pages))
            continue;
        QVector<QImage> tiles;
        bool complete = true;
        for(const QString& name : names)
        {
            const QByteArray page = pages.value(name.toLower());
            for(int slot = 0; slot < 64; ++slot)
            {
                QImage tile;
                if(!ei::decodeMmpDxt1AtlasTile(page, slot, &tile))
                { complete = false; break; }
                tiles.append(tile.convertToFormat(QImage::Format_RGBA8888));
            }
            if(!complete) break;
        }
        if(!complete) continue;
        const QByteArray hash = CTerrainAtlasProfileInference::pixelSha256(tiles);
        STerrainAtlasProfile profile;
        if(embedded.success())
        {
            profile = embedded.profile;
            if(!profile.validateBinding(sourceNamespace, hash, tiles.size(), &report)
               || profile.textureCount != int(source.mapInfo().textureCount))
                return fail("The template's embedded atlas binding is inconsistent.");
        }
        else if(CTerrainAtlasProfileStore::loadForMpr(sourceNamespace,
                    terrainTemplate, &profile))
        {
            if(!profile.validateBinding(sourceNamespace, hash, tiles.size())) continue;
        }
        else
            profile = CTerrainAtlasPresetCatalog::exactProfile(
                sourceNamespace, hash, tiles.size());
        if(profile.slotDefinitions.isEmpty())
            profile = CTerrainAtlasProfileInference::propose(sourceNamespace,
                tiles, source.mapInfo().tileTypes, {}, &report);
        if(!profile.usableAnalysis().usable)
            return fail("The template atlas has no usable terrain materials.");
        profile.mprNamespace = target.mapInfo().innerNamespace;
        profile.textureCount = targetNames.size();
        profile.resourceArchiveHint.clear();
        CResFile result;
        for(int i = 0; i < names.size(); ++i)
            result.addFiledata(targetNames.at(i), pages.value(names.at(i).toLower()));
        result.addFiledata(CTerrainAtlasProfileStore::embeddedMemberName(
            profile.mprNamespace), profile.toBytes());
        const QByteArray bytes = result.generateResData();
        CResArchiveDocument reopened;
        if(!reopened.loadBytes(bytes, "new-canvas-atlas.res"))
            return fail("The new terrain archive could not be reopened.");
        for(int i = 0; i < names.size(); ++i)
            if(reopened.memberPayload(targetNames.at(i)) != pages.value(names.at(i).toLower()))
                return fail("The new terrain archive changed an authored page.");
        *atlas = bytes;
        return true;
    }
    return fail("The template's complete terrain atlas could not be found. "
                "Configure its resource archive before creating this map.");
}

SMapSaveBatchResult commitNewTerrainCanvas(const QFileInfo& terrainTemplate,
                                          const QByteArray& candidateMpr,
                                          const QFileInfo& destination,
                                          const QStringList& resourceArchives,
                                          const QString& backupRoot, bool createOnly)
{
    SMapSaveBatchResult result;
    if(terrainTemplate.absoluteFilePath().compare(destination.absoluteFilePath(),
                                                 Qt::CaseInsensitive) == 0)
    { result.error = "The new map must not replace its template."; return result; }
    QString publicationPath;
    for(const auto& path : resourceArchives)
        if(terrainPublicationIsHDPackArchive(path) && QFileInfo(path).isFile())
        { publicationPath = QFileInfo(path).absoluteFilePath(); break; }
    if(publicationPath.isEmpty())
    { result.error = "Configure HDPack_Terrain.res before creating a new map."; return result; }
    auto resourceLease=CProcessEditLock::acquire(publicationPath,&result.error,false);
    auto mapLease=CProcessEditLock::acquire(destination.absoluteFilePath(),&result.error);
    if(!resourceLease||!mapLease)return result;
    QByteArray atlas;
    if(!buildNewTerrainAtlas(terrainTemplate, candidateMpr, resourceArchives,
                             &atlas, &result.error)) return result;
    CMprDocument map; CValidationReport report;
    CResArchiveDocument local;
    if(!map.loadBytes(candidateMpr, destination.fileName(), &report)
       || !local.loadBytes(atlas))
    { result.error = "Cannot reopen the generated map/atlas."; return result; }
    STerrainAtlasProfile profile;
    if(!STerrainAtlasProfile::fromBytes(local.memberPayload(
            CTerrainAtlasProfileStore::embeddedMemberName(map.mapInfo().innerNamespace)),
            &profile, &report))
    { result.error = report.toText(); return result; }
    QVector<QByteArray> pages;
    for(const auto& name : ei::terrainTextureResourceNames(
            map.mapInfo().innerNamespace, int(map.mapInfo().textureCount)))
        pages.append(local.memberPayload(name));
    const auto plan = prepareTerrainAtlasCompact(map, profile, pages, {}, destination,
                                                 nullptr, true);
    if(!plan.error.isEmpty()) { result.error = plan.error; return result; }
    CResArchiveIndex pack;
    if(!pack.open(publicationPath, &result.error))
    { result.error.prepend("Open HDPack " + publicationPath + ": "); return result; }
    const auto beforeHash = resFileSha256(publicationPath, &result.error);
    if(beforeHash.isEmpty())
    { result.error.prepend("Hash HDPack " + publicationPath + ": "); return result; }
    CResArchiveTransaction transaction(pack);
    for(const auto& member : plan.members) {
        const int index = pack.memberIndex(member.name);
        if(index >= 0 && pack.readMember(index) == member.payload) continue;
        // Hash identity collisions must never overwrite another map's artwork.
        if(index != -1 || !transaction.add(member.name, member.payload,
                                           member.lastWriteTime, &result.error)) {
            if(result.error.isEmpty()) result.error = "Terrain identity collision: " + member.name;
            return result;
        }
    }
    QTemporaryDir staging(QDir::tempPath()+"/new-map-atlas-XXXXXX");
    if(!staging.isValid()) { result.error = "Cannot create atlas staging directory."; return result; }
    const auto stagedPath = staging.filePath("candidate.res");
    QByteArray afterHash;
    if(!transaction.writeCandidate(stagedPath, &result.error, {}, &afterHash))
    { result.error.prepend("Stage HDPack " + stagedPath + ": "); return result; }
    const auto binding = CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
        {stagedPath}, plan.profile.mprNamespace);
    if(!binding.success() || binding.profile.toBytes() != plan.profile.toBytes())
    { result.error = "The published atlas candidate failed fresh binding validation."; return result; }
    SMapSaveFile resource;
    resource.path = publicationPath; resource.afterFile = stagedPath;
    resource.beforeSha256 = beforeHash; resource.afterSha256 = afterHash;
    SMapSaveFile mpr;
    mpr.path = destination.absoluteFilePath(); mpr.after = plan.mprBytes;
    mpr.existed = destination.exists();
    if(createOnly && mpr.existed) { result.error = "Map already exists; creation never overwrites"; return result; }
    if(mpr.existed) {
        QFile input(mpr.path);
        if(!input.open(QIODevice::ReadOnly))
        { result.error = "Read destination " + mpr.path + ": " + input.errorString(); return result; }
        mpr.before = input.readAll();
    }
    // Publish the pages before the map becomes visible. A failed map commit
    // restores this cohort's pack; existing user documents remain untouched.
    return commitMapSaveBatch({resource, mpr}, backupRoot);
}
