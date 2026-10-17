#pragma once
#include <QFileInfo>
QString canvasNavigationMarkerPath(const QFileInfo& mpr);
bool canvasNavigationIsStale(const QFileInfo& mpr);
