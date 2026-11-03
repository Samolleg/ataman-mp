#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "resource_profile.h"
#include <QCheckBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QMenu>
#include <QFileDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QUndoCommand>
#include <functional>

namespace {
class CameraUndo final : public QUndoCommand
{
public:
    CameraUndo(QSharedPointer<CCameraDocument> document, const QByteArray& before,
               const QByteArray& after, std::function<void()> refresh)
        : QUndoCommand(QString::fromUtf8("Изменить ключ камеры")), m_document(document),
          m_before(before), m_after(after), m_refresh(refresh) {}
    void undo() override { m_document->restore(m_before); m_refresh(); }
    void redo() override { m_document->restore(m_after); m_refresh(); }
private:
    QSharedPointer<CCameraDocument> m_document;
    QByteArray m_before, m_after;
    std::function<void()> m_refresh;
};
QString namedFile(const QString& directory, const QString& name)
{
    for(const auto& file : QDir(directory).entryInfoList(QDir::Files | QDir::Readable))
        if(file.fileName().compare(name, Qt::CaseInsensitive) == 0) return file.absoluteFilePath();
    return {};
}
}

void MainWindow::createCameraEditor(QMenu* menu)
{
    m_cameraDock = new QDockWidget(tr("Стартовая камера / CAM"), this);
    m_cameraDock->setObjectName("CameraTrackDock");
    auto* body = new QWidget(m_cameraDock);
    auto* layout = new QVBoxLayout(body);
    auto* open = new QPushButton(tr("Открыть .cam…"), body);
    open->setObjectName("cameraOpenFile");
    layout->addWidget(open);
    m_cameraStatus = new QLabel(body);
    m_cameraStatus->setWordWrap(true);
    m_cameraStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_cameraStatus);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    m_cameraKey = new QSpinBox(body);
    m_cameraKey->setObjectName("cameraKey");
    form->addRow(tr("Ключ"), m_cameraKey);
    const QString labels[] = {"X", "Y", "Z", tr("Рыскание, °"), tr("Наклон, °")};
    for(int field = 0; field < 5; ++field) {
        auto* editor = new QDoubleSpinBox(body);
        editor->setObjectName(QString("cameraField%1").arg(field));
        editor->setRange(-1000000, 1000000);
        if(field == 3) editor->setRange(-360, 360);
        if(field == 4) editor->setRange(-90, 90);
        editor->setDecimals(5);
        editor->setSingleStep(field < 3 ? 0.1 : 1.0);
        editor->setKeyboardTracking(false);
        m_cameraFields[field] = editor;
        form->addRow(labels[field], editor);
        connect(editor, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, field](double) { editCameraField(field); });
    }
    m_cameraAllKeys = new QCheckBox(tr("Все ключи неподвижной камеры"), body);
    m_cameraAllKeys->setObjectName("cameraAllKeys");
    layout->addWidget(m_cameraAllKeys);
    m_cameraFromView = new QPushButton(tr("Поставить по текущему виду"), body);
    m_cameraFromView->setObjectName("cameraFromView");
    layout->addWidget(m_cameraFromView);
    m_cameraPreview = new QCheckBox(tr("Вид из выбранной камеры"), body);
    m_cameraPreview->setObjectName("cameraPreview");
    layout->addWidget(m_cameraPreview);
    m_cameraMarkers = new QCheckBox(tr("Путь и точки в 3D"), body);
    m_cameraMarkers->setChecked(true);
    layout->addWidget(m_cameraMarkers);
    auto* note = new QLabel(tr("Координаты локальные для карты. Z абсолютная. Положительный наклон направлен вверх. Ctrl+S сохраняет CAM с резервной копией. Времена и неизвестные поля сохраняются побайтно. Файл может использоваться несколькими сценами."), body);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();
    m_cameraDock->setWidget(body);
    addDockWidget(Qt::RightDockWidgetArea, m_cameraDock);
    m_cameraDock->hide();
    menu->addAction(m_cameraDock->toggleViewAction());
    connect(open, &QPushButton::clicked, this, [this] {
        const QString initial = m_cameraDocument ? m_cameraDocument->path()
            : CLandscape::getInstance()->filePath().absoluteDir().absoluteFilePath("../Camera");
        const QString path = QFileDialog::getOpenFileName(this, tr("Открыть путь камеры"), initial, "Camera (*.cam)");
        if(!path.isEmpty()) openCameraFile(path);
    });
    connect(m_cameraKey, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { refreshCameraEditor(); });
    connect(m_cameraPreview, &QCheckBox::toggled, this, [this](bool) { refreshCameraEditor(); });
    connect(m_cameraMarkers, &QCheckBox::toggled, this, [this](bool) { refreshCameraEditor(); });
    connect(m_cameraDock, &QDockWidget::visibilityChanged, this, [this](bool) { refreshCameraEditor(); });
    connect(m_pView, &CView::loadedMapStateChanged, this, [this] {
        if(!CLandscape::getInstance()->isMprLoad()) {
            m_cameraDocument.clear();
            m_cameraPreview->setChecked(false);
            refreshCameraEditor();
        }
    });
    connect(m_cameraFromView, &QPushButton::clicked, this, [this] {
        if(!m_cameraDocument) return;
        SCameraPose pose;
        if(!m_pView->captureCamPose(&pose)) return;
        const QByteArray before = m_cameraDocument->bytes();
        QString error;
        if(!m_cameraDocument->setPose(m_cameraKey->value()-1, pose, m_cameraAllKeys->isChecked(), &error)) {
            m_cameraStatus->setText(error);
            return;
        }
        const QByteArray after = m_cameraDocument->bytes();
        if(before != after) {
            QPointer<MainWindow> window(this);
            m_undoStack->push(new CameraUndo(m_cameraDocument, before, after, [window] {
                if(window) window->refreshCameraEditor();
            }));
        }
    });
    refreshCameraEditor();
}

