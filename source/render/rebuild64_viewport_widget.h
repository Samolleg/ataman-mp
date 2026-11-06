#pragma once
#include <QWidget>
#include <QByteArray>
#include <QVector>
#include <QElapsedTimer>
#include <memory>
#include "rebuild64_abi.h"
class CView;
class QMenu;
class QAction;
class QStackedLayout;
class QTimer;
class CRebuild64NativeSurface;

// Owns only a display backend. CView remains the document/tool/camera owner.
class CRebuild64Viewport : public QWidget {
    Q_OBJECT
public:
    explicit CRebuild64Viewport(CView* gl, QMenu* menu, QWidget* parent=nullptr);
    ~CRebuild64Viewport() override;
    bool rebuildSelected() const { return m_selected; }
    bool backendLoaded() const;
    void selectRebuild(bool selected, bool persist=true);
    QString lastFailure() const { return m_failure; }
    EirStatistics statistics() const { return m_statistics; }
    void capture(const QString& path);
signals:
    void backendFailure(const QString& message);
    void frameCommitted(quint64 generation);
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    struct Worker;
    void tick();
    void fallback(const QString& message);
    void cameraInput(QEvent* event);
    EirCamera cameraSnapshot();
    CView* m_gl;
    CRebuild64NativeSurface* m_native;
    QStackedLayout* m_stack;
    QTimer* m_timer;
    QAction *m_glAction,*m_rebuildAction;
    std::unique_ptr<Worker> m_worker;
    QByteArray m_documentHash;
    QByteArray m_terrainState;
    bool m_documentRefreshPending{true};
    quint64 m_generation{0};
    bool m_selected{false};
    QPoint m_lastMouse;
    QString m_failure;
    EirStatistics m_statistics{};
    QElapsedTimer m_sceneSnapshotTimer;
};
