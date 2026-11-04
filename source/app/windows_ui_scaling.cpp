#include "windows_ui_scaling.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QStringList>
#include <QtMath>
#include <cstring>
#include <cstddef>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
bool legacyScaling = false;
QString startupMode;
QString startupSource;
QVector<ei::SUiMonitorDpi> startupMonitors;

#ifdef Q_OS_WIN
using GetMonitorDpi = HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*);
struct MonitorQuery
{
    GetMonitorDpi getDpi{nullptr};
    QVector<ei::SUiMonitorDpi> monitors;
};

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context)
{
    auto& query = *reinterpret_cast<MonitorQuery*>(context);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if(!GetMonitorInfoW(monitor, &info))
        return TRUE;
    UINT x = 0, y = 0;
    if(!query.getDpi || FAILED(query.getDpi(monitor, 0, &x, &y)))
    {
        HDC dc = CreateDCW(info.szDevice, info.szDevice, nullptr, nullptr);
        if(dc)
        {
            x = UINT(GetDeviceCaps(dc, LOGPIXELSX));
            DeleteDC(dc);
        }
    }
    ei::SUiMonitorDpi value;
    value.name = QString::fromWCharArray(info.szDevice);
    value.dpi = x >= 48 && x <= 768 ? x : 96;
    value.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    query.monitors.append(value);
    return TRUE;
}
#endif
}

QString ei::persistedUiScaleMode(const QString& optionsPath)
{
    QFile file(optionsPath);
    if(!file.open(QIODevice::ReadOnly))
        return QStringLiteral("auto");
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    const QString mode = root.value("General").toObject().value("uiScaleMode").toString("auto");
    return QStringList{"auto", "100", "125", "150", "175", "200"}.contains(mode)
        ? mode : QStringLiteral("auto");
}

QVector<ei::SUiMonitorDpi> ei::windowsMonitorDpi()
{
#ifdef Q_OS_WIN
    // Process-local awareness must precede both Qt and the effective-DPI query.
    // This reads the monitor's current scale, including changes since logon.
    QLibrary user32(QStringLiteral("user32"));
    using SetContext = BOOL (WINAPI*)(HANDLE);
    auto setContext = reinterpret_cast<SetContext>(user32.resolve("SetProcessDpiAwarenessContext"));
    const bool aware = setContext && setContext(reinterpret_cast<HANDLE>(qintptr(-4)));
    QLibrary shcore(QStringLiteral("shcore"));
    if(!aware)
    {
        using SetAwareness = HRESULT (WINAPI*)(int);
        auto setAwareness = reinterpret_cast<SetAwareness>(shcore.resolve("SetProcessDpiAwareness"));
        if(setAwareness) setAwareness(2);
        else
        {
            using SetLegacyAwareness = BOOL (WINAPI*)();
            auto setLegacy = reinterpret_cast<SetLegacyAwareness>(user32.resolve("SetProcessDPIAware"));
            if(setLegacy) setLegacy();
        }
    }
    MonitorQuery query;
    query.getDpi = reinterpret_cast<GetMonitorDpi>(shcore.resolve("GetDpiForMonitor"));
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&query));
    return query.monitors;
#else
    return {};
#endif
}

ei::SUiScaleEnvironment ei::uiScaleEnvironment(const QVector<SUiMonitorDpi>& monitors,
                                              const QString& mode)
{
    SUiScaleEnvironment result;
    qreal primaryFactor = 1;
    for(const auto& monitor : monitors)
        if(monitor.primary) primaryFactor = monitor.dpi / 96.0;
    const bool automatic = mode == "auto";
    const qreal requestedFactor = automatic ? 1.0 : mode.toDouble() / 100.0;
    result.globalFactor = QByteArray::number(automatic ? 1 : requestedFactor / primaryFactor, 'g', 12);
    for(const auto& monitor : monitors)
    {
        if(!result.screenFactors.isEmpty()) result.screenFactors += ';';
        const qreal factor = automatic ? monitor.dpi / 96.0 : primaryFactor;
        result.screenFactors += monitor.name.toUtf8() + '=' + QByteArray::number(factor, 'g', 12);
    }
    return result;
}

