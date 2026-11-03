#pragma once

#include "map_save_batch.h"
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>

struct SCameraPose
{
    QVector3D position;
    QQuaternion rotation;
    QVector3D forward() const;
    QVector3D up() const;
    QMatrix4x4 view() const;
    // X yaw, Y pitch, Z roll, in degrees. CAM uses +Z forward and -Y up.
    QVector3D angles() const;
    static SCameraPose fromAngles(const QVector3D& position, const QVector3D& angles);
    static bool fromView(const QMatrix4x4& view, SCameraPose* pose);
};

class CCameraDocument
{
public:
    static bool validate(const QByteArray& bytes, QString* error = nullptr);
    bool open(const QString& source, const QString& destination, QString* error = nullptr);
    bool restore(const QByteArray& bytes, QString* error = nullptr);
    bool setPose(int index, const SCameraPose& pose, bool allKeys, QString* error = nullptr);
    int count() const { return m_bytes.size() / 36; }
    SCameraPose pose(int index) const;
    quint32 timeBits(int index) const;
    float timeValue(int index) const;
    bool stationary() const;
    bool dirty() const { return m_bytes != m_saved; }
    const QByteArray& bytes() const { return m_bytes; }
    const QByteArray& savedBytes() const { return m_saved; }
    const QString& path() const { return m_path; }
    const QString& source() const { return m_source; }
    bool prepareSave(SMapSaveFile* file, QString* error) const;
    void acceptCommitted(const QByteArray& bytes);
    void discard() { m_bytes = m_saved; }

private:
    QString m_source, m_path;
    QByteArray m_bytes, m_saved, m_before;
    bool m_existed{false};
};
