#include "stdafx.h"

#include "Headers/SceneGUIElements.h"

#include "Headers/GUI.h"
#include "Headers/GUIFlash.h"
#include "Headers/GUIText.h"

#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {

SceneGUIElements::SceneGUIElements(Scene& parentScene, GUI& context)
    : GUIInterface(context),
      SceneComponent(parentScene)
{
}


void SceneGUIElements::draw(const GFXDevice& context, GFX::CommandBuffer& bufferInOut) {
    const GUIMap& map = _guiElements[to_base(GUIType::GUI_TEXT)];
    _drawTextCommand._batch.data().resize(map.size());

    U32 idx = 0;
    for (const GUIMap::value_type& guiStackIterator : map) {
        GUIText& textElement = static_cast<GUIText&>(*guiStackIterator.second.first);
        if (textElement.visible() && !textElement.text().empty()) {
            _drawTextCommand._batch.data()[idx++] = static_cast<TextElement>(textElement);
        }
    }

    if (idx > 0) {
        _drawTextCommand._batch.data().resize(idx);
        Attorney::GFXDeviceGUI::drawText(context, _drawTextCommand, bufferInOut);
    }
}

void SceneGUIElements::onEnable() {
    for (U8 i = 0; i < to_base(GUIType::COUNT); ++i) {
        for (const GUIMap::value_type& guiStackIterator : _guiElements[i]) {
            guiStackIterator.second.first->visible(guiStackIterator.second.second);
        }
    }
}

void SceneGUIElements::onDisable() {
    for (U8 i = 0; i < to_base(GUIType::COUNT); ++i) {
        for (const GUIMap::value_type& guiStackIterator : _guiElements[i]) {
            guiStackIterator.second.first->visible(false);
        }
    }
}

} // namespace Divide