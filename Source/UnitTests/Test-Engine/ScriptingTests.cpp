#include "UnitTests/unitTestCommon.h"

#include "Scripting/Headers/Script.h"

namespace Divide
{

TEST_CASE("Simple Inline Script Test", "[scripting]")
{
    platformInitRunListener::PlatformInit();

    Script input("5.3 + 2.1");
    constexpr D64 result = 7.4;

    CHECK_COMPARE(input.eval<double>(), result);
}

TEST_CASE( "External Function Script Test", "[scripting]" )
{
    platformInitRunListener::PlatformInit();

    Script input
    (R"(
        use("utility.chai");
        var my_fun = fun(x)
        {
            return x + 2;
        };

        something(my_fun)
    )");

    I32 variable = 0;
    const auto testFunc = [&variable](const DELEGATE_STD<I32, I32>& t_func) {
        variable = t_func(variable);
    };

    input.registerFunction(testFunc, "something");
    input.eval<void>();
    CHECK_EQUAL(variable, 2);
}

} //namespace Divide
