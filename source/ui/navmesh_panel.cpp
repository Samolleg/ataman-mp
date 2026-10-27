#include "navmesh_panel.h"
#include <QtWidgets>
#include <QtConcurrent>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

CNavmeshPanel::CNavmeshPanel(QWidget* parent):QWidget(parent) {
    setObjectName("navmeshPanel");
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
    auto* profileRow=new QHBoxLayout;
    profileRow->addWidget(new QLabel(QStringLiteral("Профиль:"),this));
    m_profile=new QComboBox(this);m_profile->setObjectName("navmeshProfile");
    const QStringList labels={QStringLiteral("Крошечный (tiny)"),QStringLiteral("Маленький (small)"),QStringLiteral("Человек (human)"),QStringLiteral("Крупный (large)"),QStringLiteral("Гигант (giant)"),QStringLiteral("Парящий крошечный (levitate_tiny)"),QStringLiteral("Парящий маленький (levitate_small)"),QStringLiteral("Парящий (levitate)")};
    const auto profiles=nav64Profiles();for(int i=0;i<profiles.size();++i)m_profile->addItem(labels[i],profiles[i]);m_profile->setCurrentIndex(2);profileRow->addWidget(m_profile,1);layout->addLayout(profileRow);
    m_status=new QLabel(QStringLiteral("Сетка не запечена"),this);m_status->setObjectName("navmeshStatus");m_status->setTextFormat(Qt::PlainText);m_status->setWordWrap(true);layout->addWidget(m_status);
    m_freshness=new QLabel(this);m_freshness->setObjectName("navmeshFreshness");m_freshness->setTextFormat(Qt::PlainText);m_freshness->setWordWrap(true);layout->addWidget(m_freshness);
    m_algorithmStatus=new QLabel(this);m_algorithmStatus->setObjectName("navmeshAlgorithmStatus");m_algorithmStatus->setWordWrap(true);m_algorithmStatus->setStyleSheet("color: #ffbd69");m_algorithmStatus->hide();layout->addWidget(m_algorithmStatus);
    m_bodiesStatus=new QLabel(this);m_bodiesStatus->setObjectName("navmeshBodiesStatus");m_bodiesStatus->setTextFormat(Qt::PlainText);m_bodiesStatus->setWordWrap(true);layout->addWidget(m_bodiesStatus);
    m_heightOutliers=new QLabel(this);m_heightOutliers->setTextFormat(Qt::PlainText);m_heightOutliers->setWordWrap(true);m_heightOutliers->hide();layout->addWidget(m_heightOutliers);
    auto* viewRow=new QHBoxLayout;
    viewRow->addWidget(new QLabel(QStringLiteral("Вид:"),this));
    m_fill=new QComboBox(this);m_fill->setObjectName("navmeshFill");m_fill->addItems({QStringLiteral("Заливка"),QStringLiteral("Контуры"),QStringLiteral("Потери проходимости")});viewRow->addWidget(m_fill,1);layout->addLayout(viewRow);
    auto* diagnosticsRow=new QHBoxLayout;
    m_narrow=new QCheckBox(QStringLiteral("Узкие места"),this);m_narrow->setObjectName("navmeshNarrow");m_narrow->setToolTip(QStringLiteral("Приблизительно: соединения короче диаметра профиля в уже эродированной сетке."));diagnosticsRow->addWidget(m_narrow);
    m_islands=new QCheckBox(QStringLiteral("Острова"),this);m_islands->setObjectName("navmeshIslands");diagnosticsRow->addWidget(m_islands);
    m_legend=new QCheckBox(QStringLiteral("Легенда"),this);m_legend->setObjectName("navmeshLegend");m_legend->setChecked(QSettings().value("nav64/legend",true).toBool());diagnosticsRow->addWidget(m_legend);diagnosticsRow->addStretch();layout->addLayout(diagnosticsRow);
    m_compareLayer=new QComboBox(this);m_compareLayer->setObjectName("navmeshCompareLayer");m_compareLayer->addItem(QString(),-1);for(int i=0;i<8;++i)m_compareLayer->addItem(nav64LegacyLayerLabel(i),i);layout->addWidget(m_compareLayer);
    m_compareStatus=new QLabel(this);m_compareStatus->setObjectName("navmeshCompareStatus");m_compareStatus->setWordWrap(true);layout->addWidget(m_compareStatus);
    m_regions=new QListWidget(this);m_regions->setObjectName("navmeshRegions");m_regions->setMaximumHeight(100);m_regions->setToolTip(QStringLiteral("Нажмите область для перехода и подсветки; также можно нажать заливку в сцене."));layout->addWidget(m_regions);
    m_exportLosses=new QPushButton(QStringLiteral("Экспорт списка…"),this);m_exportLosses->setObjectName("navmeshExportLosses");layout->addWidget(m_exportLosses);
    connect(m_exportLosses,&QPushButton::clicked,this,[this](){const auto path=QFileDialog::getSaveFileName(this,QStringLiteral("Экспорт потерь проходимости"),QFileInfo(m_mpr).completeBaseName()+"-"+m_profile->currentData().toString()+"-losses.csv",QStringLiteral("CSV (*.csv)"));if(path.isEmpty())return;QString error;if(!exportLosses(path,&error))m_log->appendPlainText(QStringLiteral("Экспорт: ")+error);else m_log->appendPlainText(QStringLiteral("Экспорт списка: ")+path);});
    m_bakerPath=QSettings().value("nav64/bakerPath").toString();
    m_browse=new QPushButton(this);m_browse->setObjectName("navmeshBakerBrowse");layout->addWidget(m_browse);
    m_useInstalled=new QPushButton(QStringLiteral("Использовать установленный запекатель"),this);m_useInstalled->setObjectName("navmeshUseInstalled");layout->addWidget(m_useInstalled);
    connect(m_useInstalled,&QPushButton::clicked,this,[this](){m_bakerPath.clear();QSettings().remove("nav64/bakerPath");controls();});
    auto* bakeRow=new QHBoxLayout;
    m_this=new QPushButton(QStringLiteral("Запечь эту карту"),this);m_this->setObjectName("navmeshBakeMap");bakeRow->addWidget(m_this);
    m_all=new QPushButton(QStringLiteral("Запечь все карты мода"),this);m_all->setObjectName("navmeshBakeAll");bakeRow->addWidget(m_all);layout->addLayout(bakeRow);
    auto* progressRow=new QHBoxLayout;
    m_progress=new QProgressBar(this);m_progress->setObjectName("navmeshBakeProgress");progressRow->addWidget(m_progress,1);
    m_cancel=new QPushButton(QStringLiteral("Отмена"),this);m_cancel->setObjectName("navmeshBakeCancel");progressRow->addWidget(m_cancel);layout->addLayout(progressRow);
    m_logToggle=new QToolButton(this);m_logToggle->setObjectName("navmeshLogToggle");m_logToggle->setText(QStringLiteral("Журнал ▸"));m_logToggle->setCheckable(true);layout->addWidget(m_logToggle);
    m_log=new QPlainTextEdit(this);m_log->setObjectName("navmeshBakeLog");m_log->setReadOnly(true);m_log->setMaximumBlockCount(2000);m_log->setFixedHeight(110);m_log->hide();layout->addWidget(m_log);
    connect(m_logToggle,&QToolButton::toggled,this,[this](bool open){m_log->setVisible(open);m_logToggle->setText(open?QStringLiteral("Журнал ▾"):QStringLiteral("Журнал ▸"));emit contentChanged();if(open)QTimer::singleShot(0,this,[this](){for(QWidget* p=parentWidget();p;p=p->parentWidget())if(auto* scroll=qobject_cast<QScrollArea*>(p)){scroll->ensureWidgetVisible(m_log);break;}});});
    m_process=new QProcess(this);m_loading=new QFutureWatcher<SNav64Document>(this);
    m_comparing=new QFutureWatcher<SNav64Comparison>(this);
    connect(m_comparing,&QFutureWatcher<SNav64Comparison>::finished,this,[this](){
        if(m_runningGeneration!=m_compareGeneration){if(lossesVisible()&&m_document.valid)startComparison();return;}
        m_comparison=m_comparing->result();m_regions->clear();
        if(m_comparison.valid){m_compareStatus->setText(QStringLiteral("потеряно %1 м² / появилось %2 м²\nМаска старого алгоритма, клетка 0,5 м; коллизии %3. Это пересчёт, а не маска из MOB/.grp.").arg(m_comparison.lost*.25,0,'f',2).arg(m_comparison.gained*.25,0,'f',2).arg(m_compareInput.collisionMetadataApproximate?QStringLiteral("приближённые"):QStringLiteral("из метаданных")));
            QVector<int> order;for(int i=0;i<m_comparison.areas.size();++i)order<<i;std::sort(order.begin(),order.end(),[this](int a,int b){return m_comparison.areas[a].cells>m_comparison.areas[b].cells;});
            for(int i:order){const auto& a=m_comparison.areas[i];auto* item=new QListWidgetItem(QStringLiteral("%1 %2 м² · %3, %4").arg(a.lost?QStringLiteral("Потеряно"):QStringLiteral("Появилось")).arg(a.cells*.25,0,'f',2).arg(a.center.x(),0,'f',1).arg(a.center.y(),0,'f',1),m_regions);item->setData(Qt::UserRole,i);}
        }else m_compareStatus->setText(m_comparison.error.isEmpty()?QStringLiteral("Сравнение отменено"):m_comparison.error);
        controls();emit geometryChanged();emit contentChanged();
    });
    connect(m_regions,&QListWidget::itemClicked,this,[this](QListWidgetItem* item){selectRegion(item->data(Qt::UserRole).toInt());});
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
    connect(m_fill,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](){controls();requestComparison();emit geometryChanged();});
    connect(m_compareLayer,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](){QSettings().setValue("nav64/compareLayer/"+m_profile->currentData().toString(),m_compareLayer->currentData());requestComparison();});
    connect(m_legend,&QCheckBox::toggled,this,[this](bool on){QSettings().setValue("nav64/legend",on);emit geometryChanged();});
    for(auto* check:{m_narrow,m_islands})connect(check,&QCheckBox::toggled,this,[this](){emit geometryChanged();});
    connect(m_profile,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](){QSignalBlocker block(m_compareLayer);m_compareLayer->setCurrentIndex(qMax(0,m_compareLayer->findData(QSettings().value("nav64/compareLayer/"+m_profile->currentData().toString(),-1).toInt())));controls();reload();});
    connect(m_browse,&QPushButton::clicked,this,[this](){auto path=QFileDialog::getOpenFileName(this,QStringLiteral("Запекатель Nav64"),bakerPath(),"EIRebuild64NavBake.exe (*.exe)");if(!path.isEmpty()){setBakerPath(path);QSettings().setValue("nav64/bakerPath",path);}});
    connect(m_this,&QPushButton::clicked,this,[this](){startBake(false);});connect(m_all,&QPushButton::clicked,this,[this](){startBake(true);});connect(m_cancel,&QPushButton::clicked,this,&CNavmeshPanel::cancelBake);
    connect(m_process,&QProcess::readyReadStandardOutput,this,[this](){drain(false);});connect(m_process,&QProcess::readyReadStandardError,this,[this](){drain(true);});
    connect(m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){m_lastExit=-1;m_lastSuccess=false;m_status->setText(QStringLiteral("Не удалось запустить запекатель: ")+m_process->errorString());m_log->appendPlainText(m_status->text());m_backups.clear();controls();emit bakeFinished(false);}});
    connect(m_process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        drain(false);drain(true);if(!m_stdout.isEmpty())line(m_stdout);if(!m_stderr.isEmpty())line(m_stderr);m_stdout.clear();m_stderr.clear();
        m_lastExit=code;m_lastSuccess=!m_cancelled&&status==QProcess::NormalExit&&code==0&&m_done&&!m_errors;
        m_log->appendPlainText(m_lastSuccess?QStringLiteral("Запекание завершено: exit=0 и BAKE_DONE errors=0."):QStringLiteral("Запекание не завершено: exit=%1, BAKE_DONE=%2%3").arg(code).arg(m_done).arg(m_cancelled?QStringLiteral(", отменено"):QString()));
        if(!m_lastSuccess)m_status->setText(m_cancelled?QStringLiteral("Запекание отменено. Подробности в журнале."):QStringLiteral("Запекание не завершено (код %1). Проверьте ошибки в журнале.").arg(code));
        m_backups.clear();controls();
        if(m_lastSuccess&&m_requestMpr==m_mpr&&m_requestMob==m_mob){m_directory=QDir(m_outputRoot).filePath("Maps/Nav64");reload();}emit bakeFinished(m_lastSuccess);
    });
    connect(m_loading,&QFutureWatcher<SNav64Document>::finished,this,[this](){
        const auto result=m_loading->result();
        if(m_loadingPath==result.path){
            m_document=result;
            m_status->setText(result.valid?QStringLiteral("Сетка: %1, алгоритм %2, запечена %3").arg(QFileInfo(result.path).fileName()).arg(result.header.algorithm).arg(QFileInfo(result.path).lastModified().toString("dd.MM.yyyy HH:mm")):result.error);
            m_status->setToolTip(QStringLiteral("%1\n%2 тайлов, %3 полигонов, %4 островов, %5 узких порталов\n%6").arg(result.path).arg(result.tileCount).arg(result.polygonCount).arg(result.islandCount).arg(result.narrowCount).arg(result.warnings.join("\n")));
            m_status->setStyleSheet(result.warnings.isEmpty()?QString():QStringLiteral("color: #ffbd69"));freshness();algorithmStatus();bodiesStatus();requestComparison();emit contentChanged();emit geometryChanged();
        }else reload();
    });
    controls();
}
CNavmeshPanel::~CNavmeshPanel(){if(m_compareCancel)m_compareCancel->store(true);m_comparing->waitForFinished();if(m_process->state()!=QProcess::NotRunning){m_process->kill();m_process->waitForFinished(3000);}m_loading->waitForFinished();}
bool CNavmeshPanel::layerVisible()const{return m_layerVisible&&m_document.valid;}
void CNavmeshPanel::setLayerVisible(bool visible){m_layerVisible=visible;emit geometryChanged();}
bool CNavmeshPanel::fillVisible()const{return m_fill->currentIndex()==0;}
bool CNavmeshPanel::narrowVisible()const{return m_narrow->isChecked();}
bool CNavmeshPanel::islandsVisible()const{return m_islands->isChecked();}
bool CNavmeshPanel::busy()const{return m_process->state()!=QProcess::NotRunning;}
QString CNavmeshPanel::bakerPath()const{return m_bakerPath.trimmed().isEmpty()?QDir(m_gameRoot).filePath("Engine64/EIRebuild64NavBake.exe"):m_bakerPath.trimmed();}
void CNavmeshPanel::setBakerPath(const QString& path){m_bakerPath=path;controls();}
QString CNavmeshPanel::bakeLog()const{return m_log->toPlainText();}
void CNavmeshPanel::controls(){
    m_this->setEnabled(!busy()&&!m_mpr.isEmpty()&&!m_mob.isEmpty());m_all->setEnabled(!busy()&&!m_mpr.isEmpty());
    m_cancel->setVisible(busy());m_progress->setVisible(busy());m_browse->setEnabled(!busy());
    inspectBaker();
    const QString installed=QDir(m_gameRoot).filePath("Engine64/EIRebuild64NavBake.exe");
    const bool isInstalled=QFileInfo(bakerPath()).absoluteFilePath().compare(QFileInfo(installed).absoluteFilePath(),Qt::CaseInsensitive)==0;
    const QString identity=m_bakerIdentity.algorithm?QStringLiteral(" (алгоритм %1)").arg(m_bakerIdentity.algorithm):QStringLiteral(" (алгоритм неизвестен)");
    m_browse->setText(QFileInfo(bakerPath()).isFile()?QStringLiteral("%1%2 · Изменить…").arg(isInstalled?QStringLiteral("Установленный"):QStringLiteral("Выбранный"),identity):QStringLiteral("Запекатель не найден — Указать…"));
    m_useInstalled->setVisible(!isInstalled&&QFileInfo(installed).isFile());m_useInstalled->setEnabled(!busy());
    m_compareLayer->setItemText(0,QStringLiteral("Сравнивается с: %1 (авто)").arg(nav64LegacyLayerLabel(nav64LegacyLayer(m_profile->currentData().toString()))));
    m_compareLayer->setVisible(lossesVisible());m_compareStatus->setVisible(lossesVisible());m_regions->setVisible(lossesVisible()&&m_comparison.valid);
    m_exportLosses->setVisible(lossesVisible());m_exportLosses->setEnabled(m_comparison.valid&&!m_comparing->isRunning());
    m_narrow->setEnabled(!lossesVisible());m_islands->setEnabled(!lossesVisible());algorithmStatus();
    m_browse->setToolTip(bakerPath());emit contentChanged();
}
void CNavmeshPanel::setScene(const QString& mpr,const QString& mob,const QString& gameRoot,const QString& effectiveMapsDirectory){
    const QString maps=effectiveMapsDirectory.isEmpty()?(mpr.isEmpty()?QString():QFileInfo(mpr).absolutePath()):effectiveMapsDirectory;
    if(mpr==m_mpr&&mob==m_mob&&gameRoot==m_gameRoot&&maps==m_mapsDirectory){const QFileInfo exe(bakerPath());if(m_inspectedBaker!=exe.absoluteFilePath()||m_bakerSize!=exe.size()||m_bakerMtime!=exe.lastModified().toMSecsSinceEpoch())controls();return;}
    m_mpr=mpr;m_mob=mob;m_gameRoot=gameRoot;m_mapsDirectory=maps;m_directory=maps.isEmpty()?QString():QDir(maps).absoluteFilePath("Nav64");
    controls();reload();
}
void CNavmeshPanel::setUnsavedChanges(bool dirty){if(m_unsaved==dirty)return;m_unsaved=dirty;freshness();emit contentChanged();}
void CNavmeshPanel::reload(){
    invalidateComparison();
    m_document={};m_loadingPath.clear();bodiesStatus();emit geometryChanged();
    if(m_mpr.isEmpty()||m_mob.isEmpty()){m_status->setText(QStringLiteral("Откройте карту с первичным MOB"));freshness();return;}
    QString error;const auto path=findNav64ForScene(m_directory,m_mpr,m_mob,m_profile->currentData().toString(),&error);m_loadingPath=path;
    if(path.isEmpty()){m_status->setText(error);freshness();algorithmStatus();return;}
    if(m_loading->isRunning())return; // finished callback starts the latest request
    m_status->setText(QStringLiteral("Чтение Nav64…"));m_loading->setFuture(QtConcurrent::run([path](){return loadNav64(path);}));
}
void CNavmeshPanel::freshness(){
    m_freshness->setVisible(m_document.valid);
    if(m_unsaved){m_freshness->setText(QStringLiteral("Карта изменена после запекания: есть несохранённые правки (приблизительно). Перед запеканием сохраните MPR и первичный MOB."));return;}
    if(!m_document.valid){emit contentChanged();return;}
    const auto baked=QFileInfo(m_document.path).lastModified();bool newer=false;
    QString sidecar=m_mpr;sidecar=sidecar.left(sidecar.lastIndexOf('.'))+".navsrc";
    for(const auto& path:{m_mpr,m_mob,sidecar})if(QFileInfo(path).exists()&&QFileInfo(path).lastModified()>baked)newer=true;
    m_freshness->setText(newer?QStringLiteral("Карта изменена после запекания (приблизительно)"):QStringLiteral("По датам карта не изменена (приблизительно)"));
    m_freshness->setToolTip(QStringLiteral("Даты не обнаруживают правки моделей/архивов и изменения с сохранением времени. Проверка приблизительная; команда --fingerprint не используется."));
}
void CNavmeshPanel::line(const QByteArray& bytes){
    const QString s=QString::fromUtf8(bytes).trimmed();if(s.isEmpty())return;m_log->appendPlainText(s);
    QMap<QString,QString> fields;auto matches=QRegularExpression("(?:^|\\s)([A-Za-z0-9_]+)=([^\\s]+)").globalMatch(s);
    while(matches.hasNext()){const auto match=matches.next();fields[match.captured(1)]=match.captured(2);}
    if(s.startsWith("BAKE_BEGIN ")){
        bool valid=false;const int algorithm=fields.value("algorithm").toInt(&valid);
        if(valid&&algorithm>0&&!m_runningBakerSha.isEmpty()){
            QSettings settings;settings.setValue("nav64/bakerObservedSha256",m_runningBakerSha);settings.setValue("nav64/bakerObservedAlgorithm",algorithm);
            m_inspectedBaker.clear();controls();bodiesStatus();
        }
    }
    if(s.startsWith("BAKE_DONE ")){bool mapsOk=false,completedOk=false;const int maps=fields.value("maps").toInt(&mapsOk),completed=fields.value("completed").toInt(&completedOk);m_done=mapsOk&&completedOk&&maps>0&&completed==maps&&fields.value("errors")=="0"&&fields.value("missing")=="0";if(!m_done)m_errors=true;}
    if(s.startsWith("MAP_ERROR ")||s.startsWith("BAKE_ERROR ")){m_errors=true;m_status->setText(QStringLiteral("Ошибка запекания: ")+s);}
    if(s.startsWith("MAP_MISSING ")){m_errors=true;m_status->setText(QStringLiteral("Нет исходников MPR/MOB: %1 / %2. Карта пропущена (код 5).").arg(fields.value("mpr"),fields.value("mob")));m_log->appendPlainText(m_status->text());}
    // Keep every PROFILE/MAP_DONE record visible; neither implies overall success.
    if(s.startsWith("PROFILE ")){
        ++m_completedProfiles;m_progress->setRange(0,8);m_progress->setValue(qMin(8,m_completedProfiles));
        m_status->setText(QStringLiteral("Запекание: %1, радиус %2 м, тайлов %3, %4 мс").arg(fields.value("type"),fields.value("radius"),fields.value("tiles"),fields.value("ms",fields.value("wall_ms"))));
        bool valid=false;const auto count=fields.value("height_outliers").toULongLong(&valid);
        const bool warning=!valid||count!=0;
        m_heightOutliers->setVisible(warning);
        m_heightOutliers->setText((warning?QStringLiteral("Предупреждение: "):QString())+QStringLiteral("PROFILE %1: height_outliers=%2").arg(fields.value("type"),valid?QString::number(count):QStringLiteral("не сообщён — проверьте версию запекателя")));
        m_heightOutliers->setStyleSheet(warning?QStringLiteral("color: #ffbd69"):QString());
        if(warning){m_errors=true;m_log->appendPlainText(m_heightOutliers->text());}
    }
    if(s.startsWith("MAP_DONE "))m_status->setText(QStringLiteral("Карта готова; ожидается BAKE_DONE: ")+s);
    if(s.startsWith("MAP_BEGIN ")){m_completedProfiles=0;m_progress->setRange(0,0);m_status->setText(QStringLiteral("Запекание карты: ")+fields.value("mpr"));}
    if(s.startsWith("PROFILE ")||s.startsWith("MAP_DONE ")||s.startsWith("MAP_BEGIN "))m_log->setToolTip(s);
    emit contentChanged();
}
void CNavmeshPanel::drain(bool errors){auto& buffer=errors?m_stderr:m_stdout;buffer+=errors?m_process->readAllStandardError():m_process->readAllStandardOutput();int n;while((n=buffer.indexOf('\n'))>=0){line(buffer.left(n));buffer.remove(0,n+1);}if(buffer.size()>1024*1024){m_errors=true;line(buffer.left(4096));buffer.clear();}}
bool CNavmeshPanel::startBake(bool all,const QString& outputRootOverride){
    if(busy())return false;m_lastSuccess=false;m_lastExit=-1;
    auto fail=[this](const QString& message){m_status->setText(message);m_log->appendPlainText(message);emit contentChanged();emit bakeFinished(false);return false;};
    if(m_mpr.isEmpty()||(!all&&m_mob.isEmpty()))return fail(QStringLiteral("Откройте карту с первичным MOB."));
    if(m_unsaved)return fail(QStringLiteral("Сначала сохраните MPR и первичный MOB: запекатель читает файлы с диска."));
    const QString exe=bakerPath();
    if(!QFileInfo(exe).isFile())return fail(QStringLiteral("Запекатель не найден — нажмите «Указать…»."));
    m_runningBakerSha=nav64BakerIdentity(exe).sha256;
    QDir maps(m_mapsDirectory);if(maps.dirName().compare("Maps",Qt::CaseInsensitive)!=0)return fail(QStringLiteral("Для запекания задайте каталог Maps активного мода."));maps.cdUp();const QString mod=maps.absolutePath();
    if(m_gameRoot.isEmpty()||!QDir(m_gameRoot).exists())return fail(QStringLiteral("Не задан корректный корень игры."));
    m_outputRoot=outputRootOverride.isEmpty()?mod:QDir(outputRootOverride).absolutePath();const QString output=QDir(m_outputRoot).filePath("Maps/Nav64");
    m_requestMpr=m_mpr;m_requestMob=m_mob;m_backups.clear();
    const QString prefix=QFileInfo(m_mpr).completeBaseName().toLower()+"--"+QFileInfo(m_mob).completeBaseName().toLower()+".";
    for(const auto& entry:QDir(output).entryInfoList({"*.nav64","bodies.navbody"},QDir::Files)){if(!all&&entry.fileName()!="bodies.navbody"&&!entry.fileName().startsWith(prefix,Qt::CaseInsensitive))continue;const auto backup=CBackupManager().snapshotBeforeSave(entry);if(!backup.success){m_backups.clear();return fail(QStringLiteral("Перезапись запрещена: не создан бэкап ")+entry.fileName()+": "+backup.error);}m_backups.append(backup);m_log->appendPlainText(QStringLiteral("BACKUP ")+entry.fileName()+" -> "+backup.backupPath);}
    QStringList args{"--game-root",m_gameRoot,"--mod",mod,"--output-root",m_outputRoot};if(all)args<<"--all";else args<<"--map"<<QFileInfo(m_mpr).completeBaseName()<<"--mob"<<QFileInfo(m_mob).completeBaseName();
    m_stdout.clear();m_stderr.clear();m_done=false;m_errors=false;m_cancelled=false;m_log->appendPlainText(QStringLiteral("Вывод: ")+output);
    m_completedProfiles=0;m_progress->setRange(0,0);m_status->setText(QStringLiteral("Запекание: подготовка геометрии…"));m_heightOutliers->hide();
    m_process->setWorkingDirectory(m_gameRoot);m_process->start(exe,args);controls();return true;
}
void CNavmeshPanel::cancelBake(){if(!busy())return;m_cancelled=true;m_log->appendPlainText(QStringLiteral("Отмена: запекатель остановлен; уже записанные файлы сохранены, прежние версии — в бэкапах."));m_process->terminate();QTimer::singleShot(2000,m_process,[this](){if(m_cancelled&&busy())m_process->kill();});}

