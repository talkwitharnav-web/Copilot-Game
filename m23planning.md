# M23 PLANNING — Renderer restructure: PBR, deferred, HDR

**Written 2026-08-09 from seven parallel research passes** — four reading the codebase, three reading the
web. Nothing was built or changed; this is the plan, not the work.

**Read this before touching `engine/render/`.** `START-HERE.md` §3 says where the game is;
`SYSTEM_MEMORY.md` "Rendering Approach" says how it draws today; this file says what M23 changes,
in what order, and what will silently break if the order is wrong.

---

## 0. The one-page version

**What M23 is:** the renderer stops shading a pixel the moment it draws it, and starts writing what the
surface *is* into a set of screen-sized scratch images (a **G-buffer** — plain English: a notebook with
one line per pixel saying "here is the colour, here is which way it faces, here is what it is made of").
A second pass then reads that notebook and works out the lighting for every pixel at once. On the way it
gains **HDR** (brightness values allowed to go above 1.0, so a torch can genuinely be brighter than white
paper) and **tone mapping** (the curve that squashes those values back into something a monitor can show).

**What M23 is not:** it is not a speed-up. The game currently spends **0.5–1.4 ms of GPU time out of an
8.33 ms budget** at 120 fps. It is not GPU-bound and will not become faster. Deferred rendering is an
*enabler* — it puts depth, normals, roughness and material id into textures that **M24 (shadows),
M25 (sky), M26 (water), M29 (post) and M30 (ray tracing) all need**. Frame time will go **up**, probably
to 1.5–2.5 ms. If the milestone is judged on fps it will look like a failure. Judge it on what it unlocks.

**The single biggest constraint, and it decides half the plan:** there are **no normal maps, no roughness
maps, no metallic maps and no emissive maps anywhere**, and the reference dump contains **zero PBR data** —
verified during this audit by searching all 25,981 files under `reference/` for `*_n.png`, `*_s.png`,
`*_mer.png`, `*_normal.png`, `*_heightmap.png` and `texture_set.json`. Seven glob patterns produced four
hits and all four are the enchanting table's Standard Galactic Alphabet particle glyphs (`sga_n.png` is
the letter *n*). `texture_set.json` — Bedrock RTX's material manifest, which a PBR pack cannot exist
without — has **zero hits anywhere**. The dump is Java's tree, and Java has never shipped PBR maps.
On top of that, the artist is still learning UV mapping and has not finished the *albedo* set.

So M23's material system must work from **albedo-only art plus tables the codebase already owns**. Any
plan that needs 1,444 hand-authored 16×16 maps is dead on arrival — **and the project's own standing rule
("stage the reference, prove the layout, then author") cannot be followed here, because there is nothing
to stage.** That is a structural problem, not a scheduling one.

**The six slices, each shippable on its own:**

| Slice | Name | Visible? | Risk | Ship on its own? |
|---|---|---|---|---|
| **M23a** | Per-frame UBO; push constants back under 128 bytes | No | Low | **Yes** |
| **M23b** | Offscreen HDR target + full-screen blit + tone mapping + bloom | **Yes, a lot** | Low | **Yes — a complete milestone** |
| **M23c** | The material table (layer-keyed) + emissive | **Yes** | Low | **Yes** |
| **M23d** | UI moves to its own pipeline, depth test off, drawn after tone mapping | No | Medium | **Yes** |
| **M23e** | Vertex packing + real normals; write the G-buffer alongside the forward pass | No | Medium | **Yes** |
| **M23f** | Deferred lighting pass + forward translucent pass + GGX BRDF | No (target: identical) | **High** | **Yes** |

**If time runs out, stopping after M23b or M23c is a real, defensible, visibly better milestone.**

---

## 1. Where the renderer actually is, in numbers

Everything here was measured or read out of the code during the audit. These are the facts the plan is
built on; if one of them changes, re-check the decision that rests on it.

### 1.1 Structure

| Thing | Count / value | Where |
|---|---|---|
| Graphics pipeline objects | **1** | `engine/src/render/GraphicsPipeline.cpp` |
| `VkRenderPass` / `VkFramebuffer` objects | **0** — dynamic rendering only | `Renderer.cpp` `recordCommands` |
| Descriptor sets | **1**, with **4** bindings, all `sampler2DArray` | `Renderer.cpp` `createDescriptorResources` |
| Shader files | **2** (`triangle.vert`, `triangle.frag`) | `engine/shaders/` |
| Colour attachments | **1** — the swapchain image itself | `recordCommands` |
| Compute pipelines | **0** — nothing binds `VK_PIPELINE_BIND_POINT_COMPUTE` | — |
| Post-processing passes | **0** | — |
| Frames in flight | **2** | `Renderer.hpp` `kFramesInFlight` |
| Draw categories in one `vkCmdBeginRendering` | **7** | opaque, sky, translucent, overlay, screen, clipped screen, top screen |

### 1.2 Formats and limits

| Thing | Value | Consequence |
|---|---|---|
| All four texture arrays | `VK_FORMAT_R8G8B8A8_SRGB` | **Colour space is already correct.** Lighting maths already runs in linear space. This is a genuine head start — most projects have to fix this first and we do not. |
| Swapchain | `VK_FORMAT_B8G8R8A8_SRGB`, `MAILBOX` present mode | The hardware does the linear→sRGB encode on write. **Do not add a manual `pow(c, 1/2.2)` as well** — that double-encodes and washes the image out. |
| Depth | `VK_FORMAT_D32_SFLOAT`, `storeOp = DONT_CARE`, **no `SAMPLED_BIT`** | A lighting pass cannot read depth until all three of those change together. |
| Sampler | `NEAREST` mag+min, `LINEAR` mip, `REPEAT`, **no anisotropy** | `NEAREST` is the blocky look and must stay. `REPEAT` is what makes greedy meshing's tiled UVs work. |
| Push constants | **144 bytes** (mat4 + 5 × vec4) | Vulkan guarantees only 128, and **13.6 % of Windows devices report exactly 128** (vulkan.gpuinfo.org). The device-suitability filter in `VulkanContext.cpp` currently *refuses to start* on those. |
| Spare push-constant bytes | **4** (`sunDirection.w`) | Nothing new fits. Anything M23 adds per-draw needs a buffer. |
| Vertex | **40 bytes** — `vec3 position`, `vec4 color`, `vec2 uv`, `float layer` | All four colour channels are already spoken for. |
| Device features enabled | **exactly two**: `dynamicRendering`, `shaderDemoteToHelperInvocation` | No `synchronization2`, no `descriptorIndexing`, no `samplerAnisotropy`, no `independentBlend`, no `depthBias*`. |

### 1.3 Cost

| Measurement | Value | Source |
|---|---|---|
| Triangles drawn, render distance 12, after fog cull | **1.04 M** | per-second log |
| Draw calls, same | **235** | per-second log |
| Before fog culling | 1.30 M / 308 | M20s |
| GPU time | **0.5 – 1.4 ms** | timestamp queries |
| Frustum cull removes | ~70 % of draws | `SYSTEM_MEMORY.md` "Culling" |
| Peak process RAM | 328 MB | `SYSTEM_MEMORY.md` |
| Live `vkAllocateMemory` at render distance 12 | **≈ 3,000** | derived: 2 buffers/mesh × ~1,500 meshes |
| Vulkan's guaranteed `maxMemoryAllocationCount` | **4,096** | spec minimum |

> ⛔ **That last pair is a real, unreported hazard.** One `vkAllocateMemory` per buffer, two buffers per
> mesh, up to two meshes per chunk. At render distance 12 we sit at roughly 3,000 live allocations against
> a spec floor of 4,096; at render distance 16 we are **over it**. Nothing in the codebase reads
> `maxMemoryAllocationCount`. It has never bitten because this machine's NVIDIA driver reports a very
> large number. **Add the check in M23a (five lines, logs a warning) even if the fix waits.**

### 1.4 The texture layer space

**722 layers**, and the breakdown matters because it is what a material table is keyed on:

| Kind | Layers |
|---|---|
| Block surfaces | **452** |
| Item / entity sprites (incl. 57 spawn eggs) | 204 |
| Animation frames (32 water + 32 fire) | 64 |
| Utility (`White` = 5, `Sun` = 16) | 2 |

**452 is the real answer to "how many distinct block surfaces exist"** — not 1094 (blocks) and not 722.

---

## 2. The seven decisions

Each of these was contested during research. The reasoning is recorded so a future session does not
re-litigate it, and so it can be *overturned* deliberately if one of the underlying facts changes.

---

### Decision 1 — **Deferred for opaque world geometry. Not a visibility buffer. Not clustered forward.**

**Deferred**, because every shipped voxel renderer with modern lighting is deferred:

| Engine | Architecture |
|---|---|
| Minecraft Bedrock / RenderDragon "Vibrant Visuals" | **Deferred, PBR** |
| Minecraft Java + Iris/OptiFine shader packs | Deferred for opaque, forward for translucent — baked into the program order |
| Teardown (a voxel game, OpenGL 3.3, no compute at all) | **Deferred** |
| Project Ascendant (Vulkan voxel engine, vkguide author) | **Deferred**, explicitly to unify the lighting maths across five geometry systems |
| DOOM 2016 (not voxel, but the canonical hybrid) | Clustered forward **plus two thin G-buffers via MRT** |

The vkguide author's stated reason maps exactly onto us: *"there are 5 different rendering systems with
different properties, I decided to move the engine to a deferred rendering scheme. That way I only need to
care about writing the gbuffer, and **the lighting math is unified**."* We have seven draw categories.

**Not a visibility buffer.** Its win is not paying to fetch textures for pixels that get covered up later.
**Our textures are 16×16 in an array — they fit in cache and cost essentially nothing to sample. It
optimises the thing we do not spend money on.** It would cost bindless descriptors, a global vertex/index
buffer, hand-written texture-detail (mip) selection, and the permanent death of MSAA. Wicked Engine's
author — who built one — reports it is *"slower on weaker GPUs"*, and this machine has an Intel Arc iGPU.

