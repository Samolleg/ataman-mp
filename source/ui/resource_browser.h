#ifndef RESOURCE_BROWSER_H
#define RESOURCE_BROWSER_H

#include "resource_catalog.h"

#include <QFutureWatcher>
#include <QImage>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

struct SResourceBrowserLoadResult
{
    CResourceCatalog catalog;
    QStringList errors;
};

struct SResourceBrowserPreview
{
    bool valid{false};
    QString title;
    QString details;
    QString text;
    QImage image;
    QByteArray sha256;
    QString error;
};

class CResourceBrowser final : public QWidget
{
    Q_OBJECT
public:
    explicit CResourceBrowser(QWidget* parent = nullptr);
    void setActiveProfileSources(const QVector<QString>& sources,
                                 const QString& profileName);
    bool uiContractSatisfied() const;

signals:
    void databaseEditorRequested(const QString& archivePath);

private:
    static constexpr int ArchivePathRole = Qt::UserRole + 81;
    void beginLoad(const QVector<QString>& archives, bool activeProfile,
                   const QString& label);
    void finishLoad();
    void rebuildList();
    void beginPreview(QTreeWidgetItem* item);
    void finishPreview();
    void exportSelected();
    void copySelectedName();
    void openSelectedLocation();
    void openExternalArchive();
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
    QVector<SResourceCatalogEntry> m_visibleEntries;
    quint64 m_loadGeneration{0};
    quint64 m_previewGeneration{0};
};

#endif // RESOURCE_BROWSER_H
