#include "stdafx.h"

#include "Headers/StatusBar.h"

#include "Core/Headers/PlatformContext.h"
#include "Editor/Headers/Editor.h"

#include <imgui_internal.h>

namespace Divide {

namespace {
    bool BeginBar() {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;
        if (!(window->Flags & ImGuiWindowFlags_MenuBar))
            return false;

        IM_ASSERT(!window->DC.MenuBarAppending);
        ImGui::BeginGroup(); // Backup position on layer 0 // FIXME: Misleading to use a group for that backup/restore
        ImGui::PushID("##statusbar");

        // We don't clip with current window clipping rectangle as it is already set to the area below. However we clip with window full rect.
        // We remove 1 worth of rounding to Max.x to that text in long menus and small windows don't tend to display over the lower-right rounded area, which looks particularly glitchy.
        const ImRect bar_rect = window->MenuBarRect();
        ImRect clip_rect(ImFloor(bar_rect.Min.x + window->WindowBorderSize + 0.5f), ImFloor(bar_rect.Min.y + window->WindowBorderSize + 0.5f), ImFloor(ImMax(bar_rect.Min.x, bar_rect.Max.x - ImMax(window->WindowRounding, window->WindowBorderSize)) + 0.5f), ImFloor(bar_rect.Max.y + 0.5f));
        clip_rect.ClipWith(window->OuterRectClipped);
        ImGui::PushClipRect(clip_rect.Min, clip_rect.Max, false);

        window->DC.CursorPos = ImVec2(bar_rect.Min.x + window->DC.MenuBarOffset.x, bar_rect.Min.y + window->DC.MenuBarOffset.y);
        window->DC.LayoutType = ImGuiLayoutType_Horizontal;
        window->DC.NavLayerCurrent = ImGuiNavLayer_Menu;
        window->DC.MenuBarAppending = true;
        ImGui::AlignTextToFramePadding();
        return true;
    }

    void EndBar() {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window == nullptr || window->SkipItems) {
            return;
        }

        ImGuiContext& g = *GImGui;

        // Nav: When a move request within one of our child menu failed, capture the request to navigate among our siblings.
        if (ImGui::NavMoveRequestButNoResultYet() && (g.NavMoveDir == ImGuiDir_Left || g.NavMoveDir == ImGuiDir_Right) && (g.NavWindow->Flags & ImGuiWindowFlags_ChildMenu))
        {
            ImGuiWindow* nav_earliest_child = g.NavWindow;
            while (nav_earliest_child->ParentWindow && (nav_earliest_child->ParentWindow->Flags & ImGuiWindowFlags_ChildMenu))
                nav_earliest_child = nav_earliest_child->ParentWindow;
            if (nav_earliest_child->ParentWindow == window && nav_earliest_child->DC.ParentLayoutType == ImGuiLayoutType_Horizontal && g.NavMoveRequestForward == ImGuiNavForward_None)
            {
                // To do so we claim focus back, restore NavId and then process the movement request for yet another frame.
                // This involve a one-frame delay which isn't very problematic in this situation. We could remove it by scoring in advance for multiple window (probably not worth the hassle/cost)
                const ImGuiNavLayer layer = ImGuiNavLayer_Menu;
                IM_ASSERT(window->DC.NavLayersActiveMaskNext & (1 << layer)); // Sanity check
                ImGui::FocusWindow(window);
                ImGui::SetNavID(window->NavLastIds[layer], layer, 0, window->NavRectRel[layer]);
                g.NavLayer = layer;
                g.NavDisableHighlight = true; // Hide highlight for the current frame so we don't see the intermediary selection.
                g.NavMoveRequestForward = ImGuiNavForward_ForwardQueued;
                ImGui::NavMoveRequestCancel();
            }
        }

        IM_ASSERT(window->Flags & ImGuiWindowFlags_MenuBar);
        IM_ASSERT(window->DC.MenuBarAppending);
        ImGui::PopClipRect();
        ImGui::PopID();
        window->DC.MenuBarOffset.x = window->DC.CursorPos.x - window->MenuBarRect().Min.x; // Save horizontal position so next append can reuse it. This is kinda equivalent to a per-layer CursorPos.
        g.GroupStack.back().EmitItem = false;
        ImGui::EndGroup(); // Restore position on layer 0
        window->DC.LayoutType = ImGuiLayoutType_Vertical;
        window->DC.NavLayerCurrent = ImGuiNavLayer_Main;
        window->DC.MenuBarAppending = false;
    }

    bool BeginStatusBar() {
        ImGuiContext& g = *GImGui;
        ImGuiViewport* viewport = g.Viewports[0];
        g.NextWindowData.MenuBarOffsetMinVal = ImVec2(g.Style.DisplaySafeAreaPadding.x, ImMax(g.Style.DisplaySafeAreaPadding.y - g.Style.FramePadding.y, 0.0f));
        const F32 height = g.NextWindowData.MenuBarOffsetMinVal.y + g.FontBaseSize + g.Style.FramePadding.y;
        ImGui::SetNextWindowPos(viewport->Pos + ImVec2(0.0f, g.IO.DisplaySize.y - height));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, height));
        ImGui::SetNextWindowViewport(viewport->ID); // Enforce viewport so we don't create our onw viewport when ImGuiConfigFlags_ViewportsNoMerge is set.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0, 0));
        const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
        const bool is_open = ImGui::Begin("##StatusBar", nullptr, window_flags) && BeginBar();
        ImGui::PopStyleVar(2);
        g.NextWindowData.MenuBarOffsetMinVal = ImVec2(0.0f, 0.0f);
        if (!is_open)
        {
            ImGui::End();
            return false;
        }
        return true; //-V1020
    }

    void EndStatusBar() {
        EndBar();

        // When the user has left the menu layer (typically: closed menus through activation of an item), we restore focus to the previous window
        // FIXME: With this strategy we won't be able to restore a NULL focus.
        ImGuiContext& g = *GImGui;
        if (g.CurrentWindow == g.NavWindow && g.NavLayer == 0 && !g.NavAnyRequest) {
            ImGui::FocusTopMostWindowUnderOne(g.NavWindow, nullptr);
        }

        ImGui::End();
    }
}


StatusBar::StatusBar(PlatformContext& context)
    : PlatformContextComponent(context),
      _lastMessageDurationMS(0.0f)
{

}

void StatusBar::draw() const {
    if (BeginStatusBar())
    {
        if (!_lastMessage.empty()) {
            ImGui::Text(_lastMessage.c_str());
        }
        EndStatusBar();
    }
}

void StatusBar::update(const U64 deltaTimeUS) {
    if (_lastMessageDurationMS > 0.0f) {
        _lastMessageDurationMS -= Time::MicrosecondsToMilliseconds(deltaTimeUS);
        if (_lastMessageDurationMS < 0.0f) {
            _lastMessage.clear();
        }
    }
}

void StatusBar::showMessage(const string& message, const F32 durationMS) {
    _lastMessage = message;
    _lastMessageDurationMS = durationMS;
}

F32 StatusBar::height() const {
    ImGuiContext& g = *GImGui;
    return std::max(1.0f, g.NextWindowData.MenuBarOffsetMinVal.y) + g.FontBaseSize + g.Style.FramePadding.y;
}

} //namespace Divide