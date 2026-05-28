// ui_dests.cpp - destinations panel: header, search, actions, results.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "app.h"
#include "fuzzy.h"
#include "imgui.h"
#include "net.h"
#include "theme.h"
#include "util.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{
    bool small_chip(const char* label, ImU32 fg, ImU32 bg, bool enabled = true)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(fg));
        ImGui::PushStyleColor(ImGuiCol_Button, theme::v4(bg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(theme::HOVER));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::v4(theme::AZURE_DIM));
        if (!enabled) ImGui::BeginDisabled();
        bool clicked = ImGui::SmallButton(label);
        if (!enabled) ImGui::EndDisabled();
        ImGui::PopStyleColor(4);
        return clicked;
    }

    void zone_label(const proto::Dest& d, std::string& out)
    {
        if (d.sub) { out = d.zone; out += " - "; out += d.label; }
        else       { out = d.label; }
    }

    struct Row { std::string system; std::string sys_long; proto::Dest dest; };

    void build_browse_rows(const App& a, const std::string& system, std::vector<Row>& rows)
    {
        auto it = std::find_if(a.systems.begin(), a.systems.end(),
                               [&](const proto::SystemList& s){ return s.key == system; });
        if (it == a.systems.end()) return;
        std::string lng = it->long_.empty() ? it->key : it->long_;
        rows.reserve(it->destinations.size());
        for (auto& d : it->destinations) rows.push_back({ it->key, lng, d });

        // sort: zone -> sub (numeric-aware) -> system_key
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b){
            auto za = a.dest.zone; std::transform(za.begin(), za.end(), za.begin(),
                                                  [](unsigned char c){ return (char)std::tolower(c); });
            auto zb = b.dest.zone; std::transform(zb.begin(), zb.end(), zb.begin(),
                                                  [](unsigned char c){ return (char)std::tolower(c); });
            if (za != zb) return za < zb;
            auto ka = util::sub_order(a.dest.sub);
            auto kb = util::sub_order(b.dest.sub);
            if (ka.group != kb.group) return ka.group < kb.group;
            if (ka.group == 1 && ka.num != kb.num) return ka.num < kb.num;
            if (ka.group == 2 && ka.text != kb.text) return ka.text < kb.text;
            return a.system < b.system;
        });
    }

    // Build globally-ranked search rows; returns true if results are approx-tier.
    bool build_search_rows(const App& a, const std::string& needle_clean,
                           std::vector<Row>& rows)
    {
        struct Scored { int64_t score; std::string sys; std::string lng; proto::Dest d; };
        std::vector<Scored> scored;
        for (const auto& s : a.systems)
        {
            std::string lng = s.long_.empty() ? s.key : s.long_;
            for (const auto& d : s.destinations)
            {
                if (auto v = fuzzy::dest_fuzzy(d, needle_clean))
                    scored.push_back({ *v, s.key, lng, d });
            }
        }

        if (!scored.empty())
        {
            std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b){
                if (a.score != b.score) return a.score > b.score;
                if (a.d.label.size() != b.d.label.size()) return a.d.label.size() < b.d.label.size();
                std::string za = a.d.zone, zb = b.d.zone;
                std::transform(za.begin(), za.end(), za.begin(),
                               [](unsigned char c){ return (char)std::tolower(c); });
                std::transform(zb.begin(), zb.end(), zb.begin(),
                               [](unsigned char c){ return (char)std::tolower(c); });
                return za < zb;
            });
            rows.reserve(scored.size());
            for (auto& x : scored) rows.push_back({ x.sys, x.lng, std::move(x.d) });
            return false;
        }

        // Approx tier (no direct subsequence match).
        std::vector<Scored> appr;
        for (const auto& s : a.systems)
        {
            std::string lng = s.long_.empty() ? s.key : s.long_;
            for (const auto& d : s.destinations)
                appr.push_back({ fuzzy::dest_approx(d, needle_clean), s.key, lng, d });
        }
        std::sort(appr.begin(), appr.end(), [](const Scored& a, const Scored& b){
            if (a.score != b.score) return a.score > b.score;
            return a.d.label.size() < b.d.label.size();
        });
        if (appr.size() > 12) appr.resize(12);
        rows.reserve(appr.size());
        for (auto& x : appr) rows.push_back({ x.sys, x.lng, std::move(x.d) });
        return !rows.empty();
    }
}

