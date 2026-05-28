// app.cpp - the App's central wiring.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "app.h"
#include "imgui.h"
#include "net.h"
#include "proto.h"
#include "theme.h"
#include "util.h"

#include <algorithm>
#include <chrono>

namespace
{
    // Cap the log buffer so it can't grow without bound.
    void log_push(App& a, const char* level, const std::string& text)
    {
        a.log_lines.push_back({ level ? level : "info", text });
        if (a.log_lines.size() > 200)
        {
            size_t excess = a.log_lines.size() - 200;
            a.log_lines.erase(a.log_lines.begin(), a.log_lines.begin() + excess);
        }
    }

    SystemUnlocks unlocks_from_msg(
        bool partial, int64_t updated,
        const std::optional<proto::Currency>& currency,
        const std::vector<proto::UnlockDest>& dests)
    {
        SystemUnlocks su;
        su.partial  = partial;
        su.updated  = updated;
        su.currency = currency;
        for (auto& d : dests)
            su.dests.insert_or_assign(util::destkey(d.zone, d.sub), d.unlocked);
        return su;
    }

    void handle_msg(App& a, const proto::ServerMsg& m)
    {
        using T = proto::ServerMsg::Type;
        switch (m.type)
        {
        case T::State:
        {
            a.logged_in     = m.logged_in;
            a.character     = m.character;
            a.addon_version = m.version;
            a.zone          = m.zone;
            a.zone_id       = m.zone_id;
            a.busy          = m.busy;
            a.addon_debug   = m.debug;
            // queued search-result warp finished -> drop the search.
            if (a.clear_search_pending && !m.busy)
            {
                a.search.clear();
                a.clear_search_pending = false;
            }
            break;
        }
        case T::Lists:
        {
            a.systems = m.systems;
            if (!a.selected_system)
            {
                // restore last; else first.
                if (a.config.last_system)
                {
                    for (auto& s : a.systems)
                        if (s.key == *a.config.last_system) { a.selected_system = s.key; break; }
                }
                if (!a.selected_system && !a.systems.empty())
                    a.selected_system = a.systems.front().key;
            }
            break;
        }
        case T::Unlocks:
        {
            auto& entry = a.unlocks[m.unlock_system];
            entry.partial = m.unlock_partial;
            entry.updated = m.unlock_updated;
            if (m.unlock_currency) entry.currency = m.unlock_currency;
            for (auto& d : m.unlock_destinations)
                entry.dests.insert_or_assign(util::destkey(d.zone, d.sub), d.unlocked);
            break;
        }
        case T::UnlocksAll:
        {
            for (auto& s : m.all_systems)
                a.unlocks[s.system] = unlocks_from_msg(s.partial, s.updated, s.currency, s.destinations);
            break;
        }
        case T::Message:
            log_push(a, m.level.empty() ? "info" : m.level.c_str(), m.text);
            break;
        default: break;
        }
    }

    double mono_seconds()
    {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }
}

// ---- lifecycle ----------------------------------------------------------

void app::Init(App& a)
{
    a.config = Config::load();
    a.always_on_top = a.config.always_on_top;
    std::snprintf(a.host_draft, sizeof(a.host_draft), "%s", a.config.host.c_str());
    std::snprintf(a.port_draft, sizeof(a.port_draft), "%u", (unsigned)a.config.port);
    a.last_save_time = mono_seconds();
    net::Start(a.config.host, a.config.port);
}

void app::Shutdown(App& a)
{
    if (a.config.dirty) a.config.save();
    net::Stop();
}

void app::Tick(App& a)
{
    // (Re)connection event: re-request state, lists, unlocks.
    if (net::ConsumeConnectedEvent())
    {
        Log(a, "info", "Connected to superwarp addon");
        net::SendLine(proto::cmd_get_state());
        net::SendLine(proto::cmd_get_lists());
        net::SendLine(proto::cmd_get_unlocks());
    }

    // Drain incoming.
    auto msgs = net::DrainIncoming();
    for (auto& m : msgs) handle_msg(a, m);

    // Surface the latest net error to the footer.
    a.connect_error = net::GetError();

    // Throttled save when dirty.
    if (a.config.dirty)
    {
        double now = mono_seconds();
        if (now - a.last_save_time > 3.0)
        {
            a.config.save();
            a.last_save_time = now;
        }
    }
}

void app::Log(App& a, const char* level, const std::string& text)
{
    log_push(a, level, text);
}

// ---- commands -----------------------------------------------------------

void app::WarpTo(App& a, const std::string& system,
                 const std::string& zone, const std::optional<std::string>& sub)
{
    net::SendLine(proto::cmd_warp_to(system, zone, sub, dispatch_token(a.dispatch)));
}
void app::SubCmd(App& a, const std::string& system, const std::string& sub)
{
    net::SendLine(proto::cmd_sub_cmd(system, sub, dispatch_token(a.dispatch)));
}
void app::Cancel(App& a)
{
    net::SendLine(proto::cmd_cancel(dispatch_token(a.dispatch)));
}
void app::Scan(App&, const std::string& system)
{
    net::SendLine(proto::cmd_scan(system));
}
void app::SetDebug(App&, bool on)
{
    net::SendLine(proto::cmd_debug(on));
}
void app::Reconnect(App& a, const std::string& host, uint16_t port)
{
    a.config.host = host;
    a.config.port = port;
    a.config.mark_dirty();
    a.config.save();
    net::SetEndpoint(host, port);
}

// ---- sidebar order ------------------------------------------------------

