

#include "Headers/CEGUIFormattedListBox.h"

namespace CEGUI {
//----------------------------------------------------------------------------//
FormattedListboxTextItem::FormattedListboxTextItem(
    const String& text, const Colour& col, const HorizontalTextFormatting format,
    const uint item_id, void* const item_data, const bool disabled,
    const bool auto_delete)
    :  // initialise base class
      ListboxTextItem(text, item_id, item_data, disabled, auto_delete),
      // initialise subclass fields
      d_formatting(format),
      d_formattedRenderedString(nullptr),
      d_formattingAreaSize(0, 0) {
    setTextColours(col);
}

//----------------------------------------------------------------------------//
FormattedListboxTextItem::~FormattedListboxTextItem() {
    CEGUI_DELETE_AO d_formattedRenderedString;
}

//----------------------------------------------------------------------------//
HorizontalTextFormatting FormattedListboxTextItem::getFormatting() const noexcept {
    return d_formatting;
}

//----------------------------------------------------------------------------//
void FormattedListboxTextItem::setFormatting(
    const HorizontalTextFormatting fmt) {
    if (fmt == d_formatting) return;

    d_formatting = fmt;
    CEGUI_DELETE_AO d_formattedRenderedString;
    d_formattedRenderedString = nullptr;
    d_formattingAreaSize = Sizef(0, 0);
}

//----------------------------------------------------------------------------//
Sizef FormattedListboxTextItem::getPixelSize() const {
    if (!d_owner) return Sizef(0, 0);

    // reparse text if we need to.
    if (!d_renderedStringValid) parseTextString();

    // create formatter if needed
    if (!d_formattedRenderedString) setupStringFormatter();

    // get size of render area from target Window, to see if we need to reformat
    const Sizef area_sz(
        static_cast<const Listbox*>(d_owner)->getListRenderArea().getSize());
    if (area_sz != d_formattingAreaSize) {
        d_formattedRenderedString->format(d_owner, area_sz);
        d_formattingAreaSize = area_sz;
    }

    return Sizef(d_formattedRenderedString->getHorizontalExtent(d_owner),
                 d_formattedRenderedString->getVerticalExtent(d_owner));
}

//----------------------------------------------------------------------------//
void FormattedListboxTextItem::draw(GeometryBuffer& buffer,
                                    const Rectf& targetRect,
                                    const float alpha,
                                    const Rectf* clipper) const {
    // reparse text if we need to.
    if (!d_renderedStringValid) parseTextString();

    // create formatter if needed
    if (!d_formattedRenderedString) setupStringFormatter();

    // get size of render area from target Window, to see if we need to reformat
    // NB: We do not use targetRect, since it may not represent the same area.
    const Size<float> area_sz(
        static_cast<const Listbox*>(d_owner)->getListRenderArea().getSize());
    if (area_sz != d_formattingAreaSize) {
        d_formattedRenderedString->format(d_owner, area_sz);
        d_formattingAreaSize = area_sz;
    }

    // draw selection imagery
    if (d_selected && d_selectBrush != nullptr)
        d_selectBrush->render(buffer, targetRect, clipper,
                              getModulateAlphaColourRect(d_selectCols, alpha));

    // factor the Window alpha into our colours.
    const ColourRect final_colours(
        getModulateAlphaColourRect(ColourRect(0xFFFFFFFF), alpha));

    // draw the formatted text
    d_formattedRenderedString->draw(d_owner, buffer, targetRect.getPosition(),
                                    &final_colours, clipper);
}

//----------------------------------------------------------------------------//
void FormattedListboxTextItem::setupStringFormatter() const {
    // delete any existing formatter
    CEGUI_DELETE_AO d_formattedRenderedString;
    d_formattedRenderedString = nullptr;

    // create new formatter of whichever type...
    switch (d_formatting) {
        case HTF_LEFT_ALIGNED:
            d_formattedRenderedString =
                CEGUI_NEW_AO LeftAlignedRenderedString(d_renderedString);
            break;

        case HTF_RIGHT_ALIGNED:
            d_formattedRenderedString =
                CEGUI_NEW_AO RightAlignedRenderedString(d_renderedString);
            break;

        case HTF_CENTRE_ALIGNED:
            d_formattedRenderedString =
                CEGUI_NEW_AO CentredRenderedString(d_renderedString);
            break;

        case HTF_JUSTIFIED:
            d_formattedRenderedString =
                CEGUI_NEW_AO JustifiedRenderedString(d_renderedString);
            break;

        case HTF_WORDWRAP_LEFT_ALIGNED:
            d_formattedRenderedString = CEGUI_NEW_AO
                RenderedStringWordWrapper<LeftAlignedRenderedString>(
                    d_renderedString);
            break;

        case HTF_WORDWRAP_RIGHT_ALIGNED:
            d_formattedRenderedString = CEGUI_NEW_AO
                RenderedStringWordWrapper<RightAlignedRenderedString>(
                    d_renderedString);
            break;

        case HTF_WORDWRAP_CENTRE_ALIGNED:
            d_formattedRenderedString =
                CEGUI_NEW_AO RenderedStringWordWrapper<CentredRenderedString>(
                    d_renderedString);
            break;

        case HTF_WORDWRAP_JUSTIFIED:
            d_formattedRenderedString =
                CEGUI_NEW_AO RenderedStringWordWrapper<JustifiedRenderedString>(
                    d_renderedString);
            break;
    }
}

//----------------------------------------------------------------------------//
}