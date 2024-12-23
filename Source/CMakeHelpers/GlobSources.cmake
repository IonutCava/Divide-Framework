set( AI_SOURCE_HEADERS AI/ActionInterface/CustomGOAP/Action.h
                       AI/ActionInterface/CustomGOAP/Node.h
                       AI/ActionInterface/CustomGOAP/Planner.h
                       AI/ActionInterface/CustomGOAP/WorldState.h
                       AI/ActionInterface/Headers/AIProcessor.h
                       AI/ActionInterface/Headers/AITeam.h
                       AI/ActionInterface/Headers/GOAPInterface.h
                       AI/PathFinding/NavMeshes/DetourTileCache/Headers/DivideTileCache.h
                       AI/PathFinding/NavMeshes/DetourTileCache/Headers/ReCastConvexHull.h
                       AI/PathFinding/NavMeshes/Headers/NavMesh.h
                       AI/PathFinding/NavMeshes/Headers/NavMeshConfig.h
                       AI/PathFinding/NavMeshes/Headers/NavMeshContext.h
                       AI/PathFinding/NavMeshes/Headers/NavMeshDebugDraw.h
                       AI/PathFinding/NavMeshes/Headers/NavMeshDefines.h
                       AI/PathFinding/NavMeshes/Headers/NavMeshLoader.h
                       AI/PathFinding/Waypoints/Headers/Waypoint.h
                       AI/PathFinding/Waypoints/Headers/WaypointGraph.h
                       AI/PathFinding/Headers/DivideCrowd.h
                       AI/PathFinding/Headers/DivideRecast.h
                       AI/Sensors/Headers/Sensor.h
                       AI/Sensors/Headers/AudioSensor.h
                       AI/Sensors/Headers/VisualSensor.h
                       AI/Headers/AIEntity.h
                       AI/Headers/AIManager.h
)

set( AI_SOURCE AI/ActionInterface/CustomGOAP/Action.cpp
               AI/ActionInterface/CustomGOAP/Node.cpp
               AI/ActionInterface/CustomGOAP/Planner.cpp
               AI/ActionInterface/CustomGOAP/WorldState.cpp
               AI/ActionInterface/AIProcessor.cpp
               AI/ActionInterface//AITeam.cpp
               AI/ActionInterface/GOAPInterface.cpp
               AI/PathFinding/NavMeshes/NavMesh.cpp
               AI/PathFinding/NavMeshes//NavMeshContext.cpp
               AI/PathFinding/NavMeshes/NavMeshDebugDraw.cpp
               AI/PathFinding/NavMeshes/NavMeshLoader.cpp
               AI/PathFinding/Waypoints/Waypoint.cpp
               AI/PathFinding/Waypoints/WaypointGraph.cpp
               AI/PathFinding/DivideCrowd.cpp
               AI/PathFinding/DivideRecast.cpp
               AI/Sensors/AudioSensor.cpp
               AI/Sensors/VisualSensor.cpp
               AI/AIEntity.cpp
               AI/AIManager.cpp
)

set( CORE_SOURCE_HEADERS Core/Debugging/Headers/DebugInterface.h
                         Core/Headers/Application.h
                         Core/Headers/Application.inl
                         Core/Headers/DisplayManager.h
                         Core/Headers/DisplayManager.inl
                         Core/Headers/ByteBuffer.inl
                         Core/Headers/ByteBuffer.h
                         Core/Headers/Configuration.h
                         Core/Headers/Console.h
                         Core/Headers/Console.inl
                         Core/Headers/ErrorCodes.h
                         Core/Headers/FrameListener.h
                         Core/Headers/GUIDWrapper.h
                         Core/Headers/Hashable.h
                         Core/Headers/ImGUICustomConfig.h
                         Core/Headers/Kernel.h
                         Core/Headers/KernelComponent.h
                         Core/Headers/LoopTimingData.h
                         Core/Headers/NonCopyable.h
                         Core/Headers/NonMovable.h
                         Core/Headers/ObjectPool.h
                         Core/Headers/ObjectPool.inl
                         Core/Headers/ParamHandler.h
                         Core/Headers/ParamHandler.inl
                         Core/Headers/PlatformContext.h
                         Core/Headers/PlatformContextComponent.h
                         Core/Headers/PoolHandle.h
                         Core/Headers/Profiler.h
                         Core/Headers/RingBuffer.h
                         Core/Headers/StringHelper.h
                         Core/Headers/StringHelper.inl
                         Core/Headers/TaskPool.h
                         Core/Headers/TaskPool.inl
                         Core/Headers/WindowManager.h
                         Core/Headers/WindowManager.inl
                         Core/Math/BoundingVolumes/Headers/BoundingBox.h
                         Core/Math/BoundingVolumes/Headers/BoundingBox.inl
                         Core/Math/BoundingVolumes/Headers/BoundingSphere.h
                         Core/Math/BoundingVolumes/Headers/BoundingSphere.inl
                         Core/Math/BoundingVolumes/Headers/OBB.h
                         Core/Math/Headers/Dimension.h
                         Core/Math/Headers/Line.h
                         Core/Math/Headers/MathHelper.h
                         Core/Math/Headers/MathHelper.inl
                         Core/Math/Headers/MathMatrices.h
                         Core/Math/Headers/MathMatrices.inl
                         Core/Math/Headers/MathUtil.h
                         Core/Math/Headers/MathVectors.h
                         Core/Math/Headers/MathVectors.inl
                         Core/Math/Headers/Plane.h
                         Core/Math/Headers/Quaternion.h
                         Core/Math/Headers/Quaternion.inl
                         Core/Math/Headers/Ray.h
                         Core/Math/Headers/Transform.h
                         Core/Math/Headers/Transform.inl
                         Core/Math/Headers/TransformInterface.h
                         Core/Math/Headers/TransformInterface.inl
                         Core/Resources/Headers/Resource.h
                         Core/Resources/Headers/ResourceCache.h
                         Core/Resources/Headers/ResourceCache.inl
                         Core/TemplateLibraries/Headers/CircularBuffer.h
                         Core/TemplateLibraries/Headers/EnumToString.h
                         Core/TemplateLibraries/Headers/HashMap.h
                         Core/TemplateLibraries/Headers/STLString.h
                         Core/TemplateLibraries/Headers/String.h
                         Core/TemplateLibraries/Headers/TemplateAllocator.h
                         Core/TemplateLibraries/Headers/Vector.h
                         Core/Time/Headers/ApplicationTimer.h
                         Core/Time/Headers/ApplicationTimer.inl
                         Core/Time/Headers/FrameRateHandler.h
                         Core/Time/Headers/FrameRateHandler.inl
                         Core/Time/Headers/ProfileTimer.h
                         Core/Time/Headers/ProfileTimer.inl
)

