// Isolated real-widget regression for EI-REBUILD64-BEHAVIOUR-293.
void behaviour293Scenario() {
    openPair("zone1");validateReady("zone1");
    const auto flag=[&](const char* name){return dynamic_cast<COptBool*>(view->m_pSettings->opt(name))->value();};
    record("293_owner_flags",{{"drawHelp",flag("drawHelp")},{"drawLogic",flag("drawLogic")},{"mode",int(CScene::getInstance()->getMode())},{"helpers",view->m_objectHelpersVisible}});
    const auto audit=[&](const QString& name){
        const auto snapshot=view->nativeServiceSnapshot();int paths=0,helps=0;
        for(const auto& batch:snapshot.batches)if(batch.primitive==CNativeEditorService::Strip){
            paths+=batch.color==QVector4D(.9f,.4f,.1f,1)||batch.color==QVector4D(.8f,.8f,.2f,1)||batch.color==QVector4D(.46f,.46f,.46f,.82f);
            helps+=batch.color==QVector4D(.8f,0,1,1);
        }
        int expected=0;const bool mode=CScene::getInstance()->getMode()==eEditModeLogic;
        for(auto* mob:view->m_aMob)if(view->isMobVisible(mob))for(auto* node:mob->nodes())if(auto* u=dynamic_cast<CUnit*>(node)){
            if(u->isMarkDeleted()||u->nodeState()==eHidden||!view->isObjectVisualVisible(u))continue;
            auto* logic=u->logicAt(u->editorLogicIndex());if(!logic||logic->editorPreviewPath().isEmpty())continue;
            const bool active=mob==view->m_activeMob,enabled=u->editorLogicIndex()==0||logic->isUse();
            if((enabled||(active&&mode))&&(flag("drawLogic")||(mode?active:u->nodeState()==eSelect)))++expected;
        }
        record("293_service_visibility",{{"name",name},{"paths",paths},{"expectedGLPaths",expected},{"helps",helps}});
        check(paths==expected,"293 GL path visibility: "+name);return paths;
    };
    if(!configuration["cameraOnly293"].toBool())check(audit("owner-start")==0,"293 owner start has no NPC paths");
    const auto capture=[&](const QString& name){
        bool done=false;QString error;const auto path=caseRoot+"/"+name+".png";
        auto c=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& p,const QString& e,quint64){if(p==path){done=true;error=e;}});
        native->capture(path);check(wait([&]{return done;})&&error.isEmpty(),"293 capture "+name+": "+error);disconnect(c);
    };
    if(!configuration["cameraOnly293"].toBool())capture("zone1-start-objects");
    CUnit* npc=nullptr;for(auto* n:view->m_activeMob->nodes())if(auto* u=dynamic_cast<CUnit*>(n))if(!npc&&!u->isMarkDeleted()&&u->nodeState()!=eHidden&&u->logicAt(u->editorLogicIndex())&&!u->logicAt(u->editorLogicIndex())->editorPreviewPath().isEmpty())npc=u;
    check(npc,"293 real NPC with authored logic");
    view->clearObjectSelection();if(!configuration["cameraOnly293"].toBool()){npc->setState(eSelect);emit view->selectionChanged();}
    const auto p=npc->drawPosition();QMatrix4x4 camera;camera.lookAt(p+QVector3D(0,-45,65),p,{0,0,1});view->m_cam->adoptView(camera);native->tick();
    if(!configuration["cameraOnly293"].toBool()){
    record("293_selected",{{"nid",int(npc->mapId())},{"position",QJsonArray{p.x(),p.y(),p.z()}}});check(audit("selected")==1,"293 selected NPC alone shows logic");capture("zone1-selected");
    }
    view->clearObjectSelection();action("actionChange_mod_e");
    check(CScene::getInstance()->getMode()==eEditModeLogic,"293 actual Behaviour QAction");if(!configuration["cameraOnly293"].toBool()){check(audit("behaviour")>0,"293 active MOB Behaviour has paths");
    auto* active=view->m_activeMob;view->m_activeMob=nullptr;
    check(audit("inactive-mob")==0,"293 inactive MOB has no paths when drawLogic is off");
    auto* logicOption=dynamic_cast<COptBool*>(view->m_pSettings->opt("drawLogic"));logicOption->setValue(true);
    check(audit("inactive-mob-drawLogic")>0,"293 drawLogic admits inactive MOB paths");logicOption->setValue(false);view->m_activeMob=active;
    }
    native->tick();capture("zone1-behaviour");
    auto* surface=native->findChild<QWidget*>("rebuild64NativeSurface");check(surface,"293 input surface");
    const auto mouse=[&](QEvent::Type type,const QPoint& pos,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent e(type,pos,button,buttons,Qt::NoModifier);QApplication::sendEvent(surface,&e);};
    for(const auto& kind:{QString("rotate"),QString("pan")}){
        if(configuration["panOnly293"].toBool()&&kind!="pan")continue;
        const int phase=kind=="rotate"?1:2;latency293::state().phase.store(phase);
        maxGap=0;lastPulse=qpc();const auto begin=qpc();const auto before=latestPresentClip();
        const auto center=surface->rect().center();
        if(kind=="pan"){QKeyEvent e(QEvent::KeyPress,Qt::Key_Shift,Qt::ShiftModifier);QApplication::sendEvent(surface,&e);}
        mouse(QEvent::MouseButtonPress,center,Qt::MiddleButton,Qt::MiddleButton);
        for(int i=1;i<=24;++i){
            const auto previous=configuration["panOnly293"].toBool()?latestPresentClip():QJsonArray();
            mouse(QEvent::MouseMove,center+QPoint(i*2,i/3),Qt::NoButton,Qt::MiddleButton);
            QElapsedTimer frame;frame.start();wait([&]{return frame.elapsed()>=24;},1000);
            // Capturing every new camera can outlast a fixed input window.
            // Await a real new Present through Qt; never block the GUI thread.
            if(configuration["panOnly293"].toBool())check(wait([&]{return previous!=latestPresentClip();},10000),"293 pan step has a new actual Present");
        }
        mouse(QEvent::MouseButtonRelease,center+QPoint(48,8),Qt::MiddleButton,Qt::NoButton);
        if(kind=="pan"){QKeyEvent e(QEvent::KeyRelease,Qt::Key_Shift,Qt::NoModifier);QApplication::sendEvent(surface,&e);}
        const auto end=qpc();latency293::state().phase.store(0);record("293_latency_places",{{"kind",kind},{"places",latency293::results(phase)}});record("293_camera_series",{{"kind",kind},{"beginQpc",QString::number(begin)},{"endQpc",QString::number(end)},{"maxQtGapMs",double(qMax(maxGap,end-lastPulse))/10000.0},{"clipChanged",before!=latestPresentClip()}});
        check(before!=latestPresentClip(),"293 Qt camera changes accepted world: "+kind);capture("zone1-after-"+kind);
    }
    check(native->rebuildSelected(),"293 retains Rebuild64");auditOpenResources();window->closeAll();
}
