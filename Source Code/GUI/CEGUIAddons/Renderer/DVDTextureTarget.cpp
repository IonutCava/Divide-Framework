#include "stdafx.h"

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
#include "Headers/DVDTextureTarget.h"
#include "Headers/DVDGeometryBuffer.h"

#include "CEGUI/Logger.h"

#include "Core/Headers/StringHelper.h"

#include "Platform/Video/Headers/GFXRTPool.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

namespace CEGUI
{
//! tangent of the y FOV half-angle; used to calculate viewing distance.
constexpr double S_YFOV_TAN = 0.267949192431123;

DVDTextureTarget::DVDTextureTarget( CEGUIRenderer& owner, const Sizef resolution )
    : _owner( owner )
{
    createCEGUITexture();
    initialiseRenderTexture();
    declareRenderSize( resolution );
}

DVDTextureTarget::~DVDTextureTarget()
{
    _owner.destroyTexture( *_CEGUITexture );

    if ( _renderTarget._targetID != Divide::INVALID_RENDER_TARGET_ID )
    {
        if ( !_owner.context().renderTargetPool().deallocateRT( _renderTarget ) )
        {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
    }
}

void DVDTextureTarget::draw( const GeometryBuffer& buffer )
{
    buffer.draw();
}

void DVDTextureTarget::draw( const RenderQueue& queue )
{
    queue.draw();
}

void DVDTextureTarget::declareRenderSize(const Sizef& sz)
{
    setArea(Rectf(_area.getPosition(), sz));
    resizeRenderTexture();
}

void DVDTextureTarget::clear()
{
    const Sizef sz(_area.getSize());
    // Some drivers crash when clearing a 0x0 RTT. This is a workaround for those cases.
    if (sz.d_width < 1.0f || sz.d_height < 1.0f)
    {
        return;
    }

    _requiresClear = true;
}

void DVDTextureTarget::initialiseRenderTexture()
{
    thread_local size_t FBO_INDEX = 0u;

    using namespace Divide;

    if ( _renderTarget._targetID != INVALID_RENDER_TARGET_ID )
    {
        if ( !_owner.context().renderTargetPool().deallocateRT(_renderTarget) )
        {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
    }

    SamplerDescriptor sampler = {};
    sampler.wrapUVW( TextureWrap::CLAMP_TO_EDGE );
    sampler.minFilter( TextureFilter::LINEAR );
    sampler.magFilter( TextureFilter::LINEAR );
    sampler.mipSampling( TextureMipSampling::NONE );
    sampler.anisotropyLevel( 0 );
    _samplerHash = sampler.getHash();

    TextureDescriptor descriptor( TextureType::TEXTURE_2D,
                                  GFXDataFormat::UNSIGNED_BYTE,
                                  GFXImageFormat::RGBA );
    descriptor.mipMappingState( TextureDescriptor::MipMappingState::OFF );

    RenderTargetDescriptor editorDesc = {};
    editorDesc._attachments = 
    {
        InternalRTAttachmentDescriptor{ descriptor, _samplerHash, RTAttachmentType::COLOUR, RTColourAttachmentSlot::SLOT_0}
    };

    editorDesc._name = Util::StringFormat(Util::StringFormat("CEGUI_Target_%d", FBO_INDEX++));
    editorDesc._resolution = { DEFAULT_SIZE , DEFAULT_SIZE };
    _renderTarget = _owner.context().renderTargetPool().allocateRT(editorDesc);

    _requiresClear = true;

    auto attachment = _renderTarget._rt->getAttachment( RTAttachmentType::COLOUR );
    _CEGUITexture->setDVDTexture(attachment->texture(), _area.getSize());
}

void DVDTextureTarget::resizeRenderTexture()
{
    // Some drivers (hint: Intel) segfault when glTexImage2D is called with
    // any of the dimensions being 0. The downside of this workaround is very
    // small. We waste a tiny bit of VRAM on cards with sane drivers and
    // prevent insane drivers from crashing CEGUI.
    Sizef sz(_area.getSize());
    if (sz.d_width < 1.0f || sz.d_height < 1.0f)
    {
        sz.d_width = 1.0f;
        sz.d_height = 1.0f;
    }
    else if (sz.d_width > (1 << 13) || sz.d_height > (1 << 13))
    {
        sz.d_width = 1 << 13;
        sz.d_height = 1 << 13;
    }

    _renderTarget._rt->resize(Divide::to_U16(sz.d_width), Divide::to_U16(sz.d_height));

    clear();

    auto attachment = _renderTarget._rt->getAttachment( Divide::RTAttachmentType::COLOUR );
    _CEGUITexture->setDVDTexture( attachment->texture(), sz);
}

void DVDTextureTarget::createCEGUITexture()
{
    static Divide::Texture_ptr dummy{};
    _CEGUITexture = &static_cast<DVDTexture&>(_owner.createTexture( GenerateTextureName(), dummy, _area.getSize() ));
    _requiresClear = true;
}

String DVDTextureTarget::GenerateTextureName()
{
    thread_local uint TEX_INDEX = 0;

    String tmp( "_ogl_tt_tex_" );
    tmp.append( PropertyHelper<uint>::toString( TEX_INDEX++ ) );

    return tmp;
}


Divide::Texture* DVDTextureTarget::getAttachmentTex() const
{
    auto attachment = _renderTarget._rt->getAttachment( Divide::RTAttachmentType::COLOUR );
    return attachment->texture().get();
}

void DVDTextureTarget::setArea( const Rectf& area )
{
    _area = area;
    _matrixValid = false;

    RenderTargetEventArgs args( this );
    TextureTarget::fireEvent( RenderTarget::EventAreaChanged, args );
}

void DVDTextureTarget::activate()
{
    using namespace Divide;

    GFX::CommandBuffer* cmdBuffer = _owner.cmdBuffer();

    GFX::BeginRenderPassCommand beginRenderPassCmd{};
    beginRenderPassCmd._target = _renderTarget._targetID;
    beginRenderPassCmd._name = "Render CEGUI";
    if ( _requiresClear )
    {
        beginRenderPassCmd._clearDescriptor[to_base( RTColourAttachmentSlot::SLOT_0 )] = { VECTOR4_ZERO, true};
        _requiresClear = false;
    }
    beginRenderPassCmd._descriptor._drawMask[to_base( RTColourAttachmentSlot::SLOT_0 )] = true;
    GFX::EnqueueCommand( *cmdBuffer, beginRenderPassCmd );

    if ( !_matrixValid )
    {
        updateMatrix();
    }

    _owner.setViewProjectionMatrix( _matrix );
    _owner.setActiveRenderTarget( this );
}

void DVDTextureTarget::deactivate()
{
    Divide::GFX::EnqueueCommand<Divide::GFX::EndRenderPassCommand>(*_owner.cmdBuffer() );
}

void DVDTextureTarget::unprojectPoint( const GeometryBuffer& buff, const Vector2f& p_in, Vector2f& p_out ) const
{
    if ( !_matrixValid )
    {
        updateMatrix();
    }

    const DVDGeometryBuffer& gb = static_cast<const DVDGeometryBuffer&>(buff);

    const Divide::Rect<Divide::I32> vp(
        Divide::to_I32(_area.left()),
        Divide::to_I32(_area.top()),
        Divide::to_I32(_area.getWidth()),
        Divide::to_I32(_area.getHeight())
    );

    double in_x = 0.0, in_y = 0.0, in_z = 0.0;

    glm::ivec4 viewPort = glm::ivec4( vp[0], vp[1], vp[2], vp[3] );
    const glm::mat4& projMatrix = _matrix;
    const glm::mat4& modelMatrix = gb.getMatrix();

    // unproject the ends of the ray
    glm::vec3 unprojected1;
    glm::vec3 unprojected2;
    in_x = vp[2] * 0.5;
    in_y = vp[3] * 0.5;
    in_z = -_viewDistance;
    unprojected1 = glm::unProject( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );
    in_x = p_in.d_x;
    in_y = vp[3] - p_in.d_y;
    in_z = 0.0;
    unprojected2 = glm::unProject( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );

    // project points to orientate them with GeometryBuffer plane
    glm::vec3 projected1;
    glm::vec3 projected2;
    glm::vec3 projected3;
    in_x = 0.0;
    in_y = 0.0;
    projected1 = glm::project( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );
    in_x = 1.0;
    in_y = 0.0;
    projected2 = glm::project( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );
    in_x = 0.0;
    in_y = 1.0;
    projected3 = glm::project( glm::vec3( in_x, in_y, in_z ), modelMatrix, projMatrix, viewPort );

    // calculate vectors for generating the plane
    const glm::vec3 pv1 = projected2 - projected1;
    const glm::vec3 pv2 = projected3 - projected1;
    // given the vectors, calculate the plane normal
    const glm::vec3 planeNormal = glm::cross( pv1, pv2 );
    // calculate plane
    const glm::vec3 planeNormalNormalized = glm::normalize( planeNormal );
    const double pl_d = -glm::dot( projected1, planeNormalNormalized );
    // calculate vector of picking ray
    const glm::vec3 rv = unprojected1 - unprojected2;
    // calculate intersection of ray and plane
    const double pn_dot_r1 = glm::dot( unprojected1, planeNormal );
    const double pn_dot_rv = glm::dot( rv, planeNormal );
    const double tmp1 = pn_dot_rv != 0.0 ? (pn_dot_r1 + pl_d) / pn_dot_rv : 0.0;
    const double is_x = unprojected1.x - rv.x * tmp1;
    const double is_y = unprojected1.y - rv.y * tmp1;

    p_out.d_x = static_cast<float>(is_x);
    p_out.d_y = static_cast<float>(is_y);

    p_out = p_in; // CrazyEddie wanted this
}

void DVDTextureTarget::updateMatrix() const
{
    const float w = _area.getWidth();
    const float h = _area.getHeight();

    // We need to check if width or height are zero and act accordingly to prevent running into issues
    // with divisions by zero which would lead to undefined values, as well as faulty clipping planes
    // This is mostly important for avoiding asserts
    const bool widthAndHeightNotZero = w != 0.0f && h != 0.0f;

    const float aspect = widthAndHeightNotZero ? w / h : 1.0f;
    const float midx = widthAndHeightNotZero ? w * 0.5f : 0.5f;
    const float midy = widthAndHeightNotZero ? h * 0.5f : 0.5f;
    _viewDistance = midx / (aspect * S_YFOV_TAN);

    const glm::vec3 eye = glm::vec3( midx, midy, float( -_viewDistance ) );
    const glm::vec3 center = glm::vec3( midx, midy, 1 );
    const glm::vec3 up = glm::vec3( 0, -1, 0 );

    //Older glm versions use degrees as parameter here by default (Unless radians are forced via GLM_FORCE_RADIANS). Newer versions of glm exlusively use radians.
#if (GLM_VERSION_MAJOR == 0 && GLM_VERSION_MINOR <= 9 && GLM_VERSION_PATCH < 6) && (!defined(GLM_FORCE_RADIANS))
    const glm::mat4 projectionMatrix = glm::perspective( 30.f, aspect, float( d_viewDistance * 0.5 ), float( d_viewDistance * 2.0 ) );
#else
    glm::mat4 projectionMatrix = glm::perspective( glm::radians( 30.f ), aspect, float( _viewDistance * 0.5 ), float( _viewDistance * 2.0 ) );
#endif

    // Projection matrix abuse!
    const glm::mat4 viewMatrix = glm::lookAt( eye, center, up );

    _matrix = projectionMatrix * viewMatrix;

    _matrixValid = true;
}

} // End of  CEGUI namespace section
