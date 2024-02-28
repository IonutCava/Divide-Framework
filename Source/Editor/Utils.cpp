

#include "Headers/Utils.h"

#include <imgui_internal.h>

namespace Divide
{
    namespace
    {
        static I32 g_lastComponentWidhtPushCount = 0;
        static bool g_isBoldButtonPushed = false;
        static bool g_isNarrowLabelWidthPushed = false;
        static const char* g_pushedTooltip = "";
    }
} //namespace Divide

namespace ImGui
{
    bool InputDoubleN( const char* label, double* v, const int components, const char* display_format, const ImGuiInputTextFlags extra_flags )
    {
        const ImGuiWindow* window = GetCurrentWindow();
        if ( window->SkipItems )
            return false;

        const ImGuiContext& g = *GImGui;
        bool value_changed = false;
        BeginGroup();
        PushID( label );
        PushMultiItemsWidths( components, CalcItemWidth() );
        for ( int i = 0; i < components; i++ )
        {
            PushID( i );
            value_changed |= InputDouble( "##v", &v[i], 0.0, 0.0, display_format, extra_flags );
            SameLine( 0, g.Style.ItemInnerSpacing.x );
            PopID();
            PopItemWidth();
        }
        PopID();

        TextUnformatted( label, FindRenderedTextEnd( label ) );
        EndGroup();

        return value_changed;
    }

    bool InputDouble2( const char* label, double v[2], const char* display_format, const ImGuiInputTextFlags extra_flags )
    {
        return InputDoubleN( label, v, 2, display_format, extra_flags );
    }

    bool InputDouble3( const char* label, double v[3], const char* display_format, const ImGuiInputTextFlags extra_flags )
    {
        return InputDoubleN( label, v, 3, display_format, extra_flags );
    }

    bool InputDouble4( const char* label, double v[4], const char* display_format, const ImGuiInputTextFlags extra_flags )
    {
        return InputDoubleN( label, v, 4, display_format, extra_flags );
    }
}

namespace Divide
{

