#include "travel_panel.h"
#include "travel_combo.h"
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
    m_status=new QLabel; m_status->setWordWrap(true);layout->addWidget(m_status);
    m_zoneCombo=new QComboBox;configureTravelCombo(m_zoneCombo);layout->addWidget(m_zoneCombo);
    auto* reg=new QPushButton(tr("Зарегистрировать текущую карту…"));layout->addWidget(reg);
    connect(reg,&QPushButton::clicked,this,&CTravelPanel::registerRequested);
    auto* editText=new QPushButton(tr("Редактировать map.txt…"));
    editText->setObjectName("travelEditMapText");layout->addWidget(editText);
    connect(editText,&QPushButton::clicked,this,[this](){if(flush())emit editMapTextRequested();});
    auto* route=new QPushButton(tr("Создать маршрут между картами…"));
    route->setObjectName("travelCreateRoute");layout->addWidget(route);
    connect(route,&QPushButton::clicked,this,[this](){if(flush())emit routeRequested();});
    for(int k=0;k<2;++k) {
        m_visible[k]=new QCheckBox(k?tr("Показать переходы (оранжевые)"):tr("Показать появление (синие)"));
        m_visible[k]->setChecked(true);layout->addWidget(m_visible[k]);
        connect(m_visible[k],&QCheckBox::toggled,this,&CTravelPanel::visibilityChanged);
    }
    m_exits=new QListWidget; m_exits->setMinimumHeight(100);layout->addWidget(m_exits);
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
    target->addRow(tr("Целевая зона:"),m_destination);target->addRow(tr("Вход на целевой карте:"),m_destinationExit);
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
            form->addRow(target);
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
            else
                label+=tr(" → %1, вход №%2").arg(e.destinationZoneId).arg(e.destinationExitId);
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
        m_destination->setCurrentIndex(row);fillDestinationEntries(e.destinationExitId);
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
            m_destinationExit->addItem(tr("№%1 — %2").arg(entry.exitId).arg(valid?tr("есть появление"):tr("нет области появления")),entry.exitId);
            if(!valid)static_cast<QStandardItemModel*>(m_destinationExit->model())->item(m_destinationExit->count()-1)->setEnabled(false);
            if(entry.exitId==selected && valid)usable=true;
        }
    int index=m_destinationExit->findData(selected);
    if(index<0){m_destinationExit->addItem(tr("№%1 — запись отсутствует").arg(selected),selected);index=m_destinationExit->count()-1;}
    m_destinationExit->setCurrentIndex(index);
    if(id.compare("none",Qt::CaseInsensitive)!=0 && !usable)
        m_directionNote->setText(tr("Назначение %1:%2 не готово: выберите существующую запись с областью появления. Если нужен новый вход, создайте его на целевой карте или используйте «Создать маршрут между картами…». Сохранение новой неверной связи будет отклонено.").arg(id).arg(selected));
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
