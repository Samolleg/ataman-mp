#ifndef AI_AGENT_EDIT_H
#define AI_AGENT_EDIT_H
#include "ai_agent_read.h"
#include "world_terrain_sculpt.h"
#include "terrain_atlas_profile.h"
#include "terrain_atlas_archive_binding.h"
#include "terrain_virtual_atlas.h"
#include "world_map_layout_document.h"
#include "process_edit_lock.h"
#include "ai_graph_preview_builder.h"
#include "mob_family_validation.h"
class MainWindow;
class QUndoCommand;
class CAiAgentCommand;
namespace ai {
// Detached values only. Workers never own a QWidget, a renderer or live document.
struct PaintSources {
    QVector<STerrainGlobalMaterialFamily> families;
    QMultiMap<QByteArray,STerrainGlobalTransitionSource> transitions;
    QStringList archives,mapRoots;
    QString reference, nameSpace;
    QByteArray referenceHash;
    QMap<QString,STerrainAtlasArchiveBindingResult> bindings;
    QSet<QString> missing;
    QMap<QString,QImage> tiles,uniforms;
};
// Implemented beside the manual131 source-selection helpers; same authored
// source order, resolver, shared-corner commands and edge joining.
bool realizeAgentPaintSignature(PaintSources& sources, CTerrainWorkingAtlas& atlas,
    CTerrainEditModel& terrain, const STerrainTransitionSignature& signature,
    int x,int y,quint32 seed,qint32 material,QMap<int,quint64>* usage,
    const QSet<int>& protectedSlots,CValidationReport* report);
struct EditMap {
    QSharedPointer<CProcessEditLock> editLease;
    QString id,path;
    QPointF origin;
    CMprDocument baseline;
    SWorldTerrainPaintState state;
    STerrainAtlasProfile profile;
    QString archive;
    QVector<QByteArray> pages;
};
struct EditPlan {
    QString method, revision, description, error;
    QJsonObject args;
    QVector<EditMap> maps;
    PaintSources sources;
    SWorldTerrainStroke stroke;
    QByteArray newMap;
    QString templatePath,destination;
    QStringList mapRoots;
    QMap<QString,QString> mapAliases;
    CWorldMapLayoutDocument worldDocument;
    QString profileId, activePath, mobPath;
    QByteArray mobBytes;
    QVector<SWorldMapLayoutChange> layoutChanges;
    QMap<QString,QByteArray> fileHashes;
    QJsonObject summary;
    // 272: value snapshots only; never pass live objects to preparation workers.
    SMobFamilyValidationRequest family;
    QJsonArray objects, changes;
    SAiGraphPreviewBuildInput graphInput;
    SAiGraphPreviewBuildResult graphResult;
    bool graphStale{false};
    QJsonArray captureFindings;
    QSet<quint32> reservedObjectIds;
};
EditPlan prepareEdit(EditPlan plan,const Cancellation& cancel);
}
class CAiEditAdapter {
    friend class CAiAgentCommand;
public:
    static QString revision(MainWindow* window);
    static bool busy(MainWindow* window);
    static ai::EditPlan capture(MainWindow* window,const QString& method,const QJsonObject& args);
    static QJsonObject apply(MainWindow* window,const ai::EditPlan& plan);
    static QJsonObject undo(MainWindow* window);
    static QJsonObject save(MainWindow* window);
    static bool hydrate(ai::EditPlan* plan,const ai::Cancellation& cancel);
    static bool lifecycle(const QString& method);
    static ai::EditPlan captureLifecycle(MainWindow*,const QString&,const QJsonObject&);
    static ai::EditPlan prepareLifecycle(ai::EditPlan,const ai::Cancellation&);
    static QJsonObject applyLifecycle(MainWindow*,const ai::EditPlan&);
    static bool dirty(MainWindow*);
    static bool lifecycleCommand(const QUndoCommand*);
    static bool lifecycleWrites(const QUndoCommand*);
    static bool objectTool(const QString&);
    static bool objectCommand(const QUndoCommand*);
    static bool navigationStale(MainWindow*);
    static QSharedPointer<const ei::SProfileDatabaseSnapshot> objectDatabase(MainWindow*);
    static ai::EditPlan captureObjects(MainWindow*,const QString&,const QJsonObject&);
    static ai::EditPlan prepareObjects(ai::EditPlan,const ai::Cancellation&);
    static QJsonObject applyObjects(MainWindow*,const ai::EditPlan&);
private:
    static bool replay(MainWindow* window,const SWorldTerrainStroke& stroke,bool forward,QString* error);
};
#endif
