
#include "MacosGotRelocations.hpp"
#include <string>

intptr_t getIntTypeOf()
{
    return some_ns::typeOf<int>();
}

intptr_t getFloatTypeOf()
{
    return some_ns::typeOf<float>();
}

intptr_t getStringTypeOf()
{
    // Just to touch the file
    int a = 0; // <jet_tag: mgreloc:1>
    return some_ns::typeOf<std::string>();
}
