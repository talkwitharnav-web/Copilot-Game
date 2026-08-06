# Reading Reference Textures, and Authoring Our Own

How to work out what a texture actually is, instead of assuming — and how to build ours to match. Written after the sheep, which took far more attempts than it should have because every failure came from guessing at a number that was sitting in the image the whole time.

`ASSETS-REFERENCE.md` says what is *in* `reference/`. This says what to *do* with it.

**The rule this whole file exists to enforce: a texture's layout is measurable. Measure it. Do not infer it from what you think the model is.**

And the corollary, which cost just as much: **measure the specific texture you are copying.** A rule that held for one subject — palette size, tone count, how mottled a surface is — will not transfer to the next. See §4.

---

## 1. What a box net is

A blocky creature is a handful of rectangular boxes. Each box's six faces read six rectangles out of one image — the *net*, the box unfolded flat. Get a rectangle wrong and that face shows pixels belonging to some other part of the animal.

For a box of **width `w`** (side to side), **height `h`** (up), **depth `d`** (front to back), with its net origin at `(u, v)`:

| face | rectangle | size |
|---|---|---|
| top | `(u+d, v)` | `w × d` |
| bottom | `(u+d+w, v)` | `w × d` |
| right | `(u, v+d)` | `d × h` |
| front | `(u+d, v+d)` | `w × h` |
| left | `(u+d+w, v+d)` | `d × h` |
| back | `(u+d+w+d, v+d)` | `w × h` |

The whole net occupies `2(w+d)` across and `d+h` down. It looks like a wide band with a shorter band sitting on top of it, offset right by `d`.

### Deriving `w`, `h`, `d` from the image

You never need the model source. The shape gives you all three:

- **`d`** = how far the top band is inset from the left edge of the main band.
- **`w`** = half the top band's width.
- **`h`** = the main band's height.
- Sanity check: the main band must be `2(w+d)` wide.

Worked, from the sheep's fleece head:

```
 0 ......############..........
 1 ......############..........      top band starts at x=6      -> d = 6
 ...                                 top band is 12 wide         -> w = 6
 6 ########################....
 ...                                 main band is 24 wide        -> 2(6+6) = 24 OK
11 ########################....      main band is 6 tall         -> h = 6
12 ............................
```

That is a 6×6×6 box, read straight off the alpha channel in about ten seconds.

---

## 2. The measurements worth taking

All of these are cheap. Take them *before* writing any code.

### Alpha map — gives you the net dimensions

```powershell
Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::FromFile((Resolve-Path $path).Path)
for($y=0;$y -lt 14;$y++){ $r=""; for($x=0;$x -lt 28;$x++){
  $r += $(if($b.GetPixel($x,$y).A -ge 128){'#'}else{'.'}) }; Write-Host ("{0,2} {1}" -f $y,$r) }
```

Only works where the sheet has transparency — overlay/fleece layers usually do, base layers usually do not.

### Palette and luminance spread — tells you how to author yours

```powershell
$h=@{}; for($y=0;$y -lt 32;$y++){for($x=0;$x -lt 64;$x++){$c=$b.GetPixel($x,$y)
  if($c.A -ge 128){$k="{0:X2}{1:X2}{2:X2}" -f $c.R,$c.G,$c.B; $h[$k]=[int]$h[$k]+1}}}
$h.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 14
```

Report luminance as `0.299R + 0.587G + 0.114B`. The **count** matters as much as the colour: it separates the field tones from the accents.

**Always report the total colour count too** — it decides which discipline the texture wants before you author a single pixel. Under about 20 means flat fields; in the hundreds means dense dithering. Getting this backwards is the single most expensive mistake in this file.

```powershell
$h=@{}; for($y=0;$y -lt 32;$y++){for($x=0;$x -lt 64;$x++){$p=$b.GetPixel($x,$y)
  if($p.A -ge 128){$h["$($p.R),$($p.G),$($p.B)"]=1}}}
"$($h.Count) colours"
```

### Locating a feature — settles "where is the face?"

Search for the distinctive colour. Eyes are near-black; find them and you have found the front face:

```powershell
for($y=0;$y -lt 16;$y++){for($x=0;$x -lt 30;$x++){ $p=$b.GetPixel($x,$y)
  if($p.R -lt 60 -and $p.G -lt 60 -and $p.B -lt 60){ "$x,$y" } }}
```

Sheep eyes came back at `(8,10)` and `(13,10)` — so the face rect is `(8,8)` 6×6, which *proves* `d = 8` for the skull, since `u+d = 8`.

### Region dump — shows you the anatomy

Map each palette entry to a letter and print the rectangle. This is what turns "the face" into an actual specification:

```
 8 wvWwuw     fleece fringe over the brow
 9 bBBBbd     hide
10 e#BB#e     eyes: dark outboard, glint inboard
11 bBBbbd     hide
12 vbPPdv     muzzle, fleece left in the outer corners
13 vdppdv
```

### Crop a region out to look at it

When a grid is not enough, extract the net as its own magnified image and *look*. Use `tools/preview-textures.ps1`, or clone a rectangle and scale it with `InterpolationMode='NearestNeighbor'`. This one step answered a question three rounds of reasoning had failed to.

---

## 3. Traps, all of which cost real time

**An overlay box is not the same box as the one underneath.** The sheep's skull is 6×6×**8**; its fleece is 6×6×**6**. Different depth means a different net layout, so mapping the fleece with `d=8` reads rectangles that do not exist in that sheet — which renders as grey bands and smeared pixels. *Always measure the overlay separately.*

**Where an overlay's edge lands can be a very narrow window.** The fleece cap had to stop **past the ears** (which sit 1–3 texels back from the front of the head) but **short of the face** (the plane at the very front). Too far forward masks the face completely; too far back slices the ears in half; and it could not simply be pushed back, because the body's fleece already reached to within 0.016 blocks — it would have been invisible. Eyeballing this fails. Measure where the features are, then place the edge in the gap.

**An opaque overlay hides everything beneath it.** Before assuming an overlay is subtle, check whether it has any transparency at all. The sheep's fleece head net is solid white with no holes, so *any* fleece box covering the muzzle erases the face regardless of UVs.

**Overlays that do have holes need alpha testing, not blending.** The wool's face rect is 12-of-36 transparent. Blending those holes smears semi-transparent white over the face. Discard below 0.5 and write survivors **fully opaque** — mip levels average alpha, so a distant creature otherwise arrives around 0.6, survives the test, and then blends with the sky. That exact bug turned every distant tree grey at M18.

**A rotated box remaps which net rectangle goes on which world face.** The sheep's body is authored upright and laid down a quarter turn, so the net's *front* rect becomes the body's *bottom*, its *back* becomes the *top*, and the net's *top* and *bottom* become world front and back. Work the rotation through on paper rather than guessing — and note that **an error here can stay invisible for a whole species**: having top and bottom swapped looked fine on a sheep, whose belly and back are both plain fleece, and only surfaced on a cow, which has a pink underside. It painted the belly across its spine.

**Not every net follows the formula exactly — unused faces are simply absent.** The cow's udder measures 6 opaque texels wide where `2(w+d)` predicts 10, because its back face is never sampled and was left transparent. Treat a *narrower* band than predicted as a possible missing face, not as proof the dimensions are wrong.

**Identify an unknown net by its colour before building anything on it.** Three small nets sat unexplained in the cow sheet; averaging their pixels settled all three in one command — 226,160,160 pink was the udder, 70,70,70 grey the horns, and an 80,72,63 brown one was neither. Guessing had put a slab of hide through the animal's back.

**Nets must not overlap.** An early sheep put the leg net at `y=12` while the head net ran to `y=14`. They shared pixels, so legs and head sampled each other. Add up each net's extent and check.

**Per-face 0–1 UVs give inconsistent texel density.** If every face stretches a whole tile across itself, a 4-texel leg and a 16-texel body show the same pixel count at wildly different sizes. A proper net fixes this for free, because rectangle size is proportional to face size.

**A palette rule measured on one subject does not transfer to another.** The tight-palette discipline was derived from block textures, held perfectly for the sheep, and was flatly wrong for the creeper — which is 691 colours where a block is five. Applying it there would have shipped a flat green slab. Count the colours of *the specific texture you are copying* before deciding how to author yours. See §4.

---

## 4. Authoring ours

### What is ours and what is shared

**Layout is anatomy** — a quadruped's net, where a face goes, that hooves are at the bottom. Shared, functional, fine to match exactly.

**Pixels are expressive work.** Palette, tone choices, patterning must be original. Reference art may be used to *measure*, must never live under `assets/`, and must never ship in a release. Keep it in `reference/` — the build copies `assets/` wholesale, and 925 of Mojang's PNGs once landed in the output that way.

**Almost every texture in the running game is currently Mojang's, and none of it is a mistake.** Four staged sets sit **beside each `game.exe`, never under `assets/`** — `creatures-reference.png` (42 of 46 skins), `spawn-eggs/` (all 46), `hud-reference.png` (the five catalogue tabs) and `blocks-reference/` (76 block and item textures). `assets/textures/hud.png` is the one exception that does live under `assets/`. All of it is at the user's explicit request, all of it is temporary, and all of it must be gone before a release. `START-HERE.md` §5 has the arrangement and why it exists.

**The sequence is the point, and it is a standing rule rather than a concession:** stage the reference first, prove the model or the layout against pixels known to be right, *then* author ours. Authoring art and judging the geometry by it at the same time leaves two unknowns behind one symptom. **Do not re-author the creature skins** — an artist has them — and do not treat any of the placeholders as a violation to clean up.

### Palette discipline is a property of the subject, not a house style

**Measure the specific thing you are copying. Do not carry a rule across from another one.** This is the mistake that has cost the most time on this project, twice.