void ei::configureUiScaling(int argc, char* argv[])
{
    for(int index = 1; index < argc; ++index)
        legacyScaling |= std::strcmp(argv[index], "--legacy-ui-scaling") == 0;
    startupMode = persistedUiScaleMode();
    if(legacyScaling)
    {
        startupSource = QStringLiteral("legacy");
        return;
    }
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    if(qEnvironmentVariableIsSet("QT_SCALE_FACTOR")
       || qEnvironmentVariableIsSet("QT_SCREEN_SCALE_FACTORS")
       || qEnvironmentVariableIsSet("QT_AUTO_SCREEN_SCALE_FACTOR"))
    {
        startupSource = QStringLiteral("environment");
        return;
    }
    startupMonitors = windowsMonitorDpi();
    if(startupMonitors.isEmpty())
    {
        startupSource = QStringLiteral("platform-default");
        return;
    }
    const auto environment = uiScaleEnvironment(startupMonitors, startupMode);
    // Screen factors cancel native font DPI to 96. The global factor supplies
    // a fixed override without multiplying fonts by Windows DPI a second time.
    qputenv("QT_SCALE_FACTOR", environment.globalFactor);
    qputenv("QT_SCREEN_SCALE_FACTORS", environment.screenFactors);
    startupSource = QStringLiteral("windows-monitors");
}

void ei::initializeUiScaling(QApplication& application)
{
    application.setProperty("legacyUiScaling", legacyScaling);
    application.setProperty("uiScaleStartupMode", startupMode);
    application.setProperty("uiScaleStartupSource", startupSource);
    if(!legacyScaling) application.setProperty("enduranceUiScalePercent", 100);
#ifdef Q_OS_WIN
    if(!legacyScaling)
    {
        // Qt 5.12's Windows theme derives point sizes from native metrics and
        // the global factor separately. Read the Windows message font at 96
        // DPI so manual fractional overrides cannot scale its size twice.
        QLibrary user32(QStringLiteral("user32"));
        using ParametersForDpi = BOOL (WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
        auto forDpi = reinterpret_cast<ParametersForDpi>(user32.resolve("SystemParametersInfoForDpi"));
        // The retained MinGW headers omit Vista's trailing border field.
        // Reserve it even with those headers when querying a modern Windows API.
        struct MetricsBuffer { NONCLIENTMETRICSW value; int trailingBorder; } buffer{};
        auto& metrics = buffer.value;
        const UINT metricsSize = UINT(offsetof(NONCLIENTMETRICSW, lfMessageFont) + sizeof(LOGFONTW) + sizeof(int));
        metrics.cbSize = metricsSize;
        qreal metricsDpi = 96;
        bool valid = forDpi && forDpi(SPI_GETNONCLIENTMETRICS, metricsSize, &buffer, 0, 96);
        application.setProperty("uiScaleFontSource", valid ? "SystemParametersInfoForDpi(96)" : "system-metrics");
        if(!valid)
        {
            valid = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metricsSize, &buffer, 0);
            using SystemDpi = UINT (WINAPI*)();
            auto systemDpi = reinterpret_cast<SystemDpi>(user32.resolve("GetDpiForSystem"));
            if(systemDpi) metricsDpi = qMax(96u, systemDpi());
            else if(HDC dc = GetDC(nullptr))
            {
                metricsDpi = qMax(96, GetDeviceCaps(dc, LOGPIXELSY));
                ReleaseDC(nullptr, dc);
            }
        }
        if(valid)
        {
            QFont font = application.font();
            font.setFamily(QString::fromWCharArray(metrics.lfMessageFont.lfFaceName));
            font.setPixelSize(qMax(1, qRound(qAbs(metrics.lfMessageFont.lfHeight) * 96.0 / metricsDpi)));
            application.setFont(font);
            application.setProperty("uiScaleSystemFontPixels", font.pixelSize());
        }
    }
#endif
    QJsonArray monitors;
    for(const auto& monitor : startupMonitors)
        monitors.append(QJsonObject{{"name", monitor.name}, {"dpi", monitor.dpi}, {"primary", monitor.primary}});
    application.setProperty("uiScaleMonitors", monitors);
}
