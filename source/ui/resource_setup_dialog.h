#ifndef RESOURCE_SETUP_DIALOG_H
#define RESOURCE_SETUP_DIALOG_H

#include <QDialog>

#include "resource_profile.h"

class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QPlainTextEdit;

class CResourceSetupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CResourceSetupDialog(const QString& initialGameRoot = QString(),
                                  const QString& initialProfileId = QString(),
                                  QWidget* parent = nullptr);
    SResourceProfileEvaluation evaluation() const { return m_evaluation; }

protected:
    void accept() override;

private:
    void chooseGameRoot();
    void refresh(bool chooseRecommendedProfile = false);

    QLineEdit* m_gameRoot;
    QComboBox* m_profile;
    QPlainTextEdit* m_summary;
    QDialogButtonBox* m_buttons;
    QVector<SResourceProfile> m_profiles;
    SResourceProfileEvaluation m_evaluation;
};

#endif // RESOURCE_SETUP_DIALOG_H
