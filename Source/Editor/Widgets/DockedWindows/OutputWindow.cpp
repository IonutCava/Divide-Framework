

#include "Headers/OutputWindow.h"
#include "Core/Headers/StringHelper.h"
#include "Editor/Headers/Editor.h"
#include "Editor/Headers/Utils.h"

#include <imgui_internal.h>
#include <IconsForkAwesome.h>

namespace Divide
{
    constexpr U16 g_maxLogEntries = 1024u;
    static U16 g_logEntries = 256;

    static std::atomic_size_t g_writeIndex = 0;
    static vector<Console::OutputEntry> g_log;

    OutputWindow::OutputWindow( Editor& parent, const Descriptor& descriptor )
        : DockedWindow( parent, descriptor ),
        _inputBuf{}
    {
        memset( _inputBuf, 0, sizeof _inputBuf );

        g_writeIndex = 0;
        _consoleCallbackIndex = Console::BindConsoleOutput( [this]( const Console::OutputEntry& entry )
                                                            {
                                                                PrintText( entry );
                                                                _scrollToButtomReset = true;
                                                            } );

        g_log.resize( g_maxLogEntries );
    }

    OutputWindow::~OutputWindow()
    {
        if ( !Console::UnbindConsoleOutput( _consoleCallbackIndex ) )
        {
            DIVIDE_UNEXPECTED_CALL();
        }

        clearLog();
    }


    void OutputWindow::clearLog()
    {
        for ( auto& entry : g_log)
        {
            entry = {};
        }

        g_writeIndex.store( 0 );
        _scrollToBottom = true;
    }