Counted across the same reference pack:

| texture | colours | pixels | why |
|---|---|---|---|
| `sheep_wool.png` | 5 | 2048 | wool genuinely is uniform |
| `sheep.png` | 13 | 2048 | hide, mostly flat fields |
| `creeper.png` | **691** | 2048 | lichen is mottled, so nearly every pixel differs |

Both disciplines are deliberate, in the same art style, by the same artists. There is no single correct answer — there is only what the surface is actually like.

**Flat fields — hide, wool, planks, stone.** Very few tones and a tight luminance span. Block textures live here almost without exception: reference stone spans about five near-identical greys. Contrast instinct is consistently wrong; widening the range is what makes these read as noise. The sheep's hide sits inside ~21 luminance across 3 tones, its fleece ~36 across 4, with accents spent very sparingly — 10 pink pixels, 2 eye pixels.

**Mottled surfaces — moss, lichen, scales, fur with depth.** Dense per-pixel variation, hundreds of distinct colours, a luminance span of 120 or more. Using a tight palette here produces flat paint, which is exactly what a creeper must not look like.

Either way, **interpolated or smoothed noise is never right.** Smoothing is blurring, and pixel art never blurs. Dithering is per-pixel *choices*, not a gradient.

### Dithering technique

To get hundreds of colours, jitter RGB per pixel rather than indexing a palette:

- **One correlated swing** applied to all three channels together. This moves the *value* hard while keeping the hue coherent — it is what stops the result turning into confetti.
- **Small independent nudges per channel** on top, which break up banding into visible steps.
- **A different salt per net face**, so no two faces repeat the same noise pattern.

Ours came out at 857 colours across 864 opaque pixels — denser than the reference's 691 — with luminance 9–184 against its 0–220. See `Set-Dithered` in `tools/make-creature-skins.ps1`.

One detail worth keeping: **dither the dark features too.** A flat black eye socket on an otherwise mottled creature reads as a missing texture rather than as a hole.

### Grain: where a 25-colour subject's colours actually are

Between the two extremes sits the discipline most of the roster needs. The horse family, camel, llama and goat measure 15–36 colours — too many for flat fields, far too few for dithering — and almost every one of those colours is **a single step either side of a face's base tone, sprinkled across it.** Not contrast. At 16 texels a wide spread reads as dirt.

The first pass at these nine authored one flat tone per face and landed at 9–13 colours against references at 19–36. Side by side it looked like coloured paper. Three things fixed it, and all three are in `tools/make-roster-skins.ps1`:

- **`Set-Grain`** picks per texel from a short tone list with **weights**, so the base tone keeps the majority and the extremes stay sparse. An *even* pick over the same tones reads as static — the cyborg-cow failure again.
- **A coverage `Rate` below 1.0** leaves the untouched texels exactly as a ramp or vignette left them. This matters: replacing every texel destroys the modelled shading underneath, which buried the camel's limbs until the rate came down to 0.4.
- **`Get-Mix`** blends two anchors into a third palette entry. That is not smoothing — blending *across a surface* is what pixel art never does, but blending a swatch is just mixing paint, and it is how a five-tone coat reaches the thirteen the reference implies.

**Hair strokes must be irregular in every dimension.** The first goat used a fixed modulo for the column and one baseline for the start row; it came out combed, like corduroy. `Set-Hairs` rolls the column, the start row, the length and the tone separately, so no two neighbouring strokes agree.

**A marking drawn as a rectangle reads as a sticker.** The mule's mealy belly and the horse's blaze are drawn as *broken coverage* instead — grain at 55–88% over the coat, so the pixels it skips keep the coat underneath and the edge comes out ragged for free.

### Markings are shapes, not per-pixel odds

The single most visible failure so far. A cow's patches were generated by rolling a 3×3 cell grid independently — each cell light or dark on its own odds. The result was scattered squares that the user called **"a cyborg"**, and they were right: independent rolls produce *static*, which no animal has.

Real markings are **connected blobs, three to eight texels across, with ragged edges**. The way to get them is to place a small number of centres and grow outward — overlapping discs with a per-pixel wobble on the radius, so an edge is irregular rather than a visible circle. Nine stamped discs beat two hundred independent cell rolls.

Two details that matter:

- **Stamp each band of a net separately.** A blob allowed to straddle the seam between the top band and the main band tears across the model, because those rectangles are not adjacent in 3D.
- **Fewer and larger beats more and smaller.** Lots of small discs read as spots; the reference's markings merge into a handful of big irregular shapes.

And the counterpart, from the same cow: **an accent is usually far subtler than instinct suggests.** Its muzzle lifts from luminance 56 to 82 — barely lighter than the hide. Rendering it bone-white made the face read as a mask.

### When a first attempt is wrong, rewrite it — do not tweak it

**Rule, from the user:** if the first version of one of our textures looks wrong, go back and read the reference *again*, then write it from scratch. Only tweak when it is genuinely almost there and wants a smidge.

The reason is that a bad texture is usually the wrong **technique**, not the wrong **parameter**, and tweaking parameters cannot cross that gap. Every failure so far has been technique:

| what looked wrong | what I reached for | what it actually was |
|---|---|---|
| cow patches "like a cyborg" | patch count, cell size | independent per-cell odds instead of grown blobs |
| pig reading flat and noisy | tone values | random palette picks instead of per-face planes |
| pig again, 1231 colours | — | jitter on a texture that wants flat fills |

`compare-skin.ps1` tells you which situation you are in before you touch anything. **Correlation near zero means rewrite** — there is no structural agreement to preserve. Correlation already high with one metric out means tweak. The pig went −0.03 → 0.82 by being rewritten, and 0.82 → 0.83 by being tuned; the rewrite was worth twenty times the tweak.

### Shading can be per face rather than per pixel

A third discipline, alongside flat fields and dense dithering. The pig has no markings at all — it is one pink, and every face is a **flat plane at its own luminance**, baked to a top-down light:

| face | lum |
|---|---|
| body top | 181 |
| body sides | 166 |
| body front / back | 152 / 151 |
| body belly | 139 |
| head crown | 184 |
| head sides | 159 |
| head underside | 126 |

Twelve colours for the whole animal, roughly one per face. Rolling even three tones per pixel across that destroys it: the planes turn to speckle and the correlation against the reference collapses to nothing.

**Measure each face rect's mean luminance separately** when a creature looks smoothly shaded rather than patterned. It is one command and it hands you the whole texture.

### The generator pattern

Textures are generated by a script in `tools/`, not hand-painted, so they are reproducible and reviewable as a diff.

- Put the **net formula and every box dimension in a header comment**, and state which source file must agree with it. The sheep's fleece-vs-skull depth mismatch is exactly the fact that gets lost otherwise.
- Fill the sheet **fully transparent first**, so a texel no face samples stays empty. A mistake then shows as a hole rather than as plausible-looking noise.
- Use a deterministic hash so regenerating is byte-identical.

---

## 5. Checking it mathematically

`tools/compare-skin.ps1` measures our skins against the reference they were derived from. Run it after any texture change:

```powershell
powershell -NoProfile -File tools\compare-skin.ps1
powershell -NoProfile -File tools\compare-skin.ps1 -Only Cow
```

It checks **both directions at once**, which is the whole point — structurally similar, pixel-wise original:

| metric | want | what it catches |
|---|---|---|
| **Fit** | high (≥ 92%) | our texels painted outside their nets — a net in the wrong place |
| **Corr** | moderate–high | 2×2 block luminance correlation; blocks, not pixels, so dither does not drown it |
| **Mask** | moderate–high | where light and dark sit, after an Otsu split |
| **PatchRatio** | ~0.15–3.0 | **markings scattered where the reference has slabs — the cyborg bug** |
| **Detail** | ~0.7–2.5 | **flat fills where the reference is modelled — the slab bug** |
| **Sat** | ~0.75–1.35 | **washed-out colour — a muted pink against a bright coral** |
| **Lum** | ~0.8–1.2 | systematically darker or brighter than the reference |
| **Overlap** | **low** | our exact colours that also appear in theirs — the legal check |

Every structural metric above is computed on **luminance**, which is why `Sat` had to be added separately: a muted pink and a vivid coral can share a luminance exactly, face for face, and look nothing alike. That is precisely how a washed-out pig passed everything.

**Current state — read this before trusting the metrics.** The four original species (sheep hide, sheep fleece, cow, pig, Bramble) are ours and final. Nine more were authored, **passed every metric above, and were still rejected on sight** — the roster now runs on placeholder art. That is the whole lesson of this section restated: *the table catches gross deviation; the eye is the gate.* A generator that scores well is evidence, not proof.

### Two traps in the measurement itself

**The table is a guide, not a verdict — and this is not a caveat, it is the main finding.** Measured directly: a deliberately flattened pig scored **correlation 0.76 and mask 79.1**, *beating* the properly modelled version's 0.66 and 72.7. Four separate visible faults — a slab body, a zip up the spine, a halo on the face, and panel borders on every rect — passed every check that existed at the time. Metrics catch gross deviation. They do not see what an eye sees in a second.

Both of these also made the tool lie before they were fixed, and both are worth knowing before trusting any image metric.

**A median threshold only works when the two populations are balanced.** Our cow's markings cover about a third of the hide, so the median landed *inside* the hide and split it by its own faint tone variation — reporting ~30 phantom patches where nine discs had been stamped, a PatchRatio of 11.6 against a texture that was actually correct. **Otsu's method** (maximise variance between the two sides) fixed it: the same texture measured 0.9. The art had been right the whole time.

**A metric is meaningless outside its assumptions, and will happily produce a number anyway.** Patch analysis says nothing about a near-uniform fleece (it splits noise) or a densely dithered skin (every pixel its own island). The tool now skips it when the reference's tonal spread is under 18 or its colour count is over 200, rather than emitting a confident wrong answer.

