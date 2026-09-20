#pragma once
#include "map_table_document.h"
#include <QVector3D>
class QWidget;

// Uses the current profile's authored global FIG8 surface, in its own units.
bool chooseTravelGlobalPosition(QWidget* parent, const QVector<SMapTableZone>& zones,
    const QString& allod, const QString& selectedZone, QVector3D* position,
    bool hasPosition, QString* error);
bool travelGlobalPosition(const SMapTableZone& zone, QVector3D* position);

bool travelGlobalSurfaceCheck();
