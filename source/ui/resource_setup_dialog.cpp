#include "resource_setup_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

CResourceSetupDialog::CResourceSetupDialog(const QString& initialGameRoot,
                                           const QString& initialProfileId,
                                           QWidget* parent)
    : QDialog(parent)
    , m_gameRoot(new QLineEdit(this))
    , m_profile(new QComboBox(this))
    , m_summary(new QPlainTextEdit(this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
    , m_profiles(loadResourceProfiles())
{
    setWindowTitle(tr("Game and mod setup"));
    setMinimumWidth(650);
    setModal(true);

    QLabel* introduction = new QLabel(tr(
        "Choose the Evil Islands installation folder. Endurance will find the archives and maps for the selected preset automatically."), this);
    introduction->setWordWrap(true);

    QPushButton* browse = new QPushButton(tr("Browse..."), this);
    QHBoxLayout* rootLayout = new QHBoxLayout;
    rootLayout->addWidget(m_gameRoot, 1);
    rootLayout->addWidget(browse);

    QFormLayout* form = new QFormLayout;
    form->addRow(tr("Game installation:"), rootLayout);
    form->addRow(tr("Editing profile:"), m_profile);

    m_summary->setReadOnly(true);
    m_summary->setMinimumHeight(190);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(introduction);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Detected configuration:"), this));
    layout->addWidget(m_summary, 1);
    layout->addWidget(m_buttons);

    for(const SResourceProfile& profile : m_profiles)
        m_profile->addItem(profile.displayName, profile.id);

    const int configuredProfile = m_profile->findData(initialProfileId);
    if(configuredProfile >= 0)
        m_profile->setCurrentIndex(configuredProfile);

    QString root = initialGameRoot;
    if(root.isEmpty())
    {
        const QStringList discovered = discoverGameInstallations();
        if(!discovered.isEmpty())
            root = discovered.first();
    }
    m_gameRoot->setText(root);

    connect(browse, &QPushButton::clicked, this, &CResourceSetupDialog::chooseGameRoot);
    connect(m_gameRoot, &QLineEdit::editingFinished, this, [this]() { refresh(true); });
    connect(m_profile, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refresh(false); });
    connect(m_buttons, &QDialogButtonBox::accepted, this, &CResourceSetupDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &CResourceSetupDialog::reject);
    refresh(configuredProfile < 0);
}

void CResourceSetupDialog::chooseGameRoot()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Choose Evil Islands installation"), m_gameRoot->text(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if(directory.isEmpty())
        return;
    m_gameRoot->setText(directory);
    refresh(true);
}

void CResourceSetupDialog::refresh(bool chooseRecommendedProfile)
{
    if(m_profiles.isEmpty())
        return;

    if(chooseRecommendedProfile)
    {
        int recommended = -1;
        for(int index = 0; index < m_profiles.size(); ++index)
        {
            if(evaluateResourceProfile(m_gameRoot->text(), m_profiles[index]).valid)
            {
                recommended = index;
                break;
            }
        }
        if(recommended >= 0 && recommended != m_profile->currentIndex())
        {
            m_profile->setCurrentIndex(recommended);
            return;
        }
    }

    const int index = m_profile->currentIndex();
    if(index < 0 || index >= m_profiles.size())
        return;
    m_evaluation = evaluateResourceProfile(m_gameRoot->text(), m_profiles[index]);

    QStringList lines;
    lines.append(QString("Profile: %1").arg(m_evaluation.profileName));
    if(!m_evaluation.modRoot.isEmpty())
        lines.append(QString("Mod: %1").arg(m_evaluation.modRoot));
    lines.append(QString("RES archives: %1").arg(m_evaluation.archiveCount));
    if(!m_evaluation.resourceDirectories.isEmpty())
    {
        lines.append("\nResource priority (highest first):");
        for(const QString& directory : m_evaluation.resourceDirectories)
            lines.append("  " + directory);
    }
    if(!m_evaluation.mapDirectories.isEmpty())
    {
        lines.append("\nMap locations:");
        for(const QString& directory : m_evaluation.mapDirectories)
            lines.append("  " + directory);
    }
    if(!m_evaluation.messages.isEmpty())
    {
        lines.append("\nNotices:");
        for(const QString& message : m_evaluation.messages)
            lines.append("  " + message);
    }
    lines.append(m_evaluation.valid ? "\nReady to apply." : "\nSelect a valid installation or another preset.");
    m_summary->setPlainText(lines.join('\n'));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(m_evaluation.valid);
}

void CResourceSetupDialog::accept()
{
    refresh(false);
    if(!m_evaluation.valid)
    {
        QMessageBox::warning(this, tr("Game and mod setup"),
                             tr("The selected installation and preset are not valid yet."));
        return;
    }
    QDialog::accept();
}
