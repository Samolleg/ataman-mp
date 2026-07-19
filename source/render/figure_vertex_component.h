#ifndef FIGURE_VERTEX_COMPONENT_H
#define FIGURE_VERTEX_COMPONENT_H

#include "types.h"

namespace ei {

// FIG8 stores each vertex component as normal, vertex, texture-coordinate.
// Keep this decoding in one place: interchanging the first two indices can
// produce valid-looking triangles that use unrelated morph vertices.
inline SVertexData decodeFigureVertexComponent(const US3& component,
                                                const QVector<QVector3D>& vertices,
                                                const QVector<QVector4D>& normals,
                                                const QVector<QVector2D>& textureCoordinates)
{
    QVector3D position = vertices.at(component.y);
    QVector4D normal = normals.at(component.x);
    QVector2D textureCoordinate = textureCoordinates.at(component.z);
    return SVertexData(position, normal, textureCoordinate);
}

inline int figureSourceVertexIndex(const US3& component)
{
    return int(component.y);
}

} // namespace ei

#endif // FIGURE_VERTEX_COMPONENT_H
