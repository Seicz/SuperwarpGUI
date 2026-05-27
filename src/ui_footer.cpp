// ui_footer.cpp - the bottom strip: most-recent log lines + offline reason.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "app.h"
#include "imgui.h"
#include "net.h"
#include "theme.h"

void ui::Footer(App& a)
{
    // last 3 log lines, oldest -> newest
    const size_t n = a.log_lines.size();
    const size_t start = (n > 3) ? (n - 3) : 0;
    for (size_t i = start; i < n; ++i)
    {
        const auto& l = a.log_lines[i];
        ImU32 col = (l.level == "error")   ? theme::LOCKED
                  : (l.level == "warning") ? theme::GOLD
                                           : theme::MUTED;
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(col));
        ImGui::TextUnformatted(l.text.c_str());
        ImGui::PopStyleColor();
    }

    if (!a.connect_error.empty() && net::GetStatus() != net::Status::Connected)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::TextUnformatted(a.connect_error.c_str());
        ImGui::PopStyleColor();
    }
}
