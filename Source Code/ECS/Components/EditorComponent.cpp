#include "stdafx.h"

#include "Headers/EditorComponent.h"

#include "Core/Math/BoundingVolumes/Headers/BoundingSphere.h"
#include "Geometry/Material/Headers/Material.h"
#include "ECS/Components/Headers/TransformComponent.h"

namespace Divide {
    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    namespace {
        string GetFullFieldName(const char* componentName, const Str32& fieldName) {
            constexpr std::array<std::string_view, 6> InvalidXMLStrings = {
                " ", "[", "]", "...", "..", "."
            };

            const string temp = Util::ReplaceString(fieldName.c_str(), InvalidXMLStrings, "__");
            return Util::StringFormat("%s.%s", componentName, temp.c_str());
        }
    }

    namespace TypeUtil {
        const char* ComponentTypeToString(const ComponentType compType) noexcept {
            for (U32 i = 1u; i < to_U32(ComponentType::COUNT) + 1; ++i) {
                if (1u << i == to_base(compType)) {
                    return Names::componentType[i - 1u];
                }
            }

            return Names::componentType[to_base(ComponentType::COUNT)];
        }

        ComponentType StringToComponentType(const string& name) {
            for (U32 i = 1u; i < to_U32(ComponentType::COUNT) + 1; ++i) {
                if (strcmp(name.c_str(), Names::componentType[i - 1u]) == 0) {
                    return static_cast<ComponentType>(1 << i);
                }
            }

            return ComponentType::COUNT;
        }
    }

    EditorComponent::EditorComponent(const Str128& name)
        : GUIDWrapper(),
          _name(name)
    {
    }

    EditorComponent::EditorComponent(SGNComponent& parentComp, const ComponentType parentComponentType, const Str128& name)
        : EditorComponent(name)
    {
        _parentComponentType = parentComponentType;
        _parentComp = &parentComp;
    }


    void EditorComponent::registerField(EditorComponentField&& field) {
        _fields.erase(
            std::remove_if(std::begin(_fields), std::end(_fields),
                [&field](const EditorComponentField& it)
                -> bool { return it._name == field._name &&
                                 it._type == field._type; }),
            std::end(_fields));

        assert(field._basicTypeSize == GFX::PushConstantSize::DWORD || field.supportsByteCount());

        _fields.push_back(field);
    }

    void EditorComponent::onChanged(EditorComponentField& field) const {
        if (_onChangedCbk) {
            _onChangedCbk(field._name.c_str());
        }
    }

    // May be wrong endpoint
    bool EditorComponent::saveCache(ByteBuffer& outputBuffer) const {
        outputBuffer << BYTE_BUFFER_VERSION;
        outputBuffer << to_U32(_parentComponentType);
        return true;
    }

    // May be wrong endpoint
    bool EditorComponent::loadCache(ByteBuffer& inputBuffer) {
        auto tempVer = decltype(BYTE_BUFFER_VERSION){0};
        inputBuffer >> tempVer;
        U32 tempCompType = 0u;
        inputBuffer >> tempCompType;
        if (tempVer == BYTE_BUFFER_VERSION && static_cast<ComponentType>(tempCompType) == _parentComponentType) {
            return true;
        }

        return false;
    }


    void EditorComponent::saveToXML(boost::property_tree::ptree& pt) const {
        pt.put(_name.c_str(), "");

        for (const EditorComponentField& field : _fields) {
            auto entryName = GetFullFieldName(_name.c_str(), field._name);
            if (!field._serialise) {
                continue;
            }

            switch(field._type) {
                case EditorComponentFieldType::SLIDER_TYPE:
                case EditorComponentFieldType::PUSH_TYPE: {
                    saveFieldToXML(field, pt);
                } break;
                case EditorComponentFieldType::TRANSFORM: {
                    TransformComponent* transform = field.getPtr<TransformComponent>();

                    vec3<F32> scale = transform->getLocalScale();
                    vec3<F32> position = transform->getLocalPosition();

                    vec3<Angle::DEGREES<F32>> orientationEuler;
                    Quaternion<F32> orientation = transform->getLocalOrientation();
                    orientationEuler = Angle::to_DEGREES(orientation.getEuler());

                    pt.put(entryName + ".position.<xmlattr>.x", position.x);
                    pt.put(entryName + ".position.<xmlattr>.y", position.y);
                    pt.put(entryName + ".position.<xmlattr>.z", position.z);

                    pt.put(entryName + ".orientation.<xmlattr>.x", orientationEuler.pitch);
                    pt.put(entryName + ".orientation.<xmlattr>.y", orientationEuler.yaw);
                    pt.put(entryName + ".orientation.<xmlattr>.z", orientationEuler.roll);

                    pt.put(entryName + ".scale.<xmlattr>.x", scale.x);
                    pt.put(entryName + ".scale.<xmlattr>.y", scale.y);
                    pt.put(entryName + ".scale.<xmlattr>.z", scale.z);
                }break;
                case EditorComponentFieldType::MATERIAL: {
                    field.getPtr<Material>()->saveToXML(entryName, pt);
                }break;
                default:
                case EditorComponentFieldType::BUTTON: {
                    //Skip
                } break;
                case EditorComponentFieldType::BOUNDING_BOX: {
                    BoundingBox bb = {};
                    field.get<BoundingBox>(bb);

                    pt.put(entryName + ".aabb.min.<xmlattr>.x", bb.getMin().x);
                    pt.put(entryName + ".aabb.min.<xmlattr>.y", bb.getMin().y);
                    pt.put(entryName + ".aabb.min.<xmlattr>.z", bb.getMin().z);
                    pt.put(entryName + ".aabb.max.<xmlattr>.x", bb.getMax().x);
                    pt.put(entryName + ".aabb.max.<xmlattr>.y", bb.getMax().y);
                    pt.put(entryName + ".aabb.max.<xmlattr>.z", bb.getMax().z);
                } break;
                case EditorComponentFieldType::ORIENTED_BOUNDING_BOX: {
                    // We don't save this to XML!
                }break;
                case EditorComponentFieldType::BOUNDING_SPHERE: {
                    BoundingSphere bs = {};
                    field.get<BoundingSphere>(bs);

                    pt.put(entryName + ".aabb.center.<xmlattr>.x", bs.getCenter().x);
                    pt.put(entryName + ".aabb.center.<xmlattr>.y", bs.getCenter().y);
                    pt.put(entryName + ".aabb.center.<xmlattr>.z", bs.getCenter().z);
                    pt.put(entryName + ".aabb.radius", bs.getRadius());
                }break;
            }
        }

        if (_parentComp != nullptr) {
            _parentComp->saveToXML(pt);
        }
    }

