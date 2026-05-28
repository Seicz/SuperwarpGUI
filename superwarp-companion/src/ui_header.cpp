// ui_header.cpp - banner with painted double-chevron, character/zone, status.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "app.h"
#include "imgui.h"
#include "net.h"
#include "theme.h"

#include <algorithm>

namespace
{
    // Painted multicolor double-chevron (violet back + azure front). A two-tone
    // glyph isn't drawable as a single text rune, so we paint geometry.
    // Mirror of the Rust render_header chevron block.
    void paint_chevron(const ImVec2& top_left, float h)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float half = 0.30f * h;       // vertical half-span
        const float dx   = 0.50f * h;       // arm reach (larger = sharper)
        const float sw   = std::max(0.17f * h, 2.0f);
        const float gap  = 0.32f * h;
        const float cy   = top_left.y + h * 0.5f;
        const float lx0  = top_left.x + sw * 0.5f;

        auto chev = [&](float lx, ImU32 color)
        {
            ImVec2 t(lx,      cy - half);
            ImVec2 ap(lx + dx, cy);
            ImVec2 b(lx,      cy + half);
            dl->AddLine(t,  ap, color, sw);
            dl->AddLine(ap, b,  color, sw);
            const float cap = sw * 0.5f;
            dl->AddCircleFilled(t,  cap, color);
            dl->AddCircleFilled(ap, cap, color);
            dl->AddCircleFilled(b,  cap, color);
        };
        chev(lx0,       theme::VIOLET);
        chev(lx0 + gap, theme::AZURE);
    }
}

namespace
{
    void RightAlign(float widget_w)
    {
        float avail = ImGui::GetContentRegionAvail().x;
        if (widget_w < avail)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - widget_w));
    }
}

void ui::Header(App& a)
{
    const float scale = ui::dpi();

    // Row 1: chevron + title + version  |  pin + advanced + status
    const float row_h = 22.0f * scale;
    ImVec2 origin = ImGui::GetCursorScreenPos();

    // chevron
    const float chev_h = 18.0f * scale;
    const float chev_w = chev_h * 1.15f;
    paint_chevron(ImVec2(origin.x, origin.y + (row_h - chev_h) * 0.5f), chev_h);
    ImGui::Dummy(ImVec2(chev_w + 4.0f * scale, row_h));
    ImGui::SameLine();

    // title -- bigger than the rest of the UI to mirror the Rust GUI's
    // monospace(16) vs body monospace(11) ratio.
    {
        ImFont* f = ImGui::GetFont();
        const float title_size = ImGui::GetFontSize() * 1.4f;
        const char* title = "superwarp";
        const ImVec2 ts = f->CalcTextSizeA(title_size, FLT_MAX, 0.0f, title);
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        const float ty = cp.y + (row_h - ts.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(f, title_size, ImVec2(cp.x, ty),
                                            theme::WHITE, title);
        ImGui::Dummy(ImVec2(ts.x, row_h));
        ImGui::SameLine();
    }

    // version
    {
        std::string v = a.addon_version.empty() ? "..." : a.addon_version;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::Text("v%s", v.c_str());
        ImGui::PopStyleColor();
    }

    // right-aligned cluster: status | advanced toggle | pin
    {
        // Estimate the cluster width (approx) and right-align via SameLine spacer.
        const char* status_txt = (net::GetStatus() == net::Status::Connected) ? "● connected"
                              : (net::GetStatus() == net::Status::Connecting)? "● connecting..."
                                                                             : "● offline";
        bool is_adv = (a.view == View::Advanced);
        const char* adv_txt = is_adv ? "warp" : "advanced";

        float status_w = ImGui::CalcTextSize(status_txt).x;
        float adv_w    = ImGui::CalcTextSize(adv_txt).x + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f;
        float pin_w    = ImGui::CalcTextSize("pin").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight() + 4.0f;
        float total_w  = status_w + adv_w + pin_w + 24.0f;

        ImGui::SameLine();
        float avail = ImGui::GetContentRegionAvail().x;
        if (total_w < avail)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - total_w));

        bool pin = a.always_on_top;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        if (ImGui::Checkbox("pin", &pin))
        {
            a.always_on_top = pin;
            a.config.always_on_top = pin;
            a.config.mark_dirty();
            // Window-level toggle handled in main.cpp by polling a.always_on_top.
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // advanced/warp toggle button
        ImGui::PushStyleColor(ImGuiCol_Text, is_adv ? theme::v4(theme::AZURE) : theme::v4(theme::MUTED));
        if (ImGui::SmallButton(adv_txt))
        {
            a.view = is_adv ? View::Warp : View::Advanced;
            if (a.view == View::Advanced)
            {
                std::snprintf(a.host_draft, sizeof(a.host_draft), "%s", a.config.host.c_str());
                std::snprintf(a.port_draft, sizeof(a.port_draft), "%u", (unsigned)a.config.port);
            }
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();

        ImU32 sc = (net::GetStatus() == net::Status::Connected) ? theme::AZURE
                : (net::GetStatus() == net::Status::Connecting)? theme::GOLD
                                                                : theme::LOCKED;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(sc));
        ImGui::TextUnformatted(status_txt);
        ImGui::PopStyleColor();
    }

    // Accent line
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x, p.y + 2.0f * scale),
                          ImVec2(p.x + w, p.y + 4.0f * scale), theme::AZURE);
        ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    }

    // Row 2: character + zone | BUSY + cancel
    ImGui::BeginGroup();
    if (a.character.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::TextUnformatted("not logged in");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::VIOLET));
        ImGui::TextUnformatted(a.character.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::TEXT));
        ImGui::Text("@ %s (%u)", a.zone.c_str(), a.zone_id);
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();

    if (a.busy)
    {
        ImGui::SameLine();
        const char* busy_txt = "BUSY";
        float busy_w = ImGui::CalcTextSize(busy_txt).x + 8.0f;
        float cancel_w = ImGui::CalcTextSize("cancel").x + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f;
        float total = busy_w + cancel_w;
        float avail = ImGui::GetContentRegionAvail().x;
        if (total < avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - total));

        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::GOLD));
        ImGui::TextUnformatted(busy_txt);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::LOCKED));
        if (ImGui::SmallButton("cancel")) app::Cancel(a);
        ImGui::PopStyleColor();
    }
}
