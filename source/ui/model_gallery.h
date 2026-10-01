#ifndef MODEL_GALLERY_H
#define MODEL_GALLERY_H
#include "technical_model_catalog.h"
#include <QDialog>
#include <QCache>
#include <QFutureWatcher>
#include <QImage>
#include <QTimer>
#include <QSet>
#include <functional>
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QComboBox;
class QTabBar;
class CModelGallery final : public QDialog
{
    Q_OBJECT
public:
    using ThumbnailProvider = std::function<QImage(const QString&, const QString&)>;
    CModelGallery(const QStringList& models, ei::TechnicalModelCatalog* catalog,
        const QString& profile, const QStringList& textures, const ei::TechnicalTextureCatalog* scan,
        ThumbnailProvider provider, QWidget* parent = nullptr,
        const QString& initialModel = QString(), const QString& initialTexture = QString(),
        const QString& thumbnailNamespace = QString());
    ~CModelGallery() override;
    static ThumbnailProvider resourceThumbnails(const QStringList& sourcePaths);
    static QString resourceThumbnailNamespace(const QStringList& sourcePaths);
    QString selectedModel() const;
    QString selectedTexture() const;
    void setCurrentModel(const QString& model);
    void setCurrentTexture(const QString& texture);
    void refreshCatalog();
    void showCatalogError(const QString& error);
    void requireTextureSelection();
public slots:
    void accept() override;
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    friend bool runModelGalleryWidgetCheck(QString* diagnostic);
    void filter(const QString& preferredModel = QString(), const QString& preferredTexture = QString());
    void selectionChanged();
    void saveName();
    void saveTexture();
    void savePlacement();
    void loadVisibleThumbnail();
    void finishThumbnail();
    QString textureForModel(const QString& model) const;
    QString cacheKey(const QString& model, const QString& texture) const;
    QVector<ei::STechnicalTextureChoice> textureChoices(const QString& model) const;
    void reportSave(bool success, const QString& error);
    QStringList m_models, m_textures;
    ei::TechnicalModelCatalog* m_catalog;
    const ei::TechnicalTextureCatalog* m_scan;
    QString m_profile;
    QString m_thumbnailNamespace;
    QSet<QString> m_availableTextures;
    mutable QHash<QString,QVector<ei::STechnicalTextureChoice>> m_textureChoices;
    mutable quint64 m_catalogRevision = 0, m_scanRevision = 0;
    bool m_textureRequired{false};
    ThumbnailProvider m_provider;
    QLineEdit *m_search, *m_name;
    QListWidget* m_list;
    QLabel* m_status;
    QComboBox* m_texture;
    QTabBar *m_categories, *m_regions;
    QComboBox *m_categoryChoice, *m_regionChoice;
    QPushButton* m_savePlacement;
    QPushButton *m_saveName, *m_saveTexture, *m_choose;
    QTimer m_thumbnailTimer;
    QFutureWatcher<QImage> m_thumbnailWatcher;
    bool m_thumbnailJobActive = false;
    QCache<QString,QImage> m_thumbnails{16*1024};
    QString m_loadingKey, m_loadingModel;
};
#endif