## 6. Verification checklist

Before calling a skin done:

1. Generator runs with **zero errors** (see §6 — errors here are per-pixel).
2. Net extents do not overlap, and each matches `2(w+d) × (d+h)`.
3. **Colour count is in the right band** for the subject — a handful for flat fields, hundreds for mottled ones.
4. Magnify the sheet and look at it. Not optional.
5. Every box's dimensions in code match the header comment in the generator.
6. Look at the thing in game from **several angles** — front, behind, both sides. A face on all six sides of a head is invisible from the front.
7. No reference art anywhere under `assets/`.

---

## 7. PowerShell pitfalls specific to this work

These all fire **once per pixel**, so a 64×64 sheet produces tens of thousands of error lines and will blow up a terminal or a context window.

**Always run a generator with bounded output:**

```powershell
$out = @(& powershell -NoProfile -File tools\make-creature-skins.ps1 2>&1)
$errs = @($out | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] })
Write-Host "lines: $($out.Count) | errors: $($errs.Count)"
$out | Select-Object -First 5; $errs | Select-Object -First 2
```

Bounded, but still surfaces errors — do not filter on words like `error`, because the message may say "Method invocation failed" and match nothing.

- **Int32 overflow.** `$x * 374761393` silently widens to Int64 and the `[int]` cast then throws. Keep hash multipliers small — `73856093`, `19349663`, `83492791` work.
- **Variable names are case insensitive, so a local can overwrite a parameter.** A colour-mixing helper took `[string]$B` and assigned its blue channel to `$b` — the same variable. 186 was coerced to the string `"186"`, `{2:X2}` emitted it verbatim, and the result was a **seven-character hex string** that `Substring(4,2)` read back as `0x18`. Fifteen percent of the goat came out bright yellow and every measurement still looked plausible. Prefix locals inside any function whose parameters are single letters, and validate generated hex on the way in — a six-character check costs nothing and turns this into an immediate stop.
- **`[int]` rounds, it does not truncate.** `[int](0.9 * 3)` is `3` — one past the end of a 3-colour palette. That yields `$null`, which coerces to `''`, and `''.Substring(0,2)` throws. Use `[Math]::Floor` and clamp, in *one* helper — an open-coded copy of the same lookup is how this got missed.
- **A helper that disposes its input** makes "save an intermediate state" a trap. `Save-Bitmap` disposes, so saving a half-finished bitmap kills it and every subsequent `SetPixel` fails. Pass `.Clone()`.
- **`-File` flattens array parameters.** `powershell -File script.ps1 -Files a.png,b.png` arrives as one filename containing commas. Call the script directly instead: `& .\script.ps1 -Files a,b`.
- **`,` binds tighter than arithmetic.** `@($i, $size - 1)` parses as `($i,$size) - 1` and throws.

---

## 8. Worked example: the sheep

Final numbers, as a template for the next creature. One texel is 1/16 of a block.

**Sheet** — the shared `assets/textures/creatures.png` is **128×3552**, and `kCreatureSheetWidth`/`Height` in `Creature.hpp` is the single owner of that number. The sheep still occupies its original first two 64×32 regions: row 0 bare hide and row 32 fleece shell. Selected by texture layer `-3.0`, following the same negative-layer convention the HUD (`-1`) and font (`-2`) use.

**The last 32 rows (1856–1887) are not a species.** They hold the charged Bramble's energy shell — the reference's `creeper_armor` overlay, on the *same net as the Bramble itself* so the same box UVs read it, mostly transparent so only the blue survives the cutout test. `kCreatureSheetWidth/Height` in `Creature.hpp` is the single owner of the size, and `Main.cpp` checks the PNG header against it, because a sheet of the wrong height does not fail — it slides every UV and mistextures the whole roster in silence.

| box | net origin | `w × h × d` | notes |
|---|---|---|---|
| head | `(0, 0)` | 6 × 6 × 8 | upright |
| leg | `(0, 16)` | 4 × 12 × 4 | upright, one net shared by all four |
| body | `(28, 8)` | 8 × 16 × 6 | **lying** — rotated a quarter turn |
| fleece head | `(0, 0)` | 6 × 6 × **6** | shallower than the skull |
| fleece body | `(28, 8)` | 8 × 16 × 6 | same as the body |

Fleece is inflated 0.6 texels on the head, 1.75 on the body, and its head sits so the front edge lands between the ears and the face.

Content, by region: skull mostly fleece with hide only on the jaw underside, the face, and an ear patch on each cheek 1–3 texels back from the front edge. Face: fleece fringe on the top row, hide with eyes on row 2, muzzle on the bottom two rows with fleece in the outer corners. Legs: fleece for the top 4 rows, hide for 7, dark hoof on the last. Body: bare hide, which is what shows when the fleece comes off.

**Palette:** flat fields, ~13 colours.

## 9. Worked example: the Bramble

The other discipline, on the same machinery. One 64×32 skin, no overlay shell, every box upright — so it reuses the sheep's `uprightBox` mapping with no new model code at all.

| box | net origin | `w × h × d` |
|---|---|---|
| head | `(0, 0)` | 8 × 8 × 8 |
| body | `(16, 16)` | 8 × 12 × 4 |
| leg | `(0, 16)` | 4 × 6 × 4 |

Legs 6 + body 12 + head 8 = 26 texels = 1.625 blocks tall.

**Palette:** dense dithering, 857 colours across 864 opaque pixels, luminance 9–184. Face features sit on the head's front rect at `(8, 8)`: sockets as 2×2 blocks at rows 2–3, a mouth widening from a 2-wide notch to a 4-wide block, two fangs below — all dithered rather than flat black.

## 10. Worked example: the cow

Bigger animal, **64-row net** rather than 32 — which is why skins are addressed by an explicit row offset rather than an index. The sheet is one column and each creature names the row it starts at.

| box | net origin | `w × h × d` | notes |
|---|---|---|---|
| head | `(0, 0)` | 8 × 8 × 6 | upright |
| body | `(18, 4)` | 12 × 18 × 10 | **lying** |
| leg | `(0, 16)` | 4 × 12 × 4 | upright, one net for all four |
| horn | `(22, 0)` | 1 × 3 × 1 | upright, one net for both |
| udder | `(52, 0)` | 4 × 6 × 1 | **lying** — belongs to the body group |

The udder catches two traps at once: it rotates with the body rather than standing upright, and its back face is transparent so the band measures narrow. Building it upright drove its 6-texel dimension vertically, straight through the animal's back.

**Palette:** flat fields, 28 colours in the reference. Markings are **coarse blotches**, not speckle — sampled on a 3-texel grid so patches come out several texels across, which is what a hide looks like. Ours: dark brown with grey-white patches, a bone blaze and full-width muzzle, wide-set dark eyes, pink udder, pale horns.

## 11. Worked example: the pig

The short one — the whole animal tops out at **1.0 blocks** against the cow's 1.5, so the head sits *level* with the body rather than above it. Legs are 6 texels where the cow's are 12.

| box | net origin | `w × h × d` | notes |
|---|---|---|---|
| head | `(0, 0)` | 8 × 8 × 8 | upright |
| body | `(28, 8)` | 10 × 16 × 8 | **lying** |
| leg | `(0, 16)` | 4 × 6 × 4 | upright, one net for all four |
| snout | `(16, 16)` | 4 × 3 × 1 | upright; **back face unused**, so its side row measures 6 wide not 10 |

**Palette:** flat per-face planes, 12 colours in the reference and 20 in ours. No markings whatsoever — see "Shading can be per face rather than per pixel" above. Eyes follow the same arrangement as the cow's: pupil outboard, white just inboard. Two nostrils sit on the snout's middle row.

Scores the best structural match of any skin so far: correlation **0.83**, mask agreement **81.3%**, colour overlap **0%**.

---

## 12. Proving a model before authoring its skin

A model and a skin fail in the same way — smeared pixels on the wrong face — and debugging both at once means guessing which is wrong. So they are separated:

```powershell
powershell -NoProfile -File tools\make-reference-creature-atlas.ps1
```

writes `creatures-reference.png` **beside each built `game.exe`**, packing the reference textures at the exact row offsets `Creature.cpp` uses, over the top of our own sheet so finished species keep their art. The game prefers that file when it exists. Delete it to go back to ours.

It never writes under `assets/` — the script refuses — so reference pixels cannot reach a build through the asset copy step. **It began as a debugging aid and is currently also the shipping placeholder for most of the roster**, at the user's explicit request. So *expect* to find one in a normal build right now; that is correct. What must never happen is one surviving into a release. `run.ps1` rebuilds it whenever `creatures.png` is newer, because a stale atlas serves old art silently.

To look at the result, set `creature_showcase` in `settings.cfg` — it freezes the spawner and lines the roster up in front of spawn. `SYSTEM_MEMORY.md` → "Reviewing a model" has the value for each species. Waiting for the right biome to produce the right animal is not a test loop.

### Read the alpha as runs, not as a picture

The character grid in §2 stops being readable past about 64 columns. Printing each row's opaque **runs** scales to any sheet and makes the net arithmetic fall straight out:

```powershell
Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::FromFile((Resolve-Path $path).Path)
for($y=0;$y -lt $b.Height;$y++){ $runs=@(); $s=-1
  for($x=0;$x -le $b.Width;$x++){ $on = ($x -lt $b.Width) -and ($b.GetPixel($x,$y).A -ge 128)
    if($on -and $s -lt 0){$s=$x}; if(-not $on -and $s -ge 0){ $runs += "$s-$($x-1)"; $s=-1 } }
  if($runs.Count){ Write-Host ("{0,3}: {1}" -f $y, ($runs -join '  ')) } }
```

