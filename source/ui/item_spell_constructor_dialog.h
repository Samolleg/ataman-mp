#ifndef ITEM_SPELL_CONSTRUCTOR_DIALOG_H
#define ITEM_SPELL_CONSTRUCTOR_DIALOG_H

#include <QDialog>
#include <QSharedPointer>
#include <QPointer>
#include "item_spell_constructor.h"

class CPreview;
class CView;
class CWorldObj;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;

namespace ei {
class DatabaseDocument;
struct SProfileDatabaseSnapshot;
class ItemSpellConstructorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ItemSpellConstructorDialog(const QSharedPointer<DatabaseDocument>& document,
                                        const QStringList& resourceRoots,
                                        CView* view = nullptr, QWidget* parent = nullptr);
    ~ItemSpellConstructorDialog() override;
    QString code() const;
    void setCode(const QString& code);
    // Shared by the constructor and Add from database. Caller owns the result
    // and must make its preview's shared GL context current before calling.
    // worldObject requests a persistent ordinary object, using only visuals
    // representable by native model/parts/texture fields, or returns null.
    static CWorldObj* buildItemPreview(const ItemSpellConstructorCatalog& catalog,
        const SConstructorItem& item, const SConstructorMaterial* material,
        const QSharedPointer<const SProfileDatabaseSnapshot>& snapshot,
        bool worldObject = false, QString* diagnostic = nullptr);

private slots:
    void refreshItemFilter();
    void refreshItemFilterChoices();
    void refreshArmorLayerAvailability();
    void refreshSpellFilter();
    void selectItem();
    void selectSpell();
    void selectMaterial();
    void addRune();
    void removeRune();
    void moveRuneUp();
    void moveRuneDown();
    void useSpellInItem();
    void openItemSpellInTab();
    void importCode();
    void copyCode();
    void resetItemCamera();
    void updateOutput();

private:
    const SConstructorItem* currentItem() const;
    const SConstructorMaterial* currentMaterial() const;
    const SConstructorSpell* currentSpell() const;
    QStringList selectedRuneCodes() const;
    void rebuildMaterialChoices(const QString& selected = QString());
    void updateItemPreview();
    void updateSpellPreview();
    void showImportDiagnostic(const QStringList& messages);

    bool m_nativeLoading{false};QString m_pendingCode;
    ItemSpellConstructorCatalog m_catalog;
    QSharedPointer<const SProfileDatabaseSnapshot> m_databaseSnapshot;
    QString m_providerId;
    QString m_profileId;
    QTabWidget* m_tabs{nullptr};
    QLineEdit* m_codeEdit{nullptr};
    QLabel* m_validation{nullptr};

    QLineEdit* m_itemFilter{nullptr};
    QComboBox* m_itemCategory{nullptr};
    QComboBox* m_itemType{nullptr};
    QComboBox* m_armorLayer{nullptr};
    QListWidget* m_itemList{nullptr};
    QComboBox* m_material{nullptr};
    QLineEdit* m_itemSpell{nullptr};
    QLabel* m_itemDetails{nullptr};
    QLabel* m_itemSpellBadge{nullptr};
    CPreview* m_itemPreview{nullptr};
    CWorldObj* m_previewObject{nullptr};
    QPointer<CView> m_view;
    bool m_itemPreviewInitialized{false};
    QString m_itemPreviewCategory;

    QLineEdit* m_spellFilter{nullptr};
    QComboBox* m_spellSchool{nullptr};
    QListWidget* m_spellList{nullptr};
    QLineEdit* m_runeFilter{nullptr};
    QComboBox* m_runeType{nullptr};
    QListWidget* m_availableRunes{nullptr};
    QListWidget* m_selectedRunes{nullptr};
    QLabel* m_spellIcon{nullptr};
    QLabel* m_spellDetails{nullptr};
};
}
#endif
