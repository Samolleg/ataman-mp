// Acceptance290: real EXE, ordinary Qt commands, native queries and output288.
QVector<EirPickPixel> brush290BeforeDepth;
QSize brush290BeforeSize;
QMatrix4x4 brush290BeforeClip;
void brush290PrepareDepth() {
    view->setLandscapeEditingEnabled(false);view->changeOperation(EButtonOpSelect);
    auto* surface=native->findChild<QWidget*>("rebuild64NativeSurface");QMouseEvent e(QEvent::MouseMove,surface->rect().center(),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(surface,&e);
    check(wait([&]{return !native->m_queryInFlight&&!view->m_nativeQueryPixels.isEmpty();}),"290 native depth reference before stroke");
    brush290BeforeDepth=view->m_nativeQueryPixels;brush290BeforeSize=view->m_nativeQuerySize;brush290BeforeClip=view->m_nativeQueryClip;
    view->setLandscapeEditingEnabled(true);view->changeOperation(EButtonOpTilebrush,true);
}
void brush290Capture(const QString& name) {
    const auto path=caseRoot+"/"+name+".png";bool done=false;QString failure;auto c=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& p,const QString& e,quint64){if(p==path){failure=e;done=true;}});native->capture(path);check(wait([&]{return done;})&&failure.isEmpty(),"290 capture "+name+": "+failure);disconnect(c);
}
void brush290Controls(const QString& map) {
    native->m_diagnosticFrame=map=="zone1"?300:0;
    if(map!="zone1")return;
    const auto baseline=brushSnapshot();view->prepareLandscapeTilePainting(false);view->m_landscapeTools->setPaintPaletteMode(ETerrainPaintPaletteMode::Manual);view->setLandscapeEditingEnabled(true);view->changeOperation(EButtonOpTilebrush,true);
    for(int slot=0;slot<view->activeTerrainAtlasSlotCount();++slot)if(view->m_landscapeTools->selectManualAtlasTile(slot)&&view->m_pTileForm->selectTile(slot))break;
    auto* surface=native->findChild<QWidget*>("rebuild64NativeSurface");QMouseEvent e(QEvent::MouseMove,surface->rect().center()+QPoint(28,0),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(surface,&e);
    native->tick();
    brush290Capture(map+"-unedited-paint-clock300");view->m_landscapeTools->activateMode(ELandscapeEditMode::Sculpt);native->tick();brush290Capture(map+"-unedited-sculpt-clock300");
    check(brushSnapshot()==baseline,"290 time/camera control made no document edits");record("water_diagonal_control",{{"frame",300},{"worldTime",100},{"edited",false},{"camera","same as subsequent height/paint"}});
}
void brush290Selection(const QString& map,const QByteArray& baseline) {
    const auto terrainBefore=brushSnapshot();const int historyBefore=window->m_undoStack->index();
    view->setLandscapeEditingEnabled(false);view->changeOperation(EButtonOpSelect);
    auto* surface=native->findChild<QWidget*>("rebuild64NativeSurface");
    const auto mouse=[&](QEvent::Type type,QPoint p,Qt::MouseButton b,Qt::MouseButtons bs){QMouseEvent e(type,p,b,bs,Qt::NoModifier);QApplication::sendEvent(surface,&e);};
    mouse(QEvent::MouseMove,surface->rect().center(),Qt::NoButton,Qt::NoButton);
    check(wait([&]{return !native->m_queryInFlight&&!view->m_nativeQueryPixels.isEmpty();}),"290 fresh ID/depth after height stroke");
    CMprDocument doc;CTerrainEditModel old;CValidationReport report;check(doc.loadBytes(baseline,"before-stroke.mpr",&report)&&old.load(doc,&report),"290 before-stroke reference decodes");
    QHash<quint64,CNode*> nodes;uint32_t ordinal=0;for(auto* mob:view->mobs())if(view->isMobVisible(mob)){for(auto* n:mob->nodes())if(n&&n->nodeType()==eWorldObject&&n->modelName().compare("stst8",Qt::CaseInsensitive))nodes.insert((quint64(ordinal)<<32)|n->mapId(),n);++ordinal;}
    QPoint object(-1,-1),ground(-1,-1);CNode* target=nullptr;QVector3D hit;float delta=0,error=0,changeError=0,gpuDelta=0;const auto extent=view->m_nativeQuerySize;
    check(extent==brush290BeforeSize&&view->m_nativeQueryClip==brush290BeforeClip,"290 depth comparison uses identical camera and target");
    for(int y=32;y<extent.height()-32;++y)for(int x=32;x<extent.width()-32;++x){
        const QPoint p(x*native->width()/extent.width(),y*native->height()/extent.height());bool covered=false;for(const auto& panel:native->m_panels)covered|=panel&&panel->isVisible()&&panel->geometry().contains(p);if(covered)continue;
        const auto& sample=view->m_nativeQueryPixels[y*extent.width()+x];
        if(!target&&sample.mob!=UINT32_MAX){auto* n=nodes.value((quint64(sample.mob)<<32)|sample.nid);if(n&&view->ownerMob(n)==view->m_activeMob){target=n;object=p;}}
        if(!sample.renderer_id&&sample.terrain_depth>0&&sample.terrain_depth<1){QVector3D world;if(!view->nativeTerrainPositionAt(p,&world,true))continue;bool a=false,b=false;
            const float now=view->m_terrainEdit.terrainSurfaceHeight(world.x(),world.y(),&a),before=old.terrainSurfaceHeight(world.x(),world.y(),&b);
            if(a&&b&&qAbs(now-before)>qMax(.001f,qAbs(delta))){
                const auto& previous=brush290BeforeDepth[y*extent.width()+x];const auto v=brush290BeforeClip.inverted()*QVector4D(2.f*(x+.5f)/extent.width()-1,1-2.f*(y+.5f)/extent.height(),2*previous.terrain_depth-1,1);const auto oldWorld=v.toVector3DAffine();bool oldOk=false;const float oldHeight=old.terrainSurfaceHeight(oldWorld.x(),oldWorld.y(),&oldOk);if(!oldOk)continue;
                ground=p;hit=world;delta=now-before;error=qAbs(world.z()-now);gpuDelta=world.z()-oldWorld.z();changeError=qAbs((world.z()-now)-(oldWorld.z()-oldHeight));}}
    }
    record("brush290_depth",{{"map",map},{"heightDelta",delta},{"absoluteCpuDepthError",error},{"depthChangeError",changeError},{"gpuHeightDelta",gpuDelta},{"terrainSerial",QString::number(native->terrainPresentedSerial())}});
    check(target&&object.x()>=0,"290 visible native object ID after stroke");check(ground.x()>=0&&gpuDelta>.0001f&&changeError<.08f,"290 native depth changes with terrain relative to same-camera baseline");
    const auto click=[&](QPoint p){mouse(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);};
    click(object);check(wait([&]{return view->selectionSnapshot().contains(target);},5000),"290 real object click after stroke");
    click(ground);check(wait([&]{return view->selectionSnapshot().isEmpty();},5000),"290 real ground click after stroke");
    click(object);check(wait([&]{return view->selectionSnapshot().contains(target);},5000),"290 object selected for gizmo");
    const auto before=target->position();const auto index=window->m_undoStack->index();key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");check(view->hasPendingOperation(),"290 shared gizmo starts after terrain update");
    const auto path=caseRoot+"/"+map+"-gizmo290.png";bool done=false;auto connection=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& p,const QString& e,quint64){if(p==path){done=e.isEmpty();}});native->capture(path);check(wait([&]{return done;}),"290 gizmo rendered through worker service");disconnect(connection);
    key(Qt::Key_Escape);check(target->position()==before&&window->m_undoStack->index()==index,"290 gizmo cancellation preserves data and Undo");view->clearObjectSelection();
    view->setLandscapeEditingEnabled(true);view->m_landscapeTools->activateMode(ELandscapeEditMode::Sculpt);view->changeOperation(EButtonOpTilebrush,true);
    // Selection itself is undoable. Restore these entries before testing the
    // preceding height command, without bypassing the ordinary history path.
    QJsonArray selectionHistory;
    while(window->m_undoStack->index()>historyBefore){const int at=window->m_undoStack->index();selectionHistory.append(window->m_undoStack->command(at-1)->text());window->on_actionUndo_triggered();check(window->m_undoStack->index()==at-1&&brushSnapshot()==terrainBefore,"290 selection history Undo preserves terrain");}
    record("brush290_selection_history",{{"map",map},{"commands",selectionHistory}});
}
void brush290Water(const QString& map) {
    if(map!="zone7")return;
    view->setLandscapeEditingEnabled(false);view->changeOperation(EButtonOpSelect);auto* surface=native->findChild<QWidget*>("rebuild64NativeSurface");
    QMouseEvent hover(QEvent::MouseMove,surface->rect().center(),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(surface,&hover);check(wait([&]{return !native->m_queryInFlight&&!view->m_nativeQueryPixels.isEmpty();}),"290 water native query ready");
    view->setLandscapeEditingEnabled(true);view->m_landscapeTools->activateMode(ELandscapeEditMode::OverlaySculpt);view->m_landscapeTools->setSculptTool(ELandscapeSculptTool::Raise);view->changeOperation(EButtonOpTilebrush,true);QCoreApplication::processEvents();
    QPoint point(-1,-1);const auto size=view->m_nativeQuerySize;for(int y=size.height()/3;y<2*size.height()/3&&point.x()<0;y+=3)for(int x=size.width()/3;x<2*size.width()/3;x+=3){const auto& p=view->m_nativeQueryPixels[y*size.width()+x];if(!p.renderer_id&&p.overlay_depth>0&&p.overlay_depth<1){const QPoint candidate(x*native->width()/size.width(),y*native->height()/size.height());bool covered=false;for(const auto& panel:native->m_panels)covered|=panel&&panel->isVisible()&&panel->geometry().adjusted(-16,-16,16,16).contains(candidate);if(covered)continue;bool hit=false;view->getTerrainBrushPos(candidate.x(),candidate.y(),&hit,true);if(hit&&!view->m_terrainPaintProjectionLand){point=candidate;break;}}}
    check(point.x()>=0,"290 real water/overlay pixel selected");const auto before=brushSnapshot();const auto serial=native->terrainPresentedSerial();validateReady(map+"-water-before");
    brushPointerStroke(point);
    check(brushSnapshot()!=before,"290 normal Qt overlay raise changes MPR");brushSettled(map+"-water-after",serial);
    const auto undoSerial=native->terrainPresentedSerial();window->on_actionUndo_triggered();check(brushSnapshot()==before,"290 water Undo exact MPR bytes");brushSettled(map+"-water-undo",undoSerial);view->m_landscapeTools->activateMode(ELandscapeEditMode::TilePaint);
}
void brush290Outputs(const QString& map) {
    view->setLandscapeEditingEnabled(false);view->changeOperation(EButtonOpSelect);view->clearObjectSelection();
    const auto snapshot=operation(request("window_snapshot"));check(snapshot["captureKind"].toString().startsWith("Rebuild64"),"290 MCP snapshot retains output288");
    QString error;const auto image=nativeOutput(view,0,QSize(),native->outputCamera(),&error);check(!image.isNull()&&image.save(caseRoot+"/"+map+"-output290.png"),"290 accepted native output: "+error);
    CNode* model=nullptr;for(auto* node:view->m_activeMob->nodes())if(!model&&node->nodeType()==eWorldObject&&node->modelName().compare("stst8",Qt::CaseInsensitive))model=node;check(model,"290 preview model source");
    {CPreview preview(nullptr,view);preview.setAttribute(Qt::WA_DontShowOnScreen);preview.resize(480,360);preview.attachView(view);preview.attachNode(model);preview.show();previewCheck(&preview,map+"-model290");preview.close();preview.attachNode(nullptr,false);}
    check(native->rebuildSelected(),"290 model preview close retains native view");
}
