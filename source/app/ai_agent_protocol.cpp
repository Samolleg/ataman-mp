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
QString agentInstructions()
{
    return "Ataman MCP. Tools automatically select the sole human-enabled mapper; connect_instance also accepts no instance when exactly one is available. With multiple mappers use list_instances and an explicit instance ID. The bridge may launch an invisible mapper on demand (owner decision2026-10-24), using saved dock authority. connect_instance target=background isolates work from the owner; target=editor selects the window. Auto selects background when the owner has unsaved changes. Never enable modes. shutdown_headless refuses dirty/busy state; save or undo first. Only the human can choose Read-only/Edit and Allow agent to save in the dock; these preferences persist until the human changes them. get_revision before edits; preview or direct sculpt/paint/create_map return an operation ID, poll operation_status; cancel_operation discards pending work without changing live terrain. Terrain edits and placement are one Undo; open/close are clean navigation. Revision conflicts require re-reading. Save uses the normal map pipeline only with human permission. Read tools and their protocol remain compatible with218. Bridges reconnect after editor restart; repeat the request and re-read context/revision, expired operation IDs cannot resume. ataman://tools contains the current editor instructions and catalog.";
}
QJsonObject mcpCatalog()
{
    return {{"protocolMajor",LocalProtocolMajor},{"catalogVersion",1},{"version",ENDURANCE_VERSION},
        {"tools",toolDefinitions()},{"instructions",agentInstructions()},
        {"resources",QJsonArray{
            QJsonObject{{"uri","ataman://context"},{"name","Open editor context"},{"mimeType","application/json"}},
            QJsonObject{{"uri","ataman://tools"},{"name","Current editor tools and instructions"},{"mimeType","application/json"}}}},
        {"resourceTemplates",QJsonArray{}}};
}
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
static QJsonObject integer(double lo, double hi) { return {{"type","integer"},{"minimum",lo},{"maximum",hi}}; }
static QJsonObject string() { return {{"type","string"},{"maxLength",256}}; }
static QJsonObject number(double lo,double hi) { return {{"type","number"},{"minimum",lo},{"maximum",hi}}; }
static QJsonObject choice(QJsonArray values) { return {{"type","string"},{"enum",values},{"maxLength",64}}; }
static QJsonObject object(QJsonObject props,QJsonArray required={}) { return {{"type","object"},{"properties",props},{"required",required},{"additionalProperties",false}}; }
static QJsonObject array(QJsonObject items,int lo,int hi) { return {{"type","array"},{"items",items},{"minItems",lo},{"maxItems",hi}}; }
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
    add("list_instances","List enabled instances; when absent a background mapper starts on demand under saved owner permissions.");
    add("connect_instance","Select target=background for independent work, editor for the owner's window, auto otherwise. Starts headless on demand; never enables access. Re-read revision after switching.",{{"instance",string()},{"target",choice({"auto","background","editor"})}});
    add("disconnect_instance","Detach this bridge from the editor.");
    add("shutdown_headless","Close the background mapper only if clean and no operations/previews are pending. Refuses owner windows and unsaved changes; save or undo first.");
    add("get_context","Current profile, MPR/MOB, dirty state and world-layout neighbours.");
    add("read_terrain","Start asynchronous live terrain snapshot: full height statistics and bounded grid. Poll operation_status. Grid vertices are 1 world unit apart; raw * maxZ/65535 gives world height.",region);
    add("export_map_topdown","Export active unsaved scene to aligned orthographic PNG layers/manifest in a new workspace directory. Read-only mode allowed; creates files but never saves documents. Poll operation_status (120s deadline), cancel_operation cancels. +X right, +Y up; full MPR bounds.",
        {{"revision",string()},{"outputDir",QJsonObject{{"type","string"},{"maxLength",4096}}},
         {"pixelsPerWorldUnit",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",4},{"enum",QJsonArray{1,2,4}},{"default",4}}}},{"revision","outputDir"});
    {auto tool=tools.last().toObject();tool["annotations"]=QJsonObject{{"readOnlyHint",false},{"destructiveHint",false},{"openWorldHint",false}};tools[tools.size()-1]=tool;}
    add("read_export_image","View a registered export PNG or full-resolution crop as standard image content. No arbitrary paths. Persistent after operation TTL and bridge reconnect until owner deletes package/registration. pixelRect uses this layer's full PNG pixel boundaries; response includes world bounds.",
        {{"exportId",string()},{"layer",string()},{"pixelRect",array(integer(0,32768),4,4)},{"maxSide",integer(64,1024)}},{"exportId","layer"});
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
    add("operation_status","Poll bounded operation; results expire after 60 seconds. Ordinary reads have a 30 second deadline; export_map_topdown has 120 seconds and resolving_resources/capturing/writing phases. Export terminal results consume the slot; files persist.",{{"operation",string()}},{"operation"});
    add("cancel_operation","Cancel a read operation and discard its result.",{{"operation",string()}},{"operation"});
    add("window_snapshot","Fresh current-camera GL viewport PNG in editor or invisible background, at most 1024x768 /384 KiB, profile/map/capturedRevision. Local server starts preparing/capturing/encoding operation (30s deadline, SNAPSHOT_TIMEOUT); MCP bridge awaits and returns image content. BUSY during map opening; failure preserves connection. cancel_operation or request cancellation supported.");
    add("get_revision","Exact document revision token. Every edit must supply this token; conflicts refuse without changing the map.");
    const auto objectArea=object({{"shape",choice({"rectangle","polygon"})},{"x",number(0,32768)},
        {"y",number(0,32768)},{"width",number(0.01,32768)},{"height",number(0.01,32768)},
        {"points",array(array(number(0,32768),2,2),3,128)}},{"shape"});
    const auto placementObject=object({{"prototype",string()},{"x",number(0,32768)},{"y",number(0,32768)},
        {"z",number(-10000,10000)},{"rotation",array(number(-36000,36000),3,3)},
        {"scale",array(number(0,10),3,3)},{"name",string()},{"primaryTexture",string()},{"secondaryTexture",string()},
        {"parts",array(string(),0,128)},{"shadow",QJsonObject{{"type","boolean"}}}},{"prototype","x","y"});
    const QJsonObject objectEdit{{"revision",string()},{"action",choice({"place","transform","copy","delete"})},
        {"objects",array(placementObject,1,500)},{"ids",array(integer(1,4294967295.0),1,500)},
        {"area",objectArea},{"prototype",string()},{"type",integer(0,65535)},
        {"delta",array(number(-32768,32768),3,3)},{"rotation",number(-36000,36000)},
        {"pivot",array(number(-32768,32768),3,3)},{"preview",QJsonObject{{"type","boolean"}}},
        {"snapshot",QJsonObject{{"type","boolean"}}}};
    add("edit_objects","272: place/transform/copy/delete up to500 static objects; units read-only. get_revision first, poll operation_status. preview=true returns affected IDs/MOB/coordinates without edits, then apply/cancel. place objects use key/name from search_database(prototypes), including static:model from the native model catalog; primaryTexture may select an authored textures entry, optional secondaryTexture/parts/shadow. x/y, terrain-relative z=0, Euler rotation=[X,Y,Z] degrees, scale=[grace,strength,height] as in manual figure properties. transform/copy require ids or area rectangle/polygon; optional prototype/model/type filters. delta=[dx,dy,dz], rotation=group Z degrees about pivot or group centroid. IDs unique across primary/AddMob/units/technical records. Referenced delete/transform refused with reference locations, no force. One native Undo; save separately with owner permission. Examples: {action:'delete',ids:[324],revision:'...',preview:true}; {action:'place',objects:[{prototype:'stbuho7',x:40,y:50}],revision:'...'}. Contains no arbitrary file access.",objectEdit,{"revision","action"});
    const QJsonObject graphEdit{{"revision",string()},{"preview",QJsonObject{{"type","boolean"}}},{"snapshot",QJsonObject{{"type","boolean"}}}};
    add("rebuild_ai_graph","272: rebuild all8 legacy AI layers with the same native builder as Navigation AI / Update graph. Bounded16384 graph cells, detached prepare, op ID/cancel/preview/apply, one Undo. Embeds graph in primary MOB in memory; only save persists under owner Save authority. Example {revision:'...',preview:true}; poll, then apply.",graphEdit,{"revision"});
    add("query_passability","272 read-only: current native legacy0.5m classifier for selected AI layer0..7:0 flying,1 prone,2 crawling,3 human,4 tall,5 wide/low,6 wide/tall,7 very large. At most200 XY points, nearest passable point within radius0..32m (default8), coordinates/distance or null. graphStale explicitly reports graph outdated by edits; classification uses current terrain/static objects. Poll operation_status. Example {layer:0,points:[[160.44,170.96]],radius:8}.",{{"layer",integer(0,7)},{"points",array(array(number(-32768,32768),2,2),1,200)},{"radius",number(0,32)}},{"layer","points"});
    add("validate_map","272 read-only: native MOB/AddMob family validation, duplicate IDs, missing references, parser/projection findings, resources and stale AI graph. Poll operation_status; findings have level Info/Warning/Error, code, message, ID/path/coordinates when applicable. Info is separate from findings/warnings; max2000 findings and512KiB, overflow fails explicitly. Example {}. Never repairs data or saves.");
    const auto area=object({{"shape",choice({"circle","rectangle","line"})},{"x",number(-32768,32768)},
        {"y",number(-32768,32768)},{"radius",number(0.5,1024)},{"width",number(1,2048)},
        {"height",number(1,2048)},{"points",array(array(number(-32768,32768),2,2),2,128)},
        {"falloff",number(0,1)},{"maskWidth",integer(1,64)},{"maskHeight",integer(1,64)},
        {"mask",array(number(0,1),1,MaxSamples)}},{"shape"});
    QJsonObject edit{{"revision",string()},{"area",area},{"preview",QJsonObject{{"type","boolean"}}},
        {"snapshot",QJsonObject{{"type","boolean"}}},{"seed",integer(0,2147483647)}};
    edit.insert("includeNeighbors",QJsonObject{{"type","boolean"},{"description","Background defaults to active map only; editor keeps enabled world neighbours. true requires ownership of every affected neighbour; a locked map refuses the whole operation."}});
    auto sculpt=edit;sculpt.insert("action",choice({"raise","lower","flatten","smooth","noise","hills","heightmap"}));
    sculpt.insert("amount",number(0,10000));sculpt.insert("height",number(0,10000));sculpt.insert("strength",number(0,1));
    sculpt.insert("grid",object({{"width",integer(2,64)},{"height",integer(2,64)},
        {"values",array(number(0,10000),4,MaxSamples)}},{"width","height","values"}));
    add("sculpt","Edit terrain in world metres: raise/lower, flatten, smooth, seeded noise/hills or bilinear heightmap. Detached async candidate; one Undo. preview=true prepares without applying.",sculpt,{"revision","action","area"});
    auto paint=edit;paint.insert("material",integer(1,2147483647));paint.insert("materialName",string());
    paint.insert("layer",choice({"land","overlay"}));paint.insert("shuffle",QJsonObject{{"type","boolean"}});
    paint.insert("minimumSlope",number(0,90));paint.insert("maximumSlope",number(0,90));
    add("paint","Paint palette material by ID or exact name over area/mask/polyline width in metres. Modern131 automatic transitions; optional existing overlay and seeded variant shuffle. Async, one Undo.",paint,{"revision","area"});
    const auto parameters=object({{"baseHeight",number(0,10000)},{"amplitude",number(0,10000)},
        {"scale",number(4,2048)},{"ridge",number(0,1)},{"warp",number(0,512)},{"terrace",number(0,1)},{"detail",number(0,32)},{"detailThreshold",number(0,1)},
        {"thermalIterations",integer(0,64)},{"hydraulicIterations",integer(0,64)},
        {"talus",number(0.01,8)},{"rain",number(0,1)},{"erosion",number(0,1)},
        {"waterLevel",number(0,10000)},{"rockSlope",number(0,90)},{"rockFraction",number(0,1)},{"patchScale",number(1,64)},{"shoreWidth",number(0,32)},
        {"boundaryNoise",number(0,8)},{"cliffX",number(-32768,32768)},
        {"ridgeDirection",number(0,180)},{"ridgeSpacing",number(8,256)},{"anisotropy",number(1,8)},
        {"maximumTerrainSlope",number(15,85)},{"streamPower",number(0,1)},{"islandMinArea",integer(0,4096)},{"beachSlope",number(0,45)},{"beachDirection",number(0,360)},
        {"cliffHeight",number(0,1000)},{"cliffWidth",number(0.5,64)}});
    auto generate=edit;generate.insert("preset",choice({"plain","hills","mountains","coast","swamp","river_valley"}));generate.insert("parameters",parameters);
    add("generate_terrain","Generate natural terrain in metres: seeded fBm, ridged noise, domain warp, bounded thermal/hydraulic erosion. Omit area for whole active map; shared enabled neighbours use the131 seam solver. Detached async, preview/apply, one Undo.",generate,{"revision","preset","seed"});
    auto erosion=edit;erosion.insert("parameters",parameters);
    add("erode","Bounded thermal/hydraulic erosion of existing heights, with sediment transport and deposition. 0..64 iterations each; detached async, one Undo.",erosion,{"revision"});
    auto river=edit;river.insert("points",array(array(number(-32768,32768),2,2),2,128));
    river.insert("width",number(1,128));river.insert("depth",number(0.01,100));river.insert("floodplain",number(0,128));river.insert("level",number(0,10000));
    add("carve_river","Carve a downstream polyline with varying channel width, rounded banks and floodplain. Optional level is upstream bed height. No overlay/water creation. One reversible async operation.",river,{"revision","points","width","depth"});
    auto automatic=generate;automatic.insert("palette",object({{"grass",integer(1,2147483647)},{"rock",integer(1,2147483647)},
        {"shore",integer(1,2147483647)},{"mud",integer(1,2147483647)},{"road",integer(1,2147483647)},
        {"dark",integer(1,2147483647)},{"light",integer(1,2147483647)},{"forest",integer(1,2147483647)},
        {"rock2",integer(1,2147483647)},{"rock3",integer(1,2147483647)},
        {"underwaterRock",integer(1,2147483647)},{"underwaterSand",integer(1,2147483647)}}));
    automatic.insert("rules",array(object({{"material",integer(1,2147483647)},
        {"minimumHeight",number(0,10000)},{"maximumHeight",number(0,10000)},
        {"minimumSlope",number(0,90)},{"maximumSlope",number(0,90)},
        {"minimumCurvature",number(-10000,10000)},{"maximumCurvature",number(-10000,10000)},
        {"minimumWetness",number(0,1)},{"maximumWetness",number(0,1)}},{"material"}),1,32));
    automatic.insert("road",area);
    automatic.insert("water",object({{"material",integer(1,2147483647)},{"liquid",integer(0,32766)},
        {"level",number(0,10000)}},{"material","liquid","level"}));
    add("auto_paint","Modern131 palette painting by height/slope/curvature/drainage and water level, coherent noisy thresholds and seeded variants. Optional ordered rules and road line. Palette defaults are resolved by active profile material names, IDs can override. One async Undo.",automatic,{"revision","preset"});
    add("preview","Prepare an edit without applying; returns region overlay PNG and operation ID for apply.",
        {{"tool",choice({"sculpt","paint","generate_terrain","erode","carve_river","auto_paint","edit_objects","rebuild_ai_graph"})},{"arguments",QJsonObject{{"anyOf",QJsonArray{object(sculpt,{"revision","action","area"}),object(paint,{"revision","area"}),object(generate,{"revision","preset","seed"}),object(erosion,{"revision"}),object(river,{"revision","points","width","depth"}),object(automatic,{"revision","preset"}),object(objectEdit,{"revision","action"}),object(graphEdit,{"revision"})}}}}},{"tool","arguments"});
    // Schemas overlap (e.g. generate/auto-paint); anyOf keeps client validation
    // correct. The server strictly validates the selected tool below.
    add("apply","Apply a completed preview only at its captured revision, one Undo.",{{"operation",string()},{"revision",string()},{"snapshot",QJsonObject{{"type","boolean"}}}},{"operation","revision"});
    add("create_map","Create and open a new MPR via New MPR213, including an empty editor. Optional profile template key/name; otherwise current clean map or deterministic profile template. Never overwrite. Requires human Allow agent to save for initial MPR/atlas publication.",
        {{"revision",string()},{"name",string()},{"sectorsX",integer(1,16)},{"sectorsY",integer(1,16)},
         {"height",number(0,10000)},{"maxZ",number(1,10000)},{"material",integer(1,2147483647)},
         {"materialName",string()},{"root",integer(0,15)},{"template",string()}},{"revision","name","sectorsX","sectorsY"});
    add("open_map","Open a profile MPR by catalog key, filename or zone name, with matching primary MOB and world neighbours. Refuses all unsaved changes; no save/discard dialogs. Async navigation, clears old document Undo history.",
        {{"revision",string()},{"map",string()}},{"revision","map"});
    add("close_map","Close clean documents only. Refuses unsaved terrain, neighbour, MOB, travel or database edits; never discards. Async navigation.",{{"revision",string()}},{"revision"});
    const QJsonObject placement{{"revision",string()},{"map",string()},{"x",number(-10000000,10000000)},{"y",number(-10000000,10000000)},
        {"world",string()},{"relativeTo",string()},{"side",choice({"N","S","E","W"})}};
    add("place_map_in_world","Place active or selected profile map. Either x/y (new map also world) or relativeTo/side. N=+Y, S=-Y; E/W align lower Y, N/S align left X. Rejects overlaps. Same atomic world JSON persistence as manual placement; requires Allow agent to save, one Undo.",placement,{"revision"});
    add("set_world_position","Alias of place_map_in_world with identical validation, persistence and Undo.",placement,{"revision"});
    add("create_mob","Create and load an empty primary MOB beside the active MPR, never overwrite. Optional profile MOB template and explicit copy flags use217 parameter reader; no objects/AI/VSS copied. Requires Allow agent to save; one Undo unloads, retains initial file.",
        {{"revision",string()},{"template",string()},{"copyRanges",QJsonObject{{"type","boolean"}}},{"copyScript",QJsonObject{{"type","boolean"}}},
        {"copyDiplomacy",QJsonObject{{"type","boolean"}}},{"copyWorld",QJsonObject{{"type","boolean"}}},{"defaultDiplomacy",QJsonObject{{"type","boolean"}}},
        {"rangeMinimum",integer(1,4294967295.0)},{"rangeMaximum",integer(1,4294967295.0)},{"time",number(-1000000,1000000)}},{"revision"});
    add("undo_last_agent_operation","Undo only the top agent command. Refuses if human/world history intervened. Does not undo somebody else's work.",{{"revision",string()}},{"revision"});
    add("save","Use normal map Save209–216 only when the human enabled Allow agent to save. Async status; backups, atlas publication and 126-page limit remain authoritative.",{{"revision",string()}},{"revision"});
    const QSet<QString> mutations{"edit_objects","rebuild_ai_graph","shutdown_headless","sculpt","paint","generate_terrain","erode","carve_river","auto_paint","preview","apply","create_map","open_map","close_map","place_map_in_world","set_world_position","create_mob","undo_last_agent_operation","save"};
    for(int i=0;i<tools.size();++i) {
        auto tool=tools[i].toObject();if(!mutations.contains(tool["name"].toString()))continue;
        tool["annotations"]=QJsonObject{{"readOnlyHint",false},{"destructiveHint",true},{"openWorldHint",false}};tools[i]=tool;
    }
    return tools;
}
static bool validValue(const QJsonValue& value,const QJsonObject& schema,QString* diagnostic,QString path,int depth=0)
{
    if(depth>10){*diagnostic="Argument nesting limit";return false;}
    const auto type=schema["type"].toString();bool valid=true;
    if(type=="integer"||type=="number")valid=value.isDouble()&&std::isfinite(value.toDouble())
        &&(type!="integer"||std::floor(value.toDouble())==value.toDouble())
        &&value.toDouble()>=schema["minimum"].toDouble()&&value.toDouble()<=schema["maximum"].toDouble();
    else if(type=="string")valid=value.isString()&&value.toString().size()<=schema["maxLength"].toInt(256);
    else if(type=="boolean")valid=value.isBool();
    else if(type=="array") {
        valid=value.isArray()&&value.toArray().size()>=schema["minItems"].toInt()&&value.toArray().size()<=schema["maxItems"].toInt();
        if(valid)for(const auto& entry:value.toArray())if(!validValue(entry,schema["items"].toObject(),diagnostic,path+"[]",depth+1))return false;
    } else if(type=="object") {
        valid=value.isObject();if(valid) {
            const auto args=value.toObject(),props=schema["properties"].toObject();
            for(const auto& r:schema["required"].toArray())if(!args.contains(r.toString())){*diagnostic="Missing argument: "+path+"."+r.toString();return false;}
            for(auto it=args.begin();it!=args.end();++it) {
                if(!props.contains(it.key())){*diagnostic="Unknown argument: "+path+"."+it.key();return false;}
                if(!validValue(*it,props[it.key()].toObject(),diagnostic,path+"."+it.key(),depth+1))return false;
            }
        }
    }
    if(schema.contains("enum"))valid=valid&&schema["enum"].toArray().contains(value);
    if(!valid)*diagnostic="Invalid argument: "+path;return valid;
}
bool validateArguments(const QString& name, const QJsonObject& args, QString* diagnostic)
{
    diagnostic->clear();
    if(name=="preview") {
        if(args.keys()!=QStringList{"arguments","tool"}||!args["arguments"].isObject()
           ||!QStringList{"sculpt","paint","generate_terrain","erode","carve_river","auto_paint","edit_objects","rebuild_ai_graph"}.contains(args["tool"].toString())){*diagnostic="preview expects an editable tool and its arguments";return false;}
        return validateArguments(args["tool"].toString(),args["arguments"].toObject(),diagnostic);
    }
    for(const auto& v : toolDefinitions()) {
        const auto tool = v.toObject(); if(tool["name"].toString()!=name) continue;
        const auto schema = tool["inputSchema"].toObject();
        return validValue(args,schema,diagnostic,name);
    }
    *diagnostic="Unknown read tool";return false;
}
QJsonObject registration(const QString& client, const QString& executable)
{
    if(client=="Codex") return {{"program","codex"},{"arguments",QJsonArray{"mcp","add","ataman","--",executable}}};
    return {{"mcpServers",QJsonObject{{"ataman",QJsonObject{{"type","stdio"},{"command",executable},{"args",QJsonArray{}}}}}}};
}
}
