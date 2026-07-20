#ifndef COLLECTION_EDITOR_DIALOG_H
#define COLLECTION_EDITOR_DIALOG_H

#include <QDialog>
#include <QVector>

#include "collection_document.h"
#include "map_catalog.h"

class QComboBox;
class QLineEdit;
class QListWidget;

class CCollectionEditorDialog : public QDialog
{
    Q_OBJECT
public:
    CCollectionEditorDialog(const SEnduranceCollection& initial,
                            const QVector<SMapCatalogEntry>& mapEntries,
                            QWidget* parent = nullptr);
    SEnduranceCollection collection() const;

private slots:
    void chooseMpr();
    void addMobs();
    void removeMobs();
    void moveMobUp();
    void moveMobDown();
    void validateAndAccept();

private:
    QLineEdit* m_name;
    QLineEdit* m_profile;
    QComboBox* m_map;
    QLineEdit* m_mpr;
    QListWidget* m_mobs;
};

#endif
