#ifndef FIGURE_FORMAT_H
#define FIGURE_FORMAT_H

#include <QtGlobal>

namespace ei {

// FIG1 stores one geometry form; FIG8 stores eight interpolation endpoints.
// BON endpoint counts are independent of the geometry form count.
inline int figureFormCount(quint32 signature)
{
    if(signature == 0x31474946u) return 1;
    if(signature == 0x38474946u) return 8;
    return 0;
}

} // namespace ei

#endif // FIGURE_FORMAT_H
