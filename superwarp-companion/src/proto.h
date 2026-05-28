// proto.h - typed shapes for superwarp.lua's line-delimited JSON.
// Mirrors the Rust GUI's ServerMsg / SystemList / Dest / etc.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace proto
{
    struct Currency
    {
        std::string name;
        int64_t     value = 0;
    };

    // One destination row from the `lists` catalogue. `sub` is present for
    // nested systems (homepoints / waypoints / abyssea conflux / escha portal).
    struct Dest
    {
        std::string                zone;
        std::optional<std::string> sub;
        std::string                label;
        std::vector<std::string>   aliases;
        std::optional<int64_t>     cost;
    };

    struct SystemList
    {
        std::string              key;
        std::string              short_;     // `short` is a keyword in some contexts
        std::string              long_;
        std::string              plural;
        uint32_t                 count = 0;
        std::vector<Dest>        destinations;
        std::vector<std::string> subcommands;
    };

    struct UnlockDest
    {
        std::string                zone;
        std::optional<std::string> sub;
        bool                       unlocked = false;
    };

    struct UnlockSystem
    {
        std::string              system;
        int64_t                  updated = 0;
        bool                     partial = false;
        std::optional<Currency>  currency;
        std::vector<UnlockDest>  destinations;
    };

    // Tagged union of incoming messages from the addon (the `t` field).
    struct ServerMsg
    {
        enum class Type
        {
            Unknown,
            State,
            Lists,
            Unlocks,
            UnlocksAll,
            Message,
        };
        Type type = Type::Unknown;

        // ---- State ----
        bool        logged_in = false;
        std::string character;
        std::string version;
        std::string zone;
        uint32_t    zone_id = 0;
        bool        busy = false;
        bool        debug = false;

        // ---- Lists ----
        std::vector<SystemList> systems;

        // ---- Unlocks (single-system delta) ----
        std::string             unlock_system;
        int64_t                 unlock_updated = 0;
        bool                    unlock_partial = false;
        std::optional<Currency> unlock_currency;
        std::vector<UnlockDest> unlock_destinations;

        // ---- UnlocksAll ----
        std::vector<UnlockSystem> all_systems;

        // ---- Message ----
        std::string text;
        std::string level;
    };

    // Parse one JSON line into a ServerMsg. Returns true on success.
    // Unrecognized `t` tags yield Type::Unknown but still return true (so the
    // caller can skip without breaking the stream).
    bool parse(const std::string& line, ServerMsg& out);

    // Outgoing command builders -- return a JSON line (no trailing newline,
    // the net layer adds that). `dispatch` is "" (just you), "party", or "all".

    std::string cmd_warp_to(
        const std::string& system,
        const std::string& zone,
        const std::optional<std::string>& sub,
        const std::string& dispatch);

    std::string cmd_sub_cmd(
        const std::string& system,
        const std::string& sub,
        const std::string& dispatch);

    std::string cmd_cancel(const std::string& dispatch);
    std::string cmd_scan(const std::string& system);
    std::string cmd_debug(bool on);
    std::string cmd_get_state();
    std::string cmd_get_lists();
    std::string cmd_get_unlocks();
}
