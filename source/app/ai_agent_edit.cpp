#include "ai_agent_edit.h"
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "settings.h"
#include "mob.h"
#include "node.h"
#include "new_terrain_canvas.h"
#include "resource_name.h"
#include "mmp_codec.h"
#include "terrain_save_flow_policy.h"
#include "resourcemanager.h"
#include "terrain_preview_context.h"
#include "terrain_mob_height_state.h"
#include "natural_terrain.h"
#include "ai_graph_preview_invalidation.h"
#include "res_archive_index.h"
#include <QtWidgets>
#include <cmath>
#include <algorithm>

namespace {
QString idFor(CView* v) { return CWorldTerrainSession::key(CLandscape::getInstance()->filePath().absoluteFilePath()); }
QRectF bounds(const QJsonObject& a,QString* error)
{
    const auto shape=a["shape"].toString();QRectF b;
    if(shape=="line") {
        if(!a.contains("width")||a["points"].toArray().size()<2)*error="Line requires points and width in metres";
        else {const auto points=a["points"].toArray();double loX=32768,loY=32768,hiX=-32768,hiY=-32768;
            for(const auto& p:points){auto v=p.toArray();loX=qMin(loX,v[0].toDouble());hiX=qMax(hiX,v[0].toDouble());loY=qMin(loY,v[1].toDouble());hiY=qMax(hiY,v[1].toDouble());}
            const auto r=a["width"].toDouble()/2;b=QRectF(QPointF(loX-r,loY-r),QPointF(hiX+r,hiY+r));}
    } else if(!a.contains("x")||!a.contains("y"))*error="Area requires x and y";
    else if(shape=="circle") {
        if(!a.contains("radius"))*error="Circle requires radius";
        else {double r=a["radius"].toDouble();b=QRectF(a["x"].toDouble()-r,a["y"].toDouble()-r,2*r,2*r);}
    } else if(shape=="rectangle") {
        if(!a.contains("width")||!a.contains("height"))*error="Rectangle requires width and height";
        else b=QRectF(a["x"].toDouble(),a["y"].toDouble(),a["width"].toDouble(),a["height"].toDouble());
    } else *error="Unknown area shape";
    if(b.width()>2048||b.height()>2048)*error="Area bounds limit 2048 metres per side";
    if(a.contains("mask")&&(a["mask"].toArray().size()!=a["maskWidth"].toInt()*a["maskHeight"].toInt()||!a.contains("maskWidth")||!a.contains("maskHeight")))
        *error="Mask requires exact maskWidth * maskHeight values, at most 4096";
    return b;
}
double interpolate(const QJsonArray& values,int w,int h,double u,double v)
{
    const double x=qBound(0.0,u,1.0)*(w-1),y=qBound(0.0,v,1.0)*(h-1);
    const int x0=int(x),y0=int(y),x1=qMin(w-1,x0+1),y1=qMin(h-1,y0+1);
    const double dx=x-x0,dy=y-y0;
    return (1-dy)*((1-dx)*values[y0*w+x0].toDouble()+dx*values[y0*w+x1].toDouble())
        +dy*((1-dx)*values[y1*w+x0].toDouble()+dx*values[y1*w+x1].toDouble());
}
double weight(const QJsonObject& a,const QRectF& b,double x,double y)
{
    if(!b.adjusted(-1e-7,-1e-7,1e-7,1e-7).contains(x,y))return 0;
    double margin=0,radius=0;
    if(a["shape"]=="circle") {radius=a["radius"].toDouble();margin=radius-std::hypot(x-a["x"].toDouble(),y-a["y"].toDouble());}
    else if(a["shape"]=="rectangle") {radius=qMin(b.width(),b.height())/2;margin=qMin(qMin(x-b.left(),b.right()-x),qMin(y-b.top(),b.bottom()-y));}
    else {radius=a["width"].toDouble()/2;double distance=1e20;const auto points=a["points"].toArray();
        for(int i=1;i<points.size();++i){const auto p=points[i-1].toArray(),q=points[i].toArray();
            QPointF start(p[0].toDouble(),p[1].toDouble()),d(q[0].toDouble()-start.x(),q[1].toDouble()-start.y());
            const double length=d.x()*d.x()+d.y()*d.y();const double t=length>0?qBound(0.0,((x-start.x())*d.x()+(y-start.y())*d.y())/length,1.0):0;
            distance=qMin(distance,std::hypot(x-start.x()-t*d.x(),y-start.y()-t*d.y()));}
        margin=radius-distance;}
    if(margin<0)return 0;double result=1;const double feather=a["falloff"].toDouble(0.35);
    if(feather>0){result=qBound(0.0,margin/(radius*feather),1.0);result=result*result*(3-2*result);}
    if(a.contains("mask"))result*=interpolate(a["mask"].toArray(),a["maskWidth"].toInt(),a["maskHeight"].toInt(),(x-b.x())/b.width(),(y-b.y())/b.height());
    return result;
}
double z(const CTerrainEditModel& m,int x,int y) { return m.height(qBound(0,x,m.vertexCountX()-1),qBound(0,y,m.vertexCountY()-1))*m.maxZ()/65535.0; }
QJsonObject planImage(const ai::EditPlan& plan,const QRectF& b)
{
    // Explicit top-down plan, including the unchanged context. Yellow is the
    // requested footprint, not a claim that the live map has already changed.
    if(plan.maps.isEmpty())return {};
    const auto& m=plan.maps.first();const auto& terrain=m.state.terrain;
    QImage image(384,384,QImage::Format_RGB32);
    for(int py=0;py<384;++py)for(int px=0;px<384;++px) {
        const double x=double(px)*(terrain.vertexCountX()-1)/383,y=double(383-py)*(terrain.vertexCountY()-1)/383;
        const int shade=qBound(25,int(50+z(terrain,int(x),int(y))*3),210);
        QColor color(shade/2,shade,shade/2);
        if(weight(plan.args["area"].toObject(),b,x,y)>0)color=QColor(qMin(255,shade+55),qMin(255,shade+40),35);
        image.setPixelColor(px,py,color);
    }
    QPainter p(&image);p.setPen(Qt::white);p.drawText(8,20,QStringLiteral("PREVIEW / ")+plan.method);p.end();
    QByteArray png;QBuffer buf(&png);buf.open(QIODevice::WriteOnly);image.save(&buf,"PNG");
    return {{"mimeType","image/png"},{"data",QString::fromLatin1(png.toBase64())},{"width",384},{"height",384},{"kind","planned footprint; not applied"}};
}
}

