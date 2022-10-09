#include "stdafx.h"

#include "Headers/StatusBar.h"

#include "Core/Headers/PlatformContext.h"
#include "Editor/Headers/Editor.h"

#include <imgui_internal.h>

namespace Divide {

StatusBar::StatusBar(PlatformContext& context) noexcept
    : PlatformContextComponent(context)
{
}

void StatusBar::draw() const {
    PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

    ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    if (ImGui::BeginViewportSideBar("##MainStatusBar", viewport, ImGuiDir_Down, ImGui::GetFrameHeight(), window_flags))
    {
        if (ImGui::BeginMenuBar())
        {
            if (!_messages.empty()) {
                const Message& frontMsg = _messages.front();
                if (!frontMsg._text.empty()) {
                    if (frontMsg._error) {
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 50, 0, 255));
                    }

                    ImGui::Text(frontMsg._text.c_str());

                    if (frontMsg._error) {
                        ImGui::PopStyleColor();
                    }
                }
            }
            ImGui::EndMenuBar();
        }
        ImGui::End();
    }
}

void StatusBar::update(const U64 deltaTimeUS) noexcept {
    if (_messages.empty()) {
        return;
    }
    
    Message& frontMsg = _messages.front();
    if (frontMsg._durationMS > 0.f) {
        frontMsg._durationMS -= Time::MicrosecondsToMilliseconds(deltaTimeUS);
        if (frontMsg._text.empty() || frontMsg._durationMS < 0.f) {
            _messages.pop();
        }
    }
}

void StatusBar::showMessage(const string& message, const F32 durationMS, const bool error) {
    _messages.push({message, durationMS, error});
}

} //namespace Divide