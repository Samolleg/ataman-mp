#include <QtWidgets>
#include <algorithm>
#include <stdexcept>
#include "main_window.h"
#include "view.h"
#include "resource_setup_dialog.h"
#include "ai_agent_edit.h"
#include "synthetic_terrain_fixture.h"

namespace {
class HiddenWindows : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish || event->type()==QEvent::Show)
            if(auto* widget=qobject_cast<QWidget*>(object))
                if(widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
}

// Reuse the application's test entry point and the exact Release objects.
int runAssemblyCatalogCheck()
{
    QTextStream out(stdout);int checks=0;
    const auto require=[&](bool condition,const char* message) {
        out<<(condition?"PASS ":"FAIL ")<<message<<'\n';out.flush();
        if(!condition)throw std::runtime_error(message);++checks;
    };
    try {
        QTemporaryDir root;require(root.isValid(),"owned isolated settings and synthetic data");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path()+"/settings");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        HiddenWindows hidden;qApp->installEventFilter(&hidden);
        qApp->setProperty("workspaceLoadSmoke",true);
        QTimer setup;QObject::connect(&setup,&QTimer::timeout,[] {
            for(auto* widget:QApplication::topLevelWidgets())
                if(auto* dialog=qobject_cast<CResourceSetupDialog*>(widget))dialog->reject();
        });setup.start(10);
        MainWindow window;setup.stop();window.configureAutosaveTimer(false,300000);
        window.m_minimapCameraTimer->stop();window.m_minimapRefreshTimer->stop();
        auto& view=*window.view();
        const auto bytes=ei_test::terrainMpr(16,16);
        require(view.m_mprDocument.loadBytes(bytes,"synthetic.mpr"),"valid owned 16x16-sector MPR");
        require(view.m_terrainEdit.load(view.m_mprDocument),"production editable terrain initialized");
        view.m_mprMaterials=view.m_mprDocument.mapInfo().materials;
        for(int i=0;i<8;++i){
            auto document=QSharedPointer<SWorldTerrainDocument>::create();
            document->path=root.filePath(QString("neighbor-%1.mpr").arg(i));
            document->baseline=view.m_mprDocument;document->terrain=view.m_terrainEdit;
            document->materials=view.m_mprMaterials;
            view.m_worldTerrainSession.documents.insert(CWorldTerrainSession::key(document->path),document);
        }
        require(!CAiEditAdapter::busy(&window),"revision test has no gesture, load or modal interaction");
        QElapsedTimer timer;timer.start();
        const QString initial=CAiEditAdapter::revision(&window);
        const qint64 cold=timer.nsecsElapsed();
        require(initial.size()==64,"production MCP revision is a SHA256 token");
        QVector<qint64> timings;
        for(int i=0;i<100;++i){
            timer.restart();const auto revision=CAiEditAdapter::revision(&window);
            timings.append(timer.nsecsElapsed());
            require(revision==initial,"unchanged production revision is stable");
        }
        std::sort(timings.begin(),timings.end());
        out<<"BENCH maps=9 sectorsPerMap=256 verticesPerMap="
           <<view.m_terrainEdit.vertexCountX()*view.m_terrainEdit.vertexCountY()
           <<" coldMs="<<cold/1e6<<" warmMedianMs="<<timings[50]/1e6
           <<" warmP95Ms="<<timings[94]/1e6<<'\n';out.flush();
        // Direct edits model a manual change between prepare and apply.
        const auto command=view.m_terrainEdit.makeGlobalOffset(10);
        require(view.m_terrainEdit.apply(command,true),"manual height edit succeeds");
        const QString edited=CAiEditAdapter::revision(&window);
        require(edited!=initial,"manual mutation invalidates the production MCP revision");
        ai::EditPlan stale;stale.method="sculpt";stale.revision=initial;
        const auto refused=CAiEditAdapter::apply(&window,stale);
        require(QJsonDocument(refused).toJson().contains("Document revision conflict"),
                "stale prepared operation is rejected by production apply");
        require(CAiEditAdapter::revision(&window)==edited,"conflict refusal leaves the terrain unchanged");
        require(view.m_terrainEdit.apply(command,false),"manual edit Undo succeeds");
        require(CAiEditAdapter::revision(&window)==initial,"Undo restores the original content revision");
        auto neighbor=view.m_worldTerrainSession.documents.first();
        require(neighbor->terrain.apply(command,true),"detached neighbor mutation succeeds");
        require(CAiEditAdapter::revision(&window)!=initial,"neighbor mutation invalidates the session revision");
        require(neighbor->terrain.apply(command,false),"neighbor Undo succeeds");
        require(CAiEditAdapter::revision(&window)==initial,"neighbor Undo restores the session revision");
        view.m_worldTerrainSession.clear();view.m_mprDocument=CMprDocument();
        view.m_terrainEdit=CTerrainEditModel();view.m_mprMaterials.clear();
        out<<"PASS ai-agent-revision "<<checks<<" checks\n";out.flush();return 0;
    }catch(const std::exception& error){out<<"ERROR "<<error.what()<<'\n';out.flush();return 1;}
}
