#include "database_creation_check.h"
#include "root_bon_check.h"
#include "database_map_catalog.h"
#include "endurance_document.h"
#include "mob.h"
#include "unit.h"
#include "lever.h"
#include "light.h"
#include "worldobj.h"
#include "torch.h"
#include "magictrap.h"
#include "sound.h"
#include "particle.h"
#include "utils.h"
#include "view.h"
#include "undo.h"
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QScopedPointer>
#include <QApplication>
#include <QDir>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QSettings>
#include "createobjectform.h"
#include "preview.h"
#include "settings.h"
#include "resourcemanager.h"
#include "runtime/providers/profile_database_provider_support.h"
#include "layout_components/tree_view.h"
#include "layout_components/progressview.h"
#include "tile_form.h"
#include "camera.h"
#include "operationmanager.h"
#include "item_spell_constructor_dialog.h"
#include "database_document.h"
#include "mmp_codec.h"
#include "resource_name.h"
#include "res_file.h"
#include "resource_profile.h"
#include "mob_family_validation.h"
#include "profile_visual_provider.h"
#include "unit_animation.h"
#include <QStandardPaths>
#include <QElapsedTimer>

int runDatabaseCreationCheck()
{
    const int rootBonResult=runRootBonCheck();
    if(rootBonResult || qEnvironmentVariableIsSet("EI_ROOT_BON_ONLY"))return rootBonResult;
    QTextStream output(stdout);
    CValidationReport report;
    auto fail = [&](const QString& stage) {
        output << "FAIL: " << stage << "\n" << report.toText(); output.flush(); return 1;
    };
    QByteArray baseline;
    {
        ei::SDatabaseMapEntry creature,item,lever;
        creature.name="BoarTutor";creature.model="boar";
        item.category=ei::EDatabaseMapCategory::Item;item.name="2h bone club";
        lever.category=ei::EDatabaseMapCategory::Lever;lever.name="stst58";lever.model="stst58";lever.label="database fallback";
        QVector<ei::SDatabaseMapEntry> entries{creature,item,lever};
        ei::applyDatabaseMapTexts(&entries,{{"unit BOARTUTOR",QString::fromUtf8("Молодая кабаниха\r\nОписание")},
            {"INSTR 2h_Bone_Club",QString::fromUtf8("Двуручная дубина\r\nОписание")},
            {"LEVER stst58",QString::fromUtf8("Рычаг\r\nОписание")}});
        if(entries[0].label!=QString::fromUtf8("Молодая кабаниха") || entries[0].name!="BoarTutor" || entries[0].model!="boar"
            || entries[1].label!=QString::fromUtf8("Двуручная дубина") || entries[2].label!=QString::fromUtf8("Рычаг")
            || !entries[2].labelFromTexts)return fail("texts lookup, normalization and preserved identifiers");
        QVector<ei::SDatabaseMapEntry> missing{lever};ei::applyDatabaseMapTexts(&missing,{});
        if(missing.first().label!="database fallback" || missing.first().labelFromTexts)return fail("explicit database fallback");
        output<<"PASS: localized catalogue names preserve identifiers and report fallback provenance\n";
    }
    {
        util::CMobParser writer(baseline, true);
        writer.startSection("OBJECT_DB_FILE");
        writer.startSection("SC_OBJECT_DB_FILE"); writer.endSection();
        writer.startSection("SS_TEXT"); writer.writeStringEncrypted("unchanged_test_script", 0);
        writer.endSection();
        writer.startSection("OBJECT_SECTION"); writer.endSection();
        writer.endSection();
    }
    CMobDocument empty;
    if(!empty.loadBytes(baseline, "synthetic-baseline.mob", &report)) return fail("baseline");
    {
        // Legacy technical records are opaque to the editor projection, but
        // their IDs still reserve the same namespace as copied/new objects.
        QByteArray occupied;
        util::CMobParser writer(occupied, true);
        writer.startSection("OBJECT_DB_FILE");
        writer.startSection("SC_OBJECT_DB_FILE"); writer.endSection();
        writer.startSection("OBJECT_SECTION"); writer.endSection();
        for(const QPair<QString, QString>& record : {
                QPair<QString, QString>("LIGHT_SECTION", "LIGHT"),
                QPair<QString, QString>("SOUND_SECTION", "SOUND"),
                QPair<QString, QString>("PARTICL_SECTION", "PARTICL")})
        {
            writer.startSection(record.first);
            writer.startSection(record.second);
            writer.startSection(record.second == "LIGHT" ? "LIGHT_ID"
                : record.second == "SOUND" ? "SOUND_ID" : "PARTICL_ID");
            writer.writeDword(record.second == "LIGHT" ? 1000
                : record.second == "SOUND" ? 1001 : 1002);
            writer.endSection(); writer.endSection(); writer.endSection();
        }
        writer.endSection();
        CMob destination;
        destination.m_originalData = occupied;
        if(!destination.deserialize(occupied)
           || destination.isFreeId(1000) || destination.isFreeId(1001)
           || destination.isFreeId(1002) || destination.freeMapId() != 1003)
            return fail("legacy light/sound/particle IDs reserve allocation");
        CWorldObj seed;
        QJsonObject data = seed.toJson();
        data.insert("Model name", "synthetic_model");
        data.insert("Primary texture", "synthetic_skin");
        CWorldObj object(data);
        QJsonObject prepared;
        if(!CMob::prepareFreshTechnicalNode(&object, &prepared, &report))
            return fail("legacy collision creation seed");
        destination.captureCleanProjection();
        CNode* created = destination.createNode(prepared, false);
        if(!created || created->mapId() != 1003)
            return fail("legacy collision creation ID");
        destination.createNode(created);
        if(destination.nodes().size() != 2 || destination.nodes().last()->mapId() != 1004)
            return fail("legacy collision copy ID");
        QByteArray candidate;
        if(!destination.buildValidatedCandidate(&candidate, &report))
            return fail("legacy collision guarded save");
        CMobDocument reopened;
        if(!reopened.loadBytes(candidate, "legacy-collision.mob", &report)
           || reopened.objects().size() != 2
           || reopened.auxiliaryIds().size() != 3
           || !reopened.duplicateIds().isEmpty())
            return fail("legacy collision saved IDs");
        output << "PASS: creation and copy skip legacy light/sound/particle IDs\n";
    }
    QTemporaryDir scratch;
    if(!scratch.isValid()) return fail("temporary directory");
    for(int kind = 0; kind < 8; ++kind)
    {
        QScopedPointer<CNode> seed;
        QString name;
        switch(kind)
        {
        case 0: {
            CWorldObj defaults;
            QJsonObject data = defaults.toJson();
            data.insert("Model name", "synthetic_model");
            data.insert("Primary texture", "synthetic_skin");
            seed.reset(new CWorldObj(data)); name = "world object"; break;
        }
        case 1: {
            CLever defaults;
            QJsonObject data = defaults.toJson();
            QJsonObject world = data.value("World object").toObject();
            world.insert("Model name", "synthetic_model");
            world.insert("Primary texture", "synthetic_skin");
            data.insert("World object", world);
            seed.reset(new CLever(data)); name = "lever"; break;
        }
        case 2: {
            CTorch defaults;
            QJsonObject data = defaults.toJson();
            QJsonObject world = data.value("World object").toObject();
            world.insert("Model name", "synthetic_model");
            world.insert("Primary texture", "synthetic_skin");
            data.insert("World object", world);
            seed.reset(new CTorch(data)); name = "torch"; break;
        }
        case 3: seed.reset(new CMagicTrap); name = "magic trap"; break;
        case 4: seed.reset(new CSound); name = "sound"; break;
        case 5: seed.reset(new CParticle); name = "particle"; break;
        case 6: seed.reset(new CLight); name = "light"; break;
        default: {
            ei::SDatabaseMapEntry entry;
            entry.category = ei::EDatabaseMapCategory::Creature;
            entry.name = "Synthetic creature";
            entry.model = "synthetic_model";
            entry.primaryTexture = "synthetic_skin";
            entry.unitType = 51;
            CUnit defaults;
            QJsonObject data;
            if(!ei::applyDatabaseMapEntry(entry, defaults.toJson(), &data))
                return fail("technical unit fields");
            seed.reset(new CUnit(data)); name = "unit"; break;
        }
        }
        QVector3D position(14, 19, 2);
        seed->updatePos(position);
        QJsonObject prepared;
        report = CValidationReport();
        if(!CMob::prepareFreshTechnicalNode(seed.data(), &prepared, &report))
            return fail("fresh technical " + name);
        if(seed->mapId() != 0) return fail("technical preview identity " + name);
        CMob destination;
        destination.m_originalData = baseline;
        if(!destination.deserialize(baseline)) return fail("technical destination " + name);
        destination.captureCleanProjection();
        CNode* created = destination.createNode(prepared, false);
        QByteArray candidate;
        if(!created || created->nodeType() != seed->nodeType()
           || !destination.buildValidatedCandidate(&candidate, &report))
            return fail("technical guarded candidate " + name);
        const QString path = scratch.path() + "/technical-" + QString::number(kind) + ".mob";
        if(!destination.saveAs(QFileInfo(path)))
            return fail("technical save " + name + ": " + destination.lastSaveError());
        QFile saved(path);
        if(!saved.open(QIODevice::ReadOnly)) return fail("technical file " + name);
        const QByteArray bytes = saved.readAll();
        CMob reopened;
        if(!reopened.deserialize(bytes) || reopened.nodes().size() != 1
           || reopened.nodes().first()->nodeType() != seed->nodeType()
           || reopened.nodes().first()->position() != seed->position())
            return fail("technical reopen " + name);
        destination.deleteNode(created);
        const QString removedPath = path + ".removed.mob";
        if(!destination.saveAs(QFileInfo(removedPath)))
            return fail("technical remove " + name + ": " + destination.lastSaveError());
        QFile removed(removedPath);
        if(!removed.open(QIODevice::ReadOnly) || removed.readAll() != baseline)
            return fail("technical removal changed unrelated MOB bytes " + name);
        output << "PASS: fresh technical " << name
               << " without selected template, guarded save, reopen and removal\n";
    }
    {
        CWorldObj missingModel;
        QJsonObject rejectedData;
        CValidationReport rejected;
        if(CMob::prepareFreshTechnicalNode(&missingModel, &rejectedData, &rejected)
           || !rejected.hasErrors())
            return fail("missing model was accepted");
        CMagicTrap defaults;
        QJsonObject data = defaults.toJson();
        QJsonObject world = data.value("World object").toObject();
        world.insert("Model name", "unsupported_custom_trap_model");
        data.insert("World object", world);
        CMagicTrap invalid(data);
        rejected = CValidationReport();
        if(CMob::prepareFreshTechnicalNode(&invalid, &rejectedData, &rejected)
           || !rejected.hasErrors())
            return fail("unsupported trap model was accepted");
        output << "PASS: incomplete and nonrepresentable technical objects rejected\n";
    }
    CView view(nullptr, nullptr);

    for(int category : {0, 1, 2})
    {
        ei::SDatabaseMapEntry entry;
        entry.category = static_cast<ei::EDatabaseMapCategory>(category);
        entry.name = category == 0 ? "Test creature" : category == 1 ? "Test item" : "Test lever";
        entry.model = "synthetic_model"; entry.primaryTexture = "synthetic_skin";
        entry.unitType = 51;
        CUnit unitDefaults; CLever leverDefaults; CWorldObj itemDefaults;
        QJsonObject authored;
        if(!ei::applyDatabaseMapEntry(entry, category == 0 ? unitDefaults.toJson()
            : category == 1 ? itemDefaults.toJson() : leverDefaults.toJson(), &authored)) return fail("database descriptor");
        QScopedPointer<CNode> node(category == 0 ? static_cast<CNode*>(new CUnit(authored))
                                  : category == 1 ? static_cast<CNode*>(new CWorldObj(authored))
                                                : static_cast<CNode*>(new CLever(authored)));
        QVector3D position(12, 18, 0); node->updatePos(position);
        QJsonObject prepared;
        if(!CMob::prepareFreshDatabaseNode(node.data(), &prepared, &report))
            return fail("fresh " + entry.name);
        if(node->mapId() != 0) return fail("preview identity changed");
        const auto provenance = prepared.value("_Endurance raw provenance").toObject();
        QByteArray raw = QByteArray::fromBase64(provenance.value("sourceSubtreeBase64").toString().toLatin1());
        QByteArray hash = QByteArray::fromHex(provenance.value("sourceSubtreeSha256").toString().toLatin1());
        if(raw.isEmpty() || QCryptographicHash::hash(raw, QCryptographicHash::Sha256) != hash)
            return fail("generated provenance");
        if(category == 1)
        {
            QByteArray lever;
            if(!CMobDocument::convertWorldObjectSubtreeToLever(raw, CMobDocument::SLeverConversionValues(),
                                                               &lever, &report))
                return fail("placed item can be converted to lever");
        }
        QByteArray candidate;
        if(!empty.rebuildObjectSequenceFromRaw({raw}, {hash}, &candidate, &report))
            return fail("insert without selected object");
        CMobDocument reopened;
        QString error;
        if(!reopened.loadBytes(candidate, "created.mob", &report)
           || reopened.objects().size() != 1
           || reopened.objects().first().model != entry.model
           || reopened.objects().first().positionX != 12
           || !CMob::validateEditableProjection(candidate, QFileInfo("created.mob"), &error))
            return fail("reopen: " + error);
        QByteArray undone;
        if(!reopened.rebuildObjectSequenceFromRaw({}, {}, &undone, &report)
           || undone != baseline) return fail("remove preserves original script and metadata");
        QByteArray redone;
        CMobDocument undoDocument;
        if(!undoDocument.loadBytes(undone, "undo.mob", &report)
           || !undoDocument.rebuildObjectSequenceFromRaw({raw}, {hash}, &redone, &report)
           || redone != candidate) return fail("repeat insertion is exact");
        CValidationReport rejected;
        QByteArray corrupt = raw; corrupt[corrupt.size() - 1] = char(corrupt.at(corrupt.size() - 1) ^ 1);
        QByteArray invalid;
        if(empty.rebuildObjectSequenceFromRaw({corrupt}, {hash}, &invalid, &rejected))
            return fail("corrupt provenance was accepted");
        CMob destination;
        destination.m_originalData = baseline;
        if(!destination.deserialize(baseline)) return fail("destination projection");
        destination.captureCleanProjection();
        CNode* created = destination.createNode(prepared, false);
        if(!created || !destination.buildValidatedCandidate(&candidate, &report))
            return fail("normal guarded creation");
        const uint id = created->mapId();
        if(id == 0) return fail("new object must have a positive allocated ID");
        QUndoStack history;
        history.push(new CCreateNodeCommand(&view, &destination, created));
        history.undo();
        if(!destination.nodes().isEmpty()) return fail("create undo ownership");
        history.redo();
        if(destination.nodes().size() != 1 || destination.nodes().first() != created
           || created->mapId() != id) return fail("create redo identity");
        const QString prefix = scratch.path() + "/" + QString::number(category);
        if(!destination.saveAs(QFileInfo(prefix + "-created.mob")))
            return fail("save created object: " + destination.lastSaveError());
        history.undo();
        if(!destination.saveAs(QFileInfo(prefix + "-undone.mob")))
            return fail("save after undo: " + destination.lastSaveError());
        history.redo();
        if(destination.nodes().size() != 1 || destination.nodes().first() != created
           || !destination.saveAs(QFileInfo(prefix + "-redone.mob")))
            return fail("save after undo/save/redo: " + destination.lastSaveError());
        CNode* second = destination.createNode(prepared, false);
        CNode* third = destination.createNode(prepared, false);
        if(!second || !third || second->mapId() == 0 || third->mapId() == 0
           || second->mapId() == id || third->mapId() == id
           || second->mapId() == third->mapId()
           || !destination.saveAs(QFileInfo(prefix + "-multiple.mob")))
            return fail("multiple fresh objects: " + destination.lastSaveError());
        history.undo(); history.redo();
        if(destination.nodes().size() != 3 || destination.nodes().first() != created
           || !destination.saveAs(QFileInfo(prefix + "-multiple-redone.mob")))
            return fail("undo in populated destination: " + destination.lastSaveError());
        output << "PASS: " << entry.name << " creation, reopen, removal, repeat and hash rejection\n";
        output << "PASS: real create command undo/redo and save/undo/save/redo/save\n";
    }
    {
        CLight light;
        QVector3D position(7,9,3);light.updatePos(position);
        QJsonObject prepared;
        if(!CMob::prepareFreshLightNode(&light,&prepared,&report))return fail("standalone light template");
        CMob destination;
        destination.m_originalData=baseline;
        if(!destination.deserialize(baseline))return fail("light destination");
        destination.captureCleanProjection();
        CNode* created=destination.createNode(prepared,false);
        QByteArray candidate;
        if(!created || created->nodeType()!=eLight || created->mapId()==0
           || !destination.buildValidatedCandidate(&candidate,&report))return fail("light guarded save");
        CMob reopened;
        if(!reopened.deserialize(candidate) || reopened.nodes().size()!=1
           || reopened.nodes().first()->nodeType()!=eLight
           || reopened.nodes().first()->position()!=position)return fail("light reopen");
        QUndoStack history;history.push(new CCreateNodeCommand(&view,&destination,created));
        history.undo();
        if(!destination.nodes().isEmpty())return fail("light undo");
        history.redo();
        if(destination.nodes().size()!=1 || !destination.buildValidatedCandidate(&candidate,&report))
            return fail("light redo and save");
        output << "PASS: standalone light creation without selected object, guarded save, reopen and undo/redo\n";
    }
    output.flush(); return 0;
}

