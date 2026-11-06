#pragma once
#include <windows.h>
#include <stdint.h>
#define PROBE1B_ABI 2u
typedef struct probe1b_create_info {
 uint32_t size,abi,width,height;
 HWND window;
 const wchar_t* ini;
 const wchar_t* layouts;
 const wchar_t* const* loose;
 uint32_t loose_count;
 const wchar_t* const* archives;
 uint32_t archive_count;
 const uint8_t* archive_kinds;
 uint32_t test_failure;
} probe1b_create_info;
typedef struct probe1b_error {int32_t code;char detail[2048];} probe1b_error;
typedef struct probe1b_camera {float eye[3],target[3],fov;} probe1b_camera;
typedef int32_t (__cdecl *probe1b_create_fn)(const probe1b_create_info*,void**,probe1b_error*);
typedef int32_t (__cdecl *probe1b_frame_fn)(void*,probe1b_error*);
typedef int32_t (__cdecl *probe1b_capture_fn)(void*,const wchar_t*,probe1b_error*);
typedef void (__cdecl *probe1b_destroy_fn)(void*);

// Flat, caller-owned terrain snapshot format, independent of the C++ renderer.
#define PROBE1B_SNAPSHOT_ABI 1u
typedef struct probe1b_snapshot_header {uint32_t size,abi,sectors,reserved;uint64_t generation,bytes;} probe1b_snapshot_header;
typedef struct probe1b_snapshot_sector {uint32_t x,y,vertices,tiles;} probe1b_snapshot_sector;
typedef struct probe1b_snapshot_tile {uint16_t index,page;uint32_t animated;uint16_t center,base;uint32_t reserved;} probe1b_snapshot_tile;
typedef struct probe1b_statistics {uint64_t private_bytes,working_set,video_local,target_bytes,generation;} probe1b_statistics;
typedef int32_t (__cdecl *probe1b_snapshot_fn)(void*,void*,uint64_t,uint64_t*,probe1b_error*);
typedef int32_t (__cdecl *probe1b_replace_fn)(void*,const void*,uint64_t,probe1b_error*);
typedef int32_t (__cdecl *probe1b_stats_fn)(void*,probe1b_statistics*,probe1b_error*);