ai::EditPlan ai::prepareEdit(EditPlan plan,const Cancellation& cancel)
{
    const auto fail=[&](QString error){plan.error=error;plan.stroke={};plan.newMap.clear();return plan;};
    try {
        if(cancel->load())return fail("Cancelled; live map unchanged");
        if(CAiEditAdapter::lifecycle(plan.method))return CAiEditAdapter::prepareLifecycle(plan,cancel);
        if(plan.method=="create_map") {
            plan=CAiEditAdapter::prepareLifecycle(plan,cancel);
            if(!plan.error.isEmpty())return plan;
        }
        if(!CAiEditAdapter::hydrate(&plan,cancel))return fail(plan.error);
        if(plan.method=="create_map") {
            const int initialMaterial=plan.args["material"].toInt();
            if(initialMaterial>0) {
                const auto hasUniform=[&](const EditMap& map) {
                    STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=initialMaterial;bool found=false;
                    const auto packed=map.state.atlas->catalog().selectForPaint(pure,0,0,0,&found);
                    return found&&SMprPackedTile::decode(packed).atlasSlot()<int(map.baseline.mapInfo().textureCount)*64;
                };
                if(!hasUniform(plan.maps[0])) {
                    if(plan.args.contains("template"))return fail("Initial material has no authored uniform tile in the explicit template; omit material and paint after creation");
                    // The palette source owns a real authoring template too.
                    // Select it only inside the already authorized profile roots;
                    // New MPR213 still generates and commits the complete pair.
                    QStringList names;
                    for(const auto& family:plan.sources.families)if(family.id==initialMaterial)
                        for(const auto& source:family.sources+family.thumbnailSources){auto ns=source.mprNamespace;ns.remove(QRegularExpression("(_[0-9a-f]{12})+$",QRegularExpression::CaseInsensitiveOption));if(!names.contains(ns))names.append(ns);}
                    bool found=false;
                    for(const auto& ns:names) {
                        if(found)break;
                        for(const auto& root:plan.sources.mapRoots) {
                            const QFileInfo file(QDir(root).filePath(ns+".mpr"));
                            const auto canonical=file.canonicalFilePath(),base=QFileInfo(root).canonicalFilePath();
                            if(canonical.isEmpty()||base.isEmpty()||!canonical.startsWith(base+"/",Qt::CaseInsensitive)||file.size()>MaxMapBytes)continue;
                            EditPlan sourcePlan;sourcePlan.sources=plan.sources;EditMap source;source.id=CWorldTerrainSession::key(canonical);source.path=canonical;sourcePlan.maps.append(source);
                            if(!CAiEditAdapter::hydrate(&sourcePlan,cancel)||!hasUniform(sourcePlan.maps[0]))continue;
                            plan.maps=sourcePlan.maps;plan.templatePath=canonical;
                            plan.fileHashes[canonical]=QCryptographicHash::hash(plan.maps[0].baseline.originalBytes(),QCryptographicHash::Sha256);found=true;break;
                        }
                    }
                    if(!found)return fail("The selected palette material has no available authored MPR template inside the active profile roots");
                }
            }
            SMprCreationRequest request;request.innerNamespace=QFileInfo(plan.destination).completeBaseName();
            request.sectorCountX=plan.args["sectorsX"].toInt();request.sectorCountY=plan.args["sectorsY"].toInt();
            request.maxZ=plan.args["maxZ"].toDouble(plan.maps[0].baseline.mapInfo().maxZ);
            if(plan.args["height"].toDouble()>request.maxZ)return fail("Initial height exceeds maxZ");
            request.flatRawHeight=quint16(qRound(plan.args["height"].toDouble()*65535.0/request.maxZ));
            const auto material=plan.args["material"].toInt();
            if(material>0){STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=material;bool found=false;
                request.initialPackedTile=plan.maps[0].state.atlas->catalog().selectForPaint(pure,0,0,0,&found);
                if(!found||SMprPackedTile::decode(request.initialPackedTile).atlasSlot()>=int(plan.maps[0].baseline.mapInfo().textureCount)*64)
                    return fail("Initial material must have an authored uniform tile in the current template; paint other palette materials after creation");}
            CValidationReport report;
            if(!plan.maps[0].baseline.buildNewCanvas(request,&plan.newMap,&report)||report.hasErrors())return fail(report.toText());
            plan.summary={{"name",request.innerNamespace},{"sectorsX",int(request.sectorCountX)},{"sectorsY",int(request.sectorCountY)}};
            return cancel->load()?fail("Cancelled; no file created"):plan;
        }
        QString error;const auto area=plan.args["area"].toObject();const auto b=bounds(area,&error);if(!error.isEmpty())return fail(error);
        const auto action=plan.args["action"].toString();const auto grid=plan.args["grid"].toObject();
        if(action=="heightmap"&&(grid["values"].toArray().size()!=grid["width"].toInt()*grid["height"].toInt()||grid.isEmpty()))return fail("Heightmap requires width * height world-height values, up to 64x64");
        if((action=="raise"||action=="lower"||action=="noise"||action=="hills")&&!plan.args.contains("amount"))return fail("Sculpt action requires amount in metres");
        if(action=="flatten"&&!plan.args.contains("height"))return fail("Flatten requires height in metres");
        const quint32 seed=quint32(plan.args["seed"].toInt());const double strength=plan.args["strength"].toDouble(1);
        const auto layer=plan.args["layer"]=="overlay"?ETerrainTileLayer::Overlay:ETerrainTileLayer::Land;
        QElapsedTimer computation;computation.start();
        int changed=0;QMap<QString,STerrainTileCommand> joinAreas;
        const QPointF activeOrigin=plan.maps.first().origin;
        const bool naturalHeight=plan.method=="generate_terrain"||plan.method=="erode"||plan.method=="carve_river";
        natural::Grid generated;
        if(naturalHeight) {
            QRectF extent;
            for(const auto& map:plan.maps){QRectF r(map.origin-activeOrigin,QSizeF(map.state.terrain.vertexCountX()-1,map.state.terrain.vertexCountY()-1));extent=extent.isNull()?r:extent.united(r);}
            generated.origin={std::floor(extent.left()),std::floor(extent.top())};
            generated.width=int(std::ceil(extent.right())-generated.origin.x())+1;
            generated.height=int(std::ceil(extent.bottom())-generated.origin.y())+1;
            if(qint64(generated.width)*generated.height>1100000)return fail("Natural terrain working grid exceeds 1100000 samples; reduce participating map span");
            generated.heights.resize(generated.width*generated.height);generated.wetness.fill(0,generated.heights.size());
            for(int y=0;y<generated.height;++y){if(cancel->load())return fail("Cancelled; live map unchanged");for(int x=0;x<generated.width;++x){
                const auto p=generated.origin+QPointF(x,y);double nearest=1e30,value=0;
                for(const auto& map:plan.maps){const auto q=p-(map.origin-activeOrigin);const auto& m=map.state.terrain;
                    const double cx=qBound(0.0,q.x(),double(m.vertexCountX()-1)),cy=qBound(0.0,q.y(),double(m.vertexCountY()-1));
                    const double distance=std::hypot(q.x()-cx,q.y()-cy);if(distance<nearest){nearest=distance;value=z(m,qRound(cx),qRound(cy));}}
                generated.heights[y*generated.width+x]=value;
            }}
            const auto cancelled=[&](){return cancel->load();};
            if(plan.method=="generate_terrain")natural::generate(generated,plan.args["preset"].toString(),plan.args["parameters"].toObject(),seed,cancelled);
            else if(plan.method=="erode")natural::erode(generated,plan.args["parameters"].toObject(),cancelled);
            else natural::river(generated,plan.args,seed,cancelled);
        }
        if(plan.method=="sculpt"||naturalHeight) {
            QVector<SWorldTerrainTarget> targets;
            for(const auto& m:plan.maps){SWorldTerrainTarget t;t.id=m.id;t.origin=m.origin-activeOrigin;t.model=&m.state.terrain;t.editable=true;targets.append(t);}
            STerrainBrush brush;brush.centerX=b.center().x();brush.centerY=b.center().y();brush.radius=std::hypot(b.width(),b.height())/2+1;brush.falloff=ETerrainBrushFalloff::Constant;
            bool outside=false;
            const auto influence=[&](double x,double y){if(cancel->load())throw std::runtime_error("Cancelled; live map unchanged");return weight(area,b,x,y);};
            const auto transform=[&](double x,double y,double before) {
                double target=before,amount=plan.args["amount"].toDouble();
                if(naturalHeight)target=generated.sample(x,y);
                else if(action=="raise")target+=amount;
                else if(action=="lower")target-=amount;
                else if(action=="flatten")target=plan.args["height"].toDouble();
                else if(action=="noise"||action=="hills") {
                    const double phase=(seed%100003)*0.019;
                    const double wave=(std::sin(x*0.13+phase)*std::cos(y*0.11-phase)+0.5*std::sin((x+y)*0.071+phase*2))/1.5;
                    target+=amount*(action=="hills"?(wave+1)/2:wave);
                } else if(action=="heightmap")target=interpolate(grid["values"].toArray(),grid["width"].toInt(),grid["height"].toInt(),(x-b.x())/b.width(),(y-b.y())/b.height());
                outside|=target<0;
                if(naturalHeight)for(const auto& map:plan.maps){const auto p=QPointF(x,y)-(map.origin-activeOrigin);
                    if(p.x()>=-0.51&&p.y()>=-0.51&&p.x()<=map.state.terrain.vertexCountX()-0.49&&p.y()<=map.state.terrain.vertexCountY()-0.49)outside|=target>map.state.terrain.maxZ();}
                return target;
            };
            const auto mode=action=="smooth"&&!naturalHeight?EWorldSculptMode::Smooth:EWorldSculptMode::Flatten;
            plan.stroke=makeWorldTerrainStamp(targets,brush,mode,0,0,strength,seed,influence,
                action=="smooth"&&!naturalHeight?std::function<double(double,double,double)>():transform);
            if(outside)return fail("Height outside 0..maxZ; adjust amount or map scale");
            if(!plan.stroke.error.isEmpty())return fail(plan.stroke.error);
            for(const auto& command:plan.stroke.maps)changed+=command.deltas.size();
        }
        for(auto& map:plan.maps) {
            if(cancel->load())return fail("Cancelled; live map unchanged");
            const QPointF offset=map.origin-activeOrigin;auto& model=map.state.terrain;CValidationReport report;
            if(plan.method=="paint"||plan.method=="auto_paint") {
                const bool automatic=plan.method=="auto_paint";
                const auto requestedMaterial=plan.args["material"].toInt();auto atlas=map.state.atlas->createStagingCopy();
                auto terrain=model;atlas->setDistributionSeed(seed);
                if(!terrain.initializeVirtualMaterialField(layer,atlas->catalog(),&report,ETerrainMaterialFieldRecovery::RecoverDeterministically))return fail(report.toText());
                natural::Grid drainage;QVector<double> waterDistance;QVector<quint8> rockMask,beachMask,sandyExposure;double rockCut=32;
                if(automatic){drainage.width=terrain.vertexCountX();drainage.height=terrain.vertexCountY();drainage.origin=offset;
                    drainage.heights.resize(drainage.width*drainage.height);
                    for(int y=0;y<drainage.height;++y)for(int x=0;x<drainage.width;++x)drainage.heights[y*drainage.width+x]=z(terrain,x,y);
                    natural::erode(drainage,{{"thermalIterations",0},{"hydraulicIterations",12},{"erosion",0}},[&](){return cancel->load();});
                    const int vw=terrain.tileCountX()+1,vh=terrain.tileCountY()+1;waterDistance.fill(100000,vw*vh);QVector<int> queue;
                    const double level=plan.args.value("water").toObject()["level"].toDouble(plan.args["parameters"].toObject()["waterLevel"].toDouble());
                    if(plan.args.value("water").isObject())for(int y=0;y<vh;++y)for(int x=0;x<vw;++x)if(z(terrain,x*2,y*2)<level){waterDistance[y*vw+x]=0;queue.append(y*vw+x);}
                    for(int head=0;head<queue.size();++head){const int i=queue[head];for(int d:{-1,1,-vw,vw}){const int j=i+d;
                        if(j<0||j>=waterDistance.size()||std::abs(j%vw-i%vw)+std::abs(j/vw-i/vw)!=1)continue;
                        if(waterDistance[j]>waterDistance[i]+2){waterDistance[j]=waterDistance[i]+2;queue.append(j);}}}
                    QVector<double> scores,rockScores(vw*vh);const auto parameters=plan.args["parameters"].toObject();
                    for(int y=0;y<vh;++y)for(int x=0;x<vw;++x){const auto p=offset+QPointF(x*2,y*2);if(weight(area,b,p.x(),p.y())<=0)continue;
                        const double height=z(terrain,x*2,y*2),slope=std::atan(std::hypot((z(terrain,x*2+3,y*2)-z(terrain,x*2-3,y*2))/6,(z(terrain,x*2,y*2+3)-z(terrain,x*2,y*2-3))/6))*180/3.141592653589793;
                        rockScores[y*vw+x]=(waterDistance[y*vw+x]<12&&height<level+2)?-1000:slope;}
                    rockScores=natural::blurred(rockScores,vw,vh,1);
                    for(int y=0;y<vh;++y)for(int x=0;x<vw;++x){const auto p=offset+QPointF(x*2,y*2);if(weight(area,b,p.x(),p.y())<=0)continue;
                        rockScores[y*vw+x]+=parameters["boundaryNoise"].toDouble(1.5)*natural::noise(p.x()/22,p.y()/35,seed^1801);
                        scores.append(rockScores[y*vw+x]);}
                    std::sort(scores.begin(),scores.end());
                    if(!scores.isEmpty())rockCut=qMax(parameters["rockSlope"].toDouble(32),scores[qBound(0,int((1-parameters["rockFraction"].toDouble(0.1))*(scores.size()-1)),scores.size()-1)]);
                    rockMask.resize(vw*vh);beachMask.fill(0,vw*vh);sandyExposure.fill(0,vw*vh);
                    for(int y=0;y<vh;++y)for(int x=0;x<vw;++x){const int i=y*vw+x;rockMask[i]=rockScores[i]>=rockCut;
                        if(plan.args["preset"]=="coast"){
                            const double sx=(z(terrain,x*2+6,y*2)-z(terrain,x*2-6,y*2))/12,sy=(z(terrain,x*2,y*2+6)-z(terrain,x*2,y*2-6))/12;
                            const double angle=parameters["beachDirection"].toDouble(180)*3.141592653589793/180;
                            const double facing=(-sx*std::cos(angle)-sy*std::sin(angle))/qMax(0.001,std::hypot(sx,sy));
                            sandyExposure[i]=facing>0.15&&natural::noise((x*2+offset.x())/65,(y*2+offset.y())/80,seed^3391)>-0.15;
                            const double slope=std::atan(std::hypot(sx,sy))*180/3.141592653589793;
                            beachMask[i]=sandyExposure[i]&&waterDistance[i]>0&&waterDistance[i]<parameters["shoreWidth"].toDouble(10)&&z(terrain,x*2,y*2)<level+2.2&&slope<parameters["beachSlope"].toDouble(18);}}
                    natural::coherentMask(rockMask,vw,vh,0,8,false,[&](){return cancel->load();});
                    if(plan.args["preset"]=="coast")natural::coherentMask(beachMask,vw,vh,1,12,true,[&](){return cancel->load();});
                }
                STerrainTileCommand semantic;semantic.description=plan.description;semantic.semanticAudit.valid=true;
                semantic.semanticAudit.layer=layer;semantic.semanticAudit.requestedMaterialId=requestedMaterial;
                const int nx=terrain.tileCountX(),ny=terrain.tileCountY();QSet<int> affected;
                for(int y=0;y<=ny;++y) {
                    if(cancel->load())return fail("Cancelled; live map unchanged");
                    for(int x=0;x<=nx;++x) {
                        const auto p=QPointF(x*2,y*2)+offset;const double w=weight(area,b,p.x(),p.y());if(w<=0)continue;
                        const int vx=qMin(x*2,terrain.vertexCountX()-1),vy=qMin(y*2,terrain.vertexCountY()-1);
                        const double slope=std::atan(std::hypot((z(terrain,vx+1,vy)-z(terrain,vx-1,vy))/2,(z(terrain,vx,vy+1)-z(terrain,vx,vy-1))/2))*180/3.141592653589793;
                        if(slope<plan.args["minimumSlope"].toDouble(0)||slope>plan.args["maximumSlope"].toDouble(90))continue;
                        int material=requestedMaterial;
                        if(automatic){
                            const auto parameters=plan.args["parameters"].toObject();const double height=z(terrain,vx,vy),water=plan.args.value("water").toObject()["level"].toDouble(parameters["waterLevel"].toDouble(6));
                            const double curve=z(terrain,vx+2,vy)+z(terrain,vx-2,vy)+z(terrain,vx,vy+2)+z(terrain,vx,vy-2)-4*height;
                            const double jitter=parameters["boundaryNoise"].toDouble(1.5)*natural::fbm(p.x()/6,p.y()/8,seed^1801,3);
                            const double distance=waterDistance[y*(nx+1)+x];
                            const double wet=qBound(0.0,qMax(1-distance/14,drainage.wetness.value(vy*drainage.width+vx)*0.65)+qMax(0.0,curve)*0.03,1.0);
                            const auto palette=plan.args["palette"].toObject();material=palette["grass"].toInt();
                            const double scale=parameters["patchScale"].toDouble(5);
                            const double patch=natural::fbm((p.x()+height*0.7)/scale,(p.y()-height*0.4)/(scale*1.5),seed^7211,3);
                            if(patch< -0.18)material=palette["dark"].toInt(material);
                            else if(patch>0.23)material=palette["light"].toInt(material);
                            else if(patch>0.09&&patch<(plan.args["preset"]=="mountains"?0.13:0.19))material=palette["forest"].toInt(material);
                            if(wet>0.8&&distance<5&&height>water-0.2&&(plan.args["preset"]!="coast"||drainage.wetness.value(vy*drainage.width+vx)>0.6))material=palette["mud"].toInt(material);
                            if(plan.args["preset"]=="coast"&&height<water){
                                const double seabed=natural::noise(p.x()/38,p.y()/52,seed^7753);
                                material=sandyExposure[y*(nx+1)+x]&&water-height<1.2?palette["underwaterSand"].toInt(material):
                                    seabed>-0.15?palette["underwaterRock"].toInt(material):palette["dark"].toInt(material);
                            }
                            if(plan.args["preset"]=="coast"){
                                if(beachMask[y*(nx+1)+x])material=palette["shore"].toInt(material);
                            }else if(distance>0&&distance<parameters["shoreWidth"].toDouble(1.5)+1+jitter&&height<water+1.8)material=palette["mud"].toInt(material);
                            if(rockMask[y*(nx+1)+x]&&!(distance<12&&height<water+2)){
                                material=palette["rock"].toInt(material);
                                const double angle=parameters["ridgeDirection"].toDouble(90)*3.141592653589793/180;
                                const double variant=natural::noise((p.x()*std::cos(angle)+p.y()*std::sin(angle))/35,(-p.x()*std::sin(angle)+p.y()*std::cos(angle))/12,seed^382);
                                if(variant>0.5)material=palette["rock2"].toInt(material);
                                else if(variant< -0.45)material=palette["rock3"].toInt(material);
                            }
                            // Explicit ordered rules override the derived palette. Coherent
                            // spatial noise perturbs thresholds, never independent tile speckle.
                            for(const auto& entry:plan.args["rules"].toArray()){const auto r=entry.toObject();
                                if(height+jitter>=r["minimumHeight"].toDouble(-10000)&&height+jitter<=r["maximumHeight"].toDouble(10000)
                                   &&slope+jitter>=r["minimumSlope"].toDouble(0)&&slope+jitter<=r["maximumSlope"].toDouble(90)
                                   &&curve>=r["minimumCurvature"].toDouble(-10000)&&curve<=r["maximumCurvature"].toDouble(10000)
                                   &&wet>=r["minimumWetness"].toDouble(0)&&wet<=r["maximumWetness"].toDouble(1))material=r["material"].toInt();}
                            const auto road=plan.args["road"].toObject();if(!road.isEmpty()){
                                QString roadError;const auto roadBounds=bounds(road,&roadError);const double perturb=1+0.14*natural::noise(p.x()/15,p.y()/15,seed^671);
                                auto varied=road;varied["width"]=road["width"].toDouble()*perturb;
                                if((!plan.args.value("water").isObject()||height>water+0.4)&&slope<45&&weight(varied,roadBounds.adjusted(-4,-4,4,4),p.x(),p.y())>0.3)material=palette["road"].toInt(material);}
                            if(material<=0)return fail(QString("Automatic material unavailable at %1,%2: %3").arg(x).arg(y).arg(QString::fromUtf8(QJsonDocument(palette).toJson(QJsonDocument::Compact))));
                        }
                        // Stable dither preserves the mask/falloff without inventing blended material IDs.
                        const quint32 hash=(quint32(qRound(p.x()))*73856093u)^(quint32(qRound(p.y()))*19349663u)^(seed*83492791u);
                        if(double(hash%65536)/65535.0>w)continue;
                        bool supported=false;
                        for(int cy:{y-1,y})for(int cx:{x-1,x})if(cx>=0&&cy>=0&&cx<nx&&cy<ny){bool available=false;auto packed=terrain.packedTile(cx,cy,layer,&available);supported|=available&&packed!=0xffff;}
                        if(!supported)continue;
                        const auto before=terrain.virtualMaterialAt(layer,x,y);
                        if(before==material&&!plan.args["shuffle"].toBool())continue;
                        semantic.semanticAudit.paintedVertices.append(QPoint(x,y));
                        if(before!=material){STerrainTileCommand::SVirtualVertexDelta d;d.globalX=x;d.globalY=y;d.layer=layer;d.before=before;d.after=material;semantic.virtualVertices.append(d);}
                        for(int cy:{y-1,y})for(int cx:{x-1,x})if(cx>=0&&cy>=0&&cx<nx&&cy<ny)affected.insert(cy*nx+cx);
                    }
                }
                if(semantic.semanticAudit.paintedVertices.isEmpty())continue;
                if(!terrain.apply(semantic,true,&report))return fail(report.toText());
                CTerrainVirtualMaterialField field;if(!terrain.copyVirtualMaterialField(layer,&field,false))return fail("Material field unavailable");
                QMap<int,quint64> usage;QSet<int> protectedSlots;
                for(int slot=0;slot<atlas->activeTileCount();++slot)protectedSlots.insert(slot); // Never recycle Undo artwork.
                for(int y=0;y<ny;++y)for(int x=0;x<nx;++x)for(auto l:{ETerrainTileLayer::Land,ETerrainTileLayer::Overlay}){bool available=false;auto packed=terrain.packedTile(x,y,l,&available);if(available&&packed!=0xffff)++usage[SMprPackedTile::decode(packed).atlasSlot()];}
                QList<int> ordered=affected.values();std::sort(ordered.begin(),ordered.end());
                for(int cell:ordered) {
                    if(cancel->load())return fail("Cancelled; live map unchanged");
                    const int x=cell%nx,y=cell/nx;bool found=false;const auto signature=field.cellSignature(x,y,&found);
                    // Each automatic cell has its actual shared-corner signature;
                    // route selection stays in the existing131 resolver.
                    const int material=automatic?signature.northWest:requestedMaterial;
                    if(found&&!realizeAgentPaintSignature(plan.sources,*atlas,terrain,signature,x,y,seed,material,&usage,protectedSlots,&report))return fail(QString("Paint cell %1,%2 signature %3/%4/%5/%6: %7").arg(x).arg(y).arg(signature.northWest).arg(signature.northEast).arg(signature.southEast).arg(signature.southWest).arg(report.toText().right(4000)));
                }
                const auto resolution=atlas->joinAuthoredTransitionEdges(terrain,terrain.makeVirtualStrokeResolution(semantic,atlas->catalog(),seed,&report,true,requestedMaterial),&report);
                if(report.hasErrors()||!terrain.apply(resolution,true,&report))return fail(report.toText());
                if(automatic&&plan.args.value("water").isObject()) {
                    const auto water=plan.args["water"].toObject();const int id=water["material"].toInt(),liquid=water["liquid"].toInt();
                    const double level=water["level"].toDouble();
                    if(liquid>=map.state.materials.size()||level>terrain.maxZ())return fail("Water requires an existing liquid record and level within map maxZ");
                    STerrainTransitionSignature pure;pure.northWest=pure.northEast=pure.southEast=pure.southWest=id;
                    if(!realizeAgentPaintSignature(plan.sources,*atlas,terrain,pure,0,0,seed,id,&usage,protectedSlots,&report))return fail(report.toText());
                    terrain.enableWorkingAtlasSlots(atlas->capacityLimit());terrain.installWorkingAtlasSlotTypes(atlas->activeTileTypes());
                    for(int y=0;y<ny;++y){if(cancel->load())return fail("Cancelled; live map unchanged");for(int x=0;x<nx;++x){
                        const auto p=offset+QPointF(x*2+1,y*2+1);if(weight(area,b,p.x(),p.y())<=0||z(terrain,x*2+1,y*2+1)>=level)continue;
                        bool found=false;const auto packed=SMprPackedTile::decode(atlas->catalog().selectForPaint(pure,x,y,seed,&found));
                        if(!found)return fail("Water uniform tile unavailable");STerrainTileBrush tile;tile.centerX=x;tile.centerY=y;tile.radius=0;
                        const auto activation=terrain.makeOverlayActivation(tile,qint16(liquid),packed.atlasIndex,packed.tileIndex,packed.rotation,&report);
                        if(!terrain.apply(activation,true,&report))return fail(report.toText());
                        STerrainBrush flatten;flatten.centerX=x*2+1;flatten.centerY=y*2+1;flatten.radius=1.5;flatten.falloff=ETerrainBrushFalloff::Constant;
                        const auto surface=terrain.makeOverlayFlatten(flatten,quint16(qRound(level*65535/terrain.maxZ())),1,&report);
                        if(!terrain.apply(surface,true,&report)||report.hasErrors())return fail(report.toText());
                    }}
                }
                SWorldTerrainPaintChange change;change.before=map.state;change.after=map.state;change.after.terrain=terrain;change.after.atlas=atlas;
                plan.stroke.paint[map.id]=change;joinAreas[map.id]=resolution;changed+=affected.size();
            }
        }
        // Same cross-map edge matching as the manual common brush. Packed IDs
        // are local: compare the neighbour's actual pixels and rotation.
        if(plan.method=="paint"||plan.method=="auto_paint")for(int pass=0;pass<4;++pass) {
            bool adjusted=false;
            for(auto& map:plan.maps) {
                if(cancel->load())return fail("Cancelled; live map unchanged");
                if(!plan.stroke.paint.contains(map.id))continue;
                auto& state=plan.stroke.paint[map.id].after;QMap<QPair<int,int>,QPair<QImage,quint8>> boundary;
                for(const auto& cell:joinAreas[map.id].semanticAudit.affectedCells)for(auto d:{QPoint(-1,0),QPoint(1,0),QPoint(0,-1),QPoint(0,1)}) {
                    const auto q=cell+d;if(q.x()>=0&&q.y()>=0&&q.x()<state.terrain.tileCountX()&&q.y()<state.terrain.tileCountY())continue;
                    const QPointF world=map.origin+QPointF(q.x()*2,q.y()*2);
                    for(const auto& neighbor:plan.maps)if(neighbor.id!=map.id) {
                        const auto& other=plan.stroke.paint.contains(neighbor.id)?plan.stroke.paint[neighbor.id].after:neighbor.state;
                        const auto p=(world-neighbor.origin)/2;const int x=qRound(p.x()),y=qRound(p.y());
                        if(qAbs(p.x()-x)>0.001||qAbs(p.y()-y)>0.001||x<0||y<0||x>=other.terrain.tileCountX()||y>=other.terrain.tileCountY())continue;
                        bool available=false;auto packed=SMprPackedTile::decode(other.terrain.packedTile(x,y,layer,&available));
                        if(available&&packed.atlasSlot()<other.atlas->tileImages().size())boundary[{q.x(),q.y()}]={other.atlas->tileImages()[packed.atlasSlot()],packed.rotation};break;
                    }
                }
                if(boundary.isEmpty())continue;CValidationReport report;auto areaCommand=joinAreas[map.id];areaCommand.deltas.clear();
                const auto join=state.atlas->joinAuthoredTransitionEdges(state.terrain,areaCommand,&report,boundary);
                if(report.hasErrors()||!state.terrain.apply(join,true,&report))return fail(report.toText());adjusted|=!join.isEmpty();
            }
            if(!adjusted)break;
        }
        if(plan.method=="auto_paint")for(auto it=plan.stroke.paint.begin();it!=plan.stroke.paint.end();++it){CValidationReport report;
            if(!it.value().after.atlas->canonicalizeChangedTilesForPersistence(&report,[&](){return cancel->load();}))
                return fail(cancel->load()?QString("Cancelled; live map unchanged"):report.toText());}
        if(cancel->load())return fail("Cancelled; live map unchanged");
        plan.summary={{"changed",changed},{"maps",plan.stroke.maps.size()+plan.stroke.paint.size()},{"revision",plan.revision}};
        plan.summary["computeMs"]=int(computation.elapsed());
        if(naturalHeight){auto metrics=natural::metrics(generated);
            if(plan.method=="generate_terrain")metrics["shape"]=natural::shapeMetrics(generated);
            plan.summary["terrainMetrics"]=metrics;}
        plan.summary.insert("preview",planImage(plan,b));return plan;
    } catch(const std::exception& e) {return fail(QString::fromUtf8(e.what()));}
    catch(...) {return fail("Preparation failed; live map unchanged");}
}

