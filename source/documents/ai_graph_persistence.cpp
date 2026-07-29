#include "ai_graph_persistence.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>

#include "ai_graph_document.h"
#include "backup_manager.h"

namespace {
bool readFile(const QFileInfo& info, QByteArray* bytes, QString* error)
{
    if(bytes)
        bytes->clear();
    QFile file(info.absoluteFilePath());
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error)
            *error = file.errorString();
        return false;
    }
    const QByteArray data = file.readAll();
    if(file.error() != QFileDevice::NoError)
    {
        if(error)
            *error = file.errorString();
        return false;
    }
    if(bytes)
        *bytes = data;
    return true;
}

bool validateGraph(const QByteArray& bytes, QString* error)
{
    CAiGraphDocument graph;
    QStringList warnings;
    if(!graph.parse(bytes, error, &warnings))
        return false;
    return graph.validateRuntimeCompatibility(graph.width(), graph.height(), error);
}
}

QString aiGraphRuntimeSiblingPath(const QFileInfo& activeMpr)
{
    if(activeMpr.absoluteFilePath().isEmpty())
        return QString();
    return QDir::cleanPath(QDir(activeMpr.absolutePath()).filePath(
        activeMpr.completeBaseName() + QStringLiteral(".grp")));
}

bool aiGraphEmbeddedSourceShadowsExternal(bool primaryMobHasEmbeddedGraph)
{
    return primaryMobHasEmbeddedGraph;
}

QString aiGraphSharedMobWarning(const QString& mobFileName,
                                const QStringList& mapOwners)
{
    if(mapOwners.size() < 2)
        return QString();
    return QStringLiteral(
        "%1 is shared by %2 map registrations (%3). Baking or removing its embedded AI graph changes runtime graph ownership for both variants and all listed registrations.")
        .arg(mobFileName).arg(mapOwners.size()).arg(mapOwners.join(", "));
}

SAiGraphPreviewSource selectAiGraphPreviewSource(
    const QByteArray& primaryEmbeddedGraph,
    const QString& primaryMobFileName,
    const QFileInfo& activeMpr)
{
    SAiGraphPreviewSource result;
    if(!primaryEmbeddedGraph.isEmpty())
    {
        result.bytes = primaryEmbeddedGraph;
        result.embedded = true;
        result.label = QStringLiteral("Embedded: %1").arg(primaryMobFileName);
        return result;
    }
    const QString externalPath = aiGraphRuntimeSiblingPath(activeMpr);
    if(externalPath.isEmpty())
        return result;
    QFile external(externalPath);
    if(!external.open(QIODevice::ReadOnly))
        return result;
    result.bytes = external.readAll();
    if(external.error() != QFileDevice::NoError)
    {
        result.bytes.clear();
        return result;
    }
    result.path = externalPath;
    result.label = QStringLiteral("External: %1").arg(QFileInfo(externalPath).fileName());
    return result;
}

SAiGraphFileCommitResult commitValidatedAiGraphFile(
    const SAiGraphFileCommitRequest& request)
{
    SAiGraphFileCommitResult result;
    result.phase = QStringLiteral("validation");
    if(request.destination.absoluteFilePath().isEmpty()
       || request.graphBytes.isEmpty())
    {
        result.error = QStringLiteral("A destination and graph payload are required.");
        return result;
    }
    if(!validateGraph(request.graphBytes, &result.error))
        return result;
    result.sha256 = QString::fromLatin1(QCryptographicHash::hash(
        request.graphBytes, QCryptographicHash::Sha256).toHex());

    const QFileInfo destination(request.destination.absoluteFilePath());
    const bool existed = destination.exists();
    QByteArray original;
    if(existed && !readFile(destination, &original, &result.error))
    {
        result.phase = QStringLiteral("destination-read");
        return result;
    }
    if(existed && original == request.graphBytes)
    {
        result.success = true;
        result.noOp = true;
        result.phase = QStringLiteral("no-op");
        return result;
    }
    if(!QDir(destination.absolutePath()).exists())
    {
        result.error = QStringLiteral("Destination directory does not exist: %1")
            .arg(destination.absolutePath());
        return result;
    }

    result.phase = QStringLiteral("staging");
    QTemporaryFile stage(QDir(destination.absolutePath()).filePath(
        QStringLiteral(".EIMapperEndurance-graph-stage-XXXXXX.grp")));
    stage.setAutoRemove(true);
    if(!stage.open() || stage.write(request.graphBytes) != request.graphBytes.size()
       || !stage.flush())
    {
        result.error = stage.errorString();
        return result;
    }
    const QString stagePath = stage.fileName();
    stage.close();
    QByteArray reopened;
    if(!readFile(QFileInfo(stagePath), &reopened, &result.error)
       || reopened != request.graphBytes || !validateGraph(reopened, &result.error))
        return result;

    QByteArray current;
    if(existed && (!readFile(destination, &current, &result.error)
                   || current != original))
    {
        result.phase = QStringLiteral("race-check");
        if(result.error.isEmpty())
            result.error = QStringLiteral("The destination changed during staging.");
        return result;
    }
    if(!existed && QFileInfo::exists(destination.absoluteFilePath()))
    {
        result.phase = QStringLiteral("race-check");
        result.error = QStringLiteral("The destination appeared during staging.");
        return result;
    }

    if(existed)
    {
        result.phase = QStringLiteral("backup");
        const SBackupResult backup = CBackupManager(
            request.backupRootOverride).snapshotBeforeSave(destination);
        if(!backup.success)
        {
            result.error = backup.error;
            return result;
        }
        result.backupPath = backup.backupPath;
    }
#ifdef QT_TESTLIB_LIB
    if(request.simulateAtomicCommitFailure)
    {
        result.phase = QStringLiteral("atomic-replacement");
        result.error = QStringLiteral("Simulated atomic replacement failure.");
        return result;
    }
#endif
    result.phase = QStringLiteral("atomic-replacement");
    QSaveFile output(destination.absoluteFilePath());
    if(!output.open(QIODevice::WriteOnly)
       || output.write(request.graphBytes) != request.graphBytes.size()
       || !output.commit())
    {
        result.error = output.errorString();
        return result;
    }
    QByteArray committed;
    if(!readFile(destination, &committed, &result.error)
       || committed != request.graphBytes || !validateGraph(committed, &result.error))
    {
        result.phase = QStringLiteral("post-commit-validation");
        return result;
    }
    result.success = true;
    result.phase = QStringLiteral("complete");
    return result;
}

bool validateExistingAiGraphFile(const QFileInfo& source,
                                 int expectedWidth,
                                 int expectedHeight,
                                 QStringList* warnings,
                                 QString* error)
{
    if(!source.isFile())
    {
        if(error)
            *error = QStringLiteral("No external AI graph exists at %1.")
                .arg(source.absoluteFilePath());
        return false;
    }
    QByteArray bytes;
    if(!readFile(source, &bytes, error))
        return false;

    CAiGraphDocument graph;
    QStringList parseWarnings;
    if(!graph.parse(bytes, error, &parseWarnings))
        return false;
    if(warnings)
        warnings->append(parseWarnings);
    return graph.validateRuntimeCompatibility(expectedWidth, expectedHeight,
                                              error, warnings, true);
}
