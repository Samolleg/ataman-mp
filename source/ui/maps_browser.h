#ifndef MAPS_BROWSER_H
#define MAPS_BROWSER_H

#include <QWidget>
#include <QVector>

#include "map_catalog.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class CMapsBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit CMapsBrowser(QWidget* parent = nullptr);
    void refresh(const QVector<QString>& resourceRoots, const QVector<QString>& mapRoots,
                 const QString& collectionsRoot);

signals:
    void openMapRequested(QString mprPath, QString mobPath, QString zoneId);
    void openCollectionRequested(QString path);
    void refreshRequested();

private slots:
    void applyFilter(const QString& text);
    void activateItem(QTreeWidgetItem* item, int column);

private:
    void rebuildTree();

    QLineEdit* m_filter;
    QTreeWidget* m_tree;
    QLabel* m_status;
    QPushButton* m_refresh;
    QVector<SMapCatalogEntry> m_entries;
    QStringList m_collectionPaths;
};

#endif // MAPS_BROWSER_H
