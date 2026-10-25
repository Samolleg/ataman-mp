#include "travel_panel.h"
#include "travel_combo.h"
#include "travel_destination.h"
#include <QToolButton>
#include <QStandardItemModel>
#include "world_coordinate_edit.h"
#include <QComboBox>
#include <QListWidget>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <cmath>

CTravelPanel::CTravelPanel(QWidget* parent):QWidget(parent)
{
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    auto* scroll=new QScrollArea(this);scroll->setWidgetResizable(true);
    auto* body=new QWidget(scroll);auto* layout=new QVBoxLayout(body);
    scroll->setWidget(body);outer->addWidget(scroll);
    auto* legend=new QLabel(tr("Синий: появление (#deploy) — где игрок окажется, прибыв сюда.\nОранжевый: переход (#remove) — область, которая уводит с этой карты."));
    legend->setWordWrap(true);legend->setToolTip(legend->text());layout->addWidget(legend);
    m_chain=new QLabel;m_chain->setObjectName("travelChain");m_chain->setWordWrap(true);
    m_chain->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(m_chain);
    m_status=new QLabel; m_status->setWordWrap(true);layout->addWidget(m_status);
    m_zoneCombo=new QComboBox;configureTravelCombo(m_zoneCombo);layout->addWidget(m_zoneCombo);
    auto* catalogToggle=new QToolButton;catalogToggle->setText(tr("Каталог карт и создание маршрутов"));catalogToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    catalogToggle->setCheckable(true);catalogToggle->setArrowType(Qt::RightArrow);layout->addWidget(catalogToggle);
    auto* catalog=new QWidget;auto* catalogLayout=new QVBoxLayout(catalog);catalogLayout->setContentsMargins(0,0,0,0);catalog->hide();layout->addWidget(catalog);
    connect(catalogToggle,&QToolButton::toggled,catalog,[catalog,catalogToggle](bool open){catalog->setVisible(open);catalogToggle->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    auto* reg=new QPushButton(tr("Зарегистрировать текущую карту…"));catalogLayout->addWidget(reg);
    connect(reg,&QPushButton::clicked,this,&CTravelPanel::registerRequested);
    auto* editText=new QPushButton(tr("Редактировать map.txt…"));
    editText->setObjectName("travelEditMapText");catalogLayout->addWidget(editText);
    connect(editText,&QPushButton::clicked,this,[this](){if(flush())emit editMapTextRequested();});
    auto* route=new QPushButton(tr("Создать маршрут между картами…"));
    route->setObjectName("travelCreateRoute");catalogLayout->addWidget(route);
    connect(route,&QPushButton::clicked,this,[this](){if(flush())emit routeRequested();});
    for(int k=0;k<2;++k) {
        m_visible[k]=new QCheckBox(k?tr("Показать переходы (оранжевые)"):tr("Показать появление (синие)"));
        m_visible[k]->setChecked(true);layout->addWidget(m_visible[k]);
        connect(m_visible[k],&QCheckBox::toggled,this,&CTravelPanel::visibilityChanged);
    }
    m_exits=new QListWidget; m_exits->setFixedHeight(100);layout->addWidget(m_exits);
    m_exits->setObjectName("travelExitList");
    m_exits->viewport()->installEventFilter(this);
    m_exits->setToolTip(tr("ПКМ по строке — камера к области выбранной вкладки."));
    auto* newRow=new QHBoxLayout;
    auto* addDeploy=new QPushButton(tr("+ Появление"));auto* addRemove=new QPushButton(tr("+ Переход"));
    m_add[0]=addDeploy;m_add[1]=addRemove;
    for(int k=0;k<2;++k){m_add[k]->setCheckable(true);m_add[k]->setObjectName(QString("travelAdd%1").arg(k));
        m_add[k]->setToolTip(tr("Нажмите, затем зажмите ЛКМ на карте и протяните прямоугольник. Esc — отмена."));}
    newRow->addWidget(addDeploy);newRow->addWidget(addRemove);layout->addLayout(newRow);
    connect(addDeploy,&QPushButton::clicked,this,[this](){const bool existing=m_zone>=0 && m_zone<m_zones.size() && m_exit>=0 && m_exit<m_zones[m_zone].exits.size() && !m_zones[m_zone].exits[m_exit].hasDeployRectangle;
        if(flush(0))emit drawRequested(false,!existing);else m_add[0]->setChecked(false);});
    connect(addRemove,&QPushButton::clicked,this,[this](){const bool existing=m_zone>=0 && m_zone<m_zones.size() && m_exit>=0 && m_exit<m_zones[m_zone].exits.size() && !m_zones[m_zone].exits[m_exit].hasRemoveRectangle;
        if(flush(1))emit drawRequested(true,!existing);else m_add[1]->setChecked(false);});
    m_properties=new QWidget;auto* props=new QVBoxLayout(m_properties);props->setContentsMargins(0,0,0,0);
    auto* target=new QFormLayout;m_destination=new QComboBox;
    configureTravelCombo(m_destination);m_destination->setObjectName("travelDestination");
    m_destinationExit=new QComboBox;configureTravelCombo(m_destinationExit);
    m_destinationExit->setObjectName("travelDestinationEntry");
    m_destinationExit->setToolTip(tr("Существующие записи целевой зоны. Для игровой карты нужна область появления."));
    target->addRow(tr("Зона (ID):"),m_destination);target->addRow(tr("Запись / конец маршрута:"),m_destinationExit);
    auto* tabs=new QTabWidget;m_tabs=tabs;props->addWidget(tabs);
    for(int k=0;k<2;++k) {
        auto* page=new QWidget;auto* form=new QFormLayout(page);
        if(k==0) {
            form->addRow(new QLabel(tr("Откуда прибывают сюда")));
            m_incoming=new QLabel;m_incoming->setWordWrap(true);
            m_incoming->setObjectName("travelIncoming");form->addRow(m_incoming);
            m_source=new QComboBox;configureTravelCombo(m_source);m_source->setObjectName("travelSource");
            form->addRow(tr("Запись на другой карте:"),m_source);
            m_linkIncoming=new QPushButton(tr("Направить выбранную запись сюда"));
            m_linkIncoming->setObjectName("travelLinkIncoming");form->addRow(m_linkIncoming);
            auto* note=new QLabel(tr("Команда изменит назначение выбранной записи на другой карте. Её области останутся на месте. Ctrl+S сохранит связь."));
            note->setWordWrap(true);form->addRow(note);
        } else {
            form->addRow(new QLabel(tr("Куда ведёт переход отсюда")));
            m_friendly=new QComboBox;configureTravelCombo(m_friendly);m_friendly->setObjectName("travelFinalDestination");
            form->addRow(tr("Куда ведёт:"),m_friendly);
            m_linkStatus=new QLabel;m_linkStatus->setObjectName("travelLinkStatus");m_linkStatus->setWordWrap(true);form->addRow(m_linkStatus);
            m_problem=new QPushButton(tr("Перейти к проблеме"));m_problem->setObjectName("travelProblem");form->addRow(m_problem);
            connect(m_problem,&QPushButton::clicked,this,[this](){
                if(!flush() || m_zone<0 || m_exit<0)return;
                const auto e=m_zones[m_zone].exits[m_exit];const auto d=travel::resolve(m_zones,e.destinationZoneId,e.destinationExitId);
                if(!e.hasDeployRectangle){emit destinationProblemRequested(m_zone,m_exit);return;}
                emit destinationProblemRequested(d.zone,d.entry);
            });
            auto* advanced=new QToolButton;advanced->setText(tr("Расширенные поля: зона и запись"));advanced->setCheckable(true);advanced->setArrowType(Qt::RightArrow);form->addRow(advanced);
            advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            auto* raw=new QWidget;raw->setLayout(target);raw->hide();form->addRow(raw);
            connect(advanced,&QToolButton::toggled,raw,[raw,advanced](bool open){raw->setVisible(open);advanced->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
            m_routeEditor=new QWidget;auto* routeForm=new QFormLayout(m_routeEditor);routeForm->setContentsMargins(0,0,0,0);
            m_routeWarning=new QLabel;m_routeWarning->setWordWrap(true);routeForm->addRow(m_routeWarning);
            m_farZone=new QComboBox;configureTravelCombo(m_farZone);m_farZone->setObjectName("travelFarZone");routeForm->addRow(tr("Дальний конец → карта:"),m_farZone);
            m_farEntry=new QComboBox;configureTravelCombo(m_farEntry);m_farEntry->setObjectName("travelFarEntry");routeForm->addRow(tr("Вход на этой карте:"),m_farEntry);
            auto* applyFar=new QPushButton(tr("Изменить дальний конец для всех карт"));applyFar->setObjectName("travelApplyFarEnd");routeForm->addRow(applyFar);form->addRow(m_routeEditor);
            connect(m_farZone,QOverload<int>::of(&QComboBox::activated),this,[this](){fillFarEntries(0);});
            connect(applyFar,&QPushButton::clicked,this,[this](){
                const int route=m_route,endpoint=m_farEnd;const QString id=m_farZone->currentData().toString();const int entry=m_farEntry->currentData().toInt();
                if(m_farZone->currentText()!=m_farZone->itemText(m_farZone->currentIndex()) || m_farEntry->currentText()!=m_farEntry->itemText(m_farEntry->currentIndex())){setStatus(tr("Выберите карту и вход из списка."));return;}
                const auto d=travel::resolve(m_zones,id,entry);
                if(!d.error.isEmpty() || d.route>=0){setStatus(d.error.isEmpty()?tr("Выберите игровую карту."):d.error);return;}
                if(route>=0 && endpoint>=0 && flush())emit routeEndEdited(route,endpoint,id,entry);
            });
            connect(m_friendly,QOverload<int>::of(&QComboBox::activated),this,[this](int index){
                const auto pair=m_friendly->itemData(index).toList();if(pair.size()!=2 || !flush())return;
                auto e=m_zones[m_zone].exits[m_exit];e.destinationZoneId=pair[0].toString();e.destinationExitId=pair[1].toInt();emit exitEdited(m_exit,e);
            });
            auto* createRoute=new QPushButton(tr("Создать новый маршрут…"));
            createRoute->setObjectName("travelDestinationCreate");form->addRow(createRoute);
            connect(createRoute,&QPushButton::clicked,this,[this](){if(flush())emit routeRequested();});
            auto* globalPosition=new QPushButton(tr("Положение маршрута на глобальной карте…"));
            globalPosition->setObjectName("travelGlobalPosition");form->addRow(globalPosition);
            connect(globalPosition,&QPushButton::clicked,this,[this](){if(flush())emit globalPositionRequested();});
            m_directionNote=new QLabel;m_directionNote->setWordWrap(true);
            m_directionNote->setObjectName("travelDirectionNote");form->addRow(m_directionNote);
            m_repairOrder=new QPushButton(tr("Восстановить раздельные области"));
            m_repairOrder->setObjectName("travelRepairOrder");form->addRow(m_repairOrder);
            connect(m_repairOrder,&QPushButton::clicked,this,[this](){
                if(!flush() || m_zone<0 || m_exit<0)return;
                auto e=m_zones[m_zone].exits[m_exit];e.removeOverridesDeploy=true;
                emit exitEdited(m_exit,e);
            });
        }
        m_present[k]=new QCheckBox(tr("Область задана"));form->addRow(m_present[k]);
        const QStringList labels={tr("X:"),tr("Y:"),tr("Ширина:"),tr("Высота:")};
        for(int c=0;c<4;++c) {
            m_rect[k][c]=new CWorldCoordinateEdit(c<2?0:1,page);
            m_rect[k][c]->setObjectName(QString("travelRect_%1_%2").arg(k).arg(c));
            form->addRow(labels[c],m_rect[k][c]);
            connect(m_rect[k][c],&QLineEdit::editingFinished,this,[this](){flush();});
        }
        auto* draw=new QPushButton(tr("Нарисовать заново"));draw->setObjectName(QString("travelRedraw%1").arg(k));form->addRow(draw);
        connect(draw,&QPushButton::clicked,this,[this,k](){if(flush(k))emit drawRequested(k!=0,false);});
        m_present[k]->setObjectName(QString("travelPresent%1").arg(k));
        connect(m_present[k],&QCheckBox::clicked,this,[this,k](bool checked){
            if(checked && m_zone>=0 && m_exit>=0) {
                const auto& entry=m_zones[m_zone].exits[m_exit];
                if(!(k?entry.hasRemoveRectangle:entry.hasDeployRectangle)) {
                    {QSignalBlocker block(m_present[k]);m_present[k]->setChecked(false);}
                    if(flush(k))emit drawRequested(k!=0,false);
                    return;
                }
            }
            flush();
        });
        if(k==1) {
            auto* reverse=new QPushButton(tr("Создать обратный переход…"));form->addRow(reverse);
            connect(reverse,&QPushButton::clicked,this,[this](){if(flush())emit reverseRequested();});
        }
        tabs->addTab(page,k?tr("Переход"):tr("Появление"));
    }
    tabs->setCurrentIndex(1);
    connect(m_linkIncoming,&QPushButton::clicked,this,[this](){
        if(m_source->currentText()!=m_source->itemText(m_source->currentIndex())) {
            setStatus(tr("Выберите запись из результатов поиска."));return;
        }
        const auto pair=m_source->currentData().toList();
        if(pair.size()==2 && flush())emit incomingRequested(pair[0].toInt(),pair[1].toInt());
    });
    auto* remove=new QPushButton(tr("Удалить запись входа/перехода"));props->addWidget(remove);
    layout->addWidget(m_properties);layout->addStretch();
    connect(remove,&QPushButton::clicked,this,&CTravelPanel::deleteRequested);
    connect(m_destination,QOverload<int>::of(&QComboBox::activated),this,[this](){fillDestinationEntries(0);flush();});
    connect(m_destinationExit,QOverload<int>::of(&QComboBox::activated),this,[this](){flush();});
    connect(m_zoneCombo,QOverload<int>::of(&QComboBox::activated),this,[this](int row){
        if(flush())emit zoneSelected(m_zoneCombo->itemData(row).toInt());
    });
    connect(m_exits,&QListWidget::currentRowChanged,this,[this](int row){
        if(m_filling)return;
        if(!flush()){const QSignalBlocker blocker(m_exits);m_exits->setCurrentRow(m_exit);return;}
        m_exit=row;fillExit();emit exitSelected(row);
    });
}
void CTravelPanel::populate(const QVector<SMapTableZone>& zones,int zoneIndex,int exitIndex,bool dirty)
{
    m_filling=true;m_zones=zones;m_zone=zoneIndex;m_exit=exitIndex;m_dirty=dirty;
    m_zoneCombo->clear();
    for(int i=0;i<zones.size();++i)m_zoneCombo->addItem(zones[i].zoneId,i);
    m_zoneCombo->setCurrentIndex(zoneIndex);
    m_exits->clear();
    if(zoneIndex>=0 && zoneIndex<zones.size())
        for(const auto& e:zones[zoneIndex].exits) {
            const bool linked=e.destinationZoneId.compare("none",Qt::CaseInsensitive)!=0;
            const QString kind=e.hasDeployRectangle
                ? (linked?tr("Появление и переход"):tr("Появление"))
                : (e.hasRemoveRectangle?tr("Переход"):tr("Запись без областей"));
            QString label=tr("%1 №%2").arg(kind).arg(e.exitId);
            if(e.destinationZoneId.compare("none",Qt::CaseInsensitive)==0)
                label+=tr(" — без перехода");
            else if(e.destinationExitId==0)
                label+=tr(" → %1, вход не выбран").arg(e.destinationZoneId);
            else {
                const auto destination=travel::resolve(zones,e.destinationZoneId,e.destinationExitId);
                label+=tr(" → %1").arg(travel::targetLabel(zones,destination,m_names));
            }
            auto* item=new QListWidgetItem(label,m_exits);
            item->setToolTip(label+(e.sourceLabel!=QString::number(e.exitId)?tr("\nМетка в файле: %1; игра использует порядковый номер %2.").arg(e.sourceLabel).arg(e.exitId):QString()));
        }
    m_exits->setCurrentRow(exitIndex);
    m_status->setText(dirty?tr("Есть изменения переходов — Ctrl+S для сохранения карты."):
        tr("Перетащите область или её маркеры. Esc отменяет жест. Координаты локальные."));
    m_filling=false;fillExit();
}
void CTravelPanel::fillExit()
{
    m_filling=true;
    const bool valid=m_zone>=0 && m_zone<m_zones.size() && m_exit>=0 && m_exit<m_zones[m_zone].exits.size();
    m_properties->setEnabled(valid);
    m_chain->setText(valid?QString():tr("Выберите появление или переход в списке."));
    if(valid) {
        const auto& e=m_zones[m_zone].exits[m_exit];
        QStringList incoming;
        m_source->clear();m_source->addItem(tr("Выберите запись…"));
        for(int z=0;z<m_zones.size();++z)for(int i=0;i<m_zones[z].exits.size();++i) {
            const auto& source=m_zones[z].exits[i];
            const bool linked=source.destinationZoneId.compare(m_zones[m_zone].zoneId,Qt::CaseInsensitive)==0
                && source.destinationExitId==e.exitId;
            if(linked)incoming.append(tr("%1, запись №%2 → сюда").arg(m_zones[z].zoneId).arg(source.exitId));
            if(z==m_zone)continue;
            const QString label=tr("%1, запись №%2 → %3:%4%5").arg(m_zones[z].zoneId).arg(source.exitId)
                .arg(source.destinationZoneId).arg(source.destinationExitId)
                .arg(source.hasEffectiveRemove()?QString():tr(" (область перехода не задана)"));
            m_source->addItem(label,QVariantList{z,i});
            m_source->setItemData(m_source->count()-1,label,Qt::ToolTipRole);
        }
        m_incoming->setText(incoming.isEmpty()?tr("На эту запись пока никто не ссылается."):incoming.join("\n"));
        if(e.destinationZoneId.compare("none",Qt::CaseInsensitive)!=0)
            m_incoming->setText(m_incoming->text()+tr("\nУ этой записи также есть исходящая связь → %1:%2. Она настраивается на вкладке «Переход».")
                .arg(e.destinationZoneId).arg(e.destinationExitId));
        m_linkIncoming->setEnabled(e.hasDeployRectangle);
        const bool overwritten=e.hasDeployRectangle && e.hasRemoveRectangle && !e.removeOverridesDeploy;
        m_repairOrder->setVisible(overwritten);
        m_directionNote->setText(overwritten
            ?tr("Ошибка порядка: поздний #deploy заменяет область #remove. Оранжевым показан действующий выход, совпадающий с появлением. «Восстановить раздельные области» применит записанную область #remove; затем сохраните карту.")
            :(!e.hasRemoveRectangle && e.hasDeployRectangle
                ?tr("Отдельная область перехода не задана: игра использует область появления. Для отдельного выхода нарисуйте переход. Для появления без выхода выберите «Без перехода».")
                :tr("Назначение относится к выходу с текущей карты. Номер входа — порядковый номер записи на целевой карте. После сохранения полностью перезапустите игру.")));
        m_destination->clear();m_destination->addItem(tr("Без перехода"),"none");
        for(const auto& z:m_zones)if(m_destination->findData(z.zoneId)<0)m_destination->addItem(z.zoneId+(z.zoneType=="edge"?tr(" — маршрут"):tr(" — карта")),z.zoneId);
        int row=m_destination->findData(e.destinationZoneId);
        if(row<0){m_destination->addItem(e.destinationZoneId,e.destinationZoneId);row=m_destination->count()-1;}
        m_destination->setCurrentIndex(row);fillDestinationEntries(e.destinationExitId);fillFriendlyDestination();
        for(int k=0;k<2;++k) {
            m_present[k]->setChecked(k?e.hasRemoveRectangle:e.hasDeployRectangle);
            const auto r=k?e.removeRectangle:e.deployRectangle;
            const double values[]={r.x(),r.y(),r.width(),r.height()};
            for(int c=0;c<4;++c)m_rect[k][c]->setText(QString::number(values[c],'g',17));
        }
    }
    m_filling=false;
}
void CTravelPanel::fillDestinationEntries(int selected)
{
    const QSignalBlocker blocker(m_destinationExit);
    m_destinationExit->clear();m_destinationExit->addItem(tr("Не выбран"),0);
    const QString id=m_destination->currentData().toString();
    bool usable=false;
    for(const auto& zone:m_zones)if(zone.zoneId.compare(id,Qt::CaseInsensitive)==0)
        for(const auto& entry:zone.exits) {
            const bool valid=zone.zoneType.compare("edge",Qt::CaseInsensitive)==0 || entry.hasDeployRectangle;
            const QString label=travel::edge(zone)?tr("Конец №%1 → %2:%3 (не область появления)").arg(entry.exitId).arg(entry.destinationZoneId).arg(entry.destinationExitId):travel::entryDetails(entry,m_zones[m_zone].zoneId,m_zones[m_zone].exits[m_exit].exitId);
            m_destinationExit->addItem(label,entry.exitId);
            m_destinationExit->setItemData(m_destinationExit->count()-1,valid?label:label+tr("\nНедоступен: сначала нарисуйте область появления на целевой карте."),Qt::ToolTipRole);
            if(!valid)static_cast<QStandardItemModel*>(m_destinationExit->model())->item(m_destinationExit->count()-1)->setEnabled(false);
            if(entry.exitId==selected && valid)usable=true;
        }
    int index=m_destinationExit->findData(selected);
    if(index<0){m_destinationExit->addItem(tr("№%1 — запись отсутствует").arg(selected),selected);index=m_destinationExit->count()-1;}
    m_destinationExit->setCurrentIndex(index);
    if(id.compare("none",Qt::CaseInsensitive)!=0 && !usable)
        m_directionNote->setText(tr("Назначение %1:%2 не готово: выберите существующую запись с областью появления. Если нужен новый вход, создайте его на целевой карте или используйте «Создать маршрут между картами…». Сохранение новой неверной связи будет отклонено.").arg(id).arg(selected));
}
void CTravelPanel::fillFriendlyDestination()
{
    const auto& e=m_zones[m_zone].exits[m_exit];
    const auto d=travel::resolve(m_zones,e.destinationZoneId,e.destinationExitId);
    const bool none=e.destinationZoneId.compare("none",Qt::CaseInsensitive)==0;
    m_chain->setText(none?tr("Появление №%1 — без перехода").arg(e.exitId):tr("Переход №%1 → %2%3")
        .arg(e.exitId).arg(d.route>=0?tr("маршрут %1 (по глобальной карте) → ").arg(m_zones[d.route].zoneId):QString())
        .arg(d.zone>=0 && d.entry>=0?tr("%1, вход №%2").arg(travel::name(m_zones[d.zone],m_names)).arg(m_zones[d.zone].exits[d.entry].exitId):d.error));
    const bool back=!none && travel::reciprocal(m_zones,m_zone,e,d);
    m_linkStatus->setText(none?tr("Связь не задана"):!d.error.isEmpty()?d.error:back?tr("Туда и обратно согласовано"):tr("Нет обратного входа / обратная связь не согласована"));
    if(!none && !e.hasDeployRectangle)m_linkStatus->setText(tr("Нет обратного входа: у этой записи не задано появление для возвращения."));
    m_problem->setVisible(!none && (!back || !d.error.isEmpty()));
    m_friendly->clear();m_friendly->addItem(tr("Без перехода"),QVariantList{QString("none"),0});
    int selected=none?0:-1;
    for(int z=0;z<m_zones.size();++z) {
        const auto& zone=m_zones[z];
        if(travel::zoneIndex(m_zones,zone.zoneId)!=z)continue;
        for(const auto& entry:zone.exits) {
            // An edge is selectable only from its near side; never silently reverse it.
            if(travel::edge(zone) && entry.destinationZoneId.compare(m_zones[m_zone].zoneId,Qt::CaseInsensitive)!=0)continue;
            if(!travel::edge(zone) && zone.zoneType.compare("game",Qt::CaseInsensitive)!=0)continue;
            const auto candidate=travel::resolve(m_zones,zone.zoneId,entry.exitId);
            QString label=travel::targetLabel(m_zones,candidate,m_names);
            if(candidate.route<0)label+=tr(" — напрямую");
            const int row=m_friendly->count();m_friendly->addItem(label,QVariantList{zone.zoneId,entry.exitId});
            QString hint=candidate.error;
            if(candidate.zone>=0 && candidate.entry>=0)hint=travel::entryDetails(m_zones[candidate.zone].exits[candidate.entry],
                candidate.route>=0?m_zones[candidate.route].zoneId:m_zones[m_zone].zoneId,
                candidate.route>=0 && candidate.farEnd>=0?m_zones[candidate.route].exits[candidate.farEnd].exitId:e.exitId)+"\n"+hint;
            m_friendly->setItemData(row,hint,Qt::ToolTipRole);
            if(!candidate.error.isEmpty())static_cast<QStandardItemModel*>(m_friendly->model())->item(row)->setEnabled(false);
            if(zone.zoneId.compare(e.destinationZoneId,Qt::CaseInsensitive)==0 && entry.exitId==e.destinationExitId)selected=row;
        }
    }
    if(selected<0){selected=m_friendly->count();m_friendly->addItem(tr("Текущая связь %1:%2 — проверьте расширенные поля").arg(e.destinationZoneId).arg(e.destinationExitId));}
    m_friendly->setCurrentIndex(selected);
    m_route=d.route;m_farEnd=d.farEnd;m_routeEditor->setVisible(m_route>=0 && m_farEnd>=0);
    if(m_route>=0 && m_farEnd>=0) {
        QStringList users;for(const auto& zone:m_zones)for(const auto& x:zone.exits)
            if(x.destinationZoneId.compare(m_zones[m_route].zoneId,Qt::CaseInsensitive)==0 && !users.contains(zone.zoneId))users<<zone.zoneId;
        m_routeWarning->setText(tr("Общий маршрут %1. Изменение дальнего конца затронет все использующие его карты: %2. Сохранение каталога map.txt в texts.res — Ctrl+S, с резервной копией. Отмена — Ctrl+Z.")
            .arg(m_zones[m_route].zoneId,users.join(", ")));
        const auto& endpoint=m_zones[m_route].exits[m_farEnd];m_farZone->clear();
        for(const auto& zone:m_zones)if(zone.zoneType.compare("game",Qt::CaseInsensitive)==0)m_farZone->addItem(travel::name(zone,m_names),zone.zoneId);
        m_farZone->setCurrentIndex(m_farZone->findData(endpoint.destinationZoneId));fillFarEntries(endpoint.destinationExitId);
    }
}
void CTravelPanel::fillFarEntries(int selected)
{
    m_farEntry->clear();m_farEntry->addItem(tr("Выберите вход…"),0);
    const int zone=travel::zoneIndex(m_zones,m_farZone->currentData().toString());
    if(zone>=0)for(const auto& e:m_zones[zone].exits) {
        const int row=m_farEntry->count();const auto label=travel::entryDetails(e,m_route>=0?m_zones[m_route].zoneId:QString(),m_farEnd>=0?m_zones[m_route].exits[m_farEnd].exitId:0);
        m_farEntry->addItem(label,e.exitId);m_farEntry->setItemData(row,label+(e.hasDeployRectangle?QString():tr("\nНедоступен: нет области появления.")),Qt::ToolTipRole);
        if(!e.hasDeployRectangle)static_cast<QStandardItemModel*>(m_farEntry->model())->item(row)->setEnabled(false);
    }
    m_farEntry->setCurrentIndex(qMax(0,m_farEntry->findData(selected)));
}
bool CTravelPanel::flush(int redrawArea)
{
    if(m_filling || m_zone<0 || m_zone>=m_zones.size() || m_exit<0 || m_exit>=m_zones[m_zone].exits.size())return true;
    auto e=m_zones[m_zone].exits[m_exit];
    e.destinationZoneId=m_destination->currentData().toString();
    if(m_destination->currentText()!=m_destination->itemText(m_destination->currentIndex())
       || m_destinationExit->currentText()!=m_destinationExit->itemText(m_destinationExit->currentIndex())) {
        setStatus(tr("Выберите зону и запись из результатов поиска."));return false;
    }
    e.destinationExitId=m_destinationExit->currentData().toInt();
    if(e.destinationZoneId.compare("none",Qt::CaseInsensitive)==0)e.destinationExitId=0;
    for(int k=0;k<2;++k) {
        if(k==redrawArea)continue; // The drawing gesture will replace this area atomically.
        const bool present=m_present[k]->isChecked();
        if(k)e.hasRemoveRectangle=present;else e.hasDeployRectangle=present;
        if(!present)continue;
        double v[4];
        for(int c=0;c<4;++c)if(!m_rect[k][c]->coordinate(&v[c])) {
            setStatus(tr("Нужны конечные числовые координаты."));return false;
        }
        if(v[2]<=0 || v[3]<=0) {setStatus(tr("Ширина и высота должны быть больше нуля."));return false;}
        const QRectF r(v[0],v[1],v[2],v[3]);
        if(k)e.removeRectangle=r;else e.deployRectangle=r;
    }
    if(e!=m_zones[m_zone].exits[m_exit])emit exitEdited(m_exit,e);
    return true;
}
void CTravelPanel::setStatus(const QString& text){m_status->setText(text);}
int CTravelPanel::selectedExit() const {return m_exit;}
bool CTravelPanel::showDeploy() const {return m_visible[0]->isChecked();}
bool CTravelPanel::showRemove() const {return m_visible[1]->isChecked();}

void CTravelPanel::setDrawingState(bool active,bool remove,bool newExit)
{
    for(int k=0;k<2;++k){QSignalBlocker block(m_add[k]);m_add[k]->setChecked(active && k==int(remove));}
    Q_UNUSED(newExit);
    if(active){
        revealArea(remove);
        setStatus((remove?tr("Рисование области перехода. "):tr("Рисование области появления. "))
            +tr("Зажмите ЛКМ на карте и протяните прямоугольник. Esc или ПКМ — отмена."));
    } else setStatus(m_dirty?tr("Есть изменения переходов — Ctrl+S для сохранения карты."):
        tr("Перетащите область или её маркеры. ПКМ по списку — перейти к области."));
}
void CTravelPanel::revealArea(bool remove)
{
    m_tabs->setCurrentIndex(remove?1:0);m_visible[remove?1:0]->setChecked(true);
}
bool CTravelPanel::eventFilter(QObject* watched,QEvent* event)
{
    if(watched==m_exits->viewport()){
        if(event->type()==QEvent::MouseButtonPress){
            auto* mouse=static_cast<QMouseEvent*>(event);
            if(mouse->button()==Qt::RightButton){
                auto* item=m_exits->itemAt(mouse->pos());
                const int row=item?m_exits->row(item):-1;
                if(row>=0 && flush() && row<m_exits->count()){
                    m_exits->setCurrentRow(row);
                    emit focusRequested(row,m_tabs->currentIndex()==1);
                }
                return true;
            }
        }
        if(event->type()==QEvent::ContextMenu)return true;
        if(event->type()==QEvent::MouseButtonRelease
           && static_cast<QMouseEvent*>(event)->button()==Qt::RightButton)return true;
    }
    return QWidget::eventFilter(watched,event);
}
