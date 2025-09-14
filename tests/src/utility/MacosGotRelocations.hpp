
#pragma once

#include <cstdint>

namespace some_ns
{
    template <typename T>
    intptr_t typeOf()
    {
        static char dummy;
        return reinterpret_cast<intptr_t>(&dummy);
    }
}

intptr_t getIntTypeOf();
intptr_t getFloatTypeOf();
intptr_t getStringTypeOf();

