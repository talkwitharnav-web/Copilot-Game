add_library(engine STATIC
    src/core/FrameLimiter.cpp
    src/core/Log.cpp
    src/platform/Window.cpp
    src/render/VulkanCheck.cpp
    src/render/VulkanContext.cpp
    src/render/Swapchain.cpp
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
