#include "render_material.h"

ERenderMaterialClass classifyTextureAlpha(const QImage& image)
{
    if(image.isNull())
        return ERenderMaterialClass::Conservative;
    bool hasTransparent = false;
    for(int y = 0; y < image.height(); ++y)
    {
        for(int x = 0; x < image.width(); ++x)
        {
            const int alpha = image.pixelColor(x, y).alpha();
            if(alpha > 0 && alpha < 255)
                return ERenderMaterialClass::Transparent;
            hasTransparent = hasTransparent || alpha == 0;
        }
    }
    return hasTransparent ? ERenderMaterialClass::Cutout : ERenderMaterialClass::Opaque;
}
