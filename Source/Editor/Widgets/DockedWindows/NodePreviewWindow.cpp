

#include "Headers/NodePreviewWindow.h"
#include "Editor/Headers/Utils.h"

#include "Editor/Headers/Editor.h"
#include "Editor/Widgets/Headers/ImGuiExtensions.h"

#include "Core/Headers/PlatformContext.h"

#include "Platform/Headers/DisplayWindow.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Textures/Headers/Texture.h"

#include <imgui_internal.h>
#include <IconsForkAwesome.h>

namespace Divide
{
    NodePreviewWindow::NodePreviewWindow( Editor& parent, const Descriptor& descriptor )
        : DockedWindow( parent, descriptor )
    {
        _originalName = descriptor.name;
    }

    void NodePreviewWindow::drawInternal()
    {
        const RenderTarget* rt = _parent.getNodePreviewTarget()._rt;
        const Texture_ptr& gameView = rt->getAttachment( RTAttachmentType::COLOUR )->texture();

        Attorney::EditorSceneViewWindow::editorEnableGizmo( _parent, true );
        drawInternal( gameView.get() );

        bool enableGrid = _parent.infiniteGridEnabledNode();
        if ( ImGui::Checkbox( ICON_FK_PLUS_SQUARE_O" Infinite Grid", &enableGrid ) )
        {
            _parent.infiniteGridEnabledNode( enableGrid );
        }

        if ( ImGui::IsItemHovered() )
        {
            ImGui::SetTooltip( "Toggle the editor XZ grid on/off.\nGrid sizing is controlled in the \"Editor options\" window (under \"File\" in the menu bar)" );
        }
        ImGui::SameLine();

        FColour3& bgColour = Attorney::EditorSceneViewWindow::nodePreviewBGColour( _parent );
        ImGui::ColorEdit3( "Background colour", bgColour._v, ImGuiColorEditFlags_NoInputs );
    }

    bool NodePreviewWindow::button( const bool enabled, const char* label, const char* tooltip, const bool small )
    {
        if ( !enabled )
        {
            PushReadOnly();
        }
        const bool ret = small ? ImGui::SmallButton( label ) : ImGui::Button( label );
        if ( !enabled )
        {
            PopReadOnly();
        }

        if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
        {
            ImGui::SetTooltip( tooltip );
        }

        return ret;
    }

    void NodePreviewWindow::drawInternal( Texture* tex )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::GUI );

        static IMGUICallbackData defaultData{}, noAlphaBlendData{};

        defaultData._flip = false;

        noAlphaBlendData = defaultData;
        noAlphaBlendData._colourData.a = 0;

        const ImDrawCallback toggleAlphaBlend = []( [[maybe_unused]] const ImDrawList* parent_list, const ImDrawCmd* imCmd ) -> void
        {
            IMGUICallbackData* data = static_cast<IMGUICallbackData*>(imCmd->UserCallbackData);
            DIVIDE_ASSERT( data->_cmdBuffer != nullptr );

            const PushConstantsStruct pushConstants = IMGUICallbackToPushConstants(*data, false);

            GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( *data->_cmdBuffer )->_constants.set( pushConstants );
        };

        assert( tex != nullptr );

        const I32 w = to_I32( tex->width() );
        const I32 h = to_I32( tex->height() );

        const ImVec2 curPos = ImGui::GetCursorPos();
        const ImVec2 wndSz( ImGui::GetWindowSize().x - curPos.x - 30.0f, ImGui::GetWindowSize().y - curPos.y - 30.0f );

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImRect bb( window->DC.CursorPos, ImVec2( window->DC.CursorPos.x + wndSz.x, window->DC.CursorPos.y + wndSz.y ) );
        ImGui::ItemSize( bb );
        if ( ImGui::ItemAdd( bb, ImGui::GetID("##nodePreviewRect") ) )
        {
            ImVec2 imageSz = wndSz - ImVec2( 0.2f, 0.2f );
            ImVec2 remainingWndSize( 0, 0 );
            const F32 aspectRatio = w / to_F32( h );

            const F32 wndAspectRatio = wndSz.x / wndSz.y;
            if ( aspectRatio >= wndAspectRatio )
            {
                imageSz.y = imageSz.x / aspectRatio;
                remainingWndSize.y = wndSz.y - imageSz.y;
            }
            else
            {
                imageSz.x = imageSz.y * aspectRatio;
                remainingWndSize.x = wndSz.x - imageSz.x;
            }

            const ImVec2 uvExtension = ImVec2( 1.f, 1.f );
            if ( remainingWndSize.x > 0 )
            {
                const F32 remainingSizeInUVSpace = remainingWndSize.x / imageSz.x;
                const F32 deltaUV = uvExtension.x;
                const F32 remainingUV = 1.f - deltaUV;
                if ( deltaUV < 1 )
                {
                    const F32 adder = remainingUV < remainingSizeInUVSpace ? remainingUV : remainingSizeInUVSpace;
                    remainingWndSize.x -= adder * imageSz.x;
                    imageSz.x += adder * imageSz.x;
                }
            }
            if ( remainingWndSize.y > 0 )
            {
                const F32 remainingSizeInUVSpace = remainingWndSize.y / imageSz.y;
                const F32 deltaUV = uvExtension.y;
                const F32 remainingUV = 1.f - deltaUV;
                if ( deltaUV < 1 )
                {
                    const F32 adder = remainingUV < remainingSizeInUVSpace ? remainingUV : remainingSizeInUVSpace;
                    remainingWndSize.y -= adder * imageSz.y;
                    imageSz.y += adder * imageSz.y;
                }
            }

            ImVec2 startPos = bb.Min, endPos;
            startPos.x += remainingWndSize.x * .5f;
            startPos.y += remainingWndSize.y * .5f;
            endPos.x = startPos.x + imageSz.x;
            endPos.y = startPos.y + imageSz.y;

            const bool flipImages = !ImageTools::UseUpperLeftOrigin();
            const ImVec2 uv0{ 0.f, flipImages ? 1.f : 0.f };
            const ImVec2 uv1{ 1.f, flipImages ? 0.f : 1.f };

            window->DrawList->AddCallback( toggleAlphaBlend, &noAlphaBlendData );
            window->DrawList->AddImage( (void*)tex, startPos, endPos, uv0, uv1);
            window->DrawList->AddCallback( toggleAlphaBlend, &defaultData );

            updateBounds( { startPos.x, startPos.y, imageSz.x, imageSz.y } );
        }
    }

    void NodePreviewWindow::updateBounds( Rect<I32> imageRect )
    {
        const DisplayWindow* displayWindow = static_cast<DisplayWindow*>(ImGui::GetCurrentWindow()->Viewport->PlatformHandle);
        // We might be dragging the window
        if ( displayWindow != nullptr )
        {
            _sceneRect[0] = imageRect;
            _sceneRect[1] = imageRect;
            _sceneRect[1].x -= displayWindow->getPosition().x;
            _sceneRect[1].y -= displayWindow->getPosition().y;
        }
    }

    const Rect<I32>& NodePreviewWindow::sceneRect( const bool globalCoords ) const noexcept
    {
        return  _sceneRect[globalCoords ? 0 : 1];
    }

} //namespace Divide
