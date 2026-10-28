#pragma once
#include <QWidget>
#include <QProcess>
#include <QFutureWatcher>
#include <QVector>
#include "nav64_document.h"
#include "nav64_comparison.h"
#include "backup_manager.h"
class QComboBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QProgressBar;
class QToolButton;
class QListWidget;
class QTimer;

class CNavmeshPanel : public QWidget {
    Q_OBJECT
public:
    explicit CNavmeshPanel(QWidget* parent=nullptr);
    ~CNavmeshPanel();
    void setScene(const QString& mpr,const QString& mob,const QString& gameRoot,
                  const QString& effectiveMapsDirectory=QString());
    void setUnsavedChanges(bool dirty);
    void reload();
    bool layerVisible() const;
    bool fillVisible() const;
    bool narrowVisible() const;
    bool islandsVisible() const;
    void setLayerVisible(bool visible);
    bool busy() const;
    const SNav64Document& document() const {return m_document;}
    // Sole process invocation boundary; UI and isolated checks use this function.
    bool startBake(bool all,const QString& outputRootOverride=QString());
    // Reads the successfully saved files, even if newer edits are unsaved.
    // Multiple saves coalesce; a save during a bake schedules one follow-up.
    void requestBakeOnSave(const QString& outputRootOverride=QString());
    void acknowledgeSave(const QString& path);
    void cancelBake();
    void setBakerPath(const QString& path);
    bool lastBakeSucceeded() const {return m_lastSuccess;}
    int lastBakeExitCode() const {return m_lastExit;}
    QString bakeLog() const;
    bool lossesVisible() const;
    bool legendVisible() const;
    QString legendHtml() const;
    int comparisonLayer() const;
    void setComparisonInput(const SAiGraphPreviewBuildInput& input, const QString& error=QString());
    void invalidateComparison();
    void refreshComparison() { requestComparison(); }
    const SNav64Comparison& comparison() const {return m_comparison;}
    int selectedRegion() const {return m_selectedRegion;}
    void selectRegion(int index);
    bool exportLosses(const QString& path,QString* error=nullptr) const;
signals:
    void geometryChanged();
    void contentChanged();
    void bakeFinished(bool success);
    void comparisonRequested();
    void regionSelected(const QVector3D& point);
private:
    struct SBakeScene {
        QString mpr,mob,gameRoot,mapsDirectory,outputRoot;
        qint64 savedAt=0;
    };
    SBakeScene currentBakeScene(const QString& outputRoot=QString()) const;
    QString saveStateKey(const SBakeScene& scene) const;
    bool startBakeScene(bool all,const SBakeScene& scene,bool automatic);
    void startPendingBake();
    void acceptSuccessfulBake();
    QVector<SBakeScene> m_pendingBakes;
    SBakeScene m_runningScene;
    QTimer* m_autoBakeTimer;
    bool m_shuttingDown=false;
    void drain(bool errors);
    void line(const QByteArray& bytes);
    void freshness();
    void controls();
    QString bakerPath() const;
    void inspectBaker();
    void algorithmStatus();
    void bodiesStatus();
    void requestComparison();
    void startComparison();
    QComboBox* m_profile;QComboBox* m_fill;
    QCheckBox* m_narrow;QCheckBox* m_islands;
    QCheckBox* m_legend;
    QComboBox* m_compareLayer;
    QLabel* m_compareStatus;QLabel* m_algorithmStatus;
    QListWidget* m_regions;
    QPushButton* m_useInstalled;
    QFutureWatcher<SNav64Comparison>* m_comparing;
    SAiGraphPreviewBuildInput m_compareInput;
    SNav64Comparison m_comparison;
    std::shared_ptr<std::atomic_bool> m_compareCancel;
    quint64 m_compareGeneration=0,m_runningGeneration=0;
    int m_selectedRegion=-1;
    SNav64BakerIdentity m_bakerIdentity;
    QString m_inspectedBaker,m_lastMismatch;
    QString m_runningBakerSha,m_lastBodiesStatus;
    QString m_reportRoot,m_failureDetail;
    QLabel* m_bodiesStatus;
    QPushButton* m_exportLosses;
    qint64 m_bakerSize=-1,m_bakerMtime=-1;
    QLabel* m_status;QLabel* m_freshness;QLabel* m_heightOutliers;
    QPushButton* m_browse;QPlainTextEdit* m_log;
    QProgressBar* m_progress;
    QToolButton* m_logToggle;
    QPushButton* m_this;QPushButton* m_all;QPushButton* m_cancel;
    QProcess* m_process;QFutureWatcher<SNav64Document>* m_loading;
    SNav64Document m_document;
    QString m_mpr,m_mob,m_gameRoot,m_mapsDirectory,m_directory,m_loadingPath,m_outputRoot;
    QString m_requestMpr,m_requestMob;
    QString m_bakerPath;
    QByteArray m_stdout,m_stderr;
    QVector<SBackupResult> m_backups;
    bool m_unsaved=false,m_done=false,m_errors=false,m_cancelled=false,m_lastSuccess=false;
    bool m_layerVisible=false;
    int m_completedProfiles=0;
    int m_lastExit=-1;
};
