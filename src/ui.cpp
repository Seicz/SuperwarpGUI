// ui.cpp - the superwarp companion panel.
//
// Every superwarp warp system is described as data (kSystems). The panel is
// rendered generically from that table, so adding a system or sub-command is a
// one-line edit rather than new UI code. The command strings produced match
// AkadenTK/superwarp's documented surface exactly:
//
//   warp:     sw <prefix> [target] <destination>
//   subcmd:   sw <prefix> [target] <token>
//   missing:  sw <prefix> [target] missing
//   smart:    sw [target]
//   cancel:   sw cancel [target]     (target trails for the misc verbs)
//   reset:    sw reset [target]
//
// [target] is one of "" (me), "party", or "all".
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "ui.h"
#include "net.h"
#include "imgui.h"

#include <vector>
#include <string>
#include <ctime>
#include <cstdio>

namespace
{
    // ---- Command model --------------------------------------------------

    struct SubCmd
    {
        const char* label;  // button text
        const char* token;  // appended after prefix+target ("" = bare prefix)
    };

    struct WarpSystem
    {
        const char* label;
        const char* prefix;       // superwarp system command ("hp", "wp", ...)
        bool        needs_dest;   // show destination input + Warp button
        const char* dest_hint;    // input placeholder
        std::vector<SubCmd> subs; // sub-command buttons
        bool        has_missing;  // show a per-system "missing" button
    };

    // Mirrors superwarp's README command tables.
    const std::vector<WarpSystem> kSystems =
    {
        { "Homepoint",      "hp",  true,  "zone (e.g. Norg 2, southernsand)", { {"Set nearest","set"} }, true },
        { "Waypoint",       "wp",  true,  "zone (e.g. fs, Foret de Hennetiel 4)", {}, true },
        { "Proto-Waypoint", "pwp", true,  "zone", {}, true },
        { "Survival Guide", "sg",  true,  "zone", {}, true },
        { "Escha / Reis",   "ew",  true,  "portal # (within zone)",
            { {"Enter","enter"}, {"Domain","domain"}, {"Domain return","domain return"}, {"Exit","exit"} }, true },
        { "Unity",          "un",  true,  "zone or NM name", {}, true },
        { "Abyssea",        "ab",  true,  "zone or conflux #",
            { {"Enter","enter"}, {"Exit","exit"} }, true },
        { "Runic Portal",   "po",  true,  "staging point",
            { {"Assault","assault"}, {"Return","return"} }, true },
        { "Voidwatch",      "vw",  true,  "zone", {}, true },
        { "Sortie",         "so",  true,  "s/a/b/c/d  or  0-4",
            { {"Port","port"}, {"Aminon normal","normal"}, {"Aminon hard","hard"} }, false },
        { "Odyssey",        "od",  true,  "1 / 2 / 3",
            { {"Port","port"}, {"Exit","exit"} }, false },
        { "Limbus",         "li",  true,  "nw1 / sw2 / ne3 / se4",
            { {"Port","port"}, {"Back","back"}, {"Next","next"}, {"Random","random"}, {"Enter","enter"}, {"Exit","exit"} }, false },
        { "Campaign",       "ca",  false, "",
            { {"Warp to [S]","" }, {"Return","return"} }, false },
        { "Incursion",      "in",  true,  "part of boss name",
            {}, true },
        { "Walk of Echoes", "woe", false, "",
            { {"Zone","zone"}, {"Enter","enter"}, {"Exit","exit"} }, false },
    };

    enum class Target { Me, Party, All };

    Target      g_target   = Target::Me;
    int         g_selected = 0;
    std::vector<std::string> g_log;

    // Per-system destination buffers (preserved when switching systems).
    char g_dest[64][128] = {};

    // ---- Helpers --------------------------------------------------------

    const char* TargetToken(Target t)
    {
        switch (t) { case Target::Party: return "party"; case Target::All: return "all"; default: return ""; }
    }

    // Join non-empty tokens with single spaces.
    std::string Join(std::initializer_list<const char*> parts)
    {
        std::string out;
        for (const char* p : parts)
        {
            if (!p || !*p) continue;
            if (!out.empty()) out.push_back(' ');
            out += p;
        }
        return out;
    }

    void LogLine(const std::string& s)
    {
        char ts[16];
        std::time_t t = std::time(nullptr);
        std::tm lt{};
    #if defined(_WIN32)
        localtime_s(&lt, &t);
    #else
        lt = *std::localtime(&t);
    #endif
        std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
        g_log.push_back(std::string("[") + ts + "] " + s);
        if (g_log.size() > 200) g_log.erase(g_log.begin());
    }

    // Dispatch a finished command: log it, then hand to the network worker.
    void Dispatch(const std::string& cmd)
    {
        if (cmd.empty()) return;
        LogLine("-> " + cmd);
        net::SendLine(cmd);
    }

    // ---- Sub-widgets ----------------------------------------------------

