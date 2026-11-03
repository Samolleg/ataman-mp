// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kyr4l (icon selection/background removal approach)
// Copyright (C) 2026 Ataman contributors
#include "quest_map_document.h"
#include "mmp_inspector.h"
#include <QtWidgets>
#include <algorithm>
#include <cmath>

namespace questmap {
questpixels::Image pixels(const QImage& input) {
    QImage im=input.convertToFormat(QImage::Format_RGBA8888);
    questpixels::Image out; out.width=im.width();out.height=im.height();
    for(int y=0;y<im.height();++y) out.rgba.insert(out.rgba.end(),im.constScanLine(y),im.constScanLine(y)+im.width()*4);
    return out;
}
QImage image(const questpixels::Image& p) {
    if(!p.width||!p.height) return {};
    return QImage(p.rgba.data(),int(p.width),int(p.height),int(p.width)*4,QImage::Format_RGBA8888).copy();
}
QImage cutIcon(const QImage& source,const QRect& rect) {
    if(!source.rect().contains(rect)||rect.isEmpty()) return {};
    QImage out=source.copy(rect).convertToFormat(QImage::Format_ARGB32);
    std::vector<int> rim[3];
    for(int y=0;y<out.height();++y) for(int x=0;x<out.width();++x)
        if(!x||!y||x==out.width()-1||y==out.height()-1) {
            auto p=out.pixel(x,y);rim[0].push_back(qRed(p));rim[1].push_back(qGreen(p));rim[2].push_back(qBlue(p));
        }
    int bg[3];for(int c=0;c<3;++c) {std::sort(rim[c].begin(),rim[c].end());bg[c]=rim[c][rim[c].size()/2];}
    for(int y=0;y<out.height();++y) for(int x=0;x<out.width();++x) {
        auto p=out.pixel(x,y);const int r=qRed(p)-bg[0],g=qGreen(p)-bg[1],b=qBlue(p)-bg[2];
        const int alpha=qBound(0,int((std::sqrt(double(r*r+g*g+b*b))-18.0)/52.0*255.0+0.5),255);
        // Pixel art: preserve source RGB; remove paper, without filtering.
        out.setPixel(x,y,qRgba(qRed(p),qGreen(p),qBlue(p),qAlpha(p)?alpha:0));
    }
    return out;
}
namespace {
QImage procedural(Kind kind) {
    QImage im(17,19,QImage::Format_ARGB32);im.fill(Qt::transparent);
    QPainter p(&im);p.setRenderHint(QPainter::Antialiasing,false);
    p.setPen(QPen(QColor(60,35,12),1));p.setBrush(QColor(185,136,60));
    switch(kind) {
    case Kind::House: p.drawRect(3,8,11,9);p.drawPolygon(QPolygon({{1,8},{8,1},{16,8}}));p.fillRect(7,12,3,5,QColor(65,40,15));break;
    case Kind::GoblinHouse:case Kind::GoblinHouses:p.setBrush(QColor(87,107,43));p.drawEllipse(2,5,13,12);p.drawLine(3,6,8,1);p.drawLine(8,1,14,6);p.drawRect(7,11,3,6);break;
    case Kind::Tent:case Kind::Shop:p.drawPolygon(QPolygon({{1,17},{8,2},{16,17}}));p.drawLine(8,2,8,17);if(kind==Kind::Shop){p.setBrush(QColor(121,51,22));p.drawRect(2,9,13,3);}break;
    case Kind::Tower:p.drawRect(4,4,9,13);p.drawRect(3,1,3,4);p.drawRect(10,1,3,4);p.drawLine(7,9,10,9);break;
    case Kind::Tree:p.setBrush(QColor(80,95,32));p.drawPolygon(QPolygon({{1,13},{8,1},{16,13}}));p.drawLine(8,13,8,18);break;
    case Kind::Exit:p.setPen(QPen(QColor(125,25,8),2));p.drawLine(2,10,14,10);p.drawLine(10,5,15,10);p.drawLine(10,15,15,10);break;
    case Kind::Wall:p.drawRect(1,7,15,7);for(int x=2;x<16;x+=4)p.drawLine(x,8,x,13);break;
    default:p.setBrush(kind==Kind::BridgeWood?QColor(139,24,0):QColor(110,110,100));p.drawRect(1,7,15,5);break;
    }
    return im;
}
}
QStringList loadRuntimeIcons(const TextureLoader& loader,QString* cacheDirectory) {
    QStringList notes;std::vector<NamedIcon> icons;
    for(int k=0;k<kKindCount;++k) {
        auto kind=static_cast<Kind>(k);const auto pic=pixels(procedural(kind));
        icons.push_back({std::string("ink-")+std::to_string(k),pic});
    }
    const QString cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/quest-map/icons-v1";
    // Never create derived game assets in the application installation.
    const bool cacheAllowed=!cache.isEmpty()&&!QDir::cleanPath(cache).startsWith(QDir::cleanPath(QCoreApplication::applicationDirPath())+"/");
    if(cacheDirectory)*cacheDirectory=cacheAllowed?cache:QString();
    QFile table(":/quest-map/quest-icons.tsv");table.open(QIODevice::ReadOnly);table.readLine();
    QHash<QString,QImage> sources;QHash<QString,QByteArray> hashes;int extracted=0;
    while(!table.atEnd()) {
        auto fields=QString::fromUtf8(table.readLine()).trimmed().split('\t');if(fields.size()<6)continue;
        QString member=fields[3];member.replace(QRegularExpression("\\.dds$",QRegularExpression::CaseInsensitiveOption),".mmp");
        if(!sources.contains(member)) {
            const auto bytes=loader(member);auto info=ei::inspectMmp(bytes);
            sources.insert(member,info.valid?info.baseImage:QImage());
            hashes.insert(member,QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
        }
        const QImage source=sources.value(member);if(source.isNull())continue;
        const QRect rect(fields[5].toInt(),fields[4].toInt(),fields[1].toInt(),fields[2].toInt());
        // The metadata refers to 256px originals. Do not crop unrelated HD art
        // with stock coordinates just because it reuses a member name.
        if(source.size()!=QSize(256,256)||!source.rect().contains(rect))continue;
        const auto key=hashes[member]+"-"+fields.mid(0,6).join("-").toUtf8()+".png";
        const QString path=cache+"/"+QString::fromLatin1(key);
        QImage crop;if(cacheAllowed)crop.load(path);
        if(crop.size()!=rect.size()) {
            crop=cutIcon(source,rect);
            if(cacheAllowed&&!crop.isNull()&&QDir().mkpath(cache)) {
                QSaveFile saved(path);if(saved.open(QIODevice::WriteOnly)&&crop.save(&saved,"PNG"))saved.commit();
            }
        }
        if(crop.isNull())continue;
        icons.push_back({fields[0].toStdString(),pixels(crop)});++extracted;
    }
    // Prefer available game variants for each category; only a completely
    // missing category needs a procedural generator replacement.
    for(int k=0;k<kKindCount;++k) {
        const auto& names=IconsFor(static_cast<Kind>(k));bool available=false;
        for(const auto& name:names)available|=std::any_of(icons.begin(),icons.end(),[&](const NamedIcon& i){return i.name==name;});
        if(!available&&!names.empty())icons.push_back({names.front(),pixels(procedural(static_cast<Kind>(k)))});
    }
    SetIcons(std::move(icons));
    notes << (extracted?QObject::tr("Extracted %1 icons from your configured game textures. Missing variants use procedural ink icons.").arg(extracted)
                       :QObject::tr("Original quest textures were not found at stock resolution. Using procedural ink icons; configure game resources to extract originals."));
    return notes;
}
QByteArray encode5551(const QImage& input,bool opaque) {
    if(input.isNull())return {};
    QImage im=input.convertToFormat(QImage::Format_ARGB32);QByteArray bytes;QDataStream s(&bytes,QIODevice::WriteOnly);s.setByteOrder(QDataStream::LittleEndian);
    s<<quint32(0x00504d4d)<<quint32(im.width())<<quint32(im.height())<<quint32(1)<<quint32(0x5551)
     <<quint32(16)<<quint32(0x8000)<<quint32(15)<<quint32(1)
     <<quint32(0x7c00)<<quint32(10)<<quint32(5)<<quint32(0x3e0)<<quint32(5)<<quint32(5)
     <<quint32(0x1f)<<quint32(0)<<quint32(5)<<quint32(0);
    for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x) {
        auto p=im.pixel(x,y);if(qAlpha(p)<128)p=0;
        s<<quint16(((opaque||qAlpha(p)>=128)?0x8000:0)|((qRed(p)*31+127)/255<<10)|((qGreen(p)*31+127)/255<<5)|((qBlue(p)*31+127)/255));
    }
    return bytes;
}
QByteArray encodeDds(const QImage& input) {
    if(input.isNull())return {};
    QImage im=input.convertToFormat(QImage::Format_ARGB32);QByteArray bytes;QDataStream s(&bytes,QIODevice::WriteOnly);s.setByteOrder(QDataStream::LittleEndian);
    s<<quint32(0x20534444)<<quint32(124)<<quint32(0x100f)<<quint32(im.height())<<quint32(im.width())<<quint32(im.width()*4)<<quint32(0)<<quint32(0);
    for(int i=0;i<11;++i)s<<quint32(0);
    s<<quint32(32)<<quint32(0x41)<<quint32(0)<<quint32(32)<<quint32(0xff0000)<<quint32(0xff00)<<quint32(0xff)<<quint32(0xff000000)
     <<quint32(0x1000)<<quint32(0)<<quint32(0)<<quint32(0)<<quint32(0);
    for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x)s<<quint32(im.pixel(x,y));
    return bytes;
}
class StateCommand:public QUndoCommand {
public:
    StateCommand(Document* d,const State& next,const QString& text):QUndoCommand(text),doc(d),before(d->m_state),after(next){}
    void undo() override {doc->m_state=before;}
    void redo() override {doc->m_state=after;}
    Document* doc;State before,after;
};
Document::Document():m_icons(Icons()){m_history.setUndoLimit(32);}
QImage Document::composed() const {
    auto p=pixels(m_state.paper);
    for(const auto& stamp:m_state.stamps)for(const auto& icon:m_icons)if(icon.name==stamp.name){Composite(p,icon.image,stamp.x,stamp.y,stamp.scale);break;}
    return image(p);
}
void Document::replace(const State& s,const QString& text){m_history.push(new StateCommand(this,s,text));}
bool Document::add(const QString& name,QPoint pos,int scale) {
    if(m_state.paper.isNull()||scale<1||scale>8||std::none_of(m_icons.begin(),m_icons.end(),[&](const NamedIcon& i){return i.name==name.toStdString();}))return false;
    auto next=m_state;next.stamps.push_back({name.toStdString(),pos.x(),pos.y(),scale});replace(next,QObject::tr("Add stamp"));return true;
}
bool Document::move(int i,QPoint pos){if(i<0||i>=int(m_state.stamps.size()))return false;auto n=m_state;n.stamps[i].x=pos.x();n.stamps[i].y=pos.y();replace(n,QObject::tr("Move stamp"));return true;}
bool Document::scale(int i,int scale){if(i<0||i>=int(m_state.stamps.size())||scale<1||scale>8)return false;auto n=m_state;n.stamps[i].scale=scale;replace(n,QObject::tr("Scale stamp"));return true;}
bool Document::remove(int i){if(i<0||i>=int(m_state.stamps.size()))return false;auto n=m_state;n.stamps.erase(n.stamps.begin()+i);replace(n,QObject::tr("Delete stamp"));return true;}
void Document::bake(){if(m_state.stamps.empty())return;State next;next.paper=composed();replace(next,QObject::tr("Bake stamps"));}
}
