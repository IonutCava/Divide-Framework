#pragma once

#include <memory>
#include <utility>

namespace Divide
{
    template<typename T>
    struct SharedPtrFactory
    {
        template<typename... Args>
        [[nodiscard]] std::shared_ptr<T> operator()(Args&&... args) const
        {
            return std::make_shared<T>(std::forward<Args>(args)...);
        }
    };
}
