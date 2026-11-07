// Scenario-only probe, compiled into the real EXE under the existing hidden
// --rebuild64-launch-check harness. No alternate renderer or document logic.
void serviceScenario(const QString& map,bool integrated=false) {
    openPair(map);validateReady(map);
    const float width=view->m_pLand->widthInWorldUnits(),height=view->m_pLand->heightInWorldUnits();float x=width/2.f,y=height/2.f;
    for(const auto& placement:view->m_neighborPlacements)if(CNeighborMapLayout::touchesActiveMap({width,height},placement)){
        x=qBound(0.f,float(placement.origin.x()+placement.size.width()/2),width);y=qBound(0.f,float(placement.origin.y()+placement.size.height()/2),height);break;}
    QMatrix4x4 camera;camera.lookAt({x,y-1,180},{x,y,0},{0,1,0});view->m_cam->adoptView(camera);native->tick();
    auto* surface=native->findChild<QWidget*>("rebuild64NativeSurface");check(surface,"native input surface");
    const auto mouse=[&](QEvent::Type type,const QPoint& p,Qt::MouseButton button,Qt::MouseButtons buttons,QWidget* target=nullptr){QMouseEvent e(type,p,button,buttons,Qt::NoModifier);QApplication::sendEvent(target?target:surface,&e);};
    mouse(QEvent::MouseMove,surface->rect().center(),Qt::NoButton,Qt::NoButton);
    check(wait([&]{return !native->m_queryInFlight&&!view->m_nativeQueryPixels.isEmpty();}),"native stencil/depth query finishes while Qt runs");
    check(native->rebuildSelected(),"native query keeps Rebuild64");
    const auto covered=[&](const QPoint& p){for(const auto& panel:native->m_panels)if(panel&&panel->isVisible()&&panel->geometry().contains(p))return true;return false;};
    QHash<quint64,CNode*> nodes;uint32_t ordinal=0;for(auto* mob:view->mobs())if(view->isMobVisible(mob)){for(auto* n:mob->nodes())if(n&&n->nodeType()==eWorldObject&&n->modelName().compare("stst8",Qt::CaseInsensitive))nodes.insert((quint64(ordinal)<<32)|n->mapId(),n);++ordinal;}
    QPoint object(-1,-1),ground(-1,-1),neighbour(-1,-1);CNode* target=nullptr;
    const auto size=view->m_nativeQuerySize;
    for(int py=96;py<size.height()-120;++py)for(int px=64;px<size.width()-64;++px){const QPoint p(px*native->width()/size.width(),py*native->height()/size.height());if(covered(p))continue;const auto& sample=view->m_nativeQueryPixels[py*size.width()+px];
        if(!target&&sample.mob!=UINT32_MAX){auto* n=nodes.value((quint64(sample.mob)<<32)|sample.nid);if(n&&view->ownerMob(n)==view->m_activeMob){target=n;object=p;}}
        if(ground.x()<0&&!sample.renderer_id&&sample.terrain_depth>0&&sample.terrain_depth<1){QVector3D world;if(view->nativeTerrainPositionAt(p,&world,true)&&world.x()>0&&world.x()<width&&world.y()>0&&world.y()<height)ground=p;}
        if(neighbour.x()<0&&sample.mob==UINT32_MAX&&sample.nid&&sample.renderer_id)neighbour=p;
    }
    check(target&&object.x()>=0,"native ID maps to an active MOB object");check(ground.x()>=0,"native terrain depth has a terrain pixel");check(neighbour.x()>=0,"native ID includes a neighbour MOB object");
    record("native_query_points",{{"object",QJsonArray{object.x(),object.y()}},{"nid",int(target->mapId())},{"ground",QJsonArray{ground.x(),ground.y()}},{"neighbour",QJsonArray{neighbour.x(),neighbour.y()}},{"planes",5},{"shaderChanges",0}});
    const auto click=[&](const QPoint& point){mouse(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);};
    click(object);check(view->selectionSnapshot().contains(target),"actual Qt click selects the native object ID");
    QVector3D position;check(view->nativeTerrainPositionAt(ground,&position,true),"native depth unprojects terrain under Qt pointer");
    click(ground);check(view->selectionSnapshot().isEmpty(),"actual Qt terrain click clears object selection");
    click(neighbour);check(view->property("nativeNeighborPickId").toUInt()>0&&view->selectionSnapshot().isEmpty(),"actual Qt neighbour click reads native nid and preserves readonly background");
    click(object);const auto before=target->position();const int undo=window->m_undoStack->index();
    key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");
    check(view->hasPendingOperation(),"native gizmo uses shared pending operation");
    check(wait([&]{return native->statistics().service_serial>0;}),"worker service submitted");
    const auto capture=[&](const QString& name){const auto path=caseRoot+"/"+map+"-"+name+".png";bool done=false;QString failure;auto connection=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& p,const QString& e,quint64){if(p==path){done=true;failure=e;}});native->capture(path);check(wait([&]{return done;})&&failure.isEmpty(),"native own frame: "+name);disconnect(connection);};
    capture("gizmo");key(Qt::Key_Escape);check(target->position()==before&&window->m_undoStack->index()==undo,"gizmo cancellation retains document/Undo");
    view->setScenarioOverlay({position},{position+QVector3D(5,0,0)},{position+QVector3D(0,5,0)},{position,position+QVector3D(5,0,0)});
    view->m_cameraTrackLines={position,position+QVector3D(0,7,3)};view->setTileGridVisible(true);
    view->setModelMagnetEnabled(true);check(native->rebuildSelected(),"magnet stays in native view");
    view->m_aiGraphPreviewWidget->show();if(view->m_aiGraphPreviewBody->isHidden())view->m_aiGraphPreviewCollapse->click();
    if(!view->m_aiGraphPreviewVisible)view->m_aiGraphPreviewToggle->click();native->tick();
    check(wait([&]{return view->m_aiGraphPreviewWidget->parentWidget()==surface;}),"AI panel is a native child above the view");
    check(view->m_aiGraphDocument.isValid(),"real MOB/.grp graph remains loaded");
    capture("ai-scenario-cam");
    auto* panel=view->navmeshPanel();check(panel,"native Navmesh panel");
    auto* source=view->m_aiGraphPreviewWidget->findChild<QComboBox*>("navigationSource");check(source,"real navigation source selector");source->setCurrentIndex(1);
    // A synthetic immutable drawing fixture isolates renderer transfer from
    // navbake acceptance. It is positioned on this real zone's native depth.
    auto& navdoc=const_cast<SNav64Document&>(panel->document());navdoc={};navdoc.valid=true;navdoc.header.profile="human";navdoc.tileCount=1;navdoc.polygonCount=1;
    SNav64RenderTile tile;tile.minimum=position;tile.maximum=position+QVector3D(15,15,1);tile.areas.append({{position+QVector3D(0,0,.3f),position+QVector3D(15,0,.3f),position+QVector3D(0,15,.3f)},{.2f,.85f,.4f,.55f}});tile.edges={tile.areas[0].vertices[0],tile.areas[0].vertices[1],tile.areas[0].vertices[1],tile.areas[0].vertices[2],tile.areas[0].vertices[2],tile.areas[0].vertices[0]};navdoc.renderTiles.append(tile);
    panel->setLayerVisible(true);
    check(wait([&]{native->tick();return view->m_navmeshDrawnVertices>0&&panel->layerVisible();},5000),"Navmesh immutable overlay reaches native worker");
    if(auto* legend=panel->findChild<QCheckBox*>("navmeshLegend"))if(!legend->isChecked())legend->click();
    capture("navmesh");
    view->setDiplomacyMapMode(true);check(native->rebuildSelected(),"diplomacy output stays native");
    CUnit* npc=nullptr;for(auto* node:view->m_activeMob->nodes())if(!npc&&node->nodeState()!=eHidden)npc=dynamic_cast<CUnit*>(node);check(npc,"real diplomacy NPC exists");
    check(view->focusDiplomacyNpc(view->m_activeMob->filePath().absoluteFilePath(),"node:"+QString::number(npc->innerId()))&&native->rebuildSelected(),"diplomacy NPC focus/selection stays native");
    check(wait([&]{native->movePanels(true);return view->m_diplomacyOverlay&&view->m_diplomacyOverlay->parentWidget()==surface;}),"late diplomacy panel is reparented above native view");
    const auto selection=view->selectionSnapshot();mouse(QEvent::MouseButtonPress,QPoint(5,5),Qt::LeftButton,Qt::LeftButton,view->m_aiGraphPreviewWidget);mouse(QEvent::MouseButtonRelease,QPoint(5,5),Qt::LeftButton,Qt::NoButton,view->m_aiGraphPreviewWidget);check(selection==view->selectionSnapshot(),"real Qt panel events do not click through");
    check(wait([&]{native->movePanels(true);return native->m_nativeLegend&&native->m_nativeLegend->isVisible();}),"native legend is a real Qt panel");
    for(const auto& widget:native->m_panels)if(widget&&widget!=native->m_nativeLegend&&widget->isVisible())check(!widget->geometry().intersects(native->m_nativeLegend->geometry()),"legend does not overlap "+widget->objectName());
    capture("navmesh-diplomacy");QImage combined(caseRoot+"/"+map+"-navmesh-diplomacy.png");QPainter painter(&combined);painter.scale(combined.width()/double(native->width()),combined.height()/double(native->height()));
    for(const auto& widget:native->m_panels)if(widget&&widget->isVisible()){QImage image(widget->size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);widget->render(&image);painter.drawImage(widget->pos(),image);}painter.end();check(combined.save(caseRoot+"/"+map+"-panels-own-composite.png"),"own native readback plus actual Qt panel rasters");
    view->setDiplomacyMapMode(false);panel->setLayerVisible(false);view->setScenarioOverlay({},{},{},{});view->m_cameraTrackLines.clear();view->setTileGridVisible(false);
    if(integrated) {
        view->clearObjectSelection();
        const auto snapshot=operation(request("window_snapshot"));check(!snapshot.contains("error")&&snapshot["captureKind"].toString().startsWith("Rebuild64"),"289 combined native288 MCP snapshot");
        QString error;const auto image=nativeOutput(view,0,QSize(),native->outputCamera(),&error);check(!image.isNull(),"289 combined native288 output: "+error);check(image.save(caseRoot+"/"+map+"-output288.png"),"save combined native output");
        CNode* model=nullptr;for(auto* node:view->m_activeMob->nodes())if(!model&&node->nodeType()==eWorldObject&&node->modelName().compare("stst8",Qt::CaseInsensitive))model=node;
        check(model,"289 native model source");
        {CPreview preview(nullptr,view);preview.setAttribute(Qt::WA_DontShowOnScreen);preview.resize(480,360);preview.attachView(view);preview.attachNode(model);preview.show();previewCheck(&preview,map+"-model288");preview.close();preview.attachNode(nullptr,false);}
        check(native->rebuildSelected(),"289 snapshot/model close retain Rebuild64");auditOpenResources();
    } else {native->m_glAction->trigger();validateReady(map+"-gl",true);check(!native->rebuildSelected(),"explicit OpenGL reserve remains functional");}
    window->closeAll();
}