set( CORE_SOURCE Core/Application.cpp
                 Core/DisplayManager.cpp
                 Core/ByteBuffer.cpp
                 Core/Configuration.cpp
                 Core/Console.cpp
                 Core/ErrorCodes.cpp
                 Core/FrameListener.cpp
                 Core/GUIDWrapper.cpp
                 Core/Hashable.cpp
                 Core/Kernel.cpp
                 Core/LoopTimingData.cpp
                 Core/PlatformContext.cpp
                 Core/Profiler.cpp
                 Core/RingBuffer.cpp
                 Core/StringHelper.cpp
                 Core/TaskPool.cpp
                 Core/WindowManager.cpp
                 Core/Debugging/DebugInterface.cpp
                 Core/Math/MathClasses.cpp
                 Core/Math/MathHelper.cpp
                 Core/Math/Transform.cpp
                 Core/Math/BoundingVolumes/BoundingBox.cpp
                 Core/Math/BoundingVolumes/BoundingSphere.cpp
                 Core/Math/BoundingVolumes/OBB.cpp
                 Core/Resources/Resource.cpp
                 Core/Resources/ResourceCache.cpp
                 Core/Time/ApplicationTimer.cpp
                 Core/Time/FrameRateHandler.cpp
                 Core/Time/ProfileTimer.cpp
)

set ( DYNAMICS_SOURCE_HEADERS Dynamics/Entities/Particles/Headers/ParticleData.h
                              Dynamics/Entities/Particles/Headers/ParticleEmitter.h
                              Dynamics/Entities/Particles/Headers/ParticleGenerator.h
                              Dynamics/Entities/Particles/Headers/ParticleSource.h
                              Dynamics/Entities/Particles/Headers/ParticleUpdater.h
                              Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleBoxGenerator.h
                              Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleColourGenerator.h
                              Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleRoundGenerator.h
                              Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleSphereVelocityGenerator.h
                              Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleTimeGenerator.h
                              Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleVelocityFromPositionGenerator.h
                              Dynamics/Entities/Particles/ConcreteGenerators/Headers/ParticleVelocityGenerator.h
                              Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleAttractorUpdater.h
                              Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleBasicColourUpdater.h
                              Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleBasicTimeUpdater.h
                              Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleEulerUpdater.h
                              Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleFloorUpdater.h
                              Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleFountainUpdater.h
                              Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticlePositionColourUpdater.h
                              Dynamics/Entities/Particles/ConcreteUpdaters/Headers/ParticleVelocityColourUpdater.h
                              Dynamics/Entities/Units/Headers/Character.h
                              Dynamics/Entities/Units/Headers/NPC.h
                              Dynamics/Entities/Units/Headers/Player.h
                              Dynamics/Entities/Units/Headers/Unit.h
)

set( DYNAMICS_SOURCE Dynamics/Entities/Particles/ParticleData.cpp
                     Dynamics/Entities/Particles/ParticleEmitter.cpp
                     Dynamics/Entities/Particles/ParticleGenerator.cpp
                     Dynamics/Entities/Particles/ParticleSource.cpp
                     Dynamics/Entities/Particles/ConcreteGenerators/ParticleBoxGenerator.cpp
                     Dynamics/Entities/Particles/ConcreteGenerators/ParticleColourGenerator.cpp
                     Dynamics/Entities/Particles/ConcreteGenerators/ParticleRoundGenerator.cpp
                     Dynamics/Entities/Particles/ConcreteGenerators/ParticleSphereVelocityGenerator.cpp
                     Dynamics/Entities/Particles/ConcreteGenerators/ParticleTimeGenerator.cpp
                     Dynamics/Entities/Particles/ConcreteGenerators/ParticleVelocityFromPositionGenerator.cpp
                     Dynamics/Entities/Particles/ConcreteGenerators/ParticleVelocityGenerator.cpp
                     Dynamics/Entities/Particles/ConcreteUpdaters/ParticleAttractorUpdater.cpp
                     Dynamics/Entities/Particles/ConcreteUpdaters/ParticleBasicColourUpdater.cpp
                     Dynamics/Entities/Particles/ConcreteUpdaters/ParticleBasicTimeUpdater.cpp
                     Dynamics/Entities/Particles/ConcreteUpdaters/ParticleEulerUpdater.cpp
                     Dynamics/Entities/Particles/ConcreteUpdaters/ParticleFloorUpdater.cpp
                     Dynamics/Entities/Particles/ConcreteUpdaters/ParticleFountainUpdater.cpp
                     Dynamics/Entities/Particles/ConcreteUpdaters/ParticlePositionColourUpdater.cpp
                     Dynamics/Entities/Particles/ConcreteUpdaters/ParticleVelocityColourUpdater.cpp
                     Dynamics/Entities/Units/Character.cpp
                     Dynamics/Entities/Units/NPC.cpp
                     Dynamics/Entities/Units/Player.cpp
                     Dynamics/Entities/Units/Unit.cpp
)

set( ECS_SOURCE_HEADERS ECS/Components/Headers/AnimationComponent.h
                        ECS/Components/Headers/BoundsComponent.h
                        ECS/Components/Headers/DirectionalLightComponent.h
                        ECS/Components/Headers/EditorComponent.h
                        ECS/Components/Headers/EditorComponent.inl
                        ECS/Components/Headers/EnvironmentProbeComponent.h
                        ECS/Components/Headers/IKComponent.h
                        ECS/Components/Headers/NavigationComponent.h
                        ECS/Components/Headers/NetworkingComponent.h
                        ECS/Components/Headers/PointLightComponent.h
                        ECS/Components/Headers/RagdollComponent.h
                        ECS/Components/Headers/RenderingComponent.h
                        ECS/Components/Headers/RigidBodyComponent.h
                        ECS/Components/Headers/ScriptComponent.h
                        ECS/Components/Headers/SelectionComponent.h
                        ECS/Components/Headers/SGNComponent.h
                        ECS/Components/Headers/SGNComponent.inl
                        ECS/Components/Headers/SpotLightComponent.h
                        ECS/Components/Headers/TransformComponent.h
                        ECS/Components/Headers/UnitComponent.h
                        ECS/Systems/Headers/AnimationSystem.h
                        ECS/Systems/Headers/BoundsSystem.h
                        ECS/Systems/Headers/DirectionalLightSystem.h
                        ECS/Systems/Headers/ECSManager.h
                        ECS/Systems/Headers/ECSSystem.h
                        ECS/Systems/Headers/ECSSystem.inl
                        ECS/Systems/Headers/EnvironmentProbeSystem.h
                        ECS/Systems/Headers/NavigationSystem.h
                        ECS/Systems/Headers/PointLightSystem.h
                        ECS/Systems/Headers/RenderingSystem.h
                        ECS/Systems/Headers/RigidBodySystem.h
                        ECS/Systems/Headers/SelectionSystem.h
                        ECS/Systems/Headers/SpotLightSystem.h
                        ECS/Systems/Headers/TransformSystem.h
)