int runDatabasePreviewCheck()
{
    QTextStream output(stdout);
    auto fail = [&](const QString& message) {
        output << "FAIL: " << message << "\n"; output.flush(); return 1;
    };
    const QString archive = qEnvironmentVariable("EI_TEST_PREVIEW_DATABASE");
    const QStringList roots = qEnvironmentVariable("EI_TEST_RESOURCE_ROOTS").split(';', QString::SkipEmptyParts);
    const QString outputDirectory = qEnvironmentVariable("EI_TEST_PREVIEW_OUTPUT");
    if(archive.isEmpty() || roots.isEmpty() || outputDirectory.isEmpty())
        return fail("Set EI_TEST_PREVIEW_DATABASE, EI_TEST_RESOURCE_ROOTS and EI_TEST_PREVIEW_OUTPUT.");
    QDir().mkpath(outputDirectory);
    QTemporaryDir temporary;
    if(!temporary.isValid()) return fail("temporary directory");
    struct RestoreDirectory {
        QString path = QDir::currentPath();
        ~RestoreDirectory() { QDir::setCurrent(path); }
    } restoreDirectory;
    // All settings and diagnostic state are confined to scratch. Process-lifetime
    // UI services are retained, matching the application's ownership model.
    QDir::setCurrent(temporary.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    auto* settings = new CSettings;
    auto* rootOption = dynamic_cast<COptStringList*>(settings->opt(eOptSetResource, "resourceRoots"));
    if(!rootOption) return fail("resource settings");
    rootOption->setValue(roots.toVector());
    QSharedPointer<ei::SProfileDatabaseSnapshot> snapshot(new ei::SProfileDatabaseSnapshot);
    snapshot->profileId = qEnvironmentVariable("EI_TEST_PREVIEW_PROFILE",
        archive.contains("Jamais", Qt::CaseInsensitive) ? "jamais-vu" : "base-game");
    snapshot->providerId = usesJmvRuntime(snapshot->profileId) ? "jmv.v1" : "base.standard.v1";
    snapshot->sourceArchivePath = archive;
    QVector<ei::SDatabaseDiagnostic> diagnostics;
    snapshot->rawMembers = ei::readDatabaseArchive(archive, &diagnostics);
    ei::applyRelationalMembers(snapshot->rawMembers, "read-only-test", snapshot.data());
    ei::applyActionMembers(snapshot->rawMembers, "read-only-test", false, snapshot.data());
    if(usesJmvRuntime(snapshot->profileId))
    {
        const QString overlay = ei::siblingArchive(archive, "databaseJmv.res");
        ei::applyActionMembers(ei::readDatabaseArchive(overlay, &diagnostics),
                              overlay, true, snapshot.data());
        ei::applyJmvUnitPerkTable(roots, snapshot.data());
    }
    if(!snapshot->usable()) return fail("database snapshot");
    ei::ProfileDatabaseContext::instance().activate(snapshot,
        {{"characterAppearance", snapshot->providerId}, {"equipmentLayout", snapshot->providerId},
         {"unitAnimation", snapshot->providerId}});

    auto* form = new CCreateObjectForm;
    form->setAttribute(Qt::WA_DontShowOnScreen);
    CPreview* preview = form->viewWidget();
    preview->resize(400, 400);
    preview->renderPixmap(400,400);
    if(!preview->isValid()) return fail("preview OpenGL context");
    auto* view = new CView(nullptr, preview);
    view->setAttribute(Qt::WA_DontShowOnScreen);
    view->resize(640,480);
    auto* history = new QUndoStack;
    auto* tree = new CTreeView;
    auto* table = new QTableWidget;
    table->setColumnCount(2);
    view->attach(settings, table, history, new CProgressView, new QLineEdit, tree, new CTileForm);
    form->attach(view, history);
    preview->attachView(view);
    preview->attachSettings(settings);
    preview->makeCurrent();
    CTextureList::getInstance()->attachSettings(settings);
    CObjectList::getInstance()->attachSettings(settings);
    CTextureList::getInstance()->initResource();
    CObjectList::getInstance()->initResource();
    view->renderPixmap(640,480);
    view->m_projection.setToIdentity();
    view->m_projection.perspective(45, 640.0f/480.0f, 0.02f, 1000.0f);

    QByteArray baseline;
    {
        util::CMobParser writer(baseline, true);
        writer.startSection("OBJECT_DB_FILE");
        writer.startSection("SC_OBJECT_DB_FILE"); writer.endSection();
        writer.startSection("OBJECT_SECTION"); writer.endSection();
        writer.endSection();
    }
    auto* destination = new CMob;
    destination->m_originalData = baseline;
    if(!destination->deserialize(baseline)) return fail("empty destination");
    destination->captureCleanProjection();
    view->m_aMob.append(destination);
    view->m_activeMob = destination;
    view->collectObjectTreeData();
    form->show();
    auto* category = form->findChild<QComboBox*>("databaseCategory");
    auto* search = form->findChild<QLineEdit*>("databaseSearch");
    auto* objects = form->findChild<QListWidget*>("databaseObjects");
    if(!category || !search || !objects) return fail("picker controls");
    const auto findCode=[&](const QString& code) {
        QList<QListWidgetItem*> found;
        for(int i=0;i<objects->count();++i)
            if(objects->item(i)->data(Qt::UserRole+1).toString()==code)found.append(objects->item(i));
        return found;
    };
    if(qEnvironmentVariableIsSet("EI_TEST_CREATE_CATALOG_ONLY")) {
        form->resize(1200,800);
        auto* assembly=form->findChild<QPushButton*>("createAssemblyCatalog");
        auto* gallery=form->findChild<QPushButton*>("technicalModelGallery");
        if(!assembly || !gallery)return fail("technical entry points");
        for(int c:{0,1,2}) {
            category->setCurrentIndex(c);search->clear();
            if(!assembly->isHidden() || !gallery->isHidden())return fail("assembly/gallery visible in database category");
        }
        category->setCurrentIndex(0);search->setText("BoarTutor");
        const auto boar=findCode("BoarTutor");
        if(boar.size()!=1 || !boar.first()->text().startsWith("BoarTutor - "))return fail("localized creature identity");
        const auto localized=boar.first()->text().section(" - ",1);
        search->setText(localized);
        if(findCode("BoarTutor").size()!=1)return fail("localized creature search");
        form->grab().save(outputDirectory+"/creatures.png");
        category->setCurrentIndex(1);search->setText("2h bone club");
        const auto club=findCode("2h bone club");
        if(club.size()!=1 || !club.first()->text().startsWith("2h bone club - "))return fail("normalized item texts identity");
        form->grab().save(outputDirectory+"/items.png");
        category->setCurrentIndex(2);search->setText("stst58");
        const auto lever=findCode("stst58");
        if(lever.size()!=1 || !lever.first()->text().startsWith("stst58"))return fail("lever model first");
        objects->setCurrentItem(lever.first());
        preview->makeCurrent();
        if(!CTextureList::getInstance()->hasTexture("cavelevel00","stst58"))return fail("installed lever texture");
        auto* properties=form->findChild<QTableWidget*>("tableParameters");
        bool bound=false;
        for(int row=0;properties && row<properties->rowCount();++row) {
            auto* choice=qobject_cast<QComboBox*>(properties->cellWidget(row,1));
            if(choice && choice->currentText()=="cavelevel00")bound=true;
        }
        if(!bound)return fail("lever uses catalog texture instead of name prefix");
        preview->renderPixmap(400,400).save(outputDirectory+"/lever-texture.png");
        form->grab().save(outputDirectory+"/lever.png");
        category->setCurrentIndex(3);QApplication::processEvents();
        auto* type=form->findChild<QComboBox*>("comboObjectType");
        if(assembly->isHidden() || gallery->isHidden() || !type
            || type->mapTo(form,QPoint()).y()>=gallery->mapTo(form,QPoint()).y()
            || gallery->mapTo(form,QPoint()).y()>=assembly->mapTo(form,QPoint()).y()
            || gallery->iconSize()!=QSize(24,24) || assembly->iconSize()!=QSize(24,24))return fail("technical control order and icon sizes");
        form->grab().save(outputDirectory+"/technical.png");
        form->hide();
        const int nodeCount=destination->nodes().size();
        QString placementError;QElapsedTimer placementTimer;placementTimer.start();
        if(!view->beginGalleryPlacement({{"stst58","cavelevel00"},{"stst58","cavelevel00"}},&placementError))
            return fail(QString("gallery batch placement: %1").arg(placementError));
        output<<"Gallery batch two objects: "<<placementTimer.elapsed()<<" ms\n";
        if(destination->nodes().size()!=nodeCount+2 || !view->isDatabasePlacementPending())return fail("gallery batch count/pending");
        for(auto* node:view->m_pendingPasteNodes)if(!node->assemblyTag().isEmpty())return fail("gallery objects unexpectedly grouped");
        view->cancelPendingOperation();
        if(destination->nodes().size()!=nodeCount)return fail("gallery batch cancel");
        output<<"PASS: category visibility, original IDs, texts names/search, lever texture, technical order/icons\n";
        return 0;
    }
    const bool jmv = usesJmvRuntime(snapshot->profileId);
    const QStringList creatures = jmv ? QStringList{"AncientCurseM25", "ArmoredTurtle110"}
                                     : QStringList{snapshot->monsters.first().name};
    auto capture = [&](const QString& name) {
        // Reproduce the map overlay state that formerly replaced texture color.
        view->makeCurrent();
        auto& shader = view->shaderObject();
        shader.bind(); shader.setUniformValue("customColor", QVector4D(0,1,1,1));
        const QImage frame = preview->renderPixmap(400,400).toImage().convertToFormat(QImage::Format_RGB32);
        frame.save(outputDirectory + "/" + name + ".png");
        QSet<QRgb> colors;
        for(int y=0; y<frame.height(); ++y)
            for(int x=0; x<frame.width(); ++x) colors.insert(frame.pixel(x,y));
        output << "Preview " << name << ": " << colors.size() << " colors\n"; output.flush();
        return colors.size() > 32;
    };
    if(qEnvironmentVariableIsSet("EI_TEST_MAP_WEAPONS_ONLY"))
    {
        auto document = ei::DatabaseDocument::fromSnapshot(snapshot);
        auto catalog = ei::ItemSpellConstructorCatalog::fromDocument(*document);
        ei::MapWeaponCatalog weapons;
        if(!weapons.load(ei::MapWeaponCatalog::pathForDatabase(archive), catalog))
            return fail(weapons.diagnostic());
        auto* materials = form->findChild<QComboBox*>("databaseItemMaterial");
        auto* models = form->findChild<QComboBox*>("databaseItemModel");
        auto* place = form->findChild<QPushButton*>("buttonCreate");
        if(!materials || !models || !place) return fail("weapon picker controls");
        category->setCurrentIndex(1); search->clear();
        int linked = 0;
        for(const auto& item : catalog.items) if(!weapons.models(item.name).isEmpty()) {
            if(item.category != "Weapon") return fail("non-weapon in extracted catalog");
            ++linked;
        }
        if(linked == 0 || objects->count() != linked) return fail("only catalog-backed weapons listed");
        output << "PASS: catalog-backed weapon identities=" << linked << "\n";
        int modelCount = 0, textureChoices = 0, absentTextureChoices = 0;
        QSet<QString> checkedModels;
        for(const auto& item : catalog.items) {
            const auto variants = weapons.models(item.name);
            if(variants.isEmpty()) continue;
            int availableForItem = 0;
            for(const auto& model : variants) {
                if(checkedModels.contains(model.model)) continue;
                checkedModels.insert(model.model);
                if(!CObjectList::getInstance()->getFigureResource(model.mod)) return fail("catalog model decode: " + model.mod);
                ++modelCount;
            }
            for(const auto& material : catalog.compatibleMaterials(item)) {
                const QString texture = catalog.itemTextureName(item, &material) + (jmv ? ".x2" : "");
                if(CTextureList::getInstance()->textureList().contains(texture)) { ++textureChoices; ++availableForItem; }
                else ++absentTextureChoices;
            }
            if(availableForItem == 0) output << "RESOURCE GAP: no material texture for " << item.name << "\n";
        }
        output << "PASS: linked model decode=" << modelCount << ", exact material textures=" << textureChoices
               << ", absent textures=" << absentTextureChoices << "\n";
        const QStringList names{"cheat dagger", "stone sword", "2h bone club", "small leather shield", "bow"};
        int createdCount = 0, missingTextures = 0;
        for(const QString& name : names) {
            if(weapons.models(name).isEmpty()) continue;
            form->show(); category->setCurrentIndex(1); search->setText(name);
            const auto matches = findCode(name);
            if(matches.size() != 1) return fail("weapon identity selection: " + name);
            objects->setCurrentItem(matches.first());
            if(matches.first()->text() == name) return fail("Russian weapon name: " + name);
            if(models->count() != weapons.models(name).size()) return fail("all model variants exposed");
            if(name == "cheat dagger") form->grab().save(outputDirectory + "/weapon-picker.png");
            QSet<QString> textures;
            int successes = 0;
            for(int materialIndex = 0; materialIndex < materials->count(); ++materialIndex) {
                materials->setCurrentIndex(materialIndex);
                if(!place->isEnabled()) { ++missingTextures; continue; }
                const QString chosenModel = models->currentData().toString();
                const QString chosenMaterial = materials->currentData().toString();
                if(successes == 0 && !capture("weapon-" + chosenModel)) return fail("weapon colored preview: " + name);
                QMetaObject::invokeMethod(form,"on_buttonCreate_clicked",Qt::DirectConnection);
                if(!view->isDatabasePlacementPending() || destination->nodes().size() != 1)
                    return fail("catalog weapon pending placement: " + name);
                CNode* node = destination->nodes().first();
                const QString texture = node->textureName();
                const QString base = ei::ItemSpellConstructorCatalog::itemTextureName(*catalog.findItem(name), catalog.findMaterial(chosenMaterial));
                if(node->modelName() != chosenModel || (texture != base && texture != base + ".x2")
                   || !node->toJson().value("Object parts").toArray().isEmpty())
                    return fail("exact extracted model/material/texture identity");
                textures.insert(texture);
                if(!view->cancelPendingOperation() || !destination->nodes().isEmpty()) return fail("weapon Escape rollback");
                form->show(); QMetaObject::invokeMethod(form,"on_buttonCreate_clicked",Qt::DirectConnection);
                if(!view->isDatabasePlacementPending()) return fail("weapon retry");
                QMouseEvent click(QEvent::MouseButtonRelease,QPointF(320,240),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                view->m_pOp->mouseReleaseEvent(&click);
                if(destination->nodes().size() != 1 || history->count() != 1) return fail("weapon single undo command");
                CValidationReport report; QByteArray bytes;
                if(!destination->buildValidatedCandidate(&bytes, &report)) return fail(report.toText());
                CMob reopened;
                if(!reopened.deserialize(bytes) || reopened.nodes().size() != 1) return fail("weapon save/reopen");
                CNode* restored = reopened.nodes().first();
                if(restored->mapId() == 0 || restored->modelName() != chosenModel || restored->textureName() != texture)
                    return fail("weapon persisted ID/model/texture");
                preview->makeCurrent(); restored->loadFigure(); restored->loadTexture();
                restored->setDrawPosition(QVector3D()); preview->attachNode(restored);
                if(successes == 0 && !capture("weapon-reopened-" + chosenModel)) return fail("weapon reopened render");
                preview->attachNode(nullptr, false);
                history->undo(); if(!destination->nodes().isEmpty()) return fail("weapon undo");
                history->redo(); if(destination->nodes().size() != 1) return fail("weapon redo");
                history->undo(); history->clear();
                ++createdCount; ++successes;
                form->show();
                if(successes == 2) break;
            }
            if(successes == 0) return fail("no usable material: " + name);
            if(textures.size() != successes) return fail("material changes exact texture");
            if(models->count() > 1) {
                models->setCurrentIndex(1);
                if(!place->isEnabled() || !capture("weapon-variant-" + models->currentData().toString()))
                    return fail("second extracted variant preview");
            }
            output << "PASS: " << name << " localized/materials/variants/preview/Escape/place/ID/save/reopen/Undo/Redo\n";
        }
        if(createdCount < 6) return fail("insufficient real weapon cases");
        QString error;
        QScopedPointer<CWorldObj> invalid(weapons.create(*catalog.findItem("cheat dagger"), nullptr, "uhwdg03r", true, &error));
        if(invalid || error.isEmpty()) return fail("invalid material refusal");
        invalid.reset(weapons.create(*catalog.findItem("cheat dagger"), catalog.findMaterial("bronze"), "unhuma", true, &error));
        if(invalid || error.isEmpty()) return fail("unbound model refusal");
        auto missingMaterial = *catalog.findMaterial("bronze"); missingMaterial.code = "missing-test-material";
        invalid.reset(weapons.create(*catalog.findItem("cheat dagger"), &missingMaterial, "uhwdg03r", true, &error));
        if(invalid || !error.contains("missing-test-material")) return fail("missing exact texture refusal");
        const QString malformed = temporary.filePath("invalid-weapons.json");
        QFile fixture(malformed); if(!fixture.open(QIODevice::WriteOnly)) return fail("fixture");
        fixture.write("{\"schema_version\":99}"); fixture.close();
        if(weapons.load(malformed, catalog) || !weapons.present() || weapons.diagnostic().isEmpty()
           || !weapons.models("cheat dagger").isEmpty()) return fail("invalid catalog refuses stale mappings");
        CResFile synthetic;
        synthetic.addFiledata("test.mod", QByteArray("owned synthetic index fixture"));
        synthetic.addFiledata("test.bon", QByteArray("owned synthetic index fixture"));
        synthetic.saveToFile(temporary.filePath("test.res"));
        const auto* dagger = catalog.findItem("cheat dagger");
        QJsonObject binding{{"name", dagger->name}, {"weapon_class", dagger->typeId}, {"model_index", dagger->textureType1}};
        QJsonObject row{{"model", "test"}, {"mod", "test.mod"}, {"bon", "test.bon"}, {"db_weapons", QJsonArray{binding}}};
        QJsonObject root{{"schema_version", 2}, {"path_base", "catalog_directory"}, {"model_archive", "test.res"},
            {"database", QJsonObject{{"table", "items.weapons"}, {"identity_field", 0}, {"weapon_class_field", 2}, {"model_index_field", 5}}},
            {"models", QJsonArray{row}}};
        const auto writeCatalog = [&]() {
            if(!fixture.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            fixture.write(QJsonDocument(root).toJson()); fixture.close(); return true;
        };
        if(!writeCatalog() || !weapons.load(malformed, catalog) || weapons.models(dagger->name).size() != 1)
            return fail("synthetic valid semantic binding");
        invalid.reset(weapons.create(*dagger, catalog.findMaterial("bronze"), "test", true, &error));
        if(invalid || error.isEmpty()) return fail("unavailable model archive in profile refusal");
        binding.insert("model_index", dagger->textureType1 + 1); row.insert("db_weapons", QJsonArray{binding}); root.insert("models", QJsonArray{row});
        if(!writeCatalog() || weapons.load(malformed, catalog) || !weapons.models(dagger->name).isEmpty()) return fail("stale DB geometry binding refused");
        root.insert("model_archive", "../test.res");
        if(!writeCatalog() || weapons.load(malformed, catalog)) return fail("catalog path escape refused");
        if(weapons.load(temporary.filePath("absent.json"), catalog) || weapons.present()) return fail("absent catalog legacy mode");
        output << "PASS: invalid material/model/catalog; placements=" << createdCount
               << ", unavailable material textures=" << missingTextures << "\n";
        return 0;
    }
    if(qEnvironmentVariableIsSet("EI_TEST_NPC_CREATION_ONLY"))
    {
        QStandardPaths::setTestModeEnabled(true);
        QStringList names;
        if(jmv) names=QStringList{"Jun Male Hero","Jun Female Deva M1","AncientCurseM25","ArmoredTurtle110"};
        else for(const auto& entry:ei::databaseMapCatalog(*snapshot))
            if(entry.model=="unhuma" && entry.unavailableReason.isEmpty()) { names.append(entry.name); break; }
        if(names.isEmpty()) return fail("no fixture creature in active database");
        for(const QString& name:names) {
            form->show(); category->setCurrentIndex(0); search->setText(name);
            const auto exact=findCode(name);
            if(exact.size()!=1) return fail("NPC selection: "+name);
            objects->setCurrentItem(exact.first());
            if(!capture(name)) return fail("NPC preview: "+name);
            view->m_cam->setLocation(QVector3D(0,0,30),QVector3D(),QVector3D());
            QMetaObject::invokeMethod(form,"on_buttonCreate_clicked",Qt::DirectConnection);
            if(!view->isDatabasePlacementPending()) return fail("NPC placement without manual texture/parts repair: "+name);
            if(!view->cancelPendingOperation() || !destination->nodes().isEmpty()) return fail("NPC cancel: "+name);
            form->show(); QMetaObject::invokeMethod(form,"on_buttonCreate_clicked",Qt::DirectConnection);
            if(!view->isDatabasePlacementPending()) return fail("NPC placement retry: "+name);
            QMouseEvent click(QEvent::MouseButtonRelease,QPointF(320,240),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            view->m_pOp->mouseReleaseEvent(&click);
            if(destination->nodes().size()!=1 || history->count()!=1) return fail("NPC one-step commit: "+name);
            CValidationReport checkReport; QByteArray created;
            if(!destination->buildValidatedCandidate(&created,&checkReport)) return fail(checkReport.toText());
            const QString savedPath=temporary.filePath("npc-created.mob");
            if(!destination->saveAs(QFileInfo(savedPath))) return fail(destination->lastSaveError());
            CMobDocument saved;
            if(!saved.load(QFileInfo(savedPath),&checkReport) || saved.objects().size()!=1) return fail("NPC saved reopen: "+name);
            const auto& obj=saved.objects().first();
            if(obj.prototype!=name || obj.importMode) return fail("NPC database identity lost");
            SMobFamilyValidationRequest family;
            family.profileId=snapshot->profileId; family.database=snapshot;
            family.seedMobPaths=QStringList{savedPath}; family.explicitComposition=true; family.includeProfileWideMobs=false;
            const auto validated=CMobFamilyValidator::validate(family);
            if(validated.criticalCount()!=0) return fail("NPC family critical after creation: "+name+"\n"+validated.toText());
            const QString model=obj.model;
            if(model=="unhuma" || model=="unhufe")
            {
                ProfileVisualProviderService providers;
                SUnitAnimationRequest request;
                request.model=model; request.prototype=name;
                QJsonObject animation;
                const auto resolver=providers.animationResolver(snapshot->providerId,snapshot);
                if(resolver.isNull() || !resolver->resolve(request,&animation).success
                   || animation.value("actionName").toString()!="cidle15")
                    return fail("NPC cidle15 selection: "+name);
                const auto clip=parseNestedAnimationClip(
                    CObjectList::getInstance()->modelAnimation(model),"cidle15",
                    animation.value("length").toInt());
                if(!clip.valid()) return fail("NPC cidle15 animation resource: "+name);
                output << "PASS: " << name << " cidle15 selected and model clip parsed\n";
            }
            if((model=="unhuma" || model=="unhufe") && (obj.bodyParts.size()!=16 || !obj.bodyParts.contains("hr")))
                return fail("NPC explicit humanoid anatomy: "+name);
            if((name=="AncientCurseM25" || name=="ArmoredTurtle110") && !obj.bodyParts.isEmpty())
                return fail("integral creature was given a humanoid mask");
            history->undo(); if(!destination->nodes().isEmpty()) return fail("NPC undo: "+name);
            history->redo();
            QByteArray redone;
            if(!destination->buildValidatedCandidate(&redone,&checkReport) || redone!=created) return fail("NPC redo exact bytes: "+name);
            history->undo(); history->clear();
            output << "PASS: " << name << " picker, no manual repair, cancel/place, save/reopen, family validation, undo/redo\n";
            output.flush();
        }
        return 0;
    }
    for(const QString& name : creatures)
    {
        category->setCurrentIndex(0); search->setText(name);
        if(objects->count() != 1) return fail("creature selection " + name);
        if(!capture(name)) return fail("textured creature " + name);
    }
    preview->makeCurrent();
    if(jmv && (!CTextureList::getInstance()->hasTexture("Curse00","unmocu")
               || !CTextureList::getInstance()->hasTexture("turtleblue","unanwtu")))
        return fail("ordinary DXT placement texture validation");
    if(CTextureList::getInstance()->hasTexture("missing-database-preview-test","missing"))
        return fail("missing texture accepted");
    QString weaponName = "cheat dagger";
    if(!jmv)
        for(const auto& item : snapshot->items)
            if(item.category == "weapons") { weaponName = item.name; break; }
    const bool extractedWeapons = QFileInfo::exists(ei::MapWeaponCatalog::pathForDatabase(archive));
    if(!extractedWeapons && qEnvironmentVariableIsSet("EI_TEST_SYNTHETIC_MAP_TEXTURES"))
    {
        const auto catalog = ei::ItemSpellConstructorCatalog::fromDocument(
            *ei::DatabaseDocument::fromSnapshot(snapshot));
        const auto* item = catalog.findItem(weaponName);
        if(!item) return fail("synthetic map texture item");
        if(jmv && !CTextureList::getInstance()->textureList().contains("unhumadg_03.br.1.x2map"))
        {
            QString diagnostic;
            QScopedPointer<CWorldObj> missing(ei::ItemSpellConstructorDialog::buildItemPreview(
                catalog, *item, catalog.findMaterial("bronze"), snapshot, true, &diagnostic));
            if(missing || !diagnostic.contains("unhumadg_03.br.1.x2map.mmp"))
                return fail("missing map texture must not fall back to original equipment texture");
            output << "PASS: missing map texture rejected with exact resource name\n";
        }
        QStringList sourceNames;
        if(jmv)
        {
            for(const QString& name : CTextureList::getInstance()->textureList())
                if(name.startsWith("unhumadg_03.") && !name.endsWith("map"))
                    sourceNames.append(name);
        }
        else
            for(const auto& material : catalog.compatibleMaterials(*item))
                sourceNames.append(catalog.itemTextureCandidates(*item, &material));
        if(sourceNames.isEmpty()) return fail("synthetic map texture names");
        // The equipment resolver may select an .x2 name absent from the loose
        // texture combo list. This is a synthetic binding fixture, so provide
        // the explicit dagger name used by that resolver as well.
        if(jmv) sourceNames.append("unhumadg_03.br.1.x2");
        // Deliberately authored test colors, never a copy/conversion of game pixels.
        QImage pattern(64,64,QImage::Format_ARGB32);
        for(int y=0; y<64; ++y)
            for(int x=0; x<64; ++x)
                pattern.setPixel(x,y,qRgb(x*4,y*4,(x+y)*2));
        const QByteArray encoded = ei::encodeMmpDxt1(pattern, false);
        if(encoded.isEmpty()) return fail("synthetic DXT1 encoding");
        CResFile generated;
        for(const QString& name : sourceNames)
            generated.addFiledata(ei::databaseItemMapTextureName(name) + ".mmp", encoded);
        const QString directory = temporary.path() + "/synthetic-map-resources";
        QDir().mkpath(directory);
        generated.saveToFile(directory + "/map-textures.res");
        QVector<QString> testRoots = roots.toVector(); testRoots.prepend(directory);
        rootOption->setValue(testRoots);
        CTextureList::getInstance()->refreshConfiguredSources();
        output << "Synthetic DXT1 map textures: binding test, not game appearance acceptance\n";
    }
    category->setCurrentIndex(1); search->setText(weaponName);
    const auto matches = findCode(weaponName);
    if(matches.size() != 1) return fail("weapon selection " + weaponName);
    objects->setCurrentItem(matches.first());
    if(!capture("weapon"))
    {
        for(auto* label : form->findChildren<QLabel*>()) output << label->text() << "\n";
        return fail("weapon preview " + weaponName);
    }
    auto* material = form->findChild<QComboBox*>("databaseItemMaterial");
    if(!material || material->count() == 0) return fail("weapon materials");
    if(material->count() > 1)
    {
        material->setCurrentIndex(1);
        auto* button = form->findChild<QPushButton*>("buttonCreate");
        if(button && button->isEnabled())
        {
            if(!capture("weapon-material-2")) return fail("second weapon material");
        }
        else
        {
            bool namedMissingResource = false;
            for(auto* label : form->findChildren<QLabel*>())
                namedMissingResource |= label->text().contains("map.mmp");
            if(!namedMissingResource) return fail("missing material must report its map texture");
            output << "PASS: missing second map material disables placement with filename\n";
        }
    }
    if(jmv)
    {
        const int bronze = material->findData("bronze");
        if(bronze < 0) return fail("reported dagger bronze material");
        material->setCurrentIndex(bronze);
        if(!capture("cheat-dagger-bronze")) return fail("reported dagger preview");
    }
    auto* placeButton = form->findChild<QPushButton*>("buttonCreate");
    if(!placeButton || !placeButton->isEnabled())
        return fail("selected database item must enable Place on map");
    QMetaObject::invokeMethod(form,"on_buttonCreate_clicked",Qt::DirectConnection);
    if(form->isVisible() || !view->isDatabasePlacementPending()
       || destination->nodes().size()!=1 || history->count()!=0)
        return fail("item Place closes picker and attaches object to pointer");
    CNode* itemNode = destination->nodes().first();
    if(itemNode->nodeType()!=eWorldObject || itemNode->textureName().isEmpty())
        return fail("item must be an ordinary textured world object");
    const QString itemModel = itemNode->modelName(), itemTexture = itemNode->textureName();
    // The game appends .mmp to a map identifier. Do not use the editor's
    // permissive alias/extension fallback to validate this persistence contract.
    if(itemTexture.endsWith(".mmp", Qt::CaseInsensitive)
       || !CTextureList::getInstance()->textureList().contains(itemTexture))
        return fail("saved item texture must name an exact game resource without extension");
    if(extractedWeapons) {
        if(jmv && (itemModel != "uhwdg03r" || itemTexture != "unhumadg_03.br.1.x2"))
            return fail("extracted catalog model and original material texture");
    } else {
    if(!itemTexture.endsWith("map", Qt::CaseInsensitive)
       || (jmv && itemTexture != "unhumadg_03.br.1.x2map"))
        return fail("placed item must use the separate map texture resource");
    if(ei::textureUvScale(itemTexture) != 1.0f)
        return fail("map texture must use authored UVs, without equipment x2 correction");
    }
    const QVector3D itemStart = itemNode->position();
    if(!view->updateDatabasePlacementPointer(QPoint(420,280)) || itemNode->position()==itemStart)
        return fail("item follows pointer");
    if(!view->cancelPendingOperation() || !destination->nodes().isEmpty() || history->count()!=0)
        return fail("item cancellation leaves no object or undo entry");
    form->show();
    QMetaObject::invokeMethod(form,"on_buttonCreate_clicked",Qt::DirectConnection);
    if(!view->isDatabasePlacementPending()) return fail("repeat item placement");
    QMouseEvent itemClick(QEvent::MouseButtonRelease,QPointF(420,280),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    view->m_pOp->mouseReleaseEvent(&itemClick);
    if(view->isDatabasePlacementPending() || history->count()!=1 || destination->nodes().size()!=1)
        return fail("item click commits one action");
    CNode* placedItem = destination->nodes().first();
    const uint placedId = placedItem->mapId();
    if(placedId == 0) return fail("pointer placement must allocate a positive ID");
    const auto checkLiveConversion = [&](const QString& phase) {
        CValidationReport report;
        QByteArray sourceObject, cleanObject, sourceLever, cleanLever;
        if(!destination->rawObjectTemplateBytes(placedItem, &sourceObject, &cleanObject)
           || !CMobDocument::convertWorldObjectSubtreeToLever(sourceObject,
                 CMobDocument::SLeverConversionValues(), &sourceLever, &report)
           || !CMobDocument::convertWorldObjectSubtreeToLever(cleanObject,
                 CMobDocument::SLeverConversionValues(), &cleanLever, &report)) return false;
        QJsonObject data = CLever().toJson();
        data.insert("World object", placedItem->toJson());
        auto* replacement = new CLever(data);
        replacement->setRawObjectProvenance(placedItem->rawSourceDocumentSha256(),
            placedItem->rawSourceObjectPath(), QCryptographicHash::hash(sourceLever, QCryptographicHash::Sha256));
        replacement->setRawStructuralReplacementBytes(sourceLever, cleanLever);
        QUndoStack conversions;
        conversions.push(new CConvertWorldObjectToLeverCommand(view, destination, placedItem, replacement));
        const auto verify = [&](CNode* expected) {
            QByteArray bytes;
            CMobDocument saved;
            const bool valid = destination->nodes().size() == 1 && destination->nodes().first() == expected
                && expected->mapId() == placedId && placedId > 0
                && destination->buildValidatedCandidate(&bytes, &report)
                && saved.loadBytes(bytes, "conversion-check.mob", &report)
                && saved.objects().size() == 1 && saved.objects().first().nid == placedId;
            if(!valid) output << "Conversion " << phase << " expected ID " << placedId
                              << ", actual " << expected->mapId() << "\n" << report.toText();
            return valid;
        };
        if(!verify(replacement)) return false;
        const QString prefix = temporary.path() + "/conversion-" + phase;
        if(!destination->saveAs(QFileInfo(prefix + "-lever.mob")))
        { output << "Conversion save: " << destination->lastSaveError() << "\n"; return false; }
        conversions.undo();
        if(!verify(placedItem) || !destination->saveAs(QFileInfo(prefix + "-object.mob"))) return false;
        conversions.redo();
        if(!verify(replacement) || !destination->saveAs(QFileInfo(prefix + "-redo.mob"))) return false;
        conversions.undo();
        return verify(placedItem);
    };
    if(!checkLiveConversion("fresh")) return fail("fresh live conversion retains allocated positive ID");
    if(!destination->saveAs(QFileInfo(temporary.path() + "/conversion-saved-baseline.mob")))
        return fail("save original object before second conversion");
    if(!checkLiveConversion("saved")) return fail("saved live conversion retains allocated positive ID");
    output << "PASS: live item-to-lever conversion retains positive ID through undo/redo/save/reopen\n";
    CValidationReport itemReport; QByteArray itemBytes;
    if(!destination->buildValidatedCandidate(&itemBytes,&itemReport)) return fail(itemReport.toText());
    CMob itemReopened;
    if(!itemReopened.deserialize(itemBytes) || itemReopened.nodes().size()!=1)
        return fail("reopen placed item");
    CNode* reloaded = itemReopened.nodes().first();
    if(reloaded->nodeType()!=eWorldObject || reloaded->modelName()!=itemModel
       || reloaded->textureName()!=itemTexture) return fail("item appearance identity survives reopen");
    if(reloaded->toJson().value("Secondary texture").toString()!=itemTexture)
        return fail("secondary item texture identifier survives reopen");
    preview->makeCurrent(); reloaded->loadFigure(); reloaded->loadTexture();
    reloaded->setDrawPosition(QVector3D()); // The preview camera frames local geometry.
    preview->attachNode(reloaded);
    if(!capture("item-reopened")) return fail("item appearance after reopen");
    preview->attachNode(nullptr,false);
    CMobDocument itemDocument, emptyDocument;
    QByteArray itemRaw, leverRaw, leverBytes;
    if(!itemDocument.loadBytes(itemBytes,"item.mob",&itemReport)
       || !emptyDocument.loadBytes(baseline,"empty.mob",&itemReport)) return fail("item conversion documents");
    const auto& savedItem = itemDocument.objects().first();
    if(!itemDocument.objectSubtreeBytes(savedItem.sectionPath,savedItem.subtreeSha256,&itemRaw)
       || !CMobDocument::convertWorldObjectSubtreeToLever(itemRaw,CMobDocument::SLeverConversionValues(),&leverRaw,&itemReport)
       || !emptyDocument.rebuildObjectSequenceFromRaw({leverRaw},
           {QCryptographicHash::hash(leverRaw,QCryptographicHash::Sha256)},&leverBytes,&itemReport))
        return fail("item conversion to lever: " + itemReport.toText());
    CMob converted;
    if(!converted.deserialize(leverBytes) || converted.nodes().size()!=1)
        return fail("reopen converted item lever");
    CNode* leverNode = converted.nodes().first();
    if(leverNode->nodeType()!=eLever || leverNode->modelName()!=itemModel
       || leverNode->textureName()!=itemTexture
       || leverNode->toJson().value("World object").toObject().value("Object parts")
           != reloaded->toJson().value("Object parts")) return fail("conversion retains item appearance fields");
    preview->makeCurrent(); leverNode->loadFigure(); leverNode->loadTexture();
    leverNode->setDrawPosition(QVector3D()); preview->attachNode(leverNode);
    if(!capture("item-as-lever")) return fail("converted item appearance");
    preview->attachNode(nullptr,false);
    history->undo();
    if(!destination->nodes().isEmpty()) return fail("item undo");
    history->redo();
    if(destination->nodes().size()!=1 || !destination->buildValidatedCandidate(&itemBytes,&itemReport))
        return fail("item redo");
    history->undo(); history->clear();
    form->show();
    category->setCurrentIndex(0); search->setText(creatures.first());
    if(!QMetaObject::invokeMethod(form,"on_buttonCreate_clicked",Qt::DirectConnection)
       || form->isVisible() || !view->isDatabasePlacementPending()
       || destination->nodes().size()!=1 || history->count()!=0)
        return fail("Place must hide picker and start one visible pending object before click");
    CNode* pending = destination->nodes().first();
    const QVector3D start = pending->position();
    if(!view->updateDatabasePlacementPointer(QPoint(420,280)) || pending->position()==start)
        return fail("pending object follows pointer on neutral grid");
    if(!view->cancelPendingOperation() || !destination->nodes().isEmpty()
       || history->count()!=0 || view->isDatabasePlacementPending()) return fail("Escape rollback");
    form->show(); search->setText(creatures.first());
    QMetaObject::invokeMethod(form,"on_buttonCreate_clicked",Qt::DirectConnection);
    if(!view->isDatabasePlacementPending()) return fail("repeat placement");
    QMouseEvent click(QEvent::MouseButtonRelease,QPointF(420,280),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    view->m_pOp->mouseReleaseEvent(&click);
    if(view->isDatabasePlacementPending() || destination->nodes().size()!=1 || history->count()!=1)
        return fail("click commits a single creation action");
    CValidationReport report; QByteArray candidate;
    if(!destination->buildValidatedCandidate(&candidate,&report)) return fail(report.toText());
    history->undo();
    if(!destination->nodes().isEmpty()) return fail("undo placed object");
    history->redo();
    if(destination->nodes().size()!=1 || !destination->buildValidatedCandidate(&candidate,&report))
        return fail("redo placed object");
    output << "PASS: textured previews, DXT eligibility, weapon materials, immediate pending placement, pointer motion, cancellation, one-step undo/redo and guarded candidate\n";
    output.flush(); return 0;
}
