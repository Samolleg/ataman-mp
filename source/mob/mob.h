#ifndef MOB_H
#define MOB_H

#include <QFileInfo>
#include <QMap>
#include <QDataStream>
#include <QVector>
#include <QVector3D>
#include <QSet>
#include <QOpenGLShader>
#include <functional>

#include "types.h"
#include "node.h"

struct SMapSaveFile;
namespace ei { class TechnicalModelCatalog; }
class CView;
class CProgressView;
class CPatrolPoint;
class CLookPoint;
class CActivationZone;
class CTrapCastPoint;
class CMobEditTransaction;
class CValidationReport;
class CMobDocument;
struct SPreparedMobLoad;
struct SPreparedMobProjection;
struct STerrainHeightInfluenceBounds;

struct SMobParameterState
{
    bool questMob{false};
    CWorldSet worldSet;
    QVector<SRange> mainRanges;
    QVector<SRange> secondaryRanges;
    QVector<QVector<uint>> diplomacy;
    QList<QString> diplomacyNames;
    QString script;
    uint activeRangeId{0};

    bool operator==(const SMobParameterState& other) const;
    bool operator!=(const SMobParameterState& other) const { return !(*this == other); }
};

///
/// \brief The CMob class provides reading, saving and editing of *.mob file.
///
class CMob
{
    friend int runTerrainMobHeightCheck();
public:
    struct SCanvasCropResult
    {
        int translatedObjects{0};
        int removedObjects{0};
        int removedTaperObjects{0};
        bool removedEmbeddedAiGraph{false};
    };
    using SaveProgress = std::function<void(const QString&, int, int)>;
    CMob();
    ~CMob();
    // Workspace provenance only; never serialized into the game document.
    bool loadedFromAddMob() const { return m_loadedFromAddMob; }
    void setLoadedFromAddMob(bool value) { m_loadedFromAddMob = value; }
    static bool readTemplateParameters(const QFileInfo& source,
                                       SMobParameterState* state,
                                       QString* error = nullptr);
    void attach(CView* view, CProgressView* pProgress);
    static bool validateEditableProjection(const QByteArray& data,
                                           const QFileInfo& source,
                                           QString* error = nullptr);
    int readMob(QFileInfo& path, bool createRecoverySnapshot = true,
                QStringList* nonModalWarnings = nullptr);
    bool beginPreparedLoad(const SPreparedMobLoad& source);
    bool appendPreparedObject(QByteArray record);
    void finishPreparedLoad(const SPreparedMobLoad& source, const SPreparedMobProjection& projection);
    QVector<uint> checkUniqueId(QSet<uint>& aId);
    bool prepareSaveBytes(const QFileInfo& path,QByteArray* bytes,const SaveProgress& progress = SaveProgress());
    bool saveAs(const QFileInfo& path, const SaveProgress& progress = SaveProgress());
    bool save(const SaveProgress& progress = SaveProgress());
    void restoreAssemblyInstances();
    bool prepareAssemblySave(const QByteArray& savedBytes, QMap<QString,ei::TechnicalModelCatalog>* catalogs, QString* error, const QString& destination = QString());
    void acceptAssemblySave();
    static bool appendAssemblySaveFiles(const QMap<QString,ei::TechnicalModelCatalog>& catalogs, QVector<SMapSaveFile>* files, QString* error);
    bool saveAssemblyInstances(const QString& destination, QString* error = nullptr);
    const QString& lastSaveError() const { return m_lastSaveError; }
    bool buildValidatedCandidate(QByteArray* data,
                                 CValidationReport* report = nullptr,
                                 QString* saveClass = nullptr);
    bool buildCanvasCropCandidate(float offsetX, float offsetY,
                                  float resultWidth, float resultHeight,
                                  bool removeOutside, float closedEdgeWidth,
                                  QByteArray* data, SCanvasCropResult* result,
                                  CValidationReport* report = nullptr,
                                  bool preserveEmbeddedGraph = false);
    bool persistAiGraph(const QByteArray* graph, QString* error = nullptr,
                        const SaveProgress& progress = SaveProgress());
    void serializeJson(const QFileInfo& file);
    void serializeMob(QByteArray& data, bool writeLog = true);
    void addNode(QList<CNode*>& aNode) {m_aNode.append(aNode); }
    void addNode(CNode* aNode) {m_aNode.append(aNode); }
    void createNode(CNode* pNode);
    CNode* createNode(QJsonObject data, bool loadVisuals = true);
    static bool prepareFreshDatabaseNode(CNode* node, QJsonObject* data,
                                         CValidationReport* report = nullptr);
    static bool prepareFreshLightNode(CNode* node, QJsonObject* data,
                                      CValidationReport* report = nullptr);
    static bool prepareFreshTechnicalNode(CNode* node, QJsonObject* data,
                                          CValidationReport* report = nullptr);
    void undo_createNode(uint mapId);
    CNode* restoreDeletedNode(CNode* pNode, int restoreIndex = -1);
    bool replaceNode(CNode* current, CNode* replacement, int index = -1);
    bool rebaseCopiedObjectProvenance(CNode* node, const QString& templatePath,
                                      const QByteArray& templateSubtreeSha256);
    bool rawObjectTemplatesBytes(const QList<CNode*>& nodes, QHash<CNode*,QPair<QByteArray,QByteArray>>* templates) const;
    bool rawObjectTemplateBytes(const CNode* node, QByteArray* sourceSubtree,
                                QByteArray* cleanSubtree) const;
    QList<CNode*>& nodes();
    QSet<quint32> occupiedMapIds() const;
    void deleteNode(uint mapId);
    CNode* undo_deleteNode(uint mapId, int restoreIndex = -1);
    void deleteNode(CNode* pNode);
    void discardUncommittedNode(CNode* pNode);
    void clearSelect(bool bClearLogic = true);
    void delNodes();
    CNode* nodeByMapId(uint id);
    bool isFreeId(const uint id);
    CView* view() {Q_ASSERT(m_view); return m_view;}
    QString mobName();
    const QFileInfo& filePath() {return m_filePath;}
    void setFileName(const QFileInfo& fileInfo) {m_filePath = fileInfo;}
    const CWorldSet& worldSet() {return m_worldSet;}
    void setWorldSet(const CWorldSet& ws){m_worldSet = ws;}
    const QVector<SRange>& ranges(bool bMain);
    const QList<QString>& diplomacyNames() {return m_aDiplomacyFieldName;}
    void setDiplomacyNames(const QList<QString>& arrName) {m_aDiplomacyFieldName = arrName;}
    QVector<QVector<uint>>& diplomacyField() {return m_diplomacyFoF;}
    void setDiplomacyField(const QVector<QVector<uint>>& df) {m_diplomacyFoF = df;}
    const QString& script() {return m_script;}
    void setScript(const QString& script);
    void setQuestMob(bool bQuest = true) {m_mobType = bQuest ? eEMobTypeQuest : eEMobTypeBase;}
    void ensureObjectSection() { m_hasObjectSection = true; }
    bool isQuestMob() {return m_mobType == EMobType::eEMobTypeQuest;}
    void setDirty(bool bDurty = true) {m_bDirty = bDurty;}
    bool isDirty() const {return m_bDirty;}
    void captureCleanProjection();
    bool refreshDirtyFromProjection();
    const QVector<uint>& duplicateIds() const {return m_duplicateIds;}
    const QByteArray& originalData() const {return m_originalData;}
    void adoptCommittedBytes(const QByteArray& bytes);
    void generateDiplomacyTable();
    void clearDiplomacyTable();
    void setRanges(bool bMain, const QVector<SRange>& range);
    void clearRanges(bool bMain);
    void addRange(bool bMain, const SRange range);
    const SRange& activeRange();
    uint activeRangeId() {return m_activeRangeId;}
    void setActiveRange(uint rangeId);
    SMobParameterState parameterState();
    void applyParameterState(const SMobParameterState& state);
    uint freeMapId();
    const QSet<quint32>& scriptReferencedMapIds() const;
    const QByteArray& aiGraphData() const { return m_aiGraph; }
    // In-memory graph editing. Persistence remains exclusively the normal Save pipeline.
    void setAiGraphData(const QByteArray& bytes) { m_aiGraph=bytes; }

