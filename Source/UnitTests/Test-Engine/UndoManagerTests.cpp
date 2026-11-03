/*
   Copyright (c) 2026 DIVIDE-Studio
   Copyright (c) 2009-2026 Ionut Cava

   This file is part of DIVIDE Framework.
   SPDX-License-Identifier: MIT
*/

#include "UnitTests/unitTestCommon.h"
#include "Editor/Headers/PropertyEditState.h"
#include "Editor/Headers/UndoManager.h"
#include "Rendering/Lighting/Headers/SpotLightGeometry.h"

#include <array>
#include <utility>
#include <vector>

namespace Divide
{
    TEST_CASE("Property edits retain each widget's original value", "[editor][undo]")
    {
        PropertyEditState<int> edits;
        edits.begin(11u, 10);
        edits.begin(22u, 20);
        REQUIRE(edits.finish(11u).value() == 10);
        CHECK_FALSE(edits.finish(11u).has_value());
        REQUIRE(edits.finish(22u).value() == 20);
        edits.begin(11u, 30);
        CHECK(edits.finish(11u).value() == 30);
    }

    TEST_CASE("Property edit events record complete edits and resets", "[editor][undo]")
    {
        PropertyEditState<int> edits;
        std::vector<std::pair<int, int>> changes;
        const auto record = [&changes](const int oldValue, const int newValue)
        {
            if (oldValue != newValue)
            {
                changes.emplace_back(oldValue, newValue);
            }
        };
        RecordPropertyEdits(edits, 1u, 0u, false, false, 5, 5, record);
        RecordPropertyEdits(edits, 0u, 0u, false, false, 5, 10, record);
        RecordPropertyEdits(edits, 0u, 1u, true, false, 10, 12, record);
        REQUIRE(changes.size() == 1u);
        CHECK((changes[0] == std::pair{ 5, 12 }));

        RecordPropertyEdits(edits, 0u, 0u, false, true, 12, 0, record);
        REQUIRE(changes.size() == 2u);
        CHECK((changes[1] == std::pair{ 12, 0 }));

        RecordPropertyEdits(edits, 2u, 0u, false, false, 0, 0, record);
        RecordPropertyEdits(edits, 0u, 2u, true, true, 7, 0, record);
        REQUIRE(changes.size() == 4u);
        CHECK((changes[2] == std::pair{ 0, 7 }));
        CHECK((changes[3] == std::pair{ 7, 0 }));

        RecordPropertyEdits(edits, 3u, 0u, false, false, 0, 0, record);
        RecordPropertyEdits(edits, 0u, 3u, false, false, 0, 0, record);
        RecordPropertyEdits(edits, 0u, 3u, true, false, 0, 1, record);
        CHECK(changes.size() == 4u);
    }

    TEST_CASE("Compound property edits restore their original value as one action", "[editor][undo]")
    {
        using Value = std::array<float, 3>;
        PropertyEditState<Value> edits;
        UndoManager history(4u);
        Value value{ 0.1f, 0.2f, 0.3f };
        const Value original = value;
        unsigned notifications = 0u;
        const auto record = [&](const Value& oldValue, const Value& newValue)
        {
            UndoEntry<Value> entry;
            entry._oldVal = oldValue;
            entry._newVal = newValue;
            entry._dataSetter = [&](const Value& restored) { value = restored; };
            entry._onChangedCbk = [&](std::string_view) { ++notifications; };
            history.registerUndoEntry(entry);
        };
        RecordPropertyEdits(edits, 1u, 0u, false, false, value, value, record);
        value[0] = 0.5f;
        const Value intermediate = value;
        value[1] = 0.7f;
        RecordPropertyEdits(edits, 0u, 1u, true, false, intermediate, value, record);
        const Value edited = value;
        CHECK(history.UndoStackSize() == 1u);
        REQUIRE(history.Undo());
        CHECK(value == original);
        REQUIRE(history.Redo());
        CHECK(value == edited);
        CHECK(notifications == 2u);
    }

    TEST_CASE("Undo and redo retain capacity and discard superseded history", "[editor][undo]")
    {
        UndoManager history(2u);
        int value = 0;
        unsigned notifications = 0u;
        const auto edit = [&](const int next)
        {
            UndoEntry<int> entry;
            entry._name = "Value";
            entry._oldVal = value;
            entry._newVal = next;
            entry._dataSetter = [&](const int& restored) { value = restored; };
            entry._onChangedCbk = [&](const std::string_view name)
            {
                CHECK(name == "Value");
                ++notifications;
            };
            value = next;
            history.registerUndoEntry(entry);
        };

        edit(1);
        edit(2);
        CHECK(history.UndoStackSize() == 2u);
        REQUIRE(history.Undo());
        CHECK(value == 1);
        REQUIRE(history.Redo());
        CHECK(value == 2);
        CHECK(notifications == 2u);
        REQUIRE(history.Undo());
        edit(3);
        CHECK(history.RedoStackSize() == 0u);
        CHECK_FALSE(history.Redo());
        REQUIRE(history.Undo());
        CHECK(value == 1);
        REQUIRE(history.Undo());
        CHECK(value == 0);
        CHECK_FALSE(history.Undo());

        UndoManager disabled(0u);
        UndoEntry<int> entry;
        disabled.registerUndoEntry(entry);
        CHECK(disabled.UndoStackSize() == 0u);
    }

    TEST_CASE("Removing a component discards its undo and redo entries", "[editor][undo]")
    {
        UndoManager history(4u);
        int first = 0;
        int second = 0;
        const auto edit = [&](const I64 owner, int& value)
        {
            UndoEntry<int> entry;
            entry._ownerGUID = owner;
            entry._oldVal = value;
            entry._newVal = value + 1;
            entry._dataSetter = [&value](const int& restored) { value = restored; };
            ++value;
            history.registerUndoEntry(entry);
        };

        edit(1, first);
        edit(2, second);
        REQUIRE(history.Undo());
        CHECK(second == 0);
        CHECK(history.UndoStackSize() == 1u);
        CHECK(history.RedoStackSize() == 1u);

        history.removeEntriesForOwner(2);
        CHECK(history.RedoStackSize() == 0u);
        REQUIRE(history.Undo());
        CHECK(first == 0);
        history.removeEntriesForOwner(1);
        CHECK(history.RedoStackSize() == 0u);
        CHECK_FALSE(history.Undo());
        CHECK_FALSE(history.Redo());
    }

    TEST_CASE("Spotlight angle undo and redo preserve cone ordering", "[editor][undo][lighting]")
    {
        SpotConeAngles angles;
        UndoManager history(2u);
        bool shadowsDirty = false;
        UndoEntry<float> entry;
        entry._name = "Cutoff Angle";
        entry._oldVal = angles._inner;
        entry._newVal = 60.f;
        entry._dataSetter = [&](const float& value) { angles._inner = value; };
        entry._onChangedCbk = [&](const std::string_view)
        {
            angles.setInnerClamped(angles._inner);
            shadowsDirty = true;
        };
        angles._inner = entry._newVal;
        entry._onChangedCbk(entry._name.c_str());
        history.registerUndoEntry(entry);
        CHECK(angles._inner == 35.f);
        CHECK(angles._outer == 35.f);

        shadowsDirty = false;
        REQUIRE(history.Undo());
        CHECK(angles._inner == 15.f);
        CHECK(angles._outer == 35.f);
        CHECK(shadowsDirty);
        shadowsDirty = false;
        REQUIRE(history.Redo());
        CHECK(angles._inner == 35.f);
        CHECK(angles._outer == 35.f);
        CHECK(shadowsDirty);
    }
}