set( ECS_SOURCE ECS/Components/AnimationComponent.cpp
                ECS/Components/BoundsComponent.cpp
                ECS/Components/DirectionalLightComponent.cpp
                ECS/Components/EditorComponent.cpp
                ECS/Components/EnvironmentProbeComponent.cpp
                ECS/Components/IKComponent.cpp
                ECS/Components/NavigationComponent.cpp
                ECS/Components/NetworkingComponent.cpp
                ECS/Components/PointLightComponent.cpp
                ECS/Components/RagdollComponent.cpp
                ECS/Components/RenderingComponent.cpp
                ECS/Components/RenderingComponentState.cpp
                ECS/Components/RigidBodyComponent.cpp
                ECS/Components/ScriptComponent.cpp
                ECS/Components/SelectionComponent.cpp
                ECS/Components/SGNComponent.cpp
                ECS/Components/SpotLightComponent.cpp
                ECS/Components/TransformComponent.cpp
                ECS/Components/UnitComponent.cpp
                ECS/Systems/AnimationSystem.cpp
                ECS/Systems/BoundsSystem.cpp
                ECS/Systems/DirectionalLightSystem.cpp
                ECS/Systems/ECSManager.cpp
                ECS/Systems/EnvironmentProbeSystem.cpp
                ECS/Systems/PointLightSystem.cpp
                ECS/Systems/RenderingSystem.cpp
                ECS/Systems/RigidBodySystem.cpp
                ECS/Systems/SelectionSystem.cpp
                ECS/Systems/SpotLightSystem.cpp
                ECS/Systems/TransformSystem.cpp
)

set( EDITOR_SOURCE_HEADERS Editor/Headers/Editor.h
                           Editor/Headers/Editor.inl
                           Editor/Headers/Sample.h
                           Editor/Headers/UndoManager.h
                           Editor/Headers/Utils.h
                           Editor/Headers/Utils.inl
                           Editor/Widgets/DockedWindows/Headers/ContentExplorerWindow.h
                           Editor/Widgets/DockedWindows/Headers/NodePreviewWindow.h
                           Editor/Widgets/DockedWindows/Headers/OutputWindow.h
                           Editor/Widgets/DockedWindows/Headers/PostFXWindow.h
                           Editor/Widgets/DockedWindows/Headers/PropertyWindow.h
                           Editor/Widgets/DockedWindows/Headers/SceneViewWindow.h
                           Editor/Widgets/DockedWindows/Headers/SolutionExplorerWindow.h
                           Editor/Widgets/Headers/DockedWindow.h
                           Editor/Widgets/Headers/EditorOptionsWindow.h
                           Editor/Widgets/Headers/Gizmo.h
                           Editor/Widgets/Headers/ImGuiExtensions.h
                           Editor/Widgets/Headers/MenuBar.h
                           Editor/Widgets/Headers/StatusBar.h
)

set( EDITOR_SOURCE Editor/Editor.cpp
                   Editor/UndoManager.cpp
                   Editor/Utils.cpp
                   Editor/Widgets/DockedWindow.cpp
                   Editor/Widgets/EditorOptionsWindow.cpp
                   Editor/Widgets/Gizmo.cpp
                   Editor/Widgets/ImGuiExtensions.cpp
                   Editor/Widgets/MenuBar.cpp
                   Editor/Widgets/StatusBar.cpp
                   Editor/Widgets/DockedWindows/ContentExplorerWindow.cpp
                   Editor/Widgets/DockedWindows/NodePreviewWindow.cpp
                   Editor/Widgets/DockedWindows/OutputWindow.cpp
                   Editor/Widgets/DockedWindows/PostFXWindow.cpp
                   Editor/Widgets/DockedWindows/PropertyWindow.cpp
                   Editor/Widgets/DockedWindows/SceneViewWindow.cpp
                   Editor/Widgets/DockedWindows/SolutionExplorerWindow.cpp
)

set( ENVIRONMENT_SOURCE_HEADERS Environment/Sky/Headers/Sky.h
                                Environment/Sky/Headers/Sun.h
                                Environment/Terrain/Headers/InfinitePlane.h
                                Environment/Terrain/Headers/Terrain.h
                                Environment/Terrain/Headers/TerrainChunk.h
                                Environment/Terrain/Headers/TerrainDescriptor.h
                                Environment/Terrain/Headers/TerrainDescriptor.inl
                                Environment/Terrain/Headers/TileRing.h
                                Environment/Terrain/Quadtree/Headers/Quadtree.h
                                Environment/Terrain/Quadtree/Headers/QuadtreeNode.h
                                Environment/Vegetation/Headers/Vegetation.h
                                Environment/Vegetation/Headers/VegetationDescriptor.h
                                Environment/Vegetation/Headers/VegetationDescriptor.inl
                                Environment/Water/Headers/Water.h
)

set( ENVIRONMENT_SOURCE Environment/Sky/Sky.cpp
                        Environment/Sky/Sun.cpp
                        Environment/Terrain/InfinitePlane.cpp
                        Environment/Terrain/Terrain.cpp
                        Environment/Terrain/TerrainChunk.cpp
                        Environment/Terrain/TerrainDescriptor.cpp
                        Environment/Terrain/TileRing.cpp
                        Environment/Terrain/Quadtree/Quadtree.cpp
                        Environment/Terrain/Quadtree/QuadtreeNode.cpp
                        Environment/Vegetation/Vegetation.cpp
                        Environment/Water/Water.cpp

)

