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
#ifndef DVD_XML_PARSER_H_
#define DVD_XML_PARSER_H_

#include "Platform/Headers/PlatformDefines.h"

namespace Divide
{

    class Scene;
    class GFXDevice;
    class SceneGraphNode;
    class PlatformContext;

    struct Configuration;

    FWD_DECLARE_MANAGED_CLASS( Material );

    namespace XML
    {
        namespace detail
        {
            struct LoadSave
            {
                bool _saveFileOK = false;
                ResourcePath _loadPath;
                string _loadFile;

                mutable ResourcePath _savePath;
                mutable string _saveFile;

                string _rootNodePath = {};

                mutable boost::property_tree::iptree XmlTree;

                bool read( const ResourcePath& filePath, const char* fileName, const string& rootNode );
                bool prepareSaveFile( const ResourcePath& filePath, const char* fileName ) const;
                void write() const;
            };
        }

#ifndef GET_PARAM
#define CONCAT(first, second) first second

#define GET_PARAM(X) GET_TEMP_PARAM(X, X)

#define GET_TEMP_PARAM(X, TEMP) \
    TEMP = LoadSave.XmlTree.get(LoadSave._rootNodePath + TO_STRING(X), TEMP)

#define GET_PARAM_ATTRIB(X, Y) \
    X.Y = LoadSave.XmlTree.get(CONCAT(CONCAT(LoadSave._rootNodePath + TO_STRING(X), ".<xmlattr>."), TO_STRING(Y)), (X.Y))

#define PUT_TEMP_PARAM(X, TEMP) \
    LoadSave.XmlTree.put(LoadSave._rootNodePath + TO_STRING(X), TEMP)
#define PUT_PARAM_ATTRIB(X, Y) \
    LoadSave.XmlTree.put(CONCAT(CONCAT(LoadSave._rootNodePath + TO_STRING(X), ".<xmlattr>."), TO_STRING(Y)), (X.Y))
#endif

#define PUT_PARAM(X) PUT_TEMP_PARAM(X, X)

        class IXMLSerializable
        {
            public:
            virtual ~IXMLSerializable() = default;

            protected:
            friend bool loadFromXML( IXMLSerializable& object, const ResourcePath& filePath, const char* fileName );
            friend bool saveToXML( const IXMLSerializable& object, const ResourcePath& filePath, const char* fileName );

            virtual bool fromXML( const ResourcePath& xmlFilePath, const char* fileName ) = 0;
            virtual bool toXML( const ResourcePath& xmlFilePath, const char* fileName ) const = 0;

            detail::LoadSave LoadSave;
        };

        bool loadFromXML( IXMLSerializable& object, const ResourcePath& filePath, const char* fileName );
        bool saveToXML( const IXMLSerializable& object, const ResourcePath& filePath, const char* fileName );

        void writeXML( const ResourcePath& path, const boost::property_tree::ptree& tree );
        void readXML( const ResourcePath& path, boost::property_tree::ptree& tree );
        /// Child Functions
        void loadDefaultKeyBindings( const ResourcePath& file, const Scene* scene );
        void loadMusicPlaylist( const ResourcePath& scenePath, const Str<64>& fileName, const Scene* const scene, [[maybe_unused]] const Configuration& config );

        struct SceneNode
        {
            Str<64> name;
            U64 typeHash = 0u;

            vector<SceneNode> children;
        };

    }  // namespace XML
}  // namespace Divide

#endif //DVD_XML_PARSER_H_
