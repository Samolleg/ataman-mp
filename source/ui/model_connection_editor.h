#ifndef MODEL_CONNECTION_EDITOR_H
#define MODEL_CONNECTION_EDITOR_H
#include "resource_model_preview.h"
#include "technical_model_catalog.h"
#include <QDialog>
#include <functional>
using ModelSceneProvider = std::function<ei::SResourceModelScene(const QString&)>;
class CModelConnectionEditor : public QDialog {
public:
    CModelConnectionEditor(ei::TechnicalModelCatalog* catalog,const QString& model,
        ModelSceneProvider provider,QWidget* parent = nullptr);
    static ModelSceneProvider resourceScenes(const QStringList& paths);
};
#endif
