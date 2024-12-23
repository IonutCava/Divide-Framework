

#include "Headers/TCPUDPImpl.h"
#include "Headers/Server.h"
#include "Headers/OPCodesImpl.h"
#include "Headers/Patch.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Resources/Headers/Resource.h"
#include "Utility/Headers/Localization.h"

namespace Divide
{

    TCPUDPImpl::TCPUDPImpl( boost::asio::io_context& io_context, channel& ch )
        : TCPUDPInterface( io_context, ch )
    {
    }

    void TCPUDPImpl::handlePacket( WorldPacket& p )
    {
        switch ( p.opcode() )
        {
            case OPCodesEx::CMSG_GEOMETRY_LIST: HandleGeometryListOpCode( p );
                break;

            default:
                TCPUDPInterface::handlePacket( p );
                break;
        };
    }

    void TCPUDPImpl::HandleGeometryListOpCode( WorldPacket& p )
    {
        PatchData dataIn;
        p >> dataIn.sceneName;
        p >> dataIn.size;
        Console::printfn( Util::StringFormat(LOCALE_STR("ASIO_RECEIVE_GEOMETRY_LIST"), dataIn.size ) );
        for ( U32 i = 0; i < dataIn.size; i++ )
        {
            string name, modelname;
            p >> name;
            p >> modelname;
            dataIn.name.push_back( name );
            dataIn.modelName.push_back( modelname );
        }

        if ( !Patch::compareData( dataIn ) )
        {
            WorldPacket r( OPCodesEx::SMSG_GEOMETRY_APPEND );

            const auto& patchData = Patch::modelData();
            r << patchData.size();
            for ( const FileData& dataOut : patchData )
            {
                r << dataOut.ItemName;
                r << dataOut.ModelName;
                r << dataOut.Orientation.x;
                r << dataOut.Orientation.y;
                r << dataOut.Orientation.z;
                r << dataOut.Position.x;
                r << dataOut.Position.y;
                r << dataOut.Position.z;
                r << dataOut.Scale.x;
                r << dataOut.Scale.y;
                r << dataOut.Scale.z;
            }
            Console::printfn( Util::StringFormat(LOCALE_STR("ASIO_SEND_GEOMETRY_APPEND"), patchData.size() ) );

            sendPacket( r );
            Patch::clearModelData();
        }
    }

    

};  // namespace Divide
