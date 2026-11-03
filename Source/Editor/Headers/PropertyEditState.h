/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#pragma once

#include "Platform/Headers/PlatformDataTypes.h"

#include <optional>
#include <unordered_map>

namespace Divide
{
    template<typename Value>
    class PropertyEditState
    {
      public:
        void begin(const U32 item, const Value& value)
        {
            _originalValues.insert_or_assign(item, value);
        }

        [[nodiscard]] std::optional<Value> finish(const U32 item)
        {
            const auto it = _originalValues.find(item);
            if (it == _originalValues.end())
            {
                return std::nullopt;
            }
            Value value = it->second;
            _originalValues.erase(it);
            return value;
        }

      private:
        std::unordered_map<U32, Value> _originalValues;
    };

    template<typename Value, typename Register>
    void RecordPropertyEdits(PropertyEditState<Value>& state, const U32 activatedItem,
                             const U32 deactivatedItem, const bool deactivatedAfterEdit,
                             const bool reset, const Value& previous, const Value& current, Register&& record)
    {
        if (activatedItem != 0u)
        {
            state.begin(activatedItem, previous);
        }
        if (deactivatedItem != 0u)
        {
            const auto original = state.finish(deactivatedItem);
            if (deactivatedAfterEdit && original.has_value())
            {
                record(*original, reset ? previous : current);
            }
        }
        if (reset)
        {
            record(previous, current);
        }
    }
}