    void OutputWindow::drawInternal()
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.3f, 0.3f, 0.3f, 1.0f ) );
        {
            bool tooltip = false;
            ImGui::Text( ICON_FK_SEARCH ); tooltip = tooltip || ImGui::IsItemHovered();
            ImGui::SameLine();
            ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0, 0 ) );
            _filter.Draw( "##Filter", 180 ); tooltip = tooltip || ImGui::IsItemHovered();
            if ( tooltip )
            {
                ImGui::SetTooltip( "Search/Filter (\"incl,-excl\") (\"error\")" );
            }
            ImGui::SameLine(0.f, 30.f);
            ImGui::Text( "Max log entries: ");
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 100 );
            U16 logSize = g_logEntries;
            if (ImGui::InputScalar( "##MaxLogEntries", ImGuiDataType_U16, &logSize, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue ))
            {
                if ( logSize != g_logEntries)
                {
                    g_logEntries = CLAMPED<U16>( logSize, 1u, g_maxLogEntries );
                }
            }
        }
        ImGui::PopStyleVar();
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGui::SameLine( window->SizeFull.x - 200 );
        if ( ImGui::SmallButton( "Clear" ) )
        {
            clearLog();
        }
        ImGui::SameLine();
        const bool copy_to_clipboard = ImGui::SmallButton( "Copy" );
        ImGui::Separator();

        ImGui::BeginChild( "ScrollingRegion", ImVec2( 0, -ImGui::GetFrameHeightWithSpacing() ), false, ImGuiWindowFlags_HorizontalScrollbar );
        if ( ImGui::BeginPopupContextWindow() )
        {
            if ( ImGui::Selectable( "Clear" ) )
            {
                clearLog();
            }
            ImGui::EndPopup();
        }

        static ImVec4 colours[] = {
            ImVec4( 1.0f, 1.0f, 1.0f, 1.0f ),
            ImVec4( 1.0f, 1.0f, 0.0f, 1.0f ),
            ImVec4( 1.0f, 0.4f, 0.4f, 1.0f ),
            ImVec4( 0.0f, 0.0f, 1.0f, 1.0f )
        };

        size_t readIndex = 0u;
        size_t writeIndex = g_writeIndex.load();
        if ( writeIndex >= g_logEntries )
        {
            readIndex = writeIndex - g_logEntries;
        }
        else
        {
            size_t remainder = g_logEntries - writeIndex;
            readIndex = g_maxLogEntries - remainder;
        }

        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 4, 1 ) ); // Tighten spacing

        if ( copy_to_clipboard )
        {
            ImGui::LogToClipboard();
        }
        {
            PROFILE_SCOPE( "Print Scrolling region ", Profiler::Category::GUI );

            Console::EntryType previousType = Console::EntryType::INFO;
            ImGui::PushStyleColor( ImGuiCol_Text, colours[to_U8(previousType)]);

            for ( U16 i = 0u; i < g_logEntries; ++i )
            {
                const Console::OutputEntry& message = g_log[(readIndex + i) % g_maxLogEntries];
                const char* msgBegin = message._text.c_str();
                const char* msgEnd = message._text.c_str() + message._text.length();

                if ( !_filter.PassFilter( msgBegin, msgEnd ) ) [[unlikely]]
                {
                    continue;
                }

                if ( previousType != message._type )
                {
                    ImGui::PopStyleColor();
                    ImGui::PushStyleColor( ImGuiCol_Text, colours[to_U8( message._type )] );
                    previousType = message._type;
                }

                ImGui::TextUnformatted( msgBegin, msgEnd );
            }

            ImGui::PopStyleColor();
        }

        if ( _scrollToBottom && _scrollToButtomReset )
        {
            ImGui::SetScrollHereY( 1.f );
            _scrollToButtomReset = false;
        }

        if ( copy_to_clipboard )
        {
            ImGui::LogFinish();
        }

        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::Separator();

        ImGui::Text( "Input:" ); ImGui::SameLine();
        if ( ImGui::InputText( "##Input:",
                               _inputBuf,
                               IM_ARRAYSIZE( _inputBuf ),
                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory,
                               []( ImGuiInputTextCallbackData* data ) noexcept
                               {
                                   const OutputWindow* console = static_cast<OutputWindow*>(data->UserData);
                                   return console->TextEditCallback( data );
                               },
                               (void*)this ) )
        {
            char* input_end = _inputBuf + strlen( _inputBuf );
            while ( input_end > _inputBuf && input_end[-1] == ' ' )
            {
                input_end--;
            }
            *input_end = 0;

            if ( _inputBuf[0] )
            {
                executeCommand( _inputBuf );
            }
            strcpy( _inputBuf, "" );
        }
        {
            bool tooltip = false;
            ImGui::SameLine( window->SizeFull.x - 125 );
            ImGui::Text( ICON_FK_ARROW_CIRCLE_DOWN ); tooltip = tooltip || ImGui::IsItemHovered();
            ImGui::SameLine();
            ImGui::PushID( ICON_FK_ARROW_CIRCLE_DOWN"_ID" );
            ImGui::Checkbox( "", &_scrollToBottom ); tooltip = tooltip || ImGui::IsItemHovered();
            ImGui::PopID();
            if ( tooltip )
            {
                ImGui::SetTooltip( "Auto-scroll to bottom" );
            }
        }
        // Demonstrate keeping auto focus on the input box
        if ( ImGui::IsItemHovered() || 
            (ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked( 0 )) )
        {
            ImGui::SetKeyboardFocusHere( -1 ); // Auto focus previous widget
        }
        ImGui::PopStyleColor();
    }

    void OutputWindow::PrintText( const Console::OutputEntry& entry )
    {
        g_log[g_writeIndex.fetch_add( 1 ) % g_maxLogEntries] = entry;
    }

    void OutputWindow::executeCommand( const char* command_line )
    {
        PrintText(
            {
                Util::StringFormat( "# {}\n", command_line ),
                Console::EntryType::COMMAND
            }
        );
        _scrollToButtomReset = true;
    }

    I32 OutputWindow::TextEditCallback( const ImGuiInputTextCallbackData* data ) noexcept
    {
        switch ( data->EventFlag )
        {
            case ImGuiInputTextFlags_CallbackCompletion:
            case ImGuiInputTextFlags_CallbackHistory:
            default: break;
        }

        return -1;
    }

} //namespace Divide