set( GEOMETRY_SOURCE_HEADERS Geometry/Animations/Headers/AnimationEvaluator.h
                             Geometry/Animations/Headers/AnimationEvaluator.inl
                             Geometry/Animations/Headers/AnimationUtils.h
                             Geometry/Animations/Headers/Bone.h
                             Geometry/Animations/Headers/SceneAnimator.h
                             Geometry/Animations/Headers/SceneAnimator.inl
                             Geometry/Importer/Headers/DVDConverter.h
                             Geometry/Importer/Headers/MeshImporter.h
                             Geometry/Material/Headers/Material.h
                             Geometry/Material/Headers/Material.inl
                             Geometry/Material/Headers/MaterialEnums.h
                             Geometry/Material/Headers/ShaderComputeQueue.h
                             Geometry/Material/Headers/ShaderProgramInfo.h
                             Geometry/Shapes/Headers/Mesh.h
                             Geometry/Shapes/Headers/Object3D.h
                             Geometry/Shapes/Headers/SubMesh.h
                             Geometry/Shapes/Predefined/Headers/Box3D.h
                             Geometry/Shapes/Predefined/Headers/Quad3D.h
                             Geometry/Shapes/Predefined/Headers/Sphere3D.h
)

set( GEOMETRY_SOURCE Geometry/Animations/Bone.cpp
                     Geometry/Animations/AnimationEvaluator.cpp
                     Geometry/Animations/AnimationUtils.cpp
                     Geometry/Animations/SceneAnimator.cpp
                     Geometry/Importer/DVDConverter.cpp
                     Geometry/Importer/MeshImporter.cpp
                     Geometry/Material/Material.cpp
                     Geometry/Material/MaterialProperties.cpp
                     Geometry/Material/ShaderComputeQueue.cpp
                     Geometry/Material/ShaderProgramInfo.cpp
                     Geometry/Shapes/Mesh.cpp
                     Geometry/Shapes/Object3D.cpp
                     Geometry/Shapes/SubMesh.cpp
                     Geometry/Shapes/Predefined/Box3D.cpp
                     Geometry/Shapes/Predefined/Quad3D.cpp
                     Geometry/Shapes/Predefined/Sphere3D.cpp
)

set( GRAPHS_SOURCE_HEADERS Graphs/Headers/IntersectionRecord.h
                           Graphs/Headers/SceneGraph.h
                           Graphs/Headers/SceneGraphNode.h
                           Graphs/Headers/SceneGraphNode.inl
                           Graphs/Headers/SceneNode.h
                           Graphs/Headers/SceneNodeFwd.h
                           Graphs/Headers/SceneNodeRenderState.h
                           Graphs/Headers/SGNRelationshipCache.h
)

set( GRAPHS_SOURCE Graphs/IntersectionRecord.cpp
                   Graphs/SceneGraph.cpp
                   Graphs/SceneGraphNode.cpp
                   Graphs/SceneNode.cpp
                   Graphs/SceneNodeRenderState.cpp
                   Graphs/SGNRelationshipCache.cpp
)

set( GUI_SOURCE_HEADERS GUI/CEGUIAddons/Headers/CEGUIFormattedListBox.h
                        GUI/CEGUIAddons/Headers/CEGUIInput.h
                        GUI/CEGUIAddons/Renderer/Headers/CEGUIRenderer.h
                        GUI/CEGUIAddons/Renderer/Headers/CEGUIRenderer.inl
                        GUI/CEGUIAddons/Renderer/Headers/DVDGeometryBuffer.h
                        GUI/CEGUIAddons/Renderer/Headers/DVDGeometryBuffer.inl
                        GUI/CEGUIAddons/Renderer/Headers/DVDTexture.h
                        GUI/CEGUIAddons/Renderer/Headers/DVDTexture.inl
                        GUI/CEGUIAddons/Renderer/Headers/DVDTextureTarget.h
                        GUI/CEGUIAddons/Renderer/Headers/DVDTextureTarget.inl
                        GUI/Headers/GUI.h
                        GUI/Headers/GUIButton.h
                        GUI/Headers/GUIConsole.h
                        GUI/Headers/GUIConsoleCommandParser.h
                        GUI/Headers/GUIElement.h
                        GUI/Headers/GUIFlash.h
                        GUI/Headers/GUIInterface.h
                        GUI/Headers/GUIMessageBox.h
                        GUI/Headers/GUISplash.h
                        GUI/Headers/GUIText.h
                        GUI/Headers/SceneGUIElements.h
)

set( GUI_SOURCE GUI/GUI.cpp
                GUI/GUIButton.cpp
                GUI/GUIConsole.cpp
                GUI/GUIConsoleCommandParser.cpp
                GUI/GUIElement.cpp
                GUI/GuiFlash.cpp
                GUI/GUIInterface.cpp
                GUI/GUIMessageBox.cpp
                GUI/GUISplash.cpp
                GUI/GUIText.cpp
                GUI/SceneGUIElements.cpp
                GUI/CEGUIAddons/CEGUIFormattedListBox.cpp
                GUI/CEGUIAddons/CEGUIInput.cpp
                GUI/CEGUIAddons/Renderer/CEGUIRenderer.cpp
                GUI/CEGUIAddons/Renderer/DVDGeometryBuffer.cpp
                GUI/CEGUIAddons/Renderer/DVDTexture.cpp
                GUI/CEGUIAddons/Renderer/DVDTextureTarget.cpp
)

set( MANAGERS_SOURCE_HEADERS Managers/Headers/FrameListenerManager.h
                             Managers/Headers/RenderPassManager.h
                             Managers/Headers/ProjectManager.h
)

set( MANAGERS_SOURCE Managers/FrameListenerManager.cpp
                     Managers/RenderPassManager.cpp
                     Managers/ProjectManager.cpp
)

set( NETWORKING_SOURCE_HEADERS Networking/Headers/NetworkClientInterface.h
                               Networking/Headers/NetworkClientImpl.h
                               Networking/Headers/OPCodesTpl.h
                               Networking/Headers/TCPUDPInterface.h
                               Networking/Headers/TCPUDPImpl.h
                               Networking/Headers/Utils.h
                               Networking/Headers/WorldPacket.h
                               Networking/Headers/OPCodesImpl.h
                               Networking/Headers/Patch.h
                               Networking/Headers/Client.h
                               Networking/Headers/Server.h
)

set( NETWORKING_SOURCE Networking/NetworkClientInterface.cpp
                       Networking/NetworkClientImpl.cpp
                       Networking/TCPUDPInterface.cpp
                       Networking/TCPUDPImpl.cpp
                       Networking/WorldPacket.cpp
                       Networking/Patch.cpp
                       Networking/Client.cpp
                       Networking/Server.cpp
)

set( PHYSICS_SOURCE_HEADERS Physics/Headers/PhysicsAPIWrapper.h
                            Physics/Headers/PhysicsAsset.h
                            Physics/Headers/PhysicsSceneInterface.h
                            Physics/Headers/PXDevice.h
)

