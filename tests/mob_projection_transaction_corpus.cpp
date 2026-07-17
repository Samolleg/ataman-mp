#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include "endurance_document.h"

namespace {
const SMobSection* firstNonEmptyLeaf(const QVector<SMobSection>& sections)
{
    for(const SMobSection& section : sections)
    {
        if(section.container && !section.children.isEmpty())
        {
            if(const SMobSection* child = firstNonEmptyLeaf(section.children))
                return child;
        }
        else if(section.rawBytes.size() > 8)
            return &section;
    }
    return nullptr;
}

QStringList mobFiles(const QStringList& inputs)
{
    QSet<QString> unique;
    for(const QString& input : inputs)
    {
        const QFileInfo info(input);
        if(info.isFile() && info.suffix().compare("mob", Qt::CaseInsensitive) == 0)
            unique.insert(info.absoluteFilePath());
        else if(info.isDir())
        {
            QDirIterator files(info.absoluteFilePath(), QStringList() << "*.mob",
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
    const QStringList files = mobFiles(app.arguments().mid(1));
    if(files.isEmpty())
    {
        output << "Usage: mob_projection_transaction_corpus <mob-or-directory> [...]\n";
        return 2;
    }

    int passed = 0;
    int failed = 0;
    int subtreePassed = 0;
    int subtreeSkipped = 0;
    for(const QString& path : files)
    {
        CValidationReport report;
        CMobDocument source;
        if(!source.load(QFileInfo(path), &report))
        {
            output << "FAIL load " << path << "\n" << report.toText();
            ++failed;
            continue;
        }
        const SMobSection* leaf = firstNonEmptyLeaf(source.sections());
        if(!leaf)
        {
            output << "FAIL no non-empty leaf " << path << "\n";
            ++failed;
            continue;
        }

        if(!source.objects().isEmpty())
        {
            QStringList objectPaths;
            QVector<QByteArray> objectHashes;
            for(const SMobObjectSnapshot& object : source.objects())
            {
                objectPaths.append(object.sectionPath);
                objectHashes.append(object.subtreeSha256);
            }
            QByteArray rebuiltSequence;
            if(!source.rebuildObjectSequence(objectPaths, objectHashes,
                                             &rebuiltSequence, &report) ||
               rebuiltSequence != source.originalBytes())
            {
                output << "FAIL no-op object sequence " << path << "\n" << report.toText();
                ++failed;
                continue;
            }
        }
        QByteArray reconciledLogic;
        if(!source.reconcileLogicPointStructure(source, source,
                                                &reconciledLogic, &report) ||
           reconciledLogic != source.originalBytes())
        {
            output << "FAIL no-op logic structure " << path << "\n" << report.toText();
            ++failed;
            continue;
        }

        QByteArray replacement = leaf->rawBytes.mid(8);
        replacement[replacement.size() - 1] = char(replacement.back() ^ 0x01);
        CMobEditTransaction seed("corpus seed leaf mutation");
        seed.replaceLeafPayload(leaf->path, leaf->sha256, replacement);
        QByteArray editedBytes;
        if(!source.applyTransaction(seed, &editedBytes, &report))
        {
            output << "FAIL seed " << path << "\n" << report.toText();
            ++failed;
            continue;
        }

        CMobDocument editedProjection;
        if(!editedProjection.loadBytes(editedBytes, "edited-corpus-projection.mob", &report))
        {
            output << "FAIL edited parse " << path << "\n" << report.toText();
            ++failed;
            continue;
        }
        CMobEditTransaction derived("corpus projection delta");
        QByteArray derivedBytes;
        if(!source.deriveLeafTransaction(source, editedProjection, &derived, &report) ||
           derived.edits().size() != 1 ||
           !derived.validatedChangedSaveClass() ||
           !source.applyTransaction(derived, &derivedBytes, &report) ||
           derivedBytes != editedBytes)
        {
            output << "FAIL derive " << path << "\n" << report.toText();
            ++failed;
            continue;
        }

        if(source.objects().isEmpty())
            ++subtreeSkipped;
        else
        {
            const SMobObjectSnapshot& target = source.objects().front();
            QByteArray duplicatedBytes;
            QByteArray removedBytes;
            CMobDocument duplicated;
            CMobDocument removed;
            if(!source.duplicateObjectSubtree(target.sectionPath, target.subtreeSha256,
                                              &duplicatedBytes, &report) ||
               !duplicated.loadBytes(duplicatedBytes, "duplicated-corpus.mob", &report) ||
               duplicated.objects().size() != source.objects().size() + 1 ||
               duplicated.objects().at(0).subtreeSha256 != target.subtreeSha256 ||
               duplicated.objects().at(1).subtreeSha256 != target.subtreeSha256 ||
               !source.removeObjectSubtree(target.sectionPath, target.subtreeSha256,
                                           &removedBytes, &report) ||
               !removed.loadBytes(removedBytes, "removed-corpus.mob", &report) ||
               removed.objects().size() != source.objects().size() - 1)
            {
                output << "FAIL subtree " << path << "\n" << report.toText();
                ++failed;
                continue;
            }
            bool sequenceExact = true;
            for(int index = 1; index < source.objects().size(); ++index)
            {
                sequenceExact = sequenceExact &&
                    duplicated.objects().at(index + 1).subtreeSha256 ==
                        source.objects().at(index).subtreeSha256 &&
                    removed.objects().at(index - 1).subtreeSha256 ==
                        source.objects().at(index).subtreeSha256;
            }
            if(!sequenceExact)
            {
                output << "FAIL subtree sequence " << path << "\n";
                ++failed;
                continue;
            }
            ++subtreePassed;
        }
        ++passed;
    }

    output << "Projection transaction corpus: passed=" << passed
           << " failed=" << failed << " total=" << files.size()
           << " subtreePassed=" << subtreePassed
           << " subtreeSkipped=" << subtreeSkipped << "\n";
    return failed == 0 ? 0 : 1;
}
