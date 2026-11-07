#pragma once
#include "rebuild64_abi.h"
#include <QImage>
#include <QSize>
#include <QVector3D>
#include <QVector>
class CView; class CRebuild64Viewport;
CRebuild64Viewport* nativeOutputViewport(CView* view);
QImage nativeOutput(CView* view,int mode,const QSize& size,const EirCamera& camera,QString* error,
                   const QString& model={},const QString& texture={},const QVector3D& shape={},const QVector<float>& preview={},const QVector<quint64>& objectKeys={});
QImage nativeTopdown(CView* view,const QSize& size,bool objects,QString* error);
QImage nativeCartographic(CView* view,int pass,const QSize& size,const QVector<quint64>& objectKeys,QString* error);
