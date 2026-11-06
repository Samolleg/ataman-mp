// Independently authored host UI policy for the standalone renderer probe.
// No game classes, offsets, hooks or private engine headers.
#include "retained_static_scene.h"
#include "engine_bridge.h"
#include "camp_inventory.h"
#include "widescreen.h"
#include "game_focus.h"
#include "debug_overlay.h"
#include "E1VisualFrame.h"
#include <cmath>
#include <cstdio>

namespace OwnedRender {
// The adapter publishes complete render facts into the retained scene before
// frame capture. There is no game object to query or gameplay state to emulate.
bool E1CompleteVisualFrame(owned::RetainedVisualFrame& frame) noexcept {

    for(const auto& sample:frame.entities) {
        if(sample.status!=owned::RetainedVisualStatus::ready) continue;
        if(!sample.assembly) return false;
    }
    return true;
}
bool E1CompleteAmbientFrame(owned::RetainedVisualFrame& frame) noexcept {
    return E1CompleteVisualFrame(frame);
}
}
namespace owned {
// Disabled because the editor host has no game camp, HUD, input or portraits.
// No synthetic game values or game class implementations are supplied.
bool camp_inventory_clear(const RECT&,RECT&) noexcept { return false; }
bool camp_inventory_input(HWND,UINT,WPARAM,LPARAM,bool) noexcept { return false; }
bool camp_inventory_preview_specular_rgb(std::uint32_t&) noexcept { return false; }
bool camp_inventory_take_decoration(CampDecoration& out) noexcept { out={};return false; }
bool camp_inventory_wheel(POINT,short,UINT) noexcept { return false; }
const char* camp_inventory_notice() noexcept { return "camp disabled in standalone host"; }
int camp_inventory_blit(const RECT&,const RECT&,CampBlitPiece (&)[kCampBlitPieces]) noexcept { return -1; }
CampScrollCostSnapshot camp_inventory_take_scroll_cost() noexcept { return {}; }
void camp_inventory_art_failed() noexcept {}
void camp_inventory_cancel_drag() noexcept {}
void camp_inventory_output(unsigned,unsigned,unsigned) noexcept {}
void camp_inventory_popup_failed() noexcept {}
void camp_inventory_popup_published(bool) noexcept {}
void camp_inventory_retire_textures() noexcept {}
void camp_inventory_set_wide(bool) noexcept {}
bool game_focus_swallow_activation(HWND,UINT,WPARAM) noexcept { return false; }
bool game_hud_editing() noexcept { return false; }
bool game_hud_editor_available() noexcept { return false; }
void set_game_hud_editing(bool) noexcept {}
bool widescreen_interface_owned() noexcept { return false; }
void widescreen_clear_hud_frame_sink(void*) noexcept {}
void widescreen_set_hud_frame_sink(void*,void (*)(void*) noexcept) noexcept {}
void bridge_set_camp_input_overlay(DebugOverlay*) noexcept {}
void set_background_mode(int) noexcept {} // standalone host owns its frame clock
SemanticContext current_semantic_context() { SemanticContext result{};result.kind=SemanticKind::frame;return result; }
// Retail harness screenshots are disabled; the C API owns capture of our targets.
bool bridge_live_wants_capture() noexcept { return false; }
void bridge_live_capture(ID3D11DeviceContext*,IDXGISwapChain*) noexcept {}
}
