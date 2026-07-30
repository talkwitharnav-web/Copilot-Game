add_executable(game
    src/Main.cpp
    src/world/ChunkMesher.cpp
)

# Blocks, chunks and meshing are game concepts and deliberately live here, not
# in the engine. The engine only ever receives finished mesh data.
target_include_directories(game PRIVATE src)

target_link_libraries(game PRIVATE engine)
