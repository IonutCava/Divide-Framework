/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_RENDER_TARGET_ATTACHMENT_H_
#define DVD_RENDER_TARGET_ATTACHMENT_H_

#include "Platform/Video/Textures/Headers/TextureDescriptor.h"
#include "Platform/Video/Textures/Headers/SamplerDescriptor.h"

namespace Divide {

class Texture;
FWD_DECLARE_MANAGED_CLASS(RTAttachment);


/// This enum is used when creating render targets to define the channel that the texture will attach to
enum class RTAttachmentType : U8 {
    COLOUR = 0,
    DEPTH,
    DEPTH_STENCIL,
    COUNT
};

struct RTAttachmentDescriptor
{
    explicit RTAttachmentDescriptor( const SamplerDescriptor sampler, const RTAttachmentType type, const RTColourAttachmentSlot slot, const bool autoResolve) noexcept
        : _sampler(sampler)
        , _type(type)
        , _slot(slot)
        , _autoResolve(autoResolve)
    {
    }

    SamplerDescriptor _sampler{};
    RTAttachment* _externalAttachment{nullptr};
    RTAttachmentType _type{ RTAttachmentType::COUNT };
    RTColourAttachmentSlot _slot{ RTColourAttachmentSlot::COUNT };
    bool _autoResolve{true};
};

struct RTUsageTracker
{
    enum class Layout : U8
    {
        ATTACHMENT = 0,
        SHADER_READ,
        COPY_READ,
        COPY_WRITE,
        COUNT
    };
    struct Names {
        inline static const char* layout[] = {
            "ATTACHMENT",
            "SHADER_READ",
            "COPY_READ",
            "COPY_WRITE",
            "COUNT"
        };

        static_assert(std::size(layout) == to_base(Layout::COUNT) + 1u, "Layout name array out of sync!");
    };

    Layout _usage = Layout::COUNT;
};

constexpr static U32 RT_DEPTH_ATTACHMENT_IDX = to_base( RTColourAttachmentSlot::COUNT );
constexpr static U8 RT_MAX_ATTACHMENT_COUNT = to_base( RTColourAttachmentSlot::COUNT ) + 1;

// External attachments get added last and OVERRIDE any normal attachments found at the same type+index location
struct ExternalRTAttachmentDescriptor final : public RTAttachmentDescriptor
{
    explicit ExternalRTAttachmentDescriptor(RTAttachment* attachment,
                                            const SamplerDescriptor sampler,
                                            const RTAttachmentType type,
                                            const RTColourAttachmentSlot slot,
                                            bool autoResolve = true )
        : RTAttachmentDescriptor(sampler, type, slot, autoResolve)
    {
        _externalAttachment = attachment;
    }
};

struct InternalRTAttachmentDescriptor final : public RTAttachmentDescriptor
{
    explicit InternalRTAttachmentDescriptor(TextureDescriptor& descriptor,
                                            const SamplerDescriptor sampler,
                                            const RTAttachmentType type,
                                            const RTColourAttachmentSlot slot,
                                            bool autoResolve = true)
        : RTAttachmentDescriptor(sampler, type, slot, autoResolve)
        , _texDescriptor(descriptor)
    {
    }

    TextureDescriptor _texDescriptor;
};

using InternalRTAttachmentDescriptors = fixed_vector<InternalRTAttachmentDescriptor, RT_MAX_ATTACHMENT_COUNT>;
using ExternalRTAttachmentDescriptors = fixed_vector<ExternalRTAttachmentDescriptor, RT_MAX_ATTACHMENT_COUNT>;

class RenderTarget;
class RTAttachment final
{
    public:
        explicit RTAttachment(RenderTarget& parent, const RTAttachmentDescriptor& descriptor) noexcept;
        ~RTAttachment();

        [[nodiscard]] Handle<Texture> texture() const;

        void setTexture( Handle<Texture> renderTexture, Handle<Texture> resolveTexture) noexcept;

        RenderTarget& parent() noexcept;
        [[nodiscard]] const RenderTarget& parent() const noexcept;

        RTAttachmentDescriptor _descriptor;
        RTUsageTracker _renderUsage{};  // state for render (MSAA) image
        RTUsageTracker _resolveUsage{};  // state for resolve image (single-sample target)

        PROPERTY_R_IW(Handle<Texture>, renderTexture, INVALID_HANDLE<Texture> );
        PROPERTY_R_IW(Handle<Texture>, resolvedTexture, INVALID_HANDLE<Texture> );

        PROPERTY_RW(U32, binding, 0u);
        PROPERTY_RW(bool, changed, false);

    protected:
        RenderTarget& _parent;
};

FWD_DECLARE_MANAGED_CLASS(RTAttachment);


}; //namespace Divide

#endif //DVD_RENDER_TARGET_ATTACHMENT_H_
