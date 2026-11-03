#include "camera_document.h"
#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <QtMath>
#include <cmath>
#include <cstring>

namespace {
float readFloat(const QByteArray& bytes, int offset)
{
    const quint32 bits = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(bytes.constData() + offset));
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
void writeFloat(QByteArray& bytes, int offset, float value)
{
    quint32 bits;
    std::memcpy(&bits, &value, 4);
    qToLittleEndian<quint32>(bits, reinterpret_cast<uchar*>(bytes.data() + offset));
}
QQuaternion orientation(const QVector3D& right, const QVector3D& up, const QVector3D& forward)
{
    QMatrix3x3 matrix;
    for(int row = 0; row < 3; ++row) {
        matrix(row, 0) = right[row];
        matrix(row, 1) = -up[row];
        matrix(row, 2) = forward[row];
    }
    return QQuaternion::fromRotationMatrix(matrix).normalized();
}
}

QVector3D SCameraPose::forward() const { return rotation.normalized().rotatedVector(QVector3D(0, 0, 1)); }
QVector3D SCameraPose::up() const { return rotation.normalized().rotatedVector(QVector3D(0, -1, 0)); }
QMatrix4x4 SCameraPose::view() const
{
    QMatrix4x4 result;
    result.lookAt(position, position + forward(), up());
    return result;
}
QVector3D SCameraPose::angles() const
{
    const QVector3D f = forward();
    const float yaw = qRadiansToDegrees(std::atan2(f.y(), f.x()));
    const float pitch = qRadiansToDegrees(std::asin(qBound(-1.0f, f.z(), 1.0f)));
    const SCameraPose level = fromAngles(position, QVector3D(yaw, pitch, 0));
    const float roll = qRadiansToDegrees(std::atan2(
        QVector3D::dotProduct(QVector3D::crossProduct(level.up(), up()), f),
        QVector3D::dotProduct(level.up(), up())));
    return QVector3D(yaw, pitch, roll);
}
SCameraPose SCameraPose::fromAngles(const QVector3D& position, const QVector3D& angles)
{
    const float yaw = qDegreesToRadians(angles.x());
    const float pitch = qDegreesToRadians(angles.y());
    const QVector3D forward(std::cos(pitch)*std::cos(yaw), std::cos(pitch)*std::sin(yaw), std::sin(pitch));
    const QVector3D right(std::sin(yaw), -std::cos(yaw), 0);
    const QVector3D levelUp = QVector3D::crossProduct(right, forward).normalized();
    const QVector3D up = QQuaternion::fromAxisAndAngle(forward, angles.z()).rotatedVector(levelUp);
    return {position, orientation(QVector3D::crossProduct(forward, up), up, forward)};
}
bool SCameraPose::fromView(const QMatrix4x4& view, SCameraPose* pose)
{
    bool ok = false;
    const QMatrix4x4 inverse = view.inverted(&ok);
    if(!ok || !pose) return false;
    for(int i = 0; i < 16; ++i) if(!std::isfinite(inverse.constData()[i])) return false;
    const QVector3D right = inverse.column(0).toVector3D().normalized();
    const QVector3D up = inverse.column(1).toVector3D().normalized();
    const QVector3D forward = -inverse.column(2).toVector3D().normalized();
    pose->position = inverse.column(3).toVector3D();
    pose->rotation = orientation(right, up, forward);
    return true;
}

