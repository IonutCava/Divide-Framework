// Copyright (c) 2026 DIVIDE-Studio

#pragma once

#include "ECS/Components/Headers/TransformComponent.h"

#include <cmath>

namespace Divide
{
    [[nodiscard]] inline TransformValues ComposeWorldTransform(const TransformValues& local,
                                                               const TransformValues* parent,
                                                               const bool useLocalRotations) noexcept
    {
        TransformValues world = local;
        if (parent != nullptr)
        {
            world._orientation = parent->_orientation * local._orientation;
            world._scale = parent->_scale * local._scale;
            const float3 position = useLocalRotations ? local._translation
                : parent->_orientation * (parent->_scale * local._translation);
            world._translation = parent->_translation + position;
        }
        return world;
    }

    [[nodiscard]] inline bool IsFiniteTransform(const TransformValues& values) noexcept
    {
        return std::isfinite(values._translation.x) && std::isfinite(values._translation.y) &&
               std::isfinite(values._translation.z) && std::isfinite(values._scale.x) &&
               std::isfinite(values._scale.y) && std::isfinite(values._scale.z) &&
               std::isfinite(values._orientation.X()) && std::isfinite(values._orientation.Y()) &&
               std::isfinite(values._orientation.Z()) && std::isfinite(values._orientation.W());
    }

    [[nodiscard]] inline bool TryReparentTransform(const TransformValues& local,
                                                   const TransformValues* oldParent,
                                                   const TransformValues* newParent,
                                                   const bool useLocalRotations,
                                                   TransformValues& result) noexcept
    {
        const TransformValues world = ComposeWorldTransform(local, oldParent, useLocalRotations);
        if (!IsFiniteTransform(world))
        {
            return false;
        }

        TransformValues newLocal = world;
        if (newParent != nullptr)
        {
            if (!IsFiniteTransform(*newParent) ||
                IS_ZERO(newParent->_scale.x) || IS_ZERO(newParent->_scale.y) || IS_ZERO(newParent->_scale.z) ||
                std::abs(newParent->_orientation.magnituteSQ() - 1.f) > 0.001f)
            {
                return false;
            }

            const quatf inverseOrientation = newParent->_orientation.getConjugate();
            newLocal._orientation = inverseOrientation * world._orientation;
            newLocal._scale = world._scale / newParent->_scale;
            const float3 position = world._translation - newParent->_translation;
            newLocal._translation = useLocalRotations ? position : (inverseOrientation * position) / newParent->_scale;
        }

        if (!IsFiniteTransform(newLocal))
        {
            return false;
        }
        result = newLocal;
        return true;
    }

    struct TransformHierarchyEntry
    {
        TransformComponent::Values& _local;
        TransformComponent::Values& _world;
        U32& _broadcastMask;
        U64& _resolvedEpoch;
        bool _useLocalRotations;
    };

    template<typename Node, typename GetParent, typename GetEntry>
    void ResolveWorldTransform(Node& node, const U64 epoch, const GetParent& getParent, const GetEntry& getEntry)
    {
        TransformHierarchyEntry entry = getEntry(node);
        if (entry._resolvedEpoch == epoch)
        {
            return;
        }

        const TransformComponent::Values* parentWorld = nullptr;
        if (Node* parent = getParent(node))
        {
            ResolveWorldTransform(*parent, epoch, getParent, getEntry);
            const TransformHierarchyEntry parentEntry = getEntry(*parent);
            parentWorld = &parentEntry._world;
            if (parentEntry._broadcastMask != 0u)
            {
                entry._broadcastMask |= parentEntry._broadcastMask;
                entry._world._computed = false;
            }
        }

        entry._resolvedEpoch = epoch;
        if (entry._world._computed)
        {
            return;
        }

        entry._world._matrix = entry._local._matrix;
        entry._world._previousValues = entry._world._values;
        entry._world._values = ComposeWorldTransform(entry._local._values,
                                                     parentWorld != nullptr ? &parentWorld->_values : nullptr,
                                                     entry._useLocalRotations);

        entry._world._matrix = mat4<F32>
        {
            entry._world._values._translation,
            entry._world._values._scale,
            entry._world._values._orientation.getConjugate()
        };
        entry._world._computed = true;
    }
}