    ImGuiKey DivideKeyToImGuiKey( const Input::KeyCode key ) noexcept
    {
        switch ( key )
        {
            case Input::KeyCode::KC_TAB: return ImGuiKey_Tab;
            case Input::KeyCode::KC_LEFT: return ImGuiKey_LeftArrow;
            case Input::KeyCode::KC_RIGHT: return ImGuiKey_RightArrow;
            case Input::KeyCode::KC_UP: return ImGuiKey_UpArrow;
            case Input::KeyCode::KC_DOWN: return ImGuiKey_DownArrow;
            case Input::KeyCode::KC_PGUP: return ImGuiKey_PageUp;
            case Input::KeyCode::KC_PGDOWN: return ImGuiKey_PageDown;
            case Input::KeyCode::KC_HOME: return ImGuiKey_Home;
            case Input::KeyCode::KC_END: return ImGuiKey_End;
            case Input::KeyCode::KC_INSERT: return ImGuiKey_Insert;
            case Input::KeyCode::KC_DELETE: return ImGuiKey_Delete;
            case Input::KeyCode::KC_BACK: return ImGuiKey_Backspace;
            case Input::KeyCode::KC_SPACE: return ImGuiKey_Space;
            case Input::KeyCode::KC_RETURN: return ImGuiKey_Enter;
            case Input::KeyCode::KC_ESCAPE: return ImGuiKey_Escape;
            case Input::KeyCode::KC_APOSTROPHE: return ImGuiKey_Apostrophe;
            case Input::KeyCode::KC_COMMA: return ImGuiKey_Comma;
            case Input::KeyCode::KC_MINUS: return ImGuiKey_Minus;
            case Input::KeyCode::KC_PERIOD: return ImGuiKey_Period;
            case Input::KeyCode::KC_SLASH: return ImGuiKey_Slash;
            case Input::KeyCode::KC_SEMICOLON: return ImGuiKey_Semicolon;
            case Input::KeyCode::KC_EQUALS: return ImGuiKey_Equal;
            case Input::KeyCode::KC_LBRACKET: return ImGuiKey_LeftBracket;
            case Input::KeyCode::KC_BACKSLASH: return ImGuiKey_Backslash;
            case Input::KeyCode::KC_RBRACKET: return ImGuiKey_RightBracket;
            case Input::KeyCode::KC_GRAVE: return ImGuiKey_GraveAccent;
            case Input::KeyCode::KC_CAPITAL: return ImGuiKey_CapsLock;
            case Input::KeyCode::KC_SCROLL: return ImGuiKey_ScrollLock;
            case Input::KeyCode::KC_NUMLOCK: return ImGuiKey_NumLock;
            case Input::KeyCode::KC_PRINTSCREEN: return ImGuiKey_PrintScreen;
            case Input::KeyCode::KC_PAUSE: return ImGuiKey_Pause;
            case Input::KeyCode::KC_NUMPAD0: return ImGuiKey_Keypad0;
            case Input::KeyCode::KC_NUMPAD1: return ImGuiKey_Keypad1;
            case Input::KeyCode::KC_NUMPAD2: return ImGuiKey_Keypad2;
            case Input::KeyCode::KC_NUMPAD3: return ImGuiKey_Keypad3;
            case Input::KeyCode::KC_NUMPAD4: return ImGuiKey_Keypad4;
            case Input::KeyCode::KC_NUMPAD5: return ImGuiKey_Keypad5;
            case Input::KeyCode::KC_NUMPAD6: return ImGuiKey_Keypad6;
            case Input::KeyCode::KC_NUMPAD7: return ImGuiKey_Keypad7;
            case Input::KeyCode::KC_NUMPAD8: return ImGuiKey_Keypad8;
            case Input::KeyCode::KC_NUMPAD9: return ImGuiKey_Keypad9;
            case Input::KeyCode::KC_DECIMAL: return ImGuiKey_KeypadDecimal;
            case Input::KeyCode::KC_DIVIDE: return ImGuiKey_KeypadDivide;
            case Input::KeyCode::KC_MULTIPLY: return ImGuiKey_KeypadMultiply;
            case Input::KeyCode::KC_SUBTRACT: return ImGuiKey_KeypadSubtract;
            case Input::KeyCode::KC_ADD: return ImGuiKey_KeypadAdd;
            case Input::KeyCode::KC_NUMPADENTER: return ImGuiKey_KeypadEnter;
            case Input::KeyCode::KC_NUMPADEQUALS: return ImGuiKey_KeypadEqual;
            case Input::KeyCode::KC_LCONTROL: return ImGuiKey_LeftCtrl;
            case Input::KeyCode::KC_LSHIFT: return ImGuiKey_LeftShift;
            case Input::KeyCode::KC_LMENU: return ImGuiKey_LeftAlt;
            case Input::KeyCode::KC_LWIN: return ImGuiKey_LeftSuper;
            case Input::KeyCode::KC_RCONTROL: return ImGuiKey_RightCtrl;
            case Input::KeyCode::KC_RSHIFT: return ImGuiKey_RightShift;
            case Input::KeyCode::KC_RMENU: return ImGuiKey_RightAlt;
            case Input::KeyCode::KC_RWIN: return ImGuiKey_RightSuper;
            case Input::KeyCode::KC_APPS: return ImGuiKey_Menu;
            case Input::KeyCode::KC_0: return ImGuiKey_0;
            case Input::KeyCode::KC_1: return ImGuiKey_1;
            case Input::KeyCode::KC_2: return ImGuiKey_2;
            case Input::KeyCode::KC_3: return ImGuiKey_3;
            case Input::KeyCode::KC_4: return ImGuiKey_4;
            case Input::KeyCode::KC_5: return ImGuiKey_5;
            case Input::KeyCode::KC_6: return ImGuiKey_6;
            case Input::KeyCode::KC_7: return ImGuiKey_7;
            case Input::KeyCode::KC_8: return ImGuiKey_8;
            case Input::KeyCode::KC_9: return ImGuiKey_9;
            case Input::KeyCode::KC_A: return ImGuiKey_A;
            case Input::KeyCode::KC_B: return ImGuiKey_B;
            case Input::KeyCode::KC_C: return ImGuiKey_C;
            case Input::KeyCode::KC_D: return ImGuiKey_D;
            case Input::KeyCode::KC_E: return ImGuiKey_E;
            case Input::KeyCode::KC_F: return ImGuiKey_F;
            case Input::KeyCode::KC_G: return ImGuiKey_G;
            case Input::KeyCode::KC_H: return ImGuiKey_H;
            case Input::KeyCode::KC_I: return ImGuiKey_I;
            case Input::KeyCode::KC_J: return ImGuiKey_J;
            case Input::KeyCode::KC_K: return ImGuiKey_K;
            case Input::KeyCode::KC_L: return ImGuiKey_L;
            case Input::KeyCode::KC_M: return ImGuiKey_M;
            case Input::KeyCode::KC_N: return ImGuiKey_N;
            case Input::KeyCode::KC_O: return ImGuiKey_O;
            case Input::KeyCode::KC_P: return ImGuiKey_P;
            case Input::KeyCode::KC_Q: return ImGuiKey_Q;
            case Input::KeyCode::KC_R: return ImGuiKey_R;
            case Input::KeyCode::KC_S: return ImGuiKey_S;
            case Input::KeyCode::KC_T: return ImGuiKey_T;
            case Input::KeyCode::KC_U: return ImGuiKey_U;
            case Input::KeyCode::KC_V: return ImGuiKey_V;
            case Input::KeyCode::KC_W: return ImGuiKey_W;
            case Input::KeyCode::KC_X: return ImGuiKey_X;
            case Input::KeyCode::KC_Y: return ImGuiKey_Y;
            case Input::KeyCode::KC_Z: return ImGuiKey_Z;
            case Input::KeyCode::KC_F1: return ImGuiKey_F1;
            case Input::KeyCode::KC_F2: return ImGuiKey_F2;
            case Input::KeyCode::KC_F3: return ImGuiKey_F3;
            case Input::KeyCode::KC_F4: return ImGuiKey_F4;
            case Input::KeyCode::KC_F5: return ImGuiKey_F5;
            case Input::KeyCode::KC_F6: return ImGuiKey_F6;
            case Input::KeyCode::KC_F7: return ImGuiKey_F7;
            case Input::KeyCode::KC_F8: return ImGuiKey_F8;
            case Input::KeyCode::KC_F9: return ImGuiKey_F9;
            case Input::KeyCode::KC_F10: return ImGuiKey_F10;
            case Input::KeyCode::KC_F11: return ImGuiKey_F11;
            case Input::KeyCode::KC_F12: return ImGuiKey_F12;
        }

        return ImGuiKey_None;
    }
    namespace Util
    {
        F32 GetLineHeight() noexcept
        {
            return GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.f;
        }

