#ifndef MODEL_GALLERY_H
#define MODEL_GALLERY_H
#include "technical_model_catalog.h"
#include <QDialog>
#include <QCache>
#include <QFutureWatcher>
#include <QImage>
#include <QTimer>
#include <functional>
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QComboBox;
class CModelGallery final : public QDialog
{
    Q_OBJECT
public:
    using ThumbnailProvider = std::function<QImage(const QString&, const QString&)>;
    CModelGallery(const QStringList& models, ei::TechnicalModelCatalog* catalog,
        const QString& profile, const QStringList& textures, const ei::TechnicalTextureCatalog* scan,
        ThumbnailProvider provider, QWidget* parent = nullptr);
    ~CModelGallery() override;
    QString selectedModel() const;
    QString selectedTexture() const;
    void setCurrentModel(const QString& model);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void filter();
    void selectionChanged();
    void saveName();
    void saveTexture();
    void loadVisibleThumbnail();
    void finishThumbnail();
    QString textureForModel(const QString& model) const;
    QString cacheKey(const QString& model, const QString& texture) const;
    void reportSave(bool success, const QString& error);
    QStringList m_models, m_textures;
    ei::TechnicalModelCatalog* m_catalog;
    const ei::TechnicalTextureCatalog* m_scan;
    QString m_profile;
    ThumbnailProvider m_provider;
    QLineEdit *m_search, *m_name;
    QListWidget* m_list;
    QLabel* m_status;
    QComboBox* m_texture;
    QPushButton *m_saveName, *m_saveTexture, *m_choose;
    QTimer m_thumbnailTimer;
    QFutureWatcher<QImage> m_thumbnailWatcher;
    QCache<QString,QImage> m_thumbnails{128};
    QString m_loadingKey, m_loadingModel;
};
#endif