set( PHYSICS_SOURCE Physics/PhysicsAPIWrapper.cpp
                    Physics/PhysicsAsset.cpp
                    Physics/PXDevice.cpp
                    
)

set ( PHYSICS_NONE_SOURCE Physics/None/None.cpp
)

set ( PHYSICS_NONE_SOURCE_HEADERS Physics/None/Headers/None.h
)

set ( JOLT_SOURCE Physics/Jolt/Jolt.cpp
)

set ( JOLT_SOURCE_HEADERS Physics/Jolt/Headers/Jolt.h
)

set( PHYSX_SOURCE "")
set( PHYSX_SOURCE_HEADERS "")

if(NOT MAC_OS_BUILD)
    set( PHYSX_SOURCE Physics/PhysX/PhysX.cpp
                      Physics/PhysX/PhysXActor.cpp
                      Physics/PhysX/PhysXSceneInterface.cpp
                      Physics/PhysX/pxShapeScaling.cpp
    )

    set( PHYSX_SOURCE_HEADERS Physics/PhysX/Headers/PhysX.h
                              Physics/PhysX/Headers/PhysXActor.h
                              Physics/PhysX/Headers/PhysXSceneInterface.h
                              Physics/PhysX/Headers/pxShapeScaling.h
    )
endif()

set( PHYSICS_SOURCE ${PHYSICS_SOURCE}
                    ${PHYSICS_NONE_SOURCE}
                    ${JOLT_SOURCE}
                    ${PHYSX_SOURCE}
)

set( PHYSICS_SOURCE_HEADERS ${PHYSICS_SOURCE_HEADERS}
                            ${PHYSICS_NONE_SOURCE_HEADERS}
                            ${JOLT_SOURCE_HEADERS}
                            ${PHYSX_SOURCE_HEADERS}
)

set( PLATFORM_SOURCE_HEADERS Platform/Audio/fmod/Headers/FmodWrapper.h
                             Platform/Audio/Headers/AudioAPIWrapper.h
                             Platform/Audio/Headers/AudioDescriptor.h
                             Platform/Audio/Headers/SFXDevice.h
                             Platform/Audio/openAl/Headers/ALWrapper.h
                             Platform/Audio/sdl_mixer/Headers/SDLWrapper.h
                             Platform/File/Headers/FileManagement.h
                             Platform/File/Headers/FileManagement.inl
                             Platform/File/Headers/FileUpdateMonitor.h
                             Platform/File/Headers/FileWatcherManager.h
                             Platform/File/Headers/ResourcePath.h
                             Platform/Headers/ConditionalWait.h
                             Platform/Headers/DisplayWindow.h
                             Platform/Headers/DisplayWindow.inl
                             Platform/Headers/PlatformDataTypes.h
                             Platform/Headers/PlatformDefines.h
                             Platform/Headers/PlatformDefinesApple.h
                             Platform/Headers/PlatformDefinesOS.h
                             Platform/Headers/PlatformDefinesUnix.h
                             Platform/Headers/PlatformDefinesWindows.h
                             Platform/Headers/PlatformRuntime.h
                             Platform/Headers/PlatformRuntime.inl
                             Platform/Headers/SDLEventListener.h
                             Platform/Headers/SDLEventManager.h
                             Platform/Input/Headers/AutoKeyRepeat.h
                             Platform/Input/Headers/Input.h
                             Platform/Input/Headers/InputAggregatorInterface.h
                             Platform/Input/Headers/InputHandler.h
                             Platform/Input/Headers/InputVariables.h
                             Platform/Threading/Headers/SharedMutex.h
                             Platform/Threading/Headers/Task.h
                             Platform/Threading/Headers/Task.inl
                             Platform/Threading/Headers/TaskGPUSync.h
                             Platform/Video/Buffers/Headers/BufferRange.h
                             Platform/Video/Buffers/Headers/BufferRange.inl
                             Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h
                             Platform/Video/Buffers/RenderTarget/Headers/RTAttachment.h
                             Platform/Video/Buffers/RenderTarget/Headers/RTDrawDescriptor.h
                             Platform/Video/Buffers/RenderTarget/Headers/RTDrawDescriptor.inl
                             Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h
                             Platform/Video/Buffers/VertexBuffer/GenericBuffer/Headers/GenericVertexData.h
                             Platform/Video/Buffers/VertexBuffer/Headers/BufferLocks.h
                             Platform/Video/Buffers/VertexBuffer/Headers/BufferParams.h
                             Platform/Video/Buffers/VertexBuffer/Headers/VertexBuffer.h
                             Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h
                             Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.inl
                             Platform/Video/GLIM/Declarations.h
                             Platform/Video/GLIM/glim.h
                             Platform/Video/GLIM/glimBatch.h
                             Platform/Video/GLIM/glimBatchData.h
                             Platform/Video/Headers/AttributeDescriptor.h
                             Platform/Video/Headers/BlendingProperties.h
                             Platform/Video/Headers/ClipPlanes.h
                             Platform/Video/Headers/CommandTypes.h
                             Platform/Video/Headers/CommandBuffer.h
                             Platform/Video/Headers/CommandBuffer.inl
                             Platform/Video/Headers/CommandBufferPool.h
                             Platform/Video/Headers/CommandBufferPool.inl
                             Platform/Video/Headers/Commands.h
                             Platform/Video/Headers/Commands.inl
                             Platform/Video/Headers/DescriptorSets.h
                             Platform/Video/Headers/DescriptorSets.inl
                             Platform/Video/Headers/DescriptorSetsFwd.h
                             Platform/Video/Headers/fontstash.h
                             Platform/Video/Headers/GenericDrawCommand.h
                             Platform/Video/Headers/GenericDrawCommand.inl
                             Platform/Video/Headers/GFXDevice.h
                             Platform/Video/Headers/GFXDevice.inl
                             Platform/Video/Headers/GFXRTPool.h
                             Platform/Video/Headers/GFXShaderData.h
                             Platform/Video/Headers/GFXShaderData.inl
                             Platform/Video/Headers/GraphicsResource.h
                             Platform/Video/Headers/HardwareQuery.h
                             Platform/Video/Headers/IMPrimitive.h
                             Platform/Video/Headers/IMPrimitiveDescriptors.h
                             Platform/Video/Headers/LockManager.h
                             Platform/Video/Headers/Pipeline.h
                             Platform/Video/Headers/PushConstants.h
                             Platform/Video/Headers/PushConstants.inl
                             Platform/Video/Headers/RenderAPIEnums.h
                             Platform/Video/Headers/RenderAPIWrapper.h
                             Platform/Video/Headers/RenderPackage.h
                             Platform/Video/Headers/RenderStagePass.h
                             Platform/Video/Headers/RenderStateBlock.h
                             Platform/Video/Headers/TextureData.h
                             Platform/Video/RenderBackend/None/Headers/NonePlaceholderObjects.h
                             Platform/Video/RenderBackend/None/Headers/NoneWrapper.h
                             Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glBufferImpl.h
                             Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glFramebuffer.h
                             Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glGenericVertexData.h
                             Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glMemoryManager.h
                             Platform/Video/RenderBackend/OpenGL/Buffers/Headers/glShaderBuffer.h
                             Platform/Video/RenderBackend/OpenGL/Headers/glHardwareQuery.h
                             Platform/Video/RenderBackend/OpenGL/Headers/glLockManager.h
                             Platform/Video/RenderBackend/OpenGL/Headers/glResources.h
                             Platform/Video/RenderBackend/OpenGL/Headers/glResources.inl
                             Platform/Video/RenderBackend/OpenGL/Headers/glStateTracker.h
                             Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h
                             Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShader.h
                             Platform/Video/RenderBackend/OpenGL/Shaders/Headers/glShaderProgram.h
                             Platform/Video/RenderBackend/OpenGL/Textures/Headers/glSamplerObject.h
                             Platform/Video/RenderBackend/OpenGL/Textures/Headers/glTexture.h
                             Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkBufferImpl.h
                             Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkGenericVertexData.h
                             Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkRenderTarget.h
                             Platform/Video/RenderBackend/Vulkan/Buffers/Headers/vkShaderBuffer.h
                             Platform/Video/RenderBackend/Vulkan/Headers/vkDescriptors.h
                             Platform/Video/RenderBackend/Vulkan/Headers/vkDevice.h
                             Platform/Video/RenderBackend/Vulkan/Headers/vkIMPrimitive.h
                             Platform/Video/RenderBackend/Vulkan/Headers/vkInitializers.h
                             Platform/Video/RenderBackend/Vulkan/Headers/vkMemAllocatorInclude.h
                             Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h
                             Platform/Video/RenderBackend/Vulkan/Headers/vkSwapChain.h
                             Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h
                             Platform/Video/RenderBackend/Vulkan/Shaders/Headers/vkShaderProgram.h
                             Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkSamplerObject.h
                             Platform/Video/RenderBackend/Vulkan/Textures/Headers/vkTexture.h
                             Platform/Video/RenderBackend/Vulkan/Vulkan-Descriptor-Allocator/descriptor_allocator.h
                             Platform/Video/Shaders/glsw/Headers/bstrlib.h
                             Platform/Video/Shaders/glsw/Headers/glsw.h
                             Platform/Video/Shaders/Headers/GLSLToSPIRV.h
                             Platform/Video/Shaders/Headers/ShaderDataUploader.h
                             Platform/Video/Shaders/Headers/ShaderProgram.h
                             Platform/Video/Shaders/Headers/ShaderProgramFwd.h
                             Platform/Video/Shaders/Headers/ShaderProgramFwd.inl
                             Platform/Video/Textures/Headers/SamplerDescriptor.h
                             Platform/Video/Textures/Headers/SamplerDescriptor.inl
                             Platform/Video/Textures/Headers/Texture.h
                             Platform/Video/Textures/Headers/TextureDescriptor.h
                             Platform/Video/Textures/Headers/TextureDescriptor.inl
)

