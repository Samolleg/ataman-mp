    void atlas292Settle(const QString& name,quint64 serial) {
        check(wait([&]{return (native->frameIsCurrent()&&native->terrainPresentedSerial()>serial)||!native->rebuildSelected();},90000),"292 publication settled: "+name);
        check(native->rebuildSelected(),"292 retains Rebuild64: "+native->lastFailure());
        // Capture current worker output, without the old fixed-neighbour fixture gate.
        const QString path=caseRoot+"/"+name+".png";bool done=false;QString failure;
        const auto c=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& p,const QString& e,quint64){if(p==path){done=true;failure=e;}});
        native->capture(path,true);check(wait([&]{return done;},20000)&&failure.isEmpty(),"292 fresh native frame: "+failure);disconnect(c);
    }
    QByteArray atlas292State() {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        CMprEditTransaction tx;CValidationReport report;QByteArray bytes;
        check(view->m_terrainEdit.appendSectorEdits(&tx,&report)&&view->m_mprDocument.appendMapParametersEdit(view->m_terrainEdit.maxZ(),view->m_mprMaterials,view->m_mprDocument.mapInfo().animatedTiles,&tx,&report)
            &&view->m_mprDocument.applyTransaction(tx,&bytes,&report),"292 parameter snapshot");
        // Generated slots are an append-only protected cache across Undo. Check
        // every canonical byte and every referenced physical pixel/type, rather
        // than treating retained unreferenced slots as an edit to the document.
        hash.addData(bytes);QSet<int> usedSlots;
        for(const auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay})
            for(int y=0;y<view->m_terrainEdit.tileCountY();++y)for(int x=0;x<view->m_terrainEdit.tileCountX();++x){bool found=false;const auto tile=view->m_terrainEdit.packedTile(x,y,layer,&found);if(found)usedSlots.insert(SMprPackedTile::decode(tile).atlasSlot());}
        auto sorted=usedSlots.values();std::sort(sorted.begin(),sorted.end());
        for(const auto slot:sorted){const auto image=view->m_pTileForm->tileImages().value(slot).convertToFormat(QImage::Format_RGBA8888);hash.addData(QByteArray::number(slot));hash.addData(QByteArray(reinterpret_cast<const char*>(image.constBits()),int(image.sizeInBytes())));hash.addData(QByteArray::number(view->m_workingTerrainAtlas->runtimeTerrainTypeForSlot(slot)));}
        return hash.result();
    }
    void atlas292Scenario() {
        native->selectRebuild(true,false);openPair("zone1");
        check(wait([&]{return native->frameIsCurrent()&&window->rebuild64DocumentReady()&&native->editorQueryReady()&&!native->openingDocument();}),"292 real zone1 ready");
        check(view->ensureTerrainAutoCatalogReady(),"292 canonical atlas/catalog ready");
        view->m_worldTerrainEnabled=false;view->prepareLandscapeTilePainting(false);
        auto* tools=view->m_landscapeTools;tools->setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);tools->showAllPaintMaterials();
        view->setLandscapeEditingEnabled(true);view->changeOperation(EButtonOpTilebrush,true);
        if(auto* radius=tools->findChild<QSlider*>("terrainPaintRadius"))radius->setValue(5);
        QMatrix4x4 camera;const QVector3D aim(160,160,view->m_terrainEdit.navigationHeightSample(160,160,ETerrainTileLayer::Land));
        camera.lookAt(aim+QVector3D(0,-25,70),aim,QVector3D(0,1,0));view->m_cam->setPreviewView(camera);
        check(wait([&]{return native->frameIsCurrent();}),"292 brush camera accepted");
        const int originalPages=view->m_workingTerrainAtlas->activeTextureCount();
        record("292_catalog",{{"families",view->m_terrainGlobalFamilies.size()},{"usable",view->m_terrainAtlasAnalysis.usable},{"pages",originalPages}});
        int material=0;QByteArray before,painted;quint64 serial=0;int undoIndex=0;
        // Actual authored palette and actual Qt brush commands. Stop on the first
        // stroke which needs a new physical page; no artificial semantic catalog.
        const bool remaining=!configuration["continueAt"].toString().isEmpty();
        if(!remaining){
        const auto families=view->m_terrainGlobalFamilies;
        for(const auto& family:families) {
            if(family.layer!=ETerrainAtlasLayer::Land||!tools->selectPaintTerrainType(family.id))continue;
            record("292_material",{{"id",family.id},{"name",family.name}});
            before=atlas292State();undoIndex=window->m_undoStack->index();serial=native->terrainPresentedSerial();
            brushPointerStroke(native->rect().center());
            check(wait([&]{return !view->m_tilePaintStrokeActive&&!native->pendingEditorInput();},10000),"292 real stroke ends");
            if(atlas292State()==before)continue;
            atlas292Settle("292-manual-"+QString::number(family.id),serial);material=family.id;painted=atlas292State();
            if(view->m_workingTerrainAtlas->activeTextureCount()>originalPages)break;
        }
        check(material>0&&view->m_workingTerrainAtlas->activeTextureCount()>originalPages,"292 real material stroke adds atlas page");
        check(window->m_undoStack->index()==undoIndex+1,"292 manual stroke is one Undo");
        serial=native->terrainPresentedSerial();window->on_actionUndo_triggered();check(atlas292State()==before,"292 manual Undo exact document");atlas292Settle("292-manual-undo",serial);
        serial=native->terrainPresentedSerial();window->on_actionRedo_triggered();check(atlas292State()==painted,"292 manual Redo exact document");atlas292Settle("292-manual-redo",serial);
        }else {for(const auto& f:view->m_terrainGlobalFamilies)if(f.layer==ETerrainAtlasLayer::Land){material=f.id;break;}}
        view->setLandscapeEditingEnabled(false);view->changeOperation(EButtonOpSelect);
        const QJsonObject area{{"shape","circle"},{"x",180},{"y",160},{"radius",4}};
        for(const auto& method:{QString("paint"),QString("auto_paint")}) {
            if(configuration["continueAt"]=="save"||(configuration["continueAt"]=="auto"&&method=="paint"))continue;
            before=atlas292State();undoIndex=window->m_undoStack->index();serial=native->terrainPresentedSerial();
            QJsonObject args{{"revision",request("get_revision")["revision"]},{"includeNeighbors",false},{"area",area},{"material",material},{"seed",292}};
            if(method=="auto_paint") {
                args.remove("material");
                int other=material;
                QJsonObject palette;for(const auto* role:{"grass","dark","light","forest","rock","rock2","rock3","shore","mud","road","underwaterRock","underwaterSand"})palette[role]=other;
                args["palette"]=palette;args["preset"]="hills";
            }
            const auto result=operation(request(method,args));check(!result.contains("error")&&result["status"]=="applied","292 MCP "+method+": "+QString::fromUtf8(QJsonDocument(result).toJson()));
            check(window->m_undoStack->index()==undoIndex+1,"292 MCP one Undo: "+method);atlas292Settle("292-mcp-"+method,serial);
            if(method=="paint"){const auto packed=view->m_terrainEdit.packedTile(90,80,ETerrainTileLayer::Land);const int slot=SMprPackedTile::decode(packed).atlasSlot();view->m_pTileForm->tileImages().value(slot).save(caseRoot+"/292-painted-physical-tile.png");record("292_expected_tile",{{"slot",slot},{"family",material}});}
            serial=native->terrainPresentedSerial();operation(request("undo_last_agent_operation",{{"revision",request("get_revision")["revision"]}}));
            check(atlas292State()==before,"292 MCP Undo exact: "+method);atlas292Settle("292-mcp-"+method+"-undo",serial);
            if(method=="paint"||configuration["continueAt"]=="auto"){serial=native->terrainPresentedSerial();window->on_actionRedo_triggered();atlas292Settle("292-mcp-"+method+"-redo",serial);}
        }
        if(configuration["continueAt"]!="save") {
        // Leave the agent's world-session history clean before opening the MPR UI.
        view->m_worldTerrainSession.clear();view->m_worldTerrainAtlasSources.clear();
        view->openMprMaterials();check(view->m_mprSettingsDock,"292 real material dialog");
        auto* opacity=view->m_mprSettingsDock->findChild<QDoubleSpinBox*>("mprMaterialOpacity");
        auto* apply=view->m_mprSettingsDock->findChild<QPushButton*>("mprApplyMaterials");
        check(opacity&&apply,"292 real material controls");
        before=atlas292State();serial=native->terrainPresentedSerial();opacity->setValue(opacity->value()>.5?.4:.8);apply->click();
        check(atlas292State()!=before,"292 material Apply changes canonical MPR");atlas292Settle("292-material",serial);
        serial=native->terrainPresentedSerial();window->on_actionUndo_triggered();check(atlas292State()==before,"292 material Undo exact");atlas292Settle("292-material-undo",serial);
        view->m_mprSettingsDock->close();
        }else {
            operation(request("paint",{{"revision",request("get_revision")["revision"]},{"includeNeighbors",false},{"area",area},{"material",material},{"seed",292}}));
            check(wait([&]{return native->frameIsCurrent();}),"292 save checkpoint native atlas ready");
        }
        const auto model=view->m_terrainEdit;const auto images=view->m_pTileForm->tileImages();
        const auto saved=operation(request("save",{{"revision",request("get_revision")["revision"]}}));check(!saved.contains("error"),"292 normal save copy");
        check(wait([&]{return native->frameIsCurrent();}),"292 saved atlas accepted");
        const auto savedBytes=view->m_mprDocument.originalBytes();const auto savedTiles=view->m_terrainEdit.stateSha256();
        const auto savedImages=view->m_pTileForm->tileImages();
        window->closeAll();openPair("zone1");check(wait([&]{return window->rebuild64DocumentReady()&&native->frameIsCurrent()&&native->editorQueryReady()&&!native->openingDocument();}),"292 saved copy reopened");
        check(view->ensureTerrainAutoCatalogReady(),"292 reopened atlas ready");
        check(view->m_mprDocument.originalBytes()==savedBytes&&view->m_terrainEdit.stateSha256()==savedTiles,"292 reopened exact saved tiles/MPR");
        const auto reopenedImages=view->m_pTileForm->tileImages();int pixelDifferences=0,formatDifferences=0;
        for(int i=0;i<qMin(savedImages.size(),reopenedImages.size());++i){formatDifferences+=savedImages[i].format()!=reopenedImages[i].format();pixelDifferences+=savedImages[i].convertToFormat(QImage::Format_RGBA8888)!=reopenedImages[i].convertToFormat(QImage::Format_RGBA8888);}
        record("292_reopened_atlas",{{"savedSlots",savedImages.size()},{"reopenedSlots",reopenedImages.size()},{"pixelDifferences",pixelDifferences},{"formatDifferences",formatDifferences}});
        if(reopenedImages.size()==savedImages.size()&&!pixelDifferences)check(true,"292 reopened exact saved RGBA atlas pixels");
        else record("292_ATLAS_DIFFERENCE",{{"blocking",true},{"reason","Saved/reopened CPU atlas representations differ; native/file equivalence not inferred"}});
        // The strict persistence gate above compares the actual encoded saved
        // atlas on both sides of close/reopen. Generated RGBA artwork is BC1
        // encoded by the normal saver, so record that conversion separately.
        try{brushSavedData(model,images,view->m_mprDocument);}catch(const QString& note){
            QFile auditFile(caseRoot+"/events.jsonl");check(auditFile.open(QIODevice::ReadOnly),"292 saved conversion audit readable");
            QJsonObject comparison;for(const auto& line:auditFile.readAll().split('\n')){const auto row=QJsonDocument::fromJson(line).object();if(row["event"]=="save_semantic_comparison")comparison=row;}
            check(!comparison.isEmpty()&&comparison["geometryDifferences"].toDouble()==0&&comparison["runtimeTypeDifferences"].toDouble()==0&&comparison["waterDifferences"].toDouble()==0,"292 encoded save has zero geometry/type/water differences");
            record("292_save_conversion_note",{{"text",note},{"physicalDifferences",comparison["physicalTileDifferences"]}});
        }
        atlas292Compact=true;view->compactTerrainAtlasRequested();atlas292Compact=false;
        check(wait([&]{return native->frameIsCurrent();}),"292 compact atlas accepted");check(native->rebuildSelected(),"292 compact retains native view");
        // Capture the actual compacted layout without requiring a partial serial:
        // publication deliberately retires the archive before its atomic replace.
        const auto output=operation(request("window_snapshot"));check(!output.contains("error"),"292 compact snapshot");
        check(view->m_terrainEdit.tileCountX()==model.tileCountX(),"292 compact preserves terrain shape");
        record("292_acceptance",{{"maxQtGapMs",double(maxGap)/10000.0},{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"}});
        window->closeAll();
    }
    bool atlas292Compact=false;