bool MainWindow::openCameraFile(const QString& source, const QString& destination)
{
    if(!CLandscape::getInstance()->isMprLoad()) {
        m_cameraStatus->setText(tr("Сначала откройте карту для предпросмотра .cam."));
        return false;
    }
    const QString target = QFileInfo(destination.isEmpty() ? source : destination).absoluteFilePath();
    const QString key = target.toCaseFolded();
    auto document = m_cameraDocuments.value(key);
    QString error;
    if(document) {
        QFile disk(source);
        if(!disk.open(QIODevice::ReadOnly) || disk.readAll() != document->savedBytes()) {
            m_cameraStatus->setText(tr("CAM изменён другим процессом. Загруженная версия и Undo сохранены, запись поверх внешних изменений будет отклонена."));
            return false;
        }
    } else {
        document = QSharedPointer<CCameraDocument>::create();
        if(!document->open(source, target, &error)) { m_cameraStatus->setText(error); return false; }
        m_cameraDocuments.insert(key, document);
    }
    m_cameraDocument = document;
    {
        const QSignalBlocker block(m_cameraKey);
        m_cameraKey->setRange(1, document->count());
        m_cameraKey->setValue(1);
    }
    m_cameraAllKeys->setChecked(document->stationary());
    refreshCameraEditor();
    return true;
}

