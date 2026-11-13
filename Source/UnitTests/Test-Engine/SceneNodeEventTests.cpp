// Copyright (c) 2026 DIVIDE-Studio

#include "UnitTests/unitTestCommon.h"
#include "Graphs/Headers/SceneNodeEventQueue.h"
#include "Graphs/Headers/SceneNodeRemovalQueue.h"
#include "Graphs/Headers/SGNRelationshipCache.h"
#include "Graphs/Headers/SceneNodeFwd.h"

namespace Divide
{
    TEST_CASE( "Only shadow-casting geometry invalidates cached shadows", "[scene_graph][shadows][render_smoke]" )
    {
        for (const SceneNodeType type : {SceneNodeType::TYPE_SPHERE_3D, SceneNodeType::TYPE_BOX_3D,
                                        SceneNodeType::TYPE_QUAD_3D, SceneNodeType::TYPE_SUBMESH,
                                        SceneNodeType::TYPE_TERRAIN, SceneNodeType::TYPE_VEGETATION,
                                        SceneNodeType::TYPE_PARTICLE_EMITTER})
        {
            CAPTURE(to_base(type));
            CHECK(IsShadowCaster(type, true));
            CHECK_FALSE(IsShadowCaster(type, false));
        }

        // Light nodes and aggregate mesh/root bounds are not rendered geometry.
        for (const SceneNodeType type : {SceneNodeType::TYPE_TRANSFORM, SceneNodeType::TYPE_MESH,
                                        SceneNodeType::TYPE_SKY, SceneNodeType::TYPE_WATER,
                                        SceneNodeType::TYPE_INFINITEPLANE, SceneNodeType::COUNT})
        {
            CAPTURE(to_base(type));
            CHECK_FALSE(IsShadowCaster(type, true));
            CHECK_FALSE(IsShadowCaster(type, false));
        }
    }

    namespace
    {
        ECS::CustomEvent MakeEvent(const U32 id)
        {
            ECS::CustomEvent event{};
            event._type = ECS::CustomEvent::Type::EntityFlagChanged;
            event._flag = id;
            event._data = id ^ 0xA5A55A5Au;
            return event;
        }

        struct RemovalTestGraph
        {
            struct Node
            {
                I64 _guid = -1;
                I64 _parent = -1;
                bool _alive = false;
                vector<I64> _children;
            };

            std::array<Node, 5u> _nodes
            {{
                { 0, -1, true, { 1, 2, 3 } },
                { 1, 0, true, { 4 } },
                { 2, 0, true, {} },
                { 3, 0, true, {} },
                { 4, 1, true, {} }
            }};
            vector<I64> _removed;

            Node* resolve(const I64 guid)
            {
                if (guid < 0 || guid >= to_I64(_nodes.size()))
                {
                    return nullptr;
                }
                Node& node = _nodes[to_size(guid)];
                return node._alive ? &node : nullptr;
            }

            void remove(Node& node)
            {
                const vector<I64> children = node._children;
                for (const I64 child : children)
                {
                    if (Node* childNode = resolve(child))
                    {
                        remove(*childNode);
                    }
                }
                if (Node* parent = resolve(node._parent))
                {
                    erase_if(parent->_children, [&node](const I64 child) { return child == node._guid; });
                }
                node._alive = false;
                _removed.push_back(node._guid);
            }

            void move(const I64 guid, const I64 newParent)
            {
                Node& node = *resolve(guid);
                Node& oldParent = *resolve(node._parent);
                erase_if(oldParent._children, [guid](const I64 child) { return child == guid; });
                node._parent = newParent;
                resolve(newParent)->_children.push_back(guid);
            }
        };

        class RelationshipCacheProbe final : public SGNRelationshipCache
        {
        public:
            RelationshipCacheProbe() : SGNRelationshipCache(nullptr) {}

            void replace(const I64 child, const I64 parent, const I64 sibling)
            {
                clearEntries();
                _childrenRecursiveCache.push_back({ child, 0u });
                _parentRecursiveCache.push_back({ parent, 0u });
                _siblingCache.push_back({ sibling, 0u });
                _isValid = true;
            }
        };
    }

    TEST_CASE( "Scene node events retain overflow and defer reentrant events", "[scene_graph][threading][render_smoke]" )
    {
        SceneNodeEventQueue queue;
        constexpr U32 count = to_U32(SceneNodeEventQueue::CAPACITY) + 32u;
        for (U32 i = 0u; i < count; ++i)
        {
            queue.push(MakeEvent(i));
        }

        vector<U32> received;
        queue.drain([&](const ECS::CustomEvent& event)
        {
            CHECK(event._data == (event._flag ^ 0xA5A55A5Au));
            received.push_back(event._flag);
        });
        REQUIRE(received.size() == count);
        for (U32 i = 0u; i < count; ++i)
        {
            CHECK(received[i] == i);
        }

        queue.push(MakeEvent(count));
        queue.drain([&](const ECS::CustomEvent& event)
        {
            received.push_back(event._flag);
            queue.push(MakeEvent(count + 1u));
        });
        REQUIRE(received.size() == count + 1u);
        queue.drain([&](const ECS::CustomEvent& event)
        {
            received.push_back(event._flag);
        });
        REQUIRE(received.size() == count + 2u);
        CHECK(received.back() == count + 1u);
    }