**Never truncate that listing.** The villager and zombie villager are identical row for row up to row 33 and differ only in where their arm rows *stop* — a dump cut short said "same model" with total confidence and shipped arms a third too short.

This is step 1 of the full procedure in §13, "Reading a net you have never seen before". Follow that in order; the steps rule out classes of error that are invisible to each other.

## 13. The 26.2 roster, measured

Every net below was read off the reference alpha rather than taken from a model file — the asset dump has no entity geometry. `Creature.cpp` must agree with these exactly.

**Chicken** (`chicken_temperate.png`, 64×32). Legs are **not** boxes any more: a one-texel strip and an alpha-shaped foot, drawn as crossed quads.

| part | net | `w × h × d` |
|---|---|---|
| head | (0, 0) | 4 × 6 × 3 |
| beak | (14, 0) | 4 × 2 × 2 |
| wattle | (14, 4) | 2 × 2 × 2 |
| body | (0, 9) | 6 × 8 × 6 — **lying** |
| wing | (24, 13) | 1 × 4 × 6 |
| leg strip / foot | (36, 3) 1×5 / (32, 0) 3×3 | flat quads |

There is no comb and no tail box; both are painted on. Adding them meant sampling transparent texels.

**Cat** (`cat_tabby.png`, 64×32).

| part | net | `w × h × d` |
|---|---|---|
| head | (0, 0) | 5 × 4 × 5 |
| body | (20, 0) | 4 × 16 × 6 — **lying** |
| front leg | (40, 0) | 2 × 10 × 2 |
| hind leg | (8, 13) | 2 × 6 × 2 |
| tail, lower / upper | (0, 15) / (4, 15) | 1 × 8 × 1 |
| ear, right / left | (0, 10) / (6, 10) | 1 × 1 × 2 |
| muzzle | (0, 24) | 3 × 2 × 2 — back face unused |

**Camel** (`camel.png`, 128×128, content in the top 64 rows). Everything is authored horizontally; nothing needs a quarter turn.

**Read the neck and head rows carefully — they were swapped for a whole milestone.** The `7 × 8 × 19` is the *neck*, a long low bar running forward from the chest; the `7 × 14 × 7` is the *head*, standing on its far end and carrying the eyes. Built the other way round, the tall box sits back at the shoulder and reads as a slab bolted onto the animal, which is exactly how it shipped until a user looked at it. Mojang's own `geometry.camel` settles it in one fetch.

| part | net | `w × h × d` |
|---|---|---|
| body | (0, 25) | 15 × 12 × 27 |
| hump | (74, 0) | 9 × 5 × 11 |
| **neck** | (60, 24) | 7 × 8 × 19 — the long low bar |
| **head** | (21, 0) | 7 × 14 × 7 — stands on the neck's front end |
| muzzle | (50, 0) | 5 × 5 × 6 |
| ear | (45, 0) / (67, 0) | 3 × 1 × 2 |
| leg | (0,0) (0,26) (58,16) (94,16) | 5 × 21 × 5 |
| tail | (122, 0) | flat 3 × 14 quad |

**Horse family** (`horse_brown.png`, `mule.png`, `donkey.png`, 64×64) share one layout. The only difference is the ear: the horse's is short at (19, 16), the mule's and donkey's are long at (0, 12).

| part | net | `w × h × d` |
|---|---|---|
| body | (0, 32) | 10 × 10 × 22 |
| neck | (0, 35) | 4 × 12 × 7 |
| head | (0, 13) | 6 × 5 × 7 |
| muzzle | (0, 25) | 4 × 5 × 5 |
| ear (horse / long) | (19, 16) / (0, 12) | 2 × 3 × 1 / 2 × 7 × 1 |
| leg | (48, 21) | 4 × 11 × 4 |
| mane | (56, 36) | 2 × 16 × 2 |
| tail | (42, 36) | 3 × 14 × 4 — bottom face unused |

**Llama** (`llama_brown.png`, 128×64). The two 8×8×3 nets at (45, 28) and (45, 41) are chest packs and are not built.

| part | net | `w × h × d` |
|---|---|---|
| head | (0, 0) | 4 × 4 × 9 |
| ear | (17, 0) | 3 × 3 × 2 |
| neck | (0, 14) | 8 × 18 × 6 |
| body | (29, 0) | 12 × 18 × 10 — **lying** |
| leg | (29, 29) | 4 × 14 × 4 |

**Goat** (`goat.png`, 64×64). The awkward one, and worth reading before touching it.

| part | net | `w × h × d` |
|---|---|---|
| body | (1, 1) | 9 × 11 × 16 |
| neck ruff | (0, 28) | 11 × 14 × 11 — **bottom face transparent** |
| head | (34, 46) | 5 × 8 × 10 |
| front leg | (35, 2) / (49, 2) | 3 × 10 × 3 |
| hind leg | (36, 29) / (49, 29) | 3 × 6 × 3 |
| horn (shared) | (12, 55) | 2 × 7 × 2 |
| beard | (23, 56) | 4 × 7 × 1 — top and bottom faces unused |
| ear (shared) | (2, 61) | 3 × 2 × 1 |

Three things here cost time:

- **The head is 5 across and 10 deep, and its eyes are on the *side* rects**, at `(37,58)` and `(55,58)`. Those two mirror exactly about the 10-texel depth, which is what proves the dimensions rather than a plausible-looking guess.
- **The 11×14×11 is not the head**, even though it is the biggest piece and sits where a head would. Its front rect is featureless white and its *bottom* rect is transparent — a head's underside is visible, a neck's is not. It is a woolly shoulder ruff, and it has to overlap the body far enough that the missing bottom never shows.
- **Front and hind legs are different lengths** — 10 and 6 — on four separate nets. The long pair is buried four texels into the chest so that all four hooves still reach the ground.

**Rabbit** (`rabbit_brown.png`, 64×64, content in the top 31 rows).

| part | net | `w × h × d` |
|---|---|---|
| body | (0, 0) | 8 × 6 × 10 |
| ear, right / left | (26, 0) / (32, 0) | 2 × 5 × 1 |
| head | (0, 16) | 5 × 5 × 5 |
| haunch (shared) | (20, 16) | 4 × 4 × 4 |
| front leg | (36, 18) / (44, 18) | 2 × 4 × 2 |
| hind foot | (20, 24) / (36, 24) | 2 × 1 × 6 — lies flat |

There is no tail and no nose net; the nose is a pink texel on the face.

**Wolf** (`wolf.png`, 64×32 — the smallest sheet in the roster).

| part | net | `w × h × d` |
|---|---|---|
| head | (0, 0) | 6 × 6 × 4 |
| snout | (0, 10) | 3 × 3 × 4 |
| mane / shoulder ruff | (21, 0) | 8 × 6 × 7 — laid down a quarter turn |
| ear (shared) | (16, 14) | 2 × 2 × 1 |
| body | (18, 14) | 6 × 9 × 6 — laid down |
| leg (shared, all four) | (0, 18) | 2 × 8 × 2 |
| tail | (9, 18) | 2 × 8 × 2 |

Two traps. The **mane is laid down the same quarter turn as the torso**, not stood upright — on end it is a broad slab that swallows the head, and turned it becomes the tall narrow ruff that makes the animal read as a wolf rather than a dog. And the body's net has its **top rect transparent, not its bottom**: after the quarter turn that rect becomes the world *front*, buried in the mane.

The snout's side band measures 15 columns where the arithmetic predicts 14. The extra column at x=14 is padding and is never sampled.

**Frog** (`frog_temperate.png`, 48×48).

| part | net | `w × h × d` |
|---|---|---|
| eye, right / left | (0, 0) / (0, 5) | 3 × 2 × 3 |
| **belly** | (3, 1) | 7 × 3 × 9 |
| **back** | (0, 13) | 7 × 2 × 9 |
| front leg (shared) | (0, 32) | 2 × 3 × 3 |
| hind leg (shared) | (0, 38) | 2 × 3 × 3 |
| webbed foot ×4 | (12, 35) / (28, 35) / (13, 42) / (27, 42) | flat 4 × 4 sprites |

**The two big nets stack; they do not sit end to end**, and **the alpha says which is on top before colour is consulted at all.** The (3,1) net has **no top rect** and the (0,13) net has **no bottom rect** — they interlock, so (0,13) rests on (3,1). That makes (3,1) the **belly** and (0,13) the **back**, which is the opposite of what this table said for two milestones: the frog shipped with its missing top facing the sky and a hole straight through it, reported as "the top of the frog shows as hollow".

Colour confirms it — the exposed (0,13) top is a saturated `rgb(209,121,75)` back, the exposed (3,1) underside a paler `rgb(203,147,93)` belly — but colour was also what got it wrong the first time, because both *side* bands are patterned and neither distinguishes up from down. **Where a box is missing a top or bottom rect, that absence outranks any colour reading**: an absent face is a statement about what covers it. Read as head-and-body instead, the two nets make an animal twice as long as it is wide, which a frog is not. Being the same width they would also share a side plane and z-fight, so the belly is inset by 0.008 and the patterned back wins.

The unlisted regions are the tongue (a pink strip at x 23-26, rows 13-19) and the mouth interior — both used only by the eating animation, which we do not have.

**Fox** (`fox.png`, 48×32). Every net matched the reference model's own UVs exactly, which is worth knowing as a confidence check on the method.

| part | net | `w × h × d` |
|---|---|---|
| head | (1, 5) | 8 × 6 × 6 |
| ear, right / left | (8, 1) / (15, 1) | 2 × 2 × 1 |
| snout | (6, 18) | 4 × 2 × 3 |
| body | (24, 15) | 6 × 11 × 6 — **lying** |
| tail | (30, 0) | 4 × 9 × 5 |
| leg (two nets, shared front to back) | (4, 24) / (13, 24) | 2 × 6 × 2 |