    //functions for logic processing
    QList<CNode*>& logicNodes();
    void logicNodesUpdate();
    void reprojectForCurrentTerrain();
    int reprojectForCurrentTerrain(const STerrainHeightInfluenceBounds& bounds);
    void getPatrolHash(int& unitMapIdOut, int& pointIdOut, CPatrolPoint* pPoint);
    void getViewHash(int& unitMapIdOut, int& pointIdOut, int& viewIdOut, CLookPoint* pPoint);
    int getPatrolId(uint unitMapId, CPatrolPoint* pPoint);
    void createPatrolByHash(QString hash, int logicSlot = -1);
    void undo_createPatrolByHash(QString hash, int logicSlot = -1);
    CPatrolPoint* patrolPointById(int unitId, int patrolId, int logicSlot = -1);
    CLookPoint* viewPointById(int unitId, int patrolId, int viewId,
                            int logicSlot = -1);
    void getTrapZoneHash(int& unitMapIdOut, int& zoneIdOut, CActivationZone* pZone);
    CActivationZone* actZoneById(int trapId, int zoneId);
    void getTrapCastHash(int& unitMapIdOut, int& pointIdOut, CTrapCastPoint* pCast);
    CTrapCastPoint* trapCastById(int trapId, int pointId);
    uint trapIdByPoint(CActivationZone* pZone);
    uint trapIdByPoint(CTrapCastPoint* pPoint);
    // <== end functions of logic

private:
    bool buildValidatedProjectionCandidate(QByteArray*, CValidationReport*, QString*);
    friend int runDatabaseCreationCheck();
    friend int runGroupRotationCheck();
    friend int runModelConnectionCheck();
    friend int runAssemblyCatalogCheck();
    friend int runWindowLayoutCheck();
    friend int runDatabasePreviewCheck();
    bool buildRawObjectSequenceCandidate(const QByteArray& currentProjection,
                                         CMobEditTransaction* transaction,
                                         QByteArray* candidate,
                                         CValidationReport* report,
                                         QString* saveClass) const;
    bool rebindRawObjectProvenance();
    QString getAuxDirName();
    bool deserialize(QByteArray data, const CMobDocument* parsed = nullptr, bool deferObjects = false);
    uint projectObject(util::CMobParser& parser);
    void updateObjects();
    void writeData(QJsonObject& mob, const QFileInfo& file, const QString key, const QString value);
    void writeData(QJsonObject& mob, const QFileInfo& file, const QString key, QByteArray& value);
    bool isFreeMapId(uint id);
    void collectTreeView();

private:
    //todo: global text data, script
    CView* m_view;
    CProgressView* m_pProgress;
    QFileInfo m_filePath;
    bool m_loadedFromAddMob{false};
    uint m_scriptKey;
    QString m_script;
    mutable QString m_idReservationScript;
    mutable QSet<quint32> m_scriptReferencedIds;
    QString m_textOld;
    QVector<SRange> m_aMainRange;
    QVector<SRange> m_aSecRange;
    QVector<QVector<uint>> m_diplomacyFoF;
    QList<QString> m_aDiplomacyFieldName;
    CWorldSet m_worldSet;
    QByteArray m_vss_section;
    bool m_hasVssSection;
    bool m_hasObjectSection;
    QByteArray m_directory;
    QByteArray m_directoryElements;
    QByteArray m_aiGraph;
    QByteArray m_originalData;
    QSet<uint> m_auxiliaryIds;
    QByteArray m_cleanProjection;
    QByteArray m_cleanAssemblyProjection;
    bool m_assemblyMetadataPending{false};
    QByteArray assemblyProjection() const;
    bool writeAssemblyInstances(const QString& destination, QString* error);
    QVector<uint> m_duplicateIds;
    QString m_lastSaveError;
    QSet<QString> m_navigationSavePending;

    //todo: include ALL nodes into these lists, for node assemly use children\parents
    QList<CNode*> m_aNode;
    QList<CNode*> m_aDeletedNode;
    QList<CNode*> m_aLogicNode;
    EMobType m_mobType;
    bool m_bDirty;
    uint m_activeRangeId;
};

#endif // MOB_H
