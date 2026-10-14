#pragma once

// Test data for cmake/CheckDeprecations.cmake (tests/CMakeLists.txt): never compiled.

WLW_DEPRECATED(0.2, "Use NewThing()") inline void OldThing() {}
