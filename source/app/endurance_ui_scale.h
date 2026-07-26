#ifndef ENDURANCE_UI_SCALE_H
#define ENDURANCE_UI_SCALE_H

#include <QString>

namespace ei {

int normalizedUiScalePercent(int percent);
int persistedUiScalePercent(
    const QString& optionsPath = QStringLiteral("endurance-options.json"));
void applyUiScalePercent(int percent);
double uiScaleFactor();

}

#endif // ENDURANCE_UI_SCALE_H
