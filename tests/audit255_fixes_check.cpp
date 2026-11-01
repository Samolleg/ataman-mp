#include <QtWidgets>
#include <QtTest>
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "world_terrain_session.h"

namespace {
QTextStream out(stdout);
int passed=0;
void check(bool ok,const QString& text) {
    out<<(ok?"PASS ":"FAIL ")<<text<<'\n';out.flush();
    if(!ok)throw text;
    ++passed;
}
class Hidden final : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish||event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        if(event->type()==QEvent::Show)if(auto* dialog=qobject_cast<QMessageBox*>(object))QTimer::singleShot(0,dialog,&QDialog::reject);
        return false;
    }
};
class CounterCommand final : public QUndoCommand {
    int* m_counter;
public:
    explicit CounterCommand(int* value):m_counter(value){setText("Synthetic object command");}
    void undo() override {--*m_counter;}
    void redo() override {++*m_counter;}
};
}

int runAssemblyCatalogCheck()
{
    try {
        out.setCodec("UTF-8");
        const auto root=QDir::fromNativeSeparators(qEnvironmentVariable("EI_AUDIT_FIX_ROOT"));
        check(!root.isEmpty(),"explicit isolated regression root");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        qApp->setQuitOnLastWindowClosed(false);
        Hidden hidden;qApp->installEventFilter(&hidden);
        MainWindow window;window.setAttribute(Qt::WA_DontShowOnScreen);
        window.configureAutosaveTimer(false,300000);window.resize(900,650);window.show();qApp->processEvents();
        auto& view=*window.view();view.makeCurrent();check(view.isValid(),"hidden native renderer initialized");

        const auto fixture=qEnvironmentVariable("EI_AUDIT_FIX_MPR");
        for(int limit:{2,512}) {
            window.m_undoStack->clear();window.m_undoStack->setUndoLimit(limit);
            view.m_worldTerrainSession.clear();
            QMap<QString,QSharedPointer<SWorldTerrainDocument>> documents;
            QMap<QString,QByteArray> initial,changed;
            SWorldTerrainStroke stroke;
            for(const QString id:{QString("a"),QString("b")}) {
                auto doc=QSharedPointer<SWorldTerrainDocument>::create();doc->path=root+'/'+id+".mpr";
                CValidationReport report;
                check(doc->baseline.load(QFileInfo(fixture),&report)&&doc->terrain.load(doc->baseline,&report),"synthetic terrain "+id);
                initial[id]=doc->terrain.sectorPayload(0,0);
                STerrainBrush brush;brush.centerX=5;brush.centerY=5;brush.radius=2;
                stroke.maps[id]=doc->terrain.makeRaiseLower(brush,1);
                documents[id]=doc;view.m_worldTerrainSession.documents[id]=doc;
            }
            int counter=0;
            for(int i=0;i<limit;++i)window.m_undoStack->push(new CounterCommand(&counter));
            QString error;
            check(view.m_worldTerrainSession.apply(stroke,true,&error),"two-map stroke applied atomically");
            for(const auto& id:documents.keys())changed[id]=documents[id]->terrain.sectorPayload(0,0);
            view.recordWorldTerrainStroke(stroke);
            check(window.m_undoStack->count()==limit&&view.nextWorldTerrainStroke(false),"world stroke belongs to bounded common stack");
            view.m_worldTerrainEnabled=true;view.m_landscapeTools->setEditingEnabled(true,false);
            window.m_undoStack->push(new CounterCommand(&counter));
            window.on_actionUndo_triggered();
            check(counter==limit&&documents["a"]->terrain.sectorPayload(0,0)==changed["a"],"at history limit Undo reverses newest ordinary command first");
            window.on_actionUndo_triggered();
            check(documents["a"]->terrain.sectorPayload(0,0)==initial["a"]&&documents["b"]->terrain.sectorPayload(0,0)==initial["b"],"next Undo reverses entire two-map world stroke");
            window.on_actionRedo_triggered();window.on_actionRedo_triggered();
            check(counter==limit+1&&documents["a"]->terrain.sectorPayload(0,0)==changed["a"]&&documents["b"]->terrain.sectorPayload(0,0)==changed["b"],"Redo restores world then ordinary command in order");
            window.on_actionUndo_triggered();window.on_actionUndo_triggered();
            window.m_undoStack->push(new CounterCommand(&counter));
            check(!window.m_undoStack->canRedo()&&!view.nextWorldTerrainStroke(true),"ordinary branch discards abandoned world Redo");

            window.m_undoStack->clear();
            check(view.m_worldTerrainSession.apply(stroke,true,&error),"prepare protected world command");
            view.recordWorldTerrainStroke(stroke);
            view.m_worldTerrainSession.documents.remove("b");
            const int heldIndex=window.m_undoStack->index();
            window.on_actionUndo_triggered();
            check(window.m_undoStack->index()==heldIndex&&documents["a"]->terrain.sectorPayload(0,0)==changed["a"],"missing map refuses Undo without moving index or partially changing terrain");
            window.m_undoView->selectionModel()->setCurrentIndex(window.m_undoView->model()->index(0,0),QItemSelectionModel::ClearAndSelect);
            check(window.m_undoStack->index()==heldIndex&&window.m_undoView->currentIndex().row()==heldIndex,"history selection model obeys same failed-Undo guard");
            QTest::keyClick(window.m_undoView,Qt::Key_Home);
            check(window.m_undoStack->index()==heldIndex,"history keyboard cannot bypass failed-Undo guard");
            view.m_worldTerrainSession.documents["b"]=documents["b"];
            view.m_worldTerrainEnabled=false;view.m_landscapeTools->setEditingEnabled(false,false);
            window.on_actionUndo_triggered();
            check(window.m_undoStack->index()==0&&documents["a"]->terrain.sectorPayload(0,0)==initial["a"],"world Undo also works after leaving terrain mode");
            window.on_actionRedo_triggered();
            for(const auto& id:documents.keys()) {
                CMprEditTransaction transaction("synthetic history save");QByteArray bytes;CValidationReport report;
                check(documents[id]->terrain.appendSectorEdits(&transaction,&report)&&documents[id]->baseline.applyTransaction(transaction,&bytes,&report),"serialize world state after Redo "+id);
                CMprDocument reopened;CTerrainEditModel terrain;
                check(reopened.loadBytes(bytes,"synthetic",&report)&&terrain.load(reopened,&report)&&terrain.sectorPayload(0,0)==changed[id],"saved world state reopens exactly "+id);
            }
            for(int i=0;i<limit;++i)window.m_undoStack->push(new CounterCommand(&counter));
            check(window.m_undoStack->count()==limit&&!view.nextWorldTerrainStroke(false)&&!view.m_worldTerrainSession.canUndo(),"eviction has no separate unbounded world history");
            window.m_undoStack->clear();view.m_worldTerrainSession.clear();
        }
        out<<passed<<" PASS / 0 FAIL\n";out.flush();return 0;
    } catch(const QString& error) {out<<"FAIL "<<error<<'\n';out.flush();return 1;}
}
