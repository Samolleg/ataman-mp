#include "navmesh_panel.h"
#include <QtWidgets>
#include <QtConcurrent>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

CNavmeshPanel::CNavmeshPanel(QWidget* parent):QWidget(parent) {
    setObjectName("navmeshPanel");auto* layout=new QVBoxLayout(this);
    m_mode=new QComboBox(this);m_mode->setObjectName("navmeshMode");m_mode->addItems({QStringLiteral("Слой выключен"),QStringLiteral("Старый AI-граф"),QStringLiteral("Navmesh")});layout->addWidget(m_mode);
    m_profile=new QComboBox(this);m_profile->setObjectName("navmeshProfile");m_profile->addItems(nav64Profiles());m_profile->setCurrentText("human");layout->addWidget(m_profile);
    m_status=new QLabel(QStringLiteral("Откройте карту с первичным MOB"),this);m_status->setTextFormat(Qt::PlainText);m_status->setWordWrap(true);layout->addWidget(m_status);
    m_freshness=new QLabel(this);m_freshness->setTextFormat(Qt::PlainText);m_freshness->setWordWrap(true);layout->addWidget(m_freshness);
    auto* legend=new QLabel(QStringLiteral("Заливка — область материала; контур — полигоны; голубой — тайлы; красные точки — острова вне крупнейшего; оранжевый — кандидаты узких порталов."),this);legend->setWordWrap(true);layout->addWidget(legend);
    auto* hints=new QLabel(QStringLiteral("Узкий портал: ребро соединения короче диаметра профиля. Это приблизительная подсказка по уже эродированной сетке, не измерение исходного прохода. Высотные разрешения объединены."),this);hints->setWordWrap(true);hints->setToolTip(hints->text());layout->addWidget(hints);
    auto* refresh=new QPushButton(QStringLiteral("Обновить слой"),this);layout->addWidget(refresh);
    layout->addWidget(new QLabel(QStringLiteral("Запекатель EIRebuild64NavBake.exe:"),this));
    m_baker=new QLineEdit(this);m_baker->setObjectName("navmeshBakerPath");m_baker->setText(QSettings().value("nav64/bakerPath").toString());layout->addWidget(m_baker);
    auto* browse=new QPushButton(QStringLiteral("Указать путь…"),this);layout->addWidget(browse);
    m_this=new QPushButton(QStringLiteral("Запечь navmesh — эта карта"),this);m_this->setObjectName("navmeshBakeMap");layout->addWidget(m_this);
    m_all=new QPushButton(QStringLiteral("Запечь navmesh — все карты мода"),this);m_all->setObjectName("navmeshBakeAll");layout->addWidget(m_all);
    m_cancel=new QPushButton(QStringLiteral("Отмена запекания"),this);layout->addWidget(m_cancel);
    auto* navsrc=new QPushButton(QStringLiteral("Проходимость частей (.navsrc): формат не согласован"),this);navsrc->setObjectName("navmeshNavsrcReserved");navsrc->setEnabled(false);layout->addWidget(navsrc);
    m_log=new QPlainTextEdit(this);m_log->setObjectName("navmeshBakeLog");m_log->setReadOnly(true);m_log->setMaximumBlockCount(2000);m_log->setMinimumHeight(100);layout->addWidget(m_log,1);
    m_process=new QProcess(this);m_loading=new QFutureWatcher<SNav64Document>(this);
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;});
#endif
    connect(m_mode,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int i){if(i==1)emit oldGraphRequested();emit geometryChanged();if(i==2&&!m_document.valid)reload();});
    connect(m_profile,&QComboBox::currentTextChanged,this,[this](){reload();});connect(refresh,&QPushButton::clicked,this,&CNavmeshPanel::reload);
    connect(browse,&QPushButton::clicked,this,[this](){auto path=QFileDialog::getOpenFileName(this,QStringLiteral("Запекатель Nav64"),m_baker->text(),"EIRebuild64NavBake.exe (*.exe)");if(!path.isEmpty()){m_baker->setText(path);QSettings().setValue("nav64/bakerPath",path);}});
    connect(m_baker,&QLineEdit::editingFinished,this,[this](){QSettings().setValue("nav64/bakerPath",m_baker->text());});
    connect(m_this,&QPushButton::clicked,this,[this](){startBake(false);});connect(m_all,&QPushButton::clicked,this,[this](){startBake(true);});connect(m_cancel,&QPushButton::clicked,this,&CNavmeshPanel::cancelBake);
    connect(m_process,&QProcess::readyReadStandardOutput,this,[this](){drain(false);});connect(m_process,&QProcess::readyReadStandardError,this,[this](){drain(true);});
    connect(m_process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){m_lastExit=-1;m_lastSuccess=false;m_log->appendPlainText(QStringLiteral("Запекатель не запущен: ")+m_process->errorString());m_backups.clear();controls();emit bakeFinished(false);}});
    connect(m_process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        drain(false);drain(true);if(!m_stdout.isEmpty())line(m_stdout);if(!m_stderr.isEmpty())line(m_stderr);m_stdout.clear();m_stderr.clear();
        m_lastExit=code;m_lastSuccess=!m_cancelled&&status==QProcess::NormalExit&&code==0&&m_done&&!m_errors;
        m_log->appendPlainText(m_lastSuccess?QStringLiteral("Запекание завершено: exit=0 и BAKE_DONE errors=0."):QStringLiteral("Запекание не завершено: exit=%1, BAKE_DONE=%2%3").arg(code).arg(m_done).arg(m_cancelled?QStringLiteral(", отменено"):QString()));
        m_backups.clear();controls();
        if(m_lastSuccess&&m_requestMpr==m_mpr&&m_requestMob==m_mob){m_directory=QDir(m_outputRoot).filePath("Maps/Nav64");reload();}emit bakeFinished(m_lastSuccess);
    });
    connect(m_loading,&QFutureWatcher<SNav64Document>::finished,this,[this](){const auto result=m_loading->result();if(m_loadingPath==result.path){m_document=result;m_status->setText(result.valid?QStringLiteral("%1: r=%2 м, h=%3 м\n%4 тайлов, %5 полигонов, %6 островов, %7 узких порталов\n%8").arg(result.header.profile).arg(result.header.body.radius).arg(result.header.body.height).arg(result.tileCount).arg(result.polygonCount).arg(result.islandCount).arg(result.narrowCount).arg(result.path):result.error);freshness();emit geometryChanged();}else reload();});
    controls();
}
CNavmeshPanel::~CNavmeshPanel(){if(m_process->state()!=QProcess::NotRunning){m_process->kill();m_process->waitForFinished(3000);}m_loading->waitForFinished();}
bool CNavmeshPanel::layerVisible()const{return m_mode->currentIndex()==2&&m_document.valid;}
bool CNavmeshPanel::busy()const{return m_process->state()!=QProcess::NotRunning;}
void CNavmeshPanel::setBakerPath(const QString& path){m_baker->setText(path);}
QString CNavmeshPanel::bakeLog()const{return m_log->toPlainText();}
void CNavmeshPanel::controls(){m_this->setEnabled(!busy()&&!m_mpr.isEmpty()&&!m_mob.isEmpty());m_all->setEnabled(!busy()&&!m_mpr.isEmpty());m_cancel->setEnabled(busy());m_baker->setEnabled(!busy());}
void CNavmeshPanel::setScene(const QString& mpr,const QString& mob,const QString& gameRoot,const QString& effectiveMapsDirectory){
    const QString maps=effectiveMapsDirectory.isEmpty()?(mpr.isEmpty()?QString():QFileInfo(mpr).absolutePath()):effectiveMapsDirectory;
    if(mpr==m_mpr&&mob==m_mob&&gameRoot==m_gameRoot&&maps==m_mapsDirectory)return;
    m_mpr=mpr;m_mob=mob;m_gameRoot=gameRoot;m_mapsDirectory=maps;m_directory=maps.isEmpty()?QString():QDir(maps).absoluteFilePath("Nav64");
    if(m_baker->text().isEmpty())m_baker->setPlaceholderText(QDir(gameRoot).filePath("Engine64/EIRebuild64NavBake.exe"));controls();reload();
}
void CNavmeshPanel::setUnsavedChanges(bool dirty){m_unsaved=dirty;freshness();}
void CNavmeshPanel::reload(){
    m_document={};m_loadingPath.clear();emit geometryChanged();
    if(m_mpr.isEmpty()||m_mob.isEmpty()){m_status->setText(QStringLiteral("Откройте карту с первичным MOB"));freshness();return;}
    QString error;const auto path=findNav64ForScene(m_directory,m_mpr,m_mob,m_profile->currentText(),&error);m_loadingPath=path;
    if(path.isEmpty()){m_status->setText(error);freshness();return;}
    if(m_loading->isRunning())return; // finished callback starts the latest request
    m_status->setText(QStringLiteral("Чтение Nav64…"));m_loading->setFuture(QtConcurrent::run([path](){return loadNav64(path);}));
}
void CNavmeshPanel::freshness(){
    if(m_unsaved){m_freshness->setText(QStringLiteral("Карта изменена после запекания: есть несохранённые правки (приблизительно). Перед запеканием сохраните MPR и первичный MOB."));return;}
    if(!m_document.valid){m_freshness->setText(QStringLiteral("Отпечаток: не проверено"));return;}
    const auto baked=QFileInfo(m_document.path).lastModified();bool newer=false;
    QString sidecar=m_mpr;sidecar=sidecar.left(sidecar.lastIndexOf('.'))+".navsrc";
    for(const auto& path:{m_mpr,m_mob,sidecar})if(QFileInfo(path).exists()&&QFileInfo(path).lastModified()>baked)newer=true;
    m_freshness->setText(newer?QStringLiteral("Карта изменена после запекания (приблизительно: время MPR/MOB/.navsrc). Отпечаток не проверен."):QStringLiteral("По времени MPR/MOB/.navsrc изменений не найдено (приблизительно). Отпечаток геометрии не проверен."));
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
    if(s.startsWith("PROFILE "))m_status->setText(QStringLiteral("Запекание: %1, радиус %2 м, тайлов %3, %4 мс").arg(fields.value("type"),fields.value("radius"),fields.value("tiles"),fields.value("ms",fields.value("wall_ms"))));
    if(s.startsWith("MAP_DONE "))m_status->setText(QStringLiteral("Карта готова; ожидается BAKE_DONE: ")+s);
    if(s.startsWith("PROFILE ")||s.startsWith("MAP_DONE ")||s.startsWith("MAP_BEGIN "))m_log->setToolTip(s);
}
void CNavmeshPanel::drain(bool errors){auto& buffer=errors?m_stderr:m_stdout;buffer+=errors?m_process->readAllStandardError():m_process->readAllStandardOutput();int n;while((n=buffer.indexOf('\n'))>=0){line(buffer.left(n));buffer.remove(0,n+1);}if(buffer.size()>1024*1024){m_errors=true;line(buffer.left(4096));buffer.clear();}}
bool CNavmeshPanel::startBake(bool all,const QString& outputRootOverride){
    if(busy())return false;m_lastSuccess=false;m_lastExit=-1;
    auto fail=[this](const QString& message){m_log->appendPlainText(message);emit bakeFinished(false);return false;};
    if(m_mpr.isEmpty()||(!all&&m_mob.isEmpty()))return fail(QStringLiteral("Откройте карту с первичным MOB."));
    if(m_unsaved)return fail(QStringLiteral("Сначала сохраните MPR и первичный MOB: запекатель читает файлы с диска."));
    const QString exe=m_baker->text().trimmed().isEmpty()?QDir(m_gameRoot).filePath("Engine64/EIRebuild64NavBake.exe"):m_baker->text().trimmed();
    if(!QFileInfo(exe).isFile())return fail(QStringLiteral("Запекатель не найден. Укажите путь к EIRebuild64NavBake.exe: ")+exe);
    QDir maps(m_mapsDirectory);if(maps.dirName().compare("Maps",Qt::CaseInsensitive)!=0)return fail(QStringLiteral("Для запекания задайте каталог Maps активного мода."));maps.cdUp();const QString mod=maps.absolutePath();
    if(m_gameRoot.isEmpty()||!QDir(m_gameRoot).exists())return fail(QStringLiteral("Не задан корректный корень игры."));
    m_outputRoot=outputRootOverride.isEmpty()?mod:QDir(outputRootOverride).absolutePath();const QString output=QDir(m_outputRoot).filePath("Maps/Nav64");
    m_requestMpr=m_mpr;m_requestMob=m_mob;m_backups.clear();
    const QString prefix=QFileInfo(m_mpr).completeBaseName().toLower()+"--"+QFileInfo(m_mob).completeBaseName().toLower()+".";
    for(const auto& entry:QDir(output).entryInfoList({"*.nav64"},QDir::Files)){if(!all&&!entry.fileName().startsWith(prefix,Qt::CaseInsensitive))continue;const auto backup=CBackupManager().snapshotBeforeSave(entry);if(!backup.success){m_backups.clear();return fail(QStringLiteral("Перезапись запрещена: не создан бэкап ")+entry.fileName()+": "+backup.error);}m_backups.append(backup);m_log->appendPlainText(QStringLiteral("BACKUP ")+entry.fileName()+" -> "+backup.backupPath);}
    QStringList args{"--game-root",m_gameRoot,"--mod",mod,"--output-root",m_outputRoot};if(all)args<<"--all";else args<<"--map"<<QFileInfo(m_mpr).completeBaseName()<<"--mob"<<QFileInfo(m_mob).completeBaseName();
    m_stdout.clear();m_stderr.clear();m_done=false;m_errors=false;m_cancelled=false;m_log->appendPlainText(QStringLiteral("Вывод: ")+output);
    m_process->setWorkingDirectory(m_gameRoot);m_process->start(exe,args);controls();return true;
}
void CNavmeshPanel::cancelBake(){if(!busy())return;m_cancelled=true;m_log->appendPlainText(QStringLiteral("Отмена: запекатель остановлен; уже записанные файлы сохранены, прежние версии — в бэкапах."));m_process->terminate();QTimer::singleShot(2000,m_process,[this](){if(m_cancelled&&busy())m_process->kill();});}
