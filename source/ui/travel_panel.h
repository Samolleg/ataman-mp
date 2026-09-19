#pragma once
#include <QWidget>
#include "map_table_document.h"
class QComboBox;
class QListWidget;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
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
signals:
    void zoneSelected(int index);
    void exitSelected(int index);
    void exitEdited(int index,const SMapTableExit& value);
    void drawRequested(bool remove,bool newExit);
    void deleteRequested();
    void reverseRequested();
    void registerRequested();
    void visibilityChanged();
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
    QWidget* m_properties;
};
