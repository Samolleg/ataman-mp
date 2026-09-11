#include "database_creation_check.h"
#include "database_map_catalog.h"
#include "endurance_document.h"
#include "mob.h"
#include "unit.h"
#include "lever.h"
#include "utils.h"
#include "view.h"
#include "undo.h"
#include <QTemporaryDir>
#include <QFile>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QTextStream>
#include <QScopedPointer>

int runDatabaseCreationCheck()
{
    QTextStream output(stdout);
    CValidationReport report;
    auto fail = [&](const QString& stage) {
        output << "FAIL: " << stage << "\n" << report.toText(); output.flush(); return 1;
    };
    QByteArray baseline;
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
    CView view(nullptr, nullptr);
    QTemporaryDir scratch;
    if(!scratch.isValid()) return fail("temporary directory");
    for(int category : {0, 2})
    {
        ei::SDatabaseMapEntry entry;
        entry.category = static_cast<ei::EDatabaseMapCategory>(category);
        entry.name = category == 0 ? "Test creature" : "Test lever";
        entry.model = "synthetic_model"; entry.primaryTexture = "synthetic_skin";
        entry.unitType = 51;
        CUnit unitDefaults; CLever leverDefaults;
        QJsonObject authored;
        if(!ei::applyDatabaseMapEntry(entry, category == 0 ? unitDefaults.toJson()
            : leverDefaults.toJson(), &authored)) return fail("database descriptor");
        QScopedPointer<CNode> node(category == 0 ? static_cast<CNode*>(new CUnit(authored))
                                                : static_cast<CNode*>(new CLever(authored)));
        QVector3D position(12, 18, 0); node->updatePos(position);
        QJsonObject prepared;
        if(!CMob::prepareFreshDatabaseNode(node.data(), &prepared, &report))
            return fail(category == 0 ? "fresh creature" : "fresh lever");
        if(node->mapId() != 0) return fail("preview identity changed");
        const auto provenance = prepared.value("_Endurance raw provenance").toObject();
        QByteArray raw = QByteArray::fromBase64(provenance.value("sourceSubtreeBase64").toString().toLatin1());
        QByteArray hash = QByteArray::fromHex(provenance.value("sourceSubtreeSha256").toString().toLatin1());
        if(raw.isEmpty() || QCryptographicHash::hash(raw, QCryptographicHash::Sha256) != hash)
            return fail("generated provenance");
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
        if(!second || !third || second->mapId() == id || third->mapId() == id
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
    output.flush(); return 0;
}