        void AddUnderLine()
        {
            ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            min.y = max.y;
            ImGui::GetWindowDrawList()->AddLine( min, max, ImGui::GetColorU32( ImGuiCol_Text ), 1.0f );
        }

        void BeginPropertyTable( const I32 numComponents, const char* label )
        {
            ImFont* boldFont = ImGui::GetIO().Fonts->Fonts[1];

            ImGui::PushID( label );
            ImGui::Columns( 2 );
            ImGui::SetColumnWidth( 0, g_isNarrowLabelWidthPushed ? LabelColumnWidthNarrow : LabelColumnWidth );
            ImGui::PushFont( boldFont );
            ImGui::Text( label );
            if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
            {
                if ( Util::IsPushedTooltip() )
                {
                    ImGui::SetTooltip( Util::PushedToolTip() );
                }
                else
                {
                    ImGui::SetTooltip( label );
                }
            }

            ImGui::PopFont();
            ImGui::NextColumn();
            if ( numComponents == 1 )
            {
                ImGui::PushItemWidth( ImGui::CalcItemWidth() );
            }
            else
            {
                ImGui::PushMultiItemsWidths( numComponents, ImGui::CalcItemWidth() );
            }
            g_lastComponentWidhtPushCount = numComponents;
        }

        void EndPropertyTable()
        {
            for ( I32 i = 0; i < g_lastComponentWidhtPushCount; ++i )
            {
                ImGui::PopItemWidth();
            }
            g_lastComponentWidhtPushCount = 0;
            ImGui::Columns( 1 );
            ImGui::PopID();
        }

