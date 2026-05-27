// ui_systems.cpp - the SYSTEMS sidebar.
//
// Mirrors the Rust render_systems: one fixed-height slot per system, the
// dragged row floats on a foreground draw layer following the cursor, the
// other rows ease toward their target slots opening a gap at the insertion
// point. ImGui has no FLIP/animate_value, so we hand-roll the per-row lerp
// with an exponential decay (frame-rate independent).
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "app.h"
#include "imgui.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
    // All layout dims here are at 1.0x scale; multiply by ui::dpi() at use.
    constexpr float ROW_H_BASE   = 34.0f;
    constexpr float PAD_X_BASE   = 5.0f;
    constexpr float LABEL_Y_BASE = 3.0f;
    constexpr float COUNT_Y_BASE = 19.0f;
    constexpr float COUNT_PAD_BASE = 6.0f;
    constexpr float ANIM         = 0.13f; // settle time constant (seconds)

    // Per-key animated y-offset within the list (relative to list top).
    std::unordered_map<std::string, float>& row_anim()
    {
        static std::unordered_map<std::string, float> m;
        return m;
    }

    float ease(float current, float target, float dt, float tau)
    {
        // y' = target - (target - y) * exp(-dt/tau)
        if (tau <= 0.0f) return target;
        float k = 1.0f - std::exp(-dt / tau);
        return current + (target - current) * k;
    }

    struct Row
    {
        std::string                            key;
        std::string                            label;
        std::optional<std::pair<size_t,size_t>> counts;
        bool                                   partial;
        uint32_t                               total_count;
    };

    void paint_row_text(ImVec2 origin, const Row& row, ImU32 name_col, float scale)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pad_x   = PAD_X_BASE * scale;
        const float label_y = LABEL_Y_BASE * scale;
        const float count_y = COUNT_Y_BASE * scale;
        const float count_pad = COUNT_PAD_BASE * scale;

        dl->AddText(ImVec2(origin.x + pad_x, origin.y + label_y), name_col, row.label.c_str());

        if (row.counts || row.total_count > 0)
        {
            char buf[96];
            ImU32 col = theme::MUTED;
            if (row.counts)
            {
                auto [open, known] = *row.counts;
                col = (open == known) ? theme::UNLOCKED : theme::MUTED;
                std::snprintf(buf, sizeof(buf), "%zu/%zu unlocked%s",
                              open, known, row.partial ? " ~partial" : "");
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "%u destinations", row.total_count);
            }
            ImFont* f = ImGui::GetFont();
            float small = ImGui::GetFontSize() * 0.78f;
            dl->AddText(f, small, ImVec2(origin.x + pad_x + count_pad, origin.y + count_y),
                        col, buf);
        }
    }
}

