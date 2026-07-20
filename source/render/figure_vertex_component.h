#ifndef FIGURE_VERTEX_COMPONENT_H
#define FIGURE_VERTEX_COMPONENT_H

#include "types.h"

namespace ei {

// Corpus bounds establish FIG8 vertex components as vertex, normal,
// texture-coordinate. In particular, JMV stbuho7 has 244 vertices while its
// second field reaches 809 within an 812-normal array.
inline bool decodeFigureVertexComponent(const US3& component,
                                        const QVector<QVector3D>& vertices,
                                        const QVector<QVector4D>& normals,
                                        const QVector<QVector2D>& textureCoordinates,
                                        SVertexData* decoded)
{
    if(!decoded || component.x >= vertices.size() || component.y >= normals.size()
            || component.z >= textureCoordinates.size())
        return false;
    QVector3D position = vertices.at(component.x);
    QVector4D normal = normals.at(component.y);
    QVector2D textureCoordinate = textureCoordinates.at(component.z);
    *decoded = SVertexData(position, normal, textureCoordinate);
    return true;
}

inline int figureSourceVertexIndex(const US3& component)
{
    return int(component.x);
}

} // namespace ei

#endif // FIGURE_VERTEX_COMPONENT_H
