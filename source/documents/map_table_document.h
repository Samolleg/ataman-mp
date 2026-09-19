#ifndef MAP_TABLE_DOCUMENT_H
#define MAP_TABLE_DOCUMENT_H

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

struct SMapTableMaps
{
    bool present{false};
    int sectorWidth{0};
    int sectorHeight{0};
    QString minimapTexture;
    QString objectivesTexture;

    bool operator==(const SMapTableMaps& other) const;
    bool operator!=(const SMapTableMaps& other) const { return !(*this == other); }
};

struct SMapTableExit
{
    int exitId{0};
    // Game identity is the one-based position, not the ignored #exit label.
    QString sourceLabel;
    QString destinationZoneId;
    int destinationExitId{0};
    QRectF deployRectangle;
    bool hasDeployRectangle{false};
    QRectF removeRectangle;
    bool hasRemoveRectangle{false};
    bool removeOverridesDeploy{true};
    bool hasDeployExitAngle{false};
    double deployExitAngle{0};
    bool hasRemoveExitAngle{false};
    double removeExitAngle{0};
    bool hasEffectiveRemove() const { return hasDeployRectangle || hasRemoveRectangle; }
    QRectF effectiveRemove() const {
        return hasRemoveRectangle && (!hasDeployRectangle || removeOverridesDeploy)
            ? removeRectangle : deployRectangle;
    }
    double effectiveRemoveAngle() const {
        return hasRemoveRectangle && (!hasDeployRectangle || removeOverridesDeploy)
            ? removeExitAngle : deployExitAngle;
    }
    double passTime{0.0};
    bool hasPassTime{false};
    double viewAngle{0.0};
    bool hasViewAngle{false};
    double deployAngle{0.0};
    bool hasDeployAngle{false};

    bool operator==(const SMapTableExit& other) const;
    bool operator!=(const SMapTableExit& other) const { return !(*this == other); }
};

struct SMapTableZone
{
    QString zoneId;
    QString allodId;
    QString zoneType;
    QString mprName;
    QString mobName;
    SMapTableMaps maps;
    QVector<SMapTableExit> exits;

    // Exact decoded source block for advanced review. Structured edits retain
    // unknown tags, comments, blank lines, and their relative ordering.
    QString rawBlock;
};

struct SMapTableEditResult
{
    bool success{false};
    QStringList errors;
    QStringList warnings;
    QStringList changedFields;

    QString toText() const;
};

class CMapTableDocument
{
public:
    bool loadBytes(const QByteArray& bytes, QStringList* errors = nullptr);

    const QByteArray& originalBytes() const { return m_originalBytes; }
    QByteArray serialize() const;
    const QVector<SMapTableZone>& zones() const { return m_zones; }
    bool isDirty() const { return m_dirty; }

    SMapTableEditResult replaceZone(const QString& originalZoneId,
                                    const SMapTableZone& replacement);
    SMapTableEditResult replaceZoneAt(int zoneIndex,
                                      const SMapTableZone& replacement);
    SMapTableEditResult replaceRawZoneSpan(const QString& originalZoneId,
                                           const QString& rawText);
    SMapTableEditResult replaceRawZoneSpanAt(int zoneIndex,
                                             const QString& rawText);
    SMapTableEditResult appendZone(const SMapTableZone& zone);

    static SMapTableEditResult validateZone(const SMapTableZone& zone,
                                            const QVector<SMapTableZone>& existing = {});
    SMapTableEditResult validateAllZones() const;

private:
    struct SLine
    {
        QByteArray content;
        QByteArray ending;
    };
    struct SZoneSpan
    {
        int start{-1};
        int end{-1};
        int reviewStart{-1};
        int reviewEnd{-1};
        SMapTableZone value;
    };

    bool reparse(QStringList* errors = nullptr);
    QByteArray encodeLine(const QString& text, bool* ok = nullptr) const;
    QString decodeLine(const QByteArray& bytes) const;
    QByteArray preferredEnding() const;

    QByteArray m_originalBytes;
    QVector<SLine> m_lines;
    QVector<SZoneSpan> m_spans;
    QVector<SMapTableZone> m_zones;
    bool m_dirty{false};
};

#endif // MAP_TABLE_DOCUMENT_H