The head is 6 deep against a body 6 deep, so level with each other their half-heights match and the shared faces z-fight — the head takes `grow = 0.01`, the legs `0.005`. The tail is pitched to **-1.75 rad**, past vertical, so its 9-texel length trails backward and dips rather than standing up.

**Ocelot** (`cat/ocelot.png`, 64×32). **Alpha-identical to `cat_tabby.png`**, row for row — so it reuses the cat's nets, the cat's box layout and the cat's `modelScale` outright, exactly as the mule reuses the horse. Only the pixels differ. See §10's cat table.

**Polar bear** (`bear/polarbear.png`, 128×64). The one that would have gone wrong from reasoning.

| part | net | `w × h × d` |
|---|---|---|
| head | (0, 0) | 7 × 7 × 7 |
| ear (shared) | (26, 0) | 2 × 2 × 1 |
| snout | (0, 44) | 5 × 3 × 3 |
| body, haunches | (0, 19) | 14 × 14 × 11 — **lying** |
| body, chest and shoulders | (39, 0) | 12 × 12 × 10 — **lying**, end to end with the above |
| front leg (shared) | (50, 22) | 4 × 10 × 8 |
| hind leg (shared) | (50, 40) | 4 × 10 × 6 |

**The torso is two boxes end to end, not one and not stacked.** The 12×12×10 is the chest and shoulders, the 14×14×11 the haunches, and they are contiguous on the axis the quarter turn maps to **forward** — together a 26-texel body. Stacking them vertically buries one inside the other and the bear reads as a cube with legs. It is also the second-largest net on the sheet and sits where a head might, so it is exactly the trap the goat's ruff already taught: the real head is the far smaller 7×7×7. **Front legs are 8 deep against the hind pair's 6**, which is what gives a bear its heavy front end; building all four from one net would lose that.

**Panda** (`panda/panda.png`, 64×64).

| part | net | `w × h × d` |
|---|---|---|
| head | (0, 6) | 13 × 10 × 9 |
| nose | (45, 16) | 7 × 5 × 2 |
| ear (shared) | (52, 25) | 5 × 4 × 1 |
| body | (0, 25) | 19 × 26 × 13 — **lying** |
| leg (shared, all four) | (40, 0) | 6 × 9 × 6 |

The body's side band is the **full 64-pixel width of the sheet**, which is the giveaway that this is the widest net in the roster: `2(w+d) = 64`. At 19 across it is 1.19 blocks wide, matching the reference's 1.3 hitbox almost exactly, and its 26-texel length runs forward after the quarter turn — a panda is a barrel, and the numbers say so before any judgement does.

**Slime** (`slime/slime.png`, 64×32). Four cubes and nothing else — the simplest net in the roster.

| part | net | `w × h × d` |
|---|---|---|
| outer shell | (0, 0) | 8 × 8 × 8 — **translucent** |
| inner core | (0, 16) | 6 × 6 × 6 |
| eye, right / left | (32, 0) / (32, 4) | 2 × 2 × 2 |
| mouth | (32, 8) | 1 × 1 × 1 |

**The layering is the animal, and it is the one creature that needed a blended pass.** An opaque core carries the face, and a translucent gel shell sits over it, so the eyes are read *through* the body rather than painted on it. The first attempt drew the shell alone with the face mounted proud of its front, and the user's verdict was immediate: blobs glued to a green box. The eyes have to be **inset into the core**, standing proud of the core but well inside the shell.

That forced `buildMesh` to emit two meshes. Blending depends on draw order and the renderer draws every opaque mesh before any translucent one, so a shell in the opaque mesh would blend against whatever happened to be behind it rather than against its own core — the same lesson water taught at M15c, arriving again on a creature.

All three sizes are this one model at different `modelScale` (1.04 / 2.08 / 4.16), because the reference is 8 texels across and its hitboxes are 0.52, 1.04 and 2.08.

**Spider** (`spider/spider.png`, 64×32).

| part | net | `w × h × d` |
|---|---|---|
| thorax | (0, 0) | 6 × 6 × 6 |
| head | (32, 4) | 8 × 8 × 8 |
| abdomen | (0, 12) | 10 × 8 × 12 |
| leg (shared, all eight) | (18, 0) | 16 × 2 × 2 |

**The leg net is a sideways bar, not an upright limb.** At 16 across and 2 the other two ways it takes the upright mapping with its *width* along the side axis — read as a standing leg it would be a 1-block-tall post.

**Rebuilt at M20j from `geometry.spider.v1.8`, and the earlier note here was wrong.** It used to say the reference bends each leg at a knee, which an axis-aligned box cannot express, and that level legs read correctly anyway. **They do not — they read as an animal lying on its belly, which is what the user reported.** There is no knee: the reference droops each *whole* leg 45° (33.3° for the inner pairs) about its own forward axis, from a joint level with the body, and **that droop is the entire reason a spider stands off the ground**. Thorax, head, abdomen and all eight joints share one height, **texel 9 of 16**, and the leg tips end about a tenth of a block *under* the floor — which the reference does too, and is what makes it read as planted rather than hovering. `barBox` gained its `splay` parameter for this; see §14.

**`cave_spider.png` has byte-identical alpha to `spider.png`**, so it is the same rig at `modelScale` 0.70. Diff the alpha of any new skin against the ones already modelled before deriving anything — that check has now saved two species outright.

**Zombie and skeleton** (`zombie/zombie.png` 64×64, `skeleton/skeleton.png` 64×32). One rig, two limb thicknesses.

| part | net | zombie | skeleton |
|---|---|---|---|
| head | (0, 0) | 8 × 8 × 8 | 8 × 8 × 8 |
| body | (16, 16) | 8 × 12 × 4 | 8 × 12 × 4 |
| arm | (40, 16) | 4 × 12 × 4 | **2 × 12 × 2** |
| leg | (0, 16) | 4 × 12 × 4 | **2 × 12 × 2** |

Head and body match exactly and only the limbs differ, which is what lets one lambda serve both. **Every part needs a `grow`**: a 12-tall limb beside a 12-tall torso shares a face plane exactly, and so does a head sitting on that torso.

The skeleton's body has a **transparent top rect** — the neck hole — and its skull's bottom rect is mostly transparent too. Both are buried, so neither shows; per §14.1 an absent face is a statement about what covers it.

The zombie's arms are pitched a **quarter turn** so its 12-texel arm length runs forward rather than down, which means placing each arm half a length ahead of the shoulder rather than at it.

**Villager** (`villager/villager.png`, 64×64). Its own rig — taller head, deeper body, and the crossed forearms that are most of the silhouette.

| part | net | `w × h × d` |
|---|---|---|
| head | (0, 0) | 8 × 10 × 8 |
| nose | (24, 0) | 2 × 4 × 2 |
| body | (16, 20) | 8 × 12 × 6 |
| upper arm | (44, 22) | 4 × 8 × 4 |
| crossed forearms | (40, 38) | 8 × 4 × 4 |
| leg | (0, 22) | 4 × 12 × 4 |

The head is **10 tall against the biped's 8**, which is what makes a villager's head read as oversized, and the nose is its own 2×4×2 net rather than painted on. `modelScale` is 0.92 because the taller head pushes the crown to 2.13 against a 1.95 hitbox.

**Husk** (`zombie/husk.png`, 64×64). **Byte-identical alpha to `zombie.png`**, row for row, so it reuses the zombie's nets, box layout and arms-out pose outright — the third species proved this way, after the ocelot and the cave spider. Only the pixels differ. See the zombie column above.

**Blackbone** — our name for `skeleton/wither_skeleton.png` (64×32), since *Wither* is a coined name. Its **nets are the skeleton's exactly**: head 8×8×8 at (0,0), body 8×12×4 at (16,16), arm 2×12×2 at (40,16), leg 2×12×2 at (0,16), with the same transparent top rect on the body for the neck hole. A whole-sheet alpha diff reports it as *differing* from the skeleton, which is a false negative worth knowing about — the difference is interior detail, the ribcage and skull holes, not the net boundaries. Only the run widths at the net edges decide the dimensions. `modelScale` is **1.20**, taking the shared 2.00 rig to the reference's 2.4 hitbox.

**Silverfish** (`silverfish/silverfish.png`, 64×32). Seven boxes, no legs, and every one falls straight out of §14.1 with nothing ambiguous to resolve.

| part | net | `w × h × d` |
|---|---|---|
| head | (0, 0) | 3 × 2 × 2 |
| neck | (0, 4) | 4 × 3 × 2 |
| thorax | (0, 9) | 6 × 4 × 3 |
| abdomen | (0, 16) | 3 × 3 × 3 |
| tail, first | (0, 22) | 2 × 2 × 3 |
| tail, second | (11, 0) | 2 × 1 × 2 |
| tail, tip | (13, 4) | 1 × 1 × 2 |

The segments **butt end to end** rather than overlapping, 17 texels front to back, so the animal is a full block long against a 0.4 hitbox — the reference's own proportions, and the same over-reach the horse's head already has. Butting means every junction is a shared face plane, so each box takes a small `grow`; the two hindmost pairs additionally need *different* grows from each other, because the last two are both one texel tall at the same height and the two before them are both two texels across. See §14.5.

There is no leg net at all. The walk is a **wave travelling down the body** — each segment offset sideways by `sin(gait − index × 0.8)` with the amplitude scaled by how far back it sits, so the head leads and the tail whips. `gaitSwing` is therefore a distance in blocks here rather than a leg angle.

