#pragma once
#include <QWidget>
#include <QProcess>
#include <QFutureWatcher>
#include <QVector>
#include "nav64_document.h"
#include "backup_manager.h"
class QComboBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;

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
    bool busy() const;
    const SNav64Document& document() const {return m_document;}
    // Sole process invocation boundary; UI and isolated checks use this function.
    bool startBake(bool all,const QString& outputRootOverride=QString());
    void cancelBake();
    void setBakerPath(const QString& path);
    bool lastBakeSucceeded() const {return m_lastSuccess;}
    int lastBakeExitCode() const {return m_lastExit;}
    QString bakeLog() const;
signals:
    void geometryChanged();
    void oldGraphRequested();
    void bakeFinished(bool success);
private:
    void drain(bool errors);
    void line(const QByteArray& bytes);
    void freshness();
    void controls();
    QComboBox* m_mode;QComboBox* m_profile;QComboBox* m_fill;
    QLabel* m_status;QLabel* m_freshness;QLabel* m_heightOutliers;
    QLineEdit* m_baker;QPlainTextEdit* m_log;
    QPushButton* m_this;QPushButton* m_all;QPushButton* m_cancel;
    QProcess* m_process;QFutureWatcher<SNav64Document>* m_loading;
    SNav64Document m_document;
    QString m_mpr,m_mob,m_gameRoot,m_mapsDirectory,m_directory,m_loadingPath,m_outputRoot;
    QString m_requestMpr,m_requestMob;
    QByteArray m_stdout,m_stderr;
    QVector<SBackupResult> m_backups;
    bool m_unsaved=false,m_done=false,m_errors=false,m_cancelled=false,m_lastSuccess=false;
    int m_lastExit=-1;
};
