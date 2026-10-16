#ifndef TERRAIN_BASE_HD_NATIVE_H
#define TERRAIN_BASE_HD_NATIVE_H
#include "mmp_codec.h"
#include <QtEndian>
#include <cstring>

// Copy/nearest-upscale DXT indices, retaining the original 565 palette exactly.
// Re-encoding decoded SD colors cannot provide this guarantee.
inline QByteArray baseHdNativeBlocks(const QByteArray& page,int local,int outputSize=256){
    if(page.size()<76||local<0||local>=64)return {};
    const int width=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(page.constData()+4));
    const int size=width/8;if((size!=128&&size!=256)||outputSize%size)return {};
    if(page.size()<76+width*width/2)return {};
    const int scale=outputSize/size,blocks=outputSize/4;
    QByteArray output(outputSize*outputSize/2,'\0');
    for(int by=0;by<blocks;++by)for(int bx=0;bx<blocks;++bx){
        const int sx=bx*4/scale,sy=by*4/scale;
        const int offset=76+(((7-local/8)*size/4+sy/4)*(width/4)+(local%8)*size/4+sx/4)*8;
        const auto* source=reinterpret_cast<const uchar*>(page.constData()+offset);
        char* target=output.data()+(by*blocks+bx)*8;std::memcpy(target,source,4);
        const quint32 indices=qFromLittleEndian<quint32>(source+4);quint32 expanded=0;
        for(int y=0;y<4;++y)for(int x=0;x<4;++x){
            const int index=((sy+y/scale)%4)*4+(sx+x/scale)%4;
            expanded|=((indices>>(2*index))&3u)<<(2*(y*4+x));
        }
        qToLittleEndian<quint32>(expanded,reinterpret_cast<uchar*>(target+4));
    }
    return output;
}
inline bool baseHdWriteBlocks(QByteArray* page,int local,const QByteArray& tile){
    if(!page||page->size()<76||local<0||local>=64)return false;
    const int width=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(page->constData()+4));
    const int size=width/8,blocks=size/4;
    if(size<=0||tile.size()!=size*size/2||page->size()<76+width*width/2)return false;
    for(int row=0;row<blocks;++row){const int offset=76+(((7-local/8)*blocks+row)*(width/4)+(local%8)*blocks)*8;
        std::memcpy(page->data()+offset,tile.constData()+row*blocks*8,blocks*8);}
    return true;
}
inline QImage baseHdWithOriginalRim(const QImage& hd,const QImage& original,
                                      const QByteArray& originalBlocks,QByteArray* resultBlocks,QString* error){
    const QImage old=original.scaled(256,256,Qt::IgnoreAspectRatio,Qt::FastTransformation).convertToFormat(QImage::Format_RGBA8888);
    QImage hybrid=hd.convertToFormat(QImage::Format_RGBA8888);
    if(hybrid.size()!=QSize(256,256)||originalBlocks.size()!=32768)return {};
    // Keep the UV boundary (16px) plus filtering support. Feather only inside
    // the tile, never into a neighbor or a transition.
    for(int y=0;y<256;++y)for(int x=0;x<256;++x){
        const int distance=qMin(qMin(x,255-x),qMin(y,255-y));if(distance>=48)continue;
        const double weight=qBound(0.0,(distance-24)/24.0,1.0);
        const auto a=old.pixelColor(x,y),b=hybrid.pixelColor(x,y);
        hybrid.setPixelColor(x,y,QColor(qRound(a.red()*(1-weight)+b.red()*weight),qRound(a.green()*(1-weight)+b.green()*weight),qRound(a.blue()*(1-weight)+b.blue()*weight),qRound(a.alpha()*(1-weight)+b.alpha()*weight)));
    }
    auto encoded=ei::encodeMmpDxt1(hybrid,false,error);if(encoded.size()<76+32768)return {};
    QByteArray blocks=encoded.mid(76,32768);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)if(x<6||y<6||x>=58||y>=58)
        std::memcpy(blocks.data()+(y*64+x)*8,originalBlocks.constData()+(y*64+x)*8,8);
    QImage decoded;if(!ei::decodeDxtImage(blocks,256,256,ei::DxtFormat::Dxt1,&decoded,error))return {};
    *resultBlocks=blocks;return decoded.convertToFormat(QImage::Format_RGBA8888);
}
#endif