**Stray and Bogged** (`skeleton/stray.png`, `skeleton/bogged.png`, both 64×32). **Both are the skeleton's nets exactly**, run for run — the same false negative the Blackbone gives on a whole-sheet alpha diff. `modelScale` 1.00 for both.

Two things on the bogged that are *not* built, recorded so nobody has to re-measure them:

- Its base skin carries **two small mushroom sprites** in an otherwise unused region at **x 50–55, rows 16–19 and 29–31**. The reference draws them as flat crossed quads, the way the chicken's foot and the frog's webbing already are here. Skipped for now, so a bogged reads as a differently-coloured skeleton.
- `bogged_overlay.png` and `stray_overlay.png` are **inflated shell layers**, the same idea as the sheep's fleece — the bogged's moss and the stray's icy cloak. Also skipped. Both would cost one extra skin row and a second pass of grown boxes.

**Zombie Villager** (`zombie_villager/zombie_villager.png`, 64×64). It reuses the villager rig — head 8×10×8 at (0,0), nose 2×4×2 at (24,0), body 8×12×6 at (16,20), leg 4×12×4 at (0,22), arm at (44,22) — and `modelScale` 0.92, since the head and hitbox are the villager's.

**The arm is the one net that differs, and only four rows of the sheet say so.** The villager's arm side rows run 26–33, so its arm is **4×8×4** and it needs the separate 8×4×4 crossbar at (40,38) to complete the folded assembly. The zombie villager's run 26–**37**, so its arm is **4×12×4** — a full zombie-length limb — and there is no crossbar at all, because it never folds them.

That single difference is a good argument for dumping runs over the *whole* sheet before deciding two skins share a rig. Every net matched up to row 33; the two sheets are otherwise identical row for row; and a truncated dump reported them as the same model. The pose flag on the shared rig therefore switches the arm's **length as well as its angle**.

**Witch** (`witch/witch.png`, 64×128 — the tallest sheet in the roster). Rows 0–45 are the villager rig unchanged, folded arms and all. Everything below row 63 is the hat, and it reads out of §14.1 with no ambiguity at all:

| part | net | `w × h × d` |
|---|---|---|
| wart | (0, 0) | 1 × 1 × 1 |
| hat brim | (0, 64) | 10 × 2 × 10 |
| hat cone, lower | (0, 76) | 7 × 4 × 7 |
| hat cone, upper | (0, 87) | 4 × 4 × 4 |
| hat tip | (0, 95) | 1 × 2 × 1 |

The wart sits at the sheet's very first corner and does not collide with the head net, whose band starts eight texels in. The four hat boxes **stack off the crown** and add 12 texels, taking the model to 2.65 blocks against a 1.95 hitbox — the reference's own proportions, and the tallest overshoot in the roster. Each takes its own descending `grow` (0.005 → 0.002) so consecutive boxes overlap rather than butt, since a butt joint is a shared plane.

**Wandering Trader** (`wandering_trader/wandering_trader.png`, 64×64). The villager rig again — arm side rows stop at 33, so it keeps the short arm *and* the crossbar, unlike the zombie villager.

Its one extra part is a **head wrap**: the standard overlay slot at **(32,0), 8×10×8**, the same net as the head, inflated a third of a texel. It is transparent everywhere except side rows 11–16, which is why it reads as a turban rather than a cap — and it relies on the fragment shader's alpha test for creature layers, exactly as the sheep's fleece does.

**Princepin** — our name for `piglin/piglin.png` (64×64), since *Piglin* is coined. Its torso and limbs are the biped rig at the **standard player-skin offsets**, and it is the first model here to use **separate nets per side**.

| part | net | `w × h × d` |
|---|---|---|
| head | (0, 0) | **10** × 8 × 8 |
| ear, right / left | (39, 6) / (51, 6) | 1 × 5 × 4 |
| snout | (31, 1) | 4 × 4 × 1 |
| tusk, right / left | (2, 0) / (2, 4) | 1 × 2 × 1 |
| body | (16, 16) | 8 × 12 × 4 |
| leg, right / left | (0, 16) / (16, 48) | 4 × 12 × 4 |
| arm, right / left | (40, 16) / (32, 48) | 4 × 12 × 4 |

The head is **10 wide against the biped's 8**, which is the whole silhouette — a piglin is broad-faced. Body, arms and legs are identical to the zombie's, so only the head needed deriving.

### The aquatic roster, measured (M20i)

Ten species added on 2026-08-05. Nine of them are the first models here that are **not** a spine with legs, and the drowned is the cheapest addition in the roster — it is the zombie rig twice over.

**⛔ Read this before transcribing any of it.** Every net below was **measured off the texture**, not copied out of `bedrock-samples`. Our reference art is **Java's** and the geometry is **Bedrock's**; for most mobs the two sheets agree, and for the dolphin they do not — Bedrock puts its tail at `(0,33)` and its tail fin at `(0,49)`, where the Java sheet has nothing at all, so its whole back half rendered as empty with no error anywhere. The turtle's head starts at `u = 3` where the geometry says `2`, and at `2` the top face sampled a transparent column and showed a slot across the crown. **After transcribing any UV, dump the alpha of that rect** — full coverage means the origin is right, partial means it has slid, empty means the part is about to vanish silently.

**Drowned** (`drowned/drowned.png` + `drowned/drowned_outer_layer.png`, 64×64 each). The zombie's biped rig, unchanged, drawn twice: the body, then the outer layer as a second shell inflated a quarter of a texel. The layer is an ordinary alpha-tested cutout, exactly like the sheep's fleece, so it needed no rendering work at all. It is the only aquatic that walks the seabed rather than swimming.

**Cod** (`fish/cod.png`, 32×32).

| part | net | `w × h × d` |
|---|---|---|
| body | (0, 0) | 2 × 4 × 7 |
| head | (11, 0) | 2 × 4 × 3 |
| nose | (0, 0) | 2 × 3 × 1 |
| tail fin | (20, 1) | 0 × 4 × 6 |
| dorsal | (20, −6) | 0 × 1 × 6 |
| ventral | (22, −1) | 0 × 1 × 2 |
| pectoral, right / left | (24, 4) / (24, 1) | 2 × 1 × 2 |

**Salmon** (`fish/salmon.png`, 32×32). Two body halves rather than one long box — the reference bends a salmon in the middle. Ours draws them straight, because that bend is an animation we do not have.

| part | net | `w × h × d` |
|---|---|---|
| body, front / back | (0, 0) / (0, 13) | 3 × 5 × 8 |
| head | (22, 0) | 2 × 4 × 3 |
| dorsal, front / back | (4, 2) / (2, 3) | 0 × 2 × 2 / 0 × 2 × 3 |
| tail fin | (20, 10) | 0 × 5 × 6 |
| pectoral, right / left | (2, 0) | 2 × 0 × 2 |

**Pufferfish** (`fish/pufferfish.png`, 32×32). **Three separate geometries, not one model scaled** — the spines are real parts that only exist once it is inflated, so `puff` picks a model rather than a size. The three bodies are 3, 5 and 8 texels wide, so the handover happens at the **geometric mean** of each adjacent pair, with the scale ramped continuously across it; what the eye sees is one animal swelling, with only the spines arriving.

| stage | part | net | `w × h × d` |
|---|---|---|---|
| small | body | (0, 27) | 3 × 2 × 3 |
| small | eye, right / left | (24, 6) / (28, 6) | 1 × 1 × 1 |
| small | tail fin | (−3, 0) | 3 × 0 × 3 |
| small | fin, right / left | (25, 0) | 1 × 1 × 2 |
| medium | body | (12, 22) | 5 × 5 × 5 |
| medium | eye, right / left | (24, 3) / (24, 0) | 2 × 1 × 2 |
| medium | spine, top / bottom ×2 | (19,17) (11,17) (18,20) | 5 × 1 × 0 |
| medium | spine, side ×4 | (1,17) (5,17) (9,17) | 1 × 5 × 0 |
| large | body | (0, 0) | 8 × 8 × 8 |
| large | eye, right / left | (24, 3) / (24, 0) | 2 × 1 × 2 |
| large | spike, top ×3 / bottom ×3 | (14, 16) / (14, 19) | 8 × 1 × 1 |
| large | spike, flank ×6 | (0/4/8, 16) | 1 × 8 × 1 |

Every spine and spike is pushed a sixteenth of a texel off the body. Flush is what the reference does, and flush means a shared face plane, and both of ours are drawn from both sides.

**Squid** and **Glow Squid** (`squid/squid.png`, `squid/glow_squid.png`, 64×32). One rig, one net, two skin rows; the glow squid additionally sets its own light to full. A bell with eight tentacles at 45° spacing — the first model here that is a **ring of limbs** rather than a spine with legs.

| part | net | `w × h × d` |
|---|---|---|
| bell | (0, 0) | 12 × 16 × 12 |
| tentacle ×8 | (48, 0) | 2 × 18 × 2 |

`legBox` hangs each tentacle from its top face and the derived joint lands exactly on the underside of the bell, so the ring hinges where it meets the body. The splay is **negated**, because a positive pitch swings a hanging limb toward the ring's centre and a flare is outward.

**Turtle** (`turtle/big_sea_turtle.png`, 128×64). The shell and the belly plate are authored upright and laid down a quarter turn — the reference's own `bind_pose_rotation` of 90°, which is what `lyingBox` is for. The head and the four flippers are already flat and take no rotation.

| part | net | `w × h × d` |
|---|---|---|
| shell | (6, 37) | 19 × 20 × 6 |
| belly plate | (30, 1) | 11 × 18 × 3 |
| head | **(3, 0)** | 6 × 5 × 6 |
| front flipper, right / left | (26, 30) / (26, 24) | 13 × 1 × 5 |
| rear flipper, right / left | (0, 23) / (0, 12) | 4 × 1 × 10 |

