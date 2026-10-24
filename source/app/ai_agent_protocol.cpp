#include "ai_agent_protocol.h"
#include <cmath>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#endif

namespace ai {
QString discoveryDirectory()
{
    const auto override = qEnvironmentVariable("EI_MAPPER_AUTOMATION_DIR");
    return override.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + "/EIMapperAtaman/automation" : override;
}
bool secureDirectory(const QString& path)
{
    if(!QDir().mkpath(path) || QFileInfo(path).isSymLink()) return false;
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray user(int(size), '\0');
    const bool loaded = GetTokenInformation(token, TokenUser, user.data(), size, &size);
    CloseHandle(token);
    if(!loaded) return false;
    LPWSTR sid = nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid, &sid)) return false;
    const QString sddl = "D:P(A;OICI;FA;;;" + QString::fromWCharArray(sid) + ")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(
           reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr)) return false;
    const bool ok = SetFileSecurityW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor);
    LocalFree(descriptor); return ok;
#else
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
#endif
}
QJsonObject error(const QString& message) { return {{"error", message}}; }
bool takeFrame(QByteArray& buffer, QJsonObject* value, QString* error)
{
    const int newline = buffer.indexOf('\n');
    if(newline < 0) {
        if(buffer.size() > MaxFrame) *error = "Frame too large";
        return false;
    }
    if(newline > MaxFrame) { *error = "Frame too large"; return false; }
    const QByteArray frame = buffer.left(newline); buffer.remove(0, newline+1);
    if(QString::fromUtf8(frame).toUtf8()!=frame) { *error="Invalid UTF-8 frame"; return false; }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(frame, &parse);
    if(parse.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = "Expected one JSON object per UTF-8 line"; return false;
    }
    *value = doc.object(); return true;
}
static QJsonObject integer(int lo, int hi) { return {{"type","integer"},{"minimum",lo},{"maximum",hi}}; }
static QJsonObject string() { return {{"type","string"},{"maxLength",256}}; }
QJsonArray toolDefinitions()
{
    QJsonArray tools;
    const auto add = [&](QString name, QString desc, QJsonObject props = {}, QJsonArray required = {}) {
        tools.append(QJsonObject{{"name",name},{"description",desc},
            {"inputSchema",QJsonObject{{"type","object"},{"properties",props},{"required",required},{"additionalProperties",false}}},
            {"annotations",QJsonObject{{"readOnlyHint",true},{"destructiveHint",false},{"openWorldHint",false}}}});
    };
    QJsonObject region{{"x",integer(0,32768)},{"y",integer(0,32768)},
        {"width",integer(1,64)},{"height",integer(1,64)},{"step",integer(1,1024)}};
    QJsonObject page{{"offset",integer(0,1000000)},{"limit",integer(1,MaxPage)},{"query",string()}};
    add("list_instances","List human-enabled running Ataman instances. Never starts an editor.");
    add("connect_instance","Attach to an already open instance, enabled by its user.",{{"instance",string()}},{"instance"});
    add("disconnect_instance","Detach this bridge from the editor.");
    add("get_context","Current profile, MPR/MOB, dirty state and world-layout neighbours.");
    add("read_terrain","Start asynchronous live terrain snapshot: full height statistics and bounded grid. Poll operation_status. Grid vertices are 1 world unit apart; raw * maxZ/65535 gives world height.",region);
    region.insert("layer",QJsonObject{{"type","string"},{"enum",QJsonArray{"land","overlay"}}});
    add("read_paint","Bounded live tile grid (2 world units per tile), packed physical tile, runtime type, semantic corners when known. No inferred materials.",region);
    add("material_palette","Current terrain atlas material identities, paginated.",page);
    auto objects = page; objects.insert("selectedOnly",QJsonObject{{"type","boolean"}});
    objects.insert("type",integer(0,65535));
    add("list_objects","Live MOB objects, filtered by name/model, type and existing selection. Selection is read, never changed.",objects);
    auto db = page;db.insert("category",QJsonObject{{"type","string"},{"enum",QJsonArray{"all","monsters","items","prototypes"}}});
    add("search_database","Start background search of current profile monster, item and race/model prototype records. Poll operation_status.",db);
    add("list_profile_maps","Start background catalog of MPR/MOB files inside current profile map roots. Poll operation_status.",page);
    auto map = region;map.insert("map",string());map.insert("offset",integer(0,1000000));map.insert("limit",integer(1,MaxPage));
    add("read_profile_map","Read a profile map without opening it: background MPR heights/tiles/materials and matching MOB objects, or a MOB alone. Use relative catalog key, no arbitrary paths. Poll operation_status.",map,{"map"});
    add("operation_status","Poll bounded operation; results expire after 60 seconds. Running operations have a 30 second deadline.",{{"operation",string()}},{"operation"});
    add("cancel_operation","Cancel a read operation and discard its result.",{{"operation",string()}},{"operation"});
    add("window_snapshot","PNG of editor window, scaled to at most 1024x768 and 384 KiB.");
    return tools;
}
bool validateArguments(const QString& name, const QJsonObject& args, QString* diagnostic)
{
    for(const auto& v : toolDefinitions()) {
        const auto tool = v.toObject(); if(tool["name"].toString()!=name) continue;
        const auto schema = tool["inputSchema"].toObject(), props = schema["properties"].toObject();
        for(const auto& r : schema["required"].toArray())
            if(!args.contains(r.toString())) { *diagnostic="Missing argument: "+r.toString(); return false; }
        for(auto it=args.begin();it!=args.end();++it) {
            if(!props.contains(it.key())) { *diagnostic="Unknown argument: "+it.key(); return false; }
            const auto p=props[it.key()].toObject();const auto type=p["type"].toString();bool valid=true;
            if(type=="integer") valid=it->isDouble() && std::isfinite(it->toDouble())
                && std::floor(it->toDouble())==it->toDouble() && it->toDouble()>=p["minimum"].toInt() && it->toDouble()<=p["maximum"].toInt();
            if(type=="string") valid=it->isString() && it->toString().size()<=p["maxLength"].toInt(256);
            if(type=="boolean") valid=it->isBool();
            if(p.contains("enum")) valid=valid && p["enum"].toArray().contains(*it);
            if(!valid) { *diagnostic="Invalid argument: "+it.key(); return false; }
        }
        return true;
    }
    *diagnostic="Unknown read tool";return false;
}
QJsonObject registration(const QString& client, const QString& executable)
{
    if(client=="Codex") return {{"program","codex"},{"arguments",QJsonArray{"mcp","add","ataman","--",executable}}};
    return {{"mcpServers",QJsonObject{{"ataman",QJsonObject{{"type","stdio"},{"command",executable},{"args",QJsonArray{}}}}}}};
}
}
