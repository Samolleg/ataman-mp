// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ataman contributors
#include "quest_map_dialog.h"
#include "res_archive_index.h"
#include "res_save_coordinator.h"
#include "process_edit_lock.h"
#include "mmp_inspector.h"
#include "mmp_codec.h"
#include <QtWidgets>
#include <functional>

class CQuestMapCanvas:public QWidget {
public:
    CQuestMapCanvas(questmap::Document* doc,QWidget* parent):QWidget(parent),document(doc){setMinimumSize(384,384);setFocusPolicy(Qt::StrongFocus);setObjectName("questMapCanvas");}
    questmap::Document* document;int selected=-1;QPoint dragStart,dragOrigin,dragPosition;bool dragging=false;
    std::function<void(int)> selectionChanged;
    QRectF imageRect()const {
        const auto im=document->state().paper;if(im.isNull())return {};
        const double scale=qMin(double(width())/im.width(),double(height())/im.height());
        return QRectF((width()-im.width()*scale)/2,(height()-im.height()*scale)/2,im.width()*scale,im.height()*scale);
    }
    QPoint pixel(QPoint p)const {auto r=imageRect();const auto im=document->state().paper;return QPoint(int((p.x()-r.x())/r.width()*im.width()),int((p.y()-r.y())/r.height()*im.height()));}
    QRect stampRect(int i)const {
        if(i<0||i>=int(document->state().stamps.size()))return {};
        const auto& s=document->state().stamps[i];for(const auto& icon:document->icons())if(icon.name==s.name)
            return QRect(s.x-int(icon.image.width)*s.scale/2,s.y-int(icon.image.height)*s.scale/2,int(icon.image.width)*s.scale,int(icon.image.height)*s.scale);
        return {};
    }
    void paintEvent(QPaintEvent*)override {
        QPainter painter(this);painter.fillRect(rect(),QColor(49,45,38));painter.setRenderHint(QPainter::SmoothPixmapTransform,false);
        auto im=document->composed();
        if(dragging) {
            auto p=questmap::pixels(document->state().paper);
            for(int i=0;i<int(document->state().stamps.size());++i) {
                auto stamp=document->state().stamps[i];if(i==selected){stamp.x=dragPosition.x();stamp.y=dragPosition.y();}
                for(const auto& icon:document->icons())if(icon.name==stamp.name){questmap::Composite(p,icon.image,stamp.x,stamp.y,stamp.scale);break;}
            }
            im=questmap::image(p);
        }
        const auto area=imageRect();painter.drawImage(area,im);
        if(selected>=int(document->state().stamps.size()))selected=-1;
        if(selected>=0) {
            QRectF r=stampRect(selected);if(dragging)r.translate(dragPosition-dragOrigin);
            const double scale=area.width()/im.width();r=QRectF(area.x()+r.x()*scale,area.y()+r.y()*scale,r.width()*scale,r.height()*scale);
            painter.setPen(QPen(QColor(245,82,44),2,Qt::DashLine));painter.drawRect(r);
        }
    }
    void mousePressEvent(QMouseEvent* e)override {
        if(e->button()!=Qt::LeftButton||document->state().paper.isNull())return;
        const auto p=pixel(e->pos());selected=-1;
        for(int i=int(document->state().stamps.size())-1;i>=0;--i)if(stampRect(i).contains(p)){selected=i;break;}
        if(selected>=0){dragging=true;dragStart=p;const auto& s=document->state().stamps[selected];dragOrigin=dragPosition=QPoint(s.x,s.y);}
        if(selectionChanged)selectionChanged(selected);update();
    }
    void mouseMoveEvent(QMouseEvent* e)override {if(dragging){dragPosition=dragOrigin+pixel(e->pos())-dragStart;update();}}
    void mouseReleaseEvent(QMouseEvent* e)override {
        if(e->button()!=Qt::LeftButton||!dragging)return;dragging=false;
        if(dragPosition!=dragOrigin)document->move(selected,dragPosition);update();
    }
    void keyPressEvent(QKeyEvent* e)override {
        if(e->key()==Qt::Key_Delete){document->remove(selected);selected=-1;update();return;}
        QWidget::keyPressEvent(e);
    }
};

