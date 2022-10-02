#include "stdafx.h"

#include "Headers/RTDrawDescriptor.h"

namespace Divide
{

    bool IsEnabled( const RTDrawMask& mask, const RTAttachmentType type ) noexcept
    {
        switch ( type )
        {
            case RTAttachmentType::DEPTH:
            case RTAttachmentType::DEPTH_STENCIL:   return !mask._disabledDepth;
            case RTAttachmentType::COLOUR:
            {
                for ( const bool state : mask._disabledColours )
                {
                    if ( !state )
                    {
                        return true;
                    }
                }
                return false;
            }
            default: break;
        }

        return true;
    }

    bool IsEnabled( const RTDrawMask& mask, const RTAttachmentType type, const RTColourAttachmentSlot slot ) noexcept
    {
        if ( type == RTAttachmentType::COLOUR )
        {
            return !mask._disabledColours[to_base( slot )];
        }

        return IsEnabled( mask, type );
    }

    void SetEnabled( RTDrawMask& mask, const RTAttachmentType type, const RTColourAttachmentSlot slot, const bool state ) noexcept
    {
        switch ( type )
        {
            case RTAttachmentType::DEPTH:
            case RTAttachmentType::DEPTH_STENCIL: mask._disabledDepth = !state; break;
            case RTAttachmentType::COLOUR: mask._disabledColours[to_base( slot )] = !state; break;
            default: break;
        }
    }

    void EnableAll( RTDrawMask& mask )
    {
        mask._disabledDepth = false;
        mask._disabledColours.fill( false );
    }

    void DisableAll( RTDrawMask& mask )
    {
        mask._disabledDepth = true;
        mask._disabledColours.fill( true );
    }

}; //namespace Divide