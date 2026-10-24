#ifndef AI_AGENT_READ_H
#define AI_AGENT_READ_H
#include "ai_agent_protocol.h"
#include "terrain_edit_model.h"
#include <atomic>
#include <memory>
#include "profile_database.h"
namespace ai {
using Cancellation = std::shared_ptr<std::atomic_bool>;
QJsonObject terrainSnapshot(const CTerrainEditModel& model, const QJsonObject& args,
                            const Cancellation& cancel);
QJsonObject paintSnapshot(const CTerrainEditModel& model, const QJsonObject& args);
QJsonObject databaseSnapshot(const QSharedPointer<const ei::SProfileDatabaseSnapshot>& database,
                             const QJsonObject& args, const Cancellation& cancel);
int readWorkerMain(int argc, char** argv);
}
class MainWindow;
class CAiReadAdapter {
public:
    static QJsonObject context(MainWindow* window);
    static QJsonObject read(MainWindow* window, const QString& method, const QJsonObject& args);
    static CTerrainEditModel terrain(MainWindow* window);
    static QStringList mapRoots(MainWindow* window);
};
#endif
