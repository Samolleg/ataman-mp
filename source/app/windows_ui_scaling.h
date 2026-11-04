#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

class QApplication;

namespace ei {
struct SUiMonitorDpi
{
    QString name;
    qreal dpi{96};
    bool primary{false};
};

struct SUiScaleEnvironment
{
    QByteArray globalFactor;
    QByteArray screenFactors;
};

QString persistedUiScaleMode(const QString& optionsPath = QStringLiteral("endurance-options.json"));
QVector<SUiMonitorDpi> windowsMonitorDpi();
SUiScaleEnvironment uiScaleEnvironment(const QVector<SUiMonitorDpi>& monitors, const QString& mode);
void configureUiScaling(int argc, char* argv[]);
void initializeUiScaling(QApplication& application);
}
