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
#ifndef DVD_UNDO_MANAGER_H_
#define DVD_UNDO_MANAGER_H_

#include "Platform/Video/Headers/PushConstant.h"

namespace Divide {
    struct IUndoEntry {
        virtual ~IUndoEntry() = default;

        PushConstantType _type = PushConstantType::COUNT;
        string _name = "";
        DELEGATE<void, std::string_view> _onChangedCbk;

        virtual void swapValues() = 0;
        virtual void apply() = 0;
    };

    template<typename T>
    struct UndoEntry final : IUndoEntry {
        T _oldVal = {};
        T _newVal = {};
        DELEGATE_STD<void, const T&> _dataSetter = {};

        void swapValues() noexcept override {
            std::swap(_oldVal, _newVal);
        }

        void apply() override {
            assert(_dataSetter != nullptr);
            _dataSetter(_oldVal);
            if (_onChangedCbk) {
                _onChangedCbk(_name.c_str());
            }
        }
    };

    class UndoManager {

    public:
        using UndoStack = std::deque<std::shared_ptr<IUndoEntry>>;

    public:
        explicit UndoManager(U32 maxSize);

        [[nodiscard]] bool Undo();
        [[nodiscard]] bool Redo();

        [[nodiscard]] size_t UndoStackSize() const noexcept { return _undoStack.size(); }
        [[nodiscard]] size_t RedoStackSize() const noexcept { return _redoStack.size(); }

        [[nodiscard]] const string& lasActionName() const noexcept;

        template<typename T>
        void registerUndoEntry(const UndoEntry<T>& entry) {
            auto entryPtr = std::make_shared<UndoEntry<T>>(entry);
            _undoStack.push_back(entryPtr);
            if (to_U32(_undoStack.size()) >= _maxSize) {
                _undoStack.pop_front();
            }
        }

    private:
        [[nodiscard]] bool apply(const std::shared_ptr<IUndoEntry>& entry);

    private:
        const U32 _maxSize = 10;
        UndoStack _undoStack;
        UndoStack _redoStack;
        string _lastActionName;
    };

    FWD_DECLARE_MANAGED_CLASS(UndoManager);

} //namespace Divide

#endif //DVD_UNDO_MANAGER_H_
