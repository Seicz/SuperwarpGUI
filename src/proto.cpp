// proto.cpp - JSON parse/build for the superwarp bridge.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "proto.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
    // Safe getters: tolerate missing or null fields by returning a default.
    // The Rust GUI uses serde `default`, which we mirror here.
    template <typename T>
    T get_or(const json& j, const char* key, T def)
    {
        auto it = j.find(key);
        if (it == j.end() || it->is_null()) return def;
        try { return it->template get<T>(); } catch (...) { return def; }
    }

    std::string gets(const json& j, const char* key) { return get_or<std::string>(j, key, {}); }
    bool        getb(const json& j, const char* key) { return get_or<bool>(j, key, false); }
    int64_t     geti(const json& j, const char* key) { return get_or<int64_t>(j, key, 0); }
    uint32_t    getu(const json& j, const char* key) { return get_or<uint32_t>(j, key, 0); }

    std::optional<std::string> get_opt_str(const json& j, const char* key)
    {
        auto it = j.find(key);
        if (it == j.end() || it->is_null()) return std::nullopt;
        try { return it->get<std::string>(); } catch (...) { return std::nullopt; }
    }

    std::optional<int64_t> get_opt_i64(const json& j, const char* key)
    {
        auto it = j.find(key);
        if (it == j.end() || it->is_null()) return std::nullopt;
        try { return it->get<int64_t>(); } catch (...) { return std::nullopt; }
    }

    proto::Currency read_currency(const json& j)
    {
        proto::Currency c;
        c.name  = gets(j, "name");
        c.value = geti(j, "value");
        return c;
    }

    std::optional<proto::Currency> read_opt_currency(const json& j, const char* key)
    {
        auto it = j.find(key);
        if (it == j.end() || it->is_null()) return std::nullopt;
        return read_currency(*it);
    }

    proto::Dest read_dest(const json& j)
    {
        proto::Dest d;
        d.zone    = gets(j, "zone");
        d.sub     = get_opt_str(j, "sub");
        d.label   = gets(j, "label");
        d.cost    = get_opt_i64(j, "cost");
        auto al = j.find("aliases");
        if (al != j.end() && al->is_array())
            for (auto& a : *al)
                if (a.is_string()) d.aliases.push_back(a.get<std::string>());
        return d;
    }

    proto::SystemList read_system(const json& j)
    {
        proto::SystemList s;
        s.key     = gets(j, "key");
        s.short_  = gets(j, "short");
        s.long_   = gets(j, "long");
        s.plural  = gets(j, "plural");
        s.count   = getu(j, "count");
        auto ds = j.find("destinations");
        if (ds != j.end() && ds->is_array())
            for (auto& d : *ds) s.destinations.push_back(read_dest(d));
        auto sc = j.find("subcommands");
        if (sc != j.end() && sc->is_array())
            for (auto& c : *sc)
                if (c.is_string()) s.subcommands.push_back(c.get<std::string>());
        return s;
    }

    proto::UnlockDest read_unlock_dest(const json& j)
    {
        proto::UnlockDest d;
        d.zone     = gets(j, "zone");
        d.sub      = get_opt_str(j, "sub");
        d.unlocked = getb(j, "unlocked");
        return d;
    }

    proto::UnlockSystem read_unlock_system(const json& j)
    {
        proto::UnlockSystem u;
        u.system   = gets(j, "system");
        u.updated  = geti(j, "updated");
        u.partial  = getb(j, "partial");
        u.currency = read_opt_currency(j, "currency");
        auto ds = j.find("destinations");
        if (ds != j.end() && ds->is_array())
            for (auto& d : *ds) u.destinations.push_back(read_unlock_dest(d));
        return u;
    }
}

bool proto::parse(const std::string& line, ServerMsg& out)
{
    json j;
    try { j = json::parse(line); }
    catch (...) { return false; }
    if (!j.is_object()) return false;

    const std::string t = gets(j, "t");
    out = ServerMsg{}; // reset

    if (t == "state")
    {
        out.type      = ServerMsg::Type::State;
        out.logged_in = getb(j, "logged_in");
        out.character = gets(j, "character");
        out.version   = gets(j, "version");
        out.zone      = gets(j, "zone");
        out.zone_id   = getu(j, "zone_id");
        out.busy      = getb(j, "busy");
        out.debug     = getb(j, "debug");
    }
    else if (t == "lists")
    {
        out.type = ServerMsg::Type::Lists;
        auto ss = j.find("systems");
        if (ss != j.end() && ss->is_array())
            for (auto& s : *ss) out.systems.push_back(read_system(s));
    }
    else if (t == "unlocks")
    {
        out.type                = ServerMsg::Type::Unlocks;
        out.unlock_system       = gets(j, "system");
        out.unlock_updated      = geti(j, "updated");
        out.unlock_partial      = getb(j, "partial");
        out.unlock_currency     = read_opt_currency(j, "currency");
        auto ds = j.find("destinations");
        if (ds != j.end() && ds->is_array())
            for (auto& d : *ds) out.unlock_destinations.push_back(read_unlock_dest(d));
    }
    else if (t == "unlocks_all")
    {
        out.type = ServerMsg::Type::UnlocksAll;
        auto ss = j.find("systems");
        if (ss != j.end() && ss->is_array())
            for (auto& s : *ss) out.all_systems.push_back(read_unlock_system(s));
    }
    else if (t == "msg")
    {
        out.type  = ServerMsg::Type::Message;
        out.text  = gets(j, "text");
        out.level = gets(j, "level");
    }
    else
    {
        out.type = ServerMsg::Type::Unknown;
    }
    return true;
}

namespace
{
    void maybe_dispatch(json& j, const std::string& d)
    {
        if (!d.empty()) j["dispatch"] = d;
    }
}

std::string proto::cmd_warp_to(
    const std::string& system,
    const std::string& zone,
    const std::optional<std::string>& sub,
    const std::string& dispatch)
{
    json j = { {"cmd", "warp_to"}, {"system", system}, {"zone", zone} };
    if (sub) j["sub"] = *sub;
    maybe_dispatch(j, dispatch);
    return j.dump();
}

std::string proto::cmd_sub_cmd(
    const std::string& system,
    const std::string& sub,
    const std::string& dispatch)
{
    json j = { {"cmd", "sub_cmd"}, {"system", system}, {"sub", sub} };
    maybe_dispatch(j, dispatch);
    return j.dump();
}

std::string proto::cmd_cancel(const std::string& dispatch)
{
    json j = { {"cmd", "cancel"} };
    maybe_dispatch(j, dispatch);
    return j.dump();
}

std::string proto::cmd_scan(const std::string& system)
{
    return json{ {"cmd", "scan"}, {"system", system} }.dump();
}

std::string proto::cmd_debug(bool on)
{
    return json{ {"cmd", "debug"}, {"on", on} }.dump();
}

std::string proto::cmd_get_state()   { return R"({"cmd":"get_state"})"; }
std::string proto::cmd_get_lists()   { return R"({"cmd":"get_lists"})"; }
std::string proto::cmd_get_unlocks() { return R"({"cmd":"get_unlocks"})"; }
