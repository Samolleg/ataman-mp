#ifndef MAP_TABLE_EDITOR_DIALOG_H
#define MAP_TABLE_EDITOR_DIALOG_H

#include <QDialog>

#include "map_table_document.h"
#include "res_archive_document.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

class CMapTableEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CMapTableEditorDialog(QWidget* parent = nullptr);
    bool openArchive(const QString& path);

private slots:
    void chooseArchive();
    void selectZone(int row);
    void applySelectedZone();
    void beginNewZone();
    void beginDuplicateZone();
    void registerNewZone();
    void applyRawSource();
    void revertRawSource();
    void addExit();
    void removeExit();
    void exportText();
    void importText();
    void saveArchiveAs();

private:
    void setArchiveUiEnabled(bool enabled);
    void loadZone(const SMapTableZone& zone);
    bool collectZone(SMapTableZone* zone, QString* error) const;
    bool applyCurrentZone();
    void refreshZoneList(int selectedRow);
    QString cellText(int row, int column) const;
    void setStatus(const QString& text, bool error = false);
    void setRawSourceText(const QString& text, bool editable);
    void updateRawEditState();
    bool rawSourceHasPendingChanges() const;

    QString m_archivePath;
    QString m_mapMemberName;
    CResArchiveDocument m_archive;
    CMapTableDocument m_table;
    int m_selectedZone{-1};
    bool m_newZoneMode{false};

    QLineEdit* m_archiveEdit{nullptr};
    QListWidget* m_zoneList{nullptr};
    QLineEdit* m_zoneIdEdit{nullptr};
    QLineEdit* m_allodIdEdit{nullptr};
    QComboBox* m_zoneTypeCombo{nullptr};
    QLineEdit* m_mprEdit{nullptr};
    QLineEdit* m_mobEdit{nullptr};
    QCheckBox* m_mapsEnabled{nullptr};
    QSpinBox* m_sectorWidth{nullptr};
    QSpinBox* m_sectorHeight{nullptr};
    QLineEdit* m_minimapEdit{nullptr};
    QLineEdit* m_objectivesEdit{nullptr};
    QTableWidget* m_exitTable{nullptr};
    QPlainTextEdit* m_rawReview{nullptr};
    QString m_loadedRawText;
    QPushButton* m_importButton{nullptr};
    QPushButton* m_exportButton{nullptr};
    QPushButton* m_applyButton{nullptr};
    QPushButton* m_duplicateButton{nullptr};
    QPushButton* m_registerButton{nullptr};
    QPushButton* m_applyRawButton{nullptr};
    QPushButton* m_revertRawButton{nullptr};
    QPushButton* m_saveButton{nullptr};
    QLineEdit* m_statusEdit{nullptr};
    QTabWidget* m_tabs{nullptr};
};

#endif // MAP_TABLE_EDITOR_DIALOG_H
