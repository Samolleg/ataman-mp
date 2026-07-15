#ifndef ENDURANCE_DOCUMENT_H
#define ENDURANCE_DOCUMENT_H

#include <QByteArray>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

enum class EValidationSeverity
{
    Info,
    Warning,
    Error
};

struct SValidationFinding
{
    QString code;
    EValidationSeverity severity;
    QString message;
    QString location;

    QJsonObject toJson() const;
};

class CValidationReport
{
public:
    void add(const QString& code, EValidationSeverity severity, const QString& message, const QString& location = QString());
    const QVector<SValidationFinding>& findings() const { return m_findings; }
    bool hasErrors() const;
    QJsonArray toJson() const;
    QString toText() const;

private:
    QVector<SValidationFinding> m_findings;
};

class CMobDocument
{
public:
    bool load(const QFileInfo& file, CValidationReport* report = nullptr);
    QByteArray serialize() const { return m_originalBytes; }
    const QByteArray& originalBytes() const { return m_originalBytes; }
    const QVector<quint32>& duplicateIds() const { return m_duplicateIds; }
    bool isDirty() const { return false; }

private:
    QByteArray m_originalBytes;
    QVector<quint32> m_duplicateIds;
};

class CMprDocument
{
public:
    bool load(const QFileInfo& file, CValidationReport* report = nullptr);
    QByteArray serialize() const { return m_originalBytes; }
    const QByteArray& originalBytes() const { return m_originalBytes; }
    bool isDirty() const { return false; }

private:
    QByteArray m_originalBytes;
};

#endif
