// Where a vertex actually ends up, in one place.
//
// **This exists because it did not, and a swaying plant's shadow stayed nailed
// to the ground.** The wind sway and the water wave were written in
// `triangle.vert`, which serves the G-buffer and the forward pass, and the
// depth-only `shadow.vert` transformed `inPosition` untouched. So every blade
// of grass and every crop leaned in the wind while the shadow it cast kept the
// plant's rest pose - measured at 0.275 blocks of divergence for ordinary
// grass, and 1.65 blocks lateral with 0.41 of lift for a tall stalk. Visible on
// any sunny day in any field.
//
// That is `CLAUDE.md` bug shape #14, a rule that existed, was correct and was
// commented in only one of the two places that needed it. The fix is not a
// second copy in `shadow.vert`: **a duplicated rule that agrees today is the
// same defect deferred.** Both stages call this, so they cannot drift.
//
// Include `frame.glsl` and `waves.glsl` before this. Neither declares a
// sampler, which is what lets the shadow pass include them - see the binding
// ban in `triangle.frag`.
//
// **The two bit positions this file decodes are owned by `packVertexSurface` in
// `engine/include/engine/render/Vertex.hpp`, not here.** Bit 11 is the fluid
// top and bits 12-15 the sway height; both are read below as bare literals,
// and nothing in the build compares them against the packer that wrote them.
// Change either there first - that function's own note names this file and
// `triangle.vert` as its only two decoders, and explains what a silent
// mismatch looks like on screen. Dated 2026-08-19; re-run the search
// `surface >>` / `surface &` across `engine/shaders` rather than trusting it.

/// The vertex's world position once wind and water have moved it.
///
/// `surface` is the packed `inSurface` attribute and `layer` the `inLayer` one;
/// everything this needs to know about the vertex is in those two. Anything the
/// mesher did not mark comes back unchanged, so calling it on ordinary blocks
/// is free of surprises as well as free of cost.
vec3 displacedPosition(vec3 position, uint surface, float layer) {
    // **The water surface actually moves.** Only vertices the mesher marked as
    // sitting on a fluid's top are touched, so a waterfall's foot, the seabed
    // and the sides below the waterline all stay put and the column cannot open
    // a seam. Two vertices at the same world XZ get the same answer whatever
    // chunk or face they came from, which is what keeps it watertight.
    bool fluidTop = (surface & (1u << 11)) != 0u;
    if (fluidTop && frame.animation.x >= 0.0 && abs(layer - frame.animation.x) < 0.25) {
        float height = 0.0;
        vec2 slope = vec2(0.0);
        waterWave(position.xz, frame.water.x, height, slope);
        position.y += height * frame.water.y * kWaveDisplacement;
    }

    // **Grass bends with the wind, it does not wag.** The displacement is
    // always *along* the wind and never through zero: a blade that oscillated
    // about its rest position would read as a metronome. The per-position phase
    // is what stops a whole field moving in unison - and the gust term, which
    // is shared, is what makes it occasionally do exactly that.
    //
    // **The phase must turn slowly across space.** At better than a radian per
    // block, two corners of the same blade land on opposite parts of the cycle
    // and move in opposite directions, which shears the plant apart rather than
    // bending it. A fifth of a radian per block keeps one plant coherent while
    // still separating it from its neighbours a few blocks away.
    //
    // **The phase is a function of the column only.** It used to include the
    // vertex's height, which put the top of a three-block stalk a third of a
    // radian out of step with its own base - so the plant twisted along its
    // length instead of leaning. Everything in one column now shares a phase,
    // which is what makes a tall plant move as one thing.
    //
    // The phase reads `position.xz`, which the wave above cannot have changed -
    // it only ever moves `y`. The two are order-independent, and were when they
    // lived in `triangle.vert` too.
    //
    // How far the vertex moves is `lean`: its height above its own root, in
    // blocks, so the base of a stalk holds still and the tip travels furthest.
    uint swayUnits = (surface >> 12) & 15u;
    if (swayUnits != 0u && frame.wind.z > 0.0) {
        float lean = min(float(swayUnits) * 0.5, 6.0);
        float phase = dot(position.xz, vec2(0.21, 0.29));
        float sway = 0.5 + 0.5 * sin(frame.wind.w * 2.1 + phase);
        sway += 0.25 * sin(frame.wind.w * 3.7 + phase * 1.9);
        position.xz += frame.wind.xy * frame.wind.z * sway * lean;
        // A little lift with the bend, or the tip visibly shortens as it leans.
        position.y += frame.wind.z * sway * 0.25 * lean;
    }

    return position;
}
