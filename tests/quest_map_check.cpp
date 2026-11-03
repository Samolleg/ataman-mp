// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ataman contributors
#include <QtWidgets>
#include <QtTest>
#include <windows.h>
#include <QtEndian>
#include <cmath>
#include "localization.h"
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "mob.h"
#include "settings.h"
#include "resource_setup_dialog.h"
#include "quest_map_dialog.h"
#include "mmp_inspector.h"
#include "mmp_codec.h"
#include "res_archive_index.h"
#include "map_table_document.h"
#include "map_table_editor_dialog.h"

namespace {
struct Hidden:QObject {
    bool eventFilter(QObject* obj,QEvent* event)override {
        if(event->type()==QEvent::Polish||event->type()==QEvent::Show)if(auto* w=qobject_cast<QWidget*>(obj))if(w->isWindow())w->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
int visible=0;
BOOL CALLBACK inspect(HWND w,LPARAM){DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(w))++visible;return TRUE;}
bool until(const std::function<bool()>& ready){QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<150000)QTest::qWait(10);return ready();}
QByteArray read(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
QByteArray sha(const QByteArray& data){return QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex();}
QHash<QString,QByteArray> members(const QString& path){CResArchiveIndex a;QHash<QString,QByteArray> out;if(!a.open(path))return out;for(const auto& m:a.members())out[m.name]=a.memberSha256(m.tableIndex);return out;}
int alphaDifferences(const QImage& a,const QImage& b){int count=0;for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x)if(qAlpha(a.pixel(x,y))!=qAlpha(b.pixel(x,y)))++count;return count;}
}
int runAssemblyCatalogCheck() {
    QTextStream out(stdout);out.setCodec("UTF-8");int passes=0;
    const auto check=[&](bool ok,const QString& text){out<<(ok?"PASS ":"FAIL ")<<text<<"\n";out.flush();if(!ok)throw text;++passes;};
    try {
        if(!qEnvironmentVariableIsEmpty("EI_QUEST_DISCOVER_MAPS")) {
            const QString maps=qEnvironmentVariable("EI_QUEST_DISCOVER_MAPS"),first=qEnvironmentVariable("EI_QUEST_TEST_ZONE","zone1");
            QStringList queue{QDir(maps).filePath(first+".mob")},paths;QSet<QString> seen;
            while(!queue.isEmpty()) {
                const auto path=QFileInfo(queue.takeFirst()).absoluteFilePath();if(seen.contains(path.toCaseFolded()))continue;seen.insert(path.toCaseFolded());
                CMobDocument doc;CValidationReport report;check(doc.loadBytes(read(path),path,&report),"native family discovery "+QFileInfo(path).fileName());
                QString script;check(doc.embeddedScript(&script),"native encrypted SS_TEXT decode");
                QStringList warnings;queue+=CMobFamilyValidator::addMobPaths(script,path,{maps},&warnings);
                check(warnings.isEmpty(),"native AddMob references resolved: "+warnings.join(" | "));paths<<path;
            }
            QFile result(qEnvironmentVariable("EI_QUEST_DISCOVER_OUTPUT"));check(result.open(QIODevice::WriteOnly),"family discovery receipt");result.write(QJsonDocument(QJsonArray::fromStringList(paths)).toJson());return 0;
        }
        const QString root=qEnvironmentVariable("EI_MCP_TEST_ROOT"),maps=qEnvironmentVariable("EI_MCP_TEST_MAPS");
        const QString zone=qEnvironmentVariable("EI_QUEST_TEST_ZONE","zone1");
        check(!root.isEmpty()&&!qEnvironmentVariableIsEmpty("EI_MAPPER_TEST_LOCK_ROOT"),"explicit private root and lock store");
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        check(ei::installApplicationTranslator(*qApp,QStringLiteral("ru")),"Russian production translation loads");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);qApp->setQuitOnLastWindowClosed(false);Hidden hidden;qApp->installEventFilter(&hidden);qApp->setProperty("workspaceLoadSmoke",true);
        QTimer visibility;QObject::connect(&visibility,&QTimer::timeout,[](){EnumWindows(inspect,0);});visibility.start(10);
        if(qEnvironmentVariableIsSet("EI_QUEST_CATALOG_ONLY")) {
            const QString sourcePath=root+"/Starter/MODS/DejaVu/res/texts.res",copy=root+"/catalog-copy.res";
            check(QFile::copy(sourcePath,copy),"private catalog archive copy");const auto originalHash=resFileSha256(sourcePath),copyHash=resFileSha256(copy);check(originalHash==copyHash,"catalog copy identity");
            const auto before=members(copy);CResArchiveIndex source;check(source.open(copy),"copied catalog index");const auto bytes=source.readMember("map.txt");CMapTableDocument table;check(table.loadBytes(bytes),"copied native map.txt");
            SMapTableZone selected;for(const auto& z:table.zones())if(z.mprName==zone&&z.mobName==zone&&z.maps.present){selected=z;break;}
            check(!selected.zoneId.isEmpty(),"authored game zone for catalog UI");auto expected=bytes;const auto token=selected.maps.minimapTexture.toLatin1()+" "+selected.maps.objectivesTexture.toLatin1();check(expected.count(token)==1,"unique objectives token for UI save");expected.replace(token,selected.maps.minimapTexture.toLatin1()+" quest275review");
            CMapTableEditorDialog editor;editor.setAttribute(Qt::WA_DontShowOnScreen);check(editor.openArchive(copy),"production Map Catalog Editor open copy");
            check(editor.stageObjectivesTextureForZone(selected.zoneId,"quest275review"),"production objectives staging method");
            check(editor.saveArchiveTo(copy),"production catalog save dispatch");check(until([&](){return !editor.isSaving();}),"production asynchronous catalog save finished");
            check(!editor.saveReports().isEmpty(),"production catalog save report");CResArchiveIndex reopened;check(reopened.open(copy),"saved catalog archive reopened");check(reopened.readMember("map.txt")==expected,"saved map.txt changes exactly one objectives token");
            const auto after=members(copy);check(after.size()==before.size(),"catalog member set retained");for(auto i=before.begin();i!=before.end();++i)if(i.key()!="map.txt")check(after.value(i.key())==i.value(),"unrelated catalog member byte-identical: "+i.key());
            check(resFileSha256(sourcePath)==originalHash,"source catalog archive untouched");EnumWindows(inspect,0);check(visible==0,"catalog UI zero visible native windows");out<<"RESULT "<<passes<<" PASS 0 FAIL\n";return 0;
        }
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[](){for(auto* w:QApplication::topLevelWidgets())if(auto* d=qobject_cast<CResourceSetupDialog*>(w))d->reject();});setup.start(10);
        // Missing-resource fallback is a real product path, with synthetic
        // terrain only. It must not depend on any extracted game artwork.
        const auto fallbackNotes=questmap::loadRuntimeIcons([](const QString&){return QByteArray();});
        check(!fallbackNotes.isEmpty()&&questmap::Icons().size()>=size_t(questmap::kKindCount),"missing textures have procedural palette and explanation");
        questmap::Terrain synthetic;synthetic.sectorsX=2;synthetic.sectorsY=1;synthetic.heightAt=[](float x,float y){return float(5*std::sin(x/8)*std::cos(y/8));};synthetic.waterKind=[](float x,float){return x<10?1:0;};questmap::Input generated;generated.terrain=&synthetic;
        generated.markers={{questmap::Kind::House,20,16,0},{questmap::Kind::GoblinHouse,45,12,0}};
        for(int f=0;f<3;++f){questmap::Options options;options.frame=static_cast<questmap::Frame>(f);questpixels::Image image;std::string error;check(questmap::Generate(generated,options,image,error),"procedural-only frame "+QString::number(f));check(questmap::image(image).save(root+"/fallback-frame-"+QString::number(f)+".png"),"fallback snapshot");}
        questmap::Kind kind;check(questmap::Classify("stbuho4",kind)&&kind==questmap::Kind::GoblinHouse&&questmap::Classify("stbuho9",kind)&&kind==questmap::Kind::GoblinHouses,"goblin model classification4/9");
        check(questmap::Classify("stbr11",kind)&&kind==questmap::Kind::BridgeStone&&questmap::Classify("stbr1",kind)&&kind==questmap::Kind::BridgeWood&&!questmap::Classify("random_unrelated_model",kind),"bridge classification and unknown exclusion");
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);window.m_currentCollectionPath=root+"/private.collection";
        window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1200,800);window.show();
        auto& view=*window.view();QCoreApplication::processEvents();view.makeCurrent();check(view.isValid(),"production MainWindow GL offscreen");view.m_neighborLandscapesVisible=false;
        const auto mprBefore=sha(read(maps+"/"+zone+".mpr")),mobBefore=sha(read(maps+"/"+zone+".mob"));
        check(window.loadLandscapeWithProgress(QFileInfo(maps+"/"+zone+".mpr")),"production MPR open");view.m_timer->stop();view.makeCurrent();check(view.ensureTerrainAutoCatalogReady(),"terrain ready before MOB publication");
        check(window.loadMobWithProgress(QFileInfo(maps+"/"+zone+".mob"),false),"production primary MOB open");check(until([&](){return !window.backgroundMobLoading();}),"complete MOB family");
        check(view.mobs().size()==qEnvironmentVariableIntValue("EI_QUEST_TEST_FAMILY_COUNT"),"all native-discovered family MOBs loaded");
        window.m_minimapRefreshTimer->stop();window.m_minimapCameraTimer->stop();
        check(window.findChild<QAction*>("questMapEditorAction")!=nullptr,"Tools action beside minimap export");
        auto entry=window.m_mapTestEntry;
        if(entry.isBriefing()||entry.objectivesTexture.isEmpty()||entry.mobName!=zone)
            for(const auto& e:window.m_mapCatalog.entries)if(e.mprName==zone&&e.mobName==zone&&!e.isBriefing()){entry=e;break;}
        check(entry.mprName==zone&&entry.mobName==zone&&!entry.objectivesTexture.isEmpty(),"map.txt objectives reference for current MPR/MOB");
        // Select the authored game zone among the MPR's briefing/game entries,
        // as the real map identity selector does; this is not the file stem.
        window.m_mapTestEntry=entry;
        SQuestMapSource source;QString error;check(CQuestMapSource::snapshot(&view,entry,&source,&error),"production scene snapshot: "+error);
        check(!source.markers.empty(),"model classification of current MOB family");check(!source.original.isNull(),"configured original quest texture decoded");
        int extracted=0;for(const auto& icon:source.icons)if(icon.name=="021")for(auto p:icon.image.rgba)extracted+=p!=0;
        check(extracted>0,"runtime icon palette has nonempty pixels");
        CResArchiveIndex stock;check(stock.open(root+"/res/textures.res"),"stock resource archive read only");auto stockMap=ei::inspectMmp(stock.readMember("zone3quest.mmp"));
        const QImage expectedCut=questmap::cutIcon(stockMap.baseImage,QRect(98,136,13,15));bool exactCut=false;
        for(const auto& icon:source.icons)if(icon.name=="021")exactCut=questmap::image(icon.image).convertToFormat(QImage::Format_ARGB32)==expectedCut;
        check(exactCut&&!expectedCut.isNull(),"runtime021 cut matches original texture and TSV coordinates");
        check(source.original.save(root+"/original.png"),"original reference PNG");
        window.generateQuestMap();QCoreApplication::processEvents();auto* dialog=window.findChild<CQuestMapDialog*>("questMapDialog");
        check(dialog!=nullptr,"actual MainWindow action creates production dialog");dialog->setAttribute(Qt::WA_DeleteOnClose,false);dialog->show();QTest::qWait(30);
        auto& doc=dialog->document();check(doc.state().paper.size()==QSize(256,256),"default256 parchment");
        auto* frame=dialog->findChild<QComboBox*>("questMapFrame");auto* canvas=dialog->findChild<QWidget*>("questMapCanvas");
        auto* add=dialog->findChild<QPushButton*>("questMapAdd");auto* remove=dialog->findChild<QPushButton*>("questMapDelete");auto* scale=dialog->findChild<QSpinBox*>("questMapStampScale");
        auto* undo=dialog->findChild<QPushButton*>("questMapUndo");auto* redo=dialog->findChild<QPushButton*>("questMapRedo");auto* bake=dialog->findChild<QPushButton*>("questMapBake");
        check(frame&&canvas&&add&&remove&&scale&&undo&&redo&&bake,"stamp controls present");
        QVector<QImage> frames;
        for(int f=0;f<3;++f) {
            frame->setCurrentIndex(f);check(dialog->regenerate(),"generate frame "+QString::number(f));
            const auto im=doc.composed();frames<<im;check(im.save(root+"/frame-"+QString::number(f)+".png"),"save frame preview");
            QImage ui(dialog->size(),QImage::Format_ARGB32);ui.fill(Qt::transparent);dialog->render(&ui);check(ui.save(root+"/dialog-frame-"+QString::number(f)+".png"),"offscreen production dialog render");
        }
        check(frames[0]!=frames[1]&&frames[1]!=frames[2]&&frames[0]!=frames[2],"three distinct frames");
        frame->setCurrentIndex(0);check(dialog->regenerate(),"restore torn frame");const auto regenerated=doc.composed();
        check(dialog->regenerate()&&doc.composed()==regenerated,"deterministic seed");
        auto* seed=dialog->findChild<QSpinBox*>("questMapSeed");seed->setValue(9);QTest::qWait(220);check(doc.composed()!=regenerated,"live preview seed change");seed->setValue(1);check(dialog->regenerate(),"restore seed");
        // Exercise controls and real canvas events. Every gesture is one Undo.
        const int count=int(doc.state().stamps.size());const QImage beforeStamp=doc.composed();add->click();check(int(doc.state().stamps.size())==count+1,"palette add stamp");
        const int added=int(doc.state().stamps.size())-1;const auto stamp=doc.state().stamps[added];const int history=doc.history()->index();
        const double zoom=qMin(canvas->width()/256.0,canvas->height()/256.0);const QPoint center(canvas->width()/2,canvas->height()/2);const QPoint target=center+QPoint(qRound(16*zoom),qRound(12*zoom));
        // Deliver widget-local events. QTest::mouseMove may move the real OS
        // cursor on Windows and is forbidden by this fixture's boundary.
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(center),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&press);
        QMouseEvent move(QEvent::MouseMove,QPointF(target),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(canvas,&move);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(target),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(canvas,&release);
        check(doc.history()->index()==history+1&&doc.state().stamps[added].x!=stamp.x,"canvas drag is one Undo operation");
        undo->click();check(doc.state().stamps[added].x==stamp.x&&doc.state().stamps[added].y==stamp.y,"Undo restores move");redo->click();check(doc.state().stamps[added].x!=stamp.x,"Redo restores move");
        scale->setValue(3);check(doc.state().stamps[added].scale==3,"selected stamp scale control");
        const auto scaled=doc.composed();remove->click();check(int(doc.state().stamps.size())==count,"delete selected stamp");undo->click();check(doc.composed()==scaled,"Undo restores deleted scaled stamp");
        const auto beforeBake=doc.composed();bake->click();check(doc.state().stamps.empty()&&doc.composed()==beforeBake,"bake preserves composed image");undo->click();check(!doc.state().stamps.empty()&&doc.composed()==beforeBake,"Undo restores stamp layer after bake");
        check(alphaDifferences(doc.state().paper,doc.composed())==0,"stamps never paint transparent parchment");
        doc.move(0,QPoint(0,0));check(alphaDifferences(doc.state().paper,doc.composed())==0,"edge clipping keeps alpha exact");doc.history()->undo();
        // File encoders and game loader roundtrip with original stock5551.
        const auto encoded=questmap::encode5551(doc.composed());const auto native=ei::inspectMmp(encoded);
        check(native.valid&&native.width==256&&native.height==256&&native.format==0x5551,"5551 native load");check(alphaDifferences(doc.composed(),native.baseImage)==0,"5551 binary alpha identical");
        QImage opaque=ei::inspectMmp(questmap::encode5551(doc.composed(),true)).baseImage;bool allOpaque=true;for(int y=0;y<opaque.height();++y)for(int x=0;x<opaque.width();++x)allOpaque&=qAlpha(opaque.pixel(x,y))==255;
        check(allOpaque,"questm opaque companion");
        const auto dds=questmap::encodeDds(doc.composed());QFile ddsFile(root+"/quest-map.dds");check(ddsFile.open(QIODevice::WriteOnly)&&ddsFile.write(dds)==dds.size(),"DDS export bytes");ddsFile.close();
        check(dds.size()==128+256*256*4&&dds.left(4)=="DDS ","DDS32 header and size");
        const auto dword=[&](int offset){return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(dds.constData()+offset));};
        check(dword(4)==124&&dword(12)==256&&dword(16)==256&&dword(76)==32&&dword(88)==32&&dword(92)==0xff0000&&dword(104)==0xff000000,"independent DDS header fields");
        QImage independent(256,256,QImage::Format_ARGB32);for(int y=0;y<256;++y)for(int x=0;x<256;++x)independent.setPixel(x,y,dword(128+(y*256+x)*4));
        check(independent==doc.composed().convertToFormat(QImage::Format_ARGB32),"independent DDS32 pixel readback exact");
        QImage ddsImage;QImageReader reader(root+"/quest-map.dds","dds");ddsImage=reader.read();
        // Qt's bundled DDS plugin is not guaranteed on development PATH. A
        // missing plugin is reported explicitly, never used as a fake PASS.
        if(ddsImage.isNull())out<<"INFO Qt DDS plugin unavailable: "<<reader.errorString()<<"\n";
        else check(ddsImage.convertToFormat(QImage::Format_ARGB32)==doc.composed().convertToFormat(QImage::Format_ARGB32),"DDS32 loader exact pixels");
        // The copied destination contains existing originals and unrelated
        // members. Replacement refusal and backup use the real exporter.
        const QString destination=root+"/export-copy.res";const auto before=members(destination);check(!before.isEmpty(),"copied mod archive");
        const auto fileBefore=sha(read(destination));check(!dialog->exportArchive(destination,false,&error)&&sha(read(destination))==fileBefore,"replace requires confirmation and leaves bytes unchanged");
        auto* format=dialog->findChild<QComboBox*>("questMapFormat");format->setCurrentIndex(0);const bool exported=dialog->exportArchive(destination,true,&error);check(exported,"confirmed original5551 RES transaction: "+error);
        bool verifiedBackup=false;QDirIterator backups(root+"/backups",QDir::Files,QDirIterator::Subdirectories);while(backups.hasNext()){const auto path=backups.next();if(QFileInfo(path).size()>100000000&&resFileSha256(path)==fileBefore){verifiedBackup=true;break;}}
        check(verifiedBackup,"pre-export archive backup hash matches exact original");
        auto after=members(destination);int changed=0;for(auto i=before.begin();i!=before.end();++i)if(after.value(i.key())!=i.value()){++changed;check(i.key()==entry.objectivesTexture+".mmp"||i.key()==entry.objectivesTexture+"m.mmp","only intended pair changed");}
        check(changed>=1&&changed<=2,"all unrelated members byte-identical");
        CResArchiveIndex archive;check(archive.open(destination),"reopen exported RES");auto saved=ei::inspectMmp(archive.readMember(entry.objectivesTexture+".mmp"));check(saved.valid&&saved.format==0x5551&&alphaDifferences(doc.composed(),saved.baseImage)==0,"native exported quest readback");
        const QString fresh=root+"/fresh-pair.res";check(dialog->exportArchive(fresh,false,&error),"new archive pair transaction");CResArchiveIndex freshArchive;check(freshArchive.open(fresh)&&freshArchive.memberCount()==2,"new archive has exactly the two intended members");
        auto* originalProfile=dialog->findChild<QPushButton*>("questMapOriginalProfile");originalProfile->click();check(dialog->regenerate(),"original profile generation");
        check(dialog->exportArchive(destination,true,&error),"original mod size/format export");archive.open(destination);saved=ei::inspectMmp(archive.readMember(entry.objectivesTexture+".mmp"));
        check(saved.valid&&saved.width==source.original.width()&&saved.height==source.original.height()&&saved.format==source.originalFormat,"mod original dimensions and format retained");
        check(alphaDifferences(doc.composed(),saved.baseImage)==0,"mod profile binary alpha exact");
        check(sha(read(maps+"/"+zone+".mpr"))==mprBefore&&sha(read(maps+"/"+zone+".mob"))==mobBefore,"generation/edit/export do not save MPR/MOB");
        // Lossless map.txt change uses the same canonical document as the
        // catalog editor. Only the objectives token changes; no auto publish.
        CResArchiveIndex catalog;check(catalog.open(entry.sourceArchive),"catalog source");const auto mapBytes=catalog.readMember("map.txt");CMapTableDocument table;check(table.loadBytes(mapBytes),"native map.txt parse");
        int index=-1;for(int i=0;i<table.zones().size();++i)if(table.zones()[i].zoneId==entry.zoneId)index=i;check(index>=0,"catalog zone found");auto replacement=table.zones()[index];replacement.maps.objectivesTexture="quest275review";
        check(table.replaceZoneAt(index,replacement).success,"stage objectives token");const auto edited=table.serialize();auto expected=mapBytes;const QByteArray originalToken=entry.minimapTexture.toLatin1()+" "+entry.objectivesTexture.toLatin1();check(expected.count(originalToken)==1,"unambiguous authored texture tokens");expected.replace(originalToken,entry.minimapTexture.toLatin1()+" quest275review");check(edited==expected,"map.txt edits only objectives token byte-for-byte");
        QImage contact(3*256,2*256,QImage::Format_ARGB32);contact.fill(QColor(45,40,32));QPainter painter(&contact);for(int f=0;f<3;++f){painter.drawImage(f*256,0,frames[f]);painter.drawImage(QRect(f*256,256,256,256),source.original);}painter.end();check(contact.save(root+"/comparison.png"),"generated frames and original comparison");
        QJsonObject evidence{{"zone",zone},{"originalFormat",QString::number(source.originalFormat,16)},{"originalWidth",source.original.width()},{"markers",int(source.markers.size())},{"runtimeIcons",int(source.icons.size())},{"notes",QJsonArray::fromStringList(source.notes)},{"passes",passes}};
        QFile receipt(root+"/native-receipt.json");check(receipt.open(QIODevice::WriteOnly),"native evidence");receipt.write(QJsonDocument(evidence).toJson());receipt.close();
        EnumWindows(inspect,0);check(visible==0,"zero visible native windows");dialog->close();delete dialog;window.close();
        out<<"RESULT "<<passes<<" PASS 0 FAIL\n";return 0;
    }catch(const QString& error){out<<"ERROR "<<error<<"\nRESULT "<<passes<<" PASS 1 FAIL\n";return 1;}
}