set( PLATFORM_SOURCE Platform/ConditionalWait.cpp
                     Platform/DisplayWindow.cpp
                     Platform/PlatformDataTypes.cpp
                     Platform/PlatformDefines.cpp
                     Platform/PlatformDefinesApple.cpp
                     Platform/PlatformDefinesUnix.cpp
                     Platform/PlatformDefinesWindows.cpp
                     Platform/PlatformRuntime.cpp
                     Platform/SDLEventListener.cpp
                     Platform/SDLEventManager.cpp
                     Platform/Audio/AudioAPIWrapper.cpp
                     Platform/Audio/SFXDevice.cpp
                     Platform/Audio/openAl/ALWrapper.cpp
                     Platform/Audio/sdl_mixer/SDLWrapper.cpp
                     Platform/File/FileManagementFunctions.cpp
                     Platform/File/FileManagementPaths.cpp
                     Platform/File/FileUpdateMonitor.cpp
                     Platform/File/FileWatcherManager.cpp
                     Platform/File/ResourcePath.cpp
                     Platform/Input/AutoKeyRepeat.cpp
                     Platform/Input/Input.cpp
                     Platform/Input/InputAggregatorInterface.cpp
                     Platform/Input/InputHandler.cpp
                     Platform/Threading/Task.cpp
                     Platform/Video/AttributeDescriptor.cpp
                     Platform/Video/BlendingProperties.cpp
                     Platform/Video/CommandBuffer.cpp
                     Platform/Video/CommandBufferPool.cpp
                     Platform/Video/Commands.cpp
                     Platform/Video/DescriptorSets.cpp
                     Platform/Video/GenericDrawCommand.cpp
                     Platform/Video/GFXDevice.cpp
                     Platform/Video/GFXRTPool.cpp
                     Platform/Video/GFXShaderData.cpp
                     Platform/Video/GraphicsResource.cpp
                     Platform/Video/IMPrimitive.cpp
                     Platform/Video/LockManager.cpp
                     Platform/Video/Pipeline.cpp
                     Platform/Video/PushConstants.cpp
                     Platform/Video/RenderPackage.cpp
                     Platform/Video/RenderStagePass.cpp
                     Platform/Video/RenderStateBlock.cpp
                     Platform/Video/Buffers/RenderTarget/RenderTarget.cpp
                     Platform/Video/Buffers/RenderTarget/RTAttachment.cpp
                     Platform/Video/Buffers/RenderTarget/RTDrawDescriptor.cpp
                     Platform/Video/Buffers/ShaderBuffer/ShaderBuffer.cpp
                     Platform/Video/Buffers/VertexBuffer/BufferLocks.cpp
                     Platform/Video/Buffers/VertexBuffer/VertexBuffer.cpp
                     Platform/Video/Buffers/VertexBuffer/VertexDataInterface.cpp
                     Platform/Video/Buffers/VertexBuffer/GenericBuffer/GenericVertexData.cpp
                     Platform/Video/GLIM/glimBatch.cpp
                     Platform/Video/GLIM/glimBatchAttributes.cpp
                     Platform/Video/GLIM/glimBatchData.cpp
                     Platform/Video/RenderBackend/None/NoneWrapper.cpp
                     Platform/Video/RenderBackend/OpenGL/glHardwareQuery.cpp
                     Platform/Video/RenderBackend/OpenGL/glLockManager.cpp
                     Platform/Video/RenderBackend/OpenGL/glResources.cpp
                     Platform/Video/RenderBackend/OpenGL/glStateTracker.cpp
                     Platform/Video/RenderBackend/OpenGL/GLWrapper.cpp
                     Platform/Video/RenderBackend/OpenGL/Buffers/glBufferImpl.cpp
                     Platform/Video/RenderBackend/OpenGL/Buffers/glFramebuffer.cpp
                     Platform/Video/RenderBackend/OpenGL/Buffers/glGenericVertexData.cpp
                     Platform/Video/RenderBackend/OpenGL/Buffers/glMemoryManager.cpp
                     Platform/Video/RenderBackend/OpenGL/Buffers/glShaderBuffer.cpp
                     Platform/Video/RenderBackend/OpenGL/Shaders/glShader.cpp
                     Platform/Video/RenderBackend/OpenGL/Shaders/glShaderProgram.cpp
                     Platform/Video/RenderBackend/OpenGL/Textures/glSamplerOject.cpp
                     Platform/Video/RenderBackend/OpenGL/Textures/glTexture.cpp
                     Platform/Video/RenderBackend/Vulkan/vkDescriptors.cpp
                     Platform/Video/RenderBackend/Vulkan/vkDevice.cpp
                     Platform/Video/RenderBackend/Vulkan/vkResources.cpp
                     Platform/Video/RenderBackend/Vulkan/vkSwapChain.cpp
                     Platform/Video/RenderBackend/Vulkan/VKWrapper.cpp
                     Platform/Video/RenderBackend/Vulkan/Buffers/vkBufferImpl.cpp
                     Platform/Video/RenderBackend/Vulkan/Buffers/vkGenericVertexData.cpp
                     Platform/Video/RenderBackend/Vulkan/Buffers/vkRenderTarget.cpp
                     Platform/Video/RenderBackend/Vulkan/Buffers/vkShaderBuffer.cpp
                     Platform/Video/RenderBackend/Vulkan/Shaders/vkShaderProgram.cpp
                     Platform/Video/RenderBackend/Vulkan/Textures/vkSamplerObject.cpp
                     Platform/Video/RenderBackend/Vulkan/Textures/vkTexture.cpp
                     Platform/Video/RenderBackend/Vulkan/Vulkan-Descriptor-Allocator/descriptor_allocator.cpp
                     Platform/Video/Shaders/GLSLToSPIRV.cpp
                     Platform/Video/Shaders/ShaderDataUploader.cpp
                     Platform/Video/Shaders/ShaderProgram.cpp
                     Platform/Video/Shaders/glsw/bstrlib.c
                     Platform/Video/Shaders/glsw/glsw.c
                     Platform/Video/Textures/SamplerDescriptor.cpp
                     Platform/Video/Textures/Texture.cpp
                     Platform/Video/Textures/TextureDescriptor.cpp
)
set_source_files_properties(${PLATFORM_SOURCE} PROPERTIES LANGUAGE CXX )
set_source_files_properties(${PLATFORM_SOURCE_HEADERS} PROPERTIES LANGUAGE CXX )

