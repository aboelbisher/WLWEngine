// The one main() of the engine tests: doctest runs every TEST_CASE in wlw_tests.
// Filters: wlw_tests -ts=anim (a suite), -tc="tween*" (cases); ctest runs each case on its own.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>
