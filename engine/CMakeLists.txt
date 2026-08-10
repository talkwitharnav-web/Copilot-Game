add_library(engine STATIC
    src/audio/AudioEngine.cpp
    src/audio/StbVorbis.cpp
    src/core/FrameLimiter.cpp
    src/core/JobSystem.cpp
    src/core/Log.cpp
    src/core/Paths.cpp
    src/platform/Window.cpp
    src/render/VulkanCheck.cpp
    src/render/VulkanContext.cpp
    src/render/GpuMemory.cpp
    src/render/Swapchain.cpp
    src/render/DepthImage.cpp
    src/render/RenderTarget.cpp
    src/render/ShadowMap.cpp
    src/render/Buffer.cpp
    src/render/UploadContext.cpp
    src/render/Vertex.cpp
    src/render/Camera.cpp
    src/render/TextureArray.cpp
    src/render/GraphicsPipeline.cpp
    src/render/Renderer.cpp
)

# PUBLIC: anything linking the engine also gets these headers.
# The engine's own private headers live next to its .cpp files under src/.
target_include_directories(engine
    PUBLIC  include
    PRIVATE src
)

# Third-party source, compiled at our strict settings everywhere else. stb has
# never claimed to be /W4 clean and its warnings are not ours to fix, so this
# one file silences the specific ones it raises.
#
# **Disabling by number rather than dropping to /W0**, because `/W0` after our
# global `/W4` makes the compiler itself warn (D9025) - trading one warning for
# another. And it is a *file* property rather than a pragma because MSVC raises
# C4701 from the optimizer, after any pragma region has already closed.
if(MSVC)
    set_source_files_properties(src/audio/StbVorbis.cpp PROPERTIES COMPILE_OPTIONS "/wd4701;/wd4244;/wd4245;/wd4457;/wd4456;/wd4100")
endif()

# Vulkan is PUBLIC because Vulkan types appear in the engine's public headers.
# GLM is PUBLIC for the same reason: the game passes matrices to the renderer.
# GLFW is PRIVATE: the game never talks to GLFW directly.
target_link_libraries(engine
    PUBLIC  Vulkan::Vulkan glm::glm
    PRIVATE glfw stb_image miniaudio
)

# GLM defaults to OpenGL's conventions. Vulkan's depth range is 0..1, not -1..1.
# PUBLIC because every translation unit that touches a matrix must agree on this;
# defining it in only some of them produces silently inconsistent maths.
target_compile_definitions(engine PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
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
    shaders/gbuffer.frag
    shaders/hud.frag
    shaders/shadow.vert
    shaders/shadow.frag
    shaders/fullscreen.vert
    shaders/clouds.frag
    shaders/precipitation.frag
    shaders/deferred.frag
    shaders/bloom_down.frag
    shaders/bloom_up.frag
    shaders/tonemap.frag
)

set(shader_output_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
set(compiled_shaders "")

# Shared shader code lives in .glsl files that stages #include. A stage is never
# included by another stage, so depending every stage on every header is exact
# enough: editing a header rebuilds all of them, editing one stage rebuilds one.
#
# glslc can emit a real dependency file (-MD) and CMake can consume it (DEPFILE),
# and that was tried first. **It does not work when the path to the project
# contains a space** - a depfile separates its entries with spaces and glslc does
# not escape them, so Ninja read this checkout's own folder as two dependencies,
# neither of which exists, and recompiled every shader on every build. Verified
# with `ninja -d explain`. CONFIGURE_DEPENDS re-runs the glob when the folder's
# contents change, so a new header does not need a manual edit here.
file(GLOB shader_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.glsl")

foreach(shader IN LISTS ENGINE_SHADERS)
    # The path under shaders/ is flattened into the output name rather than
    # taking the file name alone, so a/common.frag and b/common.frag cannot
    # silently write to the same .spv and leave one of them unbuilt.
    file(RELATIVE_PATH shader_relative "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${CMAKE_CURRENT_SOURCE_DIR}/${shader}")
    string(REPLACE "/" "_" shader_name "${shader_relative}")

    set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/${shader}")
    set(shader_binary "${shader_output_dir}/${shader_name}.spv")

    add_custom_command(
        OUTPUT  "${shader_binary}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${shader_output_dir}"
        COMMAND Vulkan::glslc --target-env=vulkan1.3
                -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                "${shader_source}" -o "${shader_binary}"
        DEPENDS "${shader_source}" ${shader_headers}
        COMMENT "Compiling shader ${shader_name}"
        VERBATIM
    )

    list(APPEND compiled_shaders "${shader_binary}")
endforeach()

add_custom_target(engine_shaders DEPENDS ${compiled_shaders} SOURCES ${ENGINE_SHADERS})
add_dependencies(engine engine_shaders)
