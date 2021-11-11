#include "stdafx.h"

#include "Headers/RTDrawDescriptor.h"

namespace Divide {

RTDrawMask::RTDrawMask() noexcept
    : _disabledColours{},
      _disabledDepth(false),
      _disabledStencil(false)
{
    _disabledColours.fill(false);
}

bool RTDrawMask::isEnabled(const RTAttachmentType type) const noexcept {
    switch (type) {
        case RTAttachmentType::Depth: return !_disabledDepth;
        case RTAttachmentType::Stencil: return !_disabledStencil;
        case RTAttachmentType::Colour: {
            for (const bool state : _disabledColours) {
                if (!state) {
                    return true;
                }
            }
            return false;
        }
        default: break;
    }

    return true;
}

bool RTDrawMask::isEnabled(const RTAttachmentType type, const U8 index) const noexcept {
    assert(index < MAX_RT_COLOUR_ATTACHMENTS);

    if (type == RTAttachmentType::Colour) {
        return !_disabledColours[index];
    }
     
    return isEnabled(type);
}

void RTDrawMask::setEnabled(const RTAttachmentType type, const U8 index, const bool state) noexcept {
    assert(index < MAX_RT_COLOUR_ATTACHMENTS);

    switch (type) {
        case RTAttachmentType::Depth   : _disabledDepth   = !state; break;
        case RTAttachmentType::Stencil : _disabledStencil = !state; break;
        case RTAttachmentType::Colour  : _disabledColours[index] = !state; break;
        default : break;
    }
}

void RTDrawMask::enableAll() {
    _disabledDepth = _disabledStencil = false;
    _disabledColours.fill(false);
}

void RTDrawMask::disableAll() {
    _disabledDepth = _disabledStencil = true;
    _disabledColours.fill(true);
}

RTClearDescriptor::RTClearDescriptor() noexcept
    : _clearColourAttachment{}
{
    _clearColourAttachment.fill(true);
    _clearExternalColour = false;
    _clearExternalDepth = false;
}

RTClearColourDescriptor::RTClearColourDescriptor() noexcept
{
    _customClearColour.fill(DefaultColours::BLACK);
}

RTDrawDescriptor::RTDrawDescriptor() noexcept
{
    _drawMask.enableAll();
}

}; //namespace Divide