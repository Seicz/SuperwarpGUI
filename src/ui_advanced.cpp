// ui_advanced.cpp - settings view: connection, diagnostics, about.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "app.h"
#include "imgui.h"
#include "net.h"
#include "theme.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    void section(const char* title)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::WHITE));
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 4));
    }
}

void ui::Advanced(App& a)
{
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
    ImGui::TextUnformatted("ADVANCED");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 6));

    // ---- Connection ----
    section("Connection");
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
    ImGui::TextUnformatted("host");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("##host", a.host_draft, sizeof(a.host_draft));
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(8.0f, 0)); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
    ImGui::TextUnformatted("port");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputText("##port", a.port_draft, sizeof(a.port_draft), ImGuiInputTextFlags_CharsDecimal);

    ImGui::Dummy(ImVec2(0, 4));
    bool do_apply = false;
    if (ImGui::SmallButton("apply + reconnect")) do_apply = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("defaults"))
    {
        std::snprintf(a.host_draft, sizeof(a.host_draft), "127.0.0.1");
        std::snprintf(a.port_draft, sizeof(a.port_draft), "19519");
        do_apply = true;
    }

    {
        const char* st;
        ImU32 col;
        if (net::GetStatus() == net::Status::Connected)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "connected to %s:%u", a.config.host.c_str(), (unsigned)a.config.port);
            st = buf; col = theme::UNLOCKED;
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(col));
            ImGui::TextUnformatted(buf);
            ImGui::PopStyleColor();
        }
        else if (!a.connect_error.empty())
        {
            std::string s = "offline -- " + a.connect_error;
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
            ImGui::TextUnformatted(s.c_str());
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
            ImGui::TextUnformatted("offline");
            ImGui::PopStyleColor();
        }
    }

    if (do_apply)
    {
        const char* h = a.host_draft;
        // trim
        while (*h == ' ' || *h == '\t') h++;
        std::string host(h);
        while (!host.empty() && (host.back() == ' ' || host.back() == '\t')) host.pop_back();
        int p = std::atoi(a.port_draft);
        if (!host.empty() && p > 0 && p <= 65535)
        {
            app::Log(a, "info", "reconnecting to " + host + ":" + std::to_string(p));
            app::Reconnect(a, host, (uint16_t)p);
        }
        else
        {
            app::Log(a, "error", "invalid host/port");
        }
    }

    ImGui::Dummy(ImVec2(0, 10));

    // ---- Diagnostics ----
    section("Diagnostics");
    bool connected = (net::GetStatus() == net::Status::Connected);
    bool dbg = a.addon_debug;
    if (!connected) ImGui::BeginDisabled();
    if (ImGui::Checkbox("addon debug (//sw debug)", &dbg))
    {
        app::SetDebug(a, dbg);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Toggle the addon's verbose logging. The addon\n"
                          "reports its state back, so this checkbox\n"
                          "reflects what //sw debug currently is in-game.");
    if (!connected) ImGui::EndDisabled();

    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
    ImGui::Text("%zu log lines", a.log_lines.size());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("clear")) a.log_lines.clear();

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::v4(theme::INPUT));
    ImGui::BeginChild("##logconsole", ImVec2(0, 220.0f * ui::dpi()), ImGuiChildFlags_Borders);
    if (a.log_lines.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::TextUnformatted("(no log yet)");
        ImGui::PopStyleColor();
    }
    for (const auto& l : a.log_lines)
    {
        ImU32 col = (l.level == "error")   ? theme::LOCKED
                  : (l.level == "warning") ? theme::GOLD
                  : (l.level == "debug")   ? theme::AZURE_DIM
                                           : theme::MUTED;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(col));
        ImGui::TextUnformatted(l.text.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0, 10));

    // ---- About ----
    section("About");
    const char* av = a.addon_version.empty() ? "(not connected)" : a.addon_version.c_str();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
    ImGui::Text("GUI v%s   addon v%s", GUI_VERSION, av);
    ImGui::TextUnformatted("by Eric Strawser (Seicz@Bahamut) - ITIWH.com");
    ImGui::PopStyleColor();
}
