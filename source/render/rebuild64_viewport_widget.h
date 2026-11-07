#include <QMatrix4x4>
#include <QVector3D>
#pragma once
#include <QWidget>
#include <QFileInfo>
#include <QByteArray>
#include <QStringList>
#include <QVector>
#include <QElapsedTimer>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <memory>
#include "rebuild64_abi.h"
class CView;
class QMenu;
class QAction;
class QStackedLayout;
class QTimer;
class QLabel;
class CRebuild64NativeSurface;

// Owns only a display backend. CView remains the document/tool/camera owner.
class CRebuild64Viewport : public QWidget {
    Q_OBJECT
    friend class CRebuild64LaunchCheck;
public:
    explicit CRebuild64Viewport(CView* gl, QMenu* menu, QWidget* parent=nullptr);
    ~CRebuild64Viewport() override;
    bool rebuildSelected() const { return m_selected; }
    bool openingDocument() const { return m_earlyDocument; }
    bool pendingEditorInput() const { return !m_waitingInput.isEmpty(); }
    bool backendLoaded() const;
    bool backendPrepared() const;
    bool backendResourcesRetired() const;
    void stopForApplicationShutdown();
    void finishApplicationShutdown();
    bool applicationShutdownFinished() const;
    void prepareBackend();
    void selectRebuild(bool selected, bool persist=true);
    void beginDocumentTransition();
    void openDocumentEarly(const QFileInfo& file,const QStringList& mobs,const QStringList& search);
    void requireOpenGL(const QString& action);
    QString lastFailure() const { return m_failure; }
    EirStatistics statistics() const { return m_statistics; }
    bool frameIsCurrent() const { return (m_selected||m_outputOnly) && !m_documentRefreshPending
        && m_generation && m_statistics.ready_generation==m_generation; }
    void capture(const QString& path, bool worldOnly=false);
    void cancelCapture(const QString& path);
    void cancelOutputs(const QString& reason);
    QImage emptyViewportImage();
    // 288: asynchronous owned-pixel output. A document transition cancels delivery.
    quint64 requestOutput(const QSize& size,int mode,const EirCamera& camera,
                          const QString& model={},const QString& texture={},const QVector3D& shape={},const QVector<float>& preview={},const QVector<quint64>& objectKeys={},const QString& secondary={},const QStringList& parts={},
                          const QByteArray& modelBytes={},const QByteArray& boneBytes={},const QByteArray& textureBytes={});
    EirCamera outputCamera() { return cameraSnapshot(); }

signals:
    void outputFinished(quint64 serial,const QImage& image,const QString& error,
                        quint64 generation,const QString& map,int neighbours);
    void backendFailure(const QString& message);
    void frameCommitted(quint64 generation);
    void rendererSelectionChanged(bool native);
    void documentTransitionStarted(quint64 generation);
    void captureFinished(const QString& path, const QString& error, quint64 generation);
protected:
    bool eventFilter(QObject*,QEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    struct Worker;
    void tick();
    void updateLiveCamera();
    void fallback(const QString& message);
    void editorInput(QEvent* event);
    void serviceInput();
    bool editorQueryReady() const;
    void syncEditorViewport();
    bool beginResourceWrite(QString* error);
    void endResourceWrite();
    int m_resourceWriteDepth{0};
    bool m_stopping{false},m_restartWhenFinished{false};
    void showPreparation(const QString& phase);
    void updatePreparationText();
    bool m_pointerDown{false},m_editorViewportDirty{false};
    bool m_legacyNavigationPending{false};
    QString m_earlyMap;
    bool m_earlyDocument{false};
    struct WaitingInput {std::shared_ptr<QEvent> event;QMatrix4x4 view,projection;QSize extent;};
    QVector<WaitingInput> m_waitingInput;
    const WaitingInput* m_replayInput{nullptr};
    void queueEditorInput(std::shared_ptr<QEvent> event);
    void replayEditorInput();
    QImage m_serviceImage;
    double m_serviceMilliseconds{0};
    void movePanels(bool native);
    EirCamera cameraSnapshot();
    CView* m_gl;
    CRebuild64NativeSurface* m_native;
    QWidget* m_preparation;
    QLabel* m_preparationText;
    QString m_preparationPhase;
    QElapsedTimer m_preparationClock;
    QStackedLayout* m_stack;
    QTimer* m_timer;
    QAction *m_glAction,*m_rebuildAction;
    std::unique_ptr<Worker> m_worker;
    QByteArray m_backendResourceKey;
    int m_backendResourcePreparations{0};
    QByteArray m_documentHash;
    QString m_documentMapPath;
    bool m_transformPreviewMessage{false};
    QByteArray m_documentVisibility;
    QHash<quint64,EirObjectPose> m_documentPoses;
    QSet<quint64> m_poseOverrides;
    QVector<quintptr> m_documentMobOrder;
    QByteArray m_terrainState;
    bool m_documentRefreshPending{true};
    quint64 m_outputSerial{0};
    quint64 m_generation{0};
    bool m_selected{false},m_outputOnly{false};
    QPoint m_lastMouse;
    QString m_failure;
    QString m_activeCapturePath;
    QString m_guardDirectory;
    QStringList m_interruptedStarts;
    QList<QPointer<QWidget>> m_panels;
    EirStatistics m_statistics{};
    QElapsedTimer m_sceneSnapshotTimer;
    QElapsedTimer m_previewClock;
};
