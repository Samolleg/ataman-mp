#pragma once
// Ataman <-> MSVC renderer DLL. All spans are borrowed for the duration of a call.
// The DLL owns its handle and allocations. No Qt/STL/exception ABI is exported.
#include <stdint.h>
#ifdef _WIN32
#define EIR_CALL __cdecl
#else
#define EIR_CALL
#endif
#define EIR_ABI 7u
#define EIR_MAX_POINT_LIGHTS 64u
typedef struct EirPointLight {
    float position[3], color[3], radius;
    uint32_t source_id, casts_shadow;
} EirPointLight;
typedef struct EirError { int32_t code; char message[2048]; } EirError;
typedef struct EirSpan { const void* data; uint64_t bytes; } EirSpan;
typedef struct EirMount { const uint16_t* path; uint32_t kind; uint32_t reserved; } EirMount;
typedef struct EirCreate {
    uint32_t size, abi, width, height;
    void* window;
    const EirMount* mounts;
    uint32_t mount_count, test_failure; // 1=no D3D11, 2=initialisation
    const uint16_t* scratch_path;
    const uint16_t* neighbour_layout_path; // selected game/mod world-map-layouts.json, read only
    const uint16_t* game_data_path; // isolated work directory; unchanged game Config light tables
} EirCreate;
typedef struct EirObjectPose {
    uint32_t mob,nid,visible,changed;
    float position[3],quaternion[4],shape[3]; // wxyz; semantic strength,grace,tall
} EirObjectPose;

typedef struct EirDocument {
    uint32_t size, mob_count;
    uint64_t generation;
    EirSpan terrain;
    const EirSpan* mobs;
    const char* terrain_name; // UTF-8, resource identity only; never reopened
    const EirObjectPose* objects; // immutable editor admission/geometry facts
    uint32_t object_count;
} EirDocument;
typedef struct EirCamera {
    float world_to_clip[16]; // row-major, D3D depth [0,1], Z-up
    float eye[3], target[3];
    double hour;
    uint32_t region, cave;
    uint32_t point_light_count;
    EirPointLight point_lights[EIR_MAX_POINT_LIGHTS];
    double world_time; // world segments (.055 seconds), editor preview clock
    float wind_direction[3], wind_force;
    float fog_projection[3]; // TL _33/_34/_43 from the common camera
    uint32_t neighbours_visible; // existing human neighbour-landscapes toggle
    uint32_t visuals; // 1=sky, 2=fog, 4=terrain, 8=water, 16=static objects
} EirCamera;
typedef struct EirStatistics {
    uint64_t generation, ready_generation, private_bytes, video_bytes;
    uint64_t service_serial, document_updates;
    uint32_t width, height, pending, reserved;
    uint32_t neighbours_pending, neighbours_accepted;
    double frame_ms,service_cpu_ms,service_composite_ms;
} EirStatistics;

typedef struct EirService {
    uint32_t size,width,height,stride;
    uint64_t serial,generation;
    double cpu_ms;
    EirSpan rgba; // top-down, premultiplied RGBA8; editor-owned GL depth
    const EirObjectPose* poses;
    uint32_t pose_count;
} EirService;
typedef struct EirApi {
    uint32_t size, abi;
    int32_t (EIR_CALL *create)(const EirCreate*,void**,EirError*);
    void (EIR_CALL *destroy)(void*);
    int32_t (EIR_CALL *document)(void*,const EirDocument*,EirError*);
    int32_t (EIR_CALL *camera)(void*,const EirCamera*,EirError*);
    int32_t (EIR_CALL *resize)(void*,uint32_t,uint32_t,EirError*);
    int32_t (EIR_CALL *frame)(void*,EirStatistics*,EirError*); // 1=pending, 0=committed
    int32_t (EIR_CALL *capture)(void*,const uint16_t*,EirError*);
    void (EIR_CALL *test_device_loss)(void*);
    int32_t (EIR_CALL *service)(void*,const EirService*,EirError*);
    int32_t (EIR_CALL *capture_world)(void*,const uint16_t*,EirError*);
} EirApi;
typedef int32_t (EIR_CALL *EirGetApi)(uint32_t,EirApi*);
