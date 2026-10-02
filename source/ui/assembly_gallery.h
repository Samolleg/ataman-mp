#ifndef ASSEMBLY_GALLERY_H
#define ASSEMBLY_GALLERY_H
#include "technical_model_catalog.h"
#include <QDialog>
#include <QCache>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>
#include <memory>
#include <functional>
class QListWidget;class QLineEdit;class QComboBox;class QLabel;class QPushButton;class QTabBar;
class CAssemblyGallery final : public QDialog {
    Q_OBJECT
public:
    using Capture=std::function<bool(const QString&,ei::ObjectAssembly*,QString*)>;
    CAssemblyGallery(ei::TechnicalModelCatalog* catalog,const QStringList& resources,QWidget* parent=nullptr,Capture capture={});
    ~CAssemblyGallery() override;
    ei::ObjectAssembly selectedAssembly() const;
    void setPlacementAvailable(bool available);
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    void rebuild(const QString& preferred={});void select();void save();void remove();void replaceComposition();
    void loadVisible();void finish();
    ei::TechnicalModelCatalog* m_catalog;QVector<ei::ObjectAssembly> m_entries;Capture m_capture;
    QListWidget *m_tiles,*m_parts;QLineEdit *m_search,*m_name;QComboBox *m_category,*m_region;
    QTabBar *m_categories,*m_regions;QLabel* m_status;QPushButton *m_place,*m_save,*m_remove,*m_replace;
    bool m_placementAvailable=true,m_jobActive=false;QTimer m_timer;QFutureWatcher<QImage> m_watcher;
    quint64 m_loadedRevision=~quint64(0);
    QString m_loadingKey,m_loadingId;QCache<QString,QImage> m_images{16*1024};
    std::function<QImage(const ei::ObjectAssembly&,const std::shared_ptr<std::atomic_bool>&)> m_renderer;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
#endif
