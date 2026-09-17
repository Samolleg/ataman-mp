#include <QtTest>
#include "neighbor_render_cache.h"
#include "prepared_static_visual.h"
class NeighborPerformanceTests : public QObject
{
    Q_OBJECT
private slots:
    void groupsCoverBoundariesAndLargeObjects()
    {
        QCOMPARE(neighborCell(QVector3D(-0.1f,32,0)),qMakePair(-1,1));
        SNeighborSpatialGroup group;
        group.append(7,QVector3D(31,0,0),10);
        group.append(8,QVector3D(1,0,0),2);
        QCOMPARE(group.minimum,QVector3D(-1,-10,-10));
        QCOMPARE(group.maximum,QVector3D(41,10,10));
        QVERIFY(group.withinDistance(QVector3D(42,0,0),1));
        QVERIFY(!group.withinDistance(QVector3D(43,0,0),1));
        QMatrix4x4 matrix; matrix.ortho(39,45,-2,2,-20,20);
        QVERIFY(CViewFrustum(matrix).intersectsAabb(group.minimum,group.maximum));
    }
    void distanceLodHasHysteresis()
    {
        QVERIFY(!neighborDistantLod(130,20,false));
        QVERIFY(neighborDistantLod(150,20,false));
        QVERIFY(neighborDistantLod(130,45,true));
        QVERIFY(!neighborDistantLod(100,20,true));
        QVERIFY(!neighborDistantLod(300,100,true));
    }
    void invisibleGroupsAvoidObjectTraversal()
    {
        QMap<QPair<int,int>,SNeighborSpatialGroup> groups;
        for(int i=0;i<10000;++i)
        {
            const QVector3D p(float(i%100)*4,float(i/100)*4,0);
            groups[neighborCell(p)].append(i,p,1);
        }
        QMatrix4x4 clip; clip.ortho(-16,16,-16,16,-100,100);
        CViewFrustum frustum(clip);
        int candidates=0;
        for(const auto& group:groups)
            if(frustum.intersectsAabb(group.minimum,group.maximum)
                || group.withinDistance(QVector3D(),32)) candidates+=group.objects.size();
        QVERIFY(candidates<500);
        qInfo()<<"Spatial candidates:"<<candidates<<"of 10000 objects in"<<groups.size()<<"groups";
    }
    void simplifiedMeshRetainsSurfaceAndSource()
    {
        QVector<SVertexData> source;
        const auto vertex=[](int x,int y) {
            SVertexData v; v.position=QVector3D(float(x)/64,float(y)/64,0);
            v.normal=QVector3D(0,0,1); v.texCoord=QVector2D(float(x)/64,float(y)/64); return v;
        };
        for(int y=0;y<64;++y) for(int x=0;x<64;++x)
            source<<vertex(x,y)<<vertex(x+1,y)<<vertex(x+1,y+1)
                  <<vertex(x,y)<<vertex(x+1,y+1)<<vertex(x,y+1);
        const auto original=source;
        const auto reduced=simplifyStaticMesh(source);
        QVERIFY(reduced.size()<source.size()/2);
        double area=0;
        for(int i=0;i<reduced.size();i+=3)
        {
            const auto cross=QVector3D::crossProduct(reduced[i+1].position-reduced[i].position,reduced[i+2].position-reduced[i].position);
            QVERIFY(cross.z()>0); area+=cross.z()/2;
        }
        QVERIFY(qAbs(area-1)<0.02);
        for(int i=0;i<source.size();++i) QCOMPARE(source[i].position,original[i].position);
        qInfo()<<"LOD triangles:"<<source.size()/3<<"->"<<reduced.size()/3;
    }
};
QTEST_APPLESS_MAIN(NeighborPerformanceTests)
#include "neighbor_performance_tests.moc"