class CAiAgentCommand : public QUndoCommand,public IAiGraphPreviewInvalidatingCommand {
public:
    QPointer<MainWindow> window;SWorldTerrainStroke stroke;QString baseline,newPath,previousPath;QStringList previousMobs;
    bool first{true};
    CAiAgentCommand(MainWindow* w,const QString& text):window(w){setText(text);}
    bool invalidatesAiGraphPreview()const override{return true;}
    bool switchMap(const QString& path,const QStringList& mobs) {
        auto* v=window->view();if(!v)return false;
        QScopedValueRollback<QUndoStack*> history(v->m_pUndoStack,nullptr);
        v->unloadMob("");v->unloadLand(false);v->m_worldTerrainSession.clear();v->m_worldTerrainAtlasSources.clear();
        v->prepareDocumentTransition();
        if(path.isEmpty()){window->setWorkspaceIdentity(QString(),QString(),QString(),QString());window->updateDocumentActionAvailability();window->refreshEditorWorld();return true;}
        v->loadLandscape(QFileInfo(path));if(!v->m_pLand->isMprLoad()||!v->ensureTerrainAutoCatalogReady())return false;
        for(const auto& mob:mobs){QFileInfo file(mob);v->loadMob(file);}
        window->updateDocumentActionAvailability();window->refreshMapTestContext(false,false);window->refreshEditorWorld();return true;
    }
    void undo()override{if(!window||!window->view())return;if(!newPath.isEmpty()){switchMap(previousPath,previousMobs);return;}QString error;if(!CAiEditAdapter::replay(window,stroke,false,&error))emit window->view()->updateMsg(error);}
    void redo()override{if(first){first=false;return;}if(!window||!window->view())return;if(!newPath.isEmpty()){switchMap(newPath,{});return;}QString error;if(!CAiEditAdapter::replay(window,stroke,true,&error))emit window->view()->updateMsg(error);}
};

