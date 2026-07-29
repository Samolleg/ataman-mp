#pragma once

#include <QHeaderView>

namespace ei {

inline void configureValidationTreeHeader(QHeaderView* header)
{
    if(!header) return;
    header->setStretchLastSection(false);
    for(int column = 0; column < header->count(); ++column)
        header->setSectionResizeMode(column, QHeaderView::Interactive);

    const int initialWidths[] = {90, 220, 460, 420};
    for(int column = 0; column < header->count() && column < 4; ++column)
        header->resizeSection(column, initialWidths[column]);
}

} // namespace ei
