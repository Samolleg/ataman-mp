// One sequential staging-copy acceptance, real Qt events and real MCP server.
void close292Begin(const QString& name) {
    maxGap=0;lastPulse=qpc();close292Phase=name;
    record("292_phase_begin",{{"operation",name}});
}
void close292End() {
    maxGap=qMax(maxGap,qpc()-lastPulse);
    const double gap=double(maxGap)/10000.0;
    record("292_pause",{{"operation",close292Phase},{"maxQtGapMs",gap},
        {"visualBuilds",double(view->property("nativeVisualBuilds292").toULongLong())},
        {"cacheHits",double(view->property("nativeVisualHits292").toULongLong())}});
    if(gap>1000&&close292Phase!="save"&&close292Phase!="reopen")close292Blocked=true;
    close292Phase.clear();
}
void close292Structural() {
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
    close292Begin(configuration["r2OrdinaryControl"].toBool()?"ordinary290-stroke":"keep-stroke");
    brushPointerStroke(native->rect().center());check(wait([&]{return !view->m_sculptStrokeActive&&!native->pendingEditorInput()&&!native->m_queryInFlight;}),"293 keep stroke replay done");
    check(brushSnapshot()!=before,"293 keep-height Qt sculpt changed MPR");
    if(configuration["r2OrdinaryControl"].toBool())check(target->position()==objectPosition&&target->drawPosition().z()>objectWorld.z(),"R2 ordinary290 objects follow terrain without MOB offset edits");
    else check((target->drawPosition()-objectWorld).length()<.002f&&target->position().z()!=objectPosition.z(),"293 object world height retained by MOB offset");
    check(window->m_undoStack->index()==undo+1,"293 keep-height stroke one Undo");brushSettled("zone1-keep-after",serial);close292End();
    close292Begin(configuration["r2OrdinaryControl"].toBool()?"ordinary290-undo":"keep-undo");
    window->on_actionUndo_triggered();check(brushSnapshot()==before&&brushMobs()==mobsBefore,"293 keep Undo exact MPR and all MOB bytes");reloadFrame293("zone1-keep-undo");
    check((target->drawPosition()-objectWorld).length()<.002f,"293 Undo object world pose restored");close292End();
    if(configuration["onlyKeep293"].toBool()&&!configuration["r2OrdinaryControl"].toBool())check(mobNotifications>=2,"293 ordinary MOB observers notified on sculpt and Undo");disconnect(mobObserver);
    }
    if(configuration["onlyKeep293"].toBool()){window->closeAll();return;}
    // The real map-selection dialog enables a cohort; its single history entry
    // must own changes on both sides of the edge.
    QTimer::singleShot(0,this,[&]{for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<QDialog*>(w))if(d->windowTitle()==CView::tr("World terrain maps"))if(auto* list=d->findChild<QListWidget*>()){
        if(auto* box=d->findChild<QCheckBox*>())if(!box->isChecked())box->click();for(int i=0;i<list->count();++i)list->item(i)->setCheckState(Qt::Checked);
        if(auto* buttons=d->findChild<QDialogButtonBox*>())if(auto* ok=buttons->button(QDialogButtonBox::Ok)){ok->click();return;}}});
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
    close292Begin("seam-stroke");
    brushPointerStroke(native->rect().center());check(wait([&]{return !view->m_sculptStrokeActive&&!native->pendingEditorInput()&&!native->m_queryInFlight;}),"293 edge stroke replay done");
    check(brushSnapshot()!=worldBefore&&reloadNeighbor293(neighbor)!=neighborBefore,"293 Qt stroke changed both MPR documents");
    check(window->m_undoStack->index()==worldUndo+1,"293 entire multi-MPR stroke is one Undo");reloadFrame293("zone1-seam-after");close292End();
    close292Begin("seam-undo");
    window->on_actionUndo_triggered();check(brushSnapshot()==worldBefore&&reloadNeighbor293(neighbor)==neighborBefore&&brushMobs()==mobBeforeWorld,"293 edge Undo exact both MPRs and MOB cohort");reloadFrame293("zone1-seam-undo");close292End();

}
void close292Scenario() {
    native->selectRebuild(true,false);
    if(!configuration["close292Continue"].toBool())close292Begin("opening");openPair("zone1");
    check(wait([&]{return native->frameIsCurrent()&&window->rebuild64DocumentReady()&&native->editorQueryReady()&&!native->openingDocument()&&!view->property("nativeUnitsPending289").toBool();}),"292 opening with complete cached appearance");
    check(wait([&]{return native->statistics().neighbours_pending==0&&native->statistics().neighbours_accepted==5;}),"292 five real neighbours ready");if(!configuration["close292Continue"].toBool())close292End();
    reloadFrame293("292-open");
    check(view->ensureTerrainAutoCatalogReady(),"292 canonical terrain ready");
    if(!configuration["close292Continue"].toBool()) {
    view->m_worldTerrainEnabled=false;view->prepareLandscapeTilePainting(false);
    auto* tools=view->m_landscapeTools;tools->setPaintPaletteMode(ETerrainPaintPaletteMode::Modern);tools->showAllPaintMaterials();
    view->setLandscapeEditingEnabled(true);view->changeOperation(EButtonOpTilebrush,true);
    if(auto* radius=tools->findChild<QSlider*>("terrainPaintRadius"))radius->setValue(5);
    const QVector3D aim(160,160,view->m_terrainEdit.navigationHeightSample(160,160,ETerrainTileLayer::Land));
    reloadAim293(aim,70);
    int material=-1;for(const auto& f:view->m_terrainGlobalFamilies)if(f.name=="Astral"&&f.layer==ETerrainAtlasLayer::Land)material=f.id;
    check(material>0&&tools->selectPaintTerrainType(material),"292 real authored Astral material");
    auto before=atlas292State();int history=window->m_undoStack->index();auto serial=native->terrainPresentedSerial();const int pages=view->m_workingTerrainAtlas->activeTextureCount();
    close292Begin("paint-new-page");brushPointerStroke(native->rect().center());
    check(wait([&]{return !view->m_tilePaintStrokeActive&&!native->pendingEditorInput();}),"292 real paint gesture ended");
    check(atlas292State()!=before&&view->m_workingTerrainAtlas->activeTextureCount()>pages,"292 real brush added atlas page");
    check(window->m_undoStack->index()==history+1,"292 paint one Undo");atlas292Settle("292-paint",serial);close292End();
    close292Begin("paint-undo");serial=native->terrainPresentedSerial();window->on_actionUndo_triggered();check(atlas292State()==before,"292 exact atlas/terrain Undo");atlas292Settle("292-paint-undo",serial);close292End();
    view->setLandscapeEditingEnabled(false);view->changeOperation(EButtonOpSelect);
    before=atlas292State();history=window->m_undoStack->index();serial=native->terrainPresentedSerial();
    close292Begin("mcp-paint");
    operation(request("paint",{{"revision",request("get_revision")["revision"]},{"includeNeighbors",false},{"area",QJsonObject{{"shape","circle"},{"x",180},{"y",160},{"radius",4}}},{"material",material},{"seed",292}}));
    check(atlas292State()!=before&&window->m_undoStack->index()==history+1,"292 MCP paint changed data with one Undo");atlas292Settle("292-mcp-paint",serial);close292End();
    close292Begin("mcp-paint-undo");serial=native->terrainPresentedSerial();operation(request("undo_last_agent_operation",{{"revision",request("get_revision")["revision"]}}));check(atlas292State()==before,"292 MCP paint exact Undo");atlas292Settle("292-mcp-paint-undo",serial);close292End();
    }
    if(!configuration["close292UnitsOnly"].toBool())close292Structural();
    view->m_worldTerrainEnabled=false;view->setLandscapeEditingEnabled(false);view->changeOperation(EButtonOpSelect);
    CUnit* unit=nullptr;for(auto* node:view->m_activeMob->nodes())if(auto* candidate=dynamic_cast<CUnit*>(node))if(!unit||candidate->renderedModelName().startsWith("unhum",Qt::CaseInsensitive))unit=candidate;
    check(unit,"292 real unit");const auto nid=unit->mapId();const auto original=unit->position();const auto mobsBefore=brushMobs();
    const auto appearanceBuilds=view->property("nativeVisualBuilds292").toULongLong();
    reloadAim293(unit->drawPosition()+QVector3D(0,0,1),10);reloadFrame293("292-unit-before");
    close292Begin("mcp-edit-units");operation(request("edit_units",{{"revision",request("get_revision")["revision"]},{"action","transform"},{"ids",QJsonArray{double(nid)}},{"delta",QJsonArray{.25,0,0}},{"layer",3}}));
    check(qAbs(unit->position().x()-original.x()-.25)<.001f,"292 MCP moved actual unit");reloadFrame293("292-mcp-unit");close292End();
    close292Begin("mcp-edit-units-undo");operation(request("undo_last_agent_operation",{{"revision",request("get_revision")["revision"]}}));
    check(brushMobs()==mobsBefore,"292 unit MCP exact Undo");reloadFrame293("292-mcp-unit-undo");close292End();
    check(view->property("nativeVisualBuilds292").toULongLong()==appearanceBuilds,"292 transform snapshots reused every unchanged appearance");
    if(configuration["close292UnitsOnly"].toBool()){
        auditOpenResources();window->closeAll();record("292_focused_units",{{"qtBlocked",close292Blocked},{"unchangedAppearanceReused",true}});return;
    }
    view->restoreSelection({unit});close292Begin("unit-move");key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");key(Qt::Key_Return,"\r");
    check(qAbs(unit->position().x()-original.x()-1)<.001f,"292 real Qt unit move committed");reloadFrame293("292-unit-moved");close292End();
    const auto model=view->m_terrainEdit;const auto images=view->m_pTileForm->tileImages();const auto savedMobs=brushMobs();
    close292Begin("save");operation(request("save",{{"revision",request("get_revision")["revision"]}}));
    check(wait([&]{return native->frameIsCurrent()&&!view->property("nativeUnitsPending289").toBool();}),"292 saved native scene current");close292End();
    const auto savedBytes=view->m_mprDocument.originalBytes();const auto savedImages=view->m_pTileForm->tileImages();
    close292Begin("reopen");window->closeAll();openPair("zone1");
    check(wait([&]{return native->frameIsCurrent()&&window->rebuild64DocumentReady()&&native->editorQueryReady()&&!native->openingDocument()&&!view->property("nativeUnitsPending289").toBool();}),"292 saved copy reopened");close292End();
    check(view->ensureTerrainAutoCatalogReady()&&view->m_mprDocument.originalBytes()==savedBytes&&brushMobs()==savedMobs,"292 reopened exact MPR/MOB");
    const auto reopened=view->m_pTileForm->tileImages();check(savedImages.size()==reopened.size(),"292 reopened atlas size");
    for(int i=0;i<savedImages.size();++i)check(savedImages[i].convertToFormat(QImage::Format_RGBA8888)==reopened[i].convertToFormat(QImage::Format_RGBA8888),"292 reopened atlas pixels "+QString::number(i));
    reloadFrame293("292-reopened");auditOpenResources();window->closeAll();
    record("292_complete",{{"qtBlocked",close292Blocked},{"renderer","Rebuild64"},{"oneSequentialRun",true}});
}
QString close292Phase;bool close292Blocked=false;
