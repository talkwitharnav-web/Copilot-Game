#pragma once

#include <cstdint>
#include <filesystem>

namespace game {

/// Player-chosen options that live outside any particular world.
///
/// Everything here is read **once at startup**. Nothing re-reads the file while
/// playing, and nothing here may be changed on a running game — see
/// `workerThreads` for why that is a deliberate limit rather than laziness.
struct Settings {
    /// Background threads used for world generation and meshing.
    ///
    /// Zero means no worker threads at all: every job runs on the main thread,
    /// which is the lowest-resource mode and what somebody heavily multitasking
    /// would want. The default is half the machine's hardware threads.
    ///
    /// **Changing this requires a restart.** The thread pool is fixed once it is
    /// built, because resizing one while jobs are in flight buys a whole class
    /// of bugs for nothing.
    unsigned workerThreads = 0;

    /// How far the world is drawn, in chunks (32 blocks each). Everything else
    /// derives from this: chunks are generated one ring further out so borders
    /// mesh correctly, and unloaded two rings beyond that.
    ///
    /// 12 is comfortable rather than maximal — measured at roughly 330 MB and
    /// far above the frame budget on this machine. Raise it freely.
    unsigned renderDistance = 12;

    /// How far out chunks are drawn with everything in them, in chunks.
    /// Past it they are drawn as terrain only: the blocks and their baked
    /// lighting are identical, and plants are simply not turned into triangles.
    /// Creatures, dropped items and particles stop being drawn there too.
    ///
    /// **A rendering tier, not a simulation one.** Everything out there still
    /// ticks, still spawns and despawns, still breaks and burns, and is still
    /// saved. Only the triangles are skipped, and they are built again the
    /// moment something comes back into range.
    ///
    /// Setting it to `renderDistance` or higher turns it off, which is what
    /// makes it something to compare against rather than a change you are stuck
    /// with. The frame here is bound by geometry rather than by pixels, so this
    /// is the largest single lever available.
    unsigned detailDistance = 8;

    /// Frames per second to aim for. Zero means uncapped. Adjustable at runtime
    /// with F1/F2; this is only the starting value.
    unsigned frameCap = 120;

    /// Real seconds for one full day and night. Short values are useful for
    /// watching the cycle without waiting.
    unsigned dayLengthSeconds = 600;

    /// One of everything placeable in the hotbar, and placing never runs a
    /// stack down. Useful while developing anything that is not the inventory.
    ///
    /// **Breaking still drops, and drops are still collected** - the mode
    /// changes what placing costs, not whether items exist. Suppressing either
    /// half of that produced bugs twice.
    bool creativeMode = true;

    /// Sound and music, 0 to 1. **Two buses rather than one**, because turning
    /// the music down without silencing the world is the split every settings
    /// screen in this genre offers - and M34 builds the screen, not the system.
    float soundVolume = 1.0f;
    float musicVolume = 0.55f;

    /// Census the generated world and exit, without opening a window.
    ///
    /// **Terrain is the one thing here with a distribution, and a distribution
    /// can be printed.** It reports the surface range, how much of the world is
    /// sea, every biome's share, the top block of each biome, the hollow
    /// fraction of deep rock, per-ore counts, and — the one that matters most —
    /// the number of solid cells sitting above the terrain top, which is
    /// floating land and must be zero.
    ///
    /// It exists because "both presets build clean with zero validation errors"
    /// says nothing whatsoever about whether the world is right. This found
    /// caves being flooded wholesale, humidity clamped so hard that forests
    /// nearly vanished, iron outnumbering coal, and peaks too smooth for the
    /// steep rule to fire - none of which any build could have caught.
    bool worldgenProbe = false;

    /// Writes `block-shapes.txt` beside the exe - one line per block id giving
    /// its shape and the box its drawn geometry actually occupies - and exits
    /// without opening a window.
    ///
    /// It is to block models what `worldgenProbe` is to terrain, and it exists
    /// for the same reason: nothing about a build says whether a block is the
    /// size the reference makes it. `tools/check-models.ps1` reads the dump
    /// against the reference's own `models/block/*.json` and names every block
    /// whose measurements disagree.
    bool blockProbe = false;

    /// Where the player starts, in world blocks. Height is still found from the
    /// terrain, so this only chooses the column.
    ///
    /// Exists so a specific place can be reached without flying there. Testing a
    /// cave otherwise means several minutes of travel before every single
    /// attempt, which is enough friction that the test stops being run.
    int spawnX = 8;
    int spawnZ = 8;

    /// Drops the player onto the floor of the deepest open space in the spawn
    /// column instead of the surface. Underground lighting is impossible to
    /// judge from above ground.
    bool spawnUnderground = false;

    /// Lines the roster up in front of the spawn point and stops the spawner
    /// touching them. Comparing a model against its reference net otherwise
    /// means waiting for the right biome to produce the right animal.
    ///
    /// 0 is off, 1 shows every species in a grid, and anything higher shows
    /// `creatureShowcase - 2` on its own, close enough to judge.
    int creatureShowcase = 0;

    /// Throws one of every awkward block on the floor in front of the spawn
    /// point, so what a dropped item looks like can be judged without mining
    /// for it. A dropped block is a miniature of the block itself, and the only
    /// way to know a bell reads as a bell rather than a gold brick is to look.
    ///
    /// Nothing is written to the world - dropped items are not saved - so this
    /// leaves no trace beyond the setting itself.
    bool dropShowcase = false;

