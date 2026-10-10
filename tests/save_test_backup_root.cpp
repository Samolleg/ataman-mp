#include <QDir>
#include <QTemporaryDir>

namespace {
void initializeSaveTestBackupRoot()
{
    // Covers default-constructed managers and document save coordinators,
    // including QTEST_APPLESS_MAIN targets. Each executable/run owns its root.
    static QTemporaryDir temporary(QDir::tempPath()
        + QStringLiteral("/ei-mapper-native-save-test-XXXXXX"));
    if(!temporary.isValid()
       || !qputenv("EI_MAPPER_TEST_BACKUP_ROOT",
                   temporary.filePath(QStringLiteral("backups")).toUtf8()))
        qFatal("Cannot initialize the native save test backup root");
}
}

Q_CONSTRUCTOR_FUNCTION(initializeSaveTestBackupRoot)
