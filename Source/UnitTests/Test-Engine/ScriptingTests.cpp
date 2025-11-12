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
#if defined(DIVIDE_UBSAN_REQUESTED)
    STUBBED("ToDo: Fix chaiscript function binding issues with UBSAN - Ionut");
#else
    platformInitRunListener::PlatformInit();

    Script input
    (R"(
        use("utility.chai");
        var my_fun = fun(x)
        {
            return x + 2;
        };

        return external_lambda(my_fun);
    )");

    I32 variable = 0;
    const auto testFunc = [&variable](const DELEGATE_STD<I32, I32>& t_func) {
        variable = t_func(variable);
        return true;
    };

    input.registerFunction(testFunc, "external_lambda");
    CHECK_TRUE(input.eval<bool>());
    CHECK_EQUAL(variable, 2);
#endif

}

} //namespace Divide
