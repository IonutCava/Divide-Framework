

namespace Divide {

I64 GUIDWrapper::generateGUID() noexcept {
    // Always start from 1 as we use IDs mainly as key values
    static std::atomic<I64> idGenerator{ 1 };
    return idGenerator.fetch_add(1);
}
}  // namespace Divide
