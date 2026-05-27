// ui.h - the superwarp companion panel.
// by Eric Strawser (Seicz@Bahamut) - ITIWH.com
#pragma once

namespace ui
{
    // Draws the entire panel into the current ImGui frame. Call once per frame
    // between ImGui::NewFrame() and ImGui::Render().
    void Render();
}
