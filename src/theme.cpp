// theme.cpp - configure the ImGui style to match the Rust GUI's aether theme.
//
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com

#include "theme.h"

void theme::setup(ImGuiStyle& s)
{
    // base
    ImGui::StyleColorsDark();

    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 3.0f;
    s.FrameRounding     = 3.0f;
    s.GrabRounding      = 3.0f;
    s.PopupRounding     = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.TabRounding       = 3.0f;
    s.ItemSpacing       = ImVec2(8.0f, 5.0f);
    s.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    s.FramePadding      = ImVec2(6.0f, 3.0f);
    s.WindowBorderSize  = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.ChildBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;

    ImVec4* c = s.Colors;

    c[ImGuiCol_WindowBg]          = v4(BG);
    c[ImGuiCol_ChildBg]           = v4(BG);
    c[ImGuiCol_PopupBg]           = v4(PANEL);
    c[ImGuiCol_Border]            = v4(AZURE_DIM);

    c[ImGuiCol_FrameBg]           = v4(INPUT);
    c[ImGuiCol_FrameBgHovered]    = v4(PANEL);
    c[ImGuiCol_FrameBgActive]     = v4(HOVER);

    c[ImGuiCol_TitleBg]           = v4(PANEL);
    c[ImGuiCol_TitleBgActive]     = v4(PANEL);
    c[ImGuiCol_TitleBgCollapsed]  = v4(PANEL);
    c[ImGuiCol_MenuBarBg]         = v4(PANEL);

    c[ImGuiCol_ScrollbarBg]       = v4(BG);
    c[ImGuiCol_ScrollbarGrab]     = v4(PANEL);
    c[ImGuiCol_ScrollbarGrabHovered] = v4(HOVER);
    c[ImGuiCol_ScrollbarGrabActive]  = v4(AZURE_DIM);

    c[ImGuiCol_CheckMark]         = v4(AZURE);
    c[ImGuiCol_SliderGrab]        = v4(AZURE_DIM);
    c[ImGuiCol_SliderGrabActive]  = v4(AZURE);

    c[ImGuiCol_Button]            = v4(PANEL);
    c[ImGuiCol_ButtonHovered]     = v4(HOVER);
    c[ImGuiCol_ButtonActive]      = v4(AZURE_DIM);

    c[ImGuiCol_Header]            = v4(HOVER);
    c[ImGuiCol_HeaderHovered]     = v4(HOVER);
    c[ImGuiCol_HeaderActive]      = v4(AZURE_DIM);

    c[ImGuiCol_Separator]         = v4(PANEL);
    c[ImGuiCol_SeparatorHovered]  = v4(AZURE_DIM);
    c[ImGuiCol_SeparatorActive]   = v4(AZURE);

    c[ImGuiCol_Tab]               = v4(PANEL);
    c[ImGuiCol_TabHovered]        = v4(HOVER);
    c[ImGuiCol_TabActive]         = v4(AZURE_DIM);
    c[ImGuiCol_TabUnfocused]      = v4(PANEL);
    c[ImGuiCol_TabUnfocusedActive]= v4(AZURE_DIM);

    c[ImGuiCol_Text]              = v4(TEXT);
    c[ImGuiCol_TextDisabled]      = v4(MUTED);
    c[ImGuiCol_TextSelectedBg]    = v4(AZURE_DIM);
}
