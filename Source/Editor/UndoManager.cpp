// Copyright (c) 2026 DIVIDE-Studio
// Copyright (c) 2009-2026 Ionut Cava

#include "Headers/UndoManager.h"

namespace Divide {

UndoManager::UndoManager(const U32 maxSize)
    : _maxSize(maxSize)
{
}

bool UndoManager::Undo() {
    if (!_undoStack.empty()) {
        auto entry = _undoStack.back();
        const bool ret = apply(entry);
        _undoStack.pop_back();
        if (ret) {
            entry->swapValues();
            _redoStack.push_back(entry);
        }
        return ret;
    }

    return false;
}

bool UndoManager::Redo() {
    if (!_redoStack.empty()) {
        auto entry = _redoStack.back();
        const bool ret = apply(entry);
        _redoStack.pop_back();
        if (ret) {
            entry->swapValues();
            _undoStack.push_back(entry);
        }
        return ret;
    }
    return false;
}

void UndoManager::removeEntriesForOwner(const I64 ownerGUID)
{
    if (ownerGUID == -1)
    {
        return;
    }
    const auto belongsToOwner = [ownerGUID](const std::shared_ptr<IUndoEntry>& entry)
    {
        return entry != nullptr && entry->_ownerGUID == ownerGUID;
    };
    std::erase_if(_undoStack, belongsToOwner);
    std::erase_if(_redoStack, belongsToOwner);
}

bool UndoManager::apply(const std::shared_ptr<IUndoEntry>& entry) {
    if (entry != nullptr) {
        entry->apply();
        _lastActionName = entry->_name;
        return true;
    }
    return false;
}


const string& UndoManager::lasActionName() const noexcept {
    return _lastActionName;
}

} //namespace Divide
