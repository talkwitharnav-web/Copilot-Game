add_executable(game
    src/Main.cpp
    src/core/Settings.cpp
    src/hud/Crosshair.cpp
    src/hud/DebugOverlay.cpp
    src/hud/HudPrimitives.cpp
    src/hud/Hotbar.cpp
    src/hud/LoadingScreen.cpp
    src/hud/InventoryScreen.cpp
    src/item/Inventory.cpp
    src/item/Recipe.cpp
    src/item/Smelting.cpp
    src/item/SlotOps.cpp
    src/item/Tool.cpp
    src/world/Creature.cpp
    src/world/Explosion.cpp
    src/world/Furnace.cpp
    src/world/ItemEntity.cpp
    src/world/Noise.cpp
    src/world/Biome.cpp
    src/world/Pathfinder.cpp
    src/world/Structures.cpp
    src/world/World.cpp
    src/world/WorldStore.cpp
    src/world/Player.cpp
    src/world/Raycast.cpp
    src/world/Sky.cpp
    src/world/BlockOutline.cpp
    src/world/TerrainGenerator.cpp
    src/world/ChunkMesher.cpp
)

# Blocks, chunks and meshing are game concepts and deliberately live here, not
# in the engine. The engine only ever receives finished mesh data.
target_include_directories(game PRIVATE src)

target_link_libraries(game PRIVATE engine)

# Assets are resolved relative to the executable, so they have to sit next to it.
# This is a target rather than a POST_BUILD step because POST_BUILD only fires
# when the executable actually relinks - an art-only change left the old texture
# sitting next to the exe and looked like the edit had not worked.
add_custom_target(game_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/assets"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets"
    COMMENT "Syncing assets next to the executable"
)
add_dependencies(game game_assets)

