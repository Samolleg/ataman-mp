#ifndef ENDURANCE_LOCALIZATION_H
#define ENDURANCE_LOCALIZATION_H

#include <QString>

class QApplication;

namespace ei
{
QString persistedApplicationLanguage(
    const QString& optionsPath = QStringLiteral("endurance-options.json"));
bool installApplicationTranslator(QApplication& application,
                                  const QString& language = QString());
}

#endif
