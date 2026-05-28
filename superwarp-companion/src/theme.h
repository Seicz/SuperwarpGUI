// theme.h - aether palette and ImGui style setup.
// Mirrors the Rust GUI's `setup_aether_theme` and color constants.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com
#pragma once

#include "imgui.h"

namespace theme
{
    // ---- Palette (RGBA, A=255 unless noted) ------------------------------
    inline constexpr ImU32 BG        = IM_COL32( 14,  15,  26, 255); // deep indigo-black
    inline constexpr ImU32 PANEL     = IM_COL32( 23,  26,  43, 255); // raised panel
    inline constexpr ImU32 INPUT     = IM_COL32( 10,  11,  20, 255); // sunken input
    inline constexpr ImU32 HOVER     = IM_COL32( 32,  37,  60, 255);

    inline constexpr ImU32 AZURE     = IM_COL32( 63, 200, 224, 255); // primary warp glow
    inline constexpr ImU32 AZURE_DIM = IM_COL32( 30, 100, 116, 255);
    inline constexpr ImU32 VIOLET    = IM_COL32(155, 108, 232, 255); // arcane highlight
    inline constexpr ImU32 TEXT      = IM_COL32(213, 216, 234, 255); // soft lavender-white
    inline constexpr ImU32 MUTED     = IM_COL32(107, 113, 148, 255);
    inline constexpr ImU32 WHITE     = IM_COL32(245, 247, 255, 255);

    inline constexpr ImU32 LOCKED    = IM_COL32(208,  85, 107, 255); // muted rose
    inline constexpr ImU32 UNLOCKED  = IM_COL32( 79, 208, 138, 255); // aether green
    inline constexpr ImU32 GOLD      = IM_COL32(220, 188, 110, 255); // currency
    inline constexpr ImU32 PARTY_COL = IM_COL32( 63, 200, 224, 255); // = AZURE
    inline constexpr ImU32 ALL_COL   = IM_COL32(232, 138,  96, 255); // warn coral

    // Same colors, but as ImVec4 (some ImGui APIs want floats).
    inline ImVec4 v4(ImU32 c)
    {
        return ImVec4(
            ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
            ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
            ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
            ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
    }
    inline ImU32 alpha(ImU32 c, int a)
    {
        return (c & 0x00FFFFFFu) | (static_cast<ImU32>(a & 0xFF) << IM_COL32_A_SHIFT);
    }

    // Push the aether palette into the current ImGui style.
    void setup(ImGuiStyle& style);
}
