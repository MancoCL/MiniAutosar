cmake_minimum_required(VERSION 3.20)

project(MiniAutosar
    VERSION 1.1.0
    DESCRIPTION "MiniFee / MiniNvm / MiniFlsIf host verification (RAM-simulated Fls)"
    LANGUAGES C)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Default to a debuggable build when the generator is single-config.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Emit compile_commands.json for editors / clangd / C/C++ IntelliSense.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Source layout (target sources are NOT modified; Port/ provides the host
# platform adaptation + RAM-simulated Fls + the self-test entry point).
# ---------------------------------------------------------------------------
set(MINIAUTOSAR_MEMORY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Memory/MiniFlsIf/MiniFlsIf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Memory/MiniFee/MiniFee.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Memory/MiniNvm/MiniNvm.c)

set(MINIAUTOSAR_CONFIG_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Config/MiniFee_Cfg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Config/MiniNvm_Cfg.c)

set(MINIAUTOSAR_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Test/MiniNvm_Test.c)

# Host-only platform layer: AUTOSAR type stubs, CommF helpers, RAM Fls driver,
# and the console self-test entry point.
set(MINIAUTOSAR_PORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Port/Common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Port/Fls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Port/main.c)

add_executable(miniautosar_host
    ${MINIAUTOSAR_MEMORY_SOURCES}
    ${MINIAUTOSAR_CONFIG_SOURCES}
    ${MINIAUTOSAR_TEST_SOURCES}
    ${MINIAUTOSAR_PORT_SOURCES})

target_include_directories(miniautosar_host PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Port
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Config
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Memory/MiniFlsIf
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Memory/MiniFee
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Memory/MiniNvm
    ${CMAKE_CURRENT_SOURCE_DIR}/Codes/Test)

# ---------------------------------------------------------------------------
# Host self-test tuning
#
# The ROTATE self-test case only observes a cluster migration once the active
# cluster is nearly full. With the example config (2 x 0x2000 clusters, 3 blocks
# totalling 72 B per round) that needs well over a hundred rounds. Override the
# target default (8) for the host build so the migration path is actually
# exercised. Keep it <= 255 (the loop counter is uint8).
# ---------------------------------------------------------------------------
set(MINIAUTOSAR_TEST_ROT_ROUNDS "240" CACHE STRING
    "Rotation rounds used by the host self-test (>=113 triggers a migration)")
target_compile_definitions(miniautosar_host PRIVATE
    MININVM_TEST_ROT_ROUNDS=${MINIAUTOSAR_TEST_ROT_ROUNDS}u)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(miniautosar_host PRIVATE /W4 /WX- /permissive-)
    target_compile_definitions(miniautosar_host PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(miniautosar_host PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -fno-omit-frame-pointer)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(miniautosar_host PRIVATE -g3 -O0)
    endif()
endif()

# ---------------------------------------------------------------------------
# Convenience post-build output name and CTest registration
# ---------------------------------------------------------------------------
set_target_properties(miniautosar_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

enable_testing()
add_test(NAME miniautosar_selftest COMMAND miniautosar_host)
