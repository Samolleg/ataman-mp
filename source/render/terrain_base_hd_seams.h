#ifndef TERRAIN_BASE_HD_SEAMS_H
#define TERRAIN_BASE_HD_SEAMS_H
#include "terrain_image_sizes.h"
#include "mpr_document.h"
#include <QMap>
#include <tuple>
#include <cmath>

// Compare the actual bilinear UV boundary, not the unused texture gutter.
// Edge energy, peak discontinuity and channel bias must not grow. Sampling includes every
// texel interpolation breakpoint (and midpoint) at the common GPU resolution.
class CBaseHdSeams {
public:
    CBaseHdSeams(const QVector<QImage>& before,const QVector<QImage>& after)
        : m_before(before),m_after(after),m_size(qMax(terrainMaximumTileSize(before),terrainMaximumTileSize(after))) {}
    bool safe(quint16 oldA,quint16 oldB,quint16 newA,quint16 newB,int side) {
        const auto key=std::make_tuple(oldA,oldB,newA,newB,side);
        auto found=m_pairs.constFind(key);if(found!=m_pairs.cend())return found.value();
        const auto a=edge(false,oldA,side),b=edge(false,oldB,(side+2)%4);
        const auto c=edge(true,newA,side),d=edge(true,newB,(side+2)%4);
        bool ok=!a.isEmpty()&&a.size()==b.size()&&a.size()==c.size()&&a.size()==d.size();
        double oldEnergy=0,newEnergy=0,oldPeak=0,newPeak=0;
        double oldBias[4]{},newBias[4]{};
        for(int i=0;ok&&i<a.size();++i){
            const double oldDelta=a[i]-b[i],newDelta=c[i]-d[i];
            oldEnergy+=oldDelta*oldDelta;newEnergy+=newDelta*newDelta;
            oldPeak=qMax(oldPeak,std::abs(oldDelta));newPeak=qMax(newPeak,std::abs(newDelta));
            oldBias[i%4]+=oldDelta;newBias[i%4]+=newDelta;
        }
        ok=ok&&newEnergy<=oldEnergy+1e-7&&newPeak<=oldPeak+1e-7;
        for(int channel=0;ok&&channel<4;++channel)
            ok=std::abs(newBias[channel])<=std::abs(oldBias[channel])+1e-7;
        m_pairs.insert(key,ok);return ok;
    }
    bool boundarySafe(quint16 before,quint16 after,int side) {
        const auto a=edge(false,before,side),b=edge(true,after,side);
        if(a.isEmpty()||a.size()!=b.size())return false;
        for(int i=0;i<a.size();++i)if(std::abs(a[i]-b[i])>1e-7)return false;
        return true;
    }
private:
    QVector<double> edge(bool after,quint16 packed,int side){
        const int key=(after?1<<18:0)|(int(packed)<<2)|side;
        auto found=m_edges.constFind(key);if(found!=m_edges.cend())return found.value();
        const auto tile=SMprPackedTile::decode(packed);const auto& images=after?m_after:m_before;
        if(m_size<=0||tile.atlasSlot()>=images.size())return {};
        QImage image=images[tile.atlasSlot()];if(image.isNull())return {};
        if(image.width()!=m_size)image=image.scaled(m_size,m_size,Qt::IgnoreAspectRatio,Qt::FastTransformation);
        QVector<double> result;const int steps=m_size*7/4;
        for(int s=0;s<=steps;++s){
            const double t=1.0/16+s/double(2*m_size);
            const double u=side==1?15.0/16:side==3?1.0/16:t;
            const double v=side==0?1.0/16:side==2?15.0/16:t;
            double x=u,y=1-v;
            switch(tile.rotation){case 1:x=1-v;y=1-u;break;case 2:x=1-u;y=v;break;case 3:x=v;y=u;break;}
            x=qBound(0.0,x*m_size-0.5,double(m_size-1));y=qBound(0.0,y*m_size-0.5,double(m_size-1));
            const int ix=int(x),iy=int(y);const double dx=x-ix,dy=y-iy;double values[4]{};
            for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx){
                const auto color=image.pixelColor(qMin(ix+xx,m_size-1),qMin(iy+yy,m_size-1));
                const double w=(xx?dx:1-dx)*(yy?dy:1-dy);
                values[0]+=w*color.red();values[1]+=w*color.green();values[2]+=w*color.blue();values[3]+=w*color.alpha();
            }
            for(double value:values)result.append(value);
        }
        m_edges.insert(key,result);return result;
    }
    QVector<QImage> m_before,m_after;int m_size;
    QMap<int,QVector<double>> m_edges;
    QMap<std::tuple<quint16,quint16,quint16,quint16,int>,bool> m_pairs;
};
#endif