    void DrawStatusBadge()
    {
        net::Status st = net::GetStatus();
        ImVec4 col = (st == net::Status::Connected)  ? ImVec4(0.30f, 0.78f, 0.40f, 1.0f)
                   : (st == net::Status::Connecting) ? ImVec4(0.90f, 0.75f, 0.25f, 1.0f)
                                                     : ImVec4(0.70f, 0.32f, 0.32f, 1.0f);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s  (%s:%u)", net::StatusText(), net::Host(), (unsigned)net::Port());
        ImVec2 sz = ImGui::CalcTextSize(buf);
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Dummy(ImVec2(avail - sz.x - 16.0f, 0.0f));
        ImGui::SameLine();
        ImGui::TextColored(col, "%s", buf);
    }

    void DrawTargetSelector()
    {
        ImGui::TextUnformatted("Send to:");
        ImGui::SameLine();
        struct { const char* label; Target t; } opts[] =
            { {"Me", Target::Me}, {"Party", Target::Party}, {"All", Target::All} };
        for (int i = 0; i < 3; ++i)
        {
            if (i) ImGui::SameLine();
            bool active = (g_target == opts[i].t);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(opts[i].label, ImVec2(64, 0)))
                g_target = opts[i].t;
            if (active) ImGui::PopStyleColor();
        }
    }

    void DrawGlobalControls()
    {
        const char* tt = TargetToken(g_target);
        if (ImGui::Button("Cancel", ImVec2(80, 0)))  Dispatch(Join({ "sw", "cancel", tt }));
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(80, 0)))   Dispatch(Join({ "sw", "reset", tt }));
        ImGui::SameLine();
        if (ImGui::Button("Missing", ImVec2(80, 0)))  Dispatch(Join({ "sw", "missing" }));
        ImGui::SameLine();
        if (ImGui::Button("Debug", ImVec2(80, 0)))    Dispatch(Join({ "sw", "debug" }));
    }

    void DrawSystemDetail(const WarpSystem& sys, int idx)
    {
        const char* tt = TargetToken(g_target);

        ImGui::TextUnformatted(sys.label);
        ImGui::TextDisabled("command prefix: sw %s", sys.prefix);
        ImGui::Separator();
        ImGui::Spacing();

        if (sys.needs_dest)
        {
            ImGui::TextUnformatted("Destination");
            ImGui::SetNextItemWidth(-1.0f);
            char id[32]; std::snprintf(id, sizeof(id), "##dest%d", idx);
            ImGui::InputTextWithHint(id, sys.dest_hint, g_dest[idx], sizeof(g_dest[idx]));

            bool has = g_dest[idx][0] != '\0';
            if (!has) ImGui::BeginDisabled();
            if (ImGui::Button("Warp", ImVec2(120, 0)))
                Dispatch(Join({ "sw", sys.prefix, tt, g_dest[idx] }));
            if (!has) ImGui::EndDisabled();
            ImGui::Spacing();
        }

        if (!sys.subs.empty())
        {
            ImGui::TextUnformatted("Sub-commands");
            float avail = ImGui::GetContentRegionAvail().x;
            float x = 0.0f;
            for (size_t i = 0; i < sys.subs.size(); ++i)
            {
                const SubCmd& sc = sys.subs[i];
                ImVec2 bs(0, 0);
                float w = ImGui::CalcTextSize(sc.label).x + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
                if (i && x + w < avail) ImGui::SameLine();
                else x = 0.0f;
                if (ImGui::Button(sc.label))
                    Dispatch(Join({ "sw", sys.prefix, tt, sc.token }));
                x += w + ImGui::GetStyle().ItemSpacing.x;
            }
            ImGui::Spacing();
        }

        if (sys.has_missing)
        {
            ImGui::Separator();
            if (ImGui::Button("List missing destinations", ImVec2(-1, 0)))
                Dispatch(Join({ "sw", sys.prefix, tt, "missing" }));
        }
    }
}

void ui::Render()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##root", nullptr, flags);

    // Header
    ImGui::TextUnformatted("superwarp companion");
    ImGui::SameLine();
    ImGui::TextDisabled("- Dear ImGui demo");
    DrawStatusBadge();
    ImGui::Separator();
    ImGui::Spacing();

    // Smart command + target + globals
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.52f, 0.62f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.62f, 0.72f, 1.0f));
    if (ImGui::Button("Smart Warp   //sw", ImVec2(-1, 36)))
        Dispatch(Join({ "sw", TargetToken(g_target) }));
    ImGui::PopStyleColor(2);
    ImGui::Spacing();

    DrawTargetSelector();
    ImGui::Spacing();
    DrawGlobalControls();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Two-pane: systems list | detail
    const float listW = 190.0f;
    const float logH  = 150.0f;
    float bodyH = ImGui::GetContentRegionAvail().y - logH - ImGui::GetStyle().ItemSpacing.y;

    ImGui::BeginChild("##systems", ImVec2(listW, bodyH), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)kSystems.size(); ++i)
    {
        if (ImGui::Selectable(kSystems[i].label, g_selected == i))
            g_selected = i;
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##detail", ImVec2(0, bodyH), ImGuiChildFlags_Borders);
    if (g_selected >= 0 && g_selected < (int)kSystems.size())
        DrawSystemDetail(kSystems[g_selected], g_selected);
    ImGui::EndChild();

    // Log
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("Outgoing commands (relayed to hub when connected)");
    ImGui::Separator();
    for (const std::string& l : g_log)
        ImGui::TextUnformatted(l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}