void CNavmeshPanel::inspectBaker(){
    const QFileInfo file(bakerPath());const auto modified=file.lastModified().toMSecsSinceEpoch();
    if(m_inspectedBaker==file.absoluteFilePath()&&m_bakerSize==file.size()&&m_bakerMtime==modified)return;
    m_inspectedBaker=file.absoluteFilePath();m_bakerSize=file.size();m_bakerMtime=modified;
    m_bakerIdentity=nav64BakerIdentity(bakerPath());
}
void CNavmeshPanel::bodiesStatus(){
    const auto path=m_directory.isEmpty()?QString():QDir(m_directory).filePath("bodies.navbody");
    const auto bodies=readNav64Bodies(path);const quint32 expected=m_document.valid?m_document.header.algorithm:(m_bakerIdentity.algorithm?quint32(m_bakerIdentity.algorithm):Navigation::AlgorithmVersion);
    QString text;
    if(!bodies.exists)text=QStringLiteral("Таблица тел: нет");
    else if(bodies.algorithm&&bodies.algorithm!=expected)text=QStringLiteral("Таблица тел: алгоритм не совпадает (%1, ожидается %2)").arg(bodies.algorithm).arg(expected);
    else if(!bodies.valid)text=QStringLiteral("Таблица тел: ")+bodies.error;
    else text=QStringLiteral("Таблица тел: %1 образцов, алгоритм %2").arg(bodies.sampleCount).arg(bodies.algorithm);
    m_bodiesStatus->setText(text);m_bodiesStatus->setToolTip(path);
    if(text!=m_lastBodiesStatus){m_log->appendPlainText(text);m_lastBodiesStatus=text;}
}
bool CNavmeshPanel::exportLosses(const QString& path,QString* error)const{
    return exportNav64ComparisonCsv(path,QFileInfo(m_mpr).completeBaseName(),m_document.header.profile,m_comparison,error);
}
void CNavmeshPanel::algorithmStatus(){
    quint32 algorithm=m_document.header.algorithm;
    // Preserve diagnostics even for an unsupported algorithm rejected by the
    // reader: this fixed prefix does not deserialize any untrusted payload.
    if(!algorithm&&!m_mpr.isEmpty()){
        QFile f(QDir(m_directory).filePath(QFileInfo(m_mpr).completeBaseName().toLower()+"--"+QFileInfo(m_mob).completeBaseName().toLower()+"."+m_profile->currentData().toString()+".nav64"));
        if(f.open(QIODevice::ReadOnly)){const auto b=f.read(16);if(b.size()==16&&b.left(8)==QByteArray::fromHex("4e41563636340000"))algorithm=quint32(uchar(b[12]))|(quint32(uchar(b[13]))<<8)|(quint32(uchar(b[14]))<<16)|(quint32(uchar(b[15]))<<24);}
    }
    const auto message=nav64AlgorithmMismatch(algorithm,m_bakerIdentity.algorithm);
    m_algorithmStatus->setText(message);m_algorithmStatus->setVisible(!message.isEmpty());
    if(!message.isEmpty()&&message!=m_lastMismatch)m_log->appendPlainText(message);
    m_lastMismatch=message;
}
bool CNavmeshPanel::lossesVisible()const{return m_fill->currentIndex()==2;}
bool CNavmeshPanel::legendVisible()const{return m_legend->isChecked()&&layerVisible();}
int CNavmeshPanel::comparisonLayer()const{const int layer=m_compareLayer->currentData().toInt();return layer<0?nav64LegacyLayer(m_profile->currentData().toString()):layer;}
void CNavmeshPanel::invalidateComparison(){
    ++m_compareGeneration;if(m_compareCancel)m_compareCancel->store(true);
    m_comparison={};m_selectedRegion=-1;m_regions->clear();m_regions->hide();m_exportLosses->setEnabled(false);m_compareStatus->setText(QStringLiteral("Ожидается сетка для сравнения…"));
}
void CNavmeshPanel::requestComparison(){
    invalidateComparison();
    if(lossesVisible()&&m_document.valid){m_compareStatus->setText(QStringLiteral("Расчёт потерь в фоне…"));emit comparisonRequested();}
    emit geometryChanged();emit contentChanged();
}
void CNavmeshPanel::setComparisonInput(const SAiGraphPreviewBuildInput& input,const QString& error){
    m_compareInput=input;
    if(!error.isEmpty()){m_compareStatus->setText(error);emit contentChanged();return;}
    if(!m_comparing->isRunning())startComparison();
}
void CNavmeshPanel::startComparison(){
    if(!lossesVisible()||!m_document.valid||m_compareInput.graphWidth<=0)return;
    m_runningGeneration=m_compareGeneration;m_compareCancel=std::make_shared<std::atomic_bool>(false);
    const auto input=m_compareInput;const auto mesh=m_document;const int layer=comparisonLayer();const auto cancel=m_compareCancel;
    m_comparing->setFuture(QtConcurrent::run([mesh,input,layer,cancel](){return compareNav64(mesh,input,layer,cancel);}));
}
void CNavmeshPanel::selectRegion(int index){if(index<0||index>=m_comparison.areas.size())return;m_selectedRegion=index;emit regionSelected(m_comparison.areas[index].center);emit geometryChanged();}
QString CNavmeshPanel::legendHtml()const{
    if(!legendVisible())return {};
    auto swatch=[](const QString& color,const QString& text){return QStringLiteral("<span style='color:%1'>■</span> %2<br>").arg(color,text.toHtmlEscaped());};
    QString html=QStringLiteral("<b>Navmesh · %1</b><br>").arg(m_profile->currentData().toString().toHtmlEscaped());
    if(lossesVisible())return html+swatch("#ff2629",QStringLiteral("Потеряно: старое проходимо, новое нет"))+swatch("#1aa6ff",QStringLiteral("Появилось: новое проходимо, старое нет"))+QStringLiteral("Клетка 0,5 м · пересчёт старого алгоритма<br>")+nav64LegacyLayerLabel(comparisonLayer()).toHtmlEscaped()+QStringLiteral("<br>Белая рамка — выбранная область");
    html+=QStringLiteral("Проходимо для профиля %1 (любая поза)<br>Дыры — препятствия / непроходимое<br>").arg(m_profile->currentData().toString().toHtmlEscaped());
    if(fillVisible()){
        QSet<int> areas;for(const auto& tile:m_document.renderTiles)for(int i=0;i<tile.areas.size();++i)if(!tile.areas[i].vertices.isEmpty())areas.insert(i);
        auto sorted=areas.values();std::sort(sorted.begin(),sorted.end());
        for(int id:sorted){const auto c=nav64AreaColor(id);html+=swatch(QColor::fromRgbF(c.x(),c.y(),c.z()).name(),nav64AreaName(id));}
    }else html+=swatch("#38f273",QStringLiteral("Контуры проходимых полигонов"));
    if(!m_document.tiles.isEmpty())html+=swatch("#26e6ff",QStringLiteral("Границы тайлов хранения"));
    if(narrowVisible()&&m_document.narrowCount)html+=swatch("#ff730d",QStringLiteral("Короткие порталы (< диаметра профиля)"));
    if(islandsVisible()&&!m_document.islands.isEmpty())html+=swatch("#ff1a26",QStringLiteral("Центры островов кроме крупнейшего"));
    return html;
}
