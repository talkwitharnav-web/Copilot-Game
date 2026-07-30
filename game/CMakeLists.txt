add_executable(game
    src/Main.cpp
    src/world/Noise.cpp
    src/world/World.cpp
    src/world/Player.cpp
    src/world/Raycast.cpp
    src/world/BlockOutline.cpp
    src/world/TerrainGenerator.cpp
    src/world/ChunkMesher.cpp
)

# Blocks, chunks and meshing are game concepts and deliberately live here, not
# in the engine. The engine only ever receives finished mesh data.
target_include_directories(game PRIVATE src)

target_link_libraries(game PRIVATE engine)

# Assets are resolved relative to the executable, so they have to sit next to it.
# Copied after every build rather than at configure time, so editing a texture
# and rebuilding is enough to see the change.
add_custom_command(TARGET game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
    COMMENT "Copying assets next to the executable"
)

