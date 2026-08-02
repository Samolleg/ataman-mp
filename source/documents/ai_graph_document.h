#ifndef AI_GRAPH_DOCUMENT_H
#define AI_GRAPH_DOCUMENT_H

#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

struct SAiGraphLayer
{
    QVector<qint16> weights;
    QByteArray packedPositions;
    QVector<quint16> components;
};

struct SAiGraphLayerDifference
{
    qint64 loadedOpenLinks{0};
    qint64 regeneratedOpenLinks{0};
    qint64 missingLinks{0};
    qint64 extraLinks{0};
    qint64 changedWeights{0};
    qint64 changedPositions{0};

    bool identical() const
    {
        return missingLinks == 0 && extraLinks == 0
            && changedWeights == 0 && changedPositions == 0;
    }
};

struct SAiGraphDifference
{
    bool comparable{false};
    QString error;
    QVector<SAiGraphLayerDifference> layers;

    bool identical() const;
    SAiGraphLayerDifference total() const;
};

class CAiGraphDocument
{
public:
    static constexpr int LayerCount = 8;
    static constexpr int DirectionCount = 8;

    bool parse(const QByteArray& data, QString* error = nullptr,
               QStringList* warnings = nullptr);
    bool initializePreview(int width, int height,
                           const QVector<SAiGraphLayer>& layers,
                           QString* error = nullptr);
    void clear();

    bool isValid() const { return m_valid; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    int cellCount() const { return m_width * m_height; }
    const QByteArray& rawBytes() const { return m_rawBytes; }
    QByteArray serializeNoOp() const { return m_rawBytes; }
    QByteArray serialize(QString* error = nullptr) const;
    bool recolorConnectedComponents(QString* error = nullptr);
    bool validateRuntimeCompatibility(int expectedWidth, int expectedHeight,
                                      QString* error = nullptr,
                                      QStringList* warnings = nullptr,
                                      bool allowLegacyComponentReuse = false) const;

    qint16 weight(int layer, int x, int y, int direction) const;
    quint8 packedPosition(int layer, int x, int y) const;
    int localX(int layer, int x, int y) const;
    int localY(int layer, int x, int y) const;
    quint16 component(int layer, int x, int y) const;
    SAiGraphDifference compareNavigation(const CAiGraphDocument& regenerated) const;

    static QPoint directionDelta(int direction);
    static QString layerLabel(int layer);
    static double canonicalHeight(int layer);

private:
    int index(int x, int y) const;
    bool validCell(int layer, int x, int y) const;

    int m_width{0};
    int m_height{0};
    bool m_valid{false};
    QByteArray m_rawBytes;
    QVector<SAiGraphLayer> m_layers;
};

#endif // AI_GRAPH_DOCUMENT_H
