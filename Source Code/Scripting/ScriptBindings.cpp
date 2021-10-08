#include "stdafx.h"

#include "Headers/ScriptBindings.h"

namespace Divide {

    chaiscript::ModulePtr create_chaiscript_stl_extra()
    {
        auto module = std::make_shared<chaiscript::Module>();
        chaiscript::bootstrap::standard_library::list_type<std::list<chaiscript::Boxed_Value> >("List", *module);
        chaiscript::bootstrap::standard_library::vector_type<vector<U16> >("u16vector", *module);
        module->add(chaiscript::vector_conversion<vector<U16>>());
        return module;
    }

    chaiscript::ModulePtr create_chaiscript_stdlib() {
        return chaiscript::Std_Lib::library();
    }

    chaiscript::ModulePtr create_chaiscript_bindings() {
        auto module = std::make_shared<chaiscript::Module>();

        return module;
    }
}