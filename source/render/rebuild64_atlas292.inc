    // Immutable values only. BC1 encoding runs on the native worker, never GUI.
    struct AtlasSource {
        QSharedPointer<CTerrainWorkingAtlas> atlas;
        STerrainAtlasProfile profile;
        QVector<STerrainGlobalMaterialFamily> families;
        QVector<QByteArray> authored;
        QVector<SMprMaterialRecord> materials;
        QVector<SMprAnimatedTileRecord> animations;
    };
    static std::shared_ptr<AtlasSource> atlasSource(CView* view) {
        if(!view->m_workingTerrainAtlas||!view->m_workingTerrainAtlas->isValid())return {};
        auto result=std::make_shared<AtlasSource>();
        result->atlas=view->m_workingTerrainAtlas->createStagingCopy();
        result->profile=view->m_terrainAtlasProfile;result->families=view->m_terrainGlobalFamilies;
        result->materials=view->m_mprMaterials;result->animations=view->m_mprAnimatedTiles;
        for(const auto& name:ei::terrainTextureResourceNames(view->m_mprDocument.mapInfo().innerNamespace,int(view->m_mprDocument.mapInfo().textureCount))) {
            QByteArray bytes;QString path;
            if(!CTextureList::getInstance()->terrainMmpPayload(name,&bytes,&path))return {};
            result->authored.append(bytes);
        }
        return result;
    }
    static bool atlasPackage(QByteArray& terrain,const std::shared_ptr<AtlasSource>& source,
        QVector<QByteArray>& names,QVector<QByteArray>& pages,QByteArray& profile,QString& error) {
        if(!source)return true;
        CValidationReport report;CMprDocument document;
        if(!document.loadBytes(terrain,"native-atlas.mpr",&report)){error=report.toText();return false;}
        const auto& atlas=*source->atlas;
        const auto package=CTerrainVirtualAtlas::buildWorkingPreviewPackage(document,atlas.activeTileImages(),
            atlas.activeTileTypes(),source->authored,source->materials,source->animations,&report,atlas.preservedDxtTiles());
        if(!package.success){error=package.error+report.toText();return false;}
        CResArchiveDocument archive;QStringList errors;
        if(!archive.loadBytes(package.resourceArchiveBytes,"native-atlas.res",&errors)){error=errors.join("\n");return false;}
        for(const auto& name:package.resourceMembers){names.append(name.toUtf8());pages.append(archive.memberPayload(name));}
        const auto semantics=CTerrainAtlasProfileStore::workingAtlasProfile(document.mapInfo().innerNamespace,
            atlas.activeTileImages(),atlas.catalog(),source->profile,source->families,&report);
        if(semantics.assignedSlotCount()>0&&!report.hasErrors())profile=semantics.toBytes();
        terrain=package.mprBytes;return true;
    }
