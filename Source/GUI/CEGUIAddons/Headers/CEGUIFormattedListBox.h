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
#ifndef CEGUI_FORMATTED_LIST_BOX_H_
#define CEGUI_FORMATTED_LIST_BOX_H_

// Code adapted from http://www.cegui.org.uk/phpBB2/viewtopic.php?f=10&t=4322

#include <Cegui/widgets/ListboxTextItem.h>

namespace CEGUI {
///! A ListboxItem based class that can do horizontal text formatting.
class FormattedListboxTextItem final : public ListboxTextItem {
   public:
    ///! Constructor
    FormattedListboxTextItem(
        const String& text, const Colour& col,
        HorizontalTextFormatting format = HTF_LEFT_ALIGNED,
        uint item_id = 0, void* item_data = nullptr,
        bool disabled = false, bool auto_delete = true);

    ///! Destructor.
    ~FormattedListboxTextItem();

    ///! Return the current formatting set.
    HorizontalTextFormatting getFormatting() const noexcept;
    /**!
        Set the formatting.  You should call Listbox::handleUpdatedItemData
        after setting the formatting in order to update the listbox.  We do not
        do it automatically since you may wish to batch changes to multiple
        items and multiple calls to handleUpdatedItemData is wasteful.
    */
    void setFormatting(HorizontalTextFormatting fmt);

    // overridden functions.
    Sizef getPixelSize() const override;
    void draw(GeometryBuffer& buffer, const Rectf& targetRect, float alpha, const Rectf* clipper) const override;

   protected:
    ///! Helper to create a FormattedRenderedString of an appropriate type.
    void setupStringFormatter() const;
    ///! Current formatting set
    HorizontalTextFormatting d_formatting;
    ///! Class that renders RenderedString with some formatting.
    mutable FormattedRenderedString* d_formattedRenderedString;
    ///! Tracks target area for rendering so we can reformat when needed
    mutable Sizef d_formattingAreaSize;
};

};  // namespace CEGUI
#endif