set( RENDERING_SOURCE_HEADERS Rendering/Camera/Headers/Camera.h
                              Rendering/Camera/Headers/Camera.inl
                              Rendering/Camera/Headers/CameraSnapshot.h
                              Rendering/Camera/Headers/Frustum.h
                              Rendering/Headers/ClipRegion.h
                              Rendering/Headers/Renderer.h
                              Rendering/Lighting/Headers/Light.h
                              Rendering/Lighting/Headers/Light.inl
                              Rendering/Lighting/Headers/LightPool.h
                              Rendering/Lighting/ShadowMapping/Headers/CascadedShadowMapsGenerator.h
                              Rendering/Lighting/ShadowMapping/Headers/CubeShadowMapGenerator.h
                              Rendering/Lighting/ShadowMapping/Headers/ShadowMap.h
                              Rendering/Lighting/ShadowMapping/Headers/SingleShadowMapGenerator.h
                              Rendering/PostFX/CustomOperators/Headers/BloomPreRenderOperator.h
                              Rendering/PostFX/CustomOperators/Headers/DoFPreRenderOperator.h
                              Rendering/PostFX/CustomOperators/Headers/MotionBlurPreRenderOperator.h
                              Rendering/PostFX/CustomOperators/Headers/PostAAPreRenderOperator.h
                              Rendering/PostFX/CustomOperators/Headers/SSAOPreRenderOperator.h
                              Rendering/PostFX/CustomOperators/Headers/SSRPreRenderOperator.h
                              Rendering/PostFX/Headers/PostFX.h
                              Rendering/PostFX/Headers/PreRenderBatch.h
                              Rendering/PostFX/Headers/PreRenderBatch.inl
                              Rendering/PostFX/Headers/PreRenderOperator.h
                              Rendering/RenderPass/Headers/NodeBufferedData.h
                              Rendering/RenderPass/Headers/RenderBin.h
                              Rendering/RenderPass/Headers/RenderPass.h
                              Rendering/RenderPass/Headers/RenderPassCuller.h
                              Rendering/RenderPass/Headers/RenderPassExecutor.h
                              Rendering/RenderPass/Headers/RenderQueue.h
)

set( RENDERING_SOURCE Rendering/Renderer.cpp
                      Rendering/Camera/Camera.cpp
                      Rendering/Camera/Frustum.cpp
                      Rendering/Lighting/Light.cpp
                      Rendering/Lighting/LightPool.cpp
                      Rendering/Lighting/ShadowMapping/CascadedShadowMapsGenerator.cpp
                      Rendering/Lighting/ShadowMapping/CubeShadowMapGenerator.cpp
                      Rendering/Lighting/ShadowMapping/ShadowMap.cpp
                      Rendering/Lighting/ShadowMapping/SingleShadowMapGenerator.cpp
                      Rendering/PostFX/PostFX.cpp
                      Rendering/PostFX/PreRenderBatch.cpp
                      Rendering/PostFX/PreRenderOperator.cpp
                      Rendering/PostFX/CustomOperators/BloomPreRenderOperator.cpp
                      Rendering/PostFX/CustomOperators/DoFPreRenderOperator.cpp
                      Rendering/PostFX/CustomOperators/MotionBlurPreRenderOperator.cpp
                      Rendering/PostFX/CustomOperators/PostAAPreRenderOperator.cpp
                      Rendering/PostFX/CustomOperators/SSAOPreRenderOperator.cpp
                      Rendering/PostFX/CustomOperators/SSRPreRenderOperator.cpp
                      Rendering/RenderPass/NodeBufferedData.cpp
                      Rendering/RenderPass/RenderBin.cpp
                      Rendering/RenderPass/RenderPass.cpp
                      Rendering/RenderPass/RenderPassCuller.cpp
                      Rendering/RenderPass/RenderPassExecutor.cpp
                      Rendering/RenderPass/RenderQueue.cpp
)