The shell is centred against the **flippers** rather than by eye: the front pair sit at z −6..−1 and the rear at 11..21, so a body spanning −8..12 meets both. The head is **sunk into** the shell and set a shade below its roof, because its crown originally landed on exactly the shell's top plane.

**Dolphin** (`dolphin/dolphin.png`, 64×64). **Body, tail and tail fin are the Java sheet's origins, not Bedrock's** — see the warning above.

| part | net | `w × h × d` |
|---|---|---|
| body | (22, 0) | 8 × 7 × 13 |
| head | (0, 0) | 8 × 7 × 6 |
| nose | (0, 13) | 2 × 2 × 4 |
| tail | (0, 19) | 4 × 5 × 11 |
| tail fin | (19, 20) | 10 × 1 × 6 |
| dorsal | (52, 0) | 1 × 5 × 4 |
| pectoral, right / left | (44, 27) | 8 × 1 × 4 |

The head's cross-section is the body's **exactly** — 8 by 7 — so the two met at a plane and their touching faces were the same rectangle in the same place, drawn from both sides. It is grown and pushed back into the body instead. The beak had the same fault against the head. The dorsal is swept 30° and the pectorals 20°.

**Axolotl** (`axolotl/axolotl_*.png`, 64×64 each). **Five liveries, each a whole net on its own 64 rows**, selected by `Creature::variant`.

| part | net | `w × h × d` |
|---|---|---|
| body | (0, 11) | 8 × 4 × 10 |
| back ridge | (2, 17) | 0 × 5 × 9 |
| tail | (2, 19) | 0 × 5 × 12 |
| head | (0, 1) | 8 × 5 × 5 |
| gill, right / left | (11, 40) / (0, 40) | 3 × 7 × 0 |
| gill, crown | (3, 37) | 8 × 3 × 0 |
| limb ×4 | (2, 13) | 3 × 5 × 0 |

**The gills are three real boxes cut by alpha, not decoration** — they are what makes the animal read as an axolotl. The tail sways about **where it meets the body**, not about the animal's middle, or its root swings clear of the hips on every beat. The limbs are splayed out and down rather than hanging straight, which is what lets a sprawling amphibian's legs reach the floor at all. **In water the tail does all the work and the legs hang still; on land it is the other way about**, and that is the whole difference between its two gaits.

Two traps here. The model sits below its own feet in the reference — the limbs hang to −4 — so the whole thing is **lifted** to stand in its box rather than through it. And it deliberately does **not** use `beginHead`: that helper rebuilds the frame from the creature's own position, which would throw the lift away and drop the head below the body.

**Tropical Fish** (`fish/tropical_a.png`, `tropical_b.png` and six pattern sheets each, 32×32). **Twelve fish from two body shapes crossed with six patterns.** The pattern rides over the body as a second cutout shell a fraction larger — the sheep's fleece arrangement — which is what avoids authoring all twelve as separate skins. It is a shell rather than a runtime tint deliberately: runtime tinting is exactly what produced the washed-out grass at M19a.

| shape | part | net | `w × h × d` |
|---|---|---|---|
| flat (A) | body | (0, 0) | 2 × 3 × 6 |
| flat (A) | dorsal | (10, −6) | 0 × 4 × 6 |
| flat (A) | tail fin | (24, −4) | 0 × 3 × 4 |
| tall (B) | body | (0, 20) | 2 × 6 × 6 |
| tall (B) | ventral | (20, 21) | 0 × 5 × 6 |
| tall (B) | dorsal | (20, 10) | 0 × 5 × 6 |
| tall (B) | tail fin | (21, 16) | 0 × 6 × 5 |
| both | pectoral, right / left | (2, 12) / (2, 16) | 2 × 2 × 0 |

The pectorals take **body art only** — the pattern sheet does not paint them.

### A villager-family overlay slot we do not build

The villager, zombie villager, wandering trader and witch all carry an extra region at roughly **x 0–27, rows 38–63** — a robe or skirt layer over the body and legs. The villager has a small version of it and has shipped without it since M20b, looking correct, so all four skip it. Recorded here so nobody re-measures it; it would cost one more pass of inflated boxes and an alpha-tested shell, the same shape as the sheep's fleece.

### Reading a net you have never seen before

The wolf and frog went from raw PNG to a model that reads correctly on the **second** render each. That was not luck; it was an order of operations, and it is worth following because every step rules out a class of error the next step would otherwise hide.

**1. Alpha runs per row, before anything else.** `tools/analyze-alpha.ps1` finds connected islands, but touching nets merge into one island and it will lie to you. Printing each row's opaque runs does not. Then the arithmetic falls straight out of two facts:

- a **band** row is `2w` wide starting at `u + d` — the top and bottom rects side by side
- a **side** row is `2(w + d)` wide starting at `u`
- the two row *counts* are `d` and `h`

So the wolf's head is unambiguous the moment you see rows 0-3 spanning x 4-15 and rows 4-9 spanning x 0-19: `2w = 12` so `w = 6`; `2(w+d) = 20` so `d = 4`; four band rows confirm `d = 4`; six side rows give `h = 6`.

**2. A measured band narrower than `2w` is information, not an error.** It means one of the two rects is transparent, and *which* one tells you where the box is buried. The wolf's body shows 6 columns where 12 are predicted, at `u+d+w` rather than `u+d` — so the **top** rect is the missing one, and after the torso's quarter turn that is the world front, which is where the mane sits. That single observation placed the mane before a line of code was written.

**3. Where arithmetic runs out, use colour.** The frog's two 7×9 nets are arithmetically interchangeable — same footprint, both plausible as "head" and "body". Averaging their side bands settles it in one command: patterned orange versus flat pale tan is a back and a belly, so they *stack*. Guessing would have produced an animal twice its proper length, and every metric would still have passed. This is the same lesson the cow's udder taught (`CLAUDE.md`): identify an unknown region by measuring its colour, not by reasoning about what it ought to be.

**4. Check for shared extents before rendering, not after.** Every creature quad is double-sided, so two boxes that overlap while sharing an extent on any axis will z-fight, and the symptom is a flickering *triangle diagonal* rather than anything that looks like a modelling mistake. Listing the half-extents and scanning for duplicates takes a minute. The camel cost a user bug report for exactly this; the frog's two 7-wide nets were caught in advance from the same checklist. **And check for boxes that merely *touch*, which this scan does not catch** — see §14.5c.

**4b. If you took the net from `bedrock-samples`, verify it against the alpha.** Our reference art is **Java's** and the geometry is **Bedrock's**. They agree for nearly every mob, and when they do not the failure is silent: a rect pointed at empty pixels renders **nothing at all**, with no error, no warning and no log line. Dump the alpha of each transcribed rect — full coverage means the origin is right, partial means it has slid a few texels, empty means the part is about to vanish. The dolphin lost its entire back half to this.

**5. Then render, and look at it.** Two renders is the budget. The first exposes gross placement — on the wolf, a head sunk into the ruff. The second confirms. If it takes a third, the problem is usually **pose, not dimensions** (see below), and no amount of nudging numbers will find it.

The order matters because each step's failure mode is invisible to the next. A wrong `w` produces a plausible-looking animal. A wrong *interpretation* of two correct nets produces a plausible-looking animal. A shared extent produces a correct animal that flickers. Only the eye catches the last one, and only after the first three are already right.

### A net is not a pose

Every one of the nets above was measured correctly and the rabbit still came out
as a squat brick with its head buried in its chest, because every box was level.
The dimensions were never the problem. Three tilts fixed it without changing a
single number: the body pitched so the haunches ride high and the head sits low
and forward, the head tipped slightly up, and the ears raked back off the crown.

The same applies to the horse family. With a level head, the pale muzzle sits
beside the eye and reads as a cheek patch rather than a nose — which is exactly
how it was reported. Tilting the neck forward and the head and muzzle down puts
the nose where a nose belongs.

`uprightBox` therefore takes an optional `pitch`, in radians, positive meaning
nose-down about the box's side axis. It is a proper rotation of the local axes,
so a pitch of zero is byte-identical to the old behaviour and no existing call
site changed.

A large pitch rotates the net along with the box, which is worth checking before
assuming a pose needs re-authored art. The rabbit's torso now sits at 85° off
level, and the net follows it into the right place on its own: the belly rect
ends up facing forward and the rump rect downward, which is how a rabbit on its
haunches is actually arranged.

### Scale is per species, not per net

The horse, mule and donkey share one model and one net layout; they differ only
in `modelScale` — 1.0, 0.92 and 0.87, matching the original. The scale multiplies
geometry and placement only, never the UV rectangles, which is why it lives
beside the boxes rather than in the net dimensions.


---

## 14. Formula reference

Everything on this page in one place, so a future session can check arithmetic
without re-deriving it. `kTexel = 1/16`.

### 14.1 Net arithmetic

For a box `w` wide (side), `h` high (up), `d` deep (forward), origin `(u, v)`:

```
band row (top+bottom):   width 2w   starting at x = u + d,   for d rows from v
side row (all 4 sides):  width 2(w+d) starting at x = u,     for h rows from v+d
whole net:               2(w+d) across, d+h down
```

Inverting from an alpha map — this is the working direction:

```
d = number of band rows            = the top band's inset from the main band's left edge
w = (band width) / 2               ... but see below
h = number of side rows
check: side width must equal 2(w+d)
```

**When the band is exactly half the predicted width, one rect is transparent and
`w` = the measured width, not half of it.** Which rect is missing tells you
where the box is buried:

| Opaque band sits at | Missing rect | Meaning after a `lyingBox` quarter turn |
|---|---|---|
| `u+d` | bottom | the underside is hidden |
| `u+d+w` | **top** | the net's top becomes the **world front** — something covers the front |