        void PushBoldFont()
        {
            if ( !g_isBoldButtonPushed )
            {
                ImGui::PushFont( ImGui::GetIO().Fonts->Fonts[1] );
                g_isBoldButtonPushed = true;
            }
        }

        void PopBoldFont()
        {
            if ( g_isBoldButtonPushed )
            {
                ImGui::PopFont();
                g_isBoldButtonPushed = false;
            }
        }

        void PushNarrowLabelWidth()
        {
            if ( !g_isNarrowLabelWidthPushed )
            {
                g_isNarrowLabelWidthPushed = true;
            }
        }

        void PopNarrowLabelWidth()
        {
            if ( g_isNarrowLabelWidthPushed )
            {
                g_isNarrowLabelWidthPushed = false;
            }
        }

        void PushTooltip( const char* tooltip )
        {
            g_pushedTooltip = tooltip;
        }

        void PopTooltip()
        {
            g_pushedTooltip = "";
        }

        [[nodiscard]] bool IsPushedTooltip()
        {
            return strlen( g_pushedTooltip ) > 0;
        }

        const char* PushedToolTip()
        {
            return g_pushedTooltip;
        }

        void PushButtonStyle( const bool bold,
                              const ImVec4 buttonColour,
                              const ImVec4 buttonColourHovered,
                              const ImVec4 buttonColourActive )
        {
            if ( bold )
            {
                PushBoldFont();
            }
            ImGui::PushStyleColor( ImGuiCol_Button, buttonColour );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, buttonColourHovered );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, buttonColourActive );
        }

        void PopButtonStyle()
        {
            PopBoldFont();
            ImGui::PopStyleColor( 3 );
        }


        const char* GetFormat( ImGuiDataType dataType, const char* input, const bool hex )
        {
            if ( input == nullptr || strlen( input ) == 0 )
            {
                const auto unsignedType = [dataType]()
                {
                    return dataType == ImGuiDataType_U8 || dataType == ImGuiDataType_U16 || dataType == ImGuiDataType_U32 || dataType == ImGuiDataType_U64;
                };

                return dataType == ImGuiDataType_Float ? "%.3f"
                    : dataType == ImGuiDataType_Double ? "%.6f"
                    : hex ? "%08X" : (unsignedType() ? "%u" : "%d");
            }

            return input;
        }

        bool colourInput4( Editor& parent, EditorComponentField& field )
        {
            FColour4 val = field.get<FColour4>();
            const auto setter = [val/*by value*/, &field]( const FColour4& col )
            {
                if ( col != val )
                {
                    field.set( col );
                    return true;
                }
                return false;
            };

            return colourInput4( parent, field._name.c_str(), val, field._readOnly, setter );
        }

        bool colourInput3( Editor& parent, EditorComponentField& field )
        {
            FColour3 val = field.get<FColour3>();
            const auto setter = [val/*by value*/, &field]( const FColour3& col )
            {
                if ( col != val )
                {
                    field.set( col );
                    return true;
                }
                return false;
            };

            return colourInput3( parent, field._name.c_str(), val, field._readOnly, setter );
        }

        void CenterNextWindow()
        {
            const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
            const ImVec2 workPos = main_viewport->WorkPos;
            const ImVec2 workSize = main_viewport->WorkSize;
            const ImVec2 targetPos = workPos + ImVec2( workSize.x * 0.5f, workSize.y * 0.5f );

            ImGui::SetNextWindowPos( targetPos, ImGuiCond_Always, ImVec2( 0.5f, 0.5f ) );
        }

        void OpenCenteredPopup( const char* name, const ImGuiPopupFlags popup_flags )
        {
            CenterNextWindow();
            ImGui::OpenPopup( name, popup_flags );
        }

        void PrintColouredText( const std::string_view text, ImVec4 colour )
        {
            ImGui::PushStyleColor( ImGuiCol_Text, colour );
            ImGui::TextUnformatted( text.data(), text.data() + text.length());
            ImGui::PopStyleColor();
        }
    } //namespace Util
} //namespace Divide