set( SCENES_SOURCE_HEADERS Scenes/DefaultScene/Headers/DefaultScene.h
                           Scenes/Headers/Scene.h
                           Scenes/Headers/SceneComponent.h
                           Scenes/Headers/SceneEnvironmentProbePool.h
                           Scenes/Headers/SceneInput.h
                           Scenes/Headers/SceneInputActions.h
                           Scenes/Headers/ScenePool.h
                           Scenes/Headers/SceneShaderData.h
                           Scenes/Headers/SceneState.h
                           Scenes/WarScene/AESOPActions/Headers/WarSceneActions.h
                           Scenes/WarScene/Headers/WarScene.h
                           Scenes/WarScene/Headers/WarSceneAIProcessor.h
)

set( SCENES_SOURCE Scenes/Scene.cpp
                   Scenes/SceneEnvironmentProbePool.cpp
                   Scenes/SceneInput.cpp
                   Scenes/SceneInputActions.cpp
                   Scenes/ScenePool.cpp
                   Scenes/SceneShaderData.cpp
                   Scenes/SceneState.cpp
                   Scenes/DefaultScene/DefaultScene.cpp
                   Scenes/WarScene/WarScene.cpp
                   Scenes/WarScene/WarSceneAI.cpp
                   Scenes/WarScene/WarSceneAIProcessor.cpp
                   Scenes/WarScene/AESOPActions/WarSceneActions.cpp
)

set ( SCRIPTING_SOURCE_HEADERS Scripting/Headers/GameScript.h
                               Scripting/Headers/Script.h
                               Scripting/Headers/Script.inl
                               Scripting/Headers/ScriptBindings.h
)

set( SCRIPTING_SOURCE Scripting/GameScript.cpp
                      Scripting/Script.cpp
                      Scripting/ScriptBindings.cpp
)

set ( UTILITY_SOURCE_HEADERS Utility/Headers/Colours.h
                             Utility/Headers/CommandParser.h
                             Utility/Headers/CRC.h
                             Utility/Headers/ImageTools.h
                             Utility/Headers/ImageToolsFwd.h
                             Utility/Headers/Localization.h
                             Utility/Headers/StateTracker.h
                             Utility/Headers/TextLabel.h
                             Utility/Headers/XMLParser.h
)

set( UTILITY_SOURCE Utility/Colours.cpp
                    Utility/CommandParser.cpp
                    Utility/CRC.cpp
                    Utility/EASTLImport.cpp
                    Utility/ImageTools.cpp
                    Utility/Localization.cpp
                    Utility/TextLabel.cpp
                    Utility/XMLParser.cpp
)

set( TEST_ENGINE_SOURCE UnitTests/unitTestCommon.h
                        UnitTests/unitTestCommon.cpp
                        UnitTests/Test-Engine/ByteBufferTests.cpp
                        UnitTests/Test-Engine/MathMatrixTests.cpp
                        UnitTests/Test-Engine/MathVectorTests.cpp
                        UnitTests/Test-Engine/ScriptingTests.cpp
)

set( TEST_PLATFORM_SOURCE UnitTests/unitTestCommon.h
                          UnitTests/unitTestCommon.cpp
                          UnitTests/Test-Platform/FileManagement.cpp
                          UnitTests/Test-Platform/ConversionTests.cpp
                          UnitTests/Test-Platform/DataTypeTests.cpp
                          UnitTests/Test-Platform/HashTests.cpp
                          UnitTests/Test-Platform/StringTests.cpp
                          UnitTests/Test-Platform/ThreadingTests.cpp
)

set_source_files_properties("UnitTests/unitTestCommon.h" PROPERTIES HEADER_FILE_ONLY ON)

set( ENGINE_SOURCE_CODE_HEADERS ${AI_SOURCE_HEADERS}
                                ${CORE_SOURCE_HEADERS}
                                ${DYNAMICS_SOURCE_HEADERS}
                                ${ECS_SOURCE_HEADERS}
                                ${EDITOR_SOURCE_HEADERS}
                                ${ENVIRONMENT_SOURCE_HEADERS}
                                ${GEOMETRY_SOURCE_HEADERS}
                                ${GRAPHS_SOURCE_HEADERS}
                                ${GUI_SOURCE_HEADERS}
                                ${MANAGERS_SOURCE_HEADERS}
                                ${NETWORKING_SOURCE_HEADERS}
                                ${PHYSICS_SOURCE_HEADERS}
                                ${PLATFORM_SOURCE_HEADERS}
                                ${RENDERING_SOURCE_HEADERS}
                                ${SCENES_SOURCE_HEADERS}
                                ${SCRIPTING_SOURCE_HEADERS}
                                ${UTILITY_SOURCE_HEADERS}
                                "engineMain.h"
)
set_source_files_properties(${ENGINE_SOURCE_CODE_HEADERS} PROPERTIES HEADER_FILE_ONLY ON)

set( ENGINE_SOURCE_CODE ${AI_SOURCE}
                        ${CORE_SOURCE}
                        ${DYNAMICS_SOURCE}
                        ${ECS_SOURCE}
                        ${EDITOR_SOURCE}
                        ${ENVIRONMENT_SOURCE}
                        ${GEOMETRY_SOURCE}
                        ${GRAPHS_SOURCE}
                        ${GUI_SOURCE}
                        ${MANAGERS_SOURCE}
                        ${NETWORKING_SOURCE}
                        ${PHYSICS_SOURCE}
                        ${PLATFORM_SOURCE}
                        ${RENDERING_SOURCE}
                        ${SCENES_SOURCE}
                        ${SCRIPTING_SOURCE}
                        ${UTILITY_SOURCE}
                        ${ENGINE_SOURCE_CODE_HEADERS}
)

include(ThirdParty/CMakeHelpers/GlobSources.cmake)
