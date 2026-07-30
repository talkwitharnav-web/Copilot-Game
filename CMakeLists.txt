cmake_minimum_required(VERSION 3.25)

# NOTE: "VoxelGame" is a placeholder name. See CLAUDE.md before treating it as final.
project(VoxelGame
    VERSION 0.1.0
    DESCRIPTION "Custom voxel sandbox engine and game"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Everything runnable lands in one folder so the game can find assets by a stable relative path later.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Third-party dependencies
#
# FetchContent tells CMake to download a dependency's source at configure time,
# pinned to an exact tag, and build it as part of our project. Nothing is
# committed to Git and nothing has to be installed on the machine by hand.
# ---------------------------------------------------------------------------
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

# GLM: vector and matrix maths. Header-only. Needed from the moment anything is
# drawn in 3D, because a perspective camera is matrix multiplication and hand
# rolling it is tedious and easy to get subtly, invisibly wrong.
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glm)

# Vulkan is installed system-wide via the LunarG SDK, so it is found, not fetched.
find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------------------
# Our own compile options.
#
# Declared *after* the dependencies above on purpose: add_compile_options only
# affects targets added from this point on, so our strict warning settings apply
# to our code without drowning us in warnings from third-party sources.
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(
        /W4             # high warning level
        /permissive-    # enforce standard C++ instead of Microsoft extensions
        /utf-8          # treat source files as UTF-8
        /Zc:__cplusplus # report the real C++ standard version
    )
endif()

add_subdirectory(engine)
add_subdirectory(game)