    TEST_CASE( "Concurrent scene event producers publish complete payloads once", "[scene_graph][threading][render_smoke]" )
    {
        SceneNodeEventQueue queue;
        constexpr U32 producerCount = 4u;
        constexpr U32 eventsPerProducer = 1000u;
        constexpr U32 eventCount = producerCount * eventsPerProducer;
        std::array<U32, eventCount> received{};
        std::atomic_uint finished{0u};
        std::array<std::thread, producerCount> producers;
        for (U32 producer = 0u; producer < producerCount; ++producer)
        {
            producers[producer] = std::thread([&, producer]
            {
                for (U32 i = 0u; i < eventsPerProducer; ++i)
                {
                    queue.push(MakeEvent(producer * eventsPerProducer + i));
                }
                finished.fetch_add(1u);
            });
        }

        U32 invalid = 0u;
        const auto record = [&](const ECS::CustomEvent& event)
        {
            if (event._type != ECS::CustomEvent::Type::EntityFlagChanged ||
                event._flag >= eventCount ||
                event._data != (event._flag ^ 0xA5A55A5Au))
            {
                ++invalid;
            }
            else
            {
                ++received[event._flag];
            }
        };
        while (finished.load() != producerCount)
        {
            queue.drain(record);
            std::this_thread::yield();
        }
        for (std::thread& producer : producers)
        {
            producer.join();
        }
        queue.drain(record);

        CHECK(invalid == 0u);
        for (const U32 count : received)
        {
            CHECK(count == 1u);
        }
    }

    TEST_CASE( "Deferred node removal follows stable identities across topology changes", "[scene_graph][threading][render_smoke]" )
    {
        SceneNodeRemovalQueue queue;
        RemovalTestGraph graph;
        vector<I64> processed;
        const auto resolve = [&](const I64 guid) { return graph.resolve(guid); };
        const auto remove = [&](RemovalTestGraph::Node& node)
        {
            processed.push_back(node._guid);
            graph.remove(node);
        };

        SECTION( "a sibling removal does not change the target" )
        {
            queue.push(2);
            graph.remove(*graph.resolve(1));
            queue.drain(resolve, remove);
            CHECK(graph.resolve(2) == nullptr);
            CHECK(graph.resolve(3) != nullptr);
            CHECK(processed == vector<I64>{ 2 });
        }

        SECTION( "a reparented target is removed from its current parent" )
        {
            queue.push(2);
            graph.move(2, 3);
            queue.drain(resolve, remove);
            CHECK(graph.resolve(2) == nullptr);
            REQUIRE(graph.resolve(3) != nullptr);
            CHECK(graph.resolve(3)->_children.empty());
            CHECK(graph.resolve(1) != nullptr);
        }

        SECTION( "nested and duplicate removals do not resolve destroyed descendants" )
        {
            queue.push(1);
            queue.push(4);
            queue.push(1);
            queue.drain(resolve, remove);
            CHECK(graph.resolve(1) == nullptr);
            CHECK(graph.resolve(4) == nullptr);
            CHECK(processed == vector<I64>{ 1 });
            CHECK(graph.resolve(2) != nullptr);
        }

        SECTION( "reentrant requests wait for the next batch" )
        {
            queue.push(2);
            queue.push(99);
            queue.drain(resolve, [&](RemovalTestGraph::Node& node)
            {
                remove(node);
                queue.push(3);
            });
            CHECK(graph.resolve(2) == nullptr);
            CHECK(graph.resolve(3) != nullptr);
            queue.drain(resolve, remove);
            CHECK(graph.resolve(3) == nullptr);
        }
    }

    TEST_CASE( "Concurrent deferred removal requests are deduplicated", "[scene_graph][threading][render_smoke]" )
    {
        SceneNodeRemovalQueue queue;
        constexpr U32 producerCount = 4u;
        constexpr U32 perProducer = 250u;
        constexpr U32 count = producerCount * perProducer;
        std::array<U32, count> removed{};
        std::array<std::thread, producerCount> producers;
        for (U32 producer = 0u; producer < producerCount; ++producer)
        {
            producers[producer] = std::thread([&, producer]
            {
                for (U32 index = 0u; index < perProducer; ++index)
                {
                    const I64 guid = to_I64(producer * perProducer + index);
                    queue.push(guid);
                    queue.push(guid);
                }
            });
        }
        for (std::thread& producer : producers)
        {
            producer.join();
        }
        queue.drain([&](const I64 guid) { return &removed[to_size(guid)]; },
                    [](U32& occurrences) { ++occurrences; });
        for (const U32 occurrences : removed)
        {
            CHECK(occurrences == 1u);
        }
    }

    TEST_CASE( "Rebuilt relationship entries discard previous topology", "[scene_graph][threading][render_smoke]" )
    {
        RelationshipCacheProbe cache;
        using Type = SGNRelationshipCache::RelationshipType;
        cache.replace(1, 2, 3);
        CHECK(cache.validateRelationship(1, Type::CHILD));
        CHECK(cache.validateRelationship(2, Type::PARENT));
        CHECK(cache.validateRelationship(3, Type::SIBLING));
        cache.replace(4, 5, 6);
        CHECK_FALSE(cache.validateRelationship(1, Type::CHILD));
        CHECK_FALSE(cache.validateRelationship(2, Type::PARENT));
        CHECK_FALSE(cache.validateRelationship(3, Type::SIBLING));
        CHECK(cache.validateRelationship(4, Type::CHILD));
        CHECK(cache.validateRelationship(5, Type::PARENT));
        CHECK(cache.validateRelationship(6, Type::SIBLING));
    }
}
