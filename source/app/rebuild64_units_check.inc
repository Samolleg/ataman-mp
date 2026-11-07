// Focused real-EXE check. Invoked only by the isolated launch harness.
void unitsReady289() {
    check(wait([&]{return !native->lastFailure().isEmpty() ||
        (window->rebuild64DocumentReady()&&!native->m_documentRefreshPending&&!native->openingDocument()&&native->frameIsCurrent()&&!view->property("nativeUnitsPending289").toBool());},600000),"units289 native and CPU generation ready");
    check(native->rebuildSelected()&&native->lastFailure().isEmpty(),"units289 stays Rebuild64: "+native->lastFailure());
}
void unitsCapture289(const QString& name) {
    unitsReady289();const auto path=caseRoot+"/"+name+".png";bool done=false;QString error;
    const auto connection=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& file,const QString& text,quint64){if(file==path){done=true;error=text;}});
    native->capture(path,false);check(wait([&]{return done;},30000)&&error.isEmpty(),"units289 native world readback: "+name+error);disconnect(connection);
}
void unitsMouse289(QEvent::Type type,const QPoint& point,Qt::MouseButton button=Qt::LeftButton) {
    auto* target=native->findChild<QWidget*>("rebuild64NativeSurface");
    QMouseEvent event(type,point,button,type==QEvent::MouseButtonRelease?Qt::NoButton:button,Qt::NoModifier);
    QApplication::sendEvent(target,&event);
}
void unitsServiceScenario289(CUnit* unit,const QString& map) {
    CScene::getInstance()->changeMode(eEditModeObjects);
    QSet<int> kinds;
    const auto existing=view->m_activeMob->nodes();
    for(auto* source:existing) {
        const auto type=source->nodeType();
        if(type!=eLight&&type!=eSound&&type!=eParticle&&type!=eMagicTrap&&type!=eTorch)continue;
        if(kinds.contains(int(type)))continue;kinds.insert(int(type));
        const auto count=view->m_activeMob->nodes().size();QString reason;QJsonObject descriptor;CValidationReport report;
        check(CMob::prepareFreshTechnicalNode(source,&descriptor,&report),"units289 production technical descriptor: "+report.toText());
        const bool begun=view->beginTechnicalPlacement(descriptor,&reason);
        check(begun,"units289 technical placement begins: "+QString::number(type)+reason);
        unitsMouse289(QEvent::MouseMove,native->rect().center(),Qt::NoButton);
        unitsMouse289(QEvent::MouseButtonPress,native->rect().center());unitsMouse289(QEvent::MouseButtonRelease,native->rect().center());
        check(!view->hasPendingOperation()&&view->m_activeMob->nodes().size()==count+1&&native->rebuildSelected(),"units289 technical Qt placement native: "+QString::number(type));
        window->m_undoStack->undo();check(view->m_activeMob->nodes().size()==count,"units289 technical placement Undo");
    }
    CScene::getInstance()->changeMode(eEditModeLogic);view->setLogicSlotIndex(3);view->restoreSelection({unit});
    const QJsonObject before=unit->toJson();view->addLogicPoint(false);
    check(view->hasPendingOperation(),"units289 patrol placement starts in slot 3");
    unitsMouse289(QEvent::MouseMove,native->rect().center(),Qt::NoButton);
    unitsMouse289(QEvent::MouseButtonPress,native->rect().center());unitsMouse289(QEvent::MouseButtonRelease,native->rect().center());
    check(!view->hasPendingOperation()&&unit->toJson()!=before&&native->rebuildSelected(),"units289 patrol Qt placement stays native");
    view->restoreSelection({unit});view->addLogicPoint(true);check(view->hasPendingOperation(),"units289 look placement starts");
    unitsMouse289(QEvent::MouseButtonPress,native->rect().center()+QPoint(30,10));unitsMouse289(QEvent::MouseButtonRelease,native->rect().center()+QPoint(30,10));
    check(!view->hasPendingOperation()&&native->rebuildSelected(),"units289 look Qt placement stays native");
    unitsCapture289(map+"-logic");window->m_undoStack->undo();window->m_undoStack->undo();
    check(unit->toJson()==before,"units289 patrol and look exact Undo");CScene::getInstance()->changeMode(eEditModeObjects);
    unitsCapture289(map+"-logic-undone");
    view->ensureUnitSupportSurface();int tested=0;
    for(auto* node:view->m_activeMob->nodes())if(auto* npc=dynamic_cast<CUnit*>(node)){
        const auto raw=npc->position();auto expected=CLandscape::getInstance()->unitSurfacePosition(raw);expected.setZ(expected.z()+raw.z());
        if((npc->drawPosition()-expected).length()>=.001f)record("support_mismatch289",{{"nid",int(npc->mapId())},{"raw",QJsonArray{raw.x(),raw.y(),raw.z()}},{"draw",QJsonArray{npc->drawPosition().x(),npc->drawPosition().y(),npc->drawPosition().z()}},{"expected",QJsonArray{expected.x(),expected.y(),expected.z()}}});
        check((npc->drawPosition()-expected).length()<.001f,"units289 native position uses common authored support");++tested;
    }
    record("service289",{{"technicalKinds",kinds.size()},{"supportFaces",view->m_unitSupportSurface.faceCount()},{"positionChecks",tested}});
}
void unitsMergedOutputs291(CUnit* unit,const QString& map) {
    const auto originalCamera=view->m_cam->viewMatrix();const auto originalProjection=view->m_projection;
    if(configuration["resumeAfter"].toString()!="zone1-before-preview"&&configuration["resumeAfter"].toString()!="zone1-before-mcp"||map!="zone1"){
    const auto raw=unit->position();const auto before=unit->drawPosition();
    check(view->ensureTerrainAutoCatalogReady(),"291 canonical brush catalogue ready");
    view->prepareLandscapeTilePainting(false);view->setLandscapeEditingEnabled(true);view->m_worldTerrainEnabled=false;
    view->m_landscapeTools->activateMode(ELandscapeEditMode::Sculpt);view->m_landscapeTools->setSculptTool(ELandscapeSculptTool::Raise);view->changeOperation(EButtonOpTilebrush,true);
    if(auto* radius=view->m_landscapeTools->findChild<QDoubleSpinBox*>("landscapeSculptRadius"))radius->setValue(4);
    const auto aim=before;QMatrix4x4 camera;camera.lookAt(aim+QVector3D(0,-15,45),aim,QVector3D(0,1,0));view->m_cam->adoptView(camera);
    QElapsedTimer settle;settle.start();wait([&]{return settle.elapsed()>200;},2000);unitsReady289();
    const auto terrain=brushSnapshot();const auto serial=native->terrainPresentedSerial();
    brushPointerStroke(native->rect().center());check(brushSnapshot()!=terrain,"291 terrain Qt stroke under character");check(wait([&]{return native->frameIsCurrent()&&native->terrainPresentedSerial()>serial;}),"291 sculpt accepted native Present");
    unitsCapture289(map+"-unit-sculpt");check(unit->position()==raw&&unit->drawPosition().z()>before.z()+.0001f,"291 unit follows changed terrain without MOB drift");
    const auto undoSerial=native->terrainPresentedSerial();window->on_actionUndo_triggered();check(brushSnapshot()==terrain,"291 terrain Undo exact");check(wait([&]{return native->frameIsCurrent()&&native->terrainPresentedSerial()>undoSerial;}),"291 sculpt Undo accepted native Present");
    unitsCapture289(map+"-unit-sculpt-undo");check((unit->drawPosition()-before).length()<.002f,"291 unit pose returns after terrain Undo");
    }else record("resume291",{{"map",map},{"priorSculptPoseUndo","PASS outputs-final"}});
    view->setLandscapeEditingEnabled(false);view->changeOperation(EButtonOpSelect);view->m_cam->adoptView(originalCamera);view->m_projection=originalProjection;view->m_normalProjection=originalProjection;
    const auto snapshot=operation(request("window_snapshot"));check(snapshot["captureKind"].toString().startsWith("Rebuild64"),"291 native MCP snapshot");
    {CPreview preview(nullptr,view);preview.setAttribute(Qt::WA_DontShowOnScreen);preview.resize(480,360);preview.attachView(view);preview.attachNode(unit);preview.show();check(wait([&]{return !preview.nativeImage().isNull();},30000),"291 native unit preview image ready");check(preview.nativeImage().save(caseRoot+"/"+map+"-unit-model291.png"),"291 native unit preview saved");preview.close();preview.attachNode(nullptr,false);}
    view->restoreSelection({unit});window->showUnitTexturePreview();check(native->rebuildSelected(),"291 real texture preview keeps native main view");if(window->m_unitTextureDialog)window->m_unitTextureDialog->close();
    check(native->rebuildSelected(),"291 snapshot and unit model retain Rebuild64");
}
void unitsScenario289(const QString& map) {
    openPair(map);unitsReady289();
    QJsonObject originalValidation;
    if(map=="zone3obr"){originalValidation=operation(request("validate_map"));record("original_validation289",originalValidation);}
    CUnit* unit=nullptr;double best=-1;
    for(auto* node:view->m_activeMob->nodes())if(auto* candidate=dynamic_cast<CUnit*>(node)) {
        if(!unit)unit=candidate;if(!candidate->renderedModelName().startsWith("unhum",Qt::CaseInsensitive))continue;
        double separation=1000000;
        for(auto* other:view->m_activeMob->nodes())if(other->nodeType()==eWorldObject) {
            const auto delta=other->position()-candidate->position();separation=qMin(separation,double(delta.x()*delta.x()+delta.y()*delta.y()));
        }
        if(separation>best){best=separation;unit=candidate;}
    }
    check(unit,"units289 real unit in primary MOB");const auto nid=unit->mapId();
    check(unit->logicCount()==5,"units289 five logical slots");
    const auto plan=unit->nativeVisualPlan();check(!plan["visibleParts"].toArray().isEmpty(),"units289 resolved named appearance");
    record("unit289",{{"nid",int(nid)},{"model",plan["model"]},{"parts",plan["visibleParts"]},{"animation",plan["animation"]}});
    const auto aim=unit->drawPosition()+QVector3D(0,0,1);
    QMatrix4x4 camera;camera.lookAt(aim+QVector3D(3,-4,2.5f),aim,QVector3D(0,0,1));view->m_cam->adoptView(camera);
    view->m_normalProjection.setToIdentity();view->m_normalProjection.perspective(45,float(native->width())/native->height(),.1f,2000);view->m_projection=view->m_normalProjection;
    const auto original=unit->position();const QJsonValue originalLogic=unit->toJson().value("Logics");
    const auto beforeRot=unit->rotation();
    if(configuration["resumeAfter"].toString().isEmpty()||map!="zone1") {
    QElapsedTimer settle;settle.start();wait([&]{return settle.elapsed()>300;},2000);unitsCapture289(map+"-unit-before");
    view->restoreSelection({});unitsMouse289(QEvent::MouseButtonPress,native->rect().center());unitsMouse289(QEvent::MouseButtonRelease,native->rect().center());
    check(wait([&]{return !native->m_queryInFlight&&view->selectionSnapshot().contains(unit);}),"291 common native ID click replay");
    QJsonArray clicked;for(auto* node:view->selectionSnapshot())clicked.append(QJsonObject{{"nid",int(node->mapId())},{"model",node->modelName()}});record("clicked289",{{"expected",int(nid)},{"selection",clicked}});
    check(view->selectionSnapshot().contains(unit),"units289 native Qt click selects the visible character");
    check(native->rebuildSelected(),"units289 selection does not switch to GL");
    key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");key(Qt::Key_Return,"\r");
    check(qAbs(unit->position().x()-original.x()-1)<.001f,"units289 real Qt keyboard move");
    check(unit->toJson()["Logics"]!=originalLogic&&unit->logicCount()==5,"units289 movement carries all five slots");
    const auto movedLogics=unit->toJson()["Logics"].toArray();
    for(int slot=0;slot<5;++slot){const auto a=originalLogic.toArray()[slot].toObject()["Guard placement"].toArray();const auto b=movedLogics[slot].toObject()["Guard placement"].toArray();check(qAbs(b[0].toDouble()-a[0].toDouble()-1)<.001,"units289 guard slot follows Qt movement: "+QString::number(slot));}
    unitsCapture289(map+"-unit-moved");window->m_undoStack->undo();
    check(unit->position()==original&&unit->toJson()["Logics"]==originalLogic,"units289 exact position and five-slot Undo");
    key(Qt::Key_R,"r");key(Qt::Key_Z,"z");key(Qt::Key_1,"1");key(Qt::Key_5,"5");key(Qt::Key_Return,"\r");
    check(unit->rotation()!=beforeRot,"units289 Qt rotation");window->m_undoStack->undo();check(unit->rotation()==beforeRot,"units289 rotation Undo");
    unitsReady289();
    } else record("resume291",{{"map",map},{"from","database placement"},{"priorNativeClickMoveFiveSlotsUndo","PASS case-accept291"}});
    // The actual database form and its button enter the standard placement gesture.
    if((configuration["resumeAfter"].toString()!="zone1-before-stst8"&&configuration["resumeAfter"].toString()!="zone1-before-outputs"&&configuration["resumeAfter"].toString()!="zone1-before-preview"&&configuration["resumeAfter"].toString()!="zone1-before-mcp")||map!="zone1"){
    action("actionCreate_new_object");auto* dialog=window->findChild<CCreateObjectForm*>();check(dialog,"units289 real database form");
    auto* category=dialog->findChild<QComboBox*>("databaseCategory");auto* list=dialog->findChild<QListWidget*>("databaseObjects");
    check(category&&list,"units289 database controls");category->setCurrentIndex(0);
    check(wait([&]{return list->count()>0;}),"units289 creature list populated");
    int rowIndex=0;for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole+1).toString().compare(unit->databaseName(),Qt::CaseInsensitive)==0){rowIndex=i;break;}
    list->setCurrentRow(rowIndex);QCoreApplication::processEvents();const auto count=view->m_activeMob->nodes().size();
    auto* place=dialog->findChild<QPushButton*>("buttonCreate");check(place&&place->isEnabled(),"units289 database Place enabled");place->click();
    check(view->hasPendingOperation(),"units289 database placement gesture started");
    unitsMouse289(QEvent::MouseMove,native->rect().center()+QPoint(120,30),Qt::NoButton);
    unitsMouse289(QEvent::MouseButtonPress,native->rect().center()+QPoint(120,30));unitsMouse289(QEvent::MouseButtonRelease,native->rect().center()+QPoint(120,30));
    check(wait([&]{return !native->m_queryInFlight&&!view->hasPendingOperation()&&view->m_activeMob->nodes().size()==count+1;}),"units289 Qt click confirms database placement");
    check(native->rebuildSelected(),"units289 database form and placement keep native view");unitsCapture289(map+"-unit-added");
    key(Qt::Key_Delete);check(view->m_activeMob->nodes().size()==count,"units289 Qt delete fresh unit");
    window->m_undoStack->undo();check(view->m_activeMob->nodes().size()==count+1,"units289 deletion Undo");
    window->m_undoStack->undo();check(view->m_activeMob->nodes().size()==count,"units289 addition Undo");
    }else record("resume291",{{"map",map},{"from","stst8"},{"priorDbDeleteUndo","PASS remaining-wait"}});
    if(configuration["resumeAfter"].toString()!="zone1-before-outputs"&&configuration["resumeAfter"].toString()!="zone1-before-preview"&&configuration["resumeAfter"].toString()!="zone1-before-mcp"||map!="zone1"){
    CNode* special=nullptr;for(auto* mob:view->mobs())for(auto* node:mob->nodes())if(node->modelName().compare("stst8",Qt::CaseInsensitive)==0){special=node;break;}
    if(special){
        const auto prior=view->m_cam->viewMatrix();const auto aim=special->drawPosition()+special->getBBox().center();QMatrix4x4 camera;record("stst8_bounds291",{{"radius",special->getBBox().radius()},{"drawZ",special->drawPosition().z()},{"centerZ",special->getBBox().center().z()}});camera.lookAt(aim+QVector3D(3,-5,3).normalized()*qMax(8.f,special->getBBox().radius()*3.f),aim,QVector3D(0,0,1));view->m_cam->adoptView(camera);
        record("stst8_source291",{{"source",special->toJson()},{"nativeFragmentClick","unconfirmed: no visible fragments in diagnostic camera"}});
        view->restoreSelection({special});check(native->rebuildSelected(),"291 stst8 document selection stays native");
        const auto before=special->position();key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");key(Qt::Key_Return,"\r");
        check(special->position()!=before,"units289 stst8 Qt move");window->m_undoStack->undo();check(special->position()==before,"units289 stst8 Undo");view->m_cam->adoptView(prior);}
    view->restoreSelection({unit});view->setDiplomacyMapMode(true);view->setDiplomacyAnchor(unit);
    check(native->rebuildSelected(),"units289 diplomacy and anchor keep native view");unitsCapture289(map+"-diplomacy");view->setDiplomacyMapMode(false);
    int technical=0;for(auto* mob:view->mobs())for(auto* node:mob->nodes())if(node->nodeType()==eLight||node->nodeType()==eSound||node->nodeType()==eParticle||node->nodeType()==eMagicTrap||node->nodeType()==eTorch){view->restoreSelection({node});check(native->rebuildSelected(),"units289 technical selection stays native");++technical;}
    record("technical289",{{"checked",technical}});view->restoreSelection({unit});
    }view->restoreSelection({unit});
    if(configuration["mode"].toString()!="acceptUnits291"&&map=="zone1")unitsServiceScenario289(unit,map);
    if(configuration["mode"].toString()=="acceptUnits291"&&(map!="zone1"||configuration["resumeAfter"].toString()!="zone1-before-mcp"))unitsMergedOutputs291(unit,map);
    if(originalValidation["errors"].toInt()>0){
        operation(request("get_unit",{{"id",double(nid)}}));
        record("original_data_blocks_mcp_edit289",{{"errors",originalValidation["errors"]}});
        key(Qt::Key_G,"g");key(Qt::Key_X,"x");key(Qt::Key_1,"1");key(Qt::Key_Return,"\r");
        check(qAbs(unit->position().x()-original.x()-1)<.001f,"units289 original-invalid MOB keeps native Qt editing");
    } else {
    QJsonValue revision=request("get_revision").value("revision");
    operation(request("edit_units",{{"revision",revision},{"action","transform"},{"ids",QJsonArray{double(nid)}},{"delta",QJsonArray{.25,0,0}},{"layer",3}}));
    check(qAbs(unit->position().x()-original.x()-.25)<.001f&&native->rebuildSelected(),"units289 real MCP edit_units native transform");window->m_undoStack->undo();
    revision=request("get_revision").value("revision");
    operation(request("edit_objects",{{"revision",revision},{"action","transform"},{"ids",QJsonArray{double(nid)}},{"delta",QJsonArray{.5,0,0}},{"layer",3}}));
    check(qAbs(unit->position().x()-original.x()-.5)<.001f&&unit->logicCount()==5,"units289 real MCP edit_objects unit transaction");
    }
    unitsCapture289(map+"-mcp-edited");
    // Save As uses the production dialog/save pipeline; only the owned copy is writable.
    const auto saved=caseRoot+"/"+map+"-saved.mob";nextFile=saved;view->saveMobAs();check(QFileInfo(saved).isFile(),"units289 production Save As created copy");
    QFile file(saved);check(file.open(QIODevice::ReadOnly),"units289 saved MOB readable");const auto expected=file.readAll();file.close();
    QByteArray candidate;CValidationReport report;check(view->m_activeMob->buildValidatedCandidate(&candidate,&report)&&candidate==expected,"units289 saved bytes match document MOB");
    const auto position=unit->position();window->closeAll();window->openMapPair(fixture()+"/Maps/"+map+".mpr",saved,map+" saved");unitsReady289();
    auto* reopened=dynamic_cast<CUnit*>(view->m_activeMob->nodeByMapId(nid));check(reopened&&reopened->position()==position&&reopened->logicCount()==5,"units289 reopened edit and five slots retained");
    candidate.clear();check(view->m_activeMob->buildValidatedCandidate(&candidate,&report)&&candidate==expected,"units289 reopen MOB byte equality");
    view->restoreSelection({reopened});
    unitsCapture289(map+"-reopened");view->m_cam->clearPreviewView();window->closeAll();
}
