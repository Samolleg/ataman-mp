// Production UI and publication regression. The optional private fixture is
// read only; all publications and preferences belong to QTemporaryDir.
#include <QtWidgets>
#include <QtTest>
#include <QtOpenGL>
#include <QtEndian>
#define private public
#include "view.h"
#undef private
#include "settings.h"
#include "resourcemanager.h"
#include "progressview.h"
#include "tree_view.h"
#include "tile_form.h"
#include "landscape.h"
#include "landscape_tools.h"
#include "terrain_atlas_compact.h"
#include "terrain_atlas_archive_binding.h"
#include "res_archive_index.h"
#include "endurance_document.h"
#include "mmp_codec.h"
#include "terrain_semantic_field_store.h"

namespace {
class Hidden : public QObject {
    bool eventFilter(QObject* object, QEvent* event) override {
        if(event->type() == QEvent::Polish || event->type() == QEvent::Show)
            if(auto* widget = qobject_cast<QWidget*>(object))
                if(widget->isWindow()) widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
QTextStream output(stdout);
int checks = 0;
void check(bool ok, const QString& description) {
    output << (ok ? "PASS " : "FAIL ") << description << '\n'; output.flush();
    if(!ok) throw description;
    ++checks;
}
QByteArray read(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "write owned " + QFileInfo(path).fileName());
}
QString pageName(const QString& name, int page) {
    return QString("%1%2.mmp").arg(name).arg(page, 3, 10, QLatin1Char('0'));
}
QByteArray archiveBytes(const QVector<SResArchiveNewMember>& members) {
    QByteArray empty(16, '\0'); QDataStream header(&empty, QIODevice::WriteOnly);
    header.setByteOrder(QDataStream::LittleEndian);
    header << quint32(0x019ce23c) << quint32(0) << quint32(16) << quint32(0);
    CResArchiveDocument archive; QStringList errors; QByteArray bytes;
    check(archive.loadBytes(empty, "owned-empty", &errors)
          && archive.buildWithAppendedMembers(members, &bytes, &errors),
          "build owned source archive: " + errors.join(';'));
    return bytes;
}
}

int runAssemblyCatalogCheck() {
    output.setCodec("UTF-8");
    const QString fixture = qEnvironmentVariable("EI_COMPACT_SOURCE_FIXTURE");
    if(fixture.isEmpty()) { output << "SKIP explicit private fixture unavailable\n"; return 2; }
    try {
        QTemporaryDir scratch;
        check(scratch.isValid(), "owned scratch directory");
        const QString previousCurrent = QDir::currentPath();
        QDir::setCurrent(scratch.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, scratch.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, scratch.path());
        qputenv("EI_MAPPER_BACKUP_ROOT", (scratch.path() + "/backups").toUtf8());
        Hidden hidden; qApp->installEventFilter(&hidden);
        QDir().mkpath(scratch.path() + "/Maps");
        const QString mapPath = scratch.path() + "/Maps/source.mpr";
        const QString archivePath = scratch.path() + "/Maps/source-atlas.res";
        QByteArray sourceBytes = read(fixture + "/source.mpr");
        write(mapPath, sourceBytes);
        CMprDocument source; CValidationReport report;
        check(source.loadBytes(sourceBytes, mapPath, &report) && !report.hasErrors(),
              "open exact source map copy");
        QVector<SResArchiveNewMember> members;
        for(int page = 0; page < int(source.mapInfo().textureCount); ++page) {
            SResArchiveNewMember member;
            member.name = pageName(source.mapInfo().innerNamespace, page);
            member.payload = read(fixture + '/' + member.name); member.lastWriteTime = 123456;
            check(!member.payload.isEmpty(), "read exact source page " + QString::number(page));
            members << member;
        }
        SResArchiveNewMember sentinel;
        sentinel.name = "unrelated.bin"; sentinel.payload = "preserve unrelated resource";
        sentinel.lastWriteTime = 654321; members << sentinel;
        QByteArray originalArchive = archiveBytes(members);
        write(archivePath, originalArchive);

        auto* settings = new CSettings;
        dynamic_cast<COptString*>(settings->opt("resourceProfile"))->setValue("deja-vu");
        dynamic_cast<COptString*>(settings->opt("gameRoot"))->setValue(scratch.path());
        dynamic_cast<COptStringList*>(settings->opt(eOptSetResource, "resourceRoots"))
            ->setValue({scratch.path() + "/Maps"});
        CTextureList::getInstance()->attachSettings(settings);
        auto* view = new CView(nullptr, nullptr);
        auto* history = new QUndoStack; auto* table = new QTableWidget; table->setColumnCount(2);
        auto* picker = new CTileForm;
        view->attach(settings, table, history, new CProgressView, new QLineEdit, new CTreeView, picker);
        view->setAttribute(Qt::WA_DontShowOnScreen); view->resize(640,480); view->show();
        QCoreApplication::processEvents(); view->makeCurrent();
        check(view->isValid(), "hidden production WGL context");
        CTextureList::getInstance()->initResource();
        view->attachLandscapeTools(new CLandscapeTools);
        QStringList messages; QList<QMessageBox::Icon> icons; bool confirm = false;
        QTimer dialogs; QObject::connect(&dialogs, &QTimer::timeout, [&](){
            for(auto* widget : QApplication::topLevelWidgets())
                if(auto* box = qobject_cast<QMessageBox*>(widget)) {
                    if(!box->isVisible()) continue;
                    messages << box->text(); icons << box->icon();
                    output << "DIALOG " << box->text() << '\n'; output.flush();
                    if(auto* yes = box->button(QMessageBox::Yes)) {
                        if(confirm) yes->click(); else box->button(QMessageBox::No)->click();
                    }
                    else box->accept();
                }
        }); dialogs.start(20);
        view->loadLandscape(QFileInfo(mapPath), false); view->makeCurrent();
        check(view->ensureTerrainAutoCatalogReady() && view->m_terrainAtlasAnalysis.usable,
              "open unedited map with usable atlas profile");
        check(!view->m_terrainEdit.isDirty() && !view->mprParametersDirty()
              && !view->hasDirtyWorldTerrain(), "original map is clean");
        CResArchiveIndex archive; QString error;
        check(archive.open(archivePath, &error)
              && archive.memberIndex(CTerrainAtlasProfileStore::embeddedMemberName(
                     source.mapInfo().innerNamespace)) < 0, "source has no embedded profile");
        messages.clear(); icons.clear();
        view->compactTerrainAtlasRequested();
        check(icons.size() == 1 && icons.first() == QMessageBox::Information,
              "exact original compact atlas reports no-op without false save request");
        check(read(mapPath) == sourceBytes && read(archivePath) == originalArchive,
              "original compact atlas does not publish duplicate pages");

        // Add one explicitly unused duplicate page to a private copy. The
        // user's exact original above has no removable slots; this controlled
        // extension exercises real compaction and its confirmation/cancel path.
        const auto originalProfile = view->m_terrainAtlasProfile;
        QVector<QByteArray> originalPages;
        for(int page = 0; page < int(source.mapInfo().textureCount); ++page)
            originalPages << members[page].payload;
        const QByteArray noPublishedProfile;
        const auto forced = prepareTerrainAtlasCompact(source, originalProfile, originalPages, {},
            QFileInfo(mapPath), nullptr, true, nullptr, &noPublishedProfile);
        CMprDocument forcedMap;
        check(forced.error.isEmpty() && !forced.noOp
              && forcedMap.loadBytes(forced.mprBytes, mapPath)
              && forcedMap.mapInfo().textureSize == quint32(picker->tileImages().first().width()*8),
              "forced save publication still creates owned identity with native MP sizes");
        view->unloadLand(false);
        const auto originalMap = source.mapInfo();
        const QByteArray originalMp = source.memberPayload(originalMap.memberName);
        QByteArray mp = originalMp, additionalTypes;
        QDataStream types(&additionalTypes, QIODevice::WriteOnly);
        types.setByteOrder(QDataStream::LittleEndian);
        for(int slot = 0; slot < 64; ++slot) types << originalMap.tileTypes.value(slot);
        mp.insert(38 + int(originalMap.materialCount)*44 + int(originalMap.tileCount)*4, additionalTypes);
        qToLittleEndian<quint32>(originalMap.textureCount+1, reinterpret_cast<uchar*>(mp.data()+16));
        qToLittleEndian<quint32>(originalMap.tileCount+64, reinterpret_cast<uchar*>(mp.data()+24));
        CMprEditTransaction extend("controlled unused atlas page");
        extend.replaceMemberPayload(originalMap.memberName,
            QCryptographicHash::hash(originalMp, QCryptographicHash::Sha256).toHex(), mp);
        check(source.applyTransaction(extend, &sourceBytes, &report)
              && source.loadBytes(sourceBytes, mapPath, &report), "create private unused-page map variant");
        auto duplicate = members.first();
        duplicate.name = pageName(originalMap.innerNamespace, originalMap.textureCount);
        members.insert(int(originalMap.textureCount), duplicate);
        originalArchive = archiveBytes(members); write(mapPath, sourceBytes); write(archivePath, originalArchive);
        view->makeCurrent(); view->loadLandscape(QFileInfo(mapPath), false); view->makeCurrent();
        check(view->ensureTerrainAutoCatalogReady(), "open controlled unused-page copy");
        auto expandedProfile = originalProfile;
        expandedProfile.textureCount = originalMap.textureCount+1;
        expandedProfile.atlasSlotCount = expandedProfile.textureCount*64;
        expandedProfile.atlasPixelSha256 = CTerrainAtlasProfileInference::pixelSha256(picker->tileImages());
        for(int slot = originalProfile.atlasSlotCount; slot < expandedProfile.atlasSlotCount; ++slot) {
            auto unused = originalProfile.slotDefinitions.first(); unused.slot = slot;
            unused.assigned = false; unused.paintSelectable = false; unused.referencePaintSelectable = false;
            expandedProfile.slotDefinitions << unused;
        }
        check(expandedProfile.normalizeMembership(), "extended page explicitly carries no paint seeds");
        view->m_terrainAtlasProfile = expandedProfile;
        messages.clear(); icons.clear(); view->compactTerrainAtlasRequested();
        check(icons.size() == 1 && icons.first() == QMessageBox::Question,
              "clean unpublished atlas with removable page reaches confirmation");
        check(read(mapPath) == sourceBytes && read(archivePath) == originalArchive,
              "cancel leaves removable-page MPR and RES byte exact");

        const auto materials = view->m_mprMaterials;
        check(!materials.isEmpty(), "source map has materials for dirty regression");
        view->m_mprMaterials.append(materials.first()); messages.clear(); icons.clear();
        view->compactTerrainAtlasRequested();
        check(icons.size() == 1 && icons.first() == QMessageBox::Warning,
              "genuine unsaved map change still refuses compaction");
        check(read(mapPath) == sourceBytes && read(archivePath) == originalArchive,
              "dirty refusal writes no map or resource data");
        view->m_mprMaterials = materials;

        const auto profile = view->m_terrainAtlasProfile;
        const QString profileName = CTerrainAtlasProfileStore::embeddedMemberName(profile.mprNamespace);
        const auto withProfile = [&](const QByteArray& bytes) {
            auto result = members; SResArchiveNewMember member;
            member.name = profileName; member.payload = bytes.isEmpty() ? QByteArray("x") : bytes;
            member.lastWriteTime = 567890; result << member;
            QByteArray archive = archiveBytes(result);
            if(bytes.isEmpty()) {
                const int count = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(archive.constData()+4));
                const int table = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(archive.constData()+8));
                const int names = table + count*22;
                for(int index = 0; index < count; ++index) {
                    const int row = table+index*22;
                    const int length = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(archive.constData()+row+16));
                    const int offset = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(archive.constData()+row+18));
                    if(archive.mid(names+offset, length) == profileName.toLatin1())
                        qToLittleEndian<quint32>(0, reinterpret_cast<uchar*>(archive.data()+row+4));
                }
            }
            return archive;
        };
        auto changed = profile; changed.provenance += ":external-edit";
        const QByteArray changedProfile = changed.toBytes();
        for(const QByteArray& bytes : {changedProfile, QByteArray("invalid-json"), QByteArray("")}) {
            const QByteArray external = withProfile(bytes); write(archivePath, external);
            messages.clear(); icons.clear(); view->compactTerrainAtlasRequested();
            check(icons.size() == 1 && icons.first() == QMessageBox::Warning,
                  "changed or malformed published profile still refuses in UI");
            check(read(mapPath) == sourceBytes && read(archivePath) == external,
                  "profile refusal preserves exact external files");
        }
        write(archivePath, originalArchive);
        const QByteArray canonical = profile.toBytes();
        const QByteArray formatted = profile.toBytes(false);
        QJsonObject legacyObject = profile.toJson(); legacyObject.insert("schemaVersion", 9);
        const QByteArray legacy = QJsonDocument(legacyObject).toJson();
        check(terrainCompactProfileMatchesPublished(profile, canonical)
              && terrainCompactProfileMatchesPublished(profile, formatted)
              && terrainCompactProfileMatchesPublished(profile, legacy),
              "profile comparison accepts canonical, formatted and migrated equivalent data");
        check(!terrainCompactProfileMatchesPublished(profile, changedProfile)
              && !terrainCompactProfileMatchesPublished(profile, QByteArray())
              && !terrainCompactProfileMatchesPublished(profile, "invalid-json"),
              "profile comparison rejects changed, empty and malformed members");
        QVector<QByteArray> pages;
        for(int page = 0; page < int(source.mapInfo().textureCount); ++page)
            pages << members[page].payload;
        const QByteArray absent;
        const auto plan = prepareTerrainAtlasCompact(source, profile, pages, {}, QFileInfo(mapPath),
                                                      nullptr, false, nullptr, &absent);
        check(plan.error.isEmpty() && !plan.noOp && !plan.sourceProfilePresent
              && plan.pages.size() == int(originalMap.textureCount),
              "prepare unpublished source with exact absent-profile snapshot: " + plan.error);
        const auto mismatch = prepareTerrainAtlasCompact(source, profile, pages, {}, QFileInfo(mapPath),
                                                         nullptr, false, nullptr, &changedProfile);
        check(!mismatch.error.isEmpty(), "preparation rejects different published profile");
        CMprDocument candidate; CValidationReport candidateReport;
        check(candidate.loadBytes(plan.mprBytes, mapPath, &candidateReport) && !candidateReport.hasErrors(),
              "candidate structurally reopens");
        bool pixels = true;
        for(auto it = plan.remap.cbegin(); it != plan.remap.cend(); ++it) {
            QImage image;
            pixels &= ei::decodeMmpDxt1AtlasTile(pages[it.key()/64], it.key()%64, &image, &error)
                && image == plan.images[it.value()]
                && source.mapInfo().tileTypes.value(it.key())
                    == candidate.mapInfo().tileTypes.value(it.value());
        }
        check(pixels, "all retained decoded pixels and runtime terrain types are exact");
        bool sectors = source.sectors().size() == candidate.sectors().size();
        for(const auto& oldSector : source.sectors()) {
            SMprSectorInfo newSector; bool found = false;
            for(const auto& sector : candidate.sectors())
                if(sector.sectorX == oldSector.sectorX && sector.sectorY == oldSector.sectorY) {
                    newSector = sector; found = true; break;
                }
            if(!found) { sectors = false; continue; }
            auto oldBytes = source.members()[oldSector.memberIndex].payload;
            const auto newBytes = candidate.members()[newSector.memberIndex].payload;
            for(int layer = 0; layer < 2; ++layer) {
                if(layer && !oldSector.hasOverlay) continue;
                const int offset = int(layer ? oldSector.overlayTileOffset : oldSector.landTileOffset);
                for(int cell = 0; cell < 256; ++cell) {
                    if(layer && oldSector.overlayMaterialIndices[cell] < 0) continue;
                    auto tile = SMprPackedTile::decode(qFromLittleEndian<quint16>(
                        reinterpret_cast<const uchar*>(oldBytes.constData()+offset+cell*2)));
                    const int slot = plan.remap.value(tile.atlasSlot(), -1);
                    sectors &= slot >= 0; tile.atlasIndex = slot/64; tile.tileIndex = slot%64;
                    qToLittleEndian<quint16>(tile.encode(),
                        reinterpret_cast<uchar*>(oldBytes.data()+offset+cell*2));
                }
            }
            sectors &= oldBytes == newBytes;
        }
        check(sectors && source.mapInfo().materials == candidate.mapInfo().materials
              && source.mapInfo().maxZ == candidate.mapInfo().maxZ,
              "every SEC byte except deliberate slot remap, all materials and height parameters preserved");

        const QStringList directories{scratch.path() + "/Maps"};
        const auto commit = [&](const STerrainAtlasCompactPlan& prepared) {
            return commitTerrainAtlasCompact(source, profile, prepared, mapPath, archivePath,
                directories, scratch.path() + "/direct-backups", scratch.path() + "/recovery");
        };
        const QByteArray published = withProfile(canonical);
        write(archivePath, published);
        const auto added = commit(plan);
        check(!added.success && added.error.contains("profile")
              && read(mapPath) == sourceBytes && read(archivePath) == published,
              "profile added after preview refuses commit without writing");
        const auto existingPlan = prepareTerrainAtlasCompact(source, profile, pages, {}, QFileInfo(mapPath),
                                                             nullptr, false, nullptr, &canonical);
        check(existingPlan.error.isEmpty() && existingPlan.sourceProfilePresent,
              "published canonical source prepares");
        for(const QByteArray& replacement : {originalArchive, withProfile(formatted), withProfile(changedProfile)}) {
            write(archivePath, replacement); const auto refused = commit(existingPlan);
            check(!refused.success && refused.error.contains("profile")
                  && read(mapPath) == sourceBytes && read(archivePath) == replacement,
                  "profile removal or byte replacement after preview refuses commit");
        }
        write(archivePath, originalArchive);
        write(mapPath, sourceBytes + "external-map-change"); const auto mapChanged = commit(plan);
        check(!mapChanged.success && read(mapPath) == sourceBytes + "external-map-change"
              && read(archivePath) == originalArchive, "external map change refuses commit and is preserved");
        write(mapPath, sourceBytes);
        auto corruptMembers = members; corruptMembers[plan.remap.firstKey()/64].payload.chop(1);
        const QByteArray corruptArchive = archiveBytes(corruptMembers); write(archivePath, corruptArchive);
        const auto pageChanged = commit(plan);
        check(!pageChanged.success && read(mapPath) == sourceBytes && read(archivePath) == corruptArchive,
              "external mip truncation refuses commit without writing");
        auto modifiedMembers = members;
        modifiedMembers.first().payload[76] = char(uchar(modifiedMembers.first().payload[76]) ^ 0x7f);
        const QByteArray modifiedArchive = archiveBytes(modifiedMembers); write(archivePath, modifiedArchive);
        const auto sameSizeChanged = commit(plan);
        check(!sameSizeChanged.success && read(mapPath) == sourceBytes && read(archivePath) == modifiedArchive,
              "same-size encoded source-page edit refuses commit without writing");
        write(archivePath, originalArchive);

        STerrainSemanticFieldDocument field;
        field.mprNamespace = profile.mprNamespace;
        field.terrainTilesSha256 = STerrainSemanticFieldDocument::tileBindingSha256(source);
        field.atlasPixelSha256 = QByteArray::fromHex(profile.atlasPixelSha256);
        field.profileSha256 = QCryptographicHash::hash(legacy, QCryptographicHash::Sha256);
        field.vertexWidth = source.mapInfo().sectorCountX*16+1;
        field.vertexHeight = source.mapInfo().sectorCountY*16+1;
        field.land.fill(profile.families.first().id, field.vertexWidth*field.vertexHeight);
        field.overlay = field.land;
        auto semanticPlan = prepareTerrainAtlasCompact(source, profile, pages, field.toBytes(), QFileInfo(mapPath),
                                                       nullptr, false, nullptr, &legacy);
        check(semanticPlan.error.isEmpty() && semanticPlan.sourceProfileBytes == legacy,
              "legacy serialization retains exact source snapshot and semantic binding: " + semanticPlan.error);
        CMprDocument semanticCandidate; STerrainSemanticFieldDocument rebound;
        check(semanticCandidate.loadBytes(semanticPlan.mprBytes, mapPath)
              && STerrainSemanticFieldDocument::fromBytes(semanticCandidate.memberPayload(
                     STerrainSemanticFieldDocument::embeddedMemberName()), &rebound)
              && rebound.land == field.land && rebound.overlay == field.overlay,
              "migration preserves both complete shared-corner fields");
        field.profileSha256 = QByteArray(32, 'x');
        const auto badSemantic = prepareTerrainAtlasCompact(source, profile, pages, field.toBytes(), QFileInfo(mapPath),
                                                            nullptr, false, nullptr, &legacy);
        check(!badSemantic.error.isEmpty(), "semantic field from a different profile is rejected");
        write(archivePath, withProfile(legacy));
        const auto legacyCommit = commit(semanticPlan);
        check(legacyCommit.success && read(mapPath) == semanticPlan.mprBytes,
              "legacy profile and semantic fields publish successfully: " + legacyCommit.error);
        write(mapPath, sourceBytes); write(archivePath, originalArchive);

        messages.clear(); icons.clear(); confirm = true;
        view->compactTerrainAtlasRequested(); confirm = false;
        check(icons.size() == 2 && icons[0] == QMessageBox::Question
              && icons[1] == QMessageBox::Information,
              "original no-profile UI scenario confirms, publishes and reports success");
        check(read(mapPath) == plan.mprBytes && view->m_mprDocument.originalBytes() == plan.mprBytes
              && !view->m_terrainEdit.isDirty() && !view->mprParametersDirty(),
              "UI adopts exact compacted baseline as clean map");
        const auto binding = CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive(
            {archivePath}, plan.profile.mprNamespace, archivePath);
        check(binding.success() && binding.profile.toBytes() == plan.profile.toBytes(),
              "published profile and all pages freshly resolve: " + binding.error);
        check(archive.open(archivePath, &error) && archive.readMember(sentinel.name) == sentinel.payload
              && archive.member(archive.memberIndex(sentinel.name)).lastWriteTime == sentinel.lastWriteTime,
              "unrelated resource payload and timestamp preserved");
        bool originals = true;
        for(int page = 0; page < pages.size(); ++page)
            originals &= archive.readMember(pageName(profile.mprNamespace, page)) == pages[page];
        check(originals, "original unowned atlas pages retained byte exact");
        messages.clear(); icons.clear();
        const QByteArray compactMap = read(mapPath), compactArchive = read(archivePath);
        view->compactTerrainAtlasRequested();
        check(icons.size() == 1 && icons.first() == QMessageBox::Information
              && read(mapPath) == compactMap && read(archivePath) == compactArchive,
              "repeat UI compaction is no-op without file changes");
        view->unloadLand(false); view->makeCurrent(); view->loadLandscape(QFileInfo(mapPath), false);
        check(view->ensureTerrainAutoCatalogReady() && view->m_terrainAtlasAnalysis.usable
              && view->m_terrainAtlasProfile.toBytes() == plan.profile.toBytes(),
              "compacted map reopens with usable exact profile");
        output << "RESULT pages=" << plan.oldPages << " -> " << plan.pages.size()
               << " retainedSlots=" << plan.retainedSlots << '\n';
        dialogs.stop(); view->unloadLand(false); view->hide();
        qApp->removeEventFilter(&hidden); QDir::setCurrent(previousCurrent);
        output << "RESULT " << checks << " checks, nativeAttempts=0\n"; output.flush();
        return 0;
    } catch(const QString& error) {
        output << "RESULT FAIL " << error << '\n'; output.flush(); return 1;
    }
}
