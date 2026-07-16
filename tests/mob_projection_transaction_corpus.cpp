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
           !source.applyTransaction(derived, &derivedBytes, &report) ||
           derivedBytes != editedBytes)
        {
            output << "FAIL derive " << path << "\n" << report.toText();
            ++failed;
            continue;
        }
        ++passed;
    }

    output << "Projection transaction corpus: passed=" << passed
           << " failed=" << failed << " total=" << files.size() << "\n";
    return failed == 0 ? 0 : 1;
}
