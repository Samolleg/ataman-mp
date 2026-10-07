#include "ai_graph_regeneration_check.h"
#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "progressview.h"
#include "tile_form.h"
#include "tree_view.h"
#include "endurance_document.h"
#include "ai_graph_preview_invalidation.h"
#include <QApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QtConcurrent>
#include <functional>

namespace {
class CGraphCheckTerrainCommand : public QUndoCommand,
                                 public IAiGraphPreviewInvalidatingCommand
{
public:
    CGraphCheckTerrainCommand(CView* view, const STerrainHeightCommand& command)
        : m_view(view), m_command(command) { setText("Synthetic terrain change"); }
    void redo() override { m_view->applyTerrainHeightPreview(m_command, true); }
    void undo() override { m_view->applyTerrainHeightPreview(m_command, false); }
    bool invalidatesAiGraphPreview() const override { return true; }
private:
    CView* m_view;
    STerrainHeightCommand m_command;
};
}

int runAiGraphRegenerationCheck()
{
    QTextStream out(stdout); int failures=0;
    auto check=[&](bool ok, const char* text) {
        out<<(ok?"PASS: ":"FAIL: ")<<text<<"\n";out.flush();if(!ok)++failures;
        return ok;
    };
    auto pump=[](int milliseconds) {
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<milliseconds) {
            QApplication::processEvents(QEventLoop::AllEvents,20);QThread::msleep(1);
        }
    };
    auto wait=[&](const std::function<bool()>& ready) {
        QElapsedTimer timer;timer.start();
        do {pump(10);if(ready())return true;} while(timer.elapsed()<7000);
        return false;
    };
    QTemporaryDir temporary; if(!temporary.isValid())return 2;
    CMprDocument source,mpr;CTerrainEditModel terrain;CValidationReport report;
    SMprCreationRequest request;request.innerNamespace="ai-explicit-check";
    request.flatRawHeight=10000;request.maxZ=100;
    QByteArray bytes;
    if(!source.load(QFileInfo(qEnvironmentVariable("EI_TEST_TERRAIN_FIXTURE")),&report)
       ||!source.buildNewCanvas(request,&bytes,&report)
       ||!mpr.loadBytes(bytes,"ai-explicit-check.mpr",&report)
       ||!terrain.load(mpr,&report))return 2;
    const QString path=temporary.filePath("ai-explicit-check.mpr");
    QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size())return 2;
    file.close();
    CSettings settings;QTableWidget properties;properties.setColumnCount(2);
    QUndoStack history;CProgressView progress;QLineEdit coordinates;CTreeView tree;CTileForm tiles;
    CView view(nullptr,nullptr);view.setAttribute(Qt::WA_DontShowOnScreen);
    view.attach(&settings,&properties,&history,&progress,&coordinates,&tree,&tiles);
    view.resize(320,240);view.show();pump(100);view.makeCurrent();
    view.m_pLand->readMap(QFileInfo(path),false);
    view.m_mprDocument=mpr;view.m_terrainEdit=terrain;view.m_terrainEditAvailable=true;
    view.m_pLand->setCanonicalTerrainModel(&view.m_terrainEdit);
    view.syncAiGraphPreviewWidget();
    int starts=0,finishes=0;
    view.m_aiGraphRegenerateButton->click();
    if(!view.m_aiGraphBuildWatcher)return 3;
    QObject::connect(view.m_aiGraphBuildWatcher,&QFutureWatcher<SAiGraphPreviewBuildResult>::started,
        &view,[&](){++starts;});
    QObject::connect(view.m_aiGraphBuildWatcher,&QFutureWatcher<SAiGraphPreviewBuildResult>::finished,
        &view,[&](){++finishes;});
    auto idle=[&](){return !view.m_aiGraphBuildWatcher->isRunning()
        &&!view.m_aiGraphRegenerationPending&&!view.m_aiGraphRegenerationTimer->isActive()
        &&starts==finishes;};
    if(!check(wait([&](){return view.m_aiGraphRegeneratedFresh&&idle();}),
              "explicit graph calculation completes"))return 1;
    check(starts==1&&finishes==1,"one click starts exactly one worker");
    const QByteArray originalGraph=view.m_aiGraphDocument.serialize();
    view.m_aiGraphLoadedDocument=view.m_aiGraphDocument;
    view.m_aiGraphLoadedBytes=originalGraph;view.m_aiGraphLoadedSource="Embedded: synthetic";
    view.syncAiGraphPreviewWidget();
    pump(1100);check(starts==1&&idle(),"idle graph does not recalculate");
    view.m_aiGraphLayerSlider->setValue(4);view.m_aiGraphLayerSlider->setValue(3);
    history.push(new QUndoCommand("Unrelated history"));
    pump(700);check(starts==1&&view.m_aiGraphRegeneratedFresh,
        "layer and unrelated history preserve fresh graph without recalculation");
    view.m_aiGraphPreviewToggle->click();
    // Height commands use raw 16-bit units, not meters. Use a hill large enough
    // to change navigation, rather than a sub-quantum change to display height.
    auto edit=[&](float amount=500.0f) {
        STerrainBrush brush;brush.centerX=8;brush.centerY=8;brush.radius=2;
        history.push(new CGraphCheckTerrainCommand(&view,view.m_terrainEdit.makeRaiseLower(brush,amount)));
    };
    edit(20000);pump(800);
    check(starts==1&&idle()&&!view.m_aiGraphRegeneratedFresh,
        "terrain change with hidden graph marks stale without starting a worker");
    check(view.m_aiGraphDocument.serialize()==originalGraph,
        "stale preview retains previous graph bytes");
    check(view.m_aiGraphSourceLabel->text().contains(view.tr(
        "Graph is out of date. Click Regenerate preview.")),"stale graph is visibly identified");
    history.undo();pump(700);history.redo();pump(700);
    check(starts==1&&idle()&&!view.m_aiGraphRegeneratedFresh,
        "terrain Undo and Redo do not start workers");
    QByteArray candidate;QString error;SAiGraphPreviewBuildInput input;
    const bool captured=view.captureAiGraphPreviewBuildInput(&input,&error);
    const auto expected=CAiGraphPreviewBuilder::build(input);
    CAiGraphDocument current;
    const bool built=view.buildGeneratedAiGraphCandidate(&candidate,&error)&&current.parse(candidate,&error);
    check(captured&&expected.valid&&built&&current.compareNavigation(expected.document).identical(),
        "save and test candidate uses current terrain when preview is stale");
    check(built&&!current.compareNavigation(view.m_aiGraphLoadedDocument).identical(),
        "changed-terrain candidate differs from old loaded graph");
    check(starts==1&&idle()&&!view.m_aiGraphRegeneratedFresh,
        "candidate preparation leaves explicit preview lifecycle unchanged");
    view.m_aiGraphSourceSelectorButton->click();pump(700);
    check(view.m_aiGraphPreviewUsesLoadedSource&&starts==1&&idle()
          &&view.m_aiGraphDocument.serialize()==originalGraph,
        "loaded source remains independent of terrain changes");
    QByteArray preserved;
    check(view.buildGeneratedAiGraphCandidate(&preserved,&error)&&preserved==originalGraph,
        "selected loaded candidate preserves exact bytes");
    view.m_aiGraphSourceSelectorButton->click();pump(700);
    check(!view.m_aiGraphPreviewUsesLoadedSource&&starts==1&&idle()&&!view.m_aiGraphRegeneratedFresh,
        "selecting stale generated source does not calculate implicitly");

    const int oldPoolMaximum=QThreadPool::globalInstance()->maxThreadCount();
    QThreadPool::globalInstance()->setMaxThreadCount(1);
    const int beforeChangedWorker=starts;
    auto blocker=QtConcurrent::run([](){QThread::msleep(1000);});
    view.m_aiGraphRegenerateButton->click();QTimer::singleShot(100,&view,[&](){edit();});
    check(wait(idle)&&starts==beforeChangedWorker+1&&!view.m_aiGraphRegeneratedFresh,
        "input change during a worker discards result without automatic retry");
    const int afterChangedWorker=starts;pump(900);
    check(starts==afterChangedWorker&&idle(),"discarded stale worker stays stopped in idle");

    const int beforeExplicitQueue=starts;
    blocker=QtConcurrent::run([](){QThread::msleep(1100);});
    view.m_aiGraphRegenerateButton->click();
    QTimer::singleShot(100,&view,[&](){edit();});
    QTimer::singleShot(150,&view,[&](){view.m_aiGraphRegenerateButton->click();});
    QTimer::singleShot(250,&view,[&](){view.m_aiGraphRegenerateButton->click();});
    QTimer::singleShot(300,&view,[&](){edit();});pump(700);
    check(view.m_aiGraphRegenerationPending&&view.m_aiGraphBuildWatcher->isRunning(),
        "explicit clicks queue one calculation while worker is busy");
    check(wait([&](){return idle()&&view.m_aiGraphRegeneratedFresh;})&&starts==beforeExplicitQueue+2,
        "multiple explicit clicks coalesce into exactly one follow-up");
    input=SAiGraphPreviewBuildInput();view.captureAiGraphPreviewBuildInput(&input,&error);
    const auto afterQueue=CAiGraphPreviewBuilder::build(input);
    check(afterQueue.valid&&afterQueue.document.compareNavigation(view.m_aiGraphDocument).identical(),
        "explicit follow-up uses latest input including later terrain change");
    const int afterExplicitQueue=starts;pump(900);
    check(starts==afterExplicitQueue&&idle(),"explicit follow-up does not leave an automatic loop");

    blocker=QtConcurrent::run([](){QThread::msleep(1000);});
    view.m_aiGraphRegenerateButton->click();
    QTimer::singleShot(100,&view,[&](){view.m_aiGraphRegenerateButton->click();});
    QTimer::singleShot(150,&view,[&](){view.m_aiGraphSourceSelectorButton->click();});
    check(wait(idle)&&starts==afterExplicitQueue+1&&view.m_aiGraphPreviewUsesLoadedSource
          &&view.m_aiGraphDocument.serialize()==originalGraph,
        "switching to loaded graph cancels explicit queue and rejects in-flight result");
    view.m_aiGraphSourceSelectorButton->click();pump(700);
    check(starts==afterExplicitQueue+1&&idle(),"switching back to generated graph does not restart cancelled work");
    const int beforeFailure=starts;view.m_terrainEditAvailable=false;
    view.startAiGraphPreviewRegeneration();pump(700);
    check(starts==beforeFailure&&idle()&&view.m_aiGraphRegenerateButton->text()==view.tr("Regenerate preview"),
        "invalid build input leaves no queue or busy button");
    view.m_terrainEditAvailable=true;view.syncAiGraphPreviewWidget();
    view.m_aiGraphRegenerateButton->click();
    check(wait([&](){return idle()&&view.m_aiGraphRegeneratedFresh;})&&starts==beforeFailure+1,
        "next explicit click succeeds after input failure");
    QThreadPool::globalInstance()->setMaxThreadCount(oldPoolMaximum);
    view.m_aiGraphLoadedSurfacesResolved=true;
    view.m_aiGraphLoadedSurfaceHeights={1.0f,2.0f};edit();
    check(!view.m_aiGraphLoadedSurfacesResolved&&view.m_aiGraphLoadedSurfaceHeights.isEmpty(),
        "terrain invalidation clears loaded display surface cache");
    QFile unchanged(path);unchanged.open(QIODevice::ReadOnly);
    check(unchanged.readAll()==bytes&&view.m_aiGraphLoadedBytes==originalGraph,
        "preview calculations leave MPR and loaded graph bytes unchanged");
    out<<"RESULT: failures="<<failures<<" workers="<<starts<<"\n";out.flush();
    return failures?1:0;
}
