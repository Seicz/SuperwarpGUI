// config.h - persisted user config, JSON-encoded next to the exe.
// Matches the Rust GUI's Config layout so superwarp_gui.json files are
// cross-readable between the two implementations.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Config
{
    std::optional<float> window_x;
    std::optional<float> window_y;
    std::optional<float> window_w;
    std::optional<float> window_h;
    bool                 always_on_top = false;
    std::optional<std::string> last_system;
    std::vector<std::string>   system_order;
    std::string                host = "127.0.0.1";
    uint16_t                   port = 19519;
    std::optional<float>       sidebar_width; // unscaled (DPI-independent)

    bool dirty = false; // in-memory only

    static Config load();
    void          save();
    void          mark_dirty() { dirty = true; }
};
