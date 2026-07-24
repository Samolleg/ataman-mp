#ifndef HOT_RELOAD_PROTOCOL_H
#define HOT_RELOAD_PROTOCOL_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct SMapHotReloadRequest
{
    int protocolVersion{3};
    quint64 sequence{0};
    quint64 generation{0};
    quint64 runtimeStateSequence{0};
    QString runId;
    QString profileId;
    QString sourceZoneId;
    QString bootstrapMprName;
    QString bootstrapMobName;
    QString zoneId;
    QString bootstrapMode{"automatic"};
    int entranceId{0};
    QString mprPath;
    QString mobPath;
    QString mprSha256;
    QString mobSha256;
    QString bootstrapMobPath;
    QString bootstrapMobSha256;
    QString savePath;
    bool preservePosition{false};
    bool graphless{false};
    bool syntheticMapIdentity{false};
    int mapSizeX{0};
    int mapSizeY{0};
    double spawnX{0.0};
    double spawnY{0.0};
};

QJsonObject mapHotReloadRequestToJson(const SMapHotReloadRequest& request);
bool mapHotReloadRequestFromJson(const QJsonObject& object,
                                 SMapHotReloadRequest* request,
                                 QStringList* errors = nullptr);
bool validateMapHotReloadRequest(const SMapHotReloadRequest& request,
                                 const QString& ownedRunRoot,
                                 QStringList* errors = nullptr);
QByteArray mapHotReloadRequestToControlBytes(const SMapHotReloadRequest& request);
bool mapHotReloadRequestFromControlBytes(const QByteArray& bytes,
                                         SMapHotReloadRequest* request,
                                         QStringList* errors = nullptr);

#endif