**Not clustered forward.** Clustering is a technique for culling *many lights*. We have **one** light (a
fake sun). Torches are not lights here — they are a baked 0–15 number per block. Revisit at M24+.

> **Honest caveat, stated up front:** deferred's famous benefit is "cost per lit pixel instead of cost per
> light per object". **That buys us nothing today**, because we have one light. Mojang hit the same thing
> and solved it by *keeping* the baked light model rather than converting torches into point lights. Our
> benefit is (a) unified lighting maths and (b) depth/normal/material available as textures for
> M24 onward. Both real; neither is frame time.

---

### Decision 2 — **A voxel-specialised 8-byte G-buffer, not a conventional 24-byte one**

Three facts are true of this codebase and almost no one else's, and each one deletes a chunk of a
conventional G-buffer:

1. **Every world face normal is one of exactly six axis-aligned directions.** That is **3 bits**, exactly,
   not a 16-bit octahedral approximation. The academic voxel paper *Aokana* reached the identical
   conclusion independently: *"Since we assume that voxels are axis-aligned, we only need 3 bits to store
   the normals."* An octahedral normal would be a **worse approximation of a value we already know
   perfectly**, at five times the storage.
2. **Material properties are per *texture layer*, not per pixel.** ~722 layers means an **11-bit index**
   into a small table replaces two whole 8-bit channels of roughness/metallic *and* leaves room for six
   more properties for free.
3. **Baked light is already quantised to 0–15 by the game rules.** Sky and block light are **4 bits each**,
   exactly. Storing them as two 8-bit values would be storing four bits of zero, twice.

**The layout:**

| Target | Format | Bytes/px | Contents |
|---|---|---|---|
| **GB0** | `VK_FORMAT_R8G8B8A8_SRGB` | 4 | RGB = albedo (hardware sRGB decode on read — free and correct). **A = sky light in the high nibble, block light in the low nibble** |
| **GB1** | `VK_FORMAT_R32_UINT` | 4 | bits 0–10 material id (2048 materials) · bits 11–13 face normal (6 of 8 codes used, **7 = "non-axis-aligned, see spare bits"**) · bits 14–17 ambient occlusion (16 levels — ours is already a 4-step enum) · bits 18–19 surface class (world / entity / foliage / fluid) · **bits 20–31 spare** |
| **Depth** | `VK_FORMAT_D32_SFLOAT` | 4 | Reused from today, plus `SAMPLED_BIT` and `storeOp = STORE` |
| **sceneColor** | `VK_FORMAT_R16G16B16A16_SFLOAT` | 8 | HDR lighting accumulation |

**Memory at 1080p: GB0 8.3 MB + GB1 8.3 MB + depth 8.3 MB + sceneColor 16.6 MB + bloom chain ~5.5 MB
≈ 47 MB.** At 1440p ≈ 84 MB. On 8 GB of VRAM this is nothing. Write bandwidth ≈ 2 GB/s at 120 fps
against ~256 GB/s available. **The G-buffer is not the memory risk; the chunk mesh buffers are.**

**The escape hatch, designed in on day one rather than discovered at M25:** normal code `7` means "not
axis-aligned". Creature limbs are rotated boxes and plant blades sit at 45°, so they take code 7 and put a
reduced-precision (6+6 bit) octahedral normal in GB1's spare bits.

**Why not `R11G11B10_UFLOAT` for sceneColor** (it would halve the bandwidth): it has **no sign bit**, an
uneven 6/6/5 mantissa split that causes visible yellow/blue discolouration on smooth gradients, and
roughly **twice the error of 8-bit sRGB near 1.0**. Bart Wronski's analysis concludes it is fine *"if you
don't need alpha blending or filtering"* — and we alpha-blend into it (water, translucent shells).
**Use `R11G11B10` for the bloom mip chain only**, where nothing blends.

**Why not store the texel address instead of the albedo** (a "material buffer"): greedy meshing tiles UVs
`0→N` with a repeating sampler, so reconstructing which texel was hit needs `fract(uv)` at sub-texel
precision; and moving the texture fetch to a full-screen pass **loses the hardware's automatic mip
selection**, which then has to be reconstructed by hand. It would save 2 bytes per pixel. Not worth it.

---

### Decision 3 — **Materials are a per-texture-layer table, derived from tables we already own. Zero new art.**

This is the decision the milestone lives or dies on, and the evidence is one-sided.

**Mojang ship exactly this as a first-class authoring path.** Bedrock's `texture_set.json` accepts
`"metalness_emissive_roughness": [130, 135, 140]` or a hex colour instead of a texture, and the docs say
plainly: *"values can instead be specified in the `*texture_set.json` file, which is effectively the
equivalent to referencing a texture image filled uniformly with that value."* **That is the reference
implementation endorsing per-material constants over per-texel maps.**

Supporting evidence, all primary-source:
- **NVIDIA's own Minecraft PBR guide:** *"A well-made metallic map will usually contain black and white
  pixels only."* A per-block binary flag loses nothing.