CQuestMapDialog::CQuestMapDialog(const SQuestMapSource& source,QWidget* parent):QDialog(parent),m_source(source) {
    setObjectName("questMapDialog");setWindowTitle(tr("Quest Map Editor"));setAttribute(Qt::WA_DeleteOnClose);resize(1130,760);
    auto* layout=new QVBoxLayout(this);auto* body=new QHBoxLayout;layout->addLayout(body,1);
    auto* left=new QVBoxLayout;body->addLayout(left);auto* form=new QFormLayout;left->addLayout(form);
    const auto spin=[&](const char* name,int min,int max,int value){auto* s=new QSpinBox(this);s->setObjectName(name);s->setRange(min,max);s->setValue(value);return s;};
    m_size=spin("questMapSize",64,2048,256);m_size->setSingleStep(64);form->addRow(tr("Image size"),m_size);
    m_format=new QComboBox(this);m_format->setObjectName("questMapFormat");m_format->addItems({tr("5551 (original game)"),tr("DXT1 (mod textures)")});m_format->setCurrentIndex(source.originalFormat==0x31545844?1:0);form->addRow(tr("Game export format"),m_format);
    if(!source.original.isNull()) {
        auto* profile=new QPushButton(tr("Use original size and format"),this);profile->setObjectName("questMapOriginalProfile");form->addRow(profile);
        connect(profile,&QPushButton::clicked,this,[this](){m_size->setValue(m_source.original.width());m_format->setCurrentIndex(m_source.originalFormat==0x31545844?1:0);});
    }
    m_frame=new QComboBox(this);m_frame->setObjectName("questMapFrame");m_frame->addItems({tr("Torn sheet"),tr("Rolled top and bottom"),tr("Rolled left and right")});form->addRow(tr("Frame"),m_frame);
    m_seed=spin("questMapSeed",1,1000000,1);form->addRow(tr("Seed"),m_seed);
    m_roughness=spin("questMapRoughness",0,100,50);form->addRow(tr("Edge roughness (%)"),m_roughness);
    m_iconScale=spin("questMapIconScale",0,8,0);m_iconScale->setSpecialValueText(tr("Automatic"));form->addRow(tr("Generated icon scale"),m_iconScale);
    const auto box=[&](const char* name,const QString& text,bool checked){auto* b=new QCheckBox(text,this);b->setObjectName(name);b->setChecked(checked);left->addWidget(b);return b;};
    m_relief=box("questMapRelief",tr("Relief"),true);m_water=box("questMapWater",tr("Water"),true);m_forests=box("questMapForests",tr("Forests"),false);
    m_icons=box("questMapIcons",tr("Object icons"),true);m_bridges=box("questMapBridges",tr("Bridges"),true);m_exits=box("questMapExits",tr("Exits from map.txt"),true);m_sprites=box("questMapSprites",tr("Generated icons as movable stamps"),true);
    m_textureName=new QLineEdit(source.entry.objectivesTexture,this);m_textureName->setObjectName("questMapTextureName");
    if(m_textureName->text().isEmpty())m_textureName->setText(source.entry.zoneId.isEmpty()?QString():source.entry.zoneId+"quest");
    form->addRow(tr("Game texture name"),m_textureName);
    auto* hint=new QLabel(tr("Changes regenerate the preview. Undo restores the previous image and stamps. Drag stamps on the preview; Delete removes the selection."),this);hint->setWordWrap(true);hint->setMaximumWidth(230);left->addWidget(hint);left->addStretch();
    auto* center=new QVBoxLayout;body->addLayout(center,1);center->addWidget(new QLabel(tr("Generated quest map"),this));m_canvas=new CQuestMapCanvas(&m_document,this);center->addWidget(m_canvas,1);
    auto* commands=new QHBoxLayout;center->addLayout(commands);
    auto* undo=new QPushButton(tr("Undo"),this);undo->setObjectName("questMapUndo");auto* redo=new QPushButton(tr("Redo"),this);redo->setObjectName("questMapRedo");
    undo->setShortcut(QKeySequence::Undo);redo->setShortcut(QKeySequence::Redo);commands->addWidget(undo);commands->addWidget(redo);
    connect(undo,&QPushButton::clicked,m_document.history(),&QUndoStack::undo);connect(redo,&QPushButton::clicked,m_document.history(),&QUndoStack::redo);
    connect(m_document.history(),&QUndoStack::canUndoChanged,undo,&QPushButton::setEnabled);connect(m_document.history(),&QUndoStack::canRedoChanged,redo,&QPushButton::setEnabled);redo->setEnabled(false);
    auto* bake=new QPushButton(tr("Bake stamps"),this);bake->setObjectName("questMapBake");commands->addWidget(bake);connect(bake,&QPushButton::clicked,this,[this](){m_document.bake();});
    auto* right=new QVBoxLayout;body->addLayout(right);right->addWidget(new QLabel(tr("Stamp palette"),this));
    m_palette=new QListWidget(this);m_palette->setObjectName("questMapPalette");m_palette->setIconSize(QSize(34,38));m_palette->setMaximumWidth(220);right->addWidget(m_palette,1);
    for(const auto& icon:m_document.icons()) {
        auto name=QString::fromStdString(icon.name);QString label=name;
        if(name.startsWith("ink-"))label=tr("Ink: %1").arg(tr(questmap::KindName(static_cast<questmap::Kind>(name.mid(4).toInt()))));
        auto* item=new QListWidgetItem(QIcon(QPixmap::fromImage(questmap::image(icon.image))),label,m_palette);item->setData(Qt::UserRole,name);
    }
    m_palette->setCurrentRow(0);m_stampScale=spin("questMapStampScale",1,8,1);right->addWidget(new QLabel(tr("Stamp scale"),this));right->addWidget(m_stampScale);
    auto* add=new QPushButton(tr("Add stamp"),this);add->setObjectName("questMapAdd");right->addWidget(add);
    connect(add,&QPushButton::clicked,this,[this](){auto* item=m_palette->currentItem();if(!item)return;const auto im=m_document.state().paper;
        m_document.add(item->data(Qt::UserRole).toString(),QPoint(im.width()/2,im.height()/2),m_stampScale->value());m_canvas->selected=int(m_document.state().stamps.size())-1;m_canvas->update();});
    auto* remove=new QPushButton(tr("Delete stamp"),this);remove->setObjectName("questMapDelete");right->addWidget(remove);connect(remove,&QPushButton::clicked,this,[this](){m_document.remove(m_canvas->selected);m_canvas->selected=-1;});
    m_canvas->selectionChanged=[this](int index){if(index>=0){QSignalBlocker block(m_stampScale);m_stampScale->setValue(m_document.state().stamps[index].scale);}};
    connect(m_stampScale,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){m_document.scale(m_canvas->selected,value);});
    if(!source.original.isNull()) {auto* original=new QLabel(this);original->setObjectName("questMapOriginal");original->setPixmap(QPixmap::fromImage(source.original.scaled(192,192,Qt::KeepAspectRatio,Qt::FastTransformation)));right->addWidget(new QLabel(tr("Original game texture"),this));right->addWidget(original);}
    m_status=new QLabel(source.notes.value(0),this);m_status->setObjectName("questMapStatus");m_status->setWordWrap(true);layout->addWidget(m_status);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,this);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::close);
    auto* save=buttons->addButton(tr("Save PNG / DDS..."),QDialogButtonBox::ActionRole);save->setObjectName("questMapSaveImage");connect(save,&QPushButton::clicked,this,&CQuestMapDialog::saveImage);
    auto* exportButton=buttons->addButton(tr("Export to texture RES..."),QDialogButtonBox::ActionRole);exportButton->setObjectName("questMapExport");connect(exportButton,&QPushButton::clicked,this,&CQuestMapDialog::exportToMod);
    m_regenerateTimer=new QTimer(this);m_regenerateTimer->setSingleShot(true);m_regenerateTimer->setInterval(160);connect(m_regenerateTimer,&QTimer::timeout,this,[this](){regenerate();});
    for(auto* s:{m_size,m_seed,m_roughness,m_iconScale})connect(s,qOverload<int>(&QSpinBox::valueChanged),m_regenerateTimer,qOverload<>(&QTimer::start));
    connect(m_frame,qOverload<int>(&QComboBox::currentIndexChanged),m_regenerateTimer,qOverload<>(&QTimer::start));
    for(auto* b:{m_relief,m_water,m_forests,m_icons,m_bridges,m_exits,m_sprites})connect(b,&QCheckBox::toggled,m_regenerateTimer,qOverload<>(&QTimer::start));
    connect(m_document.history(),&QUndoStack::indexChanged,this,[this](){updatePreview();});regenerate();
}
questmap::Options CQuestMapDialog::options()const {
    questmap::Options o;o.width=o.height=m_size->value();o.seed=m_seed->value();o.roughness=m_roughness->value()/100.f;o.frame=static_cast<questmap::Frame>(m_frame->currentIndex());o.iconScale=m_iconScale->value();
    o.relief=m_relief->isChecked();o.water=m_water->isChecked();o.forests=m_forests->isChecked();o.icons=m_icons->isChecked();o.bridges=m_bridges->isChecked();o.exits=m_exits->isChecked();return o;
}
bool CQuestMapDialog::regenerate() {
    questmap::SetIcons(m_source.icons);
    m_regenerateTimer->stop();questmap::Input input;input.terrain=&m_source.terrain;input.markers=m_source.markers;questpixels::Image out;std::string error;questmap::State next;
    if(!questmap::Generate(input,options(),out,error,m_sprites->isChecked()?&next.stamps:nullptr)){m_status->setText(QString::fromStdString(error));return false;}
    next.paper=questmap::image(out);m_document.replace(next,tr("Generate quest map"));m_canvas->selected=-1;updatePreview();return true;
}
void CQuestMapDialog::updatePreview(){m_canvas->update();}
bool CQuestMapDialog::exportArchive(const QString& path,bool confirmed,QString* message) {
    const auto fail=[&](const QString& error){if(message)*message=error;return false;};
    QString name=m_textureName->text().trimmed();if(name.endsWith(".mmp",Qt::CaseInsensitive))name.chop(4);
    if(!QRegularExpression("^[A-Za-z0-9_-]+$").match(name).hasMatch())return fail(tr("Enter a texture name without whitespace or path characters."));
    const auto composed=m_document.composed();const int size=composed.width();
    if(composed.isNull()||size!=composed.height()||(size&(size-1))||size<64||size>2048)return fail(tr("Game export requires a square power-of-two size from 64 to 2048."));
    QString error;auto lease=CProcessEditLock::acquire(path,&error,false);if(!lease)return fail(error);
    SResPublishedMembersSaveRequest request;request.source=request.destination=QFileInfo(path);request.allowReplaceExisting=confirmed;
    for(bool opaque:{false,true}) {
        SResArchiveNewMember member;member.name=name+(opaque?"m.mmp":".mmp");
        if(m_format->currentIndex()==1) {
            QImage output=composed;
            if(opaque) {output=output.convertToFormat(QImage::Format_ARGB32);for(int y=0;y<output.height();++y)for(int x=0;x<output.width();++x){auto p=output.pixel(x,y);output.setPixel(x,y,qAlpha(p)<128?qRgb(0,0,0):qRgb(qRed(p),qGreen(p),qBlue(p)));}}
            member.payload=ei::encodeMmpDxt1(output,true,&error);
        } else member.payload=questmap::encode5551(composed,opaque);
        const auto decoded=ei::inspectMmp(member.payload);if(!decoded.valid||decoded.format!=(m_format->currentIndex()==1?0x31545844u:0x5551u)||decoded.width!=size)return fail(decoded.diagnostic);
        request.published.append(member);
    }
    QString backup;
    if(QFileInfo::exists(path)) {
        CResArchiveIndex source;if(!source.open(path,&error))return fail(error);
        const auto expected=resFileSha256(path,&error);if(expected.isEmpty())return fail(error);
        const auto result=commitStreamedResMembers(source,request,{},expected);if(!result.success)return fail(result.phase+": "+result.error);backup=result.backupPath;
    } else {
        QByteArray empty;QDataStream s(&empty,QIODevice::WriteOnly);s.setByteOrder(QDataStream::LittleEndian);s<<quint32(0x019ce23c)<<quint32(0)<<quint32(16)<<quint32(0);
        CResArchiveDocument seed;QStringList errors;if(!seed.loadBytes(empty,path,&errors))return fail(errors.join("\n"));
        QByteArray candidate;for(const auto& member:request.published) {
            if(!seed.buildWithAppendedMember(member.name,member.payload,0,&candidate,&errors)||!seed.loadBytes(candidate,path,&errors))return fail(errors.join("\n"));
        }
        SResArchiveBytesSaveRequest save;save.destination=QFileInfo(path);save.candidateBytes=candidate;save.requireAbsent=true;
        auto result=commitValidatedResArchiveBytes(save);if(!result.success)return fail(result.phase+": "+result.error);
    }
    CResArchiveIndex reopened;if(!reopened.open(path,&error))return fail(error);
    for(const auto& member:request.published)if(reopened.readMember(member.name)!=member.payload)return fail(tr("Export readback failed."));
    if(message)*message=tr("Verified %1 and %1m. Backup: %2").arg(name,backup);return true;
}
void CQuestMapDialog::exportToMod() {
    const QString path=QFileDialog::getSaveFileName(this,tr("Export quest map to texture RES"),QString(),tr("Evil Islands resource archive (*.res)"),nullptr,QFileDialog::DontConfirmOverwrite);
    if(path.isEmpty())return;
    const auto answer=QMessageBox::question(this,tr("Export quest map"),tr("Write the quest map pair to %1? Existing members with these names will be replaced after a verified backup. Other members will be preserved.").arg(QDir::toNativeSeparators(path)),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
    if(answer!=QMessageBox::Yes)return;QString message;const bool ok=exportArchive(path,true,&message);m_status->setText(message);if(!ok)return;
    QString name=m_textureName->text().trimmed();if(name.endsWith(".mmp",Qt::CaseInsensitive))name.chop(4);
    if(!m_source.entry.zoneId.isEmpty()&&name!=m_source.entry.objectivesTexture&&QMessageBox::question(this,tr("Register quest map"),tr("Open Map Catalog Editor to review the zone's objectives texture reference? Save there to publish map.txt."),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)==QMessageBox::Yes)
        emit catalogRegistrationReady(m_source.entry.zoneId,name);
}
void CQuestMapDialog::saveImage() {
    QString filter=tr("PNG image (*.png)");const QString path=QFileDialog::getSaveFileName(this,tr("Save quest map"),m_textureName->text()+".png",filter+";;"+tr("DDS image (*.dds)"),&filter);
    if(path.isEmpty())return;QSaveFile file(path);bool ok=file.open(QIODevice::WriteOnly);
    if(ok){if(path.endsWith(".dds",Qt::CaseInsensitive)){const auto bytes=questmap::encodeDds(m_document.composed());ok=file.write(bytes)==bytes.size();}else ok=m_document.composed().save(&file,"PNG");}
    if(ok)ok=file.commit();if(!ok)m_status->setText(tr("The image could not be written: %1").arg(file.errorString()));
}
