#include "diplomacy_analysis.h"
#include "endurance_document.h"
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <cmath>

namespace diplomacy {
namespace {
QString identity(const QString& source, const QString& npc) {
    return scenario::normalizedSource(source) + "|" + npc;
}
int groupArgument(const scenario::Reference& ref, int index) {
    if(!ref.complete || index<0 || index>=ref.arguments.size()) return -1;
    const auto& arg=ref.arguments[index];
    return arg.hasNumber && arg.number>=0 && arg.number<32 && arg.number==std::floor(arg.number)
        ? int(arg.number) : -1;
}
}
Result analyze(const QVector<Source>& sources)
{
    Result result; result.sources=sources;
    scenario::Request request;
    for(const auto& src:sources) {
        scenario::Source s; s.key=src.key; s.text=src.script;
        s.objects=src.objects;
        if(s.objects.isEmpty()) for(const auto& npc:src.npcs) if(npc.hasId && !npc.scripted) {
            scenario::Object obj; obj.key=npc.key; obj.id=npc.id; obj.name=npc.name;
            s.objects.append(obj);
        }
        request.sources.append(s); request.familyRoots.append(s.key);
    }
    const auto analysis=scenario::analyze(request);
    // Index by source + object key; IDs alone may collide across opened MOBs.
    QMap<QString,QPair<int,int>> indexes;
    QMap<QString,QVector<QPair<int,int>>> names;
    QMap<QString,int> otherNames;
    for(const auto& src:sources) {
        QSet<QString> npcKeys; for(const auto& unit:src.npcs) npcKeys.insert(unit.key);
        for(const auto& obj:src.objects)
            if(!npcKeys.contains(obj.key) && !obj.name.isEmpty()) ++otherNames[obj.name.toCaseFolded()];
    }
    for(int s=0;s<result.sources.size();++s) for(int n=0;n<result.sources[s].npcs.size();++n) {
        const auto& npc=result.sources[s].npcs[n];
        indexes.insert(identity(result.sources[s].key,npc.key),{s,n});
        if(!npc.name.isEmpty()) names[npc.name.toCaseFolded()].append({s,n});
    }
    for(const auto& ref:analysis.references) if(ref.kind==scenario::Kind::Spawn) {
        Event event; event.reference=ref;
        if(ref.complete && ref.arguments.size()==6 && !ref.spawnName.isEmpty()) {
            for(int s=0;s<result.sources.size();++s) if(result.sources[s].key==ref.sourceKey) {
                Npc npc; npc.key=QString("spawn:%1").arg(ref.offset); npc.name=ref.spawnName;
                npc.prototype=ref.prototype; npc.scripted=true; npc.hasId=false; npc.line=ref.line;
                const int n=result.sources[s].npcs.size();
                result.sources[s].npcs.append(npc);
                indexes.insert(identity(ref.sourceKey,npc.key),{s,n});
                names[npc.name.toCaseFolded()].append({s,n});
                event.npcKey=npc.key; event.npcSource=ref.sourceKey;
                break;
            }
        }
        result.events.append(event);
    }
    for(const auto& ref:analysis.references) {
        if(ref.command!="SetPlayer" && ref.command!="SetDiplomacy" && ref.command!="GetDiplomacy"
           && !ref.command.startsWith("Group") && ref.kind!=scenario::Kind::Dependency) continue;
        Event event; event.reference=ref;
        if(ref.command=="SetPlayer") {
            event.group=ref.arguments.size()==2?groupArgument(ref,1):-1;
            if(ref.complete && ref.arguments.size()==2 && !ref.targets.isEmpty()) {
                const auto& target=ref.targets[0];
                const QString key=identity(target.sourceKey,target.objectKey);
                QPair<int,int> candidate(-1,-1);
                if(scenario::navigable(target.resolution) || target.resolution==scenario::Resolution::Virtual) {
                    if(indexes.contains(key) && (target.resolution!=scenario::Resolution::Virtual
                        || (names.value(target.name.toCaseFolded()).size()==1
                            && !otherNames.contains(target.name.toCaseFolded())))) candidate=indexes.value(key);
                }
                // GetObjectByName may also name a placed NPC. Require unique
                // identity among all inspected sources, including spawn sites.
                else if(analysis.familyComplete && target.expression.startsWith("GetObjectByName",Qt::CaseInsensitive)) {
                    // Nested lookup references carry the decoded literal target
                    // expression; use their lexer metadata instead of regexes.
                    for(const auto& lookup:analysis.references) if(lookup.sourceKey==ref.sourceKey
                        && lookup.command=="GetObjectByName" && lookup.offset>=ref.offset
                        && lookup.offset<ref.offset+ref.length && lookup.arguments.size()==1
                        && lookup.arguments[0].hasString) {
                        const auto options=names.value(lookup.arguments[0].text.toCaseFolded());
                        if(options.size()==1 && !otherNames.contains(lookup.arguments[0].text.toCaseFolded())) candidate=options[0];
                        break;
                    }
                }
                if(candidate.first>=0) {
                    auto& npc=result.sources[candidate.first].npcs[candidate.second];
                    event.npcKey=npc.key; event.npcSource=result.sources[candidate.first].key;
                    if(event.group>=0) npc.possibleGroups.insert(event.group);
                }
            }
        }
        result.events.append(event);
    }
    return result;
}

Result inspectConnected(const Request& request)
{
    QVector<Source> sources=request.sources;
    QStringList diagnostics;
    QSet<QString> seen;
    QMap<QString,QStringList> folders;
    for(const auto& src:sources) seen.insert(scenario::normalizedSource(src.key));
    const auto resolve=[&](const QString& name,const QString& requester) {
        const QString file=QFileInfo(name).fileName();
        const QString logical=file.endsWith(".mob",Qt::CaseInsensitive)?file:file+".mob";
        QStringList loaded;
        for(const auto& src:sources) if(QFileInfo(src.key).fileName().compare(logical,Qt::CaseInsensitive)==0)
            loaded.append(src.key);
        if(loaded.size()==1) return loaded[0];
        if(loaded.size()>1) return QString();
        QStringList roots={QFileInfo(requester).absolutePath()}; roots.append(request.roots);
        roots.removeDuplicates();
        for(const auto& root:roots) {
            if(!folders.contains(root)) folders.insert(root,QDir(root).entryList({"*.mob"},QDir::Files));
            for(const auto& entry:folders.value(root)) if(entry.compare(logical,Qt::CaseInsensitive)==0)
                return QDir(root).absoluteFilePath(entry);
        }
        return QString();
    };
    const auto read=[&](const QString& path,const QString& reason) {
        const QString key=scenario::normalizedSource(path);
        if(seen.contains(key)) return;
        seen.insert(key);
        CMobDocument doc; CValidationReport report;
        if(!doc.load(QFileInfo(path),&report)) {
            diagnostics.append(QObject::tr("Cannot read MOB: %1").arg(path)); return;
        }
        Source source; source.key=path; source.reason=reason;
        doc.embeddedScript(&source.script,nullptr);
        doc.diplomacyTable(&source.relations,&source.groupNames,nullptr);
        for(const auto& obj:doc.objects()) {
            if(obj.hasNid) {
                scenario::Object identity; identity.key="raw:"+obj.sectionPath;
                identity.id=obj.nid; identity.name=obj.name; source.objects.append(identity);
            }
            if(!obj.unit) continue;
            Npc npc; npc.key="raw:"+obj.sectionPath; npc.name=obj.name; npc.prototype=obj.prototype;
            npc.id=obj.nid; npc.hasId=obj.hasNid; npc.group=obj.hasPlayer?int(obj.player):-1;
            source.npcs.append(npc);
        }
        sources.append(source);
    };
    for(const auto& path:request.extraSources) if(!path.isEmpty()) {
        int live=0;
        for(const auto& src:request.sources) if(QFileInfo(src.key).fileName().compare(
            QFileInfo(path).fileName(),Qt::CaseInsensitive)==0) ++live;
        // A workspace copy of a primary MOB overrides its installed baseline.
        // Multiple explicit live copies remain separate rows, never guessed.
        if(live==0) read(path,QObject::tr("Map / profile MOB"));
        else if(live>1) diagnostics.append(QObject::tr("Ambiguous loaded MOB: %1").arg(QFileInfo(path).fileName()));
    }
    for(int s=0;s<sources.size();++s) {
        // A corrupted or computed chain must not scan an unlimited directory tree.
        if(s>=1024) { diagnostics.append(QObject::tr("MOB dependency limit reached; result incomplete")); break; }
        const Source src=sources[s];
        scenario::Request calls; scenario::Source script; script.key=src.key; script.text=src.script;
        calls.sources.append(script); calls.familyRoots.append(script.key);
        const auto parsed=scenario::analyze(calls);
        for(const auto& diagnostic:parsed.diagnostics)
            // This first pass has only one script; its dependency-family
            // warning is expected and resolved by the traversal below.
            if(diagnostic!=QObject::tr("Family incomplete: missing, dynamic or ambiguous MOB dependencies"))
                diagnostics.append(QFileInfo(src.key).fileName()+": "+diagnostic);
        for(const auto& ref:parsed.references) if(ref.kind==scenario::Kind::Dependency) {
            if(!ref.complete || ref.arguments.size()!=1 || !ref.arguments[0].hasString) {
                diagnostics.append(QObject::tr("Unresolved AddMob: %1:%2").arg(QFileInfo(src.key).fileName()).arg(ref.line)); continue;
            }
            const auto path=resolve(ref.arguments[0].text,src.key);
            if(path.isEmpty()) {
                diagnostics.append(QObject::tr("Missing or ambiguous MOB %1 (%2:%3)")
                    .arg(ref.arguments[0].text,QFileInfo(src.key).fileName()).arg(ref.line)); continue;
            }
            read(path,QString("AddMob · %1:%2").arg(QFileInfo(src.key).fileName()).arg(ref.line));
        }
    }
    auto result=analyze(sources); result.diagnostics=diagnostics; return result;
}
}
