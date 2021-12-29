# Install script for directory: G:/Divide-Framework/3rdParty/assimp/code

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/Assimp")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "G:/Divide-Framework/3rdParty/BuildSystems/assimp/lib/Debug/assimp-vc142-mtd.lib")
  elseif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "G:/Divide-Framework/3rdParty/BuildSystems/assimp/lib/Release/assimp-vc142-mt.lib")
  elseif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "G:/Divide-Framework/3rdParty/BuildSystems/assimp/lib/MinSizeRel/assimp-vc142-mt.lib")
  elseif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "G:/Divide-Framework/3rdParty/BuildSystems/assimp/lib/RelWithDebInfo/assimp-vc142-mt.lib")
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "G:/Divide-Framework/3rdParty/BuildSystems/assimp/bin/Debug/assimp-vc142-mtd.dll")
  elseif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "G:/Divide-Framework/3rdParty/BuildSystems/assimp/bin/Release/assimp-vc142-mt.dll")
  elseif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "G:/Divide-Framework/3rdParty/BuildSystems/assimp/bin/MinSizeRel/assimp-vc142-mt.dll")
  elseif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "G:/Divide-Framework/3rdParty/BuildSystems/assimp/bin/RelWithDebInfo/assimp-vc142-mt.dll")
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xassimp-devx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/assimp" TYPE FILE FILES
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/anim.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/aabb.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/ai_assert.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/camera.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/color4.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/color4.inl"
    "G:/Divide-Framework/3rdParty/BuildSystems/assimp/code/../include/assimp/config.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/ColladaMetaData.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/commonMetaData.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/defs.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/cfileio.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/light.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/material.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/material.inl"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/matrix3x3.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/matrix3x3.inl"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/matrix4x4.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/matrix4x4.inl"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/mesh.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/pbrmaterial.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/GltfMaterial.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/postprocess.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/quaternion.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/quaternion.inl"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/scene.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/metadata.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/texture.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/types.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/vector2.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/vector2.inl"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/vector3.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/vector3.inl"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/version.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/cimport.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/importerdesc.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Importer.hpp"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/DefaultLogger.hpp"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/ProgressHandler.hpp"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/IOStream.hpp"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/IOSystem.hpp"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Logger.hpp"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/LogStream.hpp"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/NullLogger.hpp"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/cexport.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Exporter.hpp"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/DefaultIOStream.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/DefaultIOSystem.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/ZipArchiveIOSystem.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/SceneCombiner.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/fast_atof.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/qnan.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/BaseImporter.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Hash.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/MemoryIOWrapper.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/ParsingUtils.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/StreamReader.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/StreamWriter.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/StringComparison.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/StringUtils.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/SGSpatialSort.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/GenericProperty.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/SpatialSort.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/SkeletonMeshBuilder.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/SmallVector.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/SmoothingGroups.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/SmoothingGroups.inl"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/StandardShapes.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/RemoveComments.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Subdivision.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Vertex.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/LineSplitter.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/TinyFormatter.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Profiler.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/LogAux.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Bitmap.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/XMLTools.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/IOStreamBuffer.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/CreateAnimMesh.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/XmlParser.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/BlobIOSystem.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/MathFunctions.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Exceptional.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/ByteSwapper.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xassimp-devx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/assimp/Compiler" TYPE FILE FILES
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Compiler/pushpack1.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Compiler/poppack1.h"
    "G:/Divide-Framework/3rdParty/assimp/code/../include/assimp/Compiler/pstdint.h"
    )
endif()

