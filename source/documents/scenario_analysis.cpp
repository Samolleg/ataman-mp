#include "scenario_analysis.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace scenario {
QString normalizedSource(const QString& key) { return QDir::cleanPath(key).toCaseFolded(); }
QString normalizedText(QString text) {
    text.replace("\r\n", "\n"); text.replace('\r', '\n'); return text;
}
bool navigable(Resolution r) { return r == Resolution::Local || r == Resolution::Family; }
QString resolutionLabel(Resolution value) {
    switch(value) {
    case Resolution::Local: return QObject::tr("Placed in source MOB");
    case Resolution::Family: return QObject::tr("MOB family participant");
    case Resolution::Virtual: return QObject::tr("Virtual participant (static prediction)");
    case Resolution::External: return QObject::tr("Outside the active MOB family");
    case Resolution::Ambiguous: return QObject::tr("Ambiguous identity; no automatic target");
    case Resolution::Missing: return QObject::tr("Target not found in inspected sources");
    case Resolution::Dynamic: return QObject::tr("Dynamic expression; target unresolved");
    case Resolution::Invalid: return QObject::tr("Invalid or inexact object ID");
    case Resolution::Incomplete: return QObject::tr("Incomplete family; identity not confirmed");
    }
    return QString();
}
QString kindLabel(Kind value) {
    switch(value) {
    case Kind::Lookup: return QObject::tr("Object lookup");
    case Kind::Spawn: return QObject::tr("Creation");
    case Kind::Move: return QObject::tr("Movement");
    case Kind::Control: return QObject::tr("Object control / use");
    case Kind::Group: return QObject::tr("Group membership");
    case Kind::Dependency: return QObject::tr("MOB dependency");
    case Kind::Other: return QObject::tr("Unclassified call");
    }
    return QString();
}
namespace {
struct Token {
    QString value;
    int offset = 0, end = 0, line = 1, column = 1, match = -1;
    enum Type { Identifier, String, Number, Symbol } type = Symbol;
    bool complete = true;
    bool escaped = false;
};
struct Range { int begin, end; QString text; };
struct Call { int token = 0, close = 0; QVector<QPair<int,int>> args; };
struct Parsed { QVector<Token> tokens; QVector<Call> calls; QVector<Range> functions, conditions; };
bool identifier(QChar c) { return c.isLetterOrNumber() || c == '_' || c == '#'; }
Parsed parse(const QString& text, QStringList* diagnostics) {
    Parsed p;
    int i=0, line=1, col=1;
    auto step = [&]() {
        if(text[i]=='\r') { ++line; col=1; if(i+1<text.size() && text[i+1]=='\n') ++i; }
        else if(text[i]=='\n') { ++line; col=1; } else ++col;
        ++i;
    };
    while(i<text.size()) {
        if(p.tokens.size() >= 300000) { diagnostics->append(QObject::tr("Analysis token limit reached; results are incomplete")); break; }
        if(text[i].isSpace()) { step(); continue; }
        if(text.midRef(i,2)=="//") { while(i<text.size() && text[i]!='\n' && text[i]!='\r') step(); continue; }
        if(text.midRef(i,2)=="/*") {
            step(); step();
            while(i<text.size() && text.midRef(i,2)!="*/") step();
            if(i<text.size()) { step(); step(); }
            else diagnostics->append(QObject::tr("Unclosed comment"));
            continue;
        }
        Token t; t.offset=i; t.line=line; t.column=col;
        if(text[i]=='\'' || text[i]=='"') {
            t.type=Token::String; const QChar quote=text[i]; step(); t.complete=false;
            while(i<text.size()) {
                if(text[i]==quote) { step(); t.complete=true; break; }
                if(text[i]=='\\') {
                    t.escaped=true;
                    step();
                    if(i<text.size()) { t.value+=text[i]; step(); }
                } else { t.value+=text[i]; step(); }
            }
            if(!t.complete) diagnostics->append(QObject::tr("Unclosed string"));
        } else if(text[i].isDigit() || (text[i]=='.' && i+1<text.size() && text[i+1].isDigit())) {
            t.type=Token::Number;
            while(i<text.size() && (text[i].isDigit() || text[i]=='.')) { t.value+=text[i]; step(); }
            if(i<text.size() && (text[i]=='e' || text[i]=='E')) {
                t.value+=text[i]; step();
                if(i<text.size() && (text[i]=='+' || text[i]=='-')) { t.value+=text[i]; step(); }
                while(i<text.size() && text[i].isDigit()) { t.value+=text[i]; step(); }
            }
        } else if(identifier(text[i])) {
            t.type=Token::Identifier;
            while(i<text.size() && identifier(text[i])) { t.value+=text[i]; step(); }
        } else { t.value=text[i]; step(); }
        t.end=i; p.tokens.append(t);
    }
    QVector<int> stack;
    for(int t=0;t<p.tokens.size();++t) {
        if(p.tokens[t].type!=Token::Symbol) continue;
        if(p.tokens[t].value=="(") stack.append(t);
        if(p.tokens[t].value==")") {
            if(stack.isEmpty()) diagnostics->append(QObject::tr("Unmatched closing parenthesis"));
            else { int open=stack.takeLast(); p.tokens[open].match=t; p.tokens[t].match=open; }
        }
    }
    if(!stack.isEmpty()) diagnostics->append(QObject::tr("Unclosed call or script block"));
    auto is = [&](int index,const char* value) { return index>=0 && index<p.tokens.size()
        && p.tokens[index].type!=Token::String && p.tokens[index].value.compare(value,Qt::CaseInsensitive)==0; };
    auto end = [&](int index) { return p.tokens[index].match<0 ? p.tokens.size() : p.tokens[index].match; };
    for(int t=0;t<p.tokens.size();++t) {
        if(p.tokens[t].type!=Token::Identifier) continue;
        if(is(t,"Script") && t+2<p.tokens.size() && is(t+2,"("))
            p.functions.append({t+2,end(t+2),p.tokens[t+1].value});
        if(is(t,"WorldScript") && is(t+1,"("))
            p.functions.append({t+1,end(t+1),QStringLiteral("WorldScript")});
        if(is(t,"if") && is(t+1,"(")) {
            const int stop=end(t+1);
            if(stop<p.tokens.size()) {
                const QString condition=text.mid(p.tokens[t+1].end,p.tokens[stop].offset-p.tokens[t+1].end).trimmed();
                p.conditions.append({t+1,stop,condition.isEmpty()?QObject::tr("Empty condition"):condition});
                if(is(stop+1,"then") && is(stop+2,"("))
                    p.conditions.append({stop+2,end(stop+2),condition.isEmpty()?QObject::tr("Empty condition"):condition});
            }
        }
        if(is(t,"ForIf") && is(t+1,"(")) {
            const int stop=end(t+1);
            if(stop<p.tokens.size() && is(stop+1,"("))
                p.conditions.append({stop+1,end(stop+1),text.mid(p.tokens[t].offset,p.tokens[stop].end-p.tokens[t].offset)});
        }
        if(!is(t+1,"(") || is(t,"Script") || is(t,"WorldScript") || is(t,"if") || is(t,"then")
           || is(t-1,"Script") || is(t-1,"DeclareScript")) continue;
        Call c; c.token=t; c.close=end(t+1);
        int begin=t+2;
        for(int a=begin;a<c.close;++a) {
            if(is(a,"(") && p.tokens[a].type==Token::Symbol) {
                if(p.tokens[a].match<0) { a=c.close; break; }
                a=p.tokens[a].match;
            } else if(is(a,",") && p.tokens[a].type==Token::Symbol) { c.args.append({begin,a}); begin=a+1; }
        }
        if(begin<c.close) c.args.append({begin,c.close});
        p.calls.append(c);
    }
    return p;
}
QString expression(const Parsed& p,const QString& text,QPair<int,int> range) {
    if(range.first>=range.second || range.first>=p.tokens.size()) return QString();
    return text.mid(p.tokens[range.first].offset,p.tokens[range.second-1].end-p.tokens[range.first].offset);
}
bool stringValue(const Parsed& p,QPair<int,int> arg,QString* value) {
    if(arg.second-arg.first!=1 || p.tokens[arg.first].type!=Token::String || !p.tokens[arg.first].complete || p.tokens[arg.first].escaped) return false;
    *value=p.tokens[arg.first].value; return true;
}
bool numberValue(const Parsed& p,QPair<int,int> arg,double* value) {
    QString sign;
    if(arg.first<arg.second && (p.tokens[arg.first].value=="-" || p.tokens[arg.first].value=="+")) { sign=p.tokens[arg.first].value; ++arg.first; }
    if(arg.second-arg.first!=1 || p.tokens[arg.first].type!=Token::Number) return false;
    bool ok=false; *value=(sign+p.tokens[arg.first].value).toDouble(&ok);
    return ok && std::isfinite(*value) && std::abs(*value)<=std::numeric_limits<float>::max();
}
QString canonicalCommand(const QString& name) {
    static const QStringList commands={"GetObject","GetObjectByID","GetObjectByName","AddUnitToServer",
        "SetCP","SetCPFast","MoveToPoint","MoveToObject","SwitchLeverState","SwitchLeverStateEx",
        "EnableLever","ActivateTrap","AddObject","RemoveObject","AddMob","HideObject","RemoveObjectFromServer"};
    for(const auto& command:commands) if(command.compare(name,Qt::CaseInsensitive)==0) return command;
    return name;
}
struct Lookup { bool id=false, name=false, invalid=false; quint32 value=0; QString text; };
Lookup lookup(const Parsed& p,QPair<int,int> arg) {
    Lookup result;
    const int t=arg.first;
    if(arg.second-t<4 || p.tokens[t].type!=Token::Identifier || p.tokens[t+1].value!="(" || p.tokens[t+1].match!=arg.second-1) return result;
    const auto value=qMakePair(t+2,arg.second-1);
    const QString name=canonicalCommand(p.tokens[t].value);
    if(name=="GetObjectByName") { result.name=stringValue(p,value,&result.text); return result; }
    if(name=="GetObjectByID") {
        QString raw;
        if(!stringValue(p,value,&raw)) return result;
        static const QRegularExpression digits("^[0-9]+$");
        bool ok=false; const auto id=raw.toULongLong(&ok);
        result.invalid=!ok || !digits.match(raw).hasMatch() || id==0 || id>std::numeric_limits<quint32>::max();
        if(!result.invalid) { result.id=true; result.value=quint32(id); }
    } else if(name=="GetObject") {
        double id=0;
        if(!numberValue(p,value,&id)) return result;
        // Script float IDs above 2^24 are not generally exact. Do not silently
        // round a literal to a different participant, even when uint32 fits.
        result.invalid=id<=0 || id>16777216 || id!=std::floor(id);
        if(!result.invalid) { result.id=true; result.value=quint32(id); }
    }
    return result;
}
struct Pending { int source; int reference; QVector<Lookup> lookups; QString dependency; bool dependencyLiteral=false; };
}

