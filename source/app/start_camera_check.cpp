#include "start_camera_check.h"
#include "camera_document.h"
#include "camera.h"
#include "main_window.h"
#include "view.h"
#include "landscape.h"
#include "settings.h"
#include "resource_profile.h"
#include "resource_setup_dialog.h"
#include "mob.h"
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QDataStream>
#include <QTimer>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QPushButton>
#include "resourcemanager.h"
#include "worldobj.h"
#include "node.h"
#include "unit.h"
#include <QThread>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QOpenGLFramebufferObject>
#include <cmath>

namespace {
class HiddenCameraCheckWindows : public QObject
{
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if(event->type() == QEvent::Polish || event->type() == QEvent::Show)
            if(auto* widget = qobject_cast<QWidget*>(object))
                if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QByteArray syntheticCam(bool moving)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for(int index = 0; index < 3; ++index) {
        const auto pose = SCameraPose::fromAngles(QVector3D(12 + (moving ? index : 0), 16, 9), QVector3D(150, -20, 5));
        stream << quint32(1182920056 + index*600) << quint32(0x007bfd14)
               << pose.position.x() << pose.position.y() << pose.position.z()
               << pose.rotation.scalar() << pose.rotation.x() << pose.rotation.y() << pose.rotation.z();
    }
    return bytes;
}
bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool metadataEqual(const QByteArray& before, const QByteArray& after)
{
    if(before.size() != after.size()) return false;
    for(int offset = 0; offset < before.size(); offset += 36)
        if(before.mid(offset, 8) != after.mid(offset, 8)) return false;
    return true;
}
bool matrixClose(const QMatrix4x4& a, const QMatrix4x4& b)
{
    for(int i = 0; i < 16; ++i) if(qAbs(a.constData()[i] - b.constData()[i]) > 0.0001f) return false;
    return true;
}
}

