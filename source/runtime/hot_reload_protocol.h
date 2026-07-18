#ifndef HOT_RELOAD_PROTOCOL_H
#define HOT_RELOAD_PROTOCOL_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct SMapHotReloadRequest
{
    int protocolVersion{1};
    quint64 sequence{0};
    QString runId;
    QString profileId;
    QString zoneId;
    int entranceId{0};
    QString mprPath;
    QString mobPath;
    QString mprSha256;
    QString mobSha256;
    bool preservePosition{false};
};

QJsonObject mapHotReloadRequestToJson(const SMapHotReloadRequest& request);
bool mapHotReloadRequestFromJson(const QJsonObject& object,
                                 SMapHotReloadRequest* request,
                                 QStringList* errors = nullptr);
bool validateMapHotReloadRequest(const SMapHotReloadRequest& request,
                                 const QString& ownedRunRoot,
                                 QStringList* errors = nullptr);

#endif