- **Filament (Google's PBR engine):** *"This property should be used as a binary value, set to either 0 or
  1. Intermediate values are only truly useful to create transitions between different types of surfaces
  when using textures."* At 16×16 with one material per block there are no transitions.
- **Filament again, on reflectance:** *"Reflectance should be set to 127 sRGB (0.5 linear, 4 % reflectance)
  if you cannot find a proper value."* A per-material scalar is the documented normal case.
- **LabPBR's conformance floor is two channels** — smoothness and F0. Ambient occlusion, height, porosity,
  subsurface and emission are all **explicitly optional**, and packs must work without them.
- **AutoPBR, the most advanced Minecraft auto-PBR generator that exists**, is fundamentally *a
  hand-written colour → material rule table* with nearest-colour lookup in perceptual colour space, plus
  per-texture manual overrides, plus a neural net offered as a *blend against the table*. **If pure image
  analysis worked, none of that would be there.**
- **Bedrock's "Vibrant Visuals" deferred renderer looks like a large upgrade on vanilla textures that have
  no PBR maps at all**, because almost all the gain came from *lighting*, not materials.

**And auto-generation is actively harmful here.** Deriving normals from luminance is a category error:
brightness in hand-painted art encodes *albedo* and *painted-in shading*, not height. Bedrock's own docs
say *"typically there is lighting baked in the colour image"* — so a derived normal double-counts the
artist's highlight and the surface reads as plastic. At 16×16 a Sobel kernel's support is 3 texels out of
16. Realistic assessment: **usable on ores, bricks, planks and cobblestone; actively wrong on grass, wool,
leaves, sand, concrete and glass.** Roughly a 30/70 split against us, and "wrong" is worse than "absent" —
absent looks like a clean stylised game, wrong looks like a bug.

#### 3.1 The structure

```
MaterialFamily            an enum, ~16 values, hand-authored
        │                 — written on the exact pattern of soundMaterialFor(), which already
        │                   covers 1,094 blocks with 10 values and a dozen family questions
        ▼
materialFamilyFor(shapedParent(id))     ← 640 cut shapes inherit for free
        ▼
MaterialProperties[16]    constexpr, hand-tuned, tiny
        ▼
    + per-face overrides for the handful that genuinely differ
      (grass top vs side, furnace front vs top, lit smoker mouth)
    + emissive = blockLightEmission(id) / 15.0     ← already authored, already Bedrock-accurate
        ▼
722-row layer LUT, built at startup by walking every (BlockId, BlockFace, FaceDirection)
        ▼
uploaded once as a 722×1 R8G8B8A8_UNORM image
        ▼
fragment shader: texelFetch(materialLut, int(layer), 0)
```

**Total ≈ 250 lines, of which ~85 is engine plumbing. Zero images. Zero vertex-format change. Zero
re-mesh of any chunk.** Re-tuning a roughness value afterwards is a **texture upload**, not a rebuild of
800 chunks — which is what makes it survivable for a solo developer.

#### 3.2 Why keyed on **texture layer**, not `BlockId`

Four reasons, and the first is decisive:

1. **A block does not have one material.** Grass is soil underneath, living surface on top, both on the
   side — three layers, one `BlockId`. The furnace is stone plus a glowing mouth. Chest, smoker, smithing
   table, sandstone, every log and every beehive differ per face. **This is the exact shape of the bug that
   once put a furnace's mouth on all four sides.**
2. **The fragment shader has no `BlockId`. It has `fragLayer`, already, as a `flat` varying.** Getting a
   block id into the shader means a vertex-format change, a mesher change, and a full re-mesh.
3. **The merge key already contains `layer`** (`other.layer == sample.layer`), so a layer-keyed material
   **cannot fragment greedy meshing at all**. A `BlockId`-keyed one would have to join the key and would
   fragment wherever two blocks share a texture.
4. **It covers items, entities and animation frames with the same key** — and because the animated-layer
   redirect rewrites `layer` *before* the texture sample, animated frames get their own material for free.

The one thing to guard: add a startup check that no layer is claimed by two different material families,
and **fail loudly** rather than silently picking one. This is the project's own most common bug shape.

#### 3.3 The row

```cpp
// 4 bytes per layer, uploaded as a 722×1 R8G8B8A8_UNORM image.
// R = perceptual roughness   G = metallic   B = emissive strength   A = flags
```

| Column | Derivable from what already exists? | What is left to author |
|---|---|---|
| **emissive** | **YES — completely.** `blockLightEmission(id) / 15.0`. Nineteen emitters, levels 3–15, already Bedrock-accurate, already the single owner of that fact. | **Nothing.** The single biggest free win in this milestone. |
| **metallic** | ~95 %. The metal set is already enumerated three times: `harvestTier`'s copper family switch, `isOre`/`isDeepslateOre`, and `kExtraBlocks` names ("Block of Iron/Gold/Copper…", "Iron Bars", every `*_copper`). | ~25 layers get 1.0, listed once. ~20 lines. |
| **roughness** | Mostly, from a 16-value `MaterialFamily` on the `soundMaterialFor` pattern, refined by `isPlanksBlock` / `isWoolBlock` / `isLeafBlock` / `isIce` / `isOre` and — free — the **"Polished" / "Smooth" / "Cut" name prefixes already in `kExtraBlocks`**. | ~16 family values + ~40 overrides. An afternoon. |
| **f0 / reflectance** | Default 0.04 is physically right for every dielectric and needs no authoring. Glass/ice/water want 0.05–0.08 and `isIce` / `isTranslucent` already name them. | ~6 values. **Fold into the family table; do not give it a column in v1.** |
| **flags** | `bit0 foliage` from `blockShape == Cross \|\| isLeafBlock`; `bit1 translucent` from `isTranslucent`. | Free. Reserve the bits now — M25 wants the wind flag and retrofitting a bit is expensive. |

> ⛔ **Do NOT add an ambient-occlusion column.** AO already has an owner: `kOcclusionSteps` in
> `ChunkMesher.cpp`, baked into the vertex blue channel. A second copy is this project's most-repeated bug.

> ⛔ **Do NOT add an emissive *colour* column.** Both LabPBR and Bedrock multiply emission by the albedo —
> *"the colour of the glow is determined by the pixels in the base colour map."* A torch's albedo already
> is its glow colour. Three bytes and a hand-authored colour per emitter, for zero visible gain.

#### 3.4 The one correction that must not be missed

`isFurnaceLit` covers **smokers too — sixteen ids** — and **only the front face should glow**, not the top
and sides. A block-keyed emissive would light the whole box. A layer-keyed one gets it right for free,
because `FurnaceFrontLit` (22) and `kSmokerFrontLitSprite` (190) are different layers from `FurnaceTop`.
**This is the concrete proof that the layer key is the correct one.**

---

### Decision 4 — **Khronos PBR Neutral as the default tone mapper. Not ACES.**

**The killer piece of evidence:** Bedrock's Vibrant Visuals lets a resource pack choose from six tone
mapping operators — `reinhard`, `reinhard_luma`, `reinhard_luminance`, `hable`, `aces`, and **`generic`**,
which Mojang describe as *"a generic tone mapping curve that has been hand-tuned by Mojang's artists…
**preserves a bit more hue saturation at high luminance regions**"*. **Mojang shipped ACES, then built and
shipped a custom curve specifically to avoid it**, and their own sample packs use `reinhard_luminance` and
`generic` — **never `aces`**.

**Why ACES hurts this art specifically.** From the ACES documentation itself: *"With highly saturated
colors… hues shift from primaries to secondaries with increased luminance. **Red turns yellow, green turns
cyan, and blue turns magenta.**"* And from the author of PBR Neutral: *"canary yellow, bright greens and
blues are all impossible to output to the screen"* under ACES. **Minecraft-style 16×16 art is a
canary-yellow-and-bright-green problem** — grass, lapis, emerald, redstone, gold, wool in sixteen dyes,
glazed terracotta, concrete. They are near-primary sRGB colours chosen *because they are recognisable*,
and the player knows exactly what a grass block looks like.

**Khronos PBR Neutral's stated design goal reads like a description of this game:** *"get sRGB colors in
the output render that match as faithfully as possible the input sRGB baseColor under gray-scale lighting…
aimed toward product photography use cases, where the scene is well-exposed and HDR color values are mostly
restricted to small specular highlights."*

Its guarantees:
- **Any base colour with sRGB components below 231 is reproduced exactly** under even white lighting.
- **No hue shifts anywhere in the domain.**
- **No luminance weights** — it scales colours by a scalar, preserving hue and saturation while reducing
  brightness.
- Continuous derivatives everywhere, and analytically invertible.
- Still gives the path-to-white for genuine highlights.

Twelve lines, three divides, no matrices, no lookup tables. Adopted by three.js, Filament and
`<model-viewer>`.

```glsl
// Khronos PBR Neutral — the complete implementation.
vec3 toneMapPbrNeutral(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) { return color; }

    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3(1.0), g);
}
```

**Its one honest weakness**, stated by its own author: compression only begins at 0.8, leaving 0.2 of range
for *all* highlights, so it crushes detail *inside* very bright regions harder than ACES does. Whether that
matters depends on whether you want to see structure inside a lava lake or just want it to read as "very
bright orange". If it becomes a problem, Filament's `GenericToneMapper`
(`x = pow(x, contrast); out = outputScale * x / (x + inputScale)`) is the tunable escape hatch.

**Ship four operators behind a setting** — PBR Neutral (default), Hable/Uncharted 2 (what GTA V and
DOOM 2016 shipped), Reinhard-luminance (what Mojang's sample used), ACES-fitted — and **put three of them
in front of the user side by side and let them choose.** `CLAUDE.md` records the M14c lesson: tuning a
visual effect alone and calling it "subtle" reads to the user as broken.

> ⛔ **No auto-exposure. Manual exposure with a slider.** In a block game the player constantly enters and
> leaves caves, so auto-exposure means the brightness of a torch-lit cave depends on where you were looking
> two seconds ago. Worse: **light level 0–15 is a game mechanic** (mob spawning, crop growth), and a
> floating exposure makes it unreadable from the screen. This is a gameplay failure, not a visual one.

---

### Decision 5 — **Baked block light gets diffuse only. No specular from it. Build the handheld light instead.**

The hard question: PBR specular needs a light **direction**, and baked block light is a scalar per vertex
with no direction and no position. Four options were evaluated:

| Option | Verdict |
|---|---|
| **(a) Treat baked light as pure irradiance, no specular from it** | **DO THIS.** It is what every shipping Minecraft shader does by default. Photon's deferred pass adds a specular highlight from the **sun/moon only**. |
| (b) Reconstruct a direction from the light field's gradient | **SKIP — broken by our mesher.** `dFdx` of a per-vertex-interpolated scalar is *constant across a triangle*, and our greedy-meshed quads can be 16 blocks wide, so the "gradient" is one value for the whole quad. Minecraft's mesher does not merge quads, which is why it works there. Photon ships it **disabled by default** even so. |
| (c) Real clustered point lights / light propagation volume for nearby emitters | **DEFER.** The biggest remaining visual win after shadows, and a whole system on its own. Photon ships it off by default. |
| (d) Emissive blocks as actual light sources (Minecraft RTX) | Not M23 — that is a path tracer. |

**But do build the handheld light.** One analytic point light at the player's position, with a real
position and therefore a real specular lobe. Photon has it **on by default**, it is about fifteen lines,
and it single-handedly answers "there's no specular indoors":

```glsl
float falloff = lift(1.0 / (dot(scenePos, scenePos) + 1.0), 3.0);
return lightColour * falloff * mix(ao, 1.0, falloff * falloff) * kHandheldIntensity;
```

**And steal one free line from option (c) without building it:** sample the light grid at
`voxelPos + normal * 0.5` — half a voxel *along the surface normal*. It costs nothing and is the cheapest
possible directionality on baked light: a face pointing toward a torch gets the brighter cell.

**The M23 lighting set, in full:**

| Light | Diffuse | Specular | Notes |
|---|---|---|---|
| Sun / moon | ✔ Lambert × (M24's shadow) | ✔ full GGX | one directional light |
| Sky ambient | ✔ modulated by baked sky light 0–15 | ✔ (M25) | analytic; no cubemap — see Decision 7 |
| **Block light** | ✔ falloff curve × colour × scale | ✘ **none** | correct, not a compromise |
| **Handheld** | ✔ | ✔ **full GGX** | **build at M23** |
| Emissive | — | — | additive, before bloom |

**Emissive units and the five rules that stop everything looking like neon:**

Photon uses `emission_scale = 40.0` against `blocklight_scale = 6.0` and skylight around 1.0 — so emissive
surfaces sit **~40× above diffuse mid-grey**, clipping to white in the core and blooming outward. Suggested
ratios (not nits — ratios are all that matters before tone mapping): torch 25–60, glowstone 40–80, lava
60–120, sea lantern 50, redstone torch 8–15 (deliberately dim), magma 20, campfire 60.

1. **Mask it.** A torch's stick is not emissive. Derive the mask from albedo luminance or hue — Photon does
   exactly this for lava and redstone with no map at all.
2. **Use a thresholdless bloom pyramid.** A hard luminance threshold is *the* cause of the neon look,
   because everything above the bar gets the same halo and a torch looks like the sun.
3. **Tone-map after adding emission.** The curve is what converts "40× mid-grey" into a white core with a
   coloured fringe — that desaturation-toward-white is the visual signature of a real light, for free.
4. **Cap the sun's specular** (Photon: `specular_max_value = 4.0`) so a highlight cannot overload bloom.
5. **Vary the values.** If torch, lamp, lava, glowstone and campfire all sit at 40, they all read as the
   same "glow material". The spread is what makes them read as different things.

---

### Decision 6 — **The UI moves to its own pipeline, with the depth test OFF, drawn after tone mapping**

**This is the highest-value cleanup in the entire milestone**, and it is the one place M23 makes the game
*structurally* better rather than merely more capable.

**The problem, in one line:** the fragment shader's `discard` is gated on `push.lighting.z > 0.5`, which is
only true for **world** geometry — so **there is no alpha test anywhere on the HUD at all**, and every
transparent texel of every HUD quad writes depth and rejects whatever is drawn later behind it. That single
fact has caused **three separate shipped bugs in two days**, and the audit found a **fourth, still live**.

The three already paid for:
- The stack on the cursor punched a rectangular hole through the catalogue → fixed by inventing
  `setTopScreenMesh`, a whole third screen layer whose only purpose is draw order.
- A stair icon's two boxes fought, so the slab's lit top face won over the step standing on it → fixed by a
  per-box painter's key of `0.00002`, whose margin against the hotbar count is **exactly 0.00002**.
- Every letter after the first lost its left two columns → fixed by cropping glyphs to their advance.

**The fourth, found during this audit and not yet reported:**

> ⛔ **With the inventory open AND F5 on, the debug overlay has no panel behind it.** The inventory's
> full-screen dim quad sits at depth **0.0044**; the overlay's panel, hairline edge and graph background sit
> at **0.0045 / 0.0050 / 0.0052**, and the overlay is appended to the *same mesh* afterwards. Under
> `VK_COMPARE_OP_LESS` the dim quad wins, so only the text (0.0035) and graph ink (0.0040) survive. You get
> floating numbers over the dimmed world. **This has presumably always been true and nobody has looked.**

#### The three options, costed for this codebase

| | **A — own pipeline, depth OFF, sorted** | **B — own pipeline, own depth target** | **C — leave it exactly as is** |
|---|---|---|---|
| New lines | ~135 (~30 deleted) | ~120 | ~30 |
| HUD builder changes | **A back-to-front sort in `rebuildHud`** | **none** | none |
| Kills the depth-punch bug class | **Permanently** | No — stays latent | No |
| Fixes the F5 overlay bug | **Free** | No | No |
| Survives reverse-Z | **Yes** | **Yes** | **NO — catastrophic** |
| UI isolated from HDR/bloom/tone map | **Yes** | **Yes** | No |

**Take A.** The reason is not elegance: **the depth-band table is already a correct back-to-front order** —
forty-odd values, all distinct within each coexisting set, hand-assigned with comments explaining each. The
sort does not invent an ordering, it **executes the one that is already written down and reviewed**, and it
removes the depth buffer from the equation entirely. That converts this project's most-repeated bug shape
from "will happen again" to "cannot happen". And it is verifiable: assert that after sorting, `z` is
non-increasing.

**If the milestone runs long, ship B first** (it touches no game code at all) and convert to A later by
adding the sort and deleting the depth attachment. **B → A is a clean isolated change. C → A is not.**

> ⛔ **Do not take option C if there is any chance of reverse-Z.** Under reverse-Z with `GREATER`, the
> entire forty-band depth table inverts and the HUD renders backwards — the dim quad in front of the panel,
> the tooltip behind everything. A spectacular and confusing failure.

**The second reason A is worth it:** it lets the world pipeline **stop being an über-shader**. Three of the
four negative-layer branches (HUD sheet, font atlas, and the `hudTexture`/`fontTexture` bindings) move out
of the world's fragment shader entirely, and alpha blending can finally be turned off for the G-buffer pass.

---

### Decision 7 — **What we deliberately do NOT build at M23**

| Not building | Why | Revisit |
|---|---|---|
| **Normal maps** | No art exists and none can be authored soon. Every face is perfectly flat and axis-aligned already, so the whole payoff of a normal map — breaking up a large flat surface — is already delivered by *actual geometry* every 16 texels. **Build the infrastructure and leave the slot empty:** a `normalLayer` field with a sentinel meaning "flat". | When the artist has finished albedo and wants to |
| **Parallax occlusion mapping** | **Incompatible with greedy meshing as written.** Our merged quads use UV 0→N, so a parallax ray marches straight across tile boundaries into the *next block's* heightfield. Fixing it means `fract()`-ing per step and still leaves flat silhouettes at the quad border. Disabling the merge to fix it triples the triangle count. Also 16–64× texture bandwidth on near geometry. | Probably never |
| **Cubemap image-based lighting** | Our sky is procedural. Project it into **order-2 spherical harmonics** once per frame instead — 9 `vec3` in a uniform buffer, one tiny compute dispatch — and sample a small sky-view lookup texture along the reflection vector for specular. Photon (a Minecraft shader) already does exactly this with `SH_SKYLIGHT`. Keep only the static 128×128 BRDF lookup table. | M25 |
| **Clustered / tiled light culling** | Clustering culls many lights. We have one. | When torches become real lights |
| **Bindless / descriptor indexing** | One texture array already avoids per-draw descriptor binding. Adding it now is speculative structure, which `CLAUDE.md` forbids. | If a visibility buffer or GPU-driven rendering ever lands |
| **GPU-driven rendering / indirect draws** | 235 draw calls. CPU culling is fine well past 1,000. | Much later |
| **MSAA** | Deferred + MSAA is genuinely painful (per-sample lighting). TAA supersedes it and is the standard partner for deferred. | M29 |
| **`VK_KHR_dynamic_rendering_local_read`** | It restores on-tile subpass reads. **Khronos say plainly the benefit is for tiling GPUs**; we target a discrete NVIDIA card. Windows coverage is 69.33 % versus 80.30 % for plain `dynamic_rendering`. Our G-buffer is 8 bytes/px — we would save ~2 GB/s out of 256. | Never, unless we ship to mobile |
| **Weighted-blended OIT** | Its depth weighting is tuned for many thin layers (hair, smoke) and gives visibly wrong colour for a few large flat quads at very different depths — which is exactly what voxel water is. Keep it in the pocket for particles. | M28 |
| **Dithered / stochastic transparency** | Genuinely the best answer (Teardown proves it — transparents go through the *same* deferred path, one shading path, no duplication) **but it requires TAA**, which is not in M23. | The moment TAA lands |
| **Multi-scatter energy compensation** (Fdez-Agüera) | Eight lines, free once IBL exists, worth ~nothing before. The 40 % energy loss it fixes is on *rough metals*, and our metal blocks want to be smooth. | With IBL |
| **Burley / Disney diffuse** | Filament ships Lambert at every quality tier and states the extra cost *"does not justify the slight increase in quality"*. The retro-reflection it adds is visible only on rough curved surfaces at grazing angles. Ours are flat axis-aligned quads. | Never |
| **Biome tinting** | The tints are **baked into the staged PNGs**, so albedo is already final. ⚠ **But decide this before eyeballing any roughness value** — un-baking tints later means re-deriving values that were tuned against tinted art. | Ask the user |

---

## 3. The slice plan

Six slices. **Every one compiles, runs, and looks the same or better.** Each names its own acceptance test.

---

### M23a — Plumbing (no visual change)

**Goal:** get the per-draw data block back under Vulkan's guaranteed 128 bytes, before anything else wants
to add per-frame state — and everything after this does.

**Why first:** we currently push **144 bytes** and **80 of them are identical for every draw in the frame**
(sun direction, lighting, animation, fog, eye). That is 80 bytes re-pushed 235+ times a frame for no
reason, *and* it means the game refuses to start on the 13.6 % of Windows devices that report exactly 128.

**Steps:**

1. New `engine/src/render/FrameUniforms.hpp` — a `struct FrameUniforms` holding view, projection,
   viewProjection, `sunDirection`, `lighting`, `animation`, `fog`, `eye`, `time`, `screenSize`, `exposure`,
   `toneMapper`.
2. One `VkBuffer` **per frame-in-flight** (2), `HOST_VISIBLE | HOST_COHERENT`, **persistently mapped**. Two
   frames in flight and single-threaded recording makes this the simplest correct choice — no dynamic
   offsets, no staging, no alignment arithmetic.
3. Add **binding 4** to the existing descriptor set. ⚠ **`createDescriptorResources` writes the literal
   `4` three times** (bindings array, images array, writes array) rather than deriving it from one
   constant. All three must change, and a missed one is a validation error rather than a silent fault.
4. Shrink `MeshPushConstants` to what is genuinely per-draw: the transform, plus a flags word.
   **Target ≤ 80 bytes**, comfortably under the guarantee.
5. **Delete the device-suitability filter** in `VulkanContext.cpp` that rejects GPUs with 128-byte push
   constants — but only after step 4 lands, and keep the runtime check as a backstop.
6. Add a `maxMemoryAllocationCount` check at startup that logs a warning when live allocations exceed
   75 % of the limit (§1.3).
7. Enable the device features M23 will need, **and mirror each one into the suitability test** — otherwise
   a laptop with two GPUs hard-fails instead of falling back: `synchronization2`, `independentBlend`,
   `samplerAnisotropy`, `depthBiasClamp` (M24 wants it), `shaderSampledImageArrayNonUniformIndexing` only
   if actually used.
8. CMake shader rules: add `-MD -MF "<out>.d"` to `glslc` and `DEPFILE` to `add_custom_command`, plus
   `-I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"`, and **flatten the relative path into the output name**
   (today it uses `NAME` only, so `a/common.frag` and `b/common.frag` would silently collide).

> ⛔ **Without the `DEPFILE`, editing a shared `.glsl` header does not retrigger any compile** and the
> build silently ignores your edit. This repo has already been bitten by exactly this shape once, with
> assets — the comment recording it is in `game/CMakeLists.txt`.

**Acceptance:** both presets build clean at `/W4`; debug soak with zero Vulkan validation errors;
**screenshot pixel-identical**; the startup log's push-constant line reports the new smaller size.

---

### M23b — HDR, tone mapping, bloom (**the milestone screenshot**)

**Goal:** the first genuinely modern moment. Torches glow. This slice contains **no deferred rendering at
all** and could be shipped as M23 on its own.

**Steps:**

1. **`sceneColor`** — `VK_FORMAT_R16G16B16A16_SFLOAT` at swapchain resolution. `vkCmdBeginRendering` now
   targets it instead of the swapchain image. Add it to `recreateSwapchain`.
2. **A second `vkCmdBeginRendering`** targeting the swapchain, running a full-screen pass that samples
   `sceneColor`.
   **Use a full-screen *triangle*, not two triangles**, and no vertex buffer at all:
   ```glsl
   // 3 vertices, gl_VertexIndex only. Two triangles create unnecessary helper
   // invocations along their shared edge; one oversized triangle does not.
   vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
   gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
   ```
3. **Tone mapping in that pass**: PBR Neutral, Hable, Reinhard-luminance, ACES-fitted, behind a `switch`
   on a uniform. ~60 lines total. Plus a manual `exposure` multiply before the curve.
4. **Bloom.** Thresholdless mip pyramid: **8 downsamples, 7 upsamples**, `R11G11B10_UFLOAT` mips starting
   at half resolution, Call-of-Duty 13-tap downsample / 9-tap upsample patterns, upsample combining with
   `lerp(current, previous, 0.85)`, composited into the tone-map pass with `lerp(..., 0.1–0.3)` — **not
   an add**, which brightens the whole image and is hard to balance in dark scenes.
   *Measured reference cost: 0.723 ms on an RX 5600 XT at 1080p, which is slower than this machine's GPU.*
   **Sampler detail worth copying:** clamp-to-**border** on downsample, clamp-to-**edge** on upsample —
   found by experiment; it avoids both corner-darkening and edge-overbrightening.
5. **Emissive** — even before the material table lands, wire `blockLightEmission(id) / 15.0` into the
   mesher as a temporary vertex term so bloom has something to bloom. (M23c replaces it properly.)
6. Barriers: `sceneColor` `COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` between the two passes,
   `srcStage = COLOR_ATTACHMENT_OUTPUT`, `dstStage = FRAGMENT_SHADER`,
   `srcAccess = COLOR_ATTACHMENT_WRITE`, `dstAccess = SHADER_READ`. Use `vkCmdPipelineBarrier2` once
   `synchronization2` is on from M23a.

> ⛔ **The clear colour and the distance-fog colour must be tone-mapped identically.** Their being the
> *same value* is what makes the render-distance boundary invisible — `Main.cpp`'s comment says so
> outright. Tone-map one and not the other and a visible ring appears at the edge of the world. **This is
> the single most likely cosmetic regression of the whole milestone.**

> ⚠ **The swapchain is already `_SRGB`.** Write **linear** values from the tone mapper and let the
> presentation engine apply the encode. Do **not** also `pow(c, 1/2.2)` — that double-encodes.

**Acceptance:** both presets clean; zero validation errors; **a side-by-side screenshot of the same scene
through three tone mappers put in front of the user**; frame time measured before and after and recorded.

---

### M23c — The material table

**Goal:** roughness, metallic and emissive exist as data, keyed by texture layer, derived from tables we
already own. Still forward-rendered.

**Steps:**

1. `game/src/world/Material.hpp` — `enum class MaterialFamily` (~16 values) and
   `constexpr std::array<MaterialProperties, 16>`, written on `soundMaterialFor`'s exact pattern.
2. `materialFamilyFor(BlockId)` — a dozen family questions, **forwarding through `shapedParent(id)` first**
   so 640 cut shapes inherit with no rows of their own.
3. A startup loop over every `(BlockId, BlockFace, FaceDirection)` writing into a 722-entry array through
   `blockTextureLayer(...)`, with **a hard failure if two families claim one layer**.
4. Upload as a `722×1` `R8G8B8A8_UNORM` image. **⚠ `TextureArray`'s `kFormat` is a file-scope `constexpr`
   shared by all four arrays — it must become a constructor parameter, or the material LUT is silently
   gamma-decoded and every roughness of 0.5 becomes 0.214.**
5. Binding 5 in the descriptor set. `texelFetch(materialLut, int(layer), 0)` inside the **existing `else`
   branch** of the negative-layer dispatch, so HUD/font/skin skip it automatically.
6. Emissive: `rgb += albedo * emissive * kEmissionScale` before the fog and before bloom.
7. Delete the temporary emissive hack from M23b.

**Acceptance:** a temporary startup probe printing the material row for ~30 named layers (stone, oak
planks, iron block, gold block, glass, ice, torch, glowstone, lava, furnace front lit, furnace top, grass
top, grass side, wool, leaves) — **checked by eye against the table**, then removed. `constexpr` per-block
functions are provable at startup for ~free, and this is exactly the method that found five real bugs on
2026-08-06.

---

### M23d — The UI pass (Decision 6, option A)

**Goal:** the HUD gets its own pipeline with **no depth attachment at all**, drawn straight to the
swapchain after tone mapping.

**Steps:**

1. `GraphicsPipeline`'s constructor becomes a `PipelineDesc` struct. Twelve things are hardcoded today and
   each needs to be a field — see §5.2 for the full list.
2. A second shader pair: **much smaller** fragment shader — three samplers (HUD sheet, font, block array
   for icons), no lighting, no fog, no cutout, no animated-layer redirect.
3. `recordCommands` grows a UI pass: `LOAD_OP_LOAD` on the swapchain image, **no depth attachment**,
   blending on, `depthTestEnable = VK_FALSE`.
4. **The sort.** In `rebuildHud`, stable-sort each of the three screen meshes' quads by vertex `z`
   **descending** (farthest first) before upload. Then
   `assert(z is non-increasing)` — the table in §4 is the proof that a total order exists.
5. Delete the HUD and font branches from the world fragment shader. Delete `fogged = false` from the UI
   draws. **Turn alpha blending off in the world pipeline** (it exists only for HUD panels).
6. **Fix the F5-overlay-behind-the-dim-quad bug** while here — it becomes free once depth is out.

> ⚠ **The UI pass still needs the block texture array bound**, because `TextureLayer::White = 5` is what
> every flat-colour HUD quad samples, and isometric block icons sample real block layers.

> ⚠ **Once `render_scale ≠ 1` exists, three functions that all read `m_swapchain.extent()` today must
> disagree deliberately:** `projectionMatrix()` wants the **render target's** extent; `aspectRatio()`, the
> screen transform and the clipped-mesh scissor conversion all want the **swapchain's**. Getting one wrong
> stretches either the world or the HUD, silently.

**Acceptance:** zero validation errors; **the HUD looks identical**; open the inventory with F5 on and
confirm the overlay panel is now there; drag a glass block over the catalogue and confirm no hole.

---

### M23e — Vertex packing and the G-buffer, written but not yet read

**Goal:** carry a real normal and a material id, halve the vertex, and write the G-buffer alongside the
existing forward output so it can be debugged before anything depends on it.

**Why packing is a requirement, not an optimisation:** `LESSONS.md` records that *"the render distance
limit was never a performance limit… the real ceiling was memory."* At 40 bytes, resident world vertex data
is the dominant memory cost of the renderer, and **12 of those 40 bytes carry at most 4–8 bits of real
information each** (sky light 0–15, block light 0–15, AO from a 4-step enum, alpha). A naive PBR vertex
(`+ vec3 normal + vec4 tangent + uint material`) would take it to **72 bytes — an 80 % increase across the
single largest allocation in the process.**

**The packed vertex:**

```cpp
struct Vertex {
    float position[3];   // 12
    std::uint32_t light; //  4  — sky 8 | block 8 | AO 8 | alpha 8
    float uv[2];         //  8
    std::uint32_t misc;  //  4  — layer 11 | normal 3 | class 2 | flags 16
};                       // 28 bytes, down from 40
```

*(20 bytes is achievable with 16-bit UVs; 28 is the safe first cut. Decide with a measurement.)*

**Steps:**

1. Change `Vertex`, `Vertex.cpp`'s attribute descriptions, and both shaders — **in one commit-sized
   change, then regenerate every mesh.** Do not do this incrementally.
2. **Lift `kFaces` out of `ChunkMesher.cpp`'s anonymous namespace into a shared header.** It is currently
   invisible outside that file, which is why `FallingBlock`, `ItemEntity`, `Creature` and `BlockOutline`
   all carry hand-copied face shades — **and why `FallingBlock`'s have drifted** (bottom 0.5 vs 0.45, sides
   0.8/0.6 vs 0.86/0.60/0.72). Fix that drift as part of the sweep and record it, or the next person will
   think the unification broke it.
3. **Separate `face.shade` from ambient occlusion.** They are fused into one float today
   (`sample.shading[c] = face.shade * kOcclusionSteps[occlusion]`) and **are not recoverable from the
   shipped value.** A real sun replaces `shade`; AO survives. Keeping both double-darkens; dropping the
   channel deletes AO from the whole world. **The mesher must start emitting AO alone, and `face.shade`
   must move into the shader as a function of the now-carried normal.** This is the hardest semantic
   problem in M23 and it is not plumbing.
4. Add the **6-entry TBN table** (normal, U axis, V axis) with a `constexpr` assert that
   `B == cross(N, T)` per row. Four lines, and it pre-empts the exact bug class that mirrored every shaped
   block's +X and −Z faces.
5. **Stop recovering the normal with `cross(dFdx, dFdy)`.** Read it from the 3-bit face index. Cheaper,
   exact, free of quad-derivative artefacts at silhouettes, and needed for the G-buffer anyway.
6. G-buffer pipeline: the opaque chunk pipeline gains two colour attachments. Write GB0/GB1 exactly as
   **Decision 2** defines them. **Nothing reads them yet.**
7. **Debug view modes** in the tone-map pass, cycled by a key: albedo / normal / material id / AO / depth /
   emissive. Twenty lines, and they are the verification mechanism for M23f.
8. Depth image gains `VK_IMAGE_USAGE_SAMPLED_BIT`, `storeOp` becomes `STORE`, and a
   `DEPTH_ATTACHMENT_OPTIMAL → DEPTH_READ_ONLY_OPTIMAL` barrier is added. **All three together, or the
   lighting pass reads garbage with no error.**

**Acceptance:** image identical; debug views correct by eye; **triangle count must not move** (a geometry
count change means the mesher changed behaviour); measure the G-buffer's cost with a timestamp **before
committing to M23f**.

---

### M23f — The switch

**Goal:** opaque shading moves to a deferred lighting pass; translucent, sky and overlay move to a forward
pass reading the result. Target: **pixel-identical**, then swap Lambert for GGX.

**Steps:**

1. The opaque chunk pipeline **stops writing `sceneColor`**. Its fragment shader gets much simpler.
2. A full-screen lighting pass reads GB0/GB1/depth, reconstructs world position from depth and the inverse
   view-projection, unpacks the light nibbles and the material, and runs **the exact same maths as today**:
   `daylight = ambient + sun * lambert; lit = max(blockLight, skyLight * daylight); rgb = albedo * ao *
   (floor + (1 - floor) * lit)`.
3. A forward pass for translucent + sky + overlay, targeting `sceneColor` with the depth buffer bound
   **read-only** (`depthWriteEnable = false`, `LOAD_OP_LOAD`, `DEPTH_STENCIL_READ_ONLY_OPTIMAL`). Its
   lighting maths is shared with step 2 through a GLSL `#include`.
4. **Only once step 2 is pixel-identical**, replace Lambert with Cook-Torrance:
   `D_GGX` (Filament's numerically stable `oneMinusNoHSquared = dot(cross(N,h), cross(N,h))` form),
   `V_SmithGGXCorrelated` (**the V formulation — denominator already folded in, so `Fr = D * V * F` with
   no further division**), `F_Schlick` with `f90 = saturate(50.0 * f0.g)`, Lambert diffuse `1/PI`.
   **Clamp perceptual roughness ≥ 0.045.**
5. Handheld light (Decision 5).

> ⛔ **Mixing the `G` and `V` formulations is the classic Cook-Torrance bug** and gives either a blown-out
> or a completely black highlight. `Fr = D * V * F` with **no** further division, or
> `Fr = D * G * F / (4 * NoL * NoV)`. Never a mixture.

> ⛔ **Roughness units.** Store **perceptual** roughness in the table and square it in the shader
> (`alpha = perceptualRoughness²`). **LabPBR's "roughness" is already alpha** — if a value is ever copied
> from a LabPBR source it must not be squared again. This is `CLAUDE.md`'s ported-number-in-the-wrong-unit
> trap wearing its fifth hat; **write the unit in the comment beside the table.**

**De-risking:** write the deferred lighting pass **first as a debug overlay** — blend it over the forward
result at 50 % so the two can be seen disagreeing *before* the forward path is switched off. The moment
the opaque pipeline stops writing `sceneColor`, the screen goes black until step 2 is correct.

**Acceptance:** zero validation errors; **the debug views from M23e are how this is verified**; frame time
measured and recorded; the user playtests.

---

## 4. The HUD depth table — the proof that option A works

Sorted nearest → farthest. This is the ordering the M23d sort executes. `VK_COMPARE_OP_LESS`,
`depthWriteEnable = TRUE`, screen geometry drawn with `w = 1` so the authored `z` **is** the NDC depth.

| Depth | What | Mesh |
|---:|---|---|
| 0.00000 | Crosshair inner arms | screen |
| 0.00010 | Crosshair border | screen |
| 0.00060 | Hotbar selected slot count · **Inventory tooltip** | screen · **top** |
| 0.00065 | Hotbar selected slot icon | screen |
| 0.00070 | Hotbar selected slot tint | screen |
| 0.00075 | Hotbar selected slot frame | screen |
| 0.00080 | Hotbar count · **Held-stack count** | screen · **top** |
| 0.00085 | Hotbar icon | screen |
| 0.00090 | Hotbar slot interior tint · Dig-bar fill | screen |
| 0.00095 | Hotbar slot frame · Dig-bar track | screen |
| 0.00098 | Status bars (hearts / food / air) | screen |
| 0.00100 | Loading screen text | screen |
| 0.00120 | **Held-stack icon** | **top** |
| 0.00150 | Loading bar fill | screen |
| 0.00200 | Loading bar track | screen |
| 0.00250 | Loading panel frame | screen |
| 0.00260 | Inventory / craft / chest slot counts | screen |
| 0.00300 | Loading panel · Inventory slot icons, furnace flame + arrow | screen |
| 0.00310 | Catalogue entry counts | **clipped** |
| 0.00320 | Catalogue entry icons | **clipped** |
| *0.00340* | *reserved — scrollbar, unbuilt (INTERFACE slice 9)* | — |
| 0.00350 | Catalogue labels, search text, caret · **Debug overlay text rows** | screen |
| 0.00356 | Search field recess | screen |
| 0.00357 | Search field border | screen |
| 0.00360 | Catalogue cell backgrounds | screen |
| *0.00370* | *free — tab icons are baked into the tab sprite* | — |
| 0.00380 | Selected tab | screen |
| 0.00400 | Inventory + catalogue panel sprites · **Debug overlay graph bars** | screen |
| 0.00420 | Unselected tabs | screen |
| **0.00440** | **World dim quad (full screen, α 0.55)** | screen |
| 0.00450 | Debug overlay graph background | screen |
| 0.00500 | Debug overlay panel | screen |
| 0.00520 | Debug overlay hairline edge | screen |

**Modifiers:** font shadow `depth + 0.00002` (behind the glyph, drawn first); isometric icon painter's key
`depth − 0.00002 × ((minX+maxX+minY+maxY+minZ+maxZ) × 0.5)`, which shifts a full cube by −0.00003.

**Minimum gaps:** absolute minimum between distinct bands is **0.00001** (search recess → search border).
The tightest live constraint is **0.00002** — hotbar count 0.00080 against a shifted full-cube icon at
0.00082 — **which is why `kBoxStep` cannot be raised**.

**The three rows in bold are the whole story:** the dim quad at 0.00440 sits in front of the debug
overlay's three backdrops, and under `LESS` it wins. **That is the live bug, and option A deletes it.**

---

## 5. Blast radius

### 5.1 Files M23 touches

| File | Lines today | What changes | Risk |
|---|---:|---|---|
| `engine/src/render/Renderer.cpp` | 883 | `recordCommands` splits into G-buffer / lighting / forward / post / UI passes; render targets; barriers; per-pass timestamps | **HIGH** — the whole frame is one 228-line function and every invariant is enforced in it |
| `engine/shaders/triangle.frag` | 159 | Splits into ≥ 4 shaders. Hurt tint, fuse flash and fog all move | **HIGH** — seven behaviours in one über-shader, three of which cannot survive a naive port |
| `engine/include/engine/render/Renderer.hpp` | 320 | Members for every target; per-pass stats; quality toggles | **HIGH** — 40+ members already |
| `engine/src/render/GraphicsPipeline.cpp` | 213 | Every hardcoded state becomes a `PipelineDesc` field | **HIGH** — twelve separate decisions; getting `frontFace` wrong hollows every box silently |
| `game/src/world/ChunkMesher.cpp` (merged pass) | — | Merge key, `FaceSample`, **AO/shade separation** | **HIGH** — carries > 99 % of geometry |
| `engine/include/engine/render/Vertex.hpp` | 29 | The struct | **HIGH** — not for its 29 lines, but because **13 sites in 10 files** construct it by brace-initialisation |
| `game/src/world/Creature.cpp` | ~25 of 2,374 | `skinQuad` only — but **all creature quads are double-sided** | MEDIUM |
| `game/src/hud/HudPrimitives.cpp` | ~15 | Four writes carry the whole HUD | MEDIUM |
| `engine/src/render/TextureArray.cpp` | 323 | `kFormat` becomes a parameter; sampler split | MEDIUM — a normal map in `_SRGB` is silently wrong |
| `engine/src/render/VulkanContext.cpp` | 338 | Enable features, **and mirror each into the suitability filter** | MEDIUM |
| `engine/src/render/DepthImage.cpp` | 89 | `SAMPLED_BIT`; possibly a second for shadows | MEDIUM |
| `engine/src/render/UploadContext.cpp` | 159 | **Widen the flush barrier's `dstAccessMask`** if anything but vertex/index data goes through it | MEDIUM — silent stale reads with no validation error |
| `engine/CMakeLists.txt` | 98 | ~12 shaders, `DEPFILE`, `-I`, unique names | MEDIUM |
| `engine/src/render/Buffer.cpp` | 144 | Sub-allocation or VMA | MEDIUM |
| `game/src/core/Settings.cpp` | — | ~5 new keys + **a validated float parser that does not exist** | LOW |
| **NEW** `engine/src/render/RenderTargets.*` | — | The G-buffer + HDR + bloom image set | MEDIUM |
| **NEW** `game/src/world/Material.hpp` | — | The material family table | LOW |
| `engine/src/render/{Camera,VulkanCheck}.*`, `core/{FrameLimiter,JobSystem,Paths}.*` | — | **Nothing** | **NONE** |

### 5.2 What `GraphicsPipeline` hardcodes today, and what each pass needs

| Hardcoded | G-buffer pass | Full-screen pass | UI pass |
|---|---|---|---|
| `Vertex` binding + 4 attributes | packed vertex | **none** (`vertexBindingDescriptionCount = 0`) | packed vertex |
| `cullMode = BACK`, `frontFace = CCW` | same | `CULL_MODE_NONE` | `CULL_MODE_NONE` |
| `blendEnable = VK_TRUE` + alpha factors | **false** | false | **true** |
| `attachmentCount = 1` | **3** | 1 | 1 |
| `depthTest/Write = TRUE`, `LESS` | same | off | **no depth attachment** |
| `colorAttachmentCount = 1`, `pColorAttachmentFormats = &format` | **array of 3** | 1 HDR / 1 swapchain | 1 swapchain |
| `setLayoutCount ≤ 1` | ≥ 2 | ≥ 1 | 1 |
| pipeline cache = `VK_NULL_HANDLE` | a shared `VkPipelineCache` becomes worth it at ~10 pipelines | | |
| no depth bias | M24 shadow maps need it | | |

⚠ `pColorAttachmentFormats = &colorFormat` takes the address of a **by-value parameter** — safe today
because the struct is consumed synchronously, but it must become a stored `std::vector<VkFormat>` when
N > 1.

**Pipeline count goes from 1 to about 9.** That is the honest price of M23 and it belongs in the plan up
front. Mitigate with a small helper taking a struct of overrides. **Do not build a "material system"
abstraction** — `CLAUDE.md`'s rule stands: no abstraction without a second real case, and these nine are
one case each.

---

## 6. Things that will silently break

Every one of these compiles clean, produces no validation error, and renders a plausible picture.

### 6.1 Vertex and mesher

1. ⛔ **Brace-initialising a `Vertex` after widening a field. This has already happened once.**
   `LESSONS.md`: widening `color` from `vec3` to `vec4` *"silently made the crosshair invisible"* — the
   aggregate initialiser zero-filled the missing element, which was alpha, which with blending on meant
   fully transparent. **No warning at `/W4`.** All 13 sites use positional brace init with no designated
   initialisers. **Grep all 13 by hand after any field change; the compiler cannot help.**
2. ⛔ **A per-vertex normal on double-sided geometry lights the back faces inside-out.** **Nine of the
   thirteen writes emit both windings** (leaves, plants, creatures, item sprites, arrows, the sun, HUD
   quads). One normal per vertex is correct for the front triangle and exactly inverted for the back.
   **The shader must flip by `gl_FrontFacing`, and nothing in the codebase currently reads it.**
3. ⛔ **`face.shade` and ambient occlusion are the same float and cannot be told apart.** See M23e step 3.
   Last time this channel was disturbed, geometry changed by **47 %** with no error.
4. ⛔ **Adding a field to `FaceSample` without adding it to `matches`** merges faces that differ in it,
   spreading the first cell's value across up to a 32×32 quad. **No assertion exists.** Add a
   `static_assert(sizeof(FaceSample) == N)` beside `matches` purely as a tripwire.
5. ⛔ **`SpriteModel`'s wall quads have zero UV area on purpose** — all four corners share one texture
   coordinate so the face pins to mip 0 and does not vanish at distance. **Any tangent generated from UV
   derivatives is `0/0` on every wall of every dropped item.** NaN tangent, black lighting, no report.
6. ⛔ **`BlockOutline` keeps a hand-copied duplicate of the `kFaces` corner table.** Its comment claims
   they match; nothing checks. A winding change in one and not the other makes the cage vanish.
7. ⛔ **`FallingBlock`'s shades already disagree with `kFaces` and its comment says they agree.**
   Bottom 0.5 vs 0.45, sides 0.8/0.6 vs 0.86/0.60/0.72. **It is already broken; fix it in the sweep and
   record it**, or the unification will be blamed.
8. ⚠ **The shape pass hardcodes `result.opaque`.** A translucent slab or stained-glass pane would render
   opaque and fail no test. Not currently reachable, not currently asserted.

### 6.2 Layers and textures

9. ⛔ **HUD "white" geometry samples the *block* texture array, not the HUD sheet.**
   `TextureLayer::White = 5` is used by the crosshair, tooltips, debug rectangles, hotbar interior tints
   and the inventory dim layer. **Renumber the block array and the entire HUD's solid-colour geometry
   samples whatever landed on layer 5** — grass, or gravel, or bricks, tinted by the panel colour. Layers
   are assigned by *the order files are loaded*, and M23 may well reorder them.
10. ⛔ **Five negative layer sentinels are compared with floating-point tolerances in three places**
    (`abs(layer - animation.x) < 0.25`; `< -2.5 / -1.5 / 0.0`; `< -4.5 / -3.5`). Making `layer` an integer,
    packing it, or adding a **sixth** sentinel breaks all of them with no compile error and no validation
    error. `kSkinFlashLayer = -5.0f` is already at the bottom of the range.
11. ⛔ **`TextureArray::kFormat` is one file-scope `constexpr` shared by all four arrays, and it is
    `_SRGB`.** Correct for albedo, **silently wrong for any normal/roughness/metallic data**.
12. ⛔ **Six `Renderer::texture*/font*` methods read CPU-side texture alpha at startup.** Dropped-item
    sprite walls and every variable-width glyph advance are **measured from the loaded image**. Compressed
    textures, a coverage-preserving mip chain, or a streamed atlas breaks both silently — text falls back
    to monospace and every dropped item becomes two flat pictures. **Neither logs anything.**

### 6.3 Passes and state

13. ⛔ **`createDescriptorResources` runs only in the constructor**, with the comment *"neither texture ever
    changes, so the set is written once here rather than per frame."* **G-buffer inputs change on resize.**
    That write must move into, or be called from, `recreateSwapchain`.
14. ⛔ **`m_currentFrame = 0` at the end of `recreateSwapchain` does not reset `m_timestampsPending`.**
    Today that loses one GPU-time sample. With per-pass timings it becomes a wrong-results source.
15. ⛔ **The end-of-frame timestamp is written *after* the present barrier.** Per-pass timestamps must go
    *inside* the rendering scope, and `BOTTOM_OF_PIPE` on a pass boundary means "everything submitted so
    far has drained", not "this pass cost X". **Per-pass timings on desktop Vulkan are indicative, not
    authoritative — say so on the overlay or they will be trusted more than they deserve.**
16. ⛔ **`UploadContext`'s flush barrier covers `VERTEX_ATTRIBUTE_READ | INDEX_READ` only.** Upload a
    material table or an indirect-draw buffer through it and it "works", then reads stale data with **no
    validation error**.
17. ⛔ **Do not enable alpha blending on the G-buffer pass.** Unity documents why: *"pixel normals cannot
    be correctly combined using the alpha blend equation alone… Averaging or summing normals results in
    loss of accuracy."* A blended normal is not a normal; a blended material id is nonsense. G-buffer
    writes are opaque or alpha-**tested**, never blended.
18. ⛔ **Do not put the HUD through the G-buffer.** Naively extending the shared pipeline with two extra
    colour attachments makes **every HUD quad write garbage into GB0/GB1**, and the lighting pass then
    lights the inventory screen. M23d exists to prevent this and should move earlier if it turns out cheap.
19. ⚠ **`push.animation` is applied to screen-space draws too.** Harmless today; a trap if `layer`
    semantics change.

### 6.4 Settings

20. ⛔ **`saveSettings` rewrites the whole file, and anything not in the writer is destroyed.** Adding a key
    takes **four edits with no compiler help**: field, read line, clamp, **and the writer**. Miss the
    writer and the key vanishes the first time anything calls `saveSettings` — **which happens on every
    F6/F7 press**. There is no `static_assert` tying the three lists together, unlike
    `TextureLayer::SpawnEggFirst`, which *is* checked. **Close that asymmetry; M23 adds five keys at once.**
21. ⚠ **There is no validated float parser.** `sound_volume` uses `std::atof`, which silently yields 0 on a
    typo. `render_scale=1.0` needs a real one (~20 lines, `parseUnsigned` is the template).
22. ⚠ **`Settings.hpp` already says "read once at startup, nothing re-reads the file while playing", and
    that is already false** — F6/F7 changes `render_distance` live and rewrites the whole file. Fix the
    comment while adding keys.

---

## 7. Invariants that must survive

Each was paid for once. A rewrite can silently undo any of them.

**Geometry and culling**
1. **`VK_FRONT_FACE_COUNTER_CLOCKWISE` with `VK_CULL_MODE_BACK_BIT`.** Verified on screen, not derived —
   *"a hand derivation through the Y-flip argued for CLOCKWISE and was simply wrong."* Every new pipeline
   must copy it, and any change is checked against a closed box viewed from outside.
2. **`projection[1][1] *= -1.0f` stays, and `GLM_FORCE_DEPTH_ZERO_TO_ONE` stays PUBLIC.** Without the flip
   the scene is upside down with no warning; the Gribb-Hartmann near-plane extraction depends on the define
   specifically (near plane is row 2 alone, not `w + row2`).
3. **A merged quad is built by scaling the original unit-face corners.** Scaling by positive factors cannot
   flip the winding. **Do not rewrite this to construct corners from scratch.**
4. **The texture layer stays `flat`-interpolated.**

**Transparency and ordering**
5. **`setTopScreenMesh` and its draw order** — until M23d lands. Merging it back "because it is nearer
   anyway" punches a hole through the catalogue.
6. **The isometric-icon painter's key `kBoxStep = 0.00002f`** — and **it cannot be raised**; §4 shows the
   margin is exactly that.
7. **Font glyphs cropped to their advance** — until the UI pass gains an alpha discard, and then only
   deliberately.
8. **Translucent geometry is drawn after every opaque mesh in the scene**, not per chunk.
9. **`isTranslucent(id)` is water alone.** Widening it to lava reproduces the exact bug its comment
   describes. `CLAUDE.md`'s "widening a predicate kills the early-outs behind it" applies directly.
10. **Stained glass is cutout, not blended, on purpose.** A deferred renderer makes "proper sorted
    transparency" feel newly affordable. It is not, for 16 ids at arbitrary stacking depth.
11. **A surviving cutout pixel is fully opaque; its alpha comes from the vertex, not the texture.** Mip
    levels average alpha, and letting a 0.6 survive turned every distant tree pale grey. **The 0.5
    threshold and its reasoning must be carried into the G-buffer pass.**

**Lighting**
12. **Only sky light answers to the sun. Block light must not be modulated by daylight** — otherwise
    torches go out at dusk and lit caves go dark.
13. **Face shade multiplies the result *including* the ambient floor**, or unlit caves come out perfectly
    flat.

**Fog**
14. **`fluid::kFogColour = rgb(20, 101, 231)` is MEASURED from screenshots, not taken from the JSON.** Got
    wrong three times. **Do not multiply it by daylight.**
15. **The clear colour and the distance-fog colour must remain the same value** after whatever transform
    the pipeline applies.
16. **Distance fog is radial from the eye; underwater fog is view-axis depth and applies to the sky too.**
17. **Underwater fog must never drive the `beyondFog` cull** — it is dense and close. If fog becomes a
    post-process, this cull loses its input and must be **re-plumbed, not deleted** (it is worth 308 → 235
    draws).
18. **The depth-aware fog experiment was built and rejected on sight.** A post-process makes the
    per-fragment version tempting again. **Do not rebuild it.**

**Lifecycle**
19. **No `vkDeviceWaitIdle` on mesh replacement — ever.** Buffers are retired for `kFramesInFlight + 1`
    frames. A device wait looks like harmless cleanup and reintroduces a permanent stutter visible only
    while moving.
20. **The retirement release pass runs at the very top of `drawFrame`, before the minimised early-return.**
21. **`GpuMesh::inUse` must stay** — without it a double remove hands one slot to two chunks.
22. **Retire before allocating the replacement**, or peak device memory doubles.
23. **`m_renderFinished` is per swapchain image, not per frame-in-flight.**
24. **`m_eyePosition` is recovered by inverting the view matrix**, so it cannot disagree with the matrix
    everything is drawn through. **Do not add a `setEyePosition()`.**

**Architecture**
25. **Mesh generation never touches the GPU.** `MeshData` is plain data; upload is a separate step.
26. **`engine/` must not learn what a block or a chunk is.** The pressure point is already
    `triangle.frag`'s `R = sky, G = block, B = shade × AO`, which is a game semantic living in an engine
    shader. **A G-buffer makes that pressure much worse. Move the convention into one shared, checked
    place rather than letting the engine learn more game meanings.**
27. **Separate texture-array layers, never an atlas** — mipping an atlas bleeds neighbouring tiles.
28. **`NEAREST` magnification, `LINEAR` mip mode.**
29. **`TextureArray::alphaAt` and the font metrics derived from it must keep working.**
30. **The four watchdog counters (`chunks | meshes | pending | retired`) must keep being produced.**
31. **Every expensive feature must be a runtime setting, not baked pipeline state.**

---

## 8. Settings M23 adds

| Key | Type | Default | Live? | Notes |
|---|---|---|---|---|
| `hdr` | bool | 1 | restart | Off = skip the offscreen target entirely and draw straight to the swapchain, as today |
| `bloom` | bool | 1 | **yes** | Cheapest of the five — one `bool` on `Renderer`, one `if` in `recordCommands` |
| `tone_map` | unsigned 0–3 | 0 (PBR Neutral) | **yes** | **Use a number, not a string** — reuses the existing validated `read()`; a string parser does not exist |
| `exposure` | float | 1.0 | **yes** | Needs the new validated float parser |
| `pbr` | bool | 1 | restart | Off = keep the Lambert path. Two lighting-pass permutations, or a specialisation constant |
| `render_scale` | float | 1.0 | **restart** | **The expensive one.** Live means rebuilding every target *and* rewriting the descriptor set, plus the three-way extent disagreement. Treat it exactly like `worker_threads`: restart-only, documented as such |

**Mechanism choice:** a setting the player flips in a menu wants a **uniform branch** or two prebuilt
pipelines; a setting fixed at startup wants a **specialisation constant** (a value baked into the shader at
pipeline-creation time, so the branch disappears). Do not use `#define` permutations — the combinatorial
count gets away from you at five toggles.

---

## 9. How M23 gets verified

**"Both presets clean with zero validation errors" proves almost nothing** — it proves it does not crash.
`CLAUDE.md` item 6 exists because a sweep found five real bugs, including two-keystroke item duplication,
in code that had already passed exactly that bar. So:

1. **Screenshot diffs at every slice that claims "no visual change".** M23a, M23e and the first half of
   M23f all claim it. A pixel diff is the only honest check.
2. **Debug view modes** (M23e step 7) are the primary verification mechanism for M23f. Build them early.
3. **A temporary startup probe** printing the material row for ~30 named layers, checked by eye, then
   removed. This is the method that found five bugs on 2026-08-06 and it is nearly free.
4. **Frame time recorded before and after every slice**, release build only, minimum of three runs.
   **Never benchmark on Debug** — the debug CRT serialises the heap and the numbers are not just noisy,
   they are non-monotonic.
5. **Triangle count must not move** across M23e. A geometry change means the mesher changed behaviour.
6. **`pending 0` on the first per-second log line**, and the save unchanged, before comparing any counter.
7. **Anything that writes `settings.cfg` or `saves/*/player.dat` must restore it in the same command, in a
   `finally`.** Third occurrence of that lesson.
8. **The user playtests.** Three of the four bugs at M3 were found by them playing, none of which produced
   a warning. Hand them a build and say what to look at in one line.

**Specific things only the user can judge:** which tone mapper; whether the bloom is too strong; whether
metals read as metal; whether the HUD is still legible over a bright scene; whether emissive blocks look
like lights or like neon.

---

## 10. Open questions for the user

1. **Tone mapper.** Three side-by-side screenshots, one choice. This is a taste decision and it belongs to
   you, not to the algorithm — which is exactly why Mojang made it data-driven.
2. **Biome tinting.** The grass/leaf/water tints are currently **baked into the staged PNGs**. If per-biome
   tinting is ever wanted (swamp green, badlands orange), it has to be *un-baked* — and doing that after
   PBR lands means re-deriving roughness values that were eyeballed against tinted art. **Decide before
   authoring any material value.**
3. **Reverse-Z.** Better depth precision at distance, and it costs a projection-matrix change, a clear
   value, a compare op, and a sign flip in the frustum-plane extraction. **It also inverts the entire HUD
   depth table**, which is a non-event under option A and catastrophic under option C. Worth doing, but
   only alongside M23d.
4. **How far should the game scale down?** `TIMELINE.md` lists minimum hardware as an open decision.
   M23 is where it starts to matter, because every quality toggle needs a low setting that is actually
   playable.
5. **GDC Vault access?** A. J. Fairfield's GDC talk *"Modernizing the Rendering of Minecraft"* is behind
   the paywall and is the single highest-value source for this milestone — it is the only place
   RenderDragon's actual G-buffer layout might be published.

---

## 11. Two bugs found during this audit

Neither is caused by M23; both were found while reading for it.

1. ⛔ **The F5 debug overlay has no panel behind it while the inventory is open.** The inventory's dim quad
   (0.0044) beats the overlay's panel (0.0050), hairline (0.0052) and graph background (0.0045) under
   `LESS`, and the overlay is appended to the same mesh afterwards. Only the text and graph ink survive.
   **Fixed for free by M23d.** Could also be fixed today by moving the overlay's three backdrops above
   0.0044 — but that band is crowded and M23d deletes the problem.
2. ⛔ **`FallingBlock.cpp`'s face shades disagree with `ChunkMesher`'s `kFaces`** — bottom 0.5 vs 0.45,
   sides 0.8/0.6 vs 0.86/0.60/0.72 — **and its comment says they match.** A falling sand block is shaded
   differently from the sand block it just was. Fix it when `kFaces` is lifted into a shared header
   (M23e step 2), and **record it**, or the unification will be blamed for the change.

---

## 12. Sources

**Codebase** — seven read-only audit passes on 2026-08-09 over `engine/render/`, `engine/shaders/`,
`game/src/world/ChunkMesher.cpp`, `Creature.cpp`, `Block.hpp`, `Item.hpp`, `Tool.cpp`, `Explosion.cpp`,
`Sounds.cpp`, `game/src/hud/*`, `game/src/core/Settings.*`, `Main.cpp`, `tools/make-reference-blocks.ps1`,
and the eleven project documents.

**Primary web sources, by decision:**

- *Deferred vs visibility buffer for voxels* — vkguide.dev Project Ascendant; Minecraft Wiki RenderDragon;
  wiki.bedrock.dev deferred Q&A; juandiegomontoya.github.io Teardown breakdown; adriancourreges.com
  DOOM 2016 and GTA V graphics studies; shaders.properties (Iris program order); arxiv 2505.02017 (Aokana);
  turanszkij.wordpress.com (Wicked Engine 2024); jcgt.org/published/0002/02/04 (Burns & Hunt).
- *G-buffer layouts* — Unity URP deferred docs; Unreal `r.GBufferFormat`; aras-p.info compact normal
  storage; jcgt.org/published/0003/02/01 (octahedral survey).
- *HDR / tone mapping* — learn.microsoft.com Vibrant Visuals colour grading and tone mapping
  customisation; modelviewer.dev tone mapping considerations; github.com/KhronosGroup/ToneMapping
  (PBR Neutral spec); 64.github.io/tonemapping; bartwronski.com R11G11B10F precision;
  froyok.fr custom bloom; iryoku.com next-generation post processing.
- *PBR materials* — shaderlabs.org LabPBR 1.3 spec and implementation requirements; bedrock.dev Texture
  Sets; NVIDIA Minecraft RTX PBR texturing guide; github.com/google/filament Materials.md and
  `surface_brdf.fs`; github.com/sixthsurge/photon (blocklight falloff, directional lightmaps, LPV,
  parallax, emission scale); github.com/PhoenixTheSage/AutoPBR; github.com/azagaya/laigter;
  bruop.github.io/ibl; jcgt.org 8(1) Fdez-Agüera.
- *Vulkan implementation* — khronos.org "Streamlining Render Passes"; the Vulkan 1.3/1.4 specification;
  vulkan.gpuinfo.org device limits and extension coverage (Windows, retrieved 2026-08-09);
  docs.vulkan.org forward vs forward-plus vs deferred.

---

*Written 2026-08-09. Nothing in this file has been built. When a slice ships, mark it in `TIMELINE.md`,
record the evidence in `SYSTEM_MEMORY.md`'s validation baseline, and move any contested reasoning into
`CLAUDE.md` — not here. This file is a plan and should shrink as the plan becomes fact.*
