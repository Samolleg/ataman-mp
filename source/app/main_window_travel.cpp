#include "main_window.h"
#include "travel_panel.h"
#include "travel_document.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "world_coordinate_edit.h"
#include "staged_save_validator.h"
#include "endurance_document.h"
#include <QLabel>
#include <QDockWidget>
#include <QMenu>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QFile>
#include <QDir>
#include <functional>
#include <limits>
#include <QTemporaryDir>
#include <QPushButton>
#include <QApplication>
#include <QMouseEvent>
#include "res_file.h"

namespace {
class TravelUndo final : public QUndoCommand {
public:
    TravelUndo(QSharedPointer<CTravelDocument> document,QByteArray before,QByteArray after,
               const QString& label,std::function<void()> refresh):QUndoCommand(label),
        doc(document),oldBytes(before),newBytes(after),changed(refresh){}
    int id() const override {return 0x545256;}
    void undo() override {doc->restore(oldBytes);changed();}
    void redo() override {doc->restore(newBytes);changed();}
private:
    QSharedPointer<CTravelDocument> doc;QByteArray oldBytes,newBytes;std::function<void()> changed;
};
int nextExit(const SMapTableZone& zone) {
    QSet<int> ids;for(const auto& e:zone.exits)ids.insert(e.exitId);
    for(int id=1;id<std::numeric_limits<int>::max();++id)if(!ids.contains(id))return id;
    return -1;
}
QString resolvedResource(const QVector<QString>& roots,QString name,const QString& extension) {
    if(!name.endsWith(extension,Qt::CaseInsensitive))name+=extension;
    for(const auto& root:roots)for(const auto& file:QDir(root).entryInfoList(QDir::Files|QDir::Readable))
        if(file.fileName().compare(name,Qt::CaseInsensitive)==0)return file.canonicalFilePath();
    return QString();
}
QString resourceName(const QString& path) {
    const QFileInfo file(path);const auto suffix=file.suffix().toLower();
    return suffix=="mpr" || suffix=="mob"?file.completeBaseName():file.fileName();
}
}

