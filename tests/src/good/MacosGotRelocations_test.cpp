
#include <catch.hpp>
#include <iostream>
#include "WaitForReload.hpp"
#include "catch_amalgamated.hpp"
#include "utility/MacosGotRelocations.hpp"

TEST_CASE("GOT relocations on macOS", "[common]")
{
    auto intTypeOf = getIntTypeOf();
    auto floatTypeOf = getFloatTypeOf();
    auto stringTypeOf = getStringTypeOf();

    std::cout << "JET_TEST: disable(mgreloc:1)" << std::endl;
    waitForReload();

    REQUIRE(getIntTypeOf() == intTypeOf);
    REQUIRE(getFloatTypeOf() == floatTypeOf);
    REQUIRE(getStringTypeOf() == stringTypeOf);
}
