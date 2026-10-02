#ifndef MODEL_CONNECTIONS_H
#define MODEL_CONNECTIONS_H
#include <QJsonObject>
#include <QQuaternion>
#include <QVector3D>
#include <QVector>
#include <QHash>
namespace ei {
struct ModelConnection {
    QString id, label, type;
    QVector3D position, normal{1,0,0}, up{0,0,1};
    QJsonObject extra;
};
bool validConnection(const ModelConnection& point);
bool readConnections(const QJsonValue& value, QVector<ModelConnection>* points, QString* error = nullptr);
QJsonArray writeConnections(const QVector<ModelConnection>& points);
struct ConnectionPose { QVector3D position; QQuaternion rotation; };
bool connectModelFrames(const ModelConnection& source, const ModelConnection& target,
    const ConnectionPose& targetPose, ConnectionPose* result);
// Built once for a gesture. XY cells also permit snapping upper floors/roofs.
class ConnectionIndex {
public:
    void clear();
    void add(const QVector3D& position, int index);
    QVector<int> queryNear(const QVector3D& position, float radius) const;
private:
    QHash<QString,QVector<int>> m_cells;
};
}
#endif
