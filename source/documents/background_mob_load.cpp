#include "background_mob_load.h"
#include "endurance_document.h"
#include "backup_manager.h"
#include "neighbor_scene_data.h"
#include <QFile>
#include <QCryptographicHash>

bool SPreparedMobLoad::sourceIsCurrent() const
{
    return !revision.isEmpty() ? neighborFileRevision(source.absoluteFilePath())==revision
        : neighborFileSha256(source.absoluteFilePath())==sha256;
}

SPreparedMobLoad prepareBackgroundMob(const QFileInfo& source,
                                     const std::shared_ptr<std::atomic_bool>& cancel)
{
    SPreparedMobLoad result;result.source=source;
    if(cancel->load()) {result.error="Cancelled";return result;}
    result.revision=neighborFileRevision(source.absoluteFilePath());
    QFile file(source.absoluteFilePath());
    if(!file.open(QIODevice::ReadOnly)){result.error=file.errorString();return result;}
    result.bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError){result.error=file.errorString();return result;}
    result.sha256=QCryptographicHash::hash(result.bytes,QCryptographicHash::Sha256);
    auto document=QSharedPointer<CMobDocument>::create();CValidationReport report;
    if(!document->loadBytes(result.bytes,source.absoluteFilePath(),&report)) {
        result.error=report.toText();return result;
    }
    for(const auto& finding:report.findings())
        result.warnings.append(finding.message);
    std::function<void(const QVector<SMobSection>&)> collect=[&](const QVector<SMobSection>& sections) {
        for(const auto& section:sections) {
            if(section.type==0xB000) {
                for(const auto& object:section.children)result.objects.append(object.rawBytes);
            } else collect(section.children);
        }
    };
    collect(document->sections());
    if(result.objects.size()!=document->objects().size()) {
        result.error="Unsupported object records in MOB projection.";return result;
    }
    if(cancel->load()||!result.sourceIsCurrent()){result.error="MOB changed or loading was cancelled.";return result;}
    const auto backup=CBackupManager().snapshotOnOpen(source);
    if(!backup.success || backup.sha256.toLatin1().toLower()!=result.sha256.toHex()) {
        result.error="MOB recovery snapshot failed or differs: "+backup.error;return result;
    }
    result.backupLease=backup.lease;
    if(cancel->load()||!result.sourceIsCurrent()){result.error="MOB changed or loading was cancelled.";return result;}
    result.document=document;
    return result;
}

SPreparedMobProjection validateBackgroundMobProjection(const SPreparedMobLoad& source,
                                                       const QByteArray& bytes)
{
    SPreparedMobProjection result;result.cleanBytes=bytes;
    CMobDocument projection;CValidationReport report;
    if(!projection.loadBytes(bytes,"background-mob-projection",&report)
       ||projection.objects().size()!=source.document->objects().size()) {
        result.error="Incomplete background MOB projection: "+report.toText();return result;
    }
    result.rawIndices.fill(-1,projection.objects().size());QSet<int> used;
    const auto alignment=CMobSemanticDiff::compare(*source.document,projection);
    for(const auto& entry:alignment.entries()) {
        if(entry.baselineIndex<0||entry.candidateIndex<0||entry.candidateIndex>=result.rawIndices.size()
           ||used.contains(entry.baselineIndex)||result.rawIndices[entry.candidateIndex]>=0) {
            result.rawIndices.clear();break;
        }
        result.rawIndices[entry.candidateIndex]=entry.baselineIndex;used.insert(entry.baselineIndex);
    }
    if(result.rawIndices.contains(-1)||used.size()!=projection.objects().size())result.rawIndices.clear();
    return result;
}