Result analyze(const Request& request) {
    Result result;
    QVector<Pending> pending;
    QMap<QString,QVector<int>> sourceNames;
    QMap<QString,int> sourceByKey;
    QMap<QString,QVector<int>> spawns;
    QSet<QString> prototypes;
    for(const auto& prototype:request.prototypes) prototypes.insert(prototype.toCaseFolded());
    for(int s=0;s<request.sources.size();++s) {
        const Source& source=request.sources[s];
        const QString key=normalizedSource(source.key);
        sourceByKey.insert(key,s);
        sourceNames[QFileInfo(key).fileName()].append(s);
        QStringList syntaxDiagnostics;
        const Parsed p=parse(source.text,&syntaxDiagnostics);
        for(const auto& diagnostic:syntaxDiagnostics) result.diagnostics.append(QFileInfo(source.key).fileName()+": "+diagnostic);
        for(const auto& call:p.calls) {
            const Token& token=p.tokens[call.token];
            const QString command=canonicalCommand(token.value);
            Reference ref; ref.sourceKey=source.key; ref.command=command;
            ref.offset=token.offset; ref.line=token.line; ref.column=token.column;
            const bool closed=call.close<p.tokens.size();
            ref.complete=closed && syntaxDiagnostics.isEmpty();
            ref.length=(closed?p.tokens[call.close].end:source.text.size())-ref.offset;
            ref.context=source.text.mid(ref.offset,qMin(ref.length,240)).simplified();
            for(const auto& region:p.functions) if(call.token>region.begin && call.token<region.end) ref.function=region.text;
            for(const auto& region:p.conditions) if(call.token>region.begin && call.token<region.end) ref.conditions.append(region.text);
            Pending item; item.source=s; item.reference=result.references.size();
            int arity=-1;
            QVector<int> objectArgs;
            if(command=="GetObject" || command=="GetObjectByID" || command=="GetObjectByName") {
                ref.kind=Kind::Lookup; arity=1;
                Target target; target.expression=expression(p,source.text,{call.token,qMin(call.close+1,p.tokens.size())});
                ref.targets.append(target); item.lookups.append(lookup(p,{call.token,qMin(call.close+1,p.tokens.size())}));
            } else if(command=="AddUnitToServer") { ref.kind=Kind::Spawn; arity=6; }
            else if(command=="SetCP" || command=="SetCPFast") { ref.kind=Kind::Move; arity=4; objectArgs={0}; }
            else if(command=="MoveToPoint") { ref.kind=Kind::Move; arity=3; objectArgs={0}; }
            else if(command=="MoveToObject") { ref.kind=Kind::Move; arity=2; objectArgs={0,1}; }
            else if(command=="SwitchLeverStateEx") { ref.kind=Kind::Control; arity=3; objectArgs={0}; }
            else if(command=="SwitchLeverState" || command=="EnableLever" || command=="ActivateTrap" || command=="HideObject") { ref.kind=Kind::Control; arity=2; objectArgs={0}; }
            else if(command=="RemoveObjectFromServer") { ref.kind=Kind::Control; arity=1; objectArgs={0}; }
            else if(command=="AddObject" || command=="RemoveObject") { ref.kind=Kind::Group; arity=2; objectArgs={1}; }
            else if(command=="AddMob") { ref.kind=Kind::Dependency; arity=1; }
            else {
                // Unknown script calls with arguments remain reviewable. Pure
                // arithmetic/condition helpers are already shown in context.
                if(!command.contains('#')) continue;
                ref.status=QObject::tr("Script call; arguments and execution are not resolved");
            }
            const bool valid=ref.complete && (arity<0 || arity==call.args.size());
            if(!valid) ref.status=QObject::tr("Incomplete call or unexpected argument count");
            for(int a:objectArgs) if(a<call.args.size()) {
                Target target; target.expression=expression(p,source.text,call.args[a]);
                ref.targets.append(target); item.lookups.append(lookup(p,call.args[a]));
            }
            if(ref.kind==Kind::Spawn && valid) {
                const bool named=stringValue(p,call.args[0],&ref.spawnName);
                const bool prototype=stringValue(p,call.args[1],&ref.prototype);
                ref.prototypeVerified=prototype && prototypes.contains(ref.prototype.toCaseFolded());
                if(!named || ref.spawnName.isEmpty()) ref.status=QObject::tr("Dynamic or empty spawn identity");
                else if(!ref.prototypeVerified) ref.status=QObject::tr("Prototype not verified in the active database");
            }
            if(ref.kind==Kind::Dependency) {
                item.dependencyLiteral=valid && stringValue(p,call.args[0],&item.dependency);
                if(!item.dependencyLiteral) ref.status=QObject::tr("Dynamic MOB dependency; family incomplete");
            }
            if(valid && (ref.kind==Kind::Spawn || command=="SetCP" || command=="SetCPFast" || command=="MoveToPoint")) {
                int begin=ref.kind==Kind::Spawn?3:1;
                const bool z=command!="MoveToPoint";
                double x=0,y=0,height=0;
                ref.hasDestination=numberValue(p,call.args[begin],&x) && numberValue(p,call.args[begin+1],&y)
                    && (!z || numberValue(p,call.args[begin+2],&height));
                ref.destinationHasZ=z;
                if(ref.hasDestination) ref.destination=QVector3D(float(x),float(y),float(height));
                else if(ref.status.isEmpty()) ref.status=QObject::tr("Computed or invalid coordinates; no position marker");
            }
            if(command=="MoveToObject" && valid) ref.status=QObject::tr("Target position at execution is unknown; link only");
            result.references.append(ref); pending.append(item);
        }
    }
    // Recompute closure from the actual analyzed buffers, not a previous report.
    QStringList queue=request.familyRoots;
    if(queue.isEmpty()) queue.append(request.activeSource);
    for(int q=0;q<queue.size();++q) {
        const QString key=normalizedSource(queue[q]);
        if(result.family.contains(key)) continue;
        if(!sourceByKey.contains(key)) { result.familyComplete=false; continue; }
        result.family.insert(key);
        const int s=sourceByKey.value(key);
        for(const auto& item:pending) {
            if(item.source!=s || result.references[item.reference].kind!=Kind::Dependency) continue;
            if(!item.dependencyLiteral) { result.familyComplete=false; continue; }
            const QString logical=QFileInfo(normalizedSource(item.dependency)).fileName();
            const auto candidates=sourceNames.value(logical);
            if(candidates.size()!=1) {
                result.familyComplete=false;
                result.references[item.reference].status=QObject::tr("MOB dependency missing or ambiguous");
            } else {
                queue.append(request.sources[candidates[0]].key);
                result.references[item.reference].status=QObject::tr("Static dependency; loading is not proven");
            }
        }
    }
    if(!result.familyComplete) result.diagnostics.append(QObject::tr("Family incomplete: missing, dynamic or ambiguous MOB dependencies"));
    for(int i=0;i<result.references.size();++i) {
        const auto& ref=result.references[i];
        if(ref.kind==Kind::Spawn && !ref.spawnName.isEmpty() && result.family.contains(normalizedSource(ref.sourceKey)))
            spawns[ref.spawnName.toCaseFolded()].append(i);
    }
    QMap<quint32,QVector<QPair<int,int>>> familyOwners, externalOwners;
    for(int s=0;s<request.sources.size();++s) for(int o=0;o<request.sources[s].objects.size();++o) {
        const auto id=request.sources[s].objects[o].id;
        (result.family.contains(normalizedSource(request.sources[s].key))?familyOwners:externalOwners)[id].append({s,o});
    }
    for(const auto& item:pending) {
        Reference& ref=result.references[item.reference];
        for(int t=0;t<ref.targets.size();++t) {
            Target& target=ref.targets[t]; const Lookup& look=item.lookups[t];
            if(look.invalid) { target.resolution=Resolution::Invalid; continue; }
            if(look.id) {
                target.hasId=true; target.id=look.value;
                const auto family=familyOwners.value(look.value);
                const auto external=externalOwners.value(look.value);
                if(family.size()>1) target.resolution=Resolution::Ambiguous;
                else if(family.isEmpty()) target.resolution=external.isEmpty()?Resolution::Missing:Resolution::External;
                else if(!result.familyComplete) target.resolution=Resolution::Incomplete;
                else {
                    const auto candidate=family[0];
                    target.sourceKey=request.sources[candidate.first].key;
                    const auto& obj=request.sources[candidate.first].objects[candidate.second];
                    target.objectKey=obj.key; target.name=obj.name;
                    target.resolution=normalizedSource(ref.sourceKey)==normalizedSource(target.sourceKey)?Resolution::Local:Resolution::Family;
                    // A script outside the active family must not resolve its
                    // same-number lookup to an unrelated active-map object.
                    if(!result.family.contains(normalizedSource(ref.sourceKey))) { target.resolution=Resolution::External; target.objectKey.clear(); }
                }
            } else if(look.name) {
                const auto candidates=spawns.value(look.text.toCaseFolded());
                if(candidates.size()>1) target.resolution=Resolution::Ambiguous;
                else if(candidates.isEmpty()) target.resolution=Resolution::Missing;
                else if(!result.familyComplete) target.resolution=Resolution::Incomplete;
                else if(!result.family.contains(normalizedSource(ref.sourceKey))) target.resolution=Resolution::External;
                else {
                    const auto& spawn=result.references[candidates[0]];
                    target.resolution=Resolution::Virtual;
                    target.sourceKey=spawn.sourceKey; target.objectKey=QString("spawn:%1").arg(spawn.offset); target.name=spawn.spawnName;
                }
            }
        }
        if(ref.kind==Kind::Spawn && spawns.value(ref.spawnName.toCaseFolded()).size()>1)
            ref.status=QObject::tr("Duplicate spawn identity; no unique virtual marker");
        if(!result.family.contains(normalizedSource(ref.sourceKey)) || !result.familyComplete || !ref.hasDestination || !ref.complete) continue;
        Marker marker; marker.reference=item.reference; marker.sourceKey=ref.sourceKey;
        marker.position=ref.destination; marker.hasZ=ref.destinationHasZ;
        if(ref.kind==Kind::Spawn && ref.prototypeVerified && !ref.spawnName.isEmpty()
           && spawns.value(ref.spawnName.toCaseFolded()).size()==1) {
            marker.spawn=true; marker.participantKey=QString("spawn:%1").arg(ref.offset);
            result.markers.append(marker);
        } else if(ref.kind==Kind::Move && !ref.targets.isEmpty()) {
            const auto& target=ref.targets[0];
            if(navigable(target.resolution) || target.resolution==Resolution::Virtual) {
                if(target.resolution==Resolution::Virtual) {
                    const auto candidates=spawns.value(target.name.toCaseFolded());
                    if(candidates.size()!=1 || !result.references[candidates[0]].prototypeVerified) continue;
                }
                marker.sourceKey=target.sourceKey; marker.participantKey=target.objectKey;
                result.markers.append(marker);
            }
        }
    }
    return result;
}
}