### 14.2 Box extents in world units

`uprightBox(centre, netW, netH, netD, u, v, vBase, grow, pitch, growSide, roll)`:

```
halfSide    = (netW * kTexel / 2 + grow + growSide) * modelScale
halfHeight  = (netH * kTexel / 2 + grow) * modelScale
halfDepth   = (netD * kTexel / 2 + grow) * modelScale

depthAxis   =  forward * cos(pitch) - up * sin(pitch)
pitched     =  forward * sin(pitch) + up * cos(pitch)

sideAxis    =  side * cos(roll) + pitched * sin(roll)
heightAxis  =  pitched * cos(roll) - side * sin(roll)
```

`grow` swells a box on **every** axis at once. That is right for breaking a
shared plane and wrong for reaching toward a neighbour, because it thickens the
box as much as it lengthens it. **`growSide` extends the side axis alone**, for a
part that has to span between two others without getting fatter than them — the
villager's crossed forearms are the case it exists for.

Positive `pitch` is **nose-down**. So a **negative** pitch raises the nose, and
at `pitch = -1.4835` (-85°) the depth axis is essentially straight up.

**`roll` tips the box about its own forward axis**, where `pitch` tips it about
the side axis. It exists for the chicken's wings and is the only rotation that
lifts a part *outward* rather than tilting it forward. Two things about using it:
the two rotations compose in that order, `pitch` then `roll`, so a part needing
both is easier to reason about if only one is non-zero; and **a limb must be
rotated about its pivot, not its centre** — place the box by rotating its offset
*from the shoulder*, because centre-rotation swings the tip out and the root
straight into the body.

`lyingBox` is a fixed quarter turn: `netW` -> side, `netH` -> **forward**,
`netD` -> **up**. Its extents are therefore

```
halfSide = netW * kTexel / 2   halfForward = netH * kTexel / 2   halfUp = netD * kTexel / 2
```

**`legBox` is `uprightBox` turned about a joint instead of about its centre.**
It takes the same arguments plus an angle, and the position it takes is where
the box sits **at rest** - so at an angle of zero it emits exactly the box
`uprightBox` would have, which is what let the roster convert one species at a
time. The pivot is *derived, never authored*:

```
overhang = min(netW, netD) * kTexel / 2        // material left above the joint
hang     = halfHeight - overhang
pivot    = restCentre + up * hang
centre   = pivot - heightAxis(pitch, roll) * hang
```

The overhang is the reference's own trick and the reason a limb does not tear
open at the hip: tilting swings the top face's corners by half the in-plane
extent times `sin(angle)`, so that much box has to stay above the joint, buried
in whatever it hangs from. Checked against the two shapes the reference gives
us, it reproduces both - a `4x12x4` biped arm pivoting 2 texels down from its
top, and a `1x4x6` wing pivoting effectively at its top face.

**`barBox` is for a limb that runs sideways and pivots at its inner end** - the
spider's eight legs. Its swing is a **yaw**, not a tilt, so it rotates the local
`forward`/`side` axes exactly as the head turn does and then places the box so
its inner end lands on the pivot. It also takes a **`splay`**, which droops the
leg about its *own* forward axis after the fan - so the outer end drops however
the leg happens to be pointing. Both axes rotate together rather than only the
long one, so the box stays a rigid body and its cut ends stay square to it.
**That droop is what stands a spider off the ground**; without it the body sits
on the floor with eight level bars sticking out of it.

**Rotating sweeps a box through space it did not occupy at rest, so run §14.5's
shared-extent check at the swing extremes, not just at rest** - and check the
*hip* first, because that is where the sweep concentrates.

**Which end of the top and bottom rects is the front.** The top rect folds *down*
to meet the front face, so its **last** row is the front edge and its first row is
the back; the bottom rect mirrors it, first row front. `uprightBox` had both
reversed for the whole roster, and **nothing could have caught it** - every
model built so far has top and bottom rects near enough symmetric front to back,
so the flip is invisible on all of them. It surfaced only on a model whose back
was painted asymmetrically. If a new model's top-rect artwork looks mirrored,
this is the first thing to check.

### 14.3 Vertical reach of a pitched box

The world-space half-height of a pitched box is **not** `halfHeight` — both axes
contribute:

$$\text{halfY} = |\cos p| \cdot \text{halfHeight} + |\sin p| \cdot \text{halfDepth}$$

$$\text{halfForward} = |\sin p| \cdot \text{halfHeight} + |\cos p| \cdot \text{halfDepth}$$

At `p = -85°` a 6-high, 10-deep torso is `0.087*0.1875 + 0.996*0.3125 = 0.328`
half-height — i.e. **0.656 tall**, not 0.375. This is why standing a box on end
forces `modelScale` down hard.

### 14.4 Choosing `modelScale`

$$\texttt{modelScale} = \frac{\text{target world height}}{\text{tallest box's top in model units}}$$

Target the **hitbox height** from `SYSTEM_MEMORY.md`'s size table for the head or
crown, and let ears/horns overshoot. Worked: the rabbit's ear tips reach 1.335
model units; for a 0.64 m ear tip, `modelScale = 0.64 / 1.335 = 0.48`.

### 14.5 The z-fighting check — run this before every render

**Every creature quad is double-sided.** Two boxes that overlap while sharing an
extent on any axis will z-fight, and the symptom is a flickering *triangle
diagonal*, not anything that looks like a modelling error.

For every pair of boxes that overlap in all three axes, check:

```
halfSide_A   != halfSide_B
halfHeight_A != halfHeight_B
halfDepth_A  != halfDepth_B
```

If any pair matches, **grow the box carrying the artwork** by `+0.01` (about a
sixth of a texel, visually undetectable) rather than insetting the one in front —
insetting cascades and merely relocates the conflict onto the next neighbour.

Safe separation is ~0.01 world units. Depth precision at distance `z` with near
plane `n` and a 24-bit buffer is roughly `z^2 / (n * 2^24)`, so at 20 m with
`n = 0.1` that is 0.24 mm — 0.01 m is ~40 depth units of margin.

### 14.5b When the reference itself puts two faces on one plane

Minecraft never draws the inward-facing side of a surface, so its models
cheerfully butt boxes together with **coincident faces**. Ours draws every
creature quad from both sides, so each coincident pair is somewhere to flicker.
Copying a reference model's exact offsets will therefore reproduce a flicker the
original does not have.

The villager's arms are the worked example: the reference lines up **three** pairs
at once — the undersides, fronts and backs of the crossed forearms against the
upper arms.

There are only three states, and two of them are wrong:

| Bridging part vs its neighbours | Result |
|---|---|
| **Bigger** | bulges — reads as a different, fatter part |
| **Equal** | the shared planes flicker |
| **Smaller** | tucks inside, but reads as recessed |

**The way out is not to resize at all.** Match the cross-sections exactly, then
**offset the bridging box a fraction of a texel along its own height and depth
axes**. That separates every shared plane while leaving the part the same size to
the eye. Roughly `0.006` world units is right: about a sixteenth of a texel,
invisible, and some 25× the depth precision at normal viewing range.

**Size and flicker are different problems.** Three rounds were spent trading one
for the other on this join before that was noticed — each "fix" resized the part
to cure a fight that resizing cannot cure.

### 14.5c Two boxes that merely *touch* have the same problem

§14.5's scan looks for shared extents among boxes that **overlap**. Two boxes that
sit face to face overlap in nothing at all and still put two double-sided quads on
exactly one plane, so the scan reports clean and the model flickers anyway.

It cost two bug reports on the same day. A turtle's head resting on the top plane
of its shell; a dolphin's head meeting its body at identical 8 × 7 cross-sections.
Both looked correct in a still frame and both flickered the moment anything moved.

**The cure is §14.5b's, applied for a different reason:** overlap them slightly,
with different `grow` values, rather than letting the faces meet. The general rule
is now settled — **a coincident plane is a coincident plane however the boxes
arrived at it**, and the check to run is "does any face of A lie on any face of
B", not "do A and B overlap".

Where a whole chain of parts is involved — the pufferfish's twelve spikes, the
witch's four stacked hat boxes — give each one its own **descending** `grow` so
consecutive boxes overlap rather than butt.

### 14.6 Hop physics (species with `hops`)
```
arc height   = launch^2 / (2 * gravity)
airtime      = 2 * launch / gravity
duty cycle   = airtime / (airtime + gather)
burst speed  = speed / duty cycle
hop length   = burst speed * airtime
```

**All the ground is covered in the air**, so the burst must exceed the species'
average speed by exactly the fraction of the cycle spent landed. Current values
with `gravity = 26`:

| Species | launch | gather | arc | airtime | duty | hop length (walk) |
|---|---|---|---|---|---|---|
| Rabbit | 4.2 | 0.18 s | 0.34 m | 0.32 s | 0.64 | ~1.0 m |
| Frog | 6.5 | 0.55 s | 0.81 m | 0.50 s | 0.48 | ~1.05 m |

The pose is read back out of `velocity.y`, never from a counter:

```
phase = clamp(velocity.y / launch, -1, 1)     // +1 push-off, 0 apex, -1 landing
tuck  = onGround ? 0 : 1 - |phase|            // folded at the apex
reach = onGround ? 0 : -phase                 // trailing at push-off, forward to land
```

### 14.7 Pixel-count checks

A silhouette is verified by counting, not by looking:

```
full perimeter of an NxN tile = 4N - 4        (16x16 -> 60)
a 45-degree corner cut of k pixels            = k(k+1)/2 removed per corner
```

The crafting table's corners were verified this way: 20 rim (four 5-pixel
diagonals), 24 border, 64 grid, 108 body. Every count matched the reference before
any judgement about colour was made.