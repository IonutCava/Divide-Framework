#include "UnitTests/unitTestCommon.h"

#include "Core/TemplateLibraries/Headers/SharedPtrFactory.h"
#include <functional>
#include <stdexcept>

namespace Divide
{
    namespace
    {
        struct FactoryBase
        {
            virtual ~FactoryBase() = default;
        };

        struct FactoryObject final : FactoryBase
        {
            FactoryObject(int& reference, std::unique_ptr<int> value)
                : _reference(reference), _value(std::move(value))
            {
            }

            int& _reference;
            std::unique_ptr<int> _value;
        };

        struct ThrowingFactoryObject
        {
            ThrowingFactoryObject()
            {
                throw std::runtime_error("constructor failed");
            }
        };
    }

    TEST_CASE("Shared pointer factory forwards constructor arguments", "[factory_tests]")
    {
        int reference = 7;
        const SharedPtrFactory<FactoryObject> factory;
        auto object = factory(reference, std::make_unique<int>(42));
        CHECK(&object->_reference == &reference);
        CHECK(*object->_value == 42);

        std::function<std::shared_ptr<FactoryBase>(int&, std::unique_ptr<int>)> sceneFactory = factory;
        CHECK(sceneFactory(reference, std::make_unique<int>(9)) != nullptr);
    }

    TEST_CASE("Shared pointer factory propagates constructor errors", "[factory_tests]")
    {
        CHECK_THROWS_AS(SharedPtrFactory<ThrowingFactoryObject>{}(), std::runtime_error);
    }
}