void MainWindow::createTravelEditor(QMenu* menu)
{
    m_travelDock=new QDockWidget(tr("Входы и переходы"),this);
    m_travelDock->setObjectName("TravelEditorDock");
    m_travelPanel=new CTravelPanel(m_travelDock);m_travelDock->setWidget(m_travelPanel);
    addDockWidget(Qt::RightDockWidgetArea,m_travelDock);m_travelDock->hide();
    menu->addAction(m_travelDock->toggleViewAction());
    connect(m_travelDock,&QDockWidget::visibilityChanged,this,[this](bool visible){
        if(visible){m_pView->prepareDocumentTransition();m_pView->cancelWorldMapPlacement();m_pView->clearObjectSelection();setCustomSpawnPicking(false);}
        if(visible)hideLandscapeTools();
        m_pView->setTravelEditing(visible);
        if(visible)refreshTravelContext();else updateEntranceOverlays();
    });
    connect(m_travelPanel,&CTravelPanel::zoneSelected,this,[this](int index){
        if(!m_travelDocument)return;
        const auto zones=m_travelDocument->table().zones();
        const QString loaded=CLandscape::getInstance()->filePath().canonicalFilePath();
        if(index<0 || index>=zones.size() || loaded.isEmpty()
           || resolvedResource(activeMapRoots(),zones[index].mprName,".mpr").compare(loaded,Qt::CaseInsensitive)!=0){
            refreshTravelEditor();m_travelPanel->setStatus(tr("Сначала откройте карту выбранной зоны."));return;}
        m_travelZone=index;m_travelExit=-1;refreshTravelEditor();
    });
    connect(m_travelPanel,&CTravelPanel::exitSelected,this,[this](int index){m_pView->cancelTravelGesture();m_travelExit=index;refreshTravelAreas();});
    connect(m_travelPanel,&CTravelPanel::visibilityChanged,this,&MainWindow::refreshTravelAreas);
    connect(m_pView,&CView::travelDrawingStateChanged,m_travelPanel,&CTravelPanel::setDrawingState);
    connect(m_travelPanel,&CTravelPanel::focusRequested,this,[this](int index,bool remove){
        if(!m_travelDocument || m_travelZone<0)return;
        const auto zone=m_travelDocument->table().zones()[m_travelZone];
        if(index<0 || index>=zone.exits.size())return;
        m_pView->cancelTravelGesture();m_travelExit=index;
        const auto exit=zone.exits[index];
        if(remove && !exit.hasRemoveRectangle)remove=false;
        else if(!remove && !exit.hasDeployRectangle)remove=true;
        if(remove?!exit.hasRemoveRectangle:!exit.hasDeployRectangle){
            m_travelPanel->setStatus(tr("У этой записи пока нет области. Сначала нарисуйте её."));return;}
        m_travelPanel->revealArea(remove);refreshTravelAreas();
        m_pView->focusTravelArea(index,remove);
    });
    connect(m_travelPanel,&CTravelPanel::exitEdited,this,[this](int index,const SMapTableExit& exit){
        if(!m_travelDocument || m_travelZone<0)return;
        auto zone=m_travelDocument->table().zones()[m_travelZone];
        if(index<0 || index>=zone.exits.size())return;
        zone.exits[index]=exit;changeTravelZone(m_travelZone,zone,tr("Изменить вход/переход"));
    });
    connect(m_travelPanel,&CTravelPanel::incomingRequested,this,[this](int sourceZone,int sourceExit){
        if(!m_travelDocument || m_travelZone<0 || m_travelExit<0)return;
        const auto zones=m_travelDocument->table().zones();
        if(m_travelZone>=zones.size() || m_travelExit>=zones[m_travelZone].exits.size()
           || sourceZone<0 || sourceZone>=zones.size() || sourceZone==m_travelZone
           || sourceExit<0 || sourceExit>=zones[sourceZone].exits.size())return;
        const auto& target=zones[m_travelZone].exits[m_travelExit];
        if(!target.hasDeployRectangle){m_travelPanel->setStatus(tr("Сначала задайте область появления здесь."));return;}
        if(m_travelDocument->uniqueZone(zones[m_travelZone].zoneId)!=m_travelZone
           || m_travelDocument->uniqueZone(zones[sourceZone].zoneId)!=sourceZone)return;
        int targetCount=0;for(const auto& e:zones[m_travelZone].exits)if(e.exitId==target.exitId)++targetCount;
        if(targetCount!=1){m_travelPanel->setStatus(tr("Номер появления неоднозначен."));return;}
        auto source=zones[sourceZone];
        int sourceCount=0;for(const auto& e:source.exits)if(e.exitId==source.exits[sourceExit].exitId)++sourceCount;
        if(sourceCount!=1){m_travelPanel->setStatus(tr("Номер записи на другой карте неоднозначен."));return;}
        source.exits[sourceExit].destinationZoneId=zones[m_travelZone].zoneId;
        source.exits[sourceExit].destinationExitId=target.exitId;
        changeTravelZone(sourceZone,source,tr("Направить переход к появлению"));
    });
    connect(m_travelPanel,&CTravelPanel::drawRequested,this,[this](bool remove,bool create){
        if(!m_travelDocument || m_travelZone<0){m_travelPanel->setDrawingState(false,remove,create);m_travelPanel->setStatus(tr("Сначала выберите или зарегистрируйте текущую карту."));return;}
        if(!create && m_travelExit<0)return;
        m_pView->beginTravelArea(remove,create);
    });
    connect(m_pView,&CView::travelAreaSelected,this,[this](int index,bool* accepted){
        if(!m_travelPanel->flush()){*accepted=false;return;}m_travelExit=index;refreshTravelEditor();
    });
    connect(m_pView,&CView::travelAreaCommitted,this,[this](int index,bool remove,const QRectF& rect,bool create){
        if(!m_travelDocument || m_travelZone<0)return;
        auto zone=m_travelDocument->table().zones()[m_travelZone];
        if(create){SMapTableExit e;e.exitId=nextExit(zone);e.destinationZoneId="none";index=zone.exits.size();zone.exits.append(e);}
        if(index<0 || index>=zone.exits.size())return;
        auto& e=zone.exits[index];
        if(remove){e.hasRemoveRectangle=true;e.removeRectangle=rect;}
        else {e.hasDeployRectangle=true;e.deployRectangle=rect;}
        m_travelExit=index;changeTravelZone(m_travelZone,zone,create?tr("Создать вход/переход"):tr("Переместить область"));
    });
    connect(m_travelPanel,&CTravelPanel::deleteRequested,this,[this](){
        if(!m_travelDocument || m_travelZone<0 || m_travelExit<0)return;
        auto zone=m_travelDocument->table().zones()[m_travelZone];
        if(m_travelExit>=zone.exits.size())return;
        if(QMessageBox::question(this,tr("Удалить запись"),tr("Удалить выбранную запись вместе с областями появления и выхода?"))!=QMessageBox::Yes)return;
        zone.exits.removeAt(m_travelExit);m_travelExit=-1;changeTravelZone(m_travelZone,zone,tr("Удалить вход/переход"));
    });
    connect(m_travelPanel,&CTravelPanel::registerRequested,this,&MainWindow::registerTravelMap);
    connect(m_travelPanel,&CTravelPanel::reverseRequested,this,&MainWindow::createReverseTravel);
}
void MainWindow::refreshTravelContext()
{
    if(!m_travelPanel)return;
    m_pView->cancelTravelGesture();
    const QString path=m_mapCatalog.sourceArchive;
    const QString key=QFileInfo(path).absoluteFilePath().toCaseFolded();
    m_travelDocument.clear();m_travelZone=-1;m_travelExit=-1;
    QString error;
    if(!path.isEmpty() && CLandscape::getInstance()->isMprLoad()){
        m_travelDocument=m_travelDocuments.value(key);
        if(m_travelDocument && !m_travelDocument->dirty())m_travelDocument->open(path,&error);
        if(!m_travelDocument){
            auto doc=QSharedPointer<CTravelDocument>::create();
            if(doc->open(path,&error)){m_travelDocuments.insert(key,doc);m_travelDocument=doc;}
        }
        if(m_travelDocument){
            m_travelZone=m_travelDocument->uniqueZone(m_mapTestEntry.zoneId);
            const auto zones=m_travelDocument->table().zones();
            const auto loaded=CLandscape::getInstance()->filePath().canonicalFilePath();
            if(m_travelZone>=0 && (loaded.isEmpty()
                || resolvedResource(activeMapRoots(),zones[m_travelZone].mprName,".mpr").compare(loaded,Qt::CaseInsensitive)!=0))m_travelZone=-1;
        }
    }
    refreshTravelEditor();
    if(!error.isEmpty())m_travelPanel->setStatus(error);
}
void MainWindow::refreshTravelEditor()
{
    if(!m_travelPanel)return;
    const auto zones=m_travelDocument?m_travelDocument->table().zones():QVector<SMapTableZone>();
    if(m_travelZone>=zones.size())m_travelZone=-1;
    if(m_travelZone>=0 && m_travelExit>=zones[m_travelZone].exits.size())m_travelExit=-1;
    m_travelPanel->populate(zones,m_travelZone,m_travelExit,m_travelDocument && m_travelDocument->dirty());
    if(!m_travelDocument)m_travelPanel->setStatus(tr("Откройте карту профиля с доступным texts.res / map.txt."));
    else if(m_travelZone<0)m_travelPanel->setStatus(tr("У текущей карты нет однозначной записи map.txt. Выберите соответствующую зону или зарегистрируйте карту."));
    refreshTravelAreas();updateDocumentActionAvailability();
}
void MainWindow::refreshTravelAreas()
{
    if(!m_travelPanel || !m_pView->travelEditing())return;
    QVector<STravelArea> areas;
    if(m_travelDocument && m_travelZone>=0){
        const auto zone=m_travelDocument->table().zones()[m_travelZone];
        for(int i=0;i<zone.exits.size();++i){
            const auto e=zone.exits[i];
            for(int k=0;k<2;++k){
                if(k?(!e.hasRemoveRectangle||!m_travelPanel->showRemove()):(!e.hasDeployRectangle||!m_travelPanel->showDeploy()))continue;
                STravelArea area;area.exitIndex=i;area.remove=k;area.rectangle=k?e.removeRectangle:e.deployRectangle;
                area.label=(k?tr("Выход %1 → %2:%3").arg(e.exitId).arg(e.destinationZoneId).arg(e.destinationExitId):tr("Появление %1").arg(e.exitId));
                areas.append(area);
            }
        }
    }
    m_pView->setTravelAreas(areas,m_travelExit);
}
void MainWindow::recordTravelEdit(const QByteArray& before,const QString& label)
{
    if(before==m_travelDocument->snapshot())return;
    QPointer<MainWindow> window(this);
    m_undoStack->push(new TravelUndo(m_travelDocument,before,m_travelDocument->snapshot(),label,[window](){if(window)window->refreshTravelEditor();}));
}
void MainWindow::changeTravelZone(int index,const SMapTableZone& zone,const QString& label)
{
    const auto before=m_travelDocument->snapshot();QString error;
    if(!m_travelDocument->replaceZone(index,zone,&error)){m_travelPanel->setStatus(error);return;}
    recordTravelEdit(before,label);
}
void MainWindow::registerTravelMap()
{
    if(!m_travelDocument || !CLandscape::getInstance()->isMprLoad())return;
    auto* mob=mapTestStagingMob();
    if(!mob){m_travelPanel->setStatus(tr("Для регистрации откройте основной MOB карты."));return;}
    const auto mprFile=CLandscape::getInstance()->filePath();const auto mobFile=mob->filePath();
    if(mprFile.canonicalFilePath().isEmpty() || mobFile.canonicalFilePath().isEmpty()
       || resolvedResource(activeMapRoots(),mprFile.fileName(),".mpr").compare(mprFile.canonicalFilePath(),Qt::CaseInsensitive)!=0
       || resolvedResource(activeMapRoots(),mobFile.fileName(),".mob").compare(mobFile.canonicalFilePath(),Qt::CaseInsensitive)!=0){
        m_travelPanel->setStatus(tr("Сначала сохраните MPR/MOB и добавьте их папки в пути карт текущего профиля. Имена должны разрешаться именно в открытые файлы."));return;}
    QDialog dialog(this);dialog.setWindowTitle(tr("Запись карты в map.txt"));QFormLayout form(&dialog);
    QLineEdit id,allod;id.setText(m_mapTestEntry.zoneId);allod.setText(m_mapTestEntry.allodId);
    QComboBox type;type.addItems({"game","brief"});
    QLineEdit mpr(resourceName(CLandscape::getInstance()->filePath().absoluteFilePath())),mobName(resourceName(mob->filePath().absoluteFilePath()));
    mpr.setReadOnly(true);mobName.setReadOnly(true);
    form.addRow(tr("Уникальный ID зоны:"),&id);form.addRow(tr("ID аллода:"),&allod);form.addRow(tr("Тип:"),&type);form.addRow("MPR:",&mpr);form.addRow("MOB:",&mobName);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form.addRow(&buttons);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    SMapTableZone zone;zone.zoneId=id.text().trimmed();zone.allodId=allod.text().trimmed();zone.zoneType=type.currentText();zone.mprName=mpr.text();zone.mobName=mobName.text();
    const auto before=m_travelDocument->snapshot();QString error;
    if(!m_travelDocument->appendZone(zone,&error)){QMessageBox::warning(this,tr("Регистрация отклонена"),error);return;}
    m_travelZone=m_travelDocument->table().zones().size()-1;m_travelExit=-1;recordTravelEdit(before,tr("Зарегистрировать карту"));
}
void MainWindow::createReverseTravel()
{
    if(!m_travelDocument || m_travelZone<0 || m_travelExit<0)return;
    const auto source=m_travelDocument->table().zones()[m_travelZone];const auto exit=source.exits[m_travelExit];
    if(!exit.hasDeployRectangle){m_travelPanel->setStatus(tr("Сначала задайте область появления здесь — она нужна для возвращения партии."));return;}
    const int target=m_travelDocument->uniqueZone(exit.destinationZoneId);
    if(target<0){m_travelPanel->setStatus(tr("Выберите однозначную целевую зону."));return;}
    auto zone=m_travelDocument->table().zones()[target];int index=-1;
    for(int i=0;i<zone.exits.size();++i)if(zone.exits[i].exitId==exit.destinationExitId){if(index>=0){m_travelPanel->setStatus(tr("Целевой вход неоднозначен."));return;}index=i;}
    if(exit.destinationExitId<=0){m_travelPanel->setStatus(tr("Задайте положительный номер целевого входа."));return;}
    SMapTableExit reverse;if(index>=0)reverse=zone.exits[index];else reverse.exitId=exit.destinationExitId;
    QDialog dialog(this);dialog.setWindowTitle(tr("Обратный переход из %1").arg(zone.zoneId));QFormLayout form(&dialog);
    QLabel note(tr("Локальные координаты целевой карты. Будут заданы обе области; затем их можно независимо изменить на той карте."));note.setWordWrap(true);form.addRow(&note);
    const QRectF r=reverse.hasDeployRectangle?reverse.deployRectangle:QRectF(0,0,1,1);
    CWorldCoordinateEdit x(r.x()),y(r.y()),w(r.width()),h(r.height());form.addRow("X:",&x);form.addRow("Y:",&y);form.addRow(tr("Ширина:"),&w);form.addRow(tr("Высота:"),&h);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);form.addRow(&buttons);connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    double a,b,c,d;if(!x.coordinate(&a)||!y.coordinate(&b)||!w.coordinate(&c)||!h.coordinate(&d)||c<=0||d<=0){m_travelPanel->setStatus(tr("Неверный прямоугольник."));return;}
    if(index>=0 && QMessageBox::question(this,tr("Изменить существующий вход"),tr("Перезаписать связь и обе области входа %1 зоны %2?").arg(reverse.exitId).arg(zone.zoneId))!=QMessageBox::Yes)return;
    reverse.destinationZoneId=source.zoneId;reverse.destinationExitId=exit.exitId;reverse.hasDeployRectangle=reverse.hasRemoveRectangle=true;reverse.deployRectangle=reverse.removeRectangle=QRectF(a,b,c,d);
    if(index<0)zone.exits.append(reverse);else zone.exits[index]=reverse;
    changeTravelZone(target,zone,tr("Создать обратный переход"));
}
bool MainWindow::resolveTravelClose()
{
    if(m_travelPanel && !m_travelPanel->flush())return false;
    bool dirty=false;for(const auto& doc:m_travelDocuments)dirty|=doc->dirty();if(!dirty)return true;
    const auto choice=QMessageBox::question(this,tr("Изменены входы и переходы"),tr("Сохранить карту и изменения map.txt перед продолжением?"),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    if(choice==QMessageBox::Cancel)return false;
    if(choice==QMessageBox::Save)return saveMapWithTravel();
    for(const auto& doc:m_travelDocuments)doc->discard();refreshTravelEditor();return true;
}

bool MainWindow::saveMapWithTravel(const QString& backupRoot)
{
    m_pView->cancelTravelGesture();
    if(m_travelPanel && !m_travelPanel->flush())return false;
    QVector<SMapSaveFile> files;QString error;
    QVector<QSharedPointer<CTravelDocument>> changed;
    for(const auto& doc:m_travelDocuments)if(doc->dirty()){
        SMapSaveFile file;if(!doc->prepareSave(&file,&error)){QMessageBox::warning(this,tr("Переходы не сохранены"),error);return false;}
        files.append(file);changed.append(doc);
    }
    if(changed.isEmpty())return false;
    CMob* mob=m_pView->currentMob();QByteArray mobBytes;
    if(mob && mob->isDirty()){
        CValidationReport report;QString saveClass;
        if(!mob->buildValidatedCandidate(&mobBytes,&report,&saveClass)){
            QMessageBox::warning(this,tr("MOB не сохранён"),report.toText());return false;}
        const auto checked=validateStagedSave(mobBytes,mob->filePath());
        if(!checked.success){QMessageBox::warning(this,tr("MOB не сохранён"),checked.error);return false;}
        SMapSaveFile file;file.path=mob->filePath().absoluteFilePath();file.before=mob->originalData();file.after=mobBytes;files.append(file);
    }
    if(!m_pView->saveMapCompanions(files,&error,backupRoot)){
        QMessageBox::warning(this,tr("Карта не сохранена"),error);return false;}
    for(const auto& doc:changed)doc->acceptSaved();
    if(mob && !mobBytes.isEmpty())mob->adoptCommittedBytes(mobBytes);
    refreshMapsBrowser();refreshTravelEditor();updateLoadedMapPanel();rebuildWindowTitle();
    return true;
}

bool MainWindow::hasTravelChanges() const
{
    for(const auto& doc:m_travelDocuments)if(doc->dirty())return true;
    return false;
}

bool MainWindow::flushTravelEditor(){return !m_travelPanel || m_travelPanel->flush();}

bool MainWindow::travelHistoryPending(bool redo) const
{
    const int index=m_undoStack->index()-(redo?0:1);
    return index>=0 && index<m_undoStack->count() && m_undoStack->command(index)->id()==0x545256;
}

// Invoked only by --ui-widget-check with its hidden, isolated production window.
bool travelEditorIntegrationCheck(MainWindow* window)
{
    QTemporaryDir scratch;if(!scratch.isValid())return false;
    const QString path=scratch.filePath("texts.res");
    CResFile archive;archive.addFiledata("map.txt",
        "#zone test island game\r\n#res\r\ntest test\r\n#exit 1\r\nnone 0\r\n#deploy\r\n1 2 3 4\r\n"
        "#zone other island game\r\n#res\r\nother other\r\n#exit 6\r\ntest 4\r\n#remove\r\n10 20 30 40\r\n");
    archive.addFiledata("unrelated.bin","untouched");archive.saveToFile(path);
    auto doc=QSharedPointer<CTravelDocument>::create();QString error;
    if(!doc->open(path,&error) || doc->table().zones().size()!=2)return false;
    const auto initial=doc->snapshot();
    emit window->m_travelDock->visibilityChanged(true);
    bool passed=window->m_pView->travelEditing();
    window->m_travelDocument=doc;window->m_travelDocuments.insert(path,doc);
    window->m_travelZone=0;window->m_travelExit=0;window->refreshTravelEditor();
    for(int type=0;type<2;++type){
        auto* button=window->m_travelPanel->findChild<QPushButton*>(QString("travelAdd%1").arg(type));
        if(!button)return false;
        button->click();
        passed=passed && button->isChecked() && window->m_pView->cursor().shape()==Qt::CrossCursor && !doc->dirty();
        QMouseEvent hover(QEvent::MouseMove,QPointF(10,10),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(window->m_pView,&hover);
        passed=passed && window->m_pView->cursor().shape()==Qt::CrossCursor;
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
        QApplication::sendEvent(window->m_pView,&escape);
        passed=passed && !button->isChecked() && !doc->dirty();
    }
    const QRectF rectangle(7.125,8.25,9.5,10.75);
    emit window->m_pView->travelAreaCommitted(0,false,rectangle,false);
    passed=passed && doc->dirty() && doc->table().zones()[0].exits[0].deployRectangle==rectangle;
    window->on_actionUndo_triggered();passed=passed && doc->snapshot()==initial;
    window->on_actionRedo_triggered();passed=passed && doc->table().zones()[0].exits[0].deployRectangle==rectangle;
    emit window->m_pView->travelAreaCommitted(-1,true,QRectF(20,20,4,5),true);
    passed=passed && doc->table().zones()[0].exits.size()==2
        && doc->table().zones()[0].exits[1].exitId==2
        && doc->table().zones()[0].exits[1].hasRemoveRectangle
        && !doc->table().zones()[0].exits[1].hasDeployRectangle;
    QVector3D camera,pivot,rotation;
    emit window->m_travelPanel->focusRequested(1,false); // no deploy: use remove
    passed=passed && window->m_pView->captureCameraLocation(&camera,&pivot,&rotation)
        && pivot==QVector3D(22,22.5f,0);
    emit window->m_travelPanel->focusRequested(0,false);
    passed=passed && window->m_pView->captureCameraLocation(&camera,&pivot,&rotation)
        && pivot==QVector3D(rectangle.center().x(),rectangle.center().y(),0);
    emit window->m_travelDock->visibilityChanged(false);
    passed=passed && !window->m_pView->travelEditing();
    window->on_actionUndo_triggered();passed=passed && doc->table().zones()[0].exits.size()==1;
    const auto beforeLink=doc->snapshot();
    window->m_travelExit=0;window->refreshTravelEditor();
    auto* sources=window->m_travelPanel->findChild<QComboBox*>("travelSource");
    auto* link=window->m_travelPanel->findChild<QPushButton*>("travelLinkIncoming");
    if(!sources || !link || sources->count()!=2)return false;
    sources->setCurrentIndex(1);link->click();
    passed=passed && doc->table().zones()[1].exits[0].destinationZoneId=="test"
        && doc->table().zones()[1].exits[0].destinationExitId==1
        && doc->table().zones()[1].exits[0].removeRectangle==QRectF(10,20,20,20)
        && doc->table().zones()[0].exits[0].destinationZoneId=="none";
    window->on_actionUndo_triggered();passed=passed && doc->snapshot()==beforeLink;
    window->on_actionRedo_triggered();
    passed=passed && window->saveMapWithTravel(scratch.filePath("backups")) && !doc->dirty();
    CTravelDocument reopened;
    passed=passed && reopened.open(path,&error) && reopened.snapshot()==doc->snapshot();
    window->on_actionUndo_triggered();passed=passed && doc->dirty() && doc->snapshot()==beforeLink;
    window->on_actionUndo_triggered();passed=passed && doc->snapshot()==initial;
    doc->discard();
    window->m_undoStack->clear();window->m_travelDocuments.clear();window->m_travelDocument.clear();
    window->m_travelZone=-1;window->m_travelExit=-1;window->refreshTravelEditor();
    return passed;
}
