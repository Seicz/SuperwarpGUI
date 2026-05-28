// config.cpp - load/save the JSON config next to the exe.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "config.h"

#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
    fs::path config_path()
    {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n == 0 || n == MAX_PATH) return fs::path("superwarp_gui.json");
        fs::path exe(buf);
        return exe.parent_path() / "superwarp_gui.json";
    }

    template <typename T>
    void put_opt(json& j, const char* k, const std::optional<T>& v)
    {
        if (v) j[k] = *v;
    }

    template <typename T>
    std::optional<T> get_opt(const json& j, const char* k)
    {
        auto it = j.find(k);
        if (it == j.end() || it->is_null()) return std::nullopt;
        try { return it->get<T>(); } catch (...) { return std::nullopt; }
    }
}

Config Config::load()
{
    Config c;
    std::ifstream f(config_path());
    if (!f) return c;
    std::stringstream ss;
    ss << f.rdbuf();
    json j;
    try { j = json::parse(ss.str()); }
    catch (...) { return c; }
    if (!j.is_object()) return c;

    c.window_x = get_opt<float>(j, "window_x");
    c.window_y = get_opt<float>(j, "window_y");
    c.window_w = get_opt<float>(j, "window_w");
    c.window_h = get_opt<float>(j, "window_h");

    if (auto a = j.find("always_on_top"); a != j.end() && a->is_boolean())
        c.always_on_top = a->get<bool>();

    c.last_system = get_opt<std::string>(j, "last_system");

    if (auto o = j.find("system_order"); o != j.end() && o->is_array())
        for (auto& k : *o)
            if (k.is_string()) c.system_order.push_back(k.get<std::string>());

    if (auto h = j.find("host"); h != j.end() && h->is_string())
        c.host = h->get<std::string>();
    if (auto p = j.find("port"); p != j.end() && p->is_number_integer())
        c.port = static_cast<uint16_t>(p->get<int>());
    c.sidebar_width = get_opt<float>(j, "sidebar_width");

    return c;
}

void Config::save()
{
    dirty = false;
    json j;
    put_opt(j, "window_x", window_x);
    put_opt(j, "window_y", window_y);
    put_opt(j, "window_w", window_w);
    put_opt(j, "window_h", window_h);
    j["always_on_top"] = always_on_top;
    if (last_system) j["last_system"] = *last_system;
    j["system_order"] = system_order;
    j["host"]         = host;
    j["port"]         = port;
    put_opt(j, "sidebar_width", sidebar_width);

    std::ofstream f(config_path(), std::ios::trunc);
    if (f) f << j.dump(2);
}
