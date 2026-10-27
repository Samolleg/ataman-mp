#include "navmesh_panel.h"
#include <QtWidgets>
#include <QtConcurrent>
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
    m_heightOutliers=new QLabel(this);m_heightOutliers->setTextFormat(Qt::PlainText);m_heightOutliers->setWordWrap(true);m_heightOutliers->hide();layout->addWidget(m_heightOutliers);
    auto* viewRow=new QHBoxLayout;
    viewRow->addWidget(new QLabel(QStringLiteral("Вид:"),this));
    m_fill=new QComboBox(this);m_fill->setObjectName("navmeshFill");m_fill->addItems({QStringLiteral("Заливка"),QStringLiteral("Контуры")});viewRow->addWidget(m_fill);
    m_narrow=new QCheckBox(QStringLiteral("Узкие места"),this);m_narrow->setObjectName("navmeshNarrow");m_narrow->setToolTip(QStringLiteral("Приблизительно: соединения короче диаметра профиля в уже эродированной сетке."));viewRow->addWidget(m_narrow);
    m_islands=new QCheckBox(QStringLiteral("Острова"),this);m_islands->setObjectName("navmeshIslands");viewRow->addWidget(m_islands);viewRow->addStretch();layout->addLayout(viewRow);
    m_bakerPath=QSettings().value("nav64/bakerPath").toString();
    m_browse=new QPushButton(this);m_browse->setObjectName("navmeshBakerBrowse");layout->addWidget(m_browse);
    auto* bakeRow=new QHBoxLayout;
    m_this=new QPushButton(QStringLiteral("Запечь эту карту"),this);m_this->setObjectName("navmeshBakeMap");bakeRow->addWidget(m_this);
    m_all=new QPushButton(QStringLiteral("Запечь все карты мода"),this);m_all->setObjectName("navmeshBakeAll");bakeRow->addWidget(m_all);layout->addLayout(bakeRow);
    auto* progressRow=new QHBoxLayout;
    m_progress=new QProgressBar(this);m_progress->setObjectName("navmeshBakeProgress");progressRow->addWidget(m_progress,1);
    m_cancel=new QPushButton(QStringLiteral("Отмена"),this);m_cancel->setObjectName("navmeshBakeCancel");progressRow->addWidget(m_cancel);layout->addLayout(progressRow);
    m_logToggle=new QToolButton(this);m_logToggle->setObjectName("navmeshLogToggle");m_logToggle->setText(QStringLiteral("Журнал ▸"));m_logToggle->setCheckable(true);layout->addWidget(m_logToggle);
    m_log=new QPlainTextEdit(this);m_log->setObjectName("navmeshBakeLog");m_log->setReadOnly(true);m_log->setMaximumBlockCount(2000);m_log->setFixedHeight(110);m_log->hide();layout->addWidget(m_log);
    connect(m_logToggle,&QToolButton::toggled,this,[this](bool open){m_log->setVisible(open);m_logToggle->setText(open?QStringLiteral("Журнал ▾"):QStringLiteral("Журнал ▸"));emit contentChanged();});
    m_process=new QProcess(this);m_loading=new QFutureWatcher<SNav64Document>(this);
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
    connect(m_fill,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](){emit geometryChanged();});
    for(auto* check:{m_narrow,m_islands})connect(check,&QCheckBox::toggled,this,[this](){emit geometryChanged();});
    connect(m_profile,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](){reload();});
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
            m_status->setStyleSheet(result.warnings.isEmpty()?QString():QStringLiteral("color: #ffbd69"));freshness();emit contentChanged();emit geometryChanged();
        }else reload();
    });
    controls();
}
CNavmeshPanel::~CNavmeshPanel(){if(m_process->state()!=QProcess::NotRunning){m_process->kill();m_process->waitForFinished(3000);}m_loading->waitForFinished();}
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
    m_browse->setText(QFileInfo(bakerPath()).isFile()?QStringLiteral("Запекатель найден · Указать…"):QStringLiteral("Запекатель не найден — Указать…"));
    m_browse->setToolTip(bakerPath());emit contentChanged();
}
void CNavmeshPanel::setScene(const QString& mpr,const QString& mob,const QString& gameRoot,const QString& effectiveMapsDirectory){
    const QString maps=effectiveMapsDirectory.isEmpty()?(mpr.isEmpty()?QString():QFileInfo(mpr).absolutePath()):effectiveMapsDirectory;
    if(mpr==m_mpr&&mob==m_mob&&gameRoot==m_gameRoot&&maps==m_mapsDirectory)return;
    m_mpr=mpr;m_mob=mob;m_gameRoot=gameRoot;m_mapsDirectory=maps;m_directory=maps.isEmpty()?QString():QDir(maps).absoluteFilePath("Nav64");
    controls();reload();
}
void CNavmeshPanel::setUnsavedChanges(bool dirty){if(m_unsaved==dirty)return;m_unsaved=dirty;freshness();emit contentChanged();}
void CNavmeshPanel::reload(){
    m_document={};m_loadingPath.clear();emit geometryChanged();
    if(m_mpr.isEmpty()||m_mob.isEmpty()){m_status->setText(QStringLiteral("Откройте карту с первичным MOB"));freshness();return;}
    QString error;const auto path=findNav64ForScene(m_directory,m_mpr,m_mob,m_profile->currentData().toString(),&error);m_loadingPath=path;
    if(path.isEmpty()){m_status->setText(error);freshness();return;}
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
    m_freshness->setToolTip(QStringLiteral("Mapper не повторяет нормализацию геометрии EIRebuild64. Даты не обнаруживают правки моделей/архивов и изменения с сохранением времени. Fingerprint: ")+m_document.header.fingerprint);
}
void CNavmeshPanel::line(const QByteArray& bytes){
    const QString s=QString::fromUtf8(bytes).trimmed();if(s.isEmpty())return;m_log->appendPlainText(s);
    QMap<QString,QString> fields;auto matches=QRegularExpression("(?:^|\\s)([A-Za-z0-9_]+)=([^\\s]+)").globalMatch(s);
    while(matches.hasNext()){const auto match=matches.next();fields[match.captured(1)]=match.captured(2);}
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
    QDir maps(m_mapsDirectory);if(maps.dirName().compare("Maps",Qt::CaseInsensitive)!=0)return fail(QStringLiteral("Для запекания задайте каталог Maps активного мода."));maps.cdUp();const QString mod=maps.absolutePath();
    if(m_gameRoot.isEmpty()||!QDir(m_gameRoot).exists())return fail(QStringLiteral("Не задан корректный корень игры."));
    m_outputRoot=outputRootOverride.isEmpty()?mod:QDir(outputRootOverride).absolutePath();const QString output=QDir(m_outputRoot).filePath("Maps/Nav64");
    m_requestMpr=m_mpr;m_requestMob=m_mob;m_backups.clear();
    const QString prefix=QFileInfo(m_mpr).completeBaseName().toLower()+"--"+QFileInfo(m_mob).completeBaseName().toLower()+".";
    for(const auto& entry:QDir(output).entryInfoList({"*.nav64"},QDir::Files)){if(!all&&!entry.fileName().startsWith(prefix,Qt::CaseInsensitive))continue;const auto backup=CBackupManager().snapshotBeforeSave(entry);if(!backup.success){m_backups.clear();return fail(QStringLiteral("Перезапись запрещена: не создан бэкап ")+entry.fileName()+": "+backup.error);}m_backups.append(backup);m_log->appendPlainText(QStringLiteral("BACKUP ")+entry.fileName()+" -> "+backup.backupPath);}
    QStringList args{"--game-root",m_gameRoot,"--mod",mod,"--output-root",m_outputRoot};if(all)args<<"--all";else args<<"--map"<<QFileInfo(m_mpr).completeBaseName()<<"--mob"<<QFileInfo(m_mob).completeBaseName();
    m_stdout.clear();m_stderr.clear();m_done=false;m_errors=false;m_cancelled=false;m_log->appendPlainText(QStringLiteral("Вывод: ")+output);
    m_completedProfiles=0;m_progress->setRange(0,0);m_status->setText(QStringLiteral("Запекание: подготовка геометрии…"));m_heightOutliers->hide();
    m_process->setWorkingDirectory(m_gameRoot);m_process->start(exe,args);controls();return true;
}
void CNavmeshPanel::cancelBake(){if(!busy())return;m_cancelled=true;m_log->appendPlainText(QStringLiteral("Отмена: запекатель остановлен; уже записанные файлы сохранены, прежние версии — в бэкапах."));m_process->terminate();QTimer::singleShot(2000,m_process,[this](){if(m_cancelled&&busy())m_process->kill();});}