std::vector<std::string> app::effective_order(const App& a)
{
    std::vector<std::string> order;
    order.reserve(a.systems.size());
    for (const auto& k : a.config.system_order)
        if (std::any_of(a.systems.begin(), a.systems.end(),
                        [&](const proto::SystemList& s){ return s.key == k; }))
            order.push_back(k);
    for (const auto& s : a.systems)
        if (std::find(order.begin(), order.end(), s.key) == order.end())
            order.push_back(s.key);
    return order;
}

// ---- unlock helpers -----------------------------------------------------

std::optional<bool> app::dest_unlocked(const App& a, const std::string& sys, const proto::Dest& d)
{
    auto it = a.unlocks.find(sys);
    if (it == a.unlocks.end()) return std::nullopt;
    auto dit = it->second.dests.find(util::destkey(d.zone, d.sub));
    if (dit == it->second.dests.end()) return std::nullopt;
    return dit->second;
}
const proto::Currency* app::system_currency(const App& a, const std::string& sys)
{
    auto it = a.unlocks.find(sys);
    if (it == a.unlocks.end()) return nullptr;
    return it->second.currency ? &*it->second.currency : nullptr;
}
bool app::system_partial(const App& a, const std::string& sys)
{
    auto it = a.unlocks.find(sys);
    return (it != a.unlocks.end()) && it->second.partial;
}
std::optional<int64_t> app::system_updated(const App& a, const std::string& sys)
{
    auto it = a.unlocks.find(sys);
    if (it == a.unlocks.end() || it->second.updated <= 0) return std::nullopt;
    return it->second.updated;
}
std::optional<std::pair<size_t,size_t>> app::unlock_counts(const App& a, const proto::SystemList& sys)
{
    auto it = a.unlocks.find(sys.key);
    if (it == a.unlocks.end() || it->second.dests.empty()) return std::nullopt;
    size_t known = 0, open = 0;
    for (auto& d : sys.destinations)
    {
        auto dit = it->second.dests.find(util::destkey(d.zone, d.sub));
        if (dit != it->second.dests.end())
        {
            known++;
            if (dit->second) open++;
        }
    }
    if (known == 0) return std::nullopt;
    return std::make_pair(open, known);
}

// ---- frame --------------------------------------------------------------

void app::RenderFrame(App& a)
{
    const float scale = ui::dpi();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove    | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::v4(theme::BG));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar();

    const float total_h  = ImGui::GetContentRegionAvail().y;
    const float header_h = 70.0f * scale;

    // Footer height tracks its content (up to 3 most-recent log lines, plus
    // the connect-error line when offline). Mirrors Rust's TopBottomPanel
    // auto-sizing behavior instead of a fixed strip that grows with the
    // window and leaves a huge empty band when the log is mostly empty.
    const size_t footer_log_n = (a.log_lines.size() > 3) ? 3 : a.log_lines.size();
    const bool   footer_err   = !a.connect_error.empty()
                              && net::GetStatus() != net::Status::Connected;
    const size_t footer_lines = footer_log_n + (footer_err ? 1 : 0);
    const float  line_h       = ImGui::GetTextLineHeightWithSpacing();
    const float  footer_pad_y = 6.0f * scale;
    const float  footer_h     = 2.0f * footer_pad_y + footer_lines * line_h;

    // ImGui places ItemSpacing.y between each successive child in the column,
    // so a body sized to `total - header - footer` would push the footer
    // partway off-screen by 2*spacing. Subtract that here.
    const float spacing_y = ImGui::GetStyle().ItemSpacing.y;
    const float body_h    = total_h - header_h - footer_h - 2.0f * spacing_y;

    // Header
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 8.0f * scale));
    ImGui::BeginChild("##header", ImVec2(0, header_h), ImGuiChildFlags_None);
    ui::Header(a);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Body: systems sidebar + main panel.
    const float side_w_min = 120.0f * scale;
    const float side_w_max = 360.0f * scale;
    // Default is intentionally narrow (140 unscaled) so the destinations panel
    // gets most of the horizontal real estate; the user can drag wider and the
    // change persists to superwarp_gui.json.
    static float side_w_unscaled = a.config.sidebar_width.value_or(140.0f);
    float side_w = side_w_unscaled * scale;
    if (side_w < side_w_min) side_w = side_w_min;
    if (side_w > side_w_max) side_w = side_w_max;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scale, 8.0f * scale));
    ImGui::BeginChild("##sys", ImVec2(side_w, body_h), ImGuiChildFlags_None);
    ui::Systems(a, body_h);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Resize handle.
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("##sysresize", ImVec2(4.0f * scale, body_h));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive())
    {
        side_w_unscaled = std::clamp(
            side_w_unscaled + ImGui::GetIO().MouseDelta.x / scale,
            120.0f, 360.0f);
    }
    // Commit on release so we persist a stable value, not every intermediate
    // drag frame. mark_dirty triggers the throttled save in app::Tick.
    if (ImGui::IsItemDeactivated())
    {
        a.config.sidebar_width = side_w_unscaled;
        a.config.mark_dirty();
    }
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 8.0f * scale));
    ImGui::BeginChild("##main", ImVec2(0, body_h), ImGuiChildFlags_None);
    if (a.view == View::Advanced) ui::Advanced(a);
    else                          ui::Destinations(a);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Footer.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::v4(theme::INPUT));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 6.0f * scale));
    ImGui::BeginChild("##footer", ImVec2(0, footer_h), ImGuiChildFlags_None);
    ui::Footer(a);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor();
}