    void EditorComponent::loadFromXML(const boost::property_tree::ptree& pt) {
        if (!pt.get(_name.c_str(), "").empty()) {
            for (EditorComponentField& field : _fields) {
                auto entryName = GetFullFieldName(_name.c_str(), field._name);
                if (!field._serialise) {
                    continue;
                }

                switch (field._type) {
                    case EditorComponentFieldType::SLIDER_TYPE:
                    case EditorComponentFieldType::PUSH_TYPE: {
                        loadFieldFromXML(field, pt);
                    } break;
                    case EditorComponentFieldType::TRANSFORM: {
                        TransformComponent* transform = field.getPtr<TransformComponent>();

                        vec3<F32> scale;
                        vec3<F32> position;
                        vec3<Angle::DEGREES<F32>> orientationEuler;

                        position.set(pt.get<F32>(entryName + ".position.<xmlattr>.x", 0.0f),
                                     pt.get<F32>(entryName + ".position.<xmlattr>.y", 0.0f),
                                     pt.get<F32>(entryName + ".position.<xmlattr>.z", 0.0f));

                        orientationEuler.pitch = pt.get<F32>(entryName + ".orientation.<xmlattr>.x", 0.0f);
                        orientationEuler.yaw   = pt.get<F32>(entryName + ".orientation.<xmlattr>.y", 0.0f);
                        orientationEuler.roll  = pt.get<F32>(entryName + ".orientation.<xmlattr>.z", 0.0f);

                        scale.set(pt.get<F32>(entryName + ".scale.<xmlattr>.x", 1.0f),
                                  pt.get<F32>(entryName + ".scale.<xmlattr>.y", 1.0f),
                                  pt.get<F32>(entryName + ".scale.<xmlattr>.z", 1.0f));

                        Quaternion<F32> rotation;
                        rotation.fromEuler(orientationEuler);
                        transform->setScale(scale);
                        transform->setRotation(rotation);
                        transform->setPosition(position);
                        transform->resetInterpolation();
                    }break;
                    case EditorComponentFieldType::MATERIAL: {
                        Material* mat = field.getPtr<Material>();
                        mat->loadFromXML(entryName, pt);
                    }break;
                    default:
                    case EditorComponentFieldType::DROPDOWN_TYPE:
                    case EditorComponentFieldType::BUTTON: {
                        // Skip
                    }  break;
                    case EditorComponentFieldType::BOUNDING_BOX: {
                        BoundingBox bb = {};
                        bb.setMin(
                        {
                            pt.get<F32>(entryName + ".aabb.min.<xmlattr>.x", -1.0f),
                            pt.get<F32>(entryName + ".aabb.min.<xmlattr>.y", -1.0f),
                            pt.get<F32>(entryName + ".aabb.min.<xmlattr>.z", -1.0f)
                        });
                        bb.setMax(
                        {
                            pt.get<F32>(entryName + ".aabb.max.<xmlattr>.x", 1.0f),
                            pt.get<F32>(entryName + ".aabb.max.<xmlattr>.y", 1.0f),
                            pt.get<F32>(entryName + ".aabb.max.<xmlattr>.z", 1.0f)
                        });
                        field.set<BoundingBox>(bb);
                    } break;
                    case EditorComponentFieldType::ORIENTED_BOUNDING_BOX:
                    {
                        // We don't load this from XML!
                    }break;
                    case EditorComponentFieldType::BOUNDING_SPHERE: {
                        BoundingSphere bs = {};
                        bs.setCenter(
                        {
                            pt.get<F32>(entryName + ".aabb.center.<xmlattr>.x", 0.f),
                            pt.get<F32>(entryName + ".aabb.center.<xmlattr>.y", 0.f),
                            pt.get<F32>(entryName + ".aabb.center.<xmlattr>.z", 0.f)
                        });
                        bs.setRadius(pt.get<F32>(entryName + ".aabb.radius", 1.0f));
                        field.set<BoundingSphere>(bs);
                    } break;
                }
            }
            if (_parentComp != nullptr) {
                _parentComp->loadFromXML(pt);
            }
        }
    }

    namespace {
        template<typename T, size_t num_comp>
        void saveVector(const string& entryName, const EditorComponentField& field, boost::property_tree::ptree& pt) {
            T data = {};
            field.get<T>(data);

            pt.put((entryName + ".<xmlattr>.x").c_str(), data.x);
            pt.put((entryName + ".<xmlattr>.y").c_str(), data.y);
            if_constexpr (num_comp > 2) {
                pt.put((entryName + ".<xmlattr>.z").c_str(), data.z);
                if_constexpr(num_comp > 3) {
                    pt.put((entryName + ".<xmlattr>.w").c_str(), data.w);
                }
            }
        }

