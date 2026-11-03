#ifndef MAP_TABLE_EDITOR_DIALOG_H
#define MAP_TABLE_EDITOR_DIALOG_H

#include <QDialog>
#include <QFutureWatcher>
#include <memory>

#include "map_table_document.h"
#include "map_table_change_report.h"
#include "res_save_coordinator.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QSplitter;
class QLabel;
class QProgressDialog;
class CFileWriteActivity;

class CMapTableEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CMapTableEditorDialog(QWidget* parent = nullptr);
    ~CMapTableEditorDialog() override;
    bool openArchive(const QString& path);
    bool saveArchiveTo(const QString& destination);
    bool isSaving() const { return m_saveWatcher != nullptr; }
    bool selectZoneById(const QString& zoneId);
    bool stageMinimapTextureForZone(const QString& zoneId,
                                    const QString& textureName);
    bool stageObjectivesTextureForZone(const QString& zoneId,const QString& textureName);
    const QVector<SMapTableSaveReport>& saveReports() const { return m_saveReports; }

signals:
    void archiveSaveFinished(bool success);

protected:
    void done(int result) override;

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
    void viewExistingMinimap();
    void applyZoneFilter(const QString& text);

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
    QByteArray m_originalMapPayload;
    QFutureWatcher<SResMemberSaveResult>* m_saveWatcher{nullptr};
    std::shared_ptr<CFileWriteActivity> m_saveActivity;
    CMapTableDocument m_table;
    SMapTableMaps m_loadedMaps;
    QVector<SMapTableSaveReport> m_saveReports;
    int m_selectedZone{-1};
    bool m_newZoneMode{false};
    bool m_persistUiState{true};

    QLineEdit* m_archiveEdit{nullptr};
    QLineEdit* m_zoneFilter{nullptr};
    QLabel* m_zoneFilterStatus{nullptr};
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
    QPushButton* m_viewMinimapButton{nullptr};
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
    QSplitter* m_splitter{nullptr};
};

#endif // MAP_TABLE_EDITOR_DIALOG_H
