// app.h - the App's central state struct and frame-level entry points.
//
// State is held in a plain struct (public fields), and the various UI modules
// consume an App& by reference. Lifecycle functions and command issuers live
// in the `app::` namespace.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "imgui.h"
#include "proto.h"

// ---- enums ---------------------------------------------------------------

inline constexpr const char* GUI_VERSION = "0.2.9";

// Base font size we rasterize at (main.cpp's BuildFontAtScale uses BASE_FONT_PX
// * dpi_scale). UI modules derive the current DPI scale from the actual
// rendered font size, so all our hand-painted layout constants can multiply
// against `ui::dpi()` for consistent sizing at any display scale.
// Tuned to match the Rust GUI's Hack 11-12pt visual density.
inline constexpr float BASE_FONT_PX = 12.0f;

enum class Dispatch { Off, Party, All };
enum class View     { Warp, Advanced };

inline Dispatch dispatch_next(Dispatch d)
{
    switch (d) { case Dispatch::Off: return Dispatch::Party;
                 case Dispatch::Party: return Dispatch::All;
                 default: return Dispatch::Off; }
}
inline const char* dispatch_label(Dispatch d)
{
    switch (d) { case Dispatch::Off: return "Send: Off";
                 case Dispatch::Party: return "Send: Party";
                 default: return "Send: ALL"; }
}
inline std::string dispatch_token(Dispatch d)
{
    switch (d) { case Dispatch::Party: return "party";
                 case Dispatch::All:   return "all";
                 default:              return ""; }
}

// ---- cached unlock state per system --------------------------------------

struct SystemUnlocks
{
    bool                              partial = false;
    int64_t                           updated = 0;
    std::optional<proto::Currency>    currency;
    std::unordered_map<std::string, bool> dests; // keyed by util::destkey
};

// ---- log line ------------------------------------------------------------

struct LogLine { std::string level; std::string text; };

// ---- App -----------------------------------------------------------------

struct App
{
    // network
    std::string connect_error;

    // addon-reported state
    bool        logged_in     = false;
    std::string character;
    std::string addon_version;
    std::string zone;
    uint32_t    zone_id       = 0;
    bool        busy          = false;
    bool        addon_debug   = false;

    // catalogue + unlocks
    std::vector<proto::SystemList>                   systems;
    std::unordered_map<std::string, SystemUnlocks>   unlocks;

    // UI state
    std::optional<std::string> selected_system;
    std::string                search;
    Dispatch                   dispatch = Dispatch::Off;
    bool                       clear_search_pending = false;
    View                       view = View::Warp;

    // drag-reorder (sidebar)
    std::optional<std::string> drag_system;
    float                      drag_grab_offset = 0.0f;

    // log
    std::vector<LogLine> log_lines;

    // advanced
    char host_draft[64] = {};
    char port_draft[16] = {};

    // persistence
    Config config;
    double last_save_time = 0.0;

    // window
    bool always_on_top = false;
};

namespace app
{
    void Init(App& a);
    void Shutdown(App& a);

    // Drain incoming network messages, fold them into state, handle reconnect events.
    void Tick(App& a);

    // Draw all panels into the current ImGui frame (covers the OS window).
    void RenderFrame(App& a);

    // Log helpers.
    void Log(App& a, const char* level, const std::string& text);

    // Command issuers (build JSON + push to net::SendLine, no-op when offline).
    void WarpTo(App& a, const std::string& system,
                const std::string& zone, const std::optional<std::string>& sub);
    void SubCmd(App& a, const std::string& system, const std::string& sub);
    void Cancel(App& a);
    void Scan(App& a, const std::string& system);
    void SetDebug(App& a, bool on);
    void Reconnect(App& a, const std::string& host, uint16_t port);

    // Sidebar order: persisted custom order minus stale keys, plus any new
    // systems appended at the bottom.
    std::vector<std::string> effective_order(const App& a);

    // Unlock helpers (matching the Rust impls).
    std::optional<bool>             dest_unlocked(const App& a, const std::string& sys, const proto::Dest& d);
    const proto::Currency*          system_currency(const App& a, const std::string& sys);
    bool                            system_partial(const App& a, const std::string& sys);
    std::optional<int64_t>          system_updated(const App& a, const std::string& sys);
    // (open, known) counted only over destinations we have state for.
    std::optional<std::pair<size_t,size_t>> unlock_counts(const App& a, const proto::SystemList& sys);
}

// Forward declarations of UI module entry points.
namespace ui
{
    // Current display DPI scale, derived from the actual rendered font size.
    // Use this to scale any hand-painted layout pixel that isn't already going
    // through ImGui's Style (which is pre-scaled via ScaleAllSizes in main).
    inline float dpi()
    {
        float s = ImGui::GetFontSize() / BASE_FONT_PX;
        return (s > 0.0f) ? s : 1.0f;
    }

    void Header(App& a);
    void Systems(App& a, float panel_height);
    void Destinations(App& a);
    void Advanced(App& a);
    void Footer(App& a);
}
