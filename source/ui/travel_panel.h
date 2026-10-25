#pragma once
#include <QWidget>
#include "map_table_document.h"
#include <QMap>
class QComboBox;
class QListWidget;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QToolButton;
class CWorldCoordinateEdit;

class CTravelPanel : public QWidget
{
    Q_OBJECT
public:
    explicit CTravelPanel(QWidget* parent=nullptr);
    void populate(const QVector<SMapTableZone>& zones,int zoneIndex,int exitIndex,bool dirty);
    void setStatus(const QString& text);
    bool flush(int redrawArea=-1);
    int selectedExit() const;
    bool showDeploy() const;
    bool showRemove() const;
    void setDrawingState(bool active, bool remove, bool newExit);
    void revealArea(bool remove);
    void setMapNames(const QMap<QString,QString>& names) {m_names=names;}
signals:
    void zoneSelected(int index);
    void exitSelected(int index);
    void exitEdited(int index,const SMapTableExit& value);
    void drawRequested(bool remove,bool newExit);
    void deleteRequested();
    void reverseRequested();
    void routeRequested();
    void globalPositionRequested();
    void registerRequested();
    void editMapTextRequested();
    void visibilityChanged();
    void focusRequested(int index, bool remove);
    void incomingRequested(int zoneIndex, int exitIndex);
    void routeEndEdited(int zoneIndex,int exitIndex,const QString& target,int entry);
    void destinationProblemRequested(int zoneIndex,int exitIndex);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void fillExit();
    void fillDestinationEntries(int selected);
    void fillFriendlyDestination();
    void fillFarEntries(int selected);
    QString destinationChain(const SMapTableExit& entry) const;
    QMap<QString,QString> m_names;
    QLabel* m_linkStatus;
    QComboBox* m_friendly;
    QWidget* m_routeEditor;
    QToolButton* m_routeToggle;
    QComboBox* m_farZone;
    QComboBox* m_farEntry;
    QLabel* m_routeWarning;
    QPushButton* m_problem;
    QPushButton* m_reverse;
    int m_route{-1},m_farEnd{-1};
    bool m_filling{false};
    QVector<SMapTableZone> m_zones;
    int m_zone{-1};
    int m_exit{-1};
    QComboBox* m_zoneCombo;
    QListWidget* m_exits;
    QComboBox* m_destination;
    QComboBox* m_destinationExit;
    QCheckBox* m_visible[2];
    QCheckBox* m_present[2];
    CWorldCoordinateEdit* m_rect[2][4];
    QLabel* m_status;
    QLabel* m_incoming;
    QPushButton* m_repairOrder;
    QComboBox* m_source;
    QPushButton* m_linkIncoming;
    QWidget* m_properties;
    QPushButton* m_add[2];
    QTabWidget* m_tabs;
    bool m_dirty{false};
};
