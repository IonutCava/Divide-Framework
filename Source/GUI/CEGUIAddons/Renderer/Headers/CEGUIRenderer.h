/***********************************************************************
    created:    Tue Apr 30 2013
    authors:    Paul D Turner <paul@cegui.org.uk>
                Lukas E Meindl
*************************************************************************/
/***************************************************************************
 *   Copyright (C) 2004 - 2013 Paul D Turner & The CEGUI Development Team
 *
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice shall be
 *   included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *   OTHER DEALINGS IN THE SOFTWARE.
 ***************************************************************************/
#pragma once
#ifndef _CEGUIRenderer_Divide_h_
#define _CEGUIRenderer_Divide_h_

#include "CEGUI/Base.h"
#include "CEGUI/Renderer.h"
#include "CEGUI/Size.h"
#include "CEGUI/Vector.h"
#include "CEGUI/Rect.h"
#include "CEGUI/TextureTarget.h"

#include "CEGUI/Config.h"

#include <glm/mat4x4.hpp>

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4251)
#endif

namespace Divide
{
    class GFXDevice;
    class Pipeline;
    FWD_DECLARE_MANAGED_CLASS(ShaderProgram);
    FWD_DECLARE_MANAGED_CLASS( Texture );

    namespace GFX
    {
        class CommandBuffer;
        struct MemoryBarrierCommand;
    }
};

namespace CEGUI
{

class DVDTexture;
class DVDGeometryBuffer;
class CEGUIRenderer;

class CEGUIRenderer final : public Renderer
{
public:
    enum class PipelineType : Divide::U8
    {
        BLEND_NORMAL_NO_SCISSOR = 0,
        BLEND_NORMAL_SCISSOR,
        BLEND_PREMULTIPLIED_NO_SCISSOR,
        BLEND_PREMULTIPLIED_SCISSOR,
        COUNT
    };

private:
    //! container type used to hold TextureTargets we create.
    using TextureTargetList = Divide::vector_fast<TextureTarget*>;
    //! container type used to hold GeometryBuffers created.
    using GeometryBufferList = Divide::vector_fast<DVDGeometryBuffer*>;
    //! container type used to hold Textures we create.
    using TextureMap = std::map<String, DVDTexture*, StringFastLessCompare CEGUI_MAP_ALLOC( String, DVDTexture* )>;
    //! container type used to hold our various compiled pipelines needed for rendering
    using PipelineContainer = std::array<Divide::Pipeline*, Divide::to_base( PipelineType::COUNT )>;

public:
    static CEGUIRenderer& create( Divide::GFXDevice& context, Divide::ShaderProgram_ptr shader, CEGUI::Sizef resolution, int abi = CEGUI_VERSION_ABI );
    static void destroy( CEGUIRenderer& renderer );

#pragma region Renderer Interface
    void beginRendering() override;
    void endRendering() override;

    void destroyGeometryBuffer(const GeometryBuffer& buffer) override;
    void destroyAllGeometryBuffers() override;
    void destroyTextureTarget(TextureTarget* target) override;
    void destroyAllTextureTargets() override;
    void destroyTexture(Texture& texture) override;
    void destroyTexture(const String& name) override;
    void destroyAllTextures() override;
    void setDisplaySize(const Sizef& sz) override;

    [[nodiscard]] RenderTarget& getDefaultRenderTarget() override;
    [[nodiscard]] GeometryBuffer& createGeometryBuffer() override;
    [[nodiscard]] TextureTarget* createTextureTarget() override;
    [[nodiscard]] Texture& createTexture(const String& name) override;
    [[nodiscard]] Texture& createTexture(const String& name, const String& filename, const String& resourceGroup) override;
    [[nodiscard]] Texture& createTexture(const String& name, const Sizef& size) override;
    [[nodiscard]] Texture& getTexture(const String& name) const override;
    [[nodiscard]] bool isTextureDefined(const String& name) const override;
    [[nodiscard]] const Sizef& getDisplaySize() const override;
    [[nodiscard]] const Vector2f& getDisplayDPI() const override;
    [[nodiscard]] uint getMaxTextureSize() const override;
    [[nodiscard]] const String& getIdentifierString() const override;
#pragma endregion

    Texture& createTexture(const String& name, const Divide::Texture_ptr& tex, const Sizef& sz);
    void setViewProjectionMatrix(const glm::mat4& viewProjectionMatrix);
    void setActiveRenderTarget(RenderTarget* renderTarget);

    void beginRendering( Divide::GFX::CommandBuffer& bufferInOut, Divide::GFX::MemoryBarrierCommand& memCmdInOut);
    void bindDefaultState( bool scissor, BlendMode mode, const glm::mat4& viewProjMat );

    [[nodiscard]] const Rectf& getActiveViewPort() const;
    [[nodiscard]] Divide::Texture* getTextureTarget() const;
    [[nodiscard]] Divide::GFXDevice& context();
    [[nodiscard]] bool flipClippingHeight() const noexcept;
    [[nodiscard]] Divide::GFX::CommandBuffer* cmdBuffer() const;
    [[nodiscard]] Divide::GFX::MemoryBarrierCommand* memCmd() const;
    [[nodiscard]] const glm::mat4& getViewProjectionMatrix() const noexcept;

private:
    CEGUIRenderer( Divide::GFXDevice& context, Divide::ShaderProgram_ptr shader, CEGUI::Sizef resolution);
    virtual ~CEGUIRenderer();

    /// Helper to safely log the creation of a named texture
    static void LogTextureCreation(const String& name);
    /// Helper to safely log the destruction of a named texture
    static void LogTextureDestruction(const String& name);

private:
    //! Clipping direction is the only thing we need to manually adjust between rendering APIs. (e.g. true for Vulkan, false for OpenGL)
    const bool _flipClippingHeight;
    //! Parent app's graphics context
    Divide::GFXDevice& _context;
    //! Command buffer from the parent app used to queue up rendering commands
    Divide::GFX::CommandBuffer* _bufferInOut{nullptr};
    //! Memory command from the parent app used to protected geometry buffer updates
    Divide::GFX::MemoryBarrierCommand* _memCmdInOut{nullptr};
    //! String holding the renderer identification text.
    static String s_rendererID;
    //! What the renderer considers to be the current display size.
    Sizef _displaySize{};
    //! What the renderer considers to be the current display DPI resolution.
    Vector2f _displayDPI{};
    //! The default RenderTarget
    RenderTarget* _defaultTarget{nullptr};
    //! Container used to track texture targets.
    TextureTargetList _textureTargets;
    //! Container used to track geometry buffers.
    GeometryBufferList _geometryBuffers;
    //! Container used to track textures.
    TextureMap _textures;
    //! View projection matrix
    glm::mat4  _viewProjectionMatrix;
    //! The active RenderTarget
    RenderTarget* _activeRenderTarget{nullptr};
    //! Container used to store our compiled pipelines
    PipelineContainer _pipelines;
    //! Active pipeline type bound for rendering. Used to index into the "_pipelines" container
    PipelineType _activePipelineType{PipelineType::COUNT};
};

}

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

#endif

#include "CEGUIRenderer.inl"
