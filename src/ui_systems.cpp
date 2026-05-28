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
    // Two row heights: tall for rows that show an unlock count or destination
    // count below the label, short for label-only rows (Mog Garden, Spatial
    // displacement, Walk Of Echoes, etc.) which otherwise have ~half the row
    // worth of dead space below them.
    constexpr float ROW_H_TALL_BASE  = 34.0f;
    constexpr float ROW_H_SHORT_BASE = 22.0f;
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

    inline bool row_has_meta(const Row& r)
    {
        return r.counts.has_value() || r.total_count > 0;
    }
    inline float row_height_base(const Row& r)
    {
        return row_has_meta(r) ? ROW_H_TALL_BASE : ROW_H_SHORT_BASE;
    }

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

    const size_t n = rows.size();
    if (n == 0)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::v4(theme::MUTED));
        ImGui::TextUnformatted("waiting for catalogue...");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##sysscroll", ImVec2(0, 0), ImGuiChildFlags_None, 0);
    ImGui::PopStyleVar();

    // ---- Per-row variable heights ----
    std::vector<float> heights(n);
    float total_h = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
        heights[i] = row_height_base(rows[i]) * scale;
        total_h += heights[i];
    }

    const ImVec2 area_top = ImGui::GetCursorScreenPos();
    const float  width    = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(width, total_h));

    const ImVec2 mouse    = ImGui::GetIO().MousePos;
    const bool   released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    const float  dt       = ImGui::GetIO().DeltaTime;

    // The row order with the dragged key removed, used to place every other row.
    std::vector<size_t> without_k_idx;
    without_k_idx.reserve(n);
    for (size_t i = 0; i < n; ++i)
        if (!a.drag_system || *a.drag_system != rows[i].key)
            without_k_idx.push_back(i);

    // Dragged row's own height (needed to open a gap of the right size).
    float dragged_h = 0.0f;
    if (a.drag_system)
    {
        for (size_t i = 0; i < n; ++i)
            if (rows[i].key == *a.drag_system) { dragged_h = heights[i]; break; }
    }

    // Floating tab y (cursor minus where it was grabbed). The insertion slot
    // is chosen by walking the cumulative height of the non-dragged rows and
    // finding which one's midpoint the ghost is closest to.
    std::optional<float> ghost_y;
    int ins = 0;
    if (a.drag_system)
    {
        ghost_y = mouse.y - a.drag_grab_offset;
        const float rel = *ghost_y - area_top.y;
        float acc = 0.0f;
        ins = static_cast<int>(without_k_idx.size()); // append by default
        for (size_t k = 0; k < without_k_idx.size(); ++k)
        {
            const float h = heights[without_k_idx[k]];
            if (rel < acc + h * 0.5f) { ins = static_cast<int>(k); break; }
            acc += h;
        }
        if (ins < 0) ins = 0;
    }

    // Compute target y for every row in one pass.
    std::vector<float> target_rel(n, 0.0f);
    {
        float y = 0.0f;
        for (size_t k = 0; k < without_k_idx.size(); ++k)
        {
            // Open the gap for the dragged row at slot `ins`.
            if (a.drag_system && static_cast<int>(k) == ins) y += dragged_h;
            const size_t orig = without_k_idx[k];
            target_rel[orig] = y;
            y += heights[orig];
        }
        if (a.drag_system)
        {
            for (size_t i = 0; i < n; ++i)
            {
                if (rows[i].key == *a.drag_system)
                {
                    target_rel[i] = ghost_y ? (*ghost_y - area_top.y) : 0.0f;
                    break;
                }
            }
        }
    }

    std::optional<std::string> clicked;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- pass 1: non-drag rows (in place, with animation + interaction) ----
    for (size_t i = 0; i < n; ++i)
    {
        const Row& row     = rows[i];
        const bool is_drag = a.drag_system && *a.drag_system == row.key;
        const float h      = heights[i];

        // Lerp toward target.
        float& cur = row_anim()[row.key];
        if (is_drag) cur = target_rel[i]; // pinned to cursor
        else         cur = ease(cur, target_rel[i], dt, ANIM * 0.45f);

        const float row_y = area_top.y + cur;
        const ImVec2 row_min(area_top.x, row_y);
        const ImVec2 row_max(area_top.x + width, row_y + h);

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
            ImGui::InvisibleButton(id.c_str(), ImVec2(width, h));
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

    // ---- pass 2: the gap indicator + the dragged tab ----
    if (a.drag_system)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        // Gap y = cumulative height of without_k rows before slot `ins`.
        float gap_y_offset = 0.0f;
        for (int k = 0; k < ins && k < static_cast<int>(without_k_idx.size()); ++k)
            gap_y_offset += heights[without_k_idx[k]];
        const float gap_y = area_top.y + gap_y_offset;
        dl->AddRectFilled(
            ImVec2(area_top.x + 1.0f * scale, gap_y + 2.0f * scale),
            ImVec2(area_top.x + width - 1.0f * scale, gap_y + dragged_h - 2.0f * scale),
            theme::alpha(theme::AZURE, 26), 4.0f * scale);

        // Dragged tab: drop shadow + filled tile on top.
        for (size_t i = 0; i < n; ++i)
        {
            if (rows[i].key != *a.drag_system) continue;
            const float cur = row_anim()[rows[i].key];
            const float row_y = area_top.y + cur;
            const float h = heights[i];
            ImVec2 mn(area_top.x + 1.0f * scale, row_y + 1.0f * scale);
            ImVec2 mx(area_top.x + width - 1.0f * scale, row_y + h - 1.0f * scale);
            dl->AddRectFilled(ImVec2(mn.x + 3.0f * scale, mn.y + 4.0f * scale),
                              ImVec2(mx.x + 3.0f * scale, mx.y + 4.0f * scale),
                              IM_COL32(0, 0, 0, 110), 5.0f * scale);
            dl->AddRectFilled(mn, mx, theme::HOVER, 5.0f * scale);
            paint_row_text(ImVec2(area_top.x, row_y), rows[i], theme::WHITE, scale);
            break;
        }

        // Drop commit on release.
        if (released)
        {
            std::vector<std::string> new_order;
            new_order.reserve(n);
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
