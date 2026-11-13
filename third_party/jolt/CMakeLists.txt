# ---- Jolt Physics v5.6.0 (MIT): 3D rigid-body physics, built only for wlw::physics (WLW_BUILD_PHYSICS) ----
# Jolt's own source list and target (Jolt/Jolt.cmake, unchanged) with its options chosen here. In its own directory
# so what Jolt.cmake sets (C++17 for the targets made after it) stays out of the other third-party targets.
# Only wlw::physics includes Jolt's headers: games and the engine never see them.
set(PHYSICS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# x86_64 (desktop PCs, the Android emulator): SSE4.2 + POPCNT, the x86_64 Android ABI's baseline. No AVX, FMA, F16C,
# LZCNT or TZCNT: not on every PC or emulator, and the results would differ from builds without them. ARM64 needs
# nothing (NEON is always there).
set(USE_SSE4_1 ON)
set(USE_SSE4_2 ON)
foreach(_jolt_off USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
  set(${_jolt_off} OFF)
endforeach()

# Off: GPU compute (Jolt's hair simulation), ObjectStream (Jolt's own serialization), double precision, the
# cross-platform determinism mode, floating-point exceptions, Jolt's profiler and debug renderer (debug draw turns
# the renderer on), a shared library. Asserts are set by wlw::physics (on with the engine's: Debug builds or
# WLW_ENABLE_ASSERTS).
foreach(_jolt_off JPH_USE_DX12 JPH_USE_VK JPH_USE_MTL JPH_USE_CPU_COMPUTE ENABLE_OBJECT_STREAM DOUBLE_PRECISION
                  CROSS_PLATFORM_DETERMINISTIC FLOATING_POINT_EXCEPTIONS_ENABLED PROFILER_IN_DEBUG_AND_RELEASE
                  PROFILER_IN_DISTRIBUTION DEBUG_RENDERER_IN_DEBUG_AND_RELEASE DEBUG_RENDERER_IN_DISTRIBUTION
                  JPH_BUILD_SHARED_LIBS USE_ASSERTS USE_STD_VECTOR DISABLE_CUSTOM_ALLOCATOR)
  set(${_jolt_off} OFF)
endforeach()
set(OBJECT_LAYER_BITS 16)

include("${CMAKE_CURRENT_SOURCE_DIR}/Jolt/Jolt.cmake")

set_target_properties(Jolt PROPERTIES FOLDER "third_party" POSITION_INDEPENDENT_CODE ON)
# Jolt.cmake names the instruction sets only for Visual Studio generators; with Ninja + MSVC (our presets) its
# headers would fall back to SSE2, so name them here (MSVC needs no /arch flag for SSE4).
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|amd64"
   AND NOT CMAKE_VS_PLATFORM_NAME)
  target_compile_definitions(Jolt PUBLIC JPH_USE_SSE4_1 JPH_USE_SSE4_2)
endif()
# Sanitizer builds: plain malloc for Jolt's temporary and general allocations, so ASan sees every block.
if(WLW_SANITIZE MATCHES "address")
  target_compile_definitions(Jolt PUBLIC JPH_DISABLE_TEMP_ALLOCATOR JPH_DISABLE_CUSTOM_ALLOCATOR)
endif()
# Jolt's headers are system headers for wlw::physics: the engine's warnings (and -Werror) don't apply to them.
get_target_property(_jolt_includes Jolt INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(Jolt PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_jolt_includes}")
