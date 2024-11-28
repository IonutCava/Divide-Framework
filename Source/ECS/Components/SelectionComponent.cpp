

#include "Headers/SelectionComponent.h"

namespace Divide
{
    SelectionComponent::SelectionComponent(SceneGraphNode* parentSGN, PlatformContext& context)
        : BaseComponentType<SelectionComponent, ComponentType::SELECTION>(parentSGN, context)
    {
        EditorComponentField hoverHighlightField = {};
        hoverHighlightField._name = "Show Hover Highlight";
        hoverHighlightField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data) { *static_cast<bool*>(dataOut) = hoverHighlightEnabled(); };
        hoverHighlightField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data) { hoverHighlightEnabled(*static_cast<const bool*>(data)); };
        hoverHighlightField._type = EditorComponentFieldType::SWITCH_TYPE;
        hoverHighlightField._basicType = PushConstantType::BOOL;
        hoverHighlightField._readOnly = false;

        _editorComponent.registerField(MOV(hoverHighlightField));


        EditorComponentField selectHighlightField = {};
        selectHighlightField._name = "Show Select Highlight";
        selectHighlightField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data) { *static_cast<bool*>(dataOut) = selectionHighlightEnabled(); };
        selectHighlightField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data) { selectionHighlightEnabled(*static_cast<const bool*>(data)); };
        selectHighlightField._type = EditorComponentFieldType::SWITCH_TYPE;
        selectHighlightField._basicType = PushConstantType::BOOL;
        selectHighlightField._readOnly = false;

        _editorComponent.registerField(MOV(selectHighlightField));


        EditorComponentField selectWidgetField = {};
        selectWidgetField._name = "Show Select Widget";
        selectWidgetField._dataGetter = [this](void* dataOut, [[maybe_unused]] void* user_data) { *static_cast<bool*>(dataOut) = selectionWidgetEnabled(); };
        selectWidgetField._dataSetter = [this](const void* data, [[maybe_unused]] void* user_data) { selectionWidgetEnabled(*static_cast<const bool*>(data)); };
        selectWidgetField._type = EditorComponentFieldType::SWITCH_TYPE;
        selectWidgetField._basicType = PushConstantType::BOOL;
        selectWidgetField._readOnly = false;

        _editorComponent.registerField(MOV(selectWidgetField));
    }

} //namespace Divide
