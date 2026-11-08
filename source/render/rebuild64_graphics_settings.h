#pragma once
#include <QDialog>
#include <QVariantMap>
#include <QVector>
namespace rebuild64graphics {
struct Option { const char *key,*label; int group,type; QVariant initial; double minimum,maximum,step; bool enabled; };
const QVector<Option>& options();
QString filePath();
QVariantMap defaults();
QVariantMap load();
QByteArray encode(const QVariantMap& values);
bool save(const QVariantMap& values,QString* error);
bool needsRestart(const QVariantMap& before,const QVariantMap& after);
QString pipelineDirectory(const QString& cacheRoot,const QString& dllPath);
}
class CRebuild64GraphicsDialog: public QDialog {
    Q_OBJECT
public:
    explicit CRebuild64GraphicsDialog(QWidget* parent=nullptr);
signals:
    void graphicsApplied(const QByteArray& ini,bool restart);
private:
    QVariantMap m_applied;
    QMap<QString,QWidget*> m_controls;
    void display(const QVariantMap& values);
    QVariantMap values() const;
};
