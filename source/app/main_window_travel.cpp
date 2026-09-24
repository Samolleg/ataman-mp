#include "resource_browser.h"
#include <QFileSystemWatcher>
#include "main_window.h"
#include "travel_panel.h"
#include "travel_global_map.h"
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
#include <cmath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTemporaryDir>
#include <QPushButton>
#include <QApplication>
#include <QMouseEvent>
#include <QTableWidget>
#include "travel_combo.h"
#include <QHeaderView>
#include <QRegularExpression>
#include <QTimer>
#include <QPlainTextEdit>
#include <QTextCodec>
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
    m_travelDock=new QDockWidget(tr("Появления и переходы"),this);
    m_travelDock->setObjectName("TravelEditorDock");
    m_travelPanel=new CTravelPanel(m_travelDock);m_travelDock->setWidget(m_travelPanel);
    connect(m_travelPanel,&CTravelPanel::editMapTextRequested,this,[this](){
        if(!m_travelDocument){m_travelPanel->setStatus(tr("Нет открытого texts.res с map.txt."));return;}
        const QByteArray before=m_travelDocument->snapshot();
        const auto currentZones=m_travelDocument->table().zones();
        const QString currentZoneId=m_travelZone>=0 && m_travelZone<currentZones.size()
            ?currentZones[m_travelZone].zoneId:QString();
        QTextCodec* codec=QTextCodec::codecForName("CP1251");
        if(!codec || codec->fromUnicode(codec->toUnicode(before))!=before){
            QMessageBox::warning(this,tr("map.txt"),tr("Текст содержит байты, которые нельзя без потерь показать в CP1251."));return;
        }
        QDialog dialog(this);dialog.setWindowTitle(tr("Редактировать map.txt — %1").arg(m_travelDocument->path()));
        dialog.resize(900,650);
        auto* layout=new QVBoxLayout(&dialog);
        auto* note=new QLabel(tr("Текст сохраняется вместе с картой (Ctrl+S). Перед применением проверяется структура зон. Esc отменяет изменения в окне."));
        note->setWordWrap(true);layout->addWidget(note);
        auto* editor=new QPlainTextEdit(&dialog);editor->setObjectName("travelRawMapText");
        editor->setLineWrapMode(QPlainTextEdit::NoWrap);editor->setPlainText(codec->toUnicode(before));layout->addWidget(editor);
        const QString displayedOriginal=editor->toPlainText();
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
        layout->addWidget(buttons);
        connect(buttons->button(QDialogButtonBox::Ok),&QPushButton::clicked,&dialog,[&](){
            const QString value=editor->toPlainText();
            if(value==displayedOriginal){dialog.accept();return;}
            int prefix=0;
            while(prefix<displayedOriginal.size() && prefix<value.size()
                  && displayedOriginal[prefix]==value[prefix])++prefix;
            int suffix=0;
            while(suffix<displayedOriginal.size()-prefix && suffix<value.size()-prefix
                  && displayedOriginal[displayedOriginal.size()-suffix-1]==value[value.size()-suffix-1])++suffix;
            const auto byteOffset=[&](int characterOffset){
                int byte=0;
                for(int character=0;character<characterOffset && byte<before.size();++character,++byte)
                    if(before[byte]=='\r' && byte+1<before.size() && before[byte+1]=='\n')++byte;
                return byte;
            };
            const int firstByte=byteOffset(prefix);
            const int lastByte=byteOffset(displayedOriginal.size()-suffix);
            QByteArray replacement=codec->fromUnicode(value.mid(prefix,value.size()-prefix-suffix));
            if(codec->toUnicode(replacement)!=value.mid(prefix,value.size()-prefix-suffix)){
                QMessageBox::warning(&dialog,tr("map.txt"),tr("Некоторые символы нельзя записать в CP1251."));return;
            }
            if(before.contains("\r\n"))replacement.replace("\n","\r\n");
            QByteArray bytes=before.left(firstByte)+replacement+before.mid(lastByte);
            CMapTableDocument parsed;QStringList errors;
            if(!parsed.loadBytes(bytes,&errors) || !parsed.validateAllZones().success){
                if(errors.isEmpty())errors=parsed.validateAllZones().errors;
                QMessageBox::warning(&dialog,tr("map.txt"),errors.join('\n'));return;
            }
            if(!m_travelDocument->restore(bytes)){QMessageBox::warning(&dialog,tr("map.txt"),tr("Не удалось применить текст."));return;}
            recordTravelEdit(before,tr("Редактировать map.txt"));
            m_travelZone=m_travelDocument->uniqueZone(currentZoneId.isEmpty()?m_mapTestEntry.zoneId:currentZoneId);
            m_travelExit=-1;refreshTravelEditor();dialog.accept();
        });
        connect(buttons->button(QDialogButtonBox::Cancel),&QPushButton::clicked,&dialog,&QDialog::reject);
        dialog.exec();
    });
    connect(m_travelPanel,&CTravelPanel::routeRequested,this,&MainWindow::createPairedTravelRoute);
    connect(m_travelPanel,&CTravelPanel::globalPositionRequested,this,[this](){
        if(!m_travelDocument||m_travelZone<0)return;
        const auto zones=m_travelDocument->table().zones();
        int index=m_travelZone;
        if(zones[index].zoneType!="edge") {
            if(m_travelExit<0||m_travelExit>=zones[index].exits.size())return;
            index=m_travelDocument->uniqueZone(zones[index].exits[m_travelExit].destinationZoneId);
        }
        if(index<0||zones[index].zoneType!="edge") {
            m_travelPanel->setStatus(tr("У выбранной записи нет связующей edge-зоны. Создайте маршрут между картами."));return;
        }
        QVector3D position;QString error;const bool present=travelGlobalPosition(zones[index],&position);
        if(!chooseTravelGlobalPosition(this,zones,zones[index].allodId,zones[index].zoneId,&position,present,&error)) {
            if(!error.isEmpty())m_travelPanel->setStatus(error);return;
        }
        const auto before=m_travelDocument->snapshot();
        if(!m_travelDocument->setZonePosition(index,position,&error)){m_travelPanel->setStatus(error);return;}
        recordTravelEdit(before,tr("Переместить метку маршрута"));
    });
    addDockWidget(Qt::RightDockWidgetArea,m_travelDock);m_travelDock->hide();
    menu->addAction(m_travelDock->toggleViewAction());
    connect(m_travelDock,&QDockWidget::visibilityChanged,this,[this](bool visible){
        if(m_shutdownInProgress || !m_pView)return;
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
        if(remove && !exit.hasEffectiveRemove())remove=false;
        else if(!remove && !exit.hasDeployRectangle)remove=true;
        if(remove?!exit.hasEffectiveRemove():!exit.hasDeployRectangle){
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
        if(remove){
            if(!e.hasRemoveRectangle){e.removeExitAngle=e.effectiveRemoveAngle();e.hasRemoveExitAngle=e.removeExitAngle!=0;}
            e.hasRemoveRectangle=true;e.removeRectangle=rect;
        }
        else {e.hasDeployRectangle=true;e.deployRectangle=rect;}
        m_travelExit=index;changeTravelZone(m_travelZone,zone,create?tr("Создать вход/переход"):tr("Переместить область"));
    });
    connect(m_travelPanel,&CTravelPanel::deleteRequested,this,[this](){
        if(!m_travelDocument || m_travelZone<0 || m_travelExit<0)return;
        const auto zones=m_travelDocument->table().zones();
        if(m_travelZone>=zones.size() || m_travelExit>=zones[m_travelZone].exits.size())return;
        const auto& zone=zones[m_travelZone];const int ordinal=m_travelExit+1;
        QStringList incoming;
        for(int z=0;z<zones.size();++z)for(int i=0;i<zones[z].exits.size();++i) {
            if(z==m_travelZone && i==m_travelExit)continue;
            const auto& e=zones[z].exits[i];
            if(e.destinationZoneId.compare(zone.zoneId,Qt::CaseInsensitive)==0 && e.destinationExitId==ordinal)
                incoming.append(tr("%1, запись №%2").arg(zones[z].zoneId).arg(i+1));
        }
        QString message=tr("Удалить запись %1 №%2 вместе с её областями появления и перехода?").arg(zone.zoneId).arg(ordinal);
        if(!incoming.isEmpty())message+=tr("\n\nВходящих ссылок: %1. У перечисленных записей будет установлено «Без перехода»; их области останутся на месте.").arg(incoming.size());
        message+=tr("\n\nСсылки на последующие записи будут пересчитаны. Действие можно отменить через Undo. Ctrl+S сохранит изменения.");
        QMessageBox confirm(QMessageBox::Question,tr("Удалить запись"),message,QMessageBox::Yes|QMessageBox::Cancel,this);
        confirm.setObjectName("travelDeleteConfirm");confirm.setDefaultButton(QMessageBox::Cancel);
        confirm.button(QMessageBox::Yes)->setText(tr("Удалить"));
        if(!incoming.isEmpty())confirm.setDetailedText(incoming.join("\n"));
        if(confirm.exec()!=QMessageBox::Yes)return;
        const auto before=m_travelDocument->snapshot();QString error;
        const int selected=m_travelExit;
        if(!m_travelDocument->deleteExit(m_travelZone,selected,&error)) {
            m_travelPanel->setStatus(error);
            QMessageBox::warning(this,tr("Запись не удалена"),error);return;
        }
        m_pView->cancelTravelGesture();
        m_travelExit=qMin(selected,m_travelDocument->table().zones()[m_travelZone].exits.size()-1);
        recordTravelEdit(before,tr("Удалить запись и отключить входящие переходы"));
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
    updateEntranceOverlays();updateDocumentActionAvailability();
}
void MainWindow::refreshTravelAreas()
{
    if(m_shutdownInProgress || !m_travelPanel || !m_pView || !m_pView->travelEditing())return;
    QVector<STravelArea> areas;
    if(m_travelDocument && m_travelZone>=0){
        const auto zone=m_travelDocument->table().zones()[m_travelZone];
        for(int i=0;i<zone.exits.size();++i){
            const auto e=zone.exits[i];
            for(int k=0;k<2;++k){
                if(k?(!e.hasEffectiveRemove()||!m_travelPanel->showRemove()):(!e.hasDeployRectangle||!m_travelPanel->showDeploy()))continue;
                STravelArea area;area.exitIndex=i;area.remove=k;area.rectangle=k?e.effectiveRemove():e.deployRectangle;
                area.angle=k?e.effectiveRemoveAngle():0;
                area.label=(k?tr("Переход %1 → %2:%3").arg(e.exitId).arg(e.destinationZoneId).arg(e.destinationExitId):tr("Появление %1").arg(e.exitId));
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
void MainWindow::createPairedTravelRoute()
{
    if(!m_travelDocument || m_travelZone<0){m_travelPanel->setStatus(tr("Сначала откройте зарегистрированную игровую карту."));return;}
    const auto zones=m_travelDocument->table().zones();
    const auto source=zones[m_travelZone];
    if(source.zoneType!="game"){m_travelPanel->setStatus(tr("Выберите игровую карту (game)."));return;}
    QDialog dialog(this);dialog.setWindowTitle(tr("Маршрут между картами"));
    dialog.setAttribute(Qt::WA_DontShowOnScreen,testAttribute(Qt::WA_DontShowOnScreen));
    dialog.resize(760,700);QVBoxLayout layout(&dialog);QFormLayout form;layout.addLayout(&form);
    QLabel note(tr("Два конца маршрута будут связаны через новую edge-зону. Выбранные существующие записи сохранят номера, но их направления и четыре области будут заменены значениями ниже. Остальные записи не меняются."));
    note.setWordWrap(true);layout.addWidget(&note);
    QComboBox sourceExit,targetZone,targetExit;
    for(auto* combo:{&sourceExit,&targetZone,&targetExit})configureTravelCombo(combo);
    const auto fillExits=[&](QComboBox& combo,const SMapTableZone& zone) {
        combo.clear();combo.addItem(tr("Новая запись (свободный номер)"),0);
        for(const auto& e:zone.exits)combo.addItem(tr("№%1 → %2:%3").arg(e.exitId).arg(e.destinationZoneId).arg(e.destinationExitId),e.exitId);
    };
    fillExits(sourceExit,source);
    if(m_travelExit>=0 && m_travelExit<source.exits.size())sourceExit.setCurrentIndex(m_travelExit+1);
    for(int i=0;i<zones.size();++i)if(i!=m_travelZone && zones[i].zoneType=="game"
        && zones[i].allodId.compare(source.allodId,Qt::CaseInsensitive)==0)targetZone.addItem(zones[i].zoneId,i);
    if(targetZone.count()==0){m_travelPanel->setStatus(tr("Нет другой игровой карты в этом аллоде."));return;}
    if(m_travelExit>=0 && m_travelExit<source.exits.size()) {
        const int found=targetZone.findText(source.exits[m_travelExit].destinationZoneId);
        if(found>=0)targetZone.setCurrentIndex(found);
    }
    form.addRow(tr("Текущая карта: %1").arg(source.zoneId),&sourceExit);
    form.addRow(tr("Другая карта:"),&targetZone);form.addRow(tr("Запись на другой карте:"),&targetExit);
    QTableWidget areas(4,4);areas.setObjectName("routeAreas");areas.setHorizontalHeaderLabels({"X","Y",tr("Ширина"),tr("Высота")});
    areas.setVerticalHeaderLabels({tr("Здесь: появление"),tr("Здесь: переход"),tr("Там: появление"),tr("Там: переход")});
    areas.horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);areas.setMinimumHeight(180);layout.addWidget(&areas);
    for(int row=0;row<4;++row)for(int col=0;col<4;++col)areas.setCellWidget(row,col,new CWorldCoordinateEdit(0,&areas));
    const auto fillAreas=[&](const SMapTableZone& zone,int id,int row) {
        const SMapTableExit* entry=nullptr;for(const auto& e:zone.exits)if(e.exitId==id)entry=&e;
        for(int type=0;type<2;++type){
            const bool present=entry && (type?entry->hasRemoveRectangle:entry->hasDeployRectangle);
            const QRectF rect=entry?(type?entry->removeRectangle:entry->deployRectangle):QRectF();
            const double values[]={rect.x(),rect.y(),rect.width(),rect.height()};
            for(int col=0;col<4;++col)static_cast<CWorldCoordinateEdit*>(areas.cellWidget(row+type,col))->setText(present?QString::number(values[col],'g',17):QString());
        }
    };
    QLineEdit edgeId;edgeId.setObjectName("routeEdge");form.addRow(tr("ID новой связующей зоны:"),&edgeId);
    QLabel positionNote(tr("Положение связующей зоны на игровой глобальной карте (не метры рельефа). Выберите точку на поверхности глобальной карты либо введите известные X/Y/Z."));
    positionNote.setWordWrap(true);layout.addWidget(&positionNote);
    QHBoxLayout positionLayout;layout.addLayout(&positionLayout);
    CWorldCoordinateEdit px(0),py(0),pz(0);
    px.setObjectName("routeX");py.setObjectName("routeY");pz.setObjectName("routeZ");
    positionLayout.addWidget(new QLabel("X"));positionLayout.addWidget(&px);
    positionLayout.addWidget(new QLabel("Y"));positionLayout.addWidget(&py);
    positionLayout.addWidget(new QLabel("Z"));positionLayout.addWidget(&pz);
    QPushButton choosePosition(tr("Выбрать на глобальной карте…"));
    choosePosition.setObjectName("routeChoosePosition");layout.addWidget(&choosePosition);
    connect(&choosePosition,&QPushButton::clicked,&dialog,[&](){
        double x=0,y=0,z=0;const bool present=px.coordinate(&x)&&py.coordinate(&y)&&pz.coordinate(&z);
        QVector3D position(x,y,z);QString error;
        if(chooseTravelGlobalPosition(&dialog,zones,source.allodId,QString(),&position,present,&error)) {
            px.setText(QString::number(position.x(),'g',9));py.setText(QString::number(position.y(),'g',9));pz.setText(QString::number(position.z(),'g',9));
        } else if(!error.isEmpty())QMessageBox::warning(&dialog,tr("Глобальная карта"),error);
    });
    const auto refreshTarget=[&]() {
        const auto& target=zones[targetZone.currentData().toInt()];fillExits(targetExit,target);
        if(m_travelExit>=0 && m_travelExit<source.exits.size()) {
            const auto& e=source.exits[m_travelExit];
            if(e.destinationZoneId==target.zoneId){const int found=targetExit.findData(e.destinationExitId);if(found>=0)targetExit.setCurrentIndex(found);}
        }
        fillAreas(target,targetExit.currentData().toInt(),2);
        const QString base=source.zoneId+"_"+target.zoneId;QString name=base;int suffix=2;
        while(m_travelDocument->uniqueZone(name)>=0)name=base+QString::number(suffix++);
        edgeId.setText(name);
        px.clear();py.clear();pz.clear();
    };
    connect(&sourceExit,QOverload<int>::of(&QComboBox::activated),&dialog,[&](int){fillAreas(source,sourceExit.currentData().toInt(),0);});
    connect(&targetZone,QOverload<int>::of(&QComboBox::activated),&dialog,[&](int){refreshTarget();});
    connect(&targetExit,QOverload<int>::of(&QComboBox::activated),&dialog,[&](int){fillAreas(zones[targetZone.currentData().toInt()],targetExit.currentData().toInt(),2);});
    fillAreas(source,sourceExit.currentData().toInt(),0);refreshTarget();
    QLabel error;error.setWordWrap(true);layout.addWidget(&error);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons.button(QDialogButtonBox::Ok)->setText(tr("Создать маршрут"));layout.addWidget(&buttons);
    connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,[&](){
        for(auto* combo:{&sourceExit,&targetZone,&targetExit})
            if(combo->currentText()!=combo->itemText(combo->currentIndex())) {
                error.setText(tr("Выберите карту и записи из результатов поиска; для нового входа выберите «Новая запись»."));return;
            }
        STravelRoute route;route.sourceZone=source.zoneId;route.targetZone=zones[targetZone.currentData().toInt()].zoneId;
        route.sourceExit=sourceExit.currentData().toInt();route.targetExit=targetExit.currentData().toInt();route.edgeZone=edgeId.text().trimmed();
        QRectF rectangles[4];
        for(int row=0;row<4;++row){double v[4];for(int col=0;col<4;++col)
            if(!static_cast<CWorldCoordinateEdit*>(areas.cellWidget(row,col))->coordinate(&v[col])){error.setText(tr("Заполните все четыре области числовыми координатами."));return;}
            rectangles[row]=QRectF(v[0],v[1],v[2],v[3]);
        }
        route.sourceDeploy=rectangles[0];route.sourceRemove=rectangles[1];route.targetDeploy=rectangles[2];route.targetRemove=rectangles[3];
        double x,y,z;if(!px.coordinate(&x)||!py.coordinate(&y)||!pz.coordinate(&z)){error.setText(tr("Задайте позицию связующей зоны на игровой глобальной карте."));return;}
        route.worldMapPosition=QVector3D(x,y,z);
        const auto before=m_travelDocument->snapshot();QString message;
        if(!m_travelDocument->createPairedRoute(route,&message)){error.setText(message);return;}
        recordTravelEdit(before,tr("Создать парный маршрут"));dialog.accept();
    });
    dialog.exec();
}

bool MainWindow::resolveTravelClose()
{
    if(m_travelPanel && !m_travelPanel->flush()) {
        const auto choice=QMessageBox::question(this,tr("Незавершённые поля перехода"),
            tr("В панели переходов есть незавершённые или неверные значения. Отбросить несохранённые изменения переходов и продолжить закрытие? Остальные изменённые документы будут проверены отдельно."),
            QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel);
        if(choice!=QMessageBox::Discard)return false;
        m_pView->cancelTravelGesture();
        for(const auto& doc:m_travelDocuments)doc->discard();
        refreshTravelEditor();return true;
    }
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
    // map.txt writes are ours, not external visual-resource publications. Remove
    // the watcher before commit so its queued notification cannot clear textures
    // still referenced by the current scene. Reindex without a GPU cache reset.
    QStringList travelArchives;
    for(const auto& file:files)if(file.path.endsWith(".res",Qt::CaseInsensitive)) {
        travelArchives.append(file.path);
        if(m_resourceBrowser)m_resourceBrowser->beginInternalPublication(file.path);
    }
    const bool saved=m_pView->saveMapCompanions(files,&error,backupRoot);
    for(const auto& archive:travelArchives)
        if(m_resourceBrowser)m_resourceBrowser->finishInternalPublication(archive,saved,false);
    if(!saved){
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
    if(!travelGlobalSurfaceCheck())return false;
    QTemporaryDir scratch;if(!scratch.isValid())return false;
    const QString path=scratch.filePath("texts.res");
    CResFile archive;archive.addFiledata("map.txt",
        "#zone test island game\r\n#res\r\ntest test\r\n#exit 1\r\nnone 0\r\n#deploy\r\n1 2 3 4\r\n"
        "#zone other island game\r\n#res\r\nother other\r\n#exit 6\r\ntest 4\r\n#remove\r\n10 20 30 40\r\n");
    archive.addFiledata("unrelated.bin","untouched");archive.saveToFile(path);
    auto doc=QSharedPointer<CTravelDocument>::create();QString error;
    if(!doc->open(path,&error) || doc->table().zones().size()!=2)return false;
    int visualRefreshes=0;
    const auto publicationConnection=QObject::connect(window->m_resourceBrowser,&CResourceBrowser::activeResourcesPublished,
        window,[&](const QString&){++visualRefreshes;});
    window->m_resourceBrowser->setActiveProfileSources({path},"synthetic travel save");
    const auto initial=doc->snapshot();
    emit window->m_travelDock->visibilityChanged(true);
    bool passed=window->m_pView->travelEditing();
    window->m_travelDocument=doc;window->m_travelDocuments.insert(path,doc);
    window->m_travelZone=0;window->m_travelExit=0;window->refreshTravelEditor();
    const auto rawBefore=doc->snapshot();
    bool rawEditorOpened=false;
    QTimer::singleShot(0,window,[&](){
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        auto* editor=dialog?dialog->findChild<QPlainTextEdit*>("travelRawMapText"):nullptr;
        if(!editor){if(dialog)dialog->reject();return;}
        rawEditorOpened=true;
        editor->insertPlainText(QStringLiteral("# manual map.txt edit\r\n"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        if(dialog->isVisible())dialog->reject();
    });
    window->m_travelPanel->findChild<QPushButton*>("travelEditMapText")->click();
    passed=passed && rawEditorOpened && doc->dirty() && doc->snapshot()!=rawBefore;
    window->on_actionUndo_triggered();passed=passed && doc->snapshot()==rawBefore;
    window->on_actionRedo_triggered();passed=passed && doc->snapshot()!=rawBefore;
    window->on_actionUndo_triggered();
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
    const auto linkedBeforeDelete=doc->snapshot();
    window->m_travelZone=0;window->m_travelExit=0;window->refreshTravelEditor();
    bool cancelVisited=false;
    QTimer::singleShot(0,window,[&](){
        auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if(box && box->objectName()=="travelDeleteConfirm") {
            cancelVisited=box->detailedText().contains("other");box->button(QMessageBox::Cancel)->click();
        }
    });
    emit window->m_travelPanel->deleteRequested();
    passed=passed && cancelVisited && doc->snapshot()==linkedBeforeDelete && window->m_travelExit==0;
    bool deleteVisited=false;
    QTimer::singleShot(0,window,[&](){
        auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if(box && box->objectName()=="travelDeleteConfirm") {
            deleteVisited=true;box->button(QMessageBox::Yes)->click();
        }
    });
    emit window->m_travelPanel->deleteRequested();
    const auto deleted=doc->snapshot();
    passed=passed && deleteVisited && doc->table().zones()[0].exits.isEmpty()
        && doc->table().zones()[1].exits[0].destinationZoneId=="none" && window->m_travelExit==-1;
    window->on_actionUndo_triggered();passed=passed && doc->snapshot()==linkedBeforeDelete;
    window->on_actionRedo_triggered();passed=passed && doc->snapshot()==deleted;
    window->on_actionUndo_triggered();passed=passed && doc->snapshot()==linkedBeforeDelete;
    window->m_travelZone=0;window->m_travelExit=0;window->refreshTravelEditor();
    passed=passed && window->saveMapWithTravel(scratch.filePath("backups")) && !doc->dirty();
    CTravelDocument reopened;
    passed=passed && reopened.open(path,&error) && reopened.snapshot()==doc->snapshot();
    window->on_actionUndo_triggered();passed=passed && doc->dirty() && doc->snapshot()==beforeLink;
    window->on_actionUndo_triggered();passed=passed && doc->snapshot()==initial;
    doc->discard();
    window->m_travelZone=0;window->m_travelExit=0;window->refreshTravelEditor();
    const auto beforeRoute=doc->snapshot();
    bool routeDialogVisited=false;
    QTimer::singleShot(0,window,[&](){
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)return;
        auto* areas=dialog->findChild<QTableWidget*>("routeAreas");
        auto* buttons=dialog->findChild<QDialogButtonBox*>();
        if(!areas || !buttons){dialog->reject();return;}
        for(int row=0;row<4;++row)for(int col=0;col<4;++col)
            static_cast<QLineEdit*>(areas->cellWidget(row,col))->setText(QString::number(col<2?10+row*5:3));
        for(const auto& name:{"routeX","routeY","routeZ"}) {
            auto* field=dialog->findChild<QLineEdit*>(name);if(!field){dialog->reject();return;}field->setText("0.5");
        }
        routeDialogVisited=true;buttons->button(QDialogButtonBox::Ok)->click();
        if(dialog->isVisible())dialog->reject(); // Failed validation must not leave a test modal waiting.
    });
    window->m_travelPanel->findChild<QPushButton*>("travelCreateRoute")->click();
    passed=passed && routeDialogVisited && doc->table().zones().size()==3;
    const auto paired=doc->snapshot();
    window->on_actionUndo_triggered();passed=passed && doc->snapshot()==beforeRoute;
    window->on_actionRedo_triggered();passed=passed && doc->snapshot()==paired;
    passed=passed && window->saveMapWithTravel(scratch.filePath("backups"));
    CTravelDocument pairedReload;passed=passed && pairedReload.open(path,&error) && pairedReload.snapshot()==paired;
    doc->discard();
    window->refreshTravelEditor();
    auto* width=window->m_travelPanel->findChild<QLineEdit*>("travelRect_0_2");
    width->setText("-1");
    bool sawCancel=false;
    QTimer::singleShot(0,window,[&](){auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if(box){sawCancel=true;box->button(QMessageBox::Cancel)->click();}});
    passed= !window->resolveTravelClose() && sawCancel && passed;
    bool sawDiscard=false;
    QTimer::singleShot(0,window,[&](){auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if(box){sawDiscard=true;box->button(QMessageBox::Discard)->click();}});
    passed=window->resolveTravelClose() && sawDiscard && window->m_travelPanel->flush() && passed;
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const auto* resourceWatcher=window->m_resourceBrowser->findChild<QFileSystemWatcher*>();
    passed=passed && visualRefreshes==0 && resourceWatcher
        && resourceWatcher->files().contains(QFileInfo(path).absoluteFilePath(),Qt::CaseInsensitive);
    QObject::disconnect(publicationConnection);
    window->m_resourceBrowser->setActiveProfileSources({},"test completed");
    window->m_undoStack->clear();window->m_travelDocuments.clear();window->m_travelDocument.clear();
    window->m_travelZone=-1;window->m_travelExit=-1;window->refreshTravelEditor();
    return passed;
}
