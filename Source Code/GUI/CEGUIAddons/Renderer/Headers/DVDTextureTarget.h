/***********************************************************************
    created:    Wed, 8th Feb 2012
    author:     Lukas E Meindl (based on code by Paul D Turner)
*************************************************************************/
/***************************************************************************
 *   Copyright (C) 2004 - 2012 Paul D Turner & The CEGUI Development Team
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
#ifndef _CEGUIDVDTextureTarget_h_
#define _CEGUIDVDTextureTarget_h_

#include "CEGUIRenderer.h"
#include "CEGUI/TextureTarget.h"
#include "CEGUI/Rect.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4250)
#endif

namespace CEGUI
{
class DVDTexture;
class CEGUIRenderer;

class DVDTextureTarget final : public TextureTarget
{
public:
    //! default size of created texture objects
    static constexpr float DEFAULT_SIZE = 128.f;

    DVDTextureTarget( CEGUIRenderer& owner, const Sizef resolution);
    virtual ~DVDTextureTarget();

    // implementation of TextureTarget interface
    void clear();
    void declareRenderSize(const Sizef& sz);

    [[nodiscard]] Divide::Texture* getAttachmentTex() const;
    [[nodiscard]] size_t getSamplerHash() const noexcept;

#pragma region TextureTarget Interface
    [[nodiscard]] bool isImageryCache() const override;
    [[nodiscard]] Texture& getTexture() const override;
    [[nodiscard]] bool isRenderingInverted() const override;
    [[nodiscard]] const Rectf& getArea() const override;

    void draw( const GeometryBuffer& buffer ) override;
    void draw( const RenderQueue& queue ) override;
    void setArea( const Rectf& area ) override;
    void activate() override;
    void deactivate() override;
    void unprojectPoint( const GeometryBuffer& buff, const Vector2f& p_in, Vector2f& p_out ) const override;
#pragma endregion

private:
    //! allocate and set up the texture used with the FBO.
    void initialiseRenderTexture();
    //! resize the texture
    void resizeRenderTexture();
    //! helper that initialises the cached matrix
    void updateMatrix() const;
    //! helper to create CEGUI::Texture d_CEGUITexture;
    void createCEGUITexture();
    //! helper to generate unique texture names
    static String GenerateTextureName();

private:
    //! CEGUIRenderer that created this object
    CEGUIRenderer& _owner;
    //! holds defined area for the RenderTarget
    Rectf _area{0.f, 0.f, 0.f, 0.f};
    //! saved copy of projection matrix
    mutable glm::mat4 _matrix;
    //! true if saved matrix is up to date
    mutable bool _matrixValid{false};
    //! tracks viewing distance (this is set up at the same time as d_matrix)
    mutable double _viewDistance{0};
    //! we use this to wrap _texture so it can be used by the core CEGUI lib.
    DVDTexture* _CEGUITexture{nullptr};
    //! A Divide sampler hash used for sampling from this texture target
    size_t _samplerHash{0u};
    //! A handle to a Divide render target used for rendering with this DVDTextureTarget
    Divide::RenderTargetHandle _renderTarget;
    //! Because Divide command buffers are deferred, clearing should be queued instead of instant
    bool _requiresClear{true};
};

} // End of  CEGUI namespace section

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

#endif  // end of guard _CEGUIDVDTextureTarget_h_

#include "DVDTextureTarget.inl"
