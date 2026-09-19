#ifndef RESOURCE_BROWSER_H
#define RESOURCE_BROWSER_H

#include "resource_catalog.h"
#include "resource_model_preview.h"

#include <QFutureWatcher>
#include <QImage>
#include <QSet>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QFileSystemWatcher;
class QTemporaryDir;

struct SResourceBrowserLoadResult
{
    CResourceCatalog catalog;
    QStringList errors;
};

struct SResourceBrowserPreview
{
    quint64 generation{0};
    bool valid{false};
    QString title;
    QString details;
    QString previewType;
    QString text;
    QImage image;
    QByteArray sha256;
    QByteArray payload;
    QByteArray animationPayload;
    ei::SResourceModelScene modelScene;
    QVector<SAdbAction> actions;
    QString animationModel;
    QString error;
};

class CResourceBrowser final : public QWidget
{
    Q_OBJECT
public:
    explicit CResourceBrowser(QWidget* parent = nullptr);
    ~CResourceBrowser() override;
    void setActiveProfileSources(const QVector<QString>& sources,
                                 const QString& profileName);
    bool selectResourceReference(const QString& reference);
    bool uiContractSatisfied() const;
    void beginInternalPublication(const QString& archivePath);
    void finishInternalPublication(const QString& archivePath,
                                   bool committed, bool refreshVisualResources = true);

signals:
    void databaseEditorRequested(const QString& archivePath);
    void activeResourcesPublished(const QString& archivePath);

private:
    static constexpr int ArchivePathRole = Qt::UserRole + 81;
    void beginLoad(const QVector<QString>& archives, bool activeProfile,
                   const QString& label);
    void finishLoad();
    void rebuildList();
    bool applyResourceReferenceSelection(const QString& reference);
    void beginPreview(QTreeWidgetItem* item);
    void finishPreview();
    void selectAnimation(int index);
    void advanceAnimation();
    void updateModelParts();
    void exportSelected();
    void copySelectedName();
    void openSelectedLocation();
    void openExternalArchive();
    void indexedArchiveChanged(const QString& archivePath);
    void replaceSelected();
    void addMember();
    void renameSelected();
    void deleteSelected();
    bool publishTransaction(CResArchiveTransaction& transaction,
                            const SResourceCatalogEntry& context,
                            const QString& operationSummary);
    const SResourceCatalogEntry* selectedEntry() const;
    QString selectedArchivePath() const;

    QComboBox* m_mode{nullptr};
    QLineEdit* m_search{nullptr};
    QComboBox* m_typeFilter{nullptr};
    QComboBox* m_statusFilter{nullptr};
    QPushButton* m_openArchive{nullptr};
    QTreeWidget* m_archives{nullptr};
    QTreeWidget* m_resources{nullptr};
    QLabel* m_summary{nullptr};
    QLabel* m_previewImage{nullptr};
    class CZoomableResourceImageView* m_imageView{nullptr};
    class CResourceModelCanvas* m_modelView{nullptr};
    QStackedWidget* m_previewStack{nullptr};
    QWidget* m_modelControls{nullptr};
    QLineEdit* m_partFilter{nullptr};
    QTreeWidget* m_partList{nullptr};
    QComboBox* m_animationList{nullptr};
    QPushButton* m_animationPlay{nullptr};
    QWidget* m_audioControls{nullptr};
    QPushButton* m_audioPlay{nullptr};
    class QSlider* m_audioSeek{nullptr};
    class QSlider* m_audioVolume{nullptr};
    class QMediaPlayer* m_audioPlayer{nullptr};
    QTemporaryDir* m_audioWorkspace{nullptr};
    QString m_audioPath;
    class QTimer* m_animationTimer{nullptr};
    class QSlider* m_animationSeek{nullptr};
    class QElapsedTimer* m_animationClock{nullptr};
    QLabel* m_previewDetails{nullptr};
    class QPlainTextEdit* m_previewText{nullptr};
    QPushButton* m_export{nullptr};
    QPushButton* m_copyName{nullptr};
    QPushButton* m_openLocation{nullptr};
    QPushButton* m_databaseEditor{nullptr};
    QPushButton* m_replace{nullptr};
    QPushButton* m_add{nullptr};
    QPushButton* m_rename{nullptr};
    QPushButton* m_delete{nullptr};
    QFutureWatcher<SResourceBrowserLoadResult>* m_loadWatcher{nullptr};
    QFutureWatcher<SResourceBrowserPreview>* m_previewWatcher{nullptr};
    CResourceCatalog m_catalog;
    QVector<QString> m_activeSources;
    QString m_activeProfileName;
    QString m_pendingReference;
    bool m_activeProfileMode{true};
    QVector<QString> m_loadedSources;
    QFileSystemWatcher* m_archiveWatcher{nullptr};
    QSet<QString> m_internalPublications;
    QVector<SResourceCatalogEntry> m_visibleEntries;
    quint64 m_loadGeneration{0};
    quint64 m_previewGeneration{0};
    SResourceBrowserPreview m_currentPreview;
    SAnimationClip m_currentClip;
};

#endif // RESOURCE_BROWSER_H
