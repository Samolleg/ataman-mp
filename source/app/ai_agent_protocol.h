#ifndef AI_AGENT_PROTOCOL_H
#define AI_AGENT_PROTOCOL_H
#include <QtCore>

namespace ai {
constexpr int MaxFrame = 1024 * 1024;
constexpr int MaxResult = 512 * 1024;
constexpr int MaxPage = 200;
constexpr int MaxSamples = 4096;
constexpr qint64 MaxMapBytes = 64 * 1024 * 1024;
QString discoveryDirectory();
bool secureDirectory(const QString& path);
QJsonObject error(const QString& message);
QJsonArray toolDefinitions();
// Local wire major 1 is additive to the unversioned 218-221 envelope.
constexpr int LocalProtocolMajor = 1;
QString agentInstructions();
QJsonObject mcpCatalog();
bool validateArguments(const QString& name, const QJsonObject& args, QString* error);
// Newline-delimited UTF-8 JSON, on both stdio and the private local pipe.
// A partial frame is retained; oversized input is rejected before parsing.
bool takeFrame(QByteArray& buffer, QJsonObject* value, QString* error);
QJsonObject registration(const QString& client, const QString& executable);
}
#endif