bool CAiEditAdapter::busy(MainWindow* w)
{
    auto* v=w->view();return !v||QApplication::activeModalWidget()||v->m_sculptStrokeActive||v->m_tilePaintStrokeActive
        ||!v->m_operationBackup.isEmpty()||w->m_workspaceLoadInProgress||v->m_terrainAutoCatalogBuilding;
}
QString CAiEditAdapter::revision(MainWindow* w)
{
    auto* v=w->view();if(!v)return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto add=[&](const QByteArray& b){hash.addData(QByteArray::number(b.size())+':');hash.addData(b);};
    const auto epoch=[&](const QString& path){QFileInfo f(path);add(f.absoluteFilePath().toUtf8()+QByteArray::number(f.size())+':'+QByteArray::number(f.lastModified().toMSecsSinceEpoch()));};
    epoch(CLandscape::getInstance()->filePath().absoluteFilePath());
    add(idFor(v).toUtf8());add(ei::ProfileDatabaseContext::instance().current()?ei::ProfileDatabaseContext::instance().current()->profileId.toUtf8():QByteArray());
    add(QByteArray::number(w->m_undoStack->index()));add(QByteArray::number(w->m_undoStack->count()));
    const auto model=[&](const CMprDocument& doc,const CTerrainEditModel& terrain,const QVector<SMprMaterialRecord>& materials) {
        add(doc.originalSha256());add(terrain.stateSha256());
        for(const auto& m:materials){QByteArray b;QDataStream s(&b,QIODevice::WriteOnly);s<<m.type<<m.red<<m.green<<m.blue<<m.opacity<<m.selfIllumination<<m.waveMultiplier<<m.warpSpeed<<m.reserved1<<m.reserved2<<m.reserved3;add(b);}
    };
    model(v->m_mprDocument,v->m_terrainEdit,v->m_mprMaterials);
    add(v->m_terrainAtlasProfile.toBytes());
    if(v->m_workingTerrainAtlas) {
        add(QByteArray::number(v->m_workingTerrainAtlas->catalog().size()));
        for(const auto& image:v->m_workingTerrainAtlas->activeTileImages())add(QByteArray::number(image.cacheKey()));
    }
    for(const auto& doc:v->m_worldTerrainSession.documents){if(CWorldTerrainSession::key(doc->path)==idFor(v))continue;add(doc->path.toUtf8());model(doc->baseline,doc->terrain,doc->materials);}
    for(const auto& mob:v->m_aMob){add(mob->filePath().absoluteFilePath().toUtf8());add(QByteArray::number(mob->isDirty()));for(auto* n:mob->nodes()){auto p=n->position();add(QByteArray::number(n->mapId())+':'+QByteArray::number(p.x(),'g',9)+','+QByteArray::number(p.y(),'g',9)+','+QByteArray::number(p.z(),'g',9));}}
    add(QByteArray::number(v->m_worldTerrainEnabled));add(QByteArray::number(w->hasTravelChanges()));
    auto paths=v->m_worldTerrainEnabledPaths.values();std::sort(paths.begin(),paths.end());for(const auto& path:paths)add(path.toUtf8());
    for(const auto& n:v->m_neighborPlacements){epoch(n.mprPath);add(n.mprPath.toUtf8()+':'+QByteArray::number(n.worldOrigin.x(),'g',16)+','+QByteArray::number(n.worldOrigin.y(),'g',16));}
    for(const auto& path:CTextureList::getInstance()->configuredResourceArchivePaths()){QFileInfo file(path);add(file.absoluteFilePath().toUtf8()+':'+QByteArray::number(file.size())+':'+QByteArray::number(file.lastModified().toMSecsSinceEpoch()));}
    add(CTextureList::getInstance()->terrainAuthoringReferenceSha256());
    add(QByteArray::number(dirty(w)));
    const auto layoutPath=w->m_worldMapLayoutDocument.path();
    add(layoutPath.toUtf8());QFile layout(layoutPath);if(layout.open(QIODevice::ReadOnly))add(layout.readAll());
    return QString::fromLatin1(hash.result().toHex());
}
ai::EditPlan CAiEditAdapter::capture(MainWindow* w,const QString& method,const QJsonObject& args)
{
    if(lifecycle(method)||method=="create_map")return captureLifecycle(w,method,args);
    ai::EditPlan plan;plan.method=method;plan.args=args;plan.description=QString::fromUtf8("ИИ: ")+(method=="sculpt"?QString::fromUtf8("Рельеф / ")+args["action"].toString():method=="paint"?QString::fromUtf8("Покраска"):method=="generate_terrain"?QString::fromUtf8("Природный рельеф / ")+args["preset"].toString():method=="erode"?QString::fromUtf8("Эрозия"):method=="carve_river"?QString::fromUtf8("Русло реки"):method=="auto_paint"?QString::fromUtf8("Автопокраска"):QString::fromUtf8("Новая карта"));
    auto* v=w->view();const auto fail=[&](QString error){plan.error=error;return plan;};
    if(busy(w))return fail("Editor gesture/load/modal interaction in progress; retry after it finishes");
    if(!v->m_terrainEditAvailable||!v->m_pLand->isMprLoad())return fail("Open a terrain map first");
    if(args["revision"].toString()!=revision(w))return fail("Document revision conflict; call get_revision and prepare again");
    if(v->m_terrainAutoCatalogPending||!v->m_workingTerrainAtlas)return fail("Terrain material palette is initializing; retry after it is ready");
    const bool naturalTool=QStringList{"generate_terrain","erode","carve_river","auto_paint"}.contains(method);
    if(naturalTool) {
        if(!args.contains("area"))plan.args["area"]=QJsonObject{{"shape","rectangle"},{"x",0},{"y",0},
            {"width",v->m_terrainEdit.vertexCountX()-1},{"height",v->m_terrainEdit.vertexCountY()-1},{"falloff",0}};
        try {auto parameters=natural::preset(args["preset"].toString("plain"));const auto overrides=args["parameters"].toObject();
            for(auto it=overrides.begin();it!=overrides.end();++it)parameters[it.key()]=it.value();plan.args["parameters"]=parameters;
        }catch(const std::exception& e){return fail(QString::fromUtf8(e.what()));}
        if(method=="auto_paint") {
            auto palette=args["palette"].toObject();
            const auto choose=[&](const QString& role,const QStringList& terms){if(palette.contains(role))return;
                for(const auto& term:terms)for(const auto& family:v->m_terrainGlobalFamilies)
                    if(family.layer==ETerrainAtlasLayer::Land&&family.name.startsWith(term,Qt::CaseInsensitive)){palette[role]=family.id;return;}};
            choose("grass",{"Grass / Common","Grass","трава"});choose("dark",{"Grass / Dark","Grass / Common"});
            choose("light",{"Grass / Fresh","Grass / Sparse light","Grass / Common"});choose("forest",{"Ground / Dead","Grass / Irregular","Grass / Dark"});
            choose("rock",args["preset"]=="coast"?QStringList{"Stone / Light","Stone / Lime","Stone"}:QStringList{"Stone / Lime","Stone","Rock","камень"});
            choose("rock2",{"Stone / Mossy","Stone / Lime","Stone"});choose("rock3",{"Stone / Pink","Stone / Grayish","Stone"});
            choose("shore",{"Sand / Common","Sand / Grass","Sand"});choose("mud",{"Ground / Brown","Ground / Dark brown","Ground / Dirt","Ground"});
            choose("road",{"Ground / Gravel","Gravel","Ground / Dirt","Ground"});
            choose("underwaterRock",{"Rock / Underwater emerald","Ground / Water","Ground"});choose("underwaterSand",{"Sand / Underwater","Sand / Common","Sand"});
            if(!palette.contains("grass")||!palette.contains("rock")||!palette.contains("shore")||!palette.contains("mud"))return fail("Active palette lacks grass/rock/shore/mud; supply explicit palette IDs");
            QJsonArray ids;for(auto it=palette.begin();it!=palette.end();++it)ids.append(it.value());
            for(const auto& rule:args["rules"].toArray())ids.append(rule.toObject()["material"]);
            for(const auto& id:ids){bool known=false;for(const auto& family:v->m_terrainGlobalFamilies)known|=family.id==id.toInt()&&family.layer==ETerrainAtlasLayer::Land;
                if(!known)return fail("auto_paint material must be an active land palette ID");}
            if(args.contains("water")){bool known=false;const int id=args["water"].toObject()["material"].toInt();
                for(const auto& family:v->m_terrainGlobalFamilies)known|=family.id==id&&family.layer==ETerrainAtlasLayer::Overlay;
                if(!known)return fail("Water material must be an active overlay palette ID");}
            plan.args["palette"]=palette;plan.args["shuffle"]=true;
            if(args.contains("road")){QString roadError;bounds(args["road"].toObject(),&roadError);if(!roadError.isEmpty())return fail(roadError);
                if(args["road"].toObject()["shape"]!="line")return fail("Road must use line geometry");}
        }
    }
    QString error;const QRectF area=bounds(plan.args["area"].toObject(),&error);if(!error.isEmpty())return fail(error);
    const auto materialName=args["materialName"].toString();int material=args["material"].toInt();
    if(method=="paint") {
        if(!materialName.isEmpty()) {int matches=0;for(const auto& family:v->m_terrainGlobalFamilies)if(family.name.compare(materialName,Qt::CaseInsensitive)==0){if(material>0&&material!=family.id)return fail("Material ID and name disagree");material=family.id;++matches;}if(matches!=1)return fail("Material name is absent or ambiguous; use material_palette ID");}
        if(material>0){bool known=false;for(const auto& family:v->m_terrainGlobalFamilies)known|=family.id==material;if(!known)return fail("Unknown palette material ID");}
        if(method=="paint"&&material<=0)return fail("paint requires material ID or exact materialName");
        plan.args["material"]=material;
    }
    const auto active=idFor(v);QVector<QPair<QString,QPointF>> targets{{active,v->m_activeMapWorldOrigin}};
    const bool includeNeighbors=args["includeNeighbors"].toBool(!qApp->property("agentHeadless").toBool());
    if(includeNeighbors&&v->m_worldTerrainEnabled&&v->m_hasActiveMapWorldOrigin&&v->m_neighborLayoutStorageWritable) {
        for(const auto& n:v->m_neighborPlacements){const auto id=CWorldTerrainSession::key(n.mprPath);if(id==active||!v->m_worldTerrainEnabledPaths.contains(id))continue;
            if(!QRectF(n.worldOrigin-v->m_activeMapWorldOrigin,n.size).adjusted(-2,-2,2,2).intersects(area))continue;
            bool seen=false;for(const auto& t:targets)seen|=t.first==id;if(!seen)targets.append({id,n.worldOrigin});}
    }
    int sectors=0;
    for(const auto& target:targets) {
        if(plan.maps.size()>=4)return fail("At most four maps per agent operation");
        ai::EditMap map;map.id=target.first;map.path=target.first;map.origin=target.second;
        map.editLease=CProcessEditLock::acquire(map.path,&plan.error);if(!map.editLease)return plan;
        if(target.first==active) {
            map.baseline=v->m_mprDocument;map.state.terrain=v->m_terrainEdit;map.state.materials=v->m_mprMaterials;map.state.atlas=v->m_workingTerrainAtlas->createStagingCopy();
            map.profile=v->m_terrainAtlasProfile;map.archive=v->m_activeTerrainArchivePath;
        } else if(v->m_worldTerrainSession.documents.contains(target.first)) {
            const auto doc=v->m_worldTerrainSession.documents[target.first];map.baseline=doc->baseline;map.state.terrain=doc->terrain;map.state.materials=doc->materials;
            if(doc->atlas)map.state.atlas=doc->atlas->createStagingCopy();
            const auto source=v->m_worldTerrainAtlasSources.value(target.first);map.profile=source.profile;map.archive=source.archive;map.pages=source.pages;
        }
        sectors+=map.baseline.sectors().size();if(sectors>256)return fail("Agent edit budget is 256 sectors across all affected maps; use smaller maps");
        plan.maps.append(map);
    }
    plan.sources.families=v->m_terrainGlobalFamilies;plan.sources.transitions=v->m_terrainGlobalTransitionSources;
    plan.sources.archives=CTextureList::getInstance()->configuredResourceArchivePaths();plan.sources.reference=CTextureList::getInstance()->terrainAuthoringArchivePath();
    plan.sources.mapRoots=w->activeMapRoots().toList();
    plan.sources.referenceHash=CTextureList::getInstance()->terrainAuthoringReferenceSha256();plan.sources.nameSpace=v->m_mprDocument.mapInfo().innerNamespace;
    plan.revision=revision(w);return plan;
}
bool CAiEditAdapter::replay(MainWindow* w,const SWorldTerrainStroke& stroke,bool forward,QString* error)
{
    auto* v=w->view();if(!v||busy(w)){*error="Editor is busy";return false;}
    v->syncWorldTerrainActive();
    if(!v->m_worldTerrainSession.apply(stroke,forward,error))return false;
    const auto active=idFor(v);bool ok=true;
    if(stroke.maps.contains(active))ok=v->applyTerrainHeightPreview(stroke.maps[active],forward);
    for(auto it=stroke.paint.cbegin();it!=stroke.paint.cend();++it)ok=v->previewWorldPaintState(it.key(),forward?&it->before.terrain:&it->after.terrain)&&ok;
    if(!ok){QString rollback;v->m_worldTerrainSession.apply(stroke,!forward,&rollback);
        if(stroke.maps.contains(active))v->applyTerrainHeightPreview(stroke.maps[active],!forward);
        for(const auto& id:stroke.paint.keys())v->previewWorldPaintState(id);
        *error="Preview publication failed; complete operation rolled back";return false;}
    v->applyTerrainMobHeights(stroke.mobHeightState.data(),forward,true);
    v->syncWorldTerrainActive();v->refreshWorldTerrainNeighbors();v->updateWorldTerrainStatus();return true;
}
QJsonObject CAiEditAdapter::apply(MainWindow* w,const ai::EditPlan& plan)
{
    if(busy(w))return ai::error("Editor gesture/load/modal interaction in progress; retry");
    if(revision(w)!=plan.revision)return ai::error("Document revision conflict; candidate discarded, prepare again");
    auto* v=w->view();
    if(lifecycle(plan.method))return applyLifecycle(w,plan);
    if(plan.method=="create_map") {
        QString leaseError;auto destinationLease=CProcessEditLock::acquire(plan.destination,&leaseError);
        if(!destinationLease)return ai::error(leaseError);
        if(QFileInfo::exists(plan.destination))return ai::error("Map already exists; creation refused");
        const QFileInfo parent(QFileInfo(plan.destination).absolutePath());bool inProfile=false;
        for(const auto& root:w->activeMapRoots())inProfile|=QFileInfo(root).canonicalFilePath().compare(parent.canonicalFilePath(),Qt::CaseInsensitive)==0;
        if(!inProfile||parent.isSymLink()||parent.canonicalFilePath().compare(parent.absoluteFilePath(),Qt::CaseInsensitive)!=0)return ai::error("Profile map root changed; creation refused");
        for(auto it=plan.fileHashes.begin();it!=plan.fileHashes.end();++it){QFile f(it.key());QCryptographicHash hash(QCryptographicHash::Sha256);
            if(!f.open(QIODevice::ReadOnly)||!hash.addData(&f)||hash.result()!=it.value())return ai::error("Template changed; prepare again");}
        if(dirty(w))return ai::error("Unsaved changes: save first; creation refused");
        const QString previous=v->m_pLand->isMprLoad()?v->m_pLand->filePath().absoluteFilePath():QString();QStringList mobs;for(auto* mob:v->m_aMob)mobs.append(mob->filePath().absoluteFilePath());
        QString publication;for(const auto& path:plan.sources.archives)if(terrainPublicationIsHDPackArchive(path)){publication=path;break;}
        bool ready=false;emit v->terrainPublicationPreflightRequested(publication,&ready);
        if(!ready)return ai::error("HDPack is not available for normal publication");
        const auto saved=commitNewTerrainCanvas(QFileInfo(plan.templatePath),plan.newMap,QFileInfo(plan.destination),plan.sources.archives,QString(),true);
        emit v->terrainPublicationFinished(publication,saved.success);
        if(!saved.success)return ai::error(saved.error);
        const auto publishedHash=resFileSha256(publication);
        QFile newFile(plan.destination);QByteArray committedMap;if(newFile.open(QIODevice::ReadOnly)){committedMap=newFile.readAll();newFile.close();}
        if(!w->replaceLoadedLandscape(QFileInfo(plan.destination))||!v->ensureTerrainAutoCatalogReady()) {
            // The serializer's cohort is committed, but the document must also
            // be usable. Restore only bytes still owned by this operation.
            bool restored=saved.backups.isEmpty();
            if(!saved.backups.isEmpty()){
                SMapSaveFile resource;resource.path=publication;resource.beforeSha256=publishedHash;
                resource.afterFile=saved.backups.first();resource.afterSha256=resFileSha256(resource.afterFile);
                restored=commitMapSaveBatch({resource}).success;
            }
            QFile current(plan.destination);const bool owned=current.open(QIODevice::ReadOnly)&&current.readAll()==committedMap;current.close();
            if(owned)restored=QFile::remove(plan.destination)&&restored;else restored=false;
            emit v->terrainPublicationFinished(publication,true);
            CAiAgentCommand previousWorkspace(w,QString());const bool workspaceRestored=previousWorkspace.switchMap(previous,mobs);
            return ai::error(restored&&workspaceRestored?"Creation could not open; MPR/atlas and previous workspace rolled back":"Creation could not open; rollback incomplete, retain backups and inspect "+plan.destination);
        }
        auto* command=new CAiAgentCommand(w,plan.description+": "+QFileInfo(plan.destination).completeBaseName());
        command->newPath=plan.destination;command->previousPath=previous;command->previousMobs=mobs;w->m_undoStack->push(command);
        return {{"status","applied"},{"path",plan.destination},{"revision",revision(w)},{"undo",command->text()}};
    }
    if(plan.stroke.isEmpty())return {{"status","unchanged"},{"revision",revision(w)}};
    for(const auto& map:plan.maps) {
        if(!v->m_worldTerrainSession.documents.contains(map.id)) {
            auto doc=QSharedPointer<SWorldTerrainDocument>::create();doc->path=map.path;doc->baseline=map.baseline;
            doc->terrain=map.state.terrain;doc->materials=map.state.materials;doc->atlas=map.state.atlas;
            v->m_worldTerrainSession.documents[map.id]=doc;
        } else {
            auto doc=v->m_worldTerrainSession.documents[map.id];
            if(map.id==idFor(v)){doc->terrain=v->m_terrainEdit;doc->materials=v->m_mprMaterials;}
            if(!doc->atlas)doc->atlas=map.state.atlas;
        }
        SWorldTerrainAtlasSource source;source.profile=map.profile;source.archive=map.archive;source.pages=map.pages;
        v->m_worldTerrainAtlasSources[map.id]=source;
    }
    auto stroke=plan.stroke;const auto active=idFor(v);
    if(stroke.maps.contains(active)){stroke.mobHeightState=v->makeTerrainMobHeightState();v->captureTerrainMobHeights(stroke.maps[active],stroke.mobHeightState.data());}
    QString error;if(!replay(w,stroke,true,&error))return ai::error(error);
    auto* command=new CAiAgentCommand(w,plan.description);command->stroke=stroke;w->m_undoStack->push(command);
    auto result=plan.summary;result.remove("preview");result.insert("status","applied");result.insert("revision",revision(w));result.insert("undo",plan.description);return result;
}
QJsonObject CAiEditAdapter::undo(MainWindow* w)
{
    auto* v=w->view();if(busy(w))return ai::error("Editor is busy");
    const auto* top=w->m_undoStack->index()>0?w->m_undoStack->command(w->m_undoStack->index()-1):nullptr;
    const bool command=dynamic_cast<const CAiAgentCommand*>(top)||lifecycleCommand(top);
    if(!command)return ai::error("The last operation belongs to human/world history; no agent operation undone");
    w->setProperty("aiLifecycleHistoryError",QString());
    const int index=w->m_undoStack->index();w->on_actionUndo_triggered();
    const auto lifecycleError=w->property("aiLifecycleHistoryError").toString();if(!lifecycleError.isEmpty())return ai::error(lifecycleError);
    if(w->m_undoStack->index()!=index-1)return ai::error("Active interaction prevented Undo; no agent command undone");
    return {{"status","undone"},{"revision",revision(w)}};
}
QJsonObject CAiEditAdapter::save(MainWindow* w)
{
    if(busy(w))return ai::error("Editor is busy");
    auto* v=w->view();QScopedValueRollback<bool> permission(v->m_agentSaveAuthorized,true);
    w->on_actionSave_landscape_MPR_triggered();
    if(!v->m_pLand->isDirty()&&!v->hasDirtyWorldTerrain()&&!w->hasTravelChanges())v->saveAllMob();
    if(v->m_pLand->isDirty()||v->hasDirtyWorldTerrain()||w->hasTravelChanges())return ai::error("Normal Save did not complete; changes remain in memory");
    for(auto* mob:v->m_aMob)if(mob->isDirty())return ai::error("Normal MOB save did not complete; changes remain in memory");
    // World publication rebases physical identities. Retire snapshot commands
    // with that same normal save boundary, never replay old slots after save.
    bool agentHistory=false;for(int i=0;i<w->m_undoStack->count();++i)agentHistory|=dynamic_cast<const CAiAgentCommand*>(w->m_undoStack->command(i))!=nullptr||lifecycleCommand(w->m_undoStack->command(i));
    if(agentHistory)w->m_undoStack->clear();
    return {{"status","saved"},{"revision",revision(w)}};
}