    /// Which curve squashes the high dynamic range image back into a range a
    /// monitor can show. 0 Khronos PBR Neutral, 1 Hable, 2 Reinhard on
    /// luminance, 3 ACES. **A number rather than a name** so it goes through the
    /// same validated parser as everything else. Cycled live with F10.
    ///
    /// ACES by the player's own choice after comparing all four on screen. The
    /// theoretical objection to it - that it hue-shifts saturated primaries,
    /// which is most of a blocky palette - turned out to matter less than the
    /// contrast it gives the sky.
    unsigned toneMapper = 3;

    /// Multiplies the scene before that curve. Deliberately manual - an
    /// automatic exposure would make a torchlit cave's brightness depend on
    /// where you were looking two seconds ago, and light level 0-15 is a game
    /// mechanic that has to stay readable off the screen.
    float exposure = 1.0f;

    /// Light bleeding out of bright surfaces. The most expensive thing M23
    /// added, so it gets an off switch. Toggled live with F11.
    bool bloom = true;

    /// How far the final image is mixed toward the blurred copy.
    float bloomStrength = 0.12f;

    /// Cast shadows from the sun. 0 off, 1 low, 2 medium, 3 high — each level
    /// raises the shadow map's resolution, how many slices of the view it is
    /// split into, and how far shadows reach. Cycled live with G.
    unsigned shadows = 2;

    /// How far a cast shadow darkens the ambient sky as well as the sun, 0 to 1.
    /// Cutting the sun alone leaves shadows reading as pale patches, because a
    /// shadowed surface still receives the whole sky.
    float shadowDarkness = 0.55f;

    /// A lamp riding on the camera, 0 to 1. **Off by default and that is the
    /// decision, not the default value:** its light direction is the view
    /// direction, so every surface faced head-on sits at the peak of its own
    /// highlight, and it reads as a bright spot glued to the middle of the
    /// screen. Offered because it is the only light indoors with a position.
    float handheldLight = 0.0f;

    /// The cloud deck. 0 off, 1 fast, 2 fancy — which is how many steps each
    /// ray takes through it. Cycled live with C.
    unsigned clouds = 2;

    /// How much of the sky is cloud, 0 to 1. The reference covers a little over
    /// a quarter of it.
    float cloudCoverage = 0.36f;

    /// How far a cloud darkens the ground beneath it. The cheapest part of the
    /// whole effect and most of what makes it read from ground level.
    float cloudShadow = 0.55f;

    /// How steeply the ripples tilt a water surface. 0 is a flat mirror.
    float waterWaves = 1.0f;

    /// How much sky a water surface returns. 1 is what real water does; lower
    /// is a stylistic choice, not a performance one.
    float waterReflection = 1.0f;

    /// Foam where water meets land, 0 to 1. The one cue that says where a pond
    /// ends; without it a shoreline reads as terrain that happens to be blue.
    float waterFoam = 1.0f;

    /// The moving net of focused light on a lake bed and on everything seen
    /// while submerged, 0 to 1.
    float waterCaustics = 1.0f;

    /// How far the surface bends what is seen through it, 0 to 1.
    float waterRefraction = 1.0f;

    /// Whether the weather cycles on its own. Off freezes it wherever it is,
    /// which is the reference's `doWeatherCycle`. V still forces it either way.
    bool weather = true;

    /// What the world opens with: 0 whatever the cycle says, 1 rain, 2 storm,
    /// 3 clear. Anything but 0 holds until V is pressed.
    unsigned startWeather = 0;

    /// Breaking showers, rain splashes and footstep puffs.
    bool particles = true;

    /// How far the wind bends grass and leaves, as a multiplier on the weather's
    /// own wind. 0 leaves the world still.
    float foliageSway = 1.0f;

    /// Softens hard edges after the tone curve, 0 to 1. **Off by default**: this
    /// world is made of squares, and an anti-aliaser strong enough to soften a
    /// diagonal also softens every texel boundary in it.
    float antiAlias = 0.0f;

    /// Radius in screen pixels for the depth-based darkening where surfaces
    /// meet. Catches what the mesher's baked occlusion cannot: a creature's
    /// feet, a dropped item, two surfaces meeting far from any vertex.
    float contactShadows = 24.0f;

    /// What fraction of the window's resolution the **world** is drawn at, from
    /// 0.5 to 1. The interface is drawn afterwards and is always full size, so
    /// text and inventory slots stay sharp however low this goes.
    ///
    /// The cheapest frame rate there is: everything before the tone map scales
    /// with the pixel count, so 0.75 is a little over half the shading work.
    float renderScale = 1.0f;

    /// How far the falling curtain is drawn, in columns. Kept well inside the
    /// fog, or its edge shows as a ring in clear air.
    unsigned rainDistance = 22;

    /// Highest hardware thread count worth offering, so a settings screen has a
    /// sane upper bound and a corrupt file cannot ask for ten thousand threads.
    static constexpr unsigned kMaxWorkerThreads = 64;

    /// Beyond this the chunk count grows faster than anything can feed it.
    static constexpr unsigned kMaxRenderDistance = 32;

    /// How many curves `tonemap.frag` implements.
    static constexpr unsigned kToneMapperCount = 4;

    /// Off, low, medium, high.
    static constexpr unsigned kShadowQualityCount = 4;

    /// Off, fast, fancy.
    static constexpr unsigned kCloudQualityCount = 3;
};

/// Reads `file`, filling anything missing with defaults for this machine. A
/// missing or unreadable file is not an error: it yields the defaults, and the
/// file is written back so there is something to edit.
Settings loadSettings(const std::filesystem::path& file);

void saveSettings(const std::filesystem::path& file, const Settings& settings);

} // namespace game