void ui::Destinations(App& a)
{
    if (!a.selected_system)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::TextUnformatted("Select a system on the left.");
        ImGui::PopStyleColor();
        return;
    }
    const std::string system = *a.selected_system;

    // Locate the SystemList for header bits.
    std::string sys_long, sys_short;
    std::vector<std::string> subs;
    for (auto& s : a.systems)
        if (s.key == system) { sys_long = s.long_; sys_short = s.short_; subs = s.subcommands; break; }
    if (sys_long.empty()) sys_long = system;

    // ---- Header row: title + [short] | scan + dispatch ----
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::WHITE));
    ImGui::TextUnformatted(sys_long.c_str());
    ImGui::PopStyleColor();
    if (!sys_short.empty())
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::VIOLET));
        ImGui::Text("[%s]", sys_short.c_str());
        ImGui::PopStyleColor();
    }
    // right-align cluster: dispatch + scan
    {
        const char* dlabel = dispatch_label(a.dispatch);
        ImU32       dcol   = (a.dispatch == Dispatch::Off)   ? theme::MUTED
                          : (a.dispatch == Dispatch::Party) ? theme::PARTY_COL
                                                            : theme::ALL_COL;
        const char* scan_txt = "scan";
        // SmallButton width = TextWidth + 2 * FramePadding.x; use the live style
        // values so the right-edge alignment stays accurate across DPI scales.
        const float btn_pad = 2.0f * ImGui::GetStyle().FramePadding.x;
        const float gap     = ImGui::GetStyle().ItemSpacing.x;
        float dw = ImGui::CalcTextSize(dlabel).x   + btn_pad;
        float sw = ImGui::CalcTextSize(scan_txt).x + btn_pad;
        float cluster_w = dw + gap + sw;
        ImGui::SameLine();
        float avail = ImGui::GetContentRegionAvail().x;
        if (cluster_w < avail)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - cluster_w));
        if (small_chip(dlabel, dcol, theme::PANEL))
            a.dispatch = dispatch_next(a.dispatch);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Send warps to: just you / your party / all your characters");
        ImGui::SameLine();
        bool can_scan = (net::GetStatus() == net::Status::Connected) && !a.busy;
        if (small_chip(scan_txt, theme::WHITE, theme::AZURE_DIM, can_scan))
            app::Scan(a, system);
    }
    ImGui::EndGroup();

    // ---- Metadata row: currency + scanned-ago ----
    const proto::Currency* cur = app::system_currency(a, system);
    auto upd = app::system_updated(a, system);
    if (cur || upd)
    {
        if (cur)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::GOLD));
            ImGui::Text("%s: %s", cur->name.c_str(), util::fmt_thousands(cur->value).c_str());
            ImGui::PopStyleColor();
        }
        if (upd)
        {
            if (cur) ImGui::SameLine();
            std::string ago = "scanned " + util::time_ago(*upd);
            float tw = ImGui::CalcTextSize(ago.c_str()).x;
            float avail = ImGui::GetContentRegionAvail().x;
            if (tw < avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
            ImGui::TextUnformatted(ago.c_str());
            ImGui::PopStyleColor();
        }
    }

    // ---- Search (global) ----
    a.search.reserve(128);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", a.search.c_str());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##search", "search all systems...", buf, sizeof(buf)))
    {
        a.search = buf;
        a.clear_search_pending = false; // user is editing -> cancel queued auto-clear
    }
    ImGui::Dummy(ImVec2(0, 2));

    std::string needle_clean = fuzzy::clean(a.search);
    const bool global = !needle_clean.empty();

    // ---- Actions row (browse only) ----
    if (!global)
    {
        bool can_act = (net::GetStatus() == net::Status::Connected) && !a.busy;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::TextUnformatted("actions");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (small_chip("cancel / reset", theme::LOCKED, theme::PANEL))
            app::Cancel(a);
        for (const auto& sc : subs)
        {
            ImGui::SameLine();
            if (small_chip(sc.c_str(), theme::AZURE, theme::PANEL, can_act))
            {
                app::SubCmd(a, system, sc);
                app::Log(a, "info", std::string("-> ") + system + " " + sc);
            }
        }
        ImGui::Dummy(ImVec2(0, 4));
    }

    // Separator line.
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + 1.0f), theme::PANEL);
        ImGui::Dummy(ImVec2(0, 4));
    }

    // ---- Build rows ----
    std::vector<Row> rows;
    bool approx = false;
    if (global)
        approx = build_search_rows(a, needle_clean, rows);
    else
        build_browse_rows(a, system, rows);

    if (rows.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::TextUnformatted(global ? "No matches." : "No destinations.");
        ImGui::PopStyleColor();
        return;
    }
    if (approx)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::TextUnformatted("no direct match -- closest:");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 2));
    }

    // ---- Row list ----
    ImGui::BeginChild("##destscroll", ImVec2(0, 0), ImGuiChildFlags_None);
    const bool can_warp = (net::GetStatus() == net::Status::Connected) && !a.busy;

    for (const auto& row : rows)
    {
        auto unlocked = app::dest_unlocked(a, row.system, row.dest);
        std::optional<bool> afford;
        if (row.dest.cost)
        {
            const proto::Currency* c = app::system_currency(a, row.system);
            if (c) afford = c->value >= *row.dest.cost;
        }

        ImGui::BeginGroup();
        const char* glyph = unlocked ? (*unlocked ? u8"\u25CF" : u8"\u25CB") : u8"\u00B7";
        ImU32 gcol = unlocked ? (*unlocked ? theme::UNLOCKED : theme::LOCKED) : theme::MUTED;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(gcol));
        ImGui::TextUnformatted(glyph);
        ImGui::PopStyleColor();
        ImGui::SameLine();

        std::string display;
        zone_label(row.dest, display);
        ImU32 label_col = (unlocked && !*unlocked) ? theme::MUTED : theme::TEXT;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(label_col));
        ImGui::PushStyleColor(ImGuiCol_Button, theme::v4(theme::BG));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::v4(theme::PANEL));
        if (!can_warp) ImGui::BeginDisabled();
        bool clk = ImGui::Button(display.c_str());
        if (!can_warp) ImGui::EndDisabled();
        ImGui::PopStyleColor(3);

        if (clk)
        {
            app::WarpTo(a, row.system, row.dest.zone, row.dest.sub);
            app::Log(a, "info", "-> warp " + row.system + " / " + display);
            if (global) a.clear_search_pending = true;
        }

        // Right side: system tag (during search) + cost
        std::string right;
        if (global) right = "[" + row.sys_long + "]";
        std::string cost_txt;
        if (row.dest.cost) cost_txt = util::fmt_thousands(*row.dest.cost);
        if (!right.empty() || !cost_txt.empty())
        {
            ImGui::SameLine();
            float tw = ImGui::CalcTextSize(right.c_str()).x
                     + (!right.empty() && !cost_txt.empty() ? 8.0f : 0.0f)
                     + ImGui::CalcTextSize(cost_txt.c_str()).x;
            float avail = ImGui::GetContentRegionAvail().x;
            if (tw < avail)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw));
            if (!cost_txt.empty())
            {
                ImU32 col = (afford && !*afford) ? theme::LOCKED : theme::MUTED;
                ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(col));
                ImGui::TextUnformatted(cost_txt.c_str());
                ImGui::PopStyleColor();
                if (!right.empty()) ImGui::SameLine();
            }
            if (!right.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::VIOLET));
                ImGui::TextUnformatted(right.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();
}
