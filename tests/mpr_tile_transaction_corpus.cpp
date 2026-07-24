#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include "endurance_document.h"
#include "mpr_document.h"
#include "terrain_edit_model.h"

namespace {
QStringList mprFiles(const QStringList& inputs)
{
    QSet<QString> unique;
    for(const QString& input : inputs)
    {
        const QFileInfo info(input);
        if(info.isFile() && info.suffix().compare("mpr", Qt::CaseInsensitive) == 0)
            unique.insert(info.absoluteFilePath());
        else if(info.isDir())
        {
            QDirIterator files(info.absoluteFilePath(), QStringList() << "*.mpr",
                               QDir::Files, QDirIterator::Subdirectories);
            while(files.hasNext())
                unique.insert(QFileInfo(files.next()).absoluteFilePath());
        }
    }
    QStringList result = unique.values();
    result.sort(Qt::CaseInsensitive);
    return result;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    const QStringList files = mprFiles(app.arguments().mid(1));
    if(files.isEmpty())
    {
        output << "Usage: mpr_tile_transaction_corpus <mpr-or-directory> [...]\n";
        return 2;
    }

    int passed = 0;
    int failed = 0;
    for(const QString& path : files)
    {
        CValidationReport report;
        CMprDocument baseline;
        if(!baseline.load(QFileInfo(path), &report) || report.hasErrors()
           || baseline.sectors().isEmpty())
        {
            output << "FAIL load " << path << "\n" << report.toText();
            ++failed;
            continue;
        }
        const SMprSectorInfo* sectorPointer = nullptr;
        for(const SMprSectorInfo& candidateSector : baseline.sectors())
        {
            if(candidateSector.type == 0 || candidateSector.type == 3)
            {
                sectorPointer = &candidateSector;
                break;
            }
        }
        if(!sectorPointer)
        {
            output << "FAIL no editable sector " << path << "\n";
            ++failed;
            continue;
        }
        const SMprSectorInfo& sector = *sectorPointer;
        CTerrainEditModel terrain;
        if(!terrain.load(baseline, &report) || report.hasErrors())
        {
            output << "FAIL terrain " << path << "\n" << report.toText();
            ++failed;
            continue;
        }

        const int globalX = sector.sectorX * 16;
        const int globalY = sector.sectorY * 16;
        bool available = false;
        const quint16 before = terrain.packedTile(
            globalX, globalY, ETerrainTileLayer::Land, &available);
        SMprPackedTile selected = SMprPackedTile::decode(before);
        selected.rotation = quint8((selected.rotation + 1) % 4);
        STerrainTileBrush point;
        point.centerX = float(globalX);
        point.centerY = float(globalY);
        const STerrainTileCommand command = terrain.makeTilePaint(
            point, ETerrainTileLayer::Land, selected.atlasIndex,
            selected.tileIndex, selected.rotation);
        CMprEditTransaction transaction("private in-memory tile rotation probe");
        QByteArray candidateBytes;
        CMprDocument candidate;
        CValidationReport candidateReport;
        const bool success = available && command.deltas.size() == 1
            && terrain.apply(command, true, &report)
            && terrain.appendSectorEdits(&transaction, &report)
            && transaction.edits().size() == 1
            && baseline.applyTransaction(transaction, &candidateBytes, &report)
            && candidate.loadBytes(candidateBytes, QFileInfo(path).fileName() + ".candidate",
                                   &candidateReport)
            && !report.hasErrors() && !candidateReport.hasErrors();
        if(!success)
        {
            output << "FAIL edit " << path << "\n" << report.toText()
                   << candidateReport.toText();
            ++failed;
            continue;
        }

        QByteArray localized = candidate.memberPayload(sector.memberName);
        const QByteArray original = baseline.memberPayload(sector.memberName);
        localized.replace(int(sector.landTileOffset), 2,
                          original.mid(int(sector.landTileOffset), 2));
        CTerrainEditModel reopened;
        bool reopenedAvailable = false;
        const bool exact = localized == original
            && CMprSemanticDiff::compare(baseline, candidate).entries().size() == 1
            && reopened.load(candidate, &candidateReport)
            && reopened.packedTile(globalX, globalY, ETerrainTileLayer::Land,
                                   &reopenedAvailable) == selected.encode()
            && reopenedAvailable && !candidateReport.hasErrors();
        if(!exact)
        {
            output << "FAIL locality " << path << "\n" << candidateReport.toText();
            ++failed;
            continue;
        }
        output << "PASS " << QFileInfo(path).fileName()
               << " member=" << sector.memberName << "\n";
        ++passed;
    }
    output << "SUMMARY passed=" << passed << " failed=" << failed << "\n";
    return failed == 0 ? 0 : 1;
}