void MainWindow::refreshCameraContext()
{
    if(!m_cameraDock) return;
    m_cameraDocument.clear();
    m_cameraPreview->setChecked(false);
    auto* landscape = CLandscape::getInstance();
    if(!landscape->isMprLoad()) { refreshCameraEditor(); return; }
    const QFileInfo mpr = landscape->filePath();
    QString name;
    if(mpr.completeBaseName().compare("zonemainmenunew", Qt::CaseInsensitive) == 0) name = "mainmenu.cam";
    else {
        QSet<QString> cameraNames;
        for(const auto& entry : m_mapCatalog.entries)
            if(QFileInfo(entry.mprPath).canonicalFilePath().compare(mpr.canonicalFilePath(), Qt::CaseInsensitive) == 0)
                cameraNames.insert(entry.cameraName.toCaseFolded());
        if(cameraNames.size() > 1) {
            refreshCameraEditor();
            m_cameraStatus->setText(tr("Карту используют зоны с разными камерами. Откройте нужный .cam вручную."));
            return;
        }
        if(!cameraNames.isEmpty()) name = *cameraNames.constBegin();
    }
    if(name.isEmpty()) { refreshCameraEditor(); return; }
    if(!name.endsWith(".cam", Qt::CaseInsensitive)) name += ".cam";
    if(QFileInfo(name).fileName() != name || name.contains('\\') || name.contains('/')) {
        refreshCameraEditor();
        m_cameraStatus->setText(tr("Небезопасное имя камеры в map.txt."));
        return;
    }
    const QDir root(mpr.absoluteDir().absoluteFilePath(".."));
    QString cameraDir;
    for(const auto& dir : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if(dir.fileName().compare("camera", Qt::CaseInsensitive) == 0) cameraDir = dir.absoluteFilePath();
    if(cameraDir.isEmpty()) cameraDir = root.absoluteFilePath("Camera");
    const QString own = namedFile(cameraDir, name);
    if(!own.isEmpty()) { openCameraFile(own); return; }
    const auto profile = activeResourceProfile();
    const QString fallback = namedFile(QDir(resourceGameDataRoot(profile.gameRoot)).filePath("Camera"), name);
    if(!fallback.isEmpty()) { openCameraFile(fallback, QDir(cameraDir).filePath(name)); return; }
    refreshCameraEditor();
    m_cameraStatus->setText(tr("Привязана камера %1, но .cam не найден. Откройте файл вручную.").arg(name));
}

void MainWindow::refreshCameraEditor()
{
    if(!m_cameraDock || !m_pView) return;
    const bool valid = m_cameraDocument && CLandscape::getInstance()->isMprLoad();
    m_cameraKey->setEnabled(valid);
    m_cameraPreview->setEnabled(valid);
    m_cameraFromView->setEnabled(valid && !m_cameraPreview->isChecked());
    for(auto* field : m_cameraFields) field->setEnabled(valid);
    m_cameraAllKeys->setEnabled(valid && m_cameraDocument->stationary());
    if(valid && !m_cameraDocument->stationary()) m_cameraAllKeys->setChecked(false);
    QVector<SCameraPose> poses;
    const int index = m_cameraKey->value()-1;
    if(valid && index >= 0 && index < m_cameraDocument->count()) {
        const auto pose = m_cameraDocument->pose(index);
        const auto angles = pose.angles();
        const double values[] = {pose.position.x(), pose.position.y(), pose.position.z(), angles.x(), angles.y()};
        for(int field = 0; field < 5; ++field) {
            const QSignalBlocker block(m_cameraFields[field]);
            m_cameraFields[field]->setValue(values[field]);
        }
        m_cameraStatus->setText(tr("%1\nКлюч %2 / %3, время %4 (0x%5)%6")
            .arg(m_cameraDocument->path()).arg(index+1).arg(m_cameraDocument->count())
            .arg(m_cameraDocument->timeValue(index), 0, 'f', 6)
            .arg(m_cameraDocument->timeBits(index), 8, 16, QLatin1Char('0'))
            .arg(m_cameraDocument->dirty() ? tr("\nЕсть несохранённые изменения") : QString()));
        if(m_cameraDock->isVisible() && (m_cameraMarkers->isChecked() || m_cameraPreview->isChecked()))
            for(int key = 0; key < m_cameraDocument->count(); ++key) poses.append(m_cameraDocument->pose(key));
    } else {
        m_cameraStatus->setText(tr("У карты нет явной стартовой камеры CAM. Для меню используется Camera/mainmenu.cam, для сцен — #camera из map.txt или файл PlayCamera. Откройте нужный .cam вручную."));
    }
    m_pView->setCameraTrack(poses, index, valid && m_cameraDock->isVisible() && m_cameraPreview->isChecked());
    updateDocumentActionAvailability();
}

void MainWindow::editCameraField(int field)
{
    if(!m_cameraDocument) return;
    SCameraPose pose = m_cameraDocument->pose(m_cameraKey->value()-1);
    if(field < 3) pose.position[field] = float(m_cameraFields[field]->value());
    else {
        QVector3D angles = pose.angles();
        angles[field-3] = float(m_cameraFields[field]->value());
        pose.rotation = SCameraPose::fromAngles(pose.position, angles).rotation;
    }
    const QByteArray before = m_cameraDocument->bytes();
    QString error;
    if(!m_cameraDocument->setPose(m_cameraKey->value()-1, pose, m_cameraAllKeys->isChecked(), &error)) {
        m_cameraStatus->setText(error);
        return;
    }
    if(before != m_cameraDocument->bytes()) {
        QPointer<MainWindow> window(this);
        m_undoStack->push(new CameraUndo(m_cameraDocument, before, m_cameraDocument->bytes(), [window] {
            if(window) window->refreshCameraEditor();
        }));
    }
}
void MainWindow::flushCameraEditor()
{
    for(auto* field : m_cameraFields) if(field) field->interpretText();
}
bool MainWindow::hasCameraChanges() const
{
    for(const auto& document : m_cameraDocuments) if(document->dirty()) return true;
    return false;
}
void MainWindow::discardCameraChanges()
{
    for(const auto& document : m_cameraDocuments) document->discard();
    refreshCameraEditor();
}
bool MainWindow::appendCameraSaves(QVector<SMapSaveFile>* files, QString* error) const
{
    for(const auto& document : m_cameraDocuments) {
        if(!document->dirty()) continue;
        SMapSaveFile file;
        if(!document->prepareSave(&file, error)) return false;
        if(!QDir().mkpath(QFileInfo(file.path).absolutePath())) {
            if(error) *error = tr("Не удалось создать папку камеры.");
            return false;
        }
        files->append(file);
    }
    return true;
}
void MainWindow::acceptCameraSaves(const QVector<SMapSaveFile>& files)
{
    for(const auto& file : files) {
        const auto document = m_cameraDocuments.value(QFileInfo(file.path).absoluteFilePath().toCaseFolded());
        if(document) document->acceptCommitted(file.after);
    }
    refreshCameraEditor();
}