        template<typename T, size_t num_comp>
        void loadVector(const string& entryName, EditorComponentField& field, const boost::property_tree::ptree& pt) {
            T data = field.get<T>();

            data.x = pt.get((entryName + ".<xmlattr>.x").c_str(), data.x);
            data.y = pt.get((entryName + ".<xmlattr>.y").c_str(), data.y);

            if_constexpr(num_comp > 2) {
                data.z = pt.get((entryName + ".<xmlattr>.z").c_str(), data.z);
                if_constexpr(num_comp > 3) {
                    data.w = pt.get((entryName + ".<xmlattr>.w").c_str(), data.w);
                }
            }
            field.set<T>(data);
        }

        template<typename T, size_t num_rows>
        void saveMatrix(const string& entryName, const EditorComponentField& field, boost::property_tree::ptree& pt) {
            T data = {};
            field.get<T>(data);

            pt.put((entryName + ".<xmlattr>.00").c_str(), data.m[0][0]);
            pt.put((entryName + ".<xmlattr>.01").c_str(), data.m[0][1]);
            pt.put((entryName + ".<xmlattr>.10").c_str(), data.m[1][0]);
            pt.put((entryName + ".<xmlattr>.11").c_str(), data.m[1][1]);

            if_constexpr(num_rows > 2) {
                pt.put((entryName + ".<xmlattr>.02").c_str(), data.m[0][2]);
                pt.put((entryName + ".<xmlattr>.12").c_str(), data.m[1][2]);
                pt.put((entryName + ".<xmlattr>.20").c_str(), data.m[2][0]);
                pt.put((entryName + ".<xmlattr>.21").c_str(), data.m[2][1]);
                pt.put((entryName + ".<xmlattr>.22").c_str(), data.m[2][2]);

                if_constexpr(num_rows > 3) {
                    pt.put((entryName + ".<xmlattr>.03").c_str(), data.m[0][3]);
                    pt.put((entryName + ".<xmlattr>.13").c_str(), data.m[1][3]);
                    pt.put((entryName + ".<xmlattr>.23").c_str(), data.m[2][3]);
                    pt.put((entryName + ".<xmlattr>.30").c_str(), data.m[3][0]);
                    pt.put((entryName + ".<xmlattr>.31").c_str(), data.m[3][1]);
                    pt.put((entryName + ".<xmlattr>.32").c_str(), data.m[3][2]);
                    pt.put((entryName + ".<xmlattr>.33").c_str(), data.m[3][3]);
                }
            }
        }

        template<typename T, size_t num_rows>
        void loadMatrix(const string& entryName, EditorComponentField& field, const boost::property_tree::ptree& pt) {
            T data = field.get<T>();

            data.m[0][0] = pt.get((entryName + ".<xmlattr>.00").c_str(), data.m[0][0]);
            data.m[0][1] = pt.get((entryName + ".<xmlattr>.01").c_str(), data.m[0][1]);
            data.m[1][0] = pt.get((entryName + ".<xmlattr>.10").c_str(), data.m[1][0]);
            data.m[1][1] = pt.get((entryName + ".<xmlattr>.11").c_str(), data.m[1][1]);

            if_constexpr(num_rows > 2) {
                data.m[0][2] = pt.get((entryName + ".<xmlattr>.02").c_str(), data.m[0][2]);
                data.m[1][2] = pt.get((entryName + ".<xmlattr>.12").c_str(), data.m[1][2]);
                data.m[2][0] = pt.get((entryName + ".<xmlattr>.20").c_str(), data.m[2][0]);
                data.m[2][1] = pt.get((entryName + ".<xmlattr>.21").c_str(), data.m[2][1]);
                data.m[2][2] = pt.get((entryName + ".<xmlattr>.22").c_str(), data.m[2][2]);

                if_constexpr(num_rows > 3) {
                    data.m[0][3] = pt.get((entryName + ".<xmlattr>.03").c_str(), data.m[0][3]);
                    data.m[1][3] = pt.get((entryName + ".<xmlattr>.13").c_str(), data.m[1][3]);
                    data.m[2][3] = pt.get((entryName + ".<xmlattr>.23").c_str(), data.m[2][3]);
                    data.m[3][0] = pt.get((entryName + ".<xmlattr>.30").c_str(), data.m[3][0]);
                    data.m[3][1] = pt.get((entryName + ".<xmlattr>.31").c_str(), data.m[3][1]);
                    data.m[3][2] = pt.get((entryName + ".<xmlattr>.32").c_str(), data.m[3][2]);
                    data.m[3][3] = pt.get((entryName + ".<xmlattr>.33").c_str(), data.m[3][3]);
                }
            }

            field.set<T>(data);
        }
    }

