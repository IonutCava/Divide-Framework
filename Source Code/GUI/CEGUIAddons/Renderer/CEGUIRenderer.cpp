#include "stdafx.h"

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
#include "Headers/CEGUIRenderer.h"
#include "Headers/DVDTexture.h"
#include "Headers/DVDGeometryBuffer.h"
#include "Headers/DVDTextureTarget.h"

#include "CEGUI/Exceptions.h"
#include "CEGUI/ImageCodec.h"
#include "CEGUI/Logger.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Headers/CommandBuffer.h"
#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include "glm/gtc/type_ptr.hpp"

namespace CEGUI
{

String CEGUIRenderer::s_rendererID("Divide CEGUI Renderer");

CEGUIRenderer::CEGUIRenderer( Divide::GFXDevice& context, Divide::ShaderProgram_ptr shader, const CEGUI::Sizef resolution )
    : _context(context)
    , _displaySize(resolution)
    , _flipClippingHeight(context.renderAPI() == Divide::RenderAPI::Vulkan)
{
    using namespace Divide;

    _displayDPI.d_x = _displayDPI.d_y = 96;
    _viewProjectionMatrix = {};

    RenderStateBlock defaultState;
    defaultState.setCullMode( CullMode::NONE );
    defaultState.setFillMode( FillMode::SOLID );
    defaultState.depthTestEnabled( false );

    defaultState.setScissorTest( true );
    const size_t defaultStateHashScissor = defaultState.getHash();

    defaultState.setScissorTest( false );
    const size_t defaultStateHashNoScissor = defaultState.getHash();

    PipelineDescriptor descriptor = {};
    descriptor._shaderProgramHandle = shader->handle();
    descriptor._primitiveTopology = PrimitiveTopology::TRIANGLES;
    descriptor._vertexFormat._vertexBindings.emplace_back()._strideInBytes = sizeof(DVDGeometryBuffer::DVDVertex);
    AttributeDescriptor& descPos = descriptor._vertexFormat._attributes[to_base( AttribLocation::POSITION )]; //vec3
    AttributeDescriptor& descUV = descriptor._vertexFormat._attributes[to_base( AttribLocation::TEXCOORD )];  //vec2
    AttributeDescriptor& descColour = descriptor._vertexFormat._attributes[to_base( AttribLocation::COLOR )]; //vec4

    descPos._vertexBindingIndex = 0u;
    descPos._componentsPerElement = 3u;
    descPos._dataType = GFXDataFormat::FLOAT_32;

    descUV._vertexBindingIndex = 0u;
    descUV._componentsPerElement = 2u;
    descUV._dataType = GFXDataFormat::FLOAT_32;

    descColour._vertexBindingIndex = 0u;
    descColour._componentsPerElement = 4u;
    descColour._dataType = GFXDataFormat::FLOAT_32;

    descUV._strideInBytes = offsetof( DVDGeometryBuffer::DVDVertex, tex );
    descColour._strideInBytes = offsetof( DVDGeometryBuffer::DVDVertex, colour );
    descPos._strideInBytes = offsetof( DVDGeometryBuffer::DVDVertex, position );

    BlendingSettings& blend = descriptor._blendStates._settings[0u];
    descriptor._blendStates._blendColour = DefaultColours::BLACK_U8;

    blend.enabled( true );
    blend.blendSrc( BlendProperty::ONE );
    blend.blendDest( BlendProperty::INV_SRC_ALPHA );
    blend.blendOp( BlendOperation::ADD );

    descriptor._stateHash = defaultStateHashScissor;
    _pipelines[to_base( PipelineType::BLEND_PREMULTIPLIED_SCISSOR )] = context.newPipeline( descriptor );

    descriptor._stateHash = defaultStateHashNoScissor;
    _pipelines[to_base( PipelineType::BLEND_PREMULTIPLIED_NO_SCISSOR )] = context.newPipeline( descriptor );

    blend.blendSrc( BlendProperty::SRC_ALPHA );
    blend.blendDest( BlendProperty::INV_SRC_ALPHA );
    blend.blendOp( BlendOperation::ADD );
    blend.blendSrcAlpha( BlendProperty::INV_DEST_ALPHA );
    blend.blendDestAlpha( BlendProperty::ONE );
    blend.blendOpAlpha( BlendOperation::ADD );

    descriptor._stateHash = defaultStateHashScissor;
    _pipelines[to_base( PipelineType::BLEND_NORMAL_SCISSOR )] = context.newPipeline( descriptor );

    descriptor._stateHash = defaultStateHashNoScissor;
    _pipelines[to_base( PipelineType::BLEND_NORMAL_NO_SCISSOR )] = context.newPipeline( descriptor );

    _defaultTarget = CEGUI_NEW_AO DVDTextureTarget(*this, _displaySize);
}

CEGUIRenderer::~CEGUIRenderer()
{
    CEGUI_DELETE_AO _defaultTarget;

    _pipelines.fill(nullptr);
    destroyAllGeometryBuffers();
    destroyAllTextureTargets();
    destroyAllTextures();
}

CEGUIRenderer& CEGUIRenderer::create( Divide::GFXDevice& context, Divide::ShaderProgram_ptr shader, CEGUI::Sizef resolution, const int abi )
{
    System::performVersionTest( CEGUI_VERSION_ABI, abi, CEGUI_FUNCTION_NAME );

    return *CEGUI_NEW_AO CEGUIRenderer(context, shader, resolution);
}

void CEGUIRenderer::destroy( CEGUIRenderer& renderer )
{
    CEGUI_DELETE_AO& renderer;
}

Divide::Texture* CEGUIRenderer::getTextureTarget() const
{
    DVDTextureTarget* target = static_cast<DVDTextureTarget*>(_defaultTarget);
    return target->getAttachmentTex();
}

GeometryBuffer& CEGUIRenderer::createGeometryBuffer()
{
    return *_geometryBuffers.emplace_back( CEGUI_NEW_AO DVDGeometryBuffer( *this ) );
}

void CEGUIRenderer::destroyGeometryBuffer(const GeometryBuffer& buffer)
{
    const GeometryBufferList::iterator i = std::find(_geometryBuffers.begin(),
                                                     _geometryBuffers.end(),
                                                     &buffer);

    if (_geometryBuffers.end() != i)
    {
        _geometryBuffers.erase(i);
        CEGUI_DELETE_AO &buffer;
    }
}

void CEGUIRenderer::destroyAllGeometryBuffers()
{
    while (!_geometryBuffers.empty())
    {
        destroyGeometryBuffer(**_geometryBuffers.begin());
    }
}

TextureTarget* CEGUIRenderer::createTextureTarget()
{
    TextureTarget* t = CEGUI_NEW_AO DVDTextureTarget( *this, Sizef( DVDTextureTarget::DEFAULT_SIZE, DVDTextureTarget::DEFAULT_SIZE ) );

    if (t)
    {
        _textureTargets.push_back(t);
    }

    return t;
}

void CEGUIRenderer::destroyTextureTarget(TextureTarget* target)
{
    const TextureTargetList::iterator i = std::find(_textureTargets.begin(),
                                                    _textureTargets.end(),
                                                    target);

    if (_textureTargets.end() != i)
    {
        _textureTargets.erase(i);
        CEGUI_DELETE_AO target;
    }
}

void CEGUIRenderer::destroyAllTextureTargets()
{
    while (!_textureTargets.empty())
    {
        destroyTextureTarget(*_textureTargets.begin());
    }
}

Texture& CEGUIRenderer::createTexture(const String& name)
{
    Divide::DIVIDE_ASSERT(_textures.find(name) == _textures.end(), ("A texture named '" + name + "' already exists.").c_str());

    DVDTexture* tex = CEGUI_NEW_AO DVDTexture(*this, name);
    _textures[name] = tex;

    LogTextureCreation(name);

    return *tex;
}

Texture& CEGUIRenderer::createTexture(const String& name,
                                      const String& filename,
                                      const String& resourceGroup)
{
    Divide::DIVIDE_ASSERT(_textures.find(name) == _textures.end(), ("A texture named '" + name + "' already exists.").c_str());
    
    DVDTexture* tex = CEGUI_NEW_AO DVDTexture(*this, name, filename, resourceGroup);
    _textures[name] = tex;

    LogTextureCreation(name);

    return *tex;
}

Texture& CEGUIRenderer::createTexture(const String& name, const Sizef& size)
{
    Divide::DIVIDE_ASSERT( _textures.find( name ) == _textures.end(), ("A texture named '" + name + "' already exists.").c_str());

    DVDTexture* tex = CEGUI_NEW_AO DVDTexture(*this, name, size);
    _textures[name] = tex;

    LogTextureCreation(name);

    return *tex;
}

void CEGUIRenderer::LogTextureCreation(const String& name)
{
    Logger* logger = Logger::getSingletonPtr();
    if (logger)
    {
        logger->logEvent("[OpenGLRenderer] Created texture: " + name);
    }
}

void CEGUIRenderer::destroyTexture(Texture& texture)
{
    destroyTexture(texture.getName());
}

void CEGUIRenderer::destroyTexture(const String& name)
{
    TextureMap::iterator i = _textures.find(name);

    if (_textures.end() != i)
    {
        LogTextureDestruction(name);
        CEGUI_DELETE_AO i->second;
        _textures.erase(i);
    }
}

void CEGUIRenderer::LogTextureDestruction(const String& name)
{
    Logger* logger = Logger::getSingletonPtr();
    if (logger)
    {
        logger->logEvent("[OpenGLRenderer] Destroyed texture: " + name);
    }
}

void CEGUIRenderer::destroyAllTextures()
{
    while (!_textures.empty())
    {
        destroyTexture(_textures.begin()->first);
    }
}

Texture& CEGUIRenderer::getTexture(const String& name) const
{
    const TextureMap::const_iterator i = _textures.find(name);
    Divide::DIVIDE_ASSERT(i != _textures.end(), ("No texture named '" + name + "' is available.").c_str());

    return *i->second;
}

bool CEGUIRenderer::isTextureDefined(const String& name) const
{
    return _textures.find(name) != _textures.end();
}

uint CEGUIRenderer::getMaxTextureSize() const
{
    return Divide::GFXDevice::GetDeviceInformation()._maxTextureSize;
}

Texture& CEGUIRenderer::createTexture(const String& name, const Divide::Texture_ptr& tex, const Sizef& sz)
{
    Divide::DIVIDE_ASSERT(_textures.find(name) == _textures.end(), ("A texture named '" + name + "' already exists.").c_str());

    DVDTexture* t = CEGUI_NEW_AO DVDTexture(*this, name, tex, sz);
    _textures[name] = t;

    LogTextureCreation(name);

    return *t;
}

void CEGUIRenderer::setDisplaySize(const Sizef& sz)
{
    if (sz != _displaySize)
    {
        _displaySize = sz;

        // update the default target's area
        Rectf area(_defaultTarget->getArea());
        area.setSize(sz);
        _defaultTarget->setArea(area);
    }
}

const Rectf& CEGUIRenderer::getActiveViewPort() const
{
    return _activeRenderTarget->getArea();
}

void CEGUIRenderer::beginRendering( Divide::GFX::CommandBuffer& bufferInOut, Divide::GFX::MemoryBarrierCommand& memCmdInOut )
{
    _bufferInOut = &bufferInOut;
    _memCmdInOut = &memCmdInOut;
    beginRendering();
}

void CEGUIRenderer::beginRendering()
{
    _activePipelineType = PipelineType::COUNT;
}

void CEGUIRenderer::endRendering()
{
    _activePipelineType = PipelineType::COUNT;
    _bufferInOut = nullptr;
    _memCmdInOut = nullptr;
}

void CEGUIRenderer::bindDefaultState( bool const scissor, const BlendMode mode, const glm::mat4& viewProjMat )
{
    using namespace Divide;
    thread_local PushConstantsStruct pushConstants{};

    DIVIDE_ASSERT(mode != BlendMode::BM_INVALID);

    const PipelineType pipelineType = scissor ? (mode == BlendMode::BM_NORMAL ? PipelineType::BLEND_NORMAL_SCISSOR        : PipelineType::BLEND_NORMAL_NO_SCISSOR)
                                              : (mode == BlendMode::BM_NORMAL ? PipelineType::BLEND_PREMULTIPLIED_SCISSOR : PipelineType::BLEND_PREMULTIPLIED_NO_SCISSOR);

    if (_activePipelineType != pipelineType )
    {
        _activePipelineType = pipelineType;
        Pipeline* pipeline = _pipelines[to_base( _activePipelineType )];

        DIVIDE_ASSERT(pipeline != nullptr);
        GFX::EnqueueCommand<GFX::BindPipelineCommand>( *_bufferInOut, { pipeline } );
    }

    if (_activePipelineType != PipelineType::COUNT )
    {
        memcpy(pushConstants.data[0].mat, glm::value_ptr( viewProjMat ), 16 * sizeof(float));
        pushConstants._set = true;
        GFX::EnqueueCommand<GFX::SendPushConstantsCommand>( *_bufferInOut )->_constants.set( pushConstants );
    }
}

}
