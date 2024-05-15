/* Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the Software
is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_SGN_EDITOR_COMPONENT_H_
#define DVD_SGN_EDITOR_COMPONENT_H_

#include "Core/Headers/PlatformContextComponent.h"

namespace Divide
{
    class Editor;
    class SGNComponent;
    class ByteBuffer;
    class SceneNode;
    class SceneGraphNode;
    class PropertyWindow;

    namespace Attorney
    {
        class EditorComponentEditor;
        class EditorComponentSceneGraphNode;
    }; //namespace Attorney

    enum class ComponentType : U32
    {
        TRANSFORM = toBit(1),
        ANIMATION = toBit(2),
        INVERSE_KINEMATICS = toBit(3),
        RAGDOLL = toBit(4),
        NAVIGATION = toBit(5),
        BOUNDS = toBit(6),
        RENDERING = toBit(7),
        NETWORKING = toBit(8),
        UNIT = toBit(9),
        RIGID_BODY = toBit(10),
        SELECTION = toBit(11),
        DIRECTIONAL_LIGHT = toBit(12),
        POINT_LIGHT = toBit(13),
        SPOT_LIGHT = toBit(14),
        SCRIPT = toBit(15),
        ENVIRONMENT_PROBE = toBit(16),
        COUNT = 16
    };

    namespace Names {
        static const char* componentType[] = 
        {
            "TRANSFORM",
            "ANIMATION",
            "INVERSE_KINEMATICS",
            "RAGDOLL",
            "NAVIGATION",
            "BOUNDS",
            "RENDERING",
            "NETWORKING",
            "UNIT",
            "RIGID_BODY",
            "SELECTION",
            "DIRECTIONAL_LIGHT",
            "POINT_LIGHT",
            "SPOT_LIGHT",
            "SCRIPT",
            "ENVIRONMENT_PROBE",
            "NONE",
        };
    };

    static_assert(ArrayCount(Names::componentType) == to_base(ComponentType::COUNT) + 1u, "ComponentType name array out of sync!");
    namespace TypeUtil
    {
        [[nodiscard]] const char* ComponentTypeToString(const ComponentType compType) noexcept;
        [[nodiscard]] ComponentType StringToComponentType(const string & name);
    };

    enum class EditorComponentFieldType : U8
    {
        PUSH_TYPE = 0,
        SWITCH_TYPE,
        SLIDER_TYPE,
        SEPARATOR,
        BUTTON,
        DROPDOWN_TYPE, ///<Only U8 types supported!
        BOUNDING_BOX,
        BOUNDING_SPHERE,
        ORIENTED_BOUNDING_BOX,
        TRANSFORM,
        MATERIAL,
        COUNT
    };

    struct EditorComponentField
    {
        DELEGATE_STD<void, void*> _dataGetter = {};
        DELEGATE_STD<void, const void*> _dataSetter = {};
        DELEGATE_STD<const char*, U8> _displayNameGetter = {};

        Str<128> _tooltip = "";
        void* _data = nullptr;
        vec2<F32> _range = { 0.0f, 0.0f }; ///< Used by slider_type as a min / max range or dropdown as selected_index / count
        Str<32>  _name = "";
        F32 _step = 0.0f; ///< 0.0f == no +- buttons
        F32 _resetValue = 0.f;
        const char* _format = "";
        const char* const* _labels = nullptr;
        PushConstantType _basicType = PushConstantType::COUNT;
        EditorComponentFieldType _type = EditorComponentFieldType::COUNT;
        // Use this to configure smaller data sizes for integers only (signed or unsigned) like:
        // U8: (PushConstantType::UINT, _byteCount=BYTE)
        // I16: (PushConstantType::INT, _byteCount=WORD)
        // etc
        // byteCount of 3 is currently NOT supported
        PushConstantSize _basicTypeSize = PushConstantSize::DWORD;

        bool _readOnly = false;
        bool _serialise = true;
        bool _hexadecimal = false;

        template<typename T>
        T* getPtr() const;
        template<typename T>
        T get() const;

        template<typename T>
        void get(T& dataOut) const;

        template<typename T>
        void set(const T& dataIn);

        [[nodiscard]] const char* getDisplayName(const U8 index) const;
        [[nodiscard]] bool supportsByteCount() const noexcept;
        [[nodiscard]] bool isMatrix() const noexcept;
    };

    class EditorComponent final : public PlatformContextComponent, public GUIDWrapper
    {
        friend class Attorney::EditorComponentEditor;
        friend class Attorney::EditorComponentSceneGraphNode;

      public:

        explicit EditorComponent( PlatformContext& context, ComponentType type, std::string_view name);
        ~EditorComponent() override;

        void addHeader(const Str<32>& name) {
            EditorComponentField field = {};
            field._name = name;
            field._type = EditorComponentFieldType::PUSH_TYPE;
            field._readOnly = true;
            registerField(MOV(field));
        }

        void registerField(EditorComponentField&& field);

        [[nodiscard]] vector<EditorComponentField>& fields() noexcept { return _fields; }
        [[nodiscard]] const vector<EditorComponentField>& fields() const noexcept { return _fields; }

        void onChangedCbk(const DELEGATE<void, std::string_view>& cbk) { _onChangedCbk = cbk; }
        void onChangedCbk(DELEGATE<void, std::string_view>&& cbk) noexcept { _onChangedCbk = MOV(cbk); }


        PROPERTY_RW(Str<128>, name, "");
        PROPERTY_R( ComponentType, componentType, ComponentType::COUNT );

      protected:
        void onChanged(const EditorComponentField& field) const;
        void saveToXML(boost::property_tree::ptree& pt) const;
        void loadFromXML(const boost::property_tree::ptree& pt);

        bool saveCache(ByteBuffer& outputBuffer) const;
        bool loadCache(ByteBuffer& inputBuffer);

        void saveFieldToXML(const EditorComponentField& field, boost::property_tree::ptree& pt) const;
        void loadFieldFromXML(EditorComponentField& field, const boost::property_tree::ptree& pt);

      protected:
        DELEGATE<void, std::string_view> _onChangedCbk;
        vector<EditorComponentField> _fields;
    };

    namespace Attorney {
        class EditorComponentEditor {
            static vector<EditorComponentField>& fields(EditorComponent& comp) noexcept {
                return comp._fields;
            }

            static const vector<EditorComponentField>& fields(const EditorComponent& comp) noexcept {
                return comp._fields;
            }

            static void onChanged(const EditorComponent& comp, const EditorComponentField& field) {
                comp.onChanged(field);
            }

            friend class Divide::PropertyWindow;
        };

        class EditorComponentSceneGraphNode {
            static void saveToXML(const EditorComponent& comp, boost::property_tree::ptree& pt)
            {
                comp.saveToXML(pt);
            }

            static void loadFromXML(EditorComponent& comp, const boost::property_tree::ptree& pt)
            {
                comp.loadFromXML(pt);
            }

            static bool saveCache( const EditorComponent& comp, ByteBuffer& outputBuffer )
            {
                return comp.saveCache(outputBuffer);
            }

            static bool loadCache( EditorComponent& comp, ByteBuffer& inputBuffer )
            {
                return comp.loadCache(inputBuffer);
            }

            friend class Divide::SceneNode;
            friend class Divide::SGNComponent;
            friend class Divide::SceneGraphNode;
        };
    };  // namespace Attorney

};  // namespace Divide
#endif //DVD_SGN_EDITOR_COMPONENT_H_

#include "EditorComponent.inl"
