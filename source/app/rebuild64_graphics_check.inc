// One real Qt event sequence; no polling/retry matrix or owner processes.
void graphicsScenario() {
    record("graphics_stage",{{"name","zone1"}});gallery("zone1");native->m_diagnosticFrame=60;
    if(configuration["graphicsWarmOnly"].toBool()){record("graphics_warm_first_frame");window->closeAll();return;}
    record("graphics_stage",{{"name","zone3obr"}});gallery("zone3obr");
    record("graphics_stage",{{"name","observe60"}});QElapsedTimer observation;observation.start();
    check(wait([&]{return observation.elapsed()>=60000;},62000),"zone3obr observation 60 seconds");
    check(native->frameIsCurrent()&&native->rebuildSelected(),"zone3obr still presents in Rebuild64");
    record("graphics_stage",{{"name","edit_closeup"}});
    CWorldObj* editable=nullptr;for(auto* node:view->m_activeMob->nodes())if(auto* object=dynamic_cast<CWorldObj*>(node))if(object->nodeState()!=eHidden){editable=object;break;}
    check(editable,"real object available for unsaved edit");view->restoreSelection({editable});const auto originalPosition=editable->position();
    // Deliver separate user input turns through Qt, rather than monopolising
    // one synchronous test callback with selection and all four keystrokes.
    const auto inputKey=[&](int code,const QString& text){bool delivered=false;QTimer::singleShot(25,this,[&]{key(code,text);delivered=true;});check(wait([&]{return delivered;},10000),"queued Qt key delivered");};
    inputKey(Qt::Key_G,"g");inputKey(Qt::Key_X,"x");inputKey(Qt::Key_1,"1");inputKey(Qt::Key_Return,"\r");
    check(wait([&]{return native->frameIsCurrent()&&!native->pendingEditorInput();}),"unsaved object edit reached native frame");
    check(qAbs(editable->position().x()-originalPosition.x()-1)<.001f,"real unsaved object translation");
    const auto dirtyPosition=editable->position();
    if(configuration["graphicsCloseup"].toBool()) {
        CUnit* subject=nullptr;int score=-1;for(auto* mob:view->m_aMob)for(auto* node:mob->nodes())if(auto* unit=dynamic_cast<CUnit*>(node))if(unit->nodeState()!=eHidden){
            const auto model=unit->renderedModelName();const int candidate=model=="unanwiwo"?4:model=="unanhodo"?3:model=="unanhoha"?2:model=="unanhopi"?1:0;
            if(candidate>score){subject=unit;score=candidate;}
        }
        if(!subject)for(auto* node:view->m_activeMob->nodes())if(auto* unit=dynamic_cast<CUnit*>(node))if(unit->nodeState()!=eHidden){subject=unit;break;}
        check(subject&&score>0,"animal with native fur profile for closeup");const auto p=subject->drawPosition()+QVector3D(0,0,.6f);
        QMatrix4x4 closeup;closeup.lookAt(p+QVector3D(2,-4,2),p,QVector3D(0,0,1));view->m_cam->adoptView(closeup);
        record("graphics_closeup",{{"nid",int(subject->mapId())},{"model",subject->renderedModelName()},{"prototype",subject->databaseName()}});
        QElapsedTimer cameraSettle;cameraSettle.start();check(wait([&]{return cameraSettle.elapsed()>1500&&native->frameIsCurrent();}),"closeup camera ready");
    }
    const auto camera=native->outputCamera();const auto map=CLandscape::getInstance()->filePath().absoluteFilePath();
    const auto undo=window->m_undoStack->index();const auto generation=native->statistics().generation;
    const auto saved=rebuild64graphics::load();
    check(saved==rebuild64graphics::defaults(),"clean mapper graphics use renderer defaults");
    auto listRoundtrip=saved;listRoundtrip["water_surf_no_sea_maps"]="zone7,zone8";QString roundtripError;
    check(rebuild64graphics::save(listRoundtrip,&roundtripError)&&rebuild64graphics::load().value("water_surf_no_sea_maps").toString()=="zone7,zone8","comma-separated map exclusions survive own INI reload");
    check(rebuild64graphics::save(saved,&roundtripError),"restore own INI after text roundtrip check");
    check(QFileInfo(rebuild64graphics::filePath()).isFile(),"own persistent graphics INI exists beside mapper options");
    record("graphics_stage",{{"name","dialog_open"}});action("actionRebuild64Graphics");
    auto* dialog=window->findChild<CRebuild64GraphicsDialog*>("rebuild64GraphicsDialog");
    check(dialog&&dialog->isVisible()&&dialog->testAttribute(Qt::WA_DontShowOnScreen),"real hidden graphics window opened");
    check(dialog->windowTitle()==QString::fromUtf8("Графика Rebuild64"),"Russian TS window title");
    dialog->grab().save(caseRoot+"/graphics-dialog.png");
    auto* shadows=dialog->findChild<QCheckBox*>("shadows");auto* fur=dialog->findChild<QComboBox*>("fur_mode");
    auto* apply=dialog->findChild<QPushButton*>("graphicsApply");auto* cancel=dialog->findChild<QPushButton*>("graphicsCancel");auto* defaults=dialog->findChild<QPushButton*>("graphicsDefaults");
    check(shadows&&fur&&apply&&cancel&&defaults,"real shadow/fur/apply/defaults/cancel controls");
    const auto click=[&](QWidget* target){auto p=target->rect().center();if(qobject_cast<QCheckBox*>(target))p.setX(8);QMouseEvent down(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier),up(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(target,&down);QApplication::sendEvent(target,&up);};
    const auto capture=[&](const QString& name){
        // Give the worker a frame boundary after the applied signal.
        QElapsedTimer settle;settle.start();check(wait([&]{return settle.elapsed()>1000&&native->frameIsCurrent();},30000),"graphics current after apply: "+name);
        const auto output=caseRoot+"/"+name+".png";bool done=false;QString error;
        const auto c=connect(native,&CRebuild64Viewport::captureFinished,this,[&](const QString& p,const QString& e,quint64){if(p==output){done=true;error=e;}});
        native->capture(output,true);check(wait([&]{return done;},30000)&&error.isEmpty(),"graphics world capture "+name+": "+error);disconnect(c);
        record("graphics_frame",{{"name",name},{"generation",QString::number(native->statistics().generation)},{"renderer",native->rebuildSelected()?"Rebuild64":"OpenGL"}});
        return QImage(output);
    };
    const auto baseline=capture("graphics-default");
    dialog->findChild<QTabWidget*>()->setCurrentIndex(1);
    record("graphics_stage",{{"name","apply_shadows"}});click(shadows);check(!shadows->isChecked(),"real Qt click disables shadows");click(apply);const auto noShadow=capture("graphics-no-shadows");
    check(baseline!=noShadow,"shadow toggle changes native world pixels");
    // Key event on the actual combo changes full fur (2) to off (0).
    dialog->findChild<QTabWidget*>()->setCurrentIndex(6);fur->setFocus();QKeyEvent homeDown(QEvent::KeyPress,Qt::Key_Home,Qt::NoModifier),homeUp(QEvent::KeyRelease,Qt::Key_Home,Qt::NoModifier);QApplication::sendEvent(fur,&homeDown);QApplication::sendEvent(fur,&homeUp);
    check(fur->currentData().toInt()==0,"real Qt key selected fur off");record("graphics_stage",{{"name","apply_fur"}});click(apply);const auto noFur=capture("graphics-no-fur");
    check(noShadow!=noFur,"fur toggle changes native world pixels");
    check(native->statistics().generation==generation,"live settings preserve renderer generation");
    record("graphics_stage",{{"name","defaults"}});click(defaults);check(shadows->isChecked()&&fur->currentData().toInt()==2,"renderer defaults restore widgets");
    click(apply);capture("graphics-restored");
    // Exercise a creation-time option and the same-window restart path.
    auto* terrain=dialog->findChild<QComboBox*>("terrain_tessellation");check(terrain,"restart option exists");
    dialog->findChild<QTabWidget*>()->setCurrentIndex(3);record("graphics_stage",{{"name","apply_restart"}});terrain->setFocus();QApplication::sendEvent(terrain,&homeDown);QApplication::sendEvent(terrain,&homeUp);check(terrain->currentData().toInt()==0,"real Qt key changes creation-time setting");click(apply);capture("graphics-session-recreated");
    record("graphics_stage",{{"name","defaults_restart"}});click(defaults);click(apply);capture("graphics-session-restored");
    click(shadows);click(cancel);check(rebuild64graphics::load()==saved,"Cancel does not save un-applied edits");
    check(native->rebuildSelected()&&failures==0,"Renderer selection stays Rebuild64 without fallback");
    check(CLandscape::getInstance()->filePath().absoluteFilePath()==map&&window->m_undoStack->index()==undo,"map and document Undo unchanged");
    const auto after=native->outputCamera();check(std::memcmp(camera.eye,after.eye,sizeof(camera.eye))==0&&std::memcmp(camera.world_to_clip,after.world_to_clip,sizeof(camera.world_to_clip))==0,"camera unchanged through live apply and session recreation");
    check(graphicsConfig64Attempts.load()==0,"native file events contain no Config64 opens");
    record("graphics_file_audit",{{"Config64Attempts",graphicsConfig64Attempts.load()},{"events",graphicsFileEvents.load()}});
    check(editable->position()==dirtyPosition,"unsaved object value preserved");window->on_actionUndo_triggered();
    check((editable->position()-originalPosition).length()<.001f,"test edit undone before close without Save");
    window->closeAll();
}