    void EditorComponent::saveFieldToXML(const EditorComponentField& field, boost::property_tree::ptree& pt) const {
        auto entryName = GetFullFieldName(_name.c_str(), field._name);

        switch (field._basicType) {
            case GFX::PushConstantType::BOOL: {
                pt.put(entryName.c_str(), field.get<bool>());
            } break;
            case GFX::PushConstantType::INT: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: pt.put(entryName.c_str(), field.get<I64>()); break;
                    case GFX::PushConstantSize::DWORD: pt.put(entryName.c_str(), field.get<I32>()); break;
                    case GFX::PushConstantSize::WORD:  pt.put(entryName.c_str(), field.get<I16>()); break;
                    case GFX::PushConstantSize::BYTE:  pt.put(entryName.c_str(), field.get<I8>()); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UINT: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: pt.put(entryName.c_str(), field.get<U64>()); break;
                    case GFX::PushConstantSize::DWORD: pt.put(entryName.c_str(), field.get<U32>()); break;
                    case GFX::PushConstantSize::WORD:  pt.put(entryName.c_str(), field.get<U16>()); break;
                    case GFX::PushConstantSize::BYTE:  pt.put(entryName.c_str(), field.get<U8>()); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::FLOAT: {
                pt.put(entryName.c_str(), field.get<F32>());
            } break;
            case GFX::PushConstantType::DOUBLE: {
                pt.put(entryName.c_str(), field.get<D64>());
            } break;
            case GFX::PushConstantType::IVEC2: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveVector<vec2<I64>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveVector<vec2<I32>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveVector<vec2<I16>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveVector<vec2<I8>, 2>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::IVEC3: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveVector<vec3<I64>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveVector<vec3<I32>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveVector<vec3<I16>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveVector<vec3<I8>, 3>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::IVEC4: {
               switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveVector<vec4<I64>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveVector<vec4<I32>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveVector<vec4<I16>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveVector<vec4<I8>, 4>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UVEC2: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveVector<vec2<U64>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveVector<vec2<U32>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveVector<vec2<U16>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveVector<vec2<U8>, 2>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UVEC3: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveVector<vec3<U64>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveVector<vec3<U32>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveVector<vec3<U16>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveVector<vec3<U8>, 3>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UVEC4: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveVector<vec4<U64>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveVector<vec4<U32>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveVector<vec4<U16>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveVector<vec4<U8>, 4>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::VEC2: {
                saveVector<vec2<F32>, 2>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::VEC3: {
                saveVector<vec3<F32>, 3>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::VEC4: {
                saveVector<vec4<F32>, 4>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DVEC2: {
                saveVector<vec2<D64>, 2>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DVEC3: {
                saveVector<vec3<D64>, 3>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DVEC4: {
                saveVector<vec4<D64>, 4>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::IMAT2: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveMatrix<mat2<I64>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveMatrix<mat2<I32>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveMatrix<mat2<I16>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveMatrix<mat2<I8>, 2>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::IMAT3: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveMatrix<mat3<I64>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveMatrix<mat3<I32>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveMatrix<mat3<I16>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveMatrix<mat3<I8>, 3>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::IMAT4: {
                 switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveMatrix<mat4<I64>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveMatrix<mat4<I32>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveMatrix<mat4<I16>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveMatrix<mat4<I8>, 4>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UMAT2: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveMatrix<mat2<U64>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveMatrix<mat2<U32>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveMatrix<mat2<U16>, 2>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveMatrix<mat2<U8>, 2>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UMAT3: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveMatrix<mat3<U64>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveMatrix<mat3<U32>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveMatrix<mat3<U16>, 3>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveMatrix<mat3<U8>, 3>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UMAT4: {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: {
                        saveMatrix<mat4<U64>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::DWORD: {
                        saveMatrix<mat4<U32>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::WORD: {
                        saveMatrix<mat4<U16>, 4>(entryName, field, pt);
                    } break;
                    case GFX::PushConstantSize::BYTE: {
                        saveMatrix<mat4<U8>, 4>(entryName, field, pt);
                    } break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::MAT2: {
                saveMatrix<mat2<F32>, 2>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::MAT3: {
                saveMatrix<mat3<F32>, 3>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::MAT4: {
                saveMatrix<mat4<F32>, 4>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DMAT2: {
                saveMatrix<mat2<D64>, 2>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DMAT3: {
                saveMatrix<mat3<D64>, 3>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DMAT4: {
                saveMatrix<mat4<D64>, 4>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::FCOLOUR3: {
                FColour3 data = {};
                field.get<FColour3>(data);
                pt.put((entryName + ".<xmlattr>.r").c_str(), data.r);
                pt.put((entryName + ".<xmlattr>.g").c_str(), data.g);
                pt.put((entryName + ".<xmlattr>.b").c_str(), data.b);
            } break;
            case GFX::PushConstantType::FCOLOUR4: {
                FColour4 data = {};
                field.get<FColour4>(data);
                pt.put((entryName + ".<xmlattr>.r").c_str(), data.r);
                pt.put((entryName + ".<xmlattr>.g").c_str(), data.g);
                pt.put((entryName + ".<xmlattr>.b").c_str(), data.b);
                pt.put((entryName + ".<xmlattr>.a").c_str(), data.a);
            } break;
            default: break;
        }
    }

    void EditorComponent::loadFieldFromXML(EditorComponentField& field, const boost::property_tree::ptree& pt) {
        auto entryName = GetFullFieldName(_name.c_str(), field._name);

        switch (field._basicType) {
            case GFX::PushConstantType::BOOL:
            {
                bool val = pt.get(entryName.c_str(), field.get<bool>());
                field.set<bool>(val);
            } break;
            case GFX::PushConstantType::INT:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: field.set<I64>(pt.get(entryName.c_str(), field.get<I64>())); break;
                    case GFX::PushConstantSize::DWORD: field.set<I32>(pt.get(entryName.c_str(), field.get<I32>())); break;
                    case GFX::PushConstantSize::WORD:  field.set<I16>(pt.get(entryName.c_str(), field.get<I16>())); break;
                    case GFX::PushConstantSize::BYTE:  field.set<I8>(pt.get(entryName.c_str(), field.get<I8>())); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UINT:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: field.set<U64>(pt.get(entryName.c_str(), field.get<U64>())); break;
                    case GFX::PushConstantSize::DWORD: field.set<U32>(pt.get(entryName.c_str(), field.get<U32>())); break;
                    case GFX::PushConstantSize::WORD:  field.set<U16>(pt.get(entryName.c_str(), field.get<U16>())); break;
                    case GFX::PushConstantSize::BYTE:  field.set<U8>(pt.get(entryName.c_str(), field.get<U8>())); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::FLOAT:
            {
                F32 val = pt.get(entryName.c_str(), field.get<F32>());
                field.set<F32>(val);
            } break;
            case GFX::PushConstantType::DOUBLE:
            {
                D64 val = pt.get(entryName.c_str(), field.get<D64>());
                field.set<D64>(val);
            } break;
            case GFX::PushConstantType::IVEC2:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadVector<vec2<I64>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadVector<vec2<I32>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadVector<vec2<I16>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadVector<vec2<I8>, 2>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::IVEC3:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadVector<vec3<I64>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadVector<vec3<I32>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadVector<vec3<I16>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadVector<vec3<I8>, 3>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::IVEC4:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadVector<vec4<I64>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadVector<vec4<I32>, 5>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadVector<vec4<I16>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadVector<vec4<I8>, 4>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UVEC2:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadVector<vec2<U64>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadVector<vec2<U32>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadVector<vec2<U16>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadVector<vec2<U8>, 2>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UVEC3:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadVector<vec3<U64>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadVector<vec3<U32>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadVector<vec3<U16>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadVector<vec3<U8>, 3>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UVEC4:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadVector<vec4<U64>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadVector<vec4<U32>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadVector<vec4<U16>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadVector<vec4<U8>, 4>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::VEC2:
            {
                loadVector<vec2<F32>, 2>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::VEC3:
            {
                loadVector<vec3<F32>, 3>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::VEC4:
            {
                loadVector<vec4<F32>, 4>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DVEC2:
            {
                loadVector<vec2<D64>, 2>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DVEC3:
            {
                loadVector<vec3<D64>, 3>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DVEC4:
            {
                loadVector<vec4<D64>, 4>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::IMAT2:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadMatrix<mat2<I64>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadMatrix<mat2<I32>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadMatrix<mat2<I16>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadMatrix<mat2<I8>, 2>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::IMAT3:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadMatrix<mat3<I64>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadMatrix<mat3<I32>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadMatrix<mat3<I16>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadMatrix<mat3<I8>, 3>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::IMAT4:
            {
                switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadMatrix<mat4<I64>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadMatrix<mat4<I32>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadMatrix<mat4<I16>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadMatrix<mat4<I8>, 4>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UMAT2:
            {
               switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadMatrix<mat2<U64>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadMatrix<mat2<U32>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadMatrix<mat2<U16>, 2>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadMatrix<mat2<U8>, 2>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UMAT3:
            {
               switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadMatrix<mat3<U64>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadMatrix<mat3<U32>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadMatrix<mat3<U16>, 3>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadMatrix<mat3<U8>, 3>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::UMAT4:
            {
                   switch (field._basicTypeSize) {
                    case GFX::PushConstantSize::QWORD: loadMatrix<mat4<U64>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::DWORD: loadMatrix<mat4<U32>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::WORD:  loadMatrix<mat4<U16>, 4>(entryName, field, pt); break;
                    case GFX::PushConstantSize::BYTE:  loadMatrix<mat4<U8>, 4>(entryName, field, pt); break;
                    default: DIVIDE_UNEXPECTED_CALL(); break;
                }
            } break;
            case GFX::PushConstantType::MAT2:
            {
                loadMatrix<mat2<F32>, 2>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::MAT3:
            {
                loadMatrix<mat3<F32>, 3>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::MAT4:
            {
                loadMatrix<mat4<F32>, 4>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DMAT2:
            {
                loadMatrix<mat2<D64>, 2>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DMAT3:
            {
                loadMatrix<mat3<D64>, 3>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::DMAT4:
            {
                loadMatrix<mat4<D64>, 4>(entryName, field, pt);
            } break;
            case GFX::PushConstantType::FCOLOUR3:
            {
                FColour3 data = field.get<FColour3>();
                data.set(pt.get((entryName + ".<xmlattr>.r").c_str(), data.r),
                         pt.get((entryName + ".<xmlattr>.g").c_str(), data.g),
                         pt.get((entryName + ".<xmlattr>.b").c_str(), data.b));
                field.set<FColour3>(data);
            } break;
            case GFX::PushConstantType::FCOLOUR4:
            {
                FColour4 data = field.get<FColour4>();
                data.set(pt.get((entryName + ".<xmlattr>.r").c_str(), data.r),
                         pt.get((entryName + ".<xmlattr>.g").c_str(), data.g),
                         pt.get((entryName + ".<xmlattr>.b").c_str(), data.b),
                         pt.get((entryName + ".<xmlattr>.a").c_str(), data.a));
                field.set<FColour4>(data);
            } break;
            default: break;
        }
    }
} //namespace Divide