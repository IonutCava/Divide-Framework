
set( THIRD_PARTY_LOCAL_HEADER_FILES ThirdParty/EntityComponentSystem/include/ECS/API.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Component.h
                                    ThirdParty/EntityComponentSystem/include/ECS/ComponentManager.h
                                    ThirdParty/EntityComponentSystem/include/ECS/ECS.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Engine.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Entity.h
                                    ThirdParty/EntityComponentSystem/include/ECS/EntityManager.h
                                    ThirdParty/EntityComponentSystem/include/ECS/IComponent.h
                                    ThirdParty/EntityComponentSystem/include/ECS/IEntity.h
                                    ThirdParty/EntityComponentSystem/include/ECS/ISystem.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Platform.h
                                    ThirdParty/EntityComponentSystem/include/ECS/System.h
                                    ThirdParty/EntityComponentSystem/include/ECS/SystemManager.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Event/Event.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Event/EventDelegate.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Event/EventDispatcher.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Event/EventHandler.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Event/IEvent.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Event/IEventDispatcher.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Event/IEventListener.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Log/Logger.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Log/LoggerMacro.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Log/LoggerManager.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Memory/ECSMM.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Memory/MemoryChunkAllocator.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Memory/Allocator/IAllocator.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Memory/Allocator/LinearAllocator.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Memory/Allocator/PoolAllocator.h
                                    ThirdParty/EntityComponentSystem/include/ECS/Memory/Allocator/StackAllocator.h
                                    ThirdParty/EntityComponentSystem/include/ECS/util/FamilyTypeID.h
                                    ThirdParty/EntityComponentSystem/include/ECS/util/Handle.h
                                    ThirdParty/EntityComponentSystem/include/ECS/util/Timer.h
                                    ThirdParty/ImGuiMisc/imguifilesystem/dirent_portable.h
                                    ThirdParty/ImGuiMisc/imguifilesystem/imguifilesystem.h
                                    ThirdParty/ImGuiMisc/imguifilesystem/minizip/crypt.h
                                    ThirdParty/ImGuiMisc/imguifilesystem/minizip/ioapi.h
                                    ThirdParty/ImGuiMisc/imguifilesystem/minizip/unzip.h
                                    ThirdParty/ImGuiMisc/imguifilesystem/minizip/zip.h
                                    ThirdParty/ImGuiMisc/imguistyleserializer/imguistyleserializer.h
)
set_source_files_properties(${THIRD_PARTY_LOCAL_HEADER_FILES} PROPERTIES HEADER_FILE_ONLY ON)

set( THIRD_PARTY_LOCAL_SRC_FILES ThirdParty/EntityComponentSystem/src/API.cpp
                                 ThirdParty/EntityComponentSystem/src/ComponentManager.cpp
                                 ThirdParty/EntityComponentSystem/src/Engine.cpp
                                 ThirdParty/EntityComponentSystem/src/EntityManager.cpp
                                 ThirdParty/EntityComponentSystem/src/IComponent.cpp
                                 ThirdParty/EntityComponentSystem/src/IEntity.cpp
                                 ThirdParty/EntityComponentSystem/src/ISystem.cpp
                                 ThirdParty/EntityComponentSystem/src/SystemManager.cpp
                                 ThirdParty/EntityComponentSystem/src/Event/EventHandler.cpp
                                 ThirdParty/EntityComponentSystem/src/Event/IEvent.cpp
                                 ThirdParty/EntityComponentSystem/src/Event/IEventListener.cpp
                                 ThirdParty/EntityComponentSystem/src/Log/Logger.cpp
                                 ThirdParty/EntityComponentSystem/src/Log/LoggerManager.cpp
                                 ThirdParty/EntityComponentSystem/src/Memory/ECSMM.cpp
                                 ThirdParty/EntityComponentSystem/src/Memory/Allocator/IAllocator.cpp
                                 ThirdParty/EntityComponentSystem/src/Memory/Allocator/LinearAllocator.cpp
                                 ThirdParty/EntityComponentSystem/src/Memory/Allocator/PoolAllocator.cpp
                                 ThirdParty/EntityComponentSystem/src/Memory/Allocator/StackAllocator.cpp
                                 ThirdParty/EntityComponentSystem/src/util/FamilyTypeID.cpp
                                 ThirdParty/EntityComponentSystem/src/util/Timer.cpp
)

set (THIRD_PARTY_MINIZIP ThirdParty/ImGuiMisc/imguifilesystem/minizip/ioapi.c
                         ThirdParty/ImGuiMisc/imguifilesystem/minizip/unzip.c
                         ThirdParty/ImGuiMisc/imguifilesystem/minizip/zip.c
)

set (THIRD_PARTY_IMGUIFILESYSTEM ThirdParty/ImGuiMisc/imguifilesystem/imguifilesystem.cpp
                                 ThirdParty/ImGuiMisc/imguistyleserializer/imguistyleserializer.cpp
)

if(NOT USING_MSVC)
    set( MINIZIP_COMPILE_OPTIONS "-Wno-switch-default -Wno-missing-variable-declarations -Wno-date-time" )
    set_source_files_properties(${THIRD_PARTY_MINIZIP} PROPERTIES COMPILE_FLAGS "${MINIZIP_COMPILE_OPTIONS}")

    set( IMGUIFILESYSTEM_COMPILE_OPTIONS "-Wno-unused-parameter" )
    set_source_files_properties(${THIRD_PARTY_IMGUIFILESYSTEM} PROPERTIES COMPILE_FLAGS "${IMGUIFILESYSTEM_COMPILE_OPTIONS}")
endif()

set (THIRD_PARTY_LOCAL_SRC_FILES ${THIRD_PARTY_LOCAL_SRC_FILES}
                                 ${THIRD_PARTY_MINIZIP}
                                 ${THIRD_PARTY_IMGUIFILESYSTEM}
                                  ${THIRD_PARTY_LOCAL_HEADER_FILES})

