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
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {
QString localizedSetupMessage(const QString& message)
{
    if(message == "The selected folder does not contain game.exe.")
        return CResourceSetupDialog::tr("The selected folder does not contain game.exe.");
    const struct { const char* prefix; const char* source; } prefixes[] = {
        {"Missing resource directory: ", "Missing resource directory: %1"},
        {"No RES archives found in: ", "No RES archives found in: %1"},
        {"Optional map directory not found: ", "Optional map directory not found: %1"},
        {"Missing runtime component: ", "Missing runtime component: %1"}
    };
    for(const auto& item : prefixes)
    {
        const QString prefix = QString::fromLatin1(item.prefix);
        if(message.startsWith(prefix))
            return CResourceSetupDialog::tr(item.source).arg(message.mid(prefix.size()));
    }
    const QRegularExpression profileMissing(
        "^(.+) was not found below the selected game installation\\.$");
    QRegularExpressionMatch match = profileMissing.match(message);
    if(match.hasMatch())
        return CResourceSetupDialog::tr(
            "%1 was not found below the selected game installation.").arg(match.captured(1));
    const QRegularExpression hashMismatch(
        "^Runtime hash mismatch \\[([^]]+)\\]: (.+) \\(SHA-256 ([^)]+)\\)$");
    match = hashMismatch.match(message);
    if(match.hasMatch())
        return CResourceSetupDialog::tr(
            "Runtime hash mismatch [%1]: %2 (SHA-256 %3)")
            .arg(match.captured(1), match.captured(2), match.captured(3));
    return message;
}
}

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
    // Opening the dialog can move focus away from this field and emit
    // editingFinished even when the path was not edited.  Recommending a
    // profile there made the configured profile jump back to the first valid
    // preset (normally JMV). Preserve the explicit current selection; the
    // Browse path still refreshes and the user can choose another preset.
    connect(m_gameRoot, &QLineEdit::editingFinished, this, [this]() { refresh(false); });
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
    refresh(false);
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
    lines.append(tr("Profile: %1").arg(m_evaluation.profileName));
    if(!m_evaluation.modRoot.isEmpty())
        lines.append(tr("Mod: %1").arg(m_evaluation.modRoot));
    lines.append(tr("RES archives: %1").arg(m_evaluation.archiveCount));
    if(!m_evaluation.resourceDirectories.isEmpty())
    {
        lines.append(tr("\nResource priority (highest first):"));
        for(const QString& directory : m_evaluation.resourceDirectories)
            lines.append("  " + directory);
    }
    if(!m_evaluation.mapDirectories.isEmpty())
    {
        lines.append(tr("\nMap locations:"));
        for(const QString& directory : m_evaluation.mapDirectories)
            lines.append("  " + directory);
    }
    if(!m_evaluation.messages.isEmpty())
    {
        lines.append(tr("\nNotices:"));
        for(const QString& message : m_evaluation.messages)
            lines.append("  " + localizedSetupMessage(message));
    }
    lines.append(m_evaluation.valid ? tr("\nReady to apply.")
                                    : tr("\nSelect a valid installation or another preset."));
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
