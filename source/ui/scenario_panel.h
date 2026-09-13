#ifndef SCENARIO_PANEL_H
#define SCENARIO_PANEL_H
#include <QWidget>
#include <QPointer>
#include "scenario_analysis.h"
#include "mob_family_validation.h"
class CView;
class CMob;
class QTimer;
class QTreeWidget;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QCheckBox;
class QLineEdit;
class QComboBox;

class CScenarioPanel final : public QWidget
{
    Q_OBJECT
public:
    explicit CScenarioPanel(CView* view, QWidget* parent = nullptr);
    void setFamilyReport(const CMobFamilyValidationReport& report);
    void setLayerEnabled(bool enabled);
    void refresh();
    const scenario::Result& analysis() const { return m_result; }
    const scenario::Request& snapshot() const { return m_request; }
    void showSourceLocation(CMob* mob, int offset);
signals:
    void showRequested();
private:
    void scheduleRefresh();
    void populate();
    void showReference();
    void updateOverlay();
    void navigateCode();
    void navigateTarget();
    QString selectedKey() const;
    const scenario::Source* source(const QString& key) const;
    QStringList participantKeys(const scenario::Reference& ref) const;
    int currentReference() const;
    QPointer<CView> m_view;
    QTimer* m_timer;
    QTreeWidget* m_references;
    QPlainTextEdit* m_code;
    QLabel* m_summary;
    QLabel* m_details;
    QCheckBox* m_selectedOnly;
    QCheckBox* m_overlay;
    QLineEdit* m_filter;
    QPushButton* m_toCode;
    QPushButton* m_toTarget;
    QComboBox* m_targets;
    scenario::Request m_request;
    scenario::Result m_result;
    CMobFamilyValidationReport m_family;
    bool m_enabled = false;
    bool m_refreshing = false;
    int m_navigationReference = -1;
};
#endif
