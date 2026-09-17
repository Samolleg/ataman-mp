#pragma once

#include <QLineEdit>
#include <QLocale>
#include <cmath>

// Text preserves every finite JSON double, including values beyond spin-box
// ranges. Validation happens on acceptance, never by clamping on construction.
class CWorldCoordinateEdit : public QLineEdit
{
public:
    explicit CWorldCoordinateEdit(double coordinate, QWidget* parent = nullptr)
        : QLineEdit(QString::number(coordinate, 'g', 17), parent) {}

    bool coordinate(double* result) const
    {
        bool ok = false;
        QLocale locale = QLocale::c();
        locale.setNumberOptions(QLocale::RejectGroupSeparator);
        const double value = locale.toDouble(text().trimmed(), &ok);
        if(!ok || !std::isfinite(value)) return false;
        *result = value;
        return true;
    }
};
