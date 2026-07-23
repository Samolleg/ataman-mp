#ifndef RENDER_MATERIAL_H
#define RENDER_MATERIAL_H

#include <QImage>

enum class ERenderMaterialClass
{
    Opaque = 0,
    Cutout = 1,
    Transparent = 2,
    Conservative = 3
};

ERenderMaterialClass classifyTextureAlpha(const QImage& image);

#endif // RENDER_MATERIAL_H
