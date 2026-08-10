#include "localization.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTranslator>

namespace ei
{
namespace
{
// Qt asks the application translator for these QPlatformTheme strings when it
// labels standard dialog buttons. Keeping them in our catalog avoids shipping
// a second, version-coupled qtbase_ru.qm solely for Yes/No/Cancel buttons.
const char* const kQtStandardButtonSources[] = {
    QT_TRANSLATE_NOOP("QPlatformTheme", "OK"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Save"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Save All"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Open"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "&Yes"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Yes to &All"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "&No"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "N&o to All"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Abort"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Retry"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Ignore"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Close"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Discard"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Help"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Apply"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Reset"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Restore Defaults")
};
}

QString persistedApplicationLanguage(const QString& optionsPath)
{
    QFile options(optionsPath);
    if(!options.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("en");

    const QJsonDocument document = QJsonDocument::fromJson(options.readAll());
    const QString language = document.object().value(QStringLiteral("General"))
        .toObject().value(QStringLiteral("language")).toString().toLower();
    return language == QStringLiteral("ru") ? language : QStringLiteral("en");
}

bool installApplicationTranslator(QApplication& application, const QString& requestedLanguage)
{
    Q_UNUSED(kQtStandardButtonSources);
    const QString language = requestedLanguage.isEmpty()
        ? persistedApplicationLanguage() : requestedLanguage.toLower();
    if(language != QStringLiteral("ru"))
        return false;

    // Process lifetime matches QApplication; keeping the translator installed
    // makes Qt's normal source-text fallback safe for incomplete catalogs.
    static QTranslator russianTranslator;
    if(!russianTranslator.load(QStringLiteral(":/i18n/ei_mapper_ru.qm")))
        return false;
    return application.installTranslator(&russianTranslator);
}
}
