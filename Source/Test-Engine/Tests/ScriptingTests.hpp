#include "Scripting/Headers/Script.h"

namespace Divide
{

TEST_MEMBER_FUNCTION(ScriptTestClass, eval, Simple)
{
    if (PreparePlatform()) {
        Script input("5.3 + 2.1");
        constexpr D64 result = 7.4;

        CHECK_EQUAL(input.eval<double>(), result);
    }
}

TEST_MEMBER_FUNCTION(ScriptTestClass, eval, ExternalFunction)
{
    if (PreparePlatform()) {
        Script input("use(\"utility.chai\");"
            "var my_fun = fun(x) { return x + 2; };"
            "something(my_fun)");

        I32 variable = 0;
        const auto testFunc = [&variable](const DELEGATE_STD<I32, I32>& t_func) {
            variable = t_func(variable);
        };

        input.registerFunction(testFunc, "something");
        input.eval<void>();
        CHECK_EQUAL(variable, 2);
    }
}

} //namespace Divide