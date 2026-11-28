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
        struct SerializerContext
        {
            mutable boost::property_tree::iptree XmlTree;
            std::string _rootNodePath;
            std::string _fileName;
            ResourcePath _filePath;

            [[nodiscard]] bool read( const ResourcePath& filePath, const char* fileName, const std::string& rootNode );
            [[nodiscard]] bool write( const ResourcePath& filePath, const char* fileName ) const;
        };
    }

#ifndef GET_PARAM
#define CONCAT(first, second) first second

#define GET_PARAM(X) GET_TEMP_PARAM(X, X)

#define GET_TEMP_PARAM(X, TEMP) \
    TEMP = SerializerContext.XmlTree.get(SerializerContext._rootNodePath + TO_STRING(X), TEMP)

#define GET_PARAM_ATTRIB(X, Y) \
    X.Y = SerializerContext.XmlTree.get(CONCAT(CONCAT(SerializerContext._rootNodePath + TO_STRING(X), ".<xmlattr>."), TO_STRING(Y)), (X.Y))

#define PUT_TEMP_PARAM(X, TEMP) \
    SerializerContext.XmlTree.put(SerializerContext._rootNodePath + TO_STRING(X), TEMP)
#define PUT_PARAM_ATTRIB(X, Y) \
    SerializerContext.XmlTree.put(CONCAT(CONCAT(SerializerContext._rootNodePath + TO_STRING(X), ".<xmlattr>."), TO_STRING(Y)), (X.Y))
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

        detail::SerializerContext SerializerContext;
    };

    bool loadFromXML( IXMLSerializable& object, const ResourcePath& filePath, const char* fileName );
    bool saveToXML( const IXMLSerializable& object, const ResourcePath& filePath, const char* fileName );

    void writeXML( const ResourcePath& path, const boost::property_tree::ptree& tree );
    void readXML( const ResourcePath& path, boost::property_tree::ptree& tree );

    // An XML file built in memory, to be written later (possibly on another thread).
    struct FileSnapshot
    {
        enum class Backup : U8
        {
            NONE,
            // Copy _backupSource to "<_backupSource>.bak" first. A missing or empty source is fine; any other
            // copy error skips the write.
            IF_PRESENT,
            // Write only after _backupSource was copied to "<_backupSource>.bak".
            REQUIRED_TO_WRITE
        };

        boost::property_tree::ptree _tree;
        ResourcePath _folder;
        string _fileName;
        string _backupSource;
        string _progressMessage;
        Backup _backup{ Backup::NONE };
    };

    struct SaveSnapshot
    {
        vector<ResourcePath> _directories;
        vector<FileSnapshot> _files;
    };

    // File I/O only; safe on any thread. Creates the directories, then writes the files in order, reporting each
    // non-empty progress message first. Returns false if a file was skipped because its backup failed.
    [[nodiscard]] bool writeSnapshot( const SaveSnapshot& snapshot, const DELEGATE<void, std::string_view>& msgCallback );
    /// Child Functions
    void loadDefaultKeyBindings( const ResourcePath& file, const Scene* scene );
    void loadMusicPlaylist( const ResourcePath& scenePath, const Str<64>& fileName, Scene* scene, [[maybe_unused]] const Configuration& config );

    struct SceneNode
    {
        Str<64> name;
        U64 typeHash = 0u;

        vector<SceneNode> children;
    };

}  // namespace XML
}  // namespace Divide

#endif //DVD_XML_PARSER_H_