void ui::Systems(App& a, float /*panel_height*/)
{
    const float scale = ui::dpi();
    const float ROW_H = ROW_H_BASE * scale;
    const float PAD_X = PAD_X_BASE * scale;

    ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
    ImGui::TextUnformatted("SYSTEMS");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2.0f * scale));

    // Build the ordered row data up front, mirroring effective_order().
    std::vector<Row> rows;
    {
        auto order = app::effective_order(a);
        rows.reserve(order.size());
        for (const auto& k : order)
        {
            auto it = std::find_if(a.systems.begin(), a.systems.end(),
                                   [&](const proto::SystemList& s){ return s.key == k; });
            if (it == a.systems.end()) continue;
            Row r;
            r.key   = it->key;
            r.label = it->long_.empty() ? it->key : it->long_;
            r.counts = app::unlock_counts(a, *it);
            r.partial = app::system_partial(a, it->key);
            r.total_count = it->count;
            rows.push_back(std::move(r));
        }
    }

    const int n = static_cast<int>(rows.size());
    if (n == 0)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::TextUnformatted("waiting for catalogue...");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##sysscroll", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImGui::PopStyleVar();

    const ImVec2 area_top = ImGui::GetCursorScreenPos();
    const float  width    = ImGui::GetContentRegionAvail().x;
    const float  total_h  = n * ROW_H;
    ImGui::Dummy(ImVec2(width, total_h));

    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    float dt = ImGui::GetIO().DeltaTime;

    // Float y of the dragged ghost (cursor.y - grab_offset), and the slot it
    // implies. Both nullopt when not dragging.
    std::optional<float> ghost_y;
    int ins = 0;
    if (a.drag_system)
    {
        ghost_y = mouse.y - a.drag_grab_offset;
        float rel = (*ghost_y - area_top.y) / ROW_H;
        ins = std::clamp(static_cast<int>(std::round(rel)), 0, std::max(0, n - 1));
    }

    // Build the "without dragged" key order; used to assign slots to non-drag rows.
    std::vector<const std::string*> without_k;
    without_k.reserve(rows.size());
    for (auto& r : rows)
        if (!a.drag_system || *a.drag_system != r.key)
            without_k.push_back(&r.key);

    std::optional<std::string> clicked;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- pass 1: non-drag rows (in place, with animation + interaction) ----
    for (int i = 0; i < n; ++i)
    {
        const Row& row = rows[i];
        const bool is_drag = a.drag_system && *a.drag_system == row.key;

        // Target y relative to area top.
        float target_rel;
        if (is_drag)
        {
            target_rel = ghost_y ? (*ghost_y - area_top.y) : 0.0f;
        }
        else
        {
            int j = 0;
            for (size_t k = 0; k < without_k.size(); ++k)
                if (*without_k[k] == row.key) { j = static_cast<int>(k); break; }
            int slot = a.drag_system ? (j < ins ? j : j + 1) : j;
            target_rel = slot * ROW_H;
        }

        // Lerp toward target.
        float& cur = row_anim()[row.key];
        if (is_drag) cur = target_rel; // pin to cursor; on release the lerp resumes.
        else         cur = ease(cur, target_rel, dt, ANIM * 0.45f);

        const float row_y = area_top.y + cur;
        const ImVec2 row_min(area_top.x, row_y);
        const ImVec2 row_max(area_top.x + width, row_y + ROW_H);

        if (!is_drag)
        {
            // Selection background.
            bool is_sel = a.selected_system && *a.selected_system == row.key;
            if (is_sel)
                dl->AddRectFilled(ImVec2(row_min.x, row_min.y + 1.0f * scale),
                                  ImVec2(row_max.x, row_max.y - 1.0f * scale),
                                  theme::HOVER, 3.0f * scale);

            paint_row_text(ImVec2(area_top.x, row_y), row,
                           is_sel ? theme::WHITE : theme::TEXT, scale);

            // Interaction overlay.
            ImGui::SetCursorScreenPos(row_min);
            std::string id = "##sysrow_" + row.key;
            ImGui::InvisibleButton(id.c_str(), ImVec2(width, ROW_H));
            bool hovered = ImGui::IsItemHovered();
            bool active  = ImGui::IsItemActive();

            if (hovered && !a.drag_system)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            // Begin drag once movement crosses a (DPI-scaled) threshold.
            if (active && !a.drag_system &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f * scale))
            {
                a.drag_system = row.key;
                a.drag_grab_offset = mouse.y - row_y;
            }
            // Click without drag = select.
            if (ImGui::IsItemDeactivated() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)
                && (!a.drag_system || *a.drag_system != row.key))
            {
                clicked = row.key;
            }
        }
    }

    // ---- pass 2: the gap indicator + the dragged tab on a foreground layer ----
    if (a.drag_system)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        // gap indicator
        float gap_y = area_top.y + ins * ROW_H;
        dl->AddRectFilled(
            ImVec2(area_top.x + 1.0f * scale, gap_y + 2.0f * scale),
            ImVec2(area_top.x + width - 1.0f * scale, gap_y + ROW_H - 2.0f * scale),
            theme::alpha(theme::AZURE, 26), 4.0f * scale);

        // dragged tab: drop shadow + filled tile on top.
        for (auto& row : rows)
        {
            if (row.key != *a.drag_system) continue;
            float cur = row_anim()[row.key];
            float row_y = area_top.y + cur;
            ImVec2 mn(area_top.x + 1.0f * scale, row_y + 1.0f * scale);
            ImVec2 mx(area_top.x + width - 1.0f * scale, row_y + ROW_H - 1.0f * scale);
            dl->AddRectFilled(ImVec2(mn.x + 3.0f * scale, mn.y + 4.0f * scale),
                              ImVec2(mx.x + 3.0f * scale, mx.y + 4.0f * scale),
                              IM_COL32(0, 0, 0, 110), 5.0f * scale);
            dl->AddRectFilled(mn, mx, theme::HOVER, 5.0f * scale);
            paint_row_text(ImVec2(area_top.x, row_y), row, theme::WHITE, scale);
            break;
        }

        // Drop commit on release.
        if (released)
        {
            std::vector<std::string> new_order;
            new_order.reserve(rows.size());
            for (auto& r : rows) new_order.push_back(r.key);
            auto it = std::find(new_order.begin(), new_order.end(), *a.drag_system);
            if (it != new_order.end())
            {
                std::string item = std::move(*it);
                new_order.erase(it);
                int to = std::min(ins, static_cast<int>(new_order.size()));
                new_order.insert(new_order.begin() + to, std::move(item));
                a.config.system_order = std::move(new_order);
                a.config.mark_dirty();
                a.config.save();
            }
            a.drag_system.reset();
        }
    }

    ImGui::EndChild();

    // Commit selection AFTER the child so the next frame draws with it.
    if (clicked)
    {
        a.selected_system = *clicked;
        a.config.last_system = *clicked;
        a.config.mark_dirty();
        a.view = View::Warp;
    }
}