bool CCameraDocument::validate(const QByteArray& bytes, QString* error)
{
    if(bytes.isEmpty() || bytes.size() % 36 != 0) {
        if(error) *error = QString::fromUtf8("CAM должен содержать целые записи по 36 байт.");
        return false;
    }
    for(int offset = 0; offset < bytes.size(); offset += 36) {
        for(int component = 8; component < 36; component += 4) {
            if(!std::isfinite(readFloat(bytes, offset + component))) {
                if(error) *error = QString::fromUtf8("CAM содержит нечисловую позицию или ориентацию в ключе %1.").arg(offset / 36 + 1);
                return false;
            }
        }
        const QQuaternion q(readFloat(bytes, offset+20), readFloat(bytes, offset+24),
                            readFloat(bytes, offset+28), readFloat(bytes, offset+32));
        if(q.lengthSquared() < 1e-12f || !std::isfinite(q.lengthSquared())) {
            if(error) *error = QString::fromUtf8("CAM содержит недопустимый кватернион в ключе %1.").arg(offset / 36 + 1);
            return false;
        }
    }
    return true;
}
bool CCameraDocument::open(const QString& source, const QString& destination, QString* error)
{
    QFile file(source);
    if(!file.open(QIODevice::ReadOnly)) { if(error) *error = file.errorString(); return false; }
    const QByteArray bytes = file.readAll();
    if(file.error() != QFile::NoError || !validate(bytes, error)) return false;
    QByteArray before;
    const bool existed = QFileInfo::exists(destination);
    if(existed) {
        QFile target(destination);
        if(!target.open(QIODevice::ReadOnly)) { if(error) *error = target.errorString(); return false; }
        before = target.readAll();
        if(target.error() != QFile::NoError) { if(error) *error = target.errorString(); return false; }
        if(before != bytes) { if(error) *error = "CAM destination differs from source"; return false; }
    }
    m_source = QFileInfo(source).absoluteFilePath();
    m_path = QFileInfo(destination).absoluteFilePath();
    m_existed = existed;
    m_before = before;
    m_saved = m_bytes = bytes;
    return true;
}
bool CCameraDocument::restore(const QByteArray& bytes, QString* error)
{
    if(bytes.size() != m_saved.size() || !validate(bytes, error)) return false;
    for(int offset = 0; offset < bytes.size(); offset += 36) {
        if(bytes.mid(offset, 8) != m_saved.mid(offset, 8)) {
            if(error) *error = "CAM timing or unknown bytes changed";
            return false;
        }
    }
    m_bytes = bytes;
    return true;
}
SCameraPose CCameraDocument::pose(int index) const
{
    if(index < 0 || index >= count()) return {};
    const int offset = index * 36;
    return {QVector3D(readFloat(m_bytes, offset+8), readFloat(m_bytes, offset+12), readFloat(m_bytes, offset+16)),
            QQuaternion(readFloat(m_bytes, offset+20), readFloat(m_bytes, offset+24), readFloat(m_bytes, offset+28), readFloat(m_bytes, offset+32))};
}
quint32 CCameraDocument::timeBits(int index) const
{
    if(index < 0 || index >= count()) return 0;
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(m_bytes.constData()+index*36));
}
float CCameraDocument::timeValue(int index) const
{
    return index >= 0 && index < count() ? readFloat(m_bytes, index*36) : 0;
}
bool CCameraDocument::stationary() const
{
    for(int index = 1; index < count(); ++index)
        if(m_bytes.mid(index*36+8, 28) != m_bytes.mid(8, 28)) return false;
    return count() > 0;
}
bool CCameraDocument::setPose(int index, const SCameraPose& value, bool allKeys, QString* error)
{
    if(index < 0 || index >= count()) { if(error) *error = "CAM key out of range"; return false; }
    if(allKeys && !stationary()) {
        if(error) *error = QString::fromUtf8("Все ключи разом доступны для неподвижной камеры. Выберите отдельный ключ пути.");
        return false;
    }
    QByteArray candidate = m_bytes;
    const SCameraPose previous = pose(index);
    const bool positionChanged = value.position != previous.position;
    const bool rotationChanged = value.rotation != previous.rotation;
    // Only explicitly changed components are written. Keep quaternions and
    // all other records byte-identical when changing XYZ alone.
    for(int key = 0; key < count(); ++key) {
        if(!allKeys && key != index) continue;
        const int offset = key*36;
        if(positionChanged) for(int axis = 0; axis < 3; ++axis) writeFloat(candidate, offset+8+axis*4, value.position[axis]);
        if(rotationChanged) {
            writeFloat(candidate, offset+20, value.rotation.scalar());
            writeFloat(candidate, offset+24, value.rotation.x());
            writeFloat(candidate, offset+28, value.rotation.y());
            writeFloat(candidate, offset+32, value.rotation.z());
        }
    }
    return restore(candidate, error);
}
bool CCameraDocument::prepareSave(SMapSaveFile* file, QString* error) const
{
    if(!file || !validate(m_bytes, error)) return false;
    *file = SMapSaveFile();
    file->path = m_path;
    file->before = m_before;
    file->after = m_bytes;
    file->existed = m_existed;
    return true;
}
void CCameraDocument::acceptCommitted(const QByteArray& bytes)
{
    m_before = m_saved = bytes;
    m_existed = true;
    m_source = m_path;
}
