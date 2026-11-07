    QByteArray brushMobs() {
        QByteArray result;for(auto* mob:view->m_aMob){QByteArray bytes;mob->serializeMob(bytes,false);result+=QByteArray::number(bytes.size())+":"+bytes;}return result;
    }
    QByteArray brushSnapshot() {
        CMprEditTransaction transaction;CValidationReport report;QByteArray bytes;
        check(view->m_terrainEdit.appendSectorEdits(&transaction,&report)
              &&view->m_mprDocument.appendMapParametersEdit(view->m_terrainEdit.maxZ(),view->m_mprMaterials,view->m_mprAnimatedTiles,&transaction,&report)
              &&view->m_mprDocument.applyTransaction(transaction,&bytes,&report),"canonical brush MPR snapshot: "+report.toText());
        return bytes;
    }
    void brushSavedData(const CTerrainEditModel& before,const QVector<QImage>& oldImages,const CMprDocument& reopened) {
        CValidationReport report;CTerrainEditModel after;check(after.load(reopened,&report),"saved terrain model decodes");
        const auto newImages=view->m_pTileForm->tileImages();QMap<quint16,QByteArray> oldKeys,newKeys;
        const auto key=[](quint16 value,const QVector<QImage>& images,QMap<quint16,QByteArray>& cache){
            if(cache.contains(value))return cache[value];const auto packed=SMprPackedTile::decode(value);
            if(packed.atlasSlot()>=images.size())return QByteArray();
            const auto image=images[packed.atlasSlot()].transformed(QTransform().rotate(packed.rotation*90)).convertToFormat(QImage::Format_RGBA8888);
            return cache[value]=QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(image.constBits()),int(image.sizeInBytes())),QCryptographicHash::Sha256);};
        qint64 geometry=0,tiles=0,types=0,water=0,renumbered=0;
        check(before.vertexCountX()==after.vertexCountX()&&before.vertexCountY()==after.vertexCountY()&&before.maxZ()==after.maxZ(),"save preserves terrain dimensions and maxZ");
        for(int y=0;y<before.vertexCountY();++y)for(int x=0;x<before.vertexCountX();++x){
            geometry+=before.height(x,y)!=after.height(x,y)||before.terrainVertexOffset(x,y)!=after.terrainVertexOffset(x,y);
            bool a=false,b=false;const float za=before.navigationHeightSample(x*2,y*2,ETerrainTileLayer::Overlay,&a),zb=after.navigationHeightSample(x*2,y*2,ETerrainTileLayer::Overlay,&b);
            water+=a!=b||(a&&za!=zb);
        }
        for(auto layer:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay})for(int y=0;y<before.tileCountY();++y)for(int x=0;x<before.tileCountX();++x){
            bool a=false,b=false;const auto pa=before.packedTile(x,y,layer,&a),pb=after.packedTile(x,y,layer,&b);
            if(a!=b){++tiles;continue;}if(!a)continue;renumbered+=pa!=pb;
            const auto ka=key(pa,oldImages,oldKeys),kb=key(pb,newImages,newKeys);tiles+=ka.isEmpty()||ka!=kb;
            types+=before.tileType(pa)!=after.tileType(pb);
            if(layer==ETerrainTileLayer::Overlay)water+=before.overlayMaterialIndex(x,y)!=after.overlayMaterialIndex(x,y);
        }
        record("save_semantic_comparison",{{"geometryDifferences",double(geometry)},{"physicalTileDifferences",double(tiles)},{"runtimeTypeDifferences",double(types)},{"waterDifferences",double(water)},{"renumberedPackedTiles",double(renumbered)}});
        check(!geometry&&!tiles&&!types&&!water,"normal save preserves geometry, rotated tile pixels/types and overlay surface despite atlas renumbering");
    }
    void brushSettled(const QString& name,quint64 before) {
        check(wait([&]{return (native->frameIsCurrent()&&native->terrainPresentedSerial()>before)||!native->rebuildSelected();},30000),"brush Present completed: "+name);
        check(native->rebuildSelected(),"brush retained Rebuild64: "+native->lastFailure());
        validateReady(name);
    }
    void brushPointerStroke(const QPoint& point) {
        auto* surface=native->findChild<QWidget*>("rebuild64NativeSurface");check(surface,"real native Qt input surface");
        QMouseEvent hover(QEvent::MouseMove,point,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(surface,&hover);
        QMouseEvent press(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);auto begin=qpc();QApplication::sendEvent(surface,&press);record("brush_qt_input",{{"phase","press"},{"ticks",QString::number(qpc()-begin)}});
        QElapsedTimer stroke;stroke.start();wait([&]{return stroke.elapsed()>60;},2000);
        QMouseEvent move(QEvent::MouseMove,point+QPoint(12,0),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);begin=qpc();QApplication::sendEvent(surface,&move);record("brush_qt_input",{{"phase","move"},{"ticks",QString::number(qpc()-begin)}});
        QMouseEvent release(QEvent::MouseButtonRelease,point+QPoint(12,0),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);begin=qpc();QApplication::sendEvent(surface,&release);record("brush_qt_input",{{"phase","release"},{"ticks",QString::number(qpc()-begin)}});
    }
    void brushScenario(const QString& map) {
        check(view->ensureTerrainAutoCatalogReady(),"canonical brush and authored tile catalogue ready");
        view->m_worldTerrainEnabled=false; // ordinary single-map brush; boundary keeps native welded neighbours
        const QVector3D center(160,160,view->m_terrainEdit.maxZ()/2);
        QMatrix4x4 camera;camera.lookAt(center+QVector3D(0,-90,220),center,QVector3D(0,1,0));view->m_cam->adoptView(camera);
        QElapsedTimer settle;settle.start();wait([&]{return settle.elapsed()>200;},2000);
        auto* tools=view->m_landscapeTools;check(tools,"real landscape tools");
        if(configuration["mode"]=="acceptBrush290")brush290Controls(map);
        if(configuration["mode"]=="acceptBrush290")brush290PrepareDepth();
        view->setLandscapeEditingEnabled(true);tools->activateMode(ELandscapeEditMode::Sculpt);tools->setSculptTool(ELandscapeSculptTool::Raise);
        if(auto* radius=tools->findChild<QDoubleSpinBox*>("landscapeSculptRadius"))radius->setValue(4);
        const auto initial=brushSnapshot(),initialMobs=brushMobs();const auto generation=native->statistics().generation;
        auto serial=native->terrainPresentedSerial();const int undoBefore=window->m_undoStack->index();
        brushPointerStroke(native->rect().center());
        check(brushSnapshot()!=initial,"Qt height stroke changed canonical MPR");
        brushSettled(map+"-height",serial);
        check(native->statistics().generation==generation,"height retained world generation");
        if(configuration["mode"]=="acceptBrush290")brush290Selection(map,initial);
        const auto raised=brushSnapshot();serial=native->terrainPresentedSerial();
        window->on_actionUndo_triggered();check(brushSnapshot()==initial,"height Undo exact original MPR bytes");brushSettled(map+"-height-undo",serial);
        check(brushMobs()==initialMobs,"height Undo exact MOB cohort bytes");
        check(window->m_undoStack->index()==undoBefore,"height Undo stack restored");
        serial=native->terrainPresentedSerial();window->on_actionRedo_triggered();check(brushSnapshot()==raised,"height Redo exact MPR bytes");brushSettled(map+"-height-redo",serial);
        view->prepareLandscapeTilePainting(false);tools->setPaintPaletteMode(ETerrainPaintPaletteMode::Manual);
        int selected=-1;for(int slot=0;slot<view->activeTerrainAtlasSlotCount();++slot)if(tools->selectManualAtlasTile(slot)){selected=slot;break;}
        check(selected>=0,"authored manual atlas tile selected");check(view->m_pTileForm->selectTile(selected),"canonical tile picker selected");
        serial=native->terrainPresentedSerial();const auto beforePaint=brushSnapshot();brushPointerStroke(native->rect().center()+QPoint(28,0));
        // If the first authored tile already occupies the hit area, choose another authored tile.
        if(brushSnapshot()==beforePaint){check(tools->selectManualAtlasTile(selected+1)&&view->m_pTileForm->selectTile(selected+1),"second authored tile selected");brushPointerStroke(native->rect().center()+QPoint(28,0));}
        check(brushSnapshot()!=beforePaint,"Qt paint stroke changed canonical MPR");brushSettled(map+"-paint",serial);
        const auto painted=brushSnapshot();serial=native->terrainPresentedSerial();window->on_actionUndo_triggered();check(brushSnapshot()==beforePaint,"paint Undo exact MPR bytes");brushSettled(map+"-paint-undo",serial);
        serial=native->terrainPresentedSerial();window->on_actionRedo_triggered();check(brushSnapshot()==painted,"paint Redo exact MPR bytes");brushSettled(map+"-paint-redo",serial);
        if(configuration["mode"]=="acceptBrush290")brush290Water(map);
        brushFinishScenario(map);
    }
    void brushFinishScenario(const QString& map) {
        auto* tools=view->m_landscapeTools;auto serial=native->terrainPresentedSerial();
        tools->setEditingEnabled(false);
        const QJsonObject area{{"shape","circle"},{"x",160},{"y",160},{"radius",3}};
        serial=native->terrainPresentedSerial();
        operation(request("sculpt",{{"revision",request("get_revision")["revision"]},{"area",area},{"action","raise"},{"amount",0.2},{"strength",1.0},{"includeNeighbors",false}}));
        brushSettled(map+"-mcp-sculpt",serial);
        serial=native->terrainPresentedSerial();
        const auto beforeMcpPaint=brushSnapshot();
        const auto refused=request("paint",{{"revision",request("get_revision")["revision"]},{"area",area},{"material",1}});
        check(refused["code"]=="REQUIRES_OPENGL","unported MCP semantic paint retains its explicit GL requirement");
        check(brushSnapshot()==beforeMcpPaint&&native->rebuildSelected(),"refused MCP paint preserves data and selected view");
        record("UNPORTED",{{"check","MCP semantic paint"},{"route","existing REQUIRES_OPENGL"},{"catalogueUsable",view->m_terrainAtlasAnalysis.usable},{"globalFamilies",view->m_terrainGlobalFamilies.size()},{"activeNamespace",view->m_mprDocument.mapInfo().innerNamespace}});
        const auto autosaveBefore=brushSnapshot();window->performAutosave();
        check(wait([&]{return !window->m_mprAutosaveWatcher;},30000),"production asynchronous autosave completed");
        check(!CBackupManager::recoverableAutosaves().isEmpty(),"production recovery file exists in isolated folder");
        check(brushSnapshot()==autosaveBefore,"autosave preserves edit data");
        if(configuration["mode"]=="acceptBrush290")brush290Outputs(map);
        const auto savedBytes=brushSnapshot();const auto savedModel=view->m_terrainEdit;const auto savedImages=view->m_pTileForm->tileImages();const QString destination=fixture()+"/Maps/"+map+".mpr";
        {QFile beforeFile(caseRoot+"/before-save.mpr");check(beforeFile.open(QIODevice::WriteOnly)&&beforeFile.write(savedBytes)==savedBytes.size(),"pre-save bytes retained for audit");}
        operation(request("save",{{"revision",request("get_revision")["revision"]}}));
        check(QFileInfo::exists(destination),"real cohort save of owned MPR copy");
        CMprDocument expected,reopened;CValidationReport report;
        check(expected.loadBytes(savedBytes,"expected.mpr",&report)&&reopened.load(QFileInfo(destination),&report),"saved copy reopened with real decoder");
        brushSavedData(savedModel,savedImages,reopened);
        const auto publishedBytes=brushSnapshot();
        native->m_glAction->trigger();validateReady(map+"-switch",true);
        window->closeAll();check(!view->m_pLand->isMprLoad(),"edited document closed after save copy");
        openPair(map);validateReady(map+"-reopened",true);
        check(view->ensureTerrainAutoCatalogReady(),"reopened canonical terrain ready");
        check(view->m_terrainEdit.vertexCountX()==int(expected.mapInfo().sectorCountX)*32+1,"reopened dimensions match");
        check(brushSnapshot()==publishedBytes,"reopened MPR exact published canonical bytes");
        window->closeAll();
    }
