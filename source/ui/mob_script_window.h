#ifndef MOB_SCRIPT_WINDOW_H
#define MOB_SCRIPT_WINDOW_H

#include <QWidget>

class CMob;
class CView;
class CScriptEditor;
class CMalachiteBridge;
class QCloseEvent;

class CMobScriptWindow final : public QWidget
{
    Q_OBJECT
public:
    CMobScriptWindow(CMob* mob, CView* view, QWidget* parent = nullptr);
    CMob* mob() const { return m_mob; }
    bool requestClose();
    QString analysisText() const;
    bool hasUnappliedText() const { return hasLocalChanges(); }
    bool navigateToSource(const QString& analyzedText, int offset);

signals:
    void analysisBufferChanged();
    void scenarioLocationRequested(CMob* mob, int offset);

public slots:
    void onMobScriptChanged(CMob* mob, const QString& displayScript);
    void onExternalMobScriptImported(CMob* mob, const QString& displayScript);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void applyScript();
    void openExternalEditor();

private:
    static QString displayScript(CMob* mob);
    static QString canonicalScriptText(QString text);
    bool applyCurrentText();
    bool hasLocalChanges() const;
    void startMalachite();

    CMob* m_mob;
    CView* m_view;
    CScriptEditor* m_editor;
    QString m_committedDisplayScript;
    bool m_closeAccepted;
    CMalachiteBridge* m_malachite = nullptr;
};

#endif
