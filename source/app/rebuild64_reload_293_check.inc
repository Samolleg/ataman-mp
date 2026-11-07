//293-B one sequential real-EXE scenario; no synthetic document substitutions.
void r2BeginGesture(const QString& phase) {
    if(!configuration["r2Run"].toBool())return;
    maxGap=0;lastPulse=qpc();qApp->setProperty("r2GesturePhase",phase);record("r2_phase_begin",{{"phase",phase}});
}
void r2EndGesture() {
    if(!configuration["r2Run"].toBool())return;
    maxGap=qMax(maxGap,qpc()-lastPulse);
    record("r2_phase_end",{{"phase",qApp->property("r2GesturePhase").toString()},{"maxQtGapMs",double(maxGap)/10000.0}});
    qApp->setProperty("r2GesturePhase",QString());
}
void reloadFrame293(const QString& name) {
    check(wait([&]{return native->frameIsCurrent()&&!native->pendingEditorInput()&&!native->m_queryInFlight;}),"293 current native scene: "+name);
    check(native->rebuildSelected(),"293 no fallback: "+native->lastFailure());
    check(wait([&]{return !native->statistics().neighbours_pending&&native->statistics().neighbours_accepted==5;}),"293 five owner neighbours: "+name);
    const auto path=caseRoot+"/"+name+".png";bool done=false;QString error;
    auto c=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& p,const QString& e,quint64){if(p==path){done=true;error=e;}});
    native->capture(path,false);check(wait([&]{return done;},30000)&&error.isEmpty(),"293 native capture "+name+": "+error);disconnect(c);
    record("reload293_frame",{{"name",name},{"generation",QString::number(native->statistics().generation)},{"terrainSerial",QString::number(native->terrainPresentedSerial())},{"neighbours",int(native->statistics().neighbours_accepted)}});
}
void reloadAim293(const QVector3D& aim,float distance) {
    QMatrix4x4 camera;camera.lookAt(aim+QVector3D(0,-distance*.25f,distance),aim,QVector3D(0,1,0));view->m_cam->adoptView(camera);
    QElapsedTimer elapsed;elapsed.start();check(wait([&]{return elapsed.elapsed()>250;}),"293 camera settled");
}
QByteArray reloadNeighbor293(const QString& key) {
    const auto doc=view->m_worldTerrainSession.documents.value(key);check(bool(doc),"293 retained neighbour exists");
    CMprEditTransaction tx;CValidationReport report;QByteArray bytes;
    check(doc->terrain.appendSectorEdits(&tx,&report)&&doc->baseline.appendMapParametersEdit(doc->terrain.maxZ(),doc->materials,doc->baseline.mapInfo().animatedTiles,&tx,&report)&&doc->baseline.applyTransaction(tx,&bytes,&report),"293 neighbour snapshot "+report.toText());return bytes;
}
void reloadScenario293() {
    native->selectRebuild(true,false);openPair("zone1");validateReady("zone1");
    check(view->ensureTerrainAutoCatalogReady(),"293 canonical terrain ready");
    check(view->m_neighborLandscapesVisible,"293 owner neighbour toggle retained");
    view->prepareLandscapeTilePainting(false);view->setLandscapeEditingEnabled(true);
    auto* tools=view->m_landscapeTools;tools->activateMode(ELandscapeEditMode::Sculpt);tools->setSculptTool(ELandscapeSculptTool::Raise);view->changeOperation(EButtonOpTilebrush,true);
    auto* move=tools->findChild<QCheckBox*>("landscapeMoveMobObjects");check(move,"293 real Move objects checkbox");if(move->isChecked()!=configuration["r2OrdinaryControl"].toBool())move->click();
    auto* radius=tools->findChild<QDoubleSpinBox*>("landscapeSculptRadius");check(radius,"293 real radius");radius->setValue(5);
    if(!configuration["resumeAfterKeep"].toBool()) {
    int mobNotifications=0;auto mobObserver=connect(view,&CView::mobStateChanged,this,[&]{++mobNotifications;});
    CNode* target=nullptr;for(auto* mob:view->m_aMob)for(auto* n:mob->nodes())if(!target&&n->nodeType()==eWorldObject&&!n->isMarkDeleted()&&n->position().x()>16&&n->position().y()>16&&n->position().x()<view->m_pLand->widthInWorldUnits()-16&&n->position().y()<view->m_pLand->heightInWorldUnits()-16)target=n;
    check(target,"293 existing terrain-relative object target");const auto objectPosition=target->position(),objectWorld=target->drawPosition();
    reloadAim293(objectWorld,40);reloadFrame293("zone1-keep-before");
    const auto before=brushSnapshot(),mobsBefore=brushMobs();const auto serial=native->terrainPresentedSerial();const auto undo=window->m_undoStack->index();
    r2BeginGesture(configuration["r2OrdinaryControl"].toBool()?"ordinary290-stroke":"keep-stroke");
    brushPointerStroke(native->rect().center());check(wait([&]{return !view->m_sculptStrokeActive&&!native->pendingEditorInput()&&!native->m_queryInFlight;}),"293 keep stroke replay done");
    check(brushSnapshot()!=before,"293 keep-height Qt sculpt changed MPR");
    if(configuration["r2OrdinaryControl"].toBool())check(target->position()==objectPosition&&target->drawPosition().z()>objectWorld.z(),"R2 ordinary290 objects follow terrain without MOB offset edits");
    else check((target->drawPosition()-objectWorld).length()<.002f&&target->position().z()!=objectPosition.z(),"293 object world height retained by MOB offset");
    check(window->m_undoStack->index()==undo+1,"293 keep-height stroke one Undo");brushSettled("zone1-keep-after",serial);r2EndGesture();
    r2BeginGesture(configuration["r2OrdinaryControl"].toBool()?"ordinary290-undo":"keep-undo");
    window->on_actionUndo_triggered();check(brushSnapshot()==before&&brushMobs()==mobsBefore,"293 keep Undo exact MPR and all MOB bytes");reloadFrame293("zone1-keep-undo");
    check((target->drawPosition()-objectWorld).length()<.002f,"293 Undo object world pose restored");r2EndGesture();
    if(configuration["onlyKeep293"].toBool()&&!configuration["r2OrdinaryControl"].toBool())check(mobNotifications>=2,"293 ordinary MOB observers notified on sculpt and Undo");disconnect(mobObserver);
    }
    if(configuration["onlyKeep293"].toBool()){window->closeAll();return;}
    // The real map-selection dialog enables a cohort; its single history entry
    // must own changes on both sides of the edge.
    QTimer::singleShot(0,this,[&]{for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(w))if(auto* list=d->findChild<QListWidget*>()){
        if(auto* box=d->findChild<QCheckBox*>())if(!box->isChecked())box->click();for(int i=0;i<list->count();++i)list->item(i)->setCheckState(Qt::Checked);
        d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();return;}});
    view->chooseWorldTerrainMaps();check(view->m_worldTerrainEnabled&&!view->m_worldTerrainEnabledPaths.isEmpty(),"293 world maps enabled with real dialog");
    const float width=view->m_pLand->widthInWorldUnits(),height=view->m_pLand->heightInWorldUnits();QString neighbor;QVector3D seam;
    for(const auto& p:view->m_neighborPlacements){const auto key=CWorldTerrainSession::key(p.mprPath);if(!view->m_worldTerrainEnabledPaths.contains(key))continue;
        const double loX=qMax(0.,p.origin.x()),hiX=qMin(double(width),p.origin.x()+p.size.width()),loY=qMax(0.,p.origin.y()),hiY=qMin(double(height),p.origin.y()+p.size.height());
        if(qAbs(p.origin.x()-width)<.01&&hiY-loY>24){seam=QVector3D(width,float((loY+hiY)/2),0);neighbor=key;break;}
        if(qAbs(p.origin.y()-height)<.01&&hiX-loX>24){seam=QVector3D(float((loX+hiX)/2),height,0);neighbor=key;break;}
        if(qAbs(p.origin.x()+p.size.width())<.01&&hiY-loY>24){seam=QVector3D(0,float((loY+hiY)/2),0);neighbor=key;break;}
        if(qAbs(p.origin.y()+p.size.height())<.01&&hiX-loX>24){seam=QVector3D(float((loX+hiX)/2),0,0);neighbor=key;break;}}
    check(!neighbor.isEmpty(),"293 real touching MPR edge");QString err;check(view->m_worldTerrainSession.load(neighbor,&err),"293 neighbor canonical model: "+err);
    seam.setZ(view->m_terrainEdit.terrainSurfaceHeight(qBound(0.f,seam.x(),width-.01f),qBound(0.f,seam.y(),height-.01f)));
    radius->setValue(8);reloadAim293(seam,60);reloadFrame293("zone1-seam-before");
    const auto worldBefore=brushSnapshot(),neighborBefore=reloadNeighbor293(neighbor),mobBeforeWorld=brushMobs();const auto worldUndo=window->m_undoStack->index();
    r2BeginGesture("seam-stroke");
    brushPointerStroke(native->rect().center());check(wait([&]{return !view->m_sculptStrokeActive&&!native->pendingEditorInput()&&!native->m_queryInFlight;}),"293 edge stroke replay done");
    check(brushSnapshot()!=worldBefore&&reloadNeighbor293(neighbor)!=neighborBefore,"293 Qt stroke changed both MPR documents");
    check(window->m_undoStack->index()==worldUndo+1,"293 entire multi-MPR stroke is one Undo");reloadFrame293("zone1-seam-after");r2EndGesture();
    r2BeginGesture("seam-undo");
    window->on_actionUndo_triggered();check(brushSnapshot()==worldBefore&&reloadNeighbor293(neighbor)==neighborBefore&&brushMobs()==mobBeforeWorld,"293 edge Undo exact both MPRs and MOB cohort");reloadFrame293("zone1-seam-undo");r2EndGesture();
    if(configuration["r2Gestures"].toBool()){window->closeAll();return;}
    view->m_worldTerrainEnabled=false;view->setLandscapeEditingEnabled(false);view->changeOperation(EButtonOpSelect);
    // MPR settings have no standalone water-level scalar. maxZ is the existing
    // stored scale of both terrain and water; use its actual Qt command.
    view->openMapParameters();auto* settings=view->m_mprSettingsDock.data();check(settings,"293 real MPR settings window");
    auto* tabs=settings->findChild<QTabWidget*>("mprSettingsTabs");check(tabs,"293 actual settings tabs");tabs->setCurrentIndex(4);
    auto* scale=settings->findChild<QDoubleSpinBox*>("mprScaleTarget293");auto* apply=settings->findChild<QPushButton*>("mprApplyScale293");check(scale&&apply,"293 real vertical range controls");
    const auto camera=view->m_cam->viewMatrix();const auto generation=native->statistics().generation;const float oldMaxZ=view->m_terrainEdit.maxZ();
    QVector3D waterSample;bool waterFound=false;float waterBefore=0;
    for(int y=0;y<view->m_terrainEdit.tileCountY()&&!waterFound;++y)for(int x=0;x<view->m_terrainEdit.tileCountX()&&!waterFound;++x){
        const auto slot=view->m_terrainEdit.overlayMaterialIndex(x,y);if(slot<0||slot>=view->m_mprMaterials.size()||view->m_mprMaterials[slot].type<2||view->m_mprMaterials[slot].type>3)continue;
        waterSample=QVector3D(x*2+1,y*2+1,0);waterBefore=view->m_terrainEdit.navigationHeightSample(waterSample.x(),waterSample.y(),ETerrainTileLayer::Overlay,&waterFound);
    }
    check(waterFound,"293 authored water sample found");
    scale->setValue(oldMaxZ*1.001);reloadConfirm293=true;apply->click();reloadConfirm293=false;
    bool waterValid=false;const float waterAfter=view->m_terrainEdit.navigationHeightSample(waterSample.x(),waterSample.y(),ETerrainTileLayer::Overlay,&waterValid);
    check(waterValid&&qAbs(waterAfter-waterBefore)>.00001f,"293 actual water level changed with MPR scale");
    record("reload293_water",{{"before",waterBefore},{"after",waterAfter},{"maxZBefore",oldMaxZ},{"maxZAfter",view->m_terrainEdit.maxZ()}});
    check(view->m_terrainEdit.maxZ()!=oldMaxZ,"293 MPR settings change stored terrain/water scale");
    check(wait([&]{return native->statistics().generation>generation&&native->frameIsCurrent();}),"293 settings reload completed");
    check(view->m_cam->viewMatrix()==camera,"293 MPR reload retained camera");reloadFrame293("zone1-water-settings");settings->close();
    // Save the working copy and reopen it through the normal window entry.
    const auto savedTerrain=view->m_terrainEdit;const auto savedImages=view->m_pTileForm->tileImages();const auto savedMobs=brushMobs();
    operation(request("save",{{"revision",request("get_revision")["revision"]}}));
    CMprDocument saved;CValidationReport report;check(saved.load(QFileInfo(fixture()+"/Maps/zone1.mpr"),&report),"293 saved copy decodes");brushSavedData(savedTerrain,savedImages,saved);
    const auto published=brushSnapshot();window->closeAll();openPair("zone1");validateReady("zone1-reopen");check(view->ensureTerrainAutoCatalogReady(),"293 reopened CPU terrain");
    check(brushSnapshot()==published&&brushMobs()==savedMobs,"293 reopened exact published MPR/MOB");
    // Enlarge only a private copy, keeping the real file name and adjacent maps.
    view->openMapParameters();settings=view->m_mprSettingsDock.data();tabs=settings->findChild<QTabWidget*>("mprSettingsTabs");tabs->setCurrentIndex(5);
    const auto oldSectors=view->m_mprDocument.mapInfo().sectorCountX;const auto resizeCamera=view->m_cam->viewMatrix();
    auto* sx=settings->findChild<QSpinBox*>("mprCanvasWidth");check(sx,"293 real canvas width");sx->setValue(oldSectors+1);
    reloadConfirm293=true;settings->findChild<QPushButton*>("applyCurrentMprCanvas")->click();reloadConfirm293=false;
    check(view->m_mprDocument.mapInfo().sectorCountX==oldSectors+1,"293 real canvas command enlarged copy");
    check(view->m_cam->viewMatrix()==resizeCamera,"293 canvas retained camera");settings->close();reloadFrame293("zone1-resize");
    const auto resized=brushSnapshot();window->closeAll();openPair("zone1");validateReady("zone1-resize-reopen");check(view->ensureTerrainAutoCatalogReady()&&brushSnapshot()==resized,"293 resized copy saved and reopened exact");
    reloadFrame293("zone1-resize-reopened");auditOpenResources();window->closeAll();
    record("reload293_complete",{{"maxQtGapMs",double(maxGap)/10000.0},{"renderer","Rebuild64"},{"noFallback",native->rebuildSelected()}});
}
bool reloadConfirm293=false;