bool CAiEditAdapter::hydrate(ai::EditPlan* plan,const ai::Cancellation& cancel)
{
    int sectors=0;
    const auto fail=[&](QString message){plan->error=message;return false;};
    for(auto& map:plan->maps) {
        if(cancel->load())return fail("Cancelled; live map unchanged");
        CValidationReport report;
        if(map.baseline.originalBytes().isEmpty()) {
            if(QFileInfo(map.path).size()>ai::MaxMapBytes)return fail("Map exceeds 64 MiB limit");
            if(!map.baseline.load(QFileInfo(map.path),&report)||!map.state.terrain.load(map.baseline,&report))return fail(report.toText());
            map.state.materials=map.baseline.mapInfo().materials;
        }
        sectors+=map.baseline.sectors().size();if(sectors>256)return fail("Agent edit budget is 256 sectors across all affected maps");
        const auto ns=map.baseline.mapInfo().innerNamespace;
        if(!map.state.atlas) {
            QStringList archives;QString preferred;
            for(const auto& path:plan->sources.archives)if(terrainPublicationIsHDPackArchive(path)){archives.append(path);if(preferred.isEmpty())preferred=path;}
            QFileInfo file(map.path);archives<<file.dir().filePath(file.completeBaseName()+"-atlas.res")<<file.dir().filePath("textures.res");archives+=plan->sources.archives;archives.removeDuplicates();
            const auto embedded=CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(archives,ns,preferred);
            QVector<QImage> images;
            if(embedded.success()){map.archive=embedded.archivePath;map.profile=embedded.profile;images=embedded.decodedAtlasTiles;}
            else {
                const auto names=ei::terrainTextureResourceNames(ns,int(map.baseline.mapInfo().textureCount));QMap<QString,QByteArray> payloads;
                for(const auto& archive:archives) {
                    payloads.clear();if(!CTerrainAtlasArchiveBindingResolver::readArchiveMembers(archive,names,&payloads))continue;
                    bool complete=true;for(const auto& name:names)if(payloads.value(name).isEmpty())complete=false;
                    if(complete){map.archive=archive;break;}payloads.clear();
                }
                if(payloads.isEmpty())return fail("No complete neighbour atlas");
                for(const auto& name:names)for(int slot=0;slot<64;++slot){QImage image;if(!ei::decodeMmpDxt1AtlasTile(payloads[name],slot,&image))return fail("Invalid neighbour atlas tile");images.append(image);}
                const auto hash=CTerrainAtlasProfileInference::pixelSha256(images);QString path;bool legacy=false;
                if(!CTerrainAtlasProfileStore::loadExplicitOverrideForMpr(ns,hash,images.size(),file,&map.profile,&path,&legacy,&report)
                   &&!CTerrainAtlasProfileStore::loadExplicitOverride(ns,hash,images.size(),&map.profile,QString(),&path,&report))map.profile=CTerrainAtlasPresetCatalog::exactProfile(ns,hash,images.size(),&report);
                if(map.profile.slotDefinitions.isEmpty())map.profile=CTerrainAtlasProfileInference::propose(ns,images,map.baseline.mapInfo().tileTypes,{},&report);
            }
            const auto analysis=map.profile.usableAnalysis(&report);if(!analysis.usable)return fail(report.toText());
            auto catalog=CTerrainAtlasPresetCatalog::globalizedCatalog(map.profile,analysis.overlayCatalogWithLandFamilies(),&plan->sources.families);
            if(!plan->sources.reference.isEmpty()){const auto reference=plan->sources.referenceHash;catalog.setPaintReference(reference.isEmpty()?QByteArray(64,'0'):reference,!reference.isEmpty()&&map.profile.paintReferenceSha256==reference);}
            QMap<qint32,ETerrainBlendStyle> styles;QMap<qint32,qint32> types;
            for(const auto& family:plan->sources.families){catalog.declareMaterial(family.id);styles[family.id]=CTerrainVirtualAtlas::blendStyleForMaterialName(family.name);types[family.id]=family.runtimeTerrainType;}
            map.state.atlas.reset(new CTerrainWorkingAtlas(catalog,images,0,kMprPackedTerrainAtlasSlotLimit,{},styles,map.baseline.mapInfo().tileTypes,types));
            if(!map.state.atlas->isValid())return fail(map.state.atlas->error());
            CView::restoreTerrainSemanticFieldFromArchive(map.baseline,map.profile,&map.state.terrain,QFileInfo(map.path),map.archive,false);
        }
        if(map.pages.isEmpty()) {
            const auto names=ei::terrainTextureResourceNames(ns,int(map.baseline.mapInfo().textureCount));QMap<QString,QByteArray> payloads;
            if(!CTerrainAtlasArchiveBindingResolver::readArchiveMembers(map.archive,names,&payloads))return fail("Cannot read complete source pages");
            for(const auto& name:names){if(payloads.value(name).isEmpty())return fail("Source page unavailable");map.pages.append(payloads[name]);}
        }
        map.state.terrain.enableWorkingAtlasSlots(map.state.atlas->capacityLimit());map.state.terrain.installWorkingAtlasSlotTypes(map.state.atlas->activeTileTypes());
    }
    return true;
}
