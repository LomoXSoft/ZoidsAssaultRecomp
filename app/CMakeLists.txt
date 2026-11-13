# zoidsassault - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(zoidsassault LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(ZOIDSASSAULT_SOURCES
    src/main.cpp
    src/hooks.cpp
    src/crash_diag.cpp
)

# Single version source: app/VERSION (bump per release).
file(STRINGS VERSION ZOIDS_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)

if(WIN32)
    add_executable(zoidsassault WIN32 ${ZOIDSASSAULT_SOURCES})
else()
    add_executable(zoidsassault ${ZOIDSASSAULT_SOURCES})
endif()

rexglue_setup_target(zoidsassault GPU_PLUGINS xenos)
target_compile_definitions(zoidsassault PRIVATE ZOIDS_VERSION="${ZOIDS_VERSION}")

if(WIN32)
    # crash_diag.cpp: symbolized stack walk on an unhandled access violation.
    target_link_libraries(zoidsassault PRIVATE dbghelp)
    # src/hooks.cpp replaces some generated functions; keep the linker from
    # rejecting the duplicate definitions.
    target_link_options(zoidsassault PRIVATE "LINKER:/force:multiple")
endif()
