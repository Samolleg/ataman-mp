#include "root_bon_check.h"
#include "figure.h"
#include "resourcemanager.h"
#include "res_file.h"
#include <QFile>
#include <QTextStream>
#include <QQuaternion>
namespace {
bool close(const QVector3D& a,const QVector3D& b){return (a-b).length()<.00002f;}
QByteArray triangle(){
 QByteArray bytes;QDataStream s(&bytes,QIODevice::WriteOnly);s.setByteOrder(QDataStream::LittleEndian);s.setFloatingPointPrecision(QDataStream::SinglePrecision);
 s<<quint32(0x38474946)<<quint32(1)<<quint32(1)<<quint32(3)<<quint32(3)<<quint32(3)<<quint32(0)<<quint32(0)<<quint32(0)<<quint32(0);
 for(int b=0;b<3;++b)for(int m=0;m<8;++m)s<<0.f<<0.f<<0.f;
 for(int m=0;m<8;++m)s<<1.f;
 const QVector<QVector3D> vertices{{0,0,0},{1,0,0},{0,1,0},{0,0,0}};
 for(int axis=0;axis<3;++axis)for(int m=0;m<8;++m)for(const auto& v:vertices)s<<float(v[axis]+((m&4)&&(axis==2)?1.f:0.f));
 for(int axis=0;axis<4;++axis)for(int lane=0;lane<4;++lane)s<<(axis==2?1.f:0.f);
 for(int uv=0;uv<3;++uv)s<<0.f<<0.f;
 s<<quint16(0)<<quint16(1)<<quint16(2);
 for(int v=0;v<3;++v)s<<quint16(v)<<quint16(0)<<quint16(v);
 return bytes;
}
bool load(ei::CFigure& f,const QByteArray& bytes){QDataStream s(bytes);s.setByteOrder(QDataStream::LittleEndian);s.setFloatingPointPrecision(QDataStream::SinglePrecision);return f.readData(s);}
}
int runRootBonCheck(){
 QTextStream out(stdout);int failures=0;
 auto check=[&](bool ok,const QString& label){out<<(ok?"PASS: ":"FAIL: ")<<label<<"\n";out.flush();if(!ok)++failures;};
 ei::CFigure root,child,grandchild;root.setName("root");child.setName("child");grandchild.setName("grandchild");
 if(!load(root,triangle())||!load(child,triangle())||!load(grandchild,triangle()))return 2;
 QVector<QVector3D> r(8,QVector3D(100,200,300)),c(8,QVector3D(2,3,4)),g(8,QVector3D(5,6,7));
 root.setOffset(r);child.setOffset(c);grandchild.setOffset(g);root.addChild(&child);child.addChild(&grandchild);root.applyAssemblyOffset();
 for(float height:{1.f,5.f,10.f}){
  QVector3D morph(1,1,height);QStringList filter;QVector<CPart*> parts;root.getVertexData(parts,morph,filter,false);
  check(parts.size()==3,"three-part hierarchy retained");
  if(parts.size()==3){
   check(close(parts[0]->vertData()[0].position,QVector3D(0,0,height)),"root extrapolation without BON Z="+QString::number(height));
   check(close(parts[1]->vertData()[0].position,QVector3D(2,3,height+4)),"child BON retained");
   check(close(parts[2]->vertData()[0].position,QVector3D(7,9,height+11)),"grandchild BON accumulation retained");
   check(close(parts[0]->restOffset(),QVector3D())&&close(parts[1]->restOffset(),c[0]),"animation rest offsets match geometry");
   check(close(parts[0]->authoredNavigationMinimum(),QVector3D())&&close(parts[1]->authoredNavigationMinimum(),c[0]),"navigation bounds match placement");
   QMap<QString,QString> parents;QMap<QString,QVector3D> offsets;root.getAssemblyHierarchy(parents,offsets,morph);
   check(close(offsets["root"],QVector3D())&&close(offsets["grandchild"],c[0]+g[0]),"full skeleton matches rendered parts");
  }qDeleteAll(parts);
 }
 const QString path=qEnvironmentVariable("EI_ROOT_BON_ARCHIVE");
 if(!path.isEmpty()){
  QFile file(path);if(!file.open(QIODevice::ReadOnly))return 2;
  CResFile archive(file.readAll());const auto files=archive.bufferOfFiles();
  CResFile mod(files.value("uhwdg03r.mod")),bon(files.value("uhwdg03r.bon"));const auto figs=mod.bufferOfFiles(),bones=bon.bufferOfFiles();
  ei::CFigure raw;if(!load(raw,figs.value("uhwdg03r_root")))return 2;
  for(float height:{1.f,5.f,10.f}){
   QVector3D morph(1,1,height);QStringList filter;QVector<CPart*> rawParts;raw.getVertexData(rawParts,morph,filter,false);
   const auto prepared=CObjectList::prepareStaticAssembly(files,"uhwdg03r",morph,filter);bool equal=rawParts.size()==1&&prepared.size()==1;
   if(equal){equal=rawParts[0]->vertData().size()==prepared[0].vertices.size();for(int v=0;equal&&v<prepared[0].vertices.size();++v)equal=close(rawParts[0]->vertData()[v].position,prepared[0].vertices[v].position);}
   check(equal,"actual dagger loader equals FIG without translation Z="+QString::number(height));qDeleteAll(rawParts);
  }
  QDataStream s(bones.value("uhwdg03r_root"));s.setByteOrder(QDataStream::LittleEndian);s.setFloatingPointPrecision(QDataStream::SinglePrecision);
  QVector<QVector3D> points;for(int i=0;i<8;++i){QVector3D v;s>>v;points.append(v);}const auto offset=points[3]+10*(points[7]-points[3]);
  check(close(offset,{-0.4178133011f,-0.2297830209f,-0.0367177390f}),"actual BON agrees with retail report");
  const QVector<QQuaternion> rotations{{.9907478094f,0,0,.1357155889f},{.9871362448f,0,0,.1598813534f},{.9908658862f,0,0,.1348509938f}};
  const QVector<QVector3D> expected{{.340629f,.333677f,.036718f},{.323922f,.349918f,.036718f},{.341211f,.333082f,.036718f}};
  for(int i=0;i<3;++i)check(close(rotations[i].rotatedVector(-offset),expected[i]),"world correction of user dagger "+QString::number(i));
 }
 return failures?1:0;
}
