#pragma once
#include <QWidget>
#include "map_table_document.h"
class QComboBox;
class QListWidget;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class CWorldCoordinateEdit;

class CTravelPanel : public QWidget
{
    Q_OBJECT
public:
    explicit CTravelPanel(QWidget* parent=nullptr);
    void populate(const QVector<SMapTableZone>& zones,int zoneIndex,int exitIndex,bool dirty);
    void setStatus(const QString& text);
    bool flush();
    int selectedExit() const;
    bool showDeploy() const;
    bool showRemove() const;
    void setDrawingState(bool active, bool remove, bool newExit);
    void revealArea(bool remove);
signals:
    void zoneSelected(int index);
    void exitSelected(int index);
    void exitEdited(int index,const SMapTableExit& value);
    void drawRequested(bool remove,bool newExit);
    void deleteRequested();
    void reverseRequested();
    void registerRequested();
    void visibilityChanged();
    void focusRequested(int index, bool remove);
    void incomingRequested(int zoneIndex, int exitIndex);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void fillExit();
    bool m_filling{false};
    QVector<SMapTableZone> m_zones;
    int m_zone{-1};
    int m_exit{-1};
    QComboBox* m_zoneCombo;
    QListWidget* m_exits;
    QComboBox* m_destination;
    QSpinBox* m_destinationExit;
    QCheckBox* m_visible[2];
    QCheckBox* m_present[2];
    CWorldCoordinateEdit* m_rect[2][4];
    QLabel* m_status;
    QLabel* m_incoming;
    QLabel* m_directionNote;
    QComboBox* m_source;
    QPushButton* m_linkIncoming;
    QWidget* m_properties;
    QPushButton* m_add[2];
    QTabWidget* m_tabs;
    bool m_dirty{false};
};
