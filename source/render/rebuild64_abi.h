#pragma once
// Ataman <-> MSVC renderer DLL. All spans are borrowed for the duration of a call.
// The DLL owns its handle and allocations. No Qt/STL/exception ABI is exported.
#include <stdint.h>
#ifdef _WIN32
#define EIR_CALL __cdecl
#else
#define EIR_CALL
#endif
#define EIR_ABI 2u
typedef struct EirError { int32_t code; char message[2048]; } EirError;
typedef struct EirSpan { const void* data; uint64_t bytes; } EirSpan;
typedef struct EirMount { const uint16_t* path; uint32_t kind; uint32_t reserved; } EirMount;
typedef struct EirCreate {
    uint32_t size, abi, width, height;
    void* window;
    const EirMount* mounts;
    uint32_t mount_count, test_failure; // 1=no D3D11, 2=initialisation
    const uint16_t* scratch_path;
} EirCreate;
typedef struct EirDocument {
    uint32_t size, mob_count;
    uint64_t generation;
    EirSpan terrain;
    const EirSpan* mobs;
    const char* terrain_name; // UTF-8, resource identity only; never reopened
} EirDocument;
typedef struct EirCamera {
    float world_to_clip[16]; // row-major, D3D depth [0,1], Z-up
    float eye[3], target[3];
    double hour;
    float ambient[3], sun_diffuse[3], sun_direction[3];
    float fog_color[3], fog_start, fog_end;
    uint32_t region, cave;
} EirCamera;
typedef struct EirStatistics {
    uint64_t generation, ready_generation, private_bytes, video_bytes;
    uint32_t width, height, pending, reserved;
    double frame_ms;
} EirStatistics;
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
} EirApi;
typedef int32_t (EIR_CALL *EirGetApi)(uint32_t,EirApi*);
