#include "stdafx.h"

#include "Headers/BufferLocks.h"

namespace Divide {

    [[nodiscard]] bool IsEmpty(const BufferLocks& locks) noexcept {
        if (locks.empty()) {
            return true;
        }

        for (auto& it : locks) {
            if (it._targetBuffer != nullptr) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] bool IsEmpty(const FenceLocks& locks) noexcept {
        if (locks.empty()) {
            return true;
        }

        for (auto& it : locks) {
            if (it != nullptr) {
                return false;
            }
        }

        return true;
    }
} //namespace Divide