int runStartCameraCheck()
{
    const QString root = qEnvironmentVariable("EI_CAMERA_CHECK_ROOT");
    if(root.isEmpty() || !QFileInfo(root).isDir()) return 2;
    QTemporaryDir temporary(QDir(root).filePath("tests-XXXXXX"));
    if(!temporary.isValid()) return 2;
    QDir::setCurrent(temporary.path());
    qputenv("EI_MAPPER_BACKUP_ROOT", temporary.filePath("automatic-backups").toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());
    qApp->setProperty("workspaceLoadSmoke", true);
    qApp->setQuitOnLastWindowClosed(false);
    HiddenCameraCheckWindows hidden;
    qApp->installEventFilter(&hidden);
    int failures = 0;
    QTextStream out(stdout);
    auto check = [&](bool passed, const char* name) {
        out << (passed ? "PASS " : "FAIL ") << name << "\n";
        out.flush();
        if(!passed) ++failures;
    };
    const QByteArray stationary = syntheticCam(false);
    const QByteArray moving = syntheticCam(true);
    const QString file = temporary.filePath("stationary.cam");
    check(writeFile(file, stationary), "create owned synthetic CAM");
    CCameraDocument document;
    QString error;
    check(document.open(file, file, &error) && document.bytes() == stationary && !document.dirty()
          && document.count() == 3 && document.stationary(), "CAM no-op exact bytes");
    check(!CCameraDocument::validate({}) && !CCameraDocument::validate(stationary.left(107)), "reject empty and truncated CAM");
    QByteArray zeroQuaternion = stationary;
    zeroQuaternion.replace(20, 16, QByteArray(16, 0));
    check(!CCameraDocument::validate(zeroQuaternion), "reject zero quaternion");
    QByteArray nan = stationary;
    nan.replace(8, 4, QByteArray::fromHex("0000c07f"));
    check(!CCameraDocument::validate(nan), "reject nonfinite position");
    QByteArray metadata = stationary;
    metadata[4] = char(metadata[4] ^ 1);
    check(!document.restore(metadata), "restore refuses timing or unknown-byte mutation");
    auto pose = document.pose(1);
    pose.position.setX(19.25f);
    check(document.setPose(1, pose, false) && document.dirty(), "edit one moving-track key position");
    QByteArray expected = stationary;
    expected.replace(44, 4, document.bytes().mid(44, 4));
    check(document.bytes() == expected, "single coordinate preserves every unrelated byte");
    check(document.restore(stationary) && !document.dirty(), "Undo exact original bytes");
    pose = document.pose(0);
    pose.position += QVector3D(1,2,3);
    check(document.setPose(0, pose, true) && document.stationary()
          && document.pose(2).position == pose.position && metadataEqual(stationary, document.bytes()), "stationary all-key edit preserves metadata");
    const auto edited = document.bytes();
    check(document.restore(stationary) && document.restore(edited), "Redo exact edited bytes");
    const QString movingFile = temporary.filePath("moving.cam");
    writeFile(movingFile, moving);
    CCameraDocument path;
    check(path.open(movingFile, movingFile) && !path.stationary()
          && !path.setPose(0, pose, true), "moving path rejects accidental all-key collapse");
    bool geometry = true;
    for(float pitch : {-89.0f, -20.0f, 0.0f, 45.0f, 89.0f}) {
        const auto original = SCameraPose::fromAngles(QVector3D(13,24,8), QVector3D(-169,pitch,12));
        SCameraPose reopened;
        const auto rebuilt = SCameraPose::fromAngles(original.position, original.angles());
        geometry &= SCameraPose::fromView(original.view(), &reopened)
            && (reopened.position-original.position).length() < 0.0001f
            && QVector3D::dotProduct(original.forward(), reopened.forward()) > 0.99999f
            && QVector3D::dotProduct(original.up(), rebuilt.up()) > 0.99999f;
    }
    check(geometry, "CAM WXYZ forward/up view and yaw-pitch-roll roundtrip");
    CCamera camera;
    camera.setLocation(QVector3D(1,2,10), QVector3D(3,4,0), QVector3D(-75,0,25));
    const auto normalView = camera.viewMatrix();
    camera.setPreviewView(pose.view());
    const bool previewExact = matrixClose(camera.update(), pose.view()) && matrixClose(camera.viewMatrix(), pose.view());
    float angle = 15;
    camera.xRotate(angle);
    camera.setPreviewView(pose.view());
    camera.clearPreviewView();
    check(previewExact && matrixClose(camera.viewMatrix(), normalView), "preview uses absolute eye and restores ordinary camera");
    SMapSaveFile save;
    check(document.prepareSave(&save, &error), "prepare lossless CAM save");
    const QString backups = temporary.filePath("backups");
    const auto saved = commitMapSaveBatch({save}, backups);
    check(saved.success && readFile(file) == edited && saved.backups.size() == 1
          && readFile(saved.backups.value(0)) == stationary, "atomic CAM save and verified backup");
    document.acceptCommitted(edited);
    check(!document.dirty(), "accepted save establishes clean checkpoint");
    document.setPose(0, SCameraPose::fromAngles(QVector3D(20,20,10), QVector3D(5,-10,0)), true);
    document.prepareSave(&save, &error);
    writeFile(file, moving);
    check(!commitMapSaveBatch({save}, backups).success && readFile(file) == moving, "external edit prevents overwrite");
    writeFile(file, edited);
    SMapSaveFile second;
    second.path = temporary.filePath("second.cam");
    second.before = moving;
    second.after = stationary;
    writeFile(second.path, moving);
    const auto failed = commitMapSaveBatch({save, second}, backups, 1);
    check(!failed.success && failed.rollbackComplete && readFile(file) == edited && readFile(second.path) == moving,
          "partial two-CAM save restores both exact originals");
    const auto zones = CMapCatalog::parseMapTopology("#zone z gipat brief\n#res\na b\n#camera\nz1m\n#exit 1\nnone 0\n#deploy\n1 2 3 4\n#view\n20\n");
    check(zones.size() == 1 && zones[0].cameraName == "z1m" && zones[0].mprName == "a"
          && zones[0].travelZones.size() == 1 && zones[0].travelZones[0].viewAngle == 20,
          "map.txt camera binding preserves resources and entrances");
    const QString actualPath = temporary.filePath("z1m.cam");
    const QByteArray actualBytes = readFile(QDir(root).filePath("new/Camera/z1m.cam"));
    writeFile(actualPath,actualBytes);
    CCameraDocument actual;
    bool actualOk = actual.open(actualPath,actualPath) && actual.count()==174 && actual.stationary();
    if(actualOk) {
        auto key = actual.pose(87);
        auto angles = key.angles();
        angles.setY(angles.y()+1);
        key.rotation = SCameraPose::fromAngles(key.position,angles).rotation;
        actualOk = actual.setPose(87,key,false) && metadataEqual(actualBytes,actual.bytes())
            && actual.bytes().left(87*36+20)==actualBytes.left(87*36+20)
            && actual.bytes().mid(88*36)==actualBytes.mid(88*36);
        SMapSaveFile actualSave;
        actualOk = actualOk && actual.prepareSave(&actualSave,&error)
            && commitMapSaveBatch({actualSave},backups).success && readFile(actualPath)==actual.bytes();
    }
    check(actualOk,"real z1m 174-key CAM edits and saves only chosen quaternion");
    const QString recordedPath = temporary.filePath("camera01.cam");
    const QByteArray recordedBytes = readFile(QDir(root).filePath("base/Camera/camera01.cam"));
    writeFile(recordedPath,recordedBytes);
    CCameraDocument recorded;
    bool recordedOk = recorded.open(recordedPath,recordedPath) && recorded.count()==401 && !recorded.stationary();
    if(recordedOk) {
        auto key = recorded.pose(200);
        key.position.setZ(key.position.z()+0.5f);
        recordedOk = recorded.setPose(200,key,false) && metadataEqual(recordedBytes,recorded.bytes())
            && recorded.bytes().left(200*36)==recordedBytes.left(200*36)
            && recorded.bytes().mid(201*36)==recordedBytes.mid(201*36);
    }
    check(recordedOk,"real Base camera01 401-key moving path keeps other keys exact");

    // Production widgets and normal Ctrl+S entry point. Read MPR directly so
    // this check never acquires the owner's ProgramData map leases.
    const QString fixture = QDir(root).filePath("new/Maps/zonemainmenunew.mpr");
    if(!QFileInfo::exists(fixture)) return 2;
    QDir().mkpath(temporary.filePath("Maps"));
    QDir().mkpath(temporary.filePath("Camera"));
    const QString mprPath = temporary.filePath("Maps/zonemainmenunew.mpr");
    QFile::copy(fixture, mprPath);
    const QByteArray mprBefore = readFile(mprPath);
    QTimer modalDriver;
    QObject::connect(&modalDriver, &QTimer::timeout, [&] {
        for(QWidget* widget : QApplication::topLevelWidgets()) {
            if(auto* setup = qobject_cast<CResourceSetupDialog*>(widget)) setup->reject();
            else if(auto* box = qobject_cast<QMessageBox*>(widget)) {
                if(box->isVisible()) {
                    out << "Unexpected dialog " << box->text() << "\n";
                    check(false, "no unexpected modal dialog");
                    box->reject();
                }
            }
        }
    });
    modalDriver.start(1);
    {
        MainWindow window;
        window.setAttribute(Qt::WA_DontShowOnScreen);
        window.resize(1280,800);
        window.show();
        QApplication::processEvents();
        auto* view = window.m_pView;
        view->makeCurrent();
        view->m_pLand->readMap(QFileInfo(mprPath), false);
        CValidationReport report;
        view->m_mprDocument.loadBytes(mprBefore, mprPath, &report);
        view->m_pLand->setDirty(false);
        const QString uiCam = temporary.filePath("Camera/mainmenu.cam");
        writeFile(uiCam, stationary);
        check(window.openCameraFile(uiCam), "production CAM file open");
        window.m_cameraDock->show();
        QApplication::processEvents();
        const int historyBefore = window.m_undoStack->count();
        window.m_cameraFields[0]->setValue(20);
        check(window.hasTravelChanges() && window.m_undoStack->count() == historyBefore+1
              && window.m_cameraDocument->pose(2).position.x() == 20, "production coordinate widget and all-key Undo");
        window.on_actionUndo_triggered();
        check(!window.m_cameraDocument->dirty() && window.m_cameraDocument->bytes() == stationary,
              "normal Undo restores CAM checkpoint");
        window.on_actionRedo_triggered();
        check(window.m_cameraDocument->dirty(), "normal Redo restores CAM edit");
        const auto uiEdited = window.m_cameraDocument->bytes();
        window.m_cameraPreview->setChecked(true);
        SCameraPose captured;
        check(view->captureCamPose(&captured) && (captured.position-window.m_cameraDocument->pose(0).position).length()<0.0001f
              && !window.m_cameraFromView->isEnabled(), "production view-from-camera selected key");
        window.m_cameraPreview->setChecked(false);
        window.on_actionSave_triggered();
        check(!window.m_cameraDocument->dirty() && readFile(uiCam) == uiEdited && readFile(mprPath) == mprBefore,
              "normal Ctrl+S writes only CAM and preserves MPR");
        window.on_actionUndo_triggered();
        check(window.m_cameraDocument->dirty() && window.m_cameraDocument->bytes() == stationary,
              "Undo remains bound to current CAM after save and context refresh");
        window.on_actionRedo_triggered();
        check(!window.m_cameraDocument->dirty(), "Redo returns to saved CAM checkpoint");
        const QString pathCam = temporary.filePath("path.cam");
        writeFile(pathCam, moving);
        check(window.openCameraFile(pathCam) && !window.m_cameraAllKeys->isEnabled(), "moving CAM opens with per-key editing");
        window.m_cameraKey->setValue(2);
        window.m_cameraFields[4]->setValue(-30);
        check(window.m_cameraDocument->bytes().left(36) == moving.left(36)
              && window.m_cameraDocument->bytes().mid(72) == moving.mid(72)
              && metadataEqual(moving, window.m_cameraDocument->bytes()), "selected-key pitch preserves other keys and metadata");
        window.m_undoStack->undo();
        SCameraPose fromView;
        view->captureCamPose(&fromView);
        window.m_cameraFromView->click();
        check((window.m_cameraDocument->pose(1).position-fromView.position).length()<0.0001f
              && QVector3D::dotProduct(window.m_cameraDocument->pose(1).forward(),fromView.forward())>0.99999f,
              "production set-key-from-current-view button");
        window.m_undoStack->undo();
        window.m_cameraDock->hide();
        window.m_cameraDocument.clear();
        window.refreshCameraEditor();
        view->unloadLand(false);
        window.m_undoStack->clear();
        if(qEnvironmentVariableIntValue("EI_CAMERA_CHECK_SCENE") == 1) {
            const QString gameRoot = qEnvironmentVariable("EI_CAMERA_GAME_ROOT");
            const QDir workspace(root);
            const QString evidence = workspace.filePath("evidence");
            const QString corrected = workspace.filePath("corrected");
            QDir().mkpath(evidence);
            QDir().mkpath(corrected+"/Maps");
            QDir().mkpath(corrected+"/Camera");
            for(const QString& relative : {QString("Maps/zonemainmenunew.mpr"), QString("Maps/zonemainmenunew.mob"),
                    QString("Maps/zonemainmenunew-atlas.res"), QString("Camera/mainmenu.cam")}) {
                const QString source = workspace.filePath("new/"+relative);
                const QString target = corrected+"/"+relative;
                if(!QFileInfo::exists(target)) check(QFile::copy(source,target), qPrintable("copy corrected "+relative));
            }
            QVector3D baseColumn;
            SCameraPose basePose;
            QImage baseImage, newImage;
            QJsonObject scene;
            for(int phase = 0; phase < 2; ++phase) {
                const QString kind = phase == 0 ? "base" : "corrected";
                SResourceProfileEvaluation profile;
                for(auto candidate : builtInResourceProfiles()) {
                    if(candidate.id != (phase == 0 ? "base-game" : "deja-vu")) continue;
                    // Rendering reads Base/Res directly. The portable layout's
                    // Engine/game.exe is never launched or required by this fixture.
                    if(phase == 0) {
                        profile.gameRoot = QDir(gameRoot).filePath("Base");
                        profile.profileId = candidate.id;
                        profile.profileName = "Base resources, render fixture";
                        profile.providers = candidate.providers;
                        profile.runtimeManifestId = candidate.runtimeManifestId;
                        profile.resourceDirectories = QStringList{QDir(profile.gameRoot).filePath("Res")};
                        profile.mapDirectories = QStringList{workspace.filePath("base/Maps")};
                        profile.valid = QFileInfo::exists(QDir(profile.gameRoot).filePath("Res/database.res"));
                    } else profile = evaluateResourceProfile(gameRoot,candidate);
                }
                check(profile.valid, qPrintable(kind+" resource profile"));
                if(!profile.valid) return 2;
                window.m_settings->applyResourceProfile(profile);
                view->refreshAfterResourceSetup();
                view->makeCurrent();
                const QString sceneMpr = workspace.filePath(kind+"/Maps/zonemainmenunew.mpr");
                const QString sceneMob = workspace.filePath(kind+"/Maps/zonemainmenunew.mob");
                const QString sceneCam = workspace.filePath(kind+"/Camera/mainmenu.cam");
                const QString atlas = workspace.filePath(kind+"/Maps/zonemainmenunew-atlas.res");
                CTextureList::getInstance()->setMapLocalTerrainArchive(QFileInfo::exists(atlas) ? atlas : QString());
                view->m_activeTerrainArchivePath = QFileInfo::exists(atlas) ? atlas : QString();
                view->m_pLand->readMap(QFileInfo(sceneMpr), false);
                CValidationReport loadedReport;
                check(view->m_mprDocument.loadBytes(readFile(sceneMpr),sceneMpr,&loadedReport)
                      && view->m_terrainEdit.load(view->m_mprDocument,&loadedReport), qPrintable(kind+" MPR canonical load"));
                view->m_pLand->setCanonicalTerrainModel(&view->m_terrainEdit);
                view->m_terrainEditAvailable = true;
                view->m_worldTerrainEnabled = false;
                view->m_pLand->setDirty(false);
                auto* mob = new CMob;
                mob->attach(view,view->m_pProgress);
                QFileInfo mobFile(sceneMob);
                QStringList warnings;
                mob->readMob(mobFile,false,&warnings);
                view->m_aMob.append(mob);
                view->m_activeMob = mob;
                QVector3D column;
                bool foundColumn = false;
                for(auto* node : mob->nodes()) if(node->mapId() == 2) {
                    column = node->drawPosition();
                    foundColumn = true;
                }
                check(foundColumn, qPrintable(kind+" column anchor"));
                window.m_cameraDocuments.clear();
                check(window.openCameraFile(sceneCam), qPrintable(kind+" CAM open"));
                window.m_cameraDock->show();
                if(phase == 0) {
                    baseColumn = column;
                    basePose = window.m_cameraDocument->pose(0);
                } else {
                    SCameraPose transferred = basePose;
                    transferred.position += column-baseColumn;
                    check(window.m_cameraDocument->setPose(0,transferred,true,&error), "transfer Base column-relative camera to corrected copy");
                    window.refreshCameraEditor();
                    const bool saved = !window.m_cameraDocument->dirty()
                        || window.saveMapWithTravel(workspace.filePath("camera-backups"));
                    check(saved && readFile(sceneCam)==window.m_cameraDocument->bytes(), "normal save corrected CAM copy");
                    scene.insert("translation",QJsonArray{column.x()-baseColumn.x(),column.y()-baseColumn.y(),column.z()-baseColumn.z()});
                    scene.insert("correctedPosition",QJsonArray{transferred.position.x(),transferred.position.y(),transferred.position.z()});
                }
                window.m_cameraPreview->setChecked(true);
                window.refreshCameraEditor();
                view->setMapSkyType("", "gipat");
                view->m_objectHelpersVisible = false;
                QElapsedTimer preparation;
                preparation.start();
                bool prepared = false;
                while(preparation.elapsed() < 30000) {
                    QApplication::processEvents();
                    QString warmupError;
                    view->captureTopdownPass(ETopdownPass::None,QSize(960,720),{},&warmupError);
                    prepared = true;
                    for(auto* node : mob->nodes()) if(auto* unit = dynamic_cast<CUnit*>(node))
                        if(unit->needsVisualResolution() || unit->needsAppearancePreparation()) prepared = false;
                    if(prepared) break;
                    QThread::msleep(10);
                }
                check(prepared,qPrintable(kind+" all native unit appearances ready before capture"));
                QString captureError;
                const auto image = view->captureTopdownPass(ETopdownPass::None,QSize(960,720),{},&captureError);
                check(!image.isNull() && image.save(evidence+"/"+kind+"-camera.png"), qPrintable(kind+" owning-context camera frame"));
                if(!captureError.isEmpty()) out << captureError << "\n";
                if(phase == 0) baseImage = image; else newImage = image;
                scene.insert(kind+"Column",QJsonArray{column.x(),column.y(),column.z()});
                scene.insert(kind+"Nodes",mob->nodes().size());
                window.m_cameraPreview->setChecked(false);
                window.m_cameraDocument.clear();
                window.m_cameraDocuments.clear();
                window.m_undoStack->clear();
                view->unloadMob("");
                view->unloadLand(false);
            }
            if(!baseImage.isNull() && !newImage.isNull()) {
                QImage comparison(1920,760,QImage::Format_RGB32);
                comparison.fill(QColor(25,28,32));
                QPainter painter(&comparison);
                painter.drawImage(0,40,baseImage);
                painter.drawImage(960,40,newImage);
                painter.setPen(Qt::white);
                painter.setFont(QFont("Segoe UI",15));
                painter.drawText(20,28,QString::fromUtf8("Base / исходная CAM"));
                painter.drawText(980,28,QString::fromUtf8("DejaVu / ракурс Base относительно колонны ID2"));
                painter.end();
                check(comparison.save(evidence+"/camera-comparison.png"), "side-by-side camera comparison");
            }
            writeFile(evidence+"/scene.json",QJsonDocument(scene).toJson());
        }
        window.close();
        QElapsedTimer deadline;
        deadline.start();
        while(window.isVisible() && deadline.elapsed() < 20000) QApplication::processEvents();
        check(!window.isVisible(), "production window asynchronous shutdown");
    }
    qApp->removeEventFilter(&hidden);
    out << "RESULT failures=" << failures << "\n";
    return failures ? 1 : 0;
}
