#ifndef MAPS_BROWSER_H
#define MAPS_BROWSER_H

#include <QWidget>
#include <QVector>

#include "map_catalog.h"

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

class CMapsBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit CMapsBrowser(QWidget* parent = nullptr);
    void refresh(const QVector<QString>& resourceRoots, const QVector<QString>& mapRoots);

signals:
    void openMapRequested(QString mprPath, QString mobPath, QString zoneId);

private slots:
    void applyFilter(const QString& text);
    void activateItem(QTreeWidgetItem* item, int column);

private:
    void rebuildTree();

    QLineEdit* m_filter;
    QTreeWidget* m_tree;
    QLabel* m_status;
    QVector<SMapCatalogEntry> m_entries;
};

#endif // MAPS_BROWSER_H
