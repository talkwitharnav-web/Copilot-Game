add_library(engine STATIC
    src/core/FrameLimiter.cpp
    src/core/Log.cpp
    src/core/Paths.cpp
    src/platform/Window.cpp
    src/render/VulkanCheck.cpp
    src/render/VulkanContext.cpp
    src/render/Swapchain.cpp
    src/render/Buffer.cpp
    src/render/Vertex.cpp
    src/render/GraphicsPipeline.cpp
    src/render/Renderer.cpp
)

# PUBLIC: anything linking the engine also gets these headers.
# The engine's own private headers live next to its .cpp files under src/.
target_include_directories(engine
    PUBLIC  include
    PRIVATE src
)

# Vulkan is PUBLIC because Vulkan types appear in the engine's public headers.
# GLFW is PRIVATE: the game never talks to GLFW directly.
target_link_libraries(engine
    PUBLIC  Vulkan::Vulkan
    PRIVATE glfw
)

# Validation layers are Vulkan's built-in error checker. They cost real
# performance, so they are compiled in for Debug builds only.
target_compile_definitions(engine PRIVATE
    $<$<CONFIG:Debug>:ENGINE_ENABLE_VALIDATION>
)

# ---------------------------------------------------------------------------
# Shaders
#
# GPUs cannot read GLSL source. glslc (from the Vulkan SDK) compiles it to
# SPIR-V, the binary format Vulkan accepts. Each shader gets its own build rule
# that reruns whenever that shader's source changes, so editing a shader and
# rebuilding is enough to see the difference on screen.
# ---------------------------------------------------------------------------
set(ENGINE_SHADERS
    shaders/triangle.vert
    shaders/triangle.frag
)

set(shader_output_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
set(compiled_shaders "")

foreach(shader IN LISTS ENGINE_SHADERS)
    get_filename_component(shader_name "${shader}" NAME)
    set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/${shader}")
    set(shader_binary "${shader_output_dir}/${shader_name}.spv")

    add_custom_command(
        OUTPUT  "${shader_binary}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${shader_output_dir}"
        COMMAND Vulkan::glslc --target-env=vulkan1.3 "${shader_source}" -o "${shader_binary}"
        DEPENDS "${shader_source}"
        COMMENT "Compiling shader ${shader_name}"
        VERBATIM
    )

    list(APPEND compiled_shaders "${shader_binary}")
endforeach()

add_custom_target(engine_shaders DEPENDS ${compiled_shaders} SOURCES ${ENGINE_SHADERS})
add_dependencies(engine engine_shaders)
