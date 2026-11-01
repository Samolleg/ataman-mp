#ifndef TOPDOWN_EXPORT_H
#define TOPDOWN_EXPORT_H
#include "ai_agent_read.h"
#include "topdown_capture.h"
#include "technical_model_catalog.h"
#include <QFutureWatcher>
#include <QPointer>
#include <functional>
class CView;
class MainWindow;
class CNode;
class CPart;

// Operation owner lives on the GUI thread. Only detached images/JSON/path values
// enter the encoder worker; no QWidget, document, GL handle or live cache does.
class CTopdownExport : public QObject {
public:
    CTopdownExport(MainWindow*, const QJsonObject&);
    ~CTopdownExport() override;
    void begin();
    void cancel(const QString& code="CANCELLED");
    bool done() const { return !m_terminal.isEmpty(); }
    QString phase() const { return done()?m_terminal:m_phase; }
    QJsonObject result() const { return m_result; }
    QString incompletePath() const { return m_temporary; }
    std::function<void()> finished;
private:
    void step();
    bool initialize();
    bool resolveObject();
    bool unchanged();
    QByteArray sceneIdentity() const;
    void fail(QString code, QString message, QJsonObject details={});
    void releaseScene();
    QPointer<MainWindow> m_window;
    QPointer<CView> m_view;
    QJsonObject m_args,m_manifest,m_result;
    QStringList m_forbidden;
    std::shared_ptr<void> m_outputLease;
    QString m_phase{"resolving_resources"},m_terminal,m_revision,m_output,m_temporary,m_catalogPath;
    QElapsedTimer m_clock;
    ai::Cancellation m_cancel{std::make_shared<std::atomic_bool>(false)};
    QByteArray m_sceneIdentity;
    quint64 m_validationCount{0},m_validationNanoseconds{0},m_providerChecks{0};
    QVector<QPointer<CNode>> m_nodes;
    QVector<QString> m_mobPaths;
    int m_next{0},m_pass{0},m_structures{0},m_vegetation{0},m_excluded{0};
    QJsonArray m_objects,m_resources;
    QSet<QString> m_resourceKeys;
    ei::TechnicalModelCatalog m_catalog;
    QVector<SRenderItem> m_structureItems,m_vegetationItems;
    QVector<QSharedPointer<CPart>> m_parts;
    QVector<QSharedPointer<QOpenGLTexture>> m_textures;
    QMap<QString,QImage> m_images;
    QSize m_size;
    QFutureWatcher<QJsonObject>* m_writer{nullptr};
};

namespace ai {
QJsonObject encodeImageBounded(QImage image,QSize maximum,const Cancellation& cancel);
bool registerTopdownExport(const QString& manifestPath,const QString& exportId,const QByteArray& sha256,QString* error);
QJsonObject readExportImage(const QJsonObject& args);
QString topdownWorkspaceRoot();
// Returns a canonical, absent destination under workspace. Every existing
// ancestor is checked for symlinks and Windows reparse points (junctions too).
QJsonObject validateTopdownOutput(const QString& output,const QString& workspace,const QStringList& forbidden);
std::shared_ptr<void> lockTopdownParent(const QString& output,const QString& workspace,QString* error);
QJsonObject writeTopdownImages(QMap<QString,QImage> images,const QString& temporary,
                              QJsonObject manifest,const Cancellation& cancel);
}
#endif
