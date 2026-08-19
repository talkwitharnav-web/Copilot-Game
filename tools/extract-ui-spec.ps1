# Derives assets\ui\ui-atlas.json - the machine-readable inventory of every UI
# sprite in the reference art - by measuring the art itself.
#
#   powershell -NoProfile -File tools\extract-ui-spec.ps1
#   powershell -NoProfile -File tools\extract-ui-spec.ps1 -Check   # fails if stale
#
# WHY THIS IS A SCRIPT AND NOT A HAND-WRITTEN FILE
#
# A sprite's size, its nine-slice border and the rectangle it actually occupies
# are all facts about a PNG on disk. Typing them into a JSON file makes a second
# copy that nothing keeps honest, which is this project's single commonest bug
# shape - a value derived somewhere other than the one place that owns it. So
# the art owns the numbers and this script reads them:
#
#   size          System.Drawing, the real pixel dimensions
#   nine-slice    the sprite's own `<name>.png.mcmeta` sidecar, which is
#                 vanilla's authoritative declaration of how it stretches
#   drawn rect    the bounding box of non-transparent pixels, MEASURED, so the
#                 classic 176x166 inventory rect inside a 256x256 sheet is read
#                 off the art instead of remembered. "Rectangles guessed out of
#                 an image instead of read from the model that names them" has
#                 cost this project four playtest rounds already.
#
# A sprite with no .mcmeta is not a hole: modern Minecraft's default GUI scaling
# is `stretch`, and this file writes that out explicitly for every one of them
# so a reader never has to know the default.
#
# THE ANTI-HOLE GUARD. Every folder in the reference gui tree must appear in
# $groups below. A folder that is not described throws rather than being quietly
# skipped, so a newer reference drop cannot silently lose sprites.
#
# ENCODING. Written with UTF8Encoding($false) - UTF-8 with no byte-order mark.
# This project has been bitten twice by an encoding that looked fine in an
# editor; see tools\check-encoding.ps1.
#
# WHAT IT DOES NOT DO. It copies no pixels. The art stays in reference/, which
# is gitignored and never ships; this is metadata about it and nothing more.

param(
    [switch]$Check,
    [string]$Out = "assets\ui\ui-atlas.json"
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$schemaVersion = 1
$referenceVersion = 'minecraft-assets-26.2'

$root = Split-Path -Parent $PSScriptRoot
$referenceRoot = Join-Path $root 'reference'
$guiRelative = 'minecraft-assets-26.2/minecraft-assets-26.2/assets/minecraft/textures/gui'
$guiRoot = Join-Path $referenceRoot ($guiRelative -replace '/', '\')
$bedrockRelative = 'ui-icons'
$bedrockRoot = Join-Path $referenceRoot $bedrockRelative

# reference/ is gitignored on purpose, so it is genuinely absent on a fresh
# clone. Say which tool refills it rather than reporting a bare missing path.
if (-not (Test-Path $referenceRoot)) {
    throw "No reference/ folder at $referenceRoot. It is gitignored, so it does not survive a clone: restore the Mojang asset drop and re-run. Nothing else in this script can work without it."
}
if (-not (Test-Path $guiRoot)) {
    throw "No reference GUI tree at $guiRoot. Expected the $referenceVersion asset drop unpacked under reference/."
}
if (-not (Test-Path $bedrockRoot)) {
    throw "No reference/ui-icons at $bedrockRoot. Run tools\extract-ui-icons.ps1 to rebuild it from reference\crafting-ui.png."
}

# ---------------------------------------------------------------------------
# The groups.
#
# Source is relative to the gui root; '' is the gui root's own loose files. Each
# folder is read NON-recursively, because a subfolder is its own group with its
# own purpose. Id is the namespace a sprite id is built from - `sprites/` is
# stripped, since it says nothing, and the pre-split full-screen sheets under
# gui\container are called `screen/` so they cannot collide with the split slot
# chrome under gui\sprites\container.
#
# Relevance is the one judgement call in the file, and it is recorded rather
# than acted on: `core` is art for a screen this game has or wants, `reference`
# is worth reading but not on the route, `unused` is Realms and multiplayer,
# which is permanently cut. Nothing is filtered out on the strength of it - a
# reader can, but the inventory stays complete.
#
# Each is the fallback purpose, `{0}` being the file name with underscores
# turned into spaces. $curated below overrides it wherever a sprite deserves a
# real sentence.
# ---------------------------------------------------------------------------
$groups = @(
    @{ Source = '';                                    Id = 'background';               Title = 'Screen backdrops and separators'; Relevance = 'core';      Purpose = 'The tiled backdrops and edge separators every menu screen is built on top of.'; Each = 'Menu backdrop or separator: {0}.' },
    @{ Source = 'container';                           Id = 'screen';                   Title = 'Container screen backgrounds';    Relevance = 'core';      Purpose = 'The pre-split full-screen container sheets. Each is a 256x256 (or 512x256) page whose screen occupies the top-left corner only - `opaque` on every entry is that rectangle, measured, and it is the number to lay a screen out against.'; Each = 'Container screen background: {0}.' },
    @{ Source = 'container/creative_inventory';        Id = 'screen/creative_inventory'; Title = 'Creative inventory pages';       Relevance = 'reference'; Purpose = 'Full-page backgrounds for the three creative-mode tab layouts.'; Each = 'Creative inventory page background: {0}.' },
    @{ Source = 'advancements';                        Id = 'screen/advancements';      Title = 'Advancements window';             Relevance = 'reference'; Purpose = 'The advancement screen frame.'; Each = 'Advancements screen art: {0}.' },
    @{ Source = 'advancements/backgrounds';            Id = 'advancement_background';   Title = 'Advancement tab backdrops';       Relevance = 'reference'; Purpose = 'The tiling stone/nether/end textures behind each advancement tab.'; Each = 'Tiling advancement backdrop: {0}.' },
    @{ Source = 'hanging_signs';                       Id = 'hanging_sign';             Title = 'Hanging sign edit screens';       Relevance = 'reference'; Purpose = 'Per-wood backgrounds for the hanging-sign text editor.'; Each = 'Hanging sign edit background: {0}.' },
    @{ Source = 'signs';                               Id = 'sign';                     Title = 'Sign edit screens';               Relevance = 'reference'; Purpose = 'Per-wood backgrounds for the standing/wall sign text editor.'; Each = 'Sign edit background: {0}.' },
    @{ Source = 'presets';                             Id = 'preset';                   Title = 'World preset art';                Relevance = 'reference'; Purpose = 'Artwork for the world-type preset picker.'; Each = 'World preset illustration: {0}.' },
    @{ Source = 'realms';                              Id = 'realms';                   Title = 'Realms promotional art';          Relevance = 'unused';    Purpose = 'Subscription-service artwork. No use here; multiplayer is permanently cut.'; Each = 'Realms artwork: {0}.' },
    @{ Source = 'title';                               Id = 'title';                    Title = 'Title screen logos';              Relevance = 'core';      Purpose = 'Logo lettering for the title screen. Reference only in the strictest sense - the lettering is expressive work and ours must be original - but the SIZES are what a title screen is laid out against.'; Each = 'Title screen logo art: {0}.' },
    @{ Source = 'title/background';                    Id = 'panorama';                 Title = 'Title screen panorama';           Relevance = 'core';      Purpose = 'The six faces of the title-screen skybox cube plus its darkening overlay. The six-face-plus-overlay structure is the reusable fact; the imagery is not.'; Each = 'Title panorama face: {0}.' },

    @{ Source = 'sprites/advancements';                Id = 'advancements';             Title = 'Advancement widgets';             Relevance = 'reference'; Purpose = 'Frames, boxes and the twelve tab shapes of the advancement screen.'; Each = 'Advancement screen widget: {0}.' },
    @{ Source = 'sprites/boss_bar';                    Id = 'boss_bar';                 Title = 'Boss health bars';                Relevance = 'core';      Purpose = 'Six colours and four notch patterns, each as a background and a progress overlay drawn on top of it. All are 182x5, which is the hotbar width.'; Each = 'Boss bar layer: {0}.' },
    @{ Source = 'sprites/container';                   Id = 'container';                Title = 'Shared slot chrome';              Relevance = 'core';      Purpose = 'The slot art every container screen shares - the 18x18 well and the two halves of the hover highlight.'; Each = 'Shared container chrome: {0}.' },
    @{ Source = 'sprites/container/anvil';             Id = 'container/anvil';          Title = 'Anvil screen';                    Relevance = 'core';      Purpose = 'Rename field and the too-expensive error glyph.'; Each = 'Anvil screen sprite: {0}.' },
    @{ Source = 'sprites/container/beacon';            Id = 'container/beacon';         Title = 'Beacon screen';                   Relevance = 'reference'; Purpose = 'The beacon power buttons and its confirm/cancel pair.'; Each = 'Beacon screen sprite: {0}.' },
    @{ Source = 'sprites/container/blast_furnace';     Id = 'container/blast_furnace';  Title = 'Blast furnace screen';            Relevance = 'core';      Purpose = 'Progress arrow and fuel flame. The same 24x16 and 14x14 as the furnace and the smoker - the three cooking screens differ in art, not in geometry, so one drawing routine serves all three.'; Each = 'Blast furnace sprite: {0}.' },
    @{ Source = 'sprites/container/brewing_stand';     Id = 'container/brewing_stand';  Title = 'Brewing stand screen';            Relevance = 'core';      Purpose = 'The brew arrow, the rising bubbles and the blaze-powder fuel gauge.'; Each = 'Brewing stand sprite: {0}.' },
    @{ Source = 'sprites/container/bundle';            Id = 'container/bundle';         Title = 'Bundle tooltip';                  Relevance = 'reference'; Purpose = 'The bundle fullness bar and the slot art of its spill-out grid.'; Each = 'Bundle sprite: {0}.' },
    @{ Source = 'sprites/container/cartography_table'; Id = 'container/cartography_table'; Title = 'Cartography table';            Relevance = 'reference'; Purpose = 'Map preview frames and the mode glyphs beside them.'; Each = 'Cartography table sprite: {0}.' },
    @{ Source = 'sprites/container/crafter';           Id = 'container/crafter';        Title = 'Crafter screen';                  Relevance = 'reference'; Purpose = 'Disabled-slot cross and the powered/unpowered redstone lamp.'; Each = 'Crafter sprite: {0}.' },
    @{ Source = 'sprites/container/creative_inventory'; Id = 'container/creative_inventory'; Title = 'Creative inventory tabs';    Relevance = 'reference'; Purpose = 'Seven tab shapes on each of the top and bottom rows, selected and unselected, plus the item-list scroller. All twenty-eight tabs are 26x32; the 1..7 suffix is POSITION along the row, not category, which is why there are seven of a thing the screen shows more than seven of.'; Each = 'Creative inventory sprite: {0}.' },
    @{ Source = 'sprites/container/enchanting_table';  Id = 'container/enchanting_table'; Title = 'Enchanting table';              Relevance = 'core';      Purpose = 'The three enchantment offer rows in three states, and the level pip for each.'; Each = 'Enchanting table sprite: {0}.' },
    @{ Source = 'sprites/container/furnace';           Id = 'container/furnace';        Title = 'Furnace screen';                  Relevance = 'core';      Purpose = 'The smelt progress arrow and the burning fuel flame. Both are drawn cropped to a fraction of their own size, so their full dimensions are the 100% case.'; Each = 'Furnace sprite: {0}.' },
    @{ Source = 'sprites/container/grindstone';        Id = 'container/grindstone';     Title = 'Grindstone screen';               Relevance = 'reference'; Purpose = 'The invalid-combination cross.'; Each = 'Grindstone sprite: {0}.' },
    @{ Source = 'sprites/container/horse';             Id = 'container/horse';          Title = 'Horse inventory';                 Relevance = 'reference'; Purpose = 'The donkey/llama chest slot block, drawn only when the animal carries chests.'; Each = 'Horse inventory sprite: {0}.' },
    @{ Source = 'sprites/container/inventory';         Id = 'container/inventory';      Title = 'Inventory effect panel';          Relevance = 'core';      Purpose = 'The panel a status effect is listed in beside the inventory, ambient and non-ambient. Both are nine-sliced, so the panel grows with the text.'; Each = 'Inventory panel sprite: {0}.' },
    @{ Source = 'sprites/container/loom';              Id = 'container/loom';           Title = 'Loom screen';                     Relevance = 'reference'; Purpose = 'Banner pattern buttons in three states, plus the pattern-list scroller.'; Each = 'Loom sprite: {0}.' },
    @{ Source = 'sprites/container/slot';              Id = 'container/slot';           Title = 'Empty slot glyphs';               Relevance = 'core';      Purpose = 'The faint ghost picture drawn in an empty slot to say what belongs there - a helmet outline in the helmet slot, a sword in the smithing input. Every one is 16x16, drawn centred in an 18x18 well.'; Each = 'Empty slot ghost glyph: {0}.' },
    @{ Source = 'sprites/container/smithing';          Id = 'container/smithing';       Title = 'Smithing table';                  Relevance = 'reference'; Purpose = 'The invalid-combination cross.'; Each = 'Smithing table sprite: {0}.' },
    @{ Source = 'sprites/container/smoker';            Id = 'container/smoker';         Title = 'Smoker screen';                   Relevance = 'core';      Purpose = 'Progress arrow and fuel flame, 24x16 and 14x14 - the furnace geometry again.'; Each = 'Smoker sprite: {0}.' },
    @{ Source = 'sprites/container/stonecutter';       Id = 'container/stonecutter';    Title = 'Stonecutter screen';              Relevance = 'reference'; Purpose = 'Recipe buttons in three states, plus the recipe-list scroller.'; Each = 'Stonecutter sprite: {0}.' },
    @{ Source = 'sprites/container/villager';          Id = 'container/villager';       Title = 'Trading screen';                  Relevance = 'reference'; Purpose = 'Trade arrows, the out-of-stock cross, the discount strikethrough and the villager level bar.'; Each = 'Trading screen sprite: {0}.' },
    @{ Source = 'sprites/dialog';                      Id = 'dialog';                   Title = 'Warning dialog buttons';          Relevance = 'reference'; Purpose = 'A red-tinted variant of the standard button, for destructive confirmations.'; Each = 'Warning dialog button: {0}.' },
    @{ Source = 'sprites/friends';                     Id = 'friends';                  Title = 'Friends list';                    Relevance = 'unused';    Purpose = 'Social screen chrome. No use here; multiplayer is permanently cut. Kept in the inventory because its button and panel art is a second, independently authored example of the same nine-slice shapes.'; Each = 'Friends screen sprite: {0}.' },
    @{ Source = 'sprites/gamemode_switcher';           Id = 'gamemode_switcher';        Title = 'Gamemode switcher';               Relevance = 'reference'; Purpose = 'The F3+F4 mode picker: a slot and its selection ring.'; Each = 'Gamemode switcher sprite: {0}.' },
    @{ Source = 'sprites/hud';                         Id = 'hud';                      Title = 'Heads-up display';                Relevance = 'core';      Purpose = 'Everything drawn over the world: hotbar, crosshair, armour, food, air, experience and the attack-cooldown indicator.'; Each = 'HUD element: {0}.' },
    @{ Source = 'sprites/hud/heart';                   Id = 'hud/heart';                Title = 'Health hearts';                   Relevance = 'core';      Purpose = 'Every health heart the HUD can draw. The naming is a product of four independent axes - condition (plain, poisoned, withered, frozen, absorbing), hardcore or not, full or half, blinking or not - and every combination that exists is a separate 9x9 file rather than a tint.'; Each = 'Health heart: {0}.' },
    @{ Source = 'sprites/hud/locator_bar_dot';         Id = 'hud/locator_bar_dot';      Title = 'Locator bar markers';             Relevance = 'reference'; Purpose = 'Player markers on the locator bar.'; Each = 'Locator bar marker: {0}.' },
    @{ Source = 'sprites/icon';                        Id = 'icon';                     Title = 'Small screen icons';              Relevance = 'reference'; Purpose = 'The small glyphs menus reuse: search, checkmark, info, language, accessibility, link.'; Each = 'Screen icon: {0}.' },
    @{ Source = 'sprites/notification';                Id = 'notification';             Title = 'Notification badges';             Relevance = 'unused';    Purpose = 'Unread-count badges for the Realms notification bell.'; Each = 'Notification badge: {0}.' },
    @{ Source = 'sprites/pause_menu';                  Id = 'pause_menu';               Title = 'Pause menu icons';                Relevance = 'reference'; Purpose = 'Button glyphs on the pause screen.'; Each = 'Pause menu icon: {0}.' },
    @{ Source = 'sprites/pending_invite';              Id = 'pending_invite';           Title = 'Pending invites';                 Relevance = 'unused';    Purpose = 'Realms invite screen. No use here.'; Each = 'Pending invite sprite: {0}.' },
    @{ Source = 'sprites/player_list';                 Id = 'player_list';              Title = 'Player list';                     Relevance = 'unused';    Purpose = 'The multiplayer tab-list overlay. No use here.'; Each = 'Player list sprite: {0}.' },
    @{ Source = 'sprites/popup';                       Id = 'popup';                    Title = 'Popup panel';                     Relevance = 'core';      Purpose = 'A nine-sliced panel for a modal box over a screen.'; Each = 'Popup panel: {0}.' },
    @{ Source = 'sprites/realm_status';                Id = 'realm_status';             Title = 'Realm status';                    Relevance = 'unused';    Purpose = 'Subscription state badges. No use here.'; Each = 'Realm status badge: {0}.' },
    @{ Source = 'sprites/recipe_book';                 Id = 'recipe_book';              Title = 'Recipe book';                     Relevance = 'core';      Purpose = 'The recipe book panel: its open/close button, the craftable-only filter, the four recipe slot states and the overlay that fans out a recipe.'; Each = 'Recipe book sprite: {0}.' },
    @{ Source = 'sprites/server_list';                 Id = 'server_list';              Title = 'Server list';                     Relevance = 'unused';    Purpose = 'Multiplayer server browser. No use here.'; Each = 'Server list sprite: {0}.' },
    @{ Source = 'sprites/social_interactions';         Id = 'social_interactions';      Title = 'Social interactions';             Relevance = 'unused';    Purpose = 'Player muting screen. No use here.'; Each = 'Social interactions sprite: {0}.' },
    @{ Source = 'sprites/spectator';                   Id = 'spectator';                Title = 'Spectator menu';                  Relevance = 'reference'; Purpose = 'The spectator-mode radial menu icons.'; Each = 'Spectator menu icon: {0}.' },
    @{ Source = 'sprites/statistics';                  Id = 'statistics';               Title = 'Statistics screen';               Relevance = 'reference'; Purpose = 'Column headers and sort arrows for the statistics tables.'; Each = 'Statistics screen icon: {0}.' },
    @{ Source = 'sprites/toast';                       Id = 'toast';                    Title = 'Toasts';                          Relevance = 'core';      Purpose = 'The corner pop-ups that announce an advancement, a new recipe or a tutorial hint, plus the glyphs drawn inside them.'; Each = 'Toast sprite: {0}.' },
    @{ Source = 'sprites/tooltip';                     Id = 'tooltip';                  Title = 'Tooltips';                        Relevance = 'core';      Purpose = 'The two layers of an item tooltip: a flat fill and the gradient frame over it. Both are nine-sliced from a 100x100 sheet, which is why a tooltip can be any size at all.'; Each = 'Tooltip layer: {0}.' },
    @{ Source = 'sprites/transferable_list';           Id = 'transferable_list';        Title = 'Transferable list';               Relevance = 'unused';    Purpose = 'Realms world transfer screen. No use here.'; Each = 'Transferable list sprite: {0}.' },
    @{ Source = 'sprites/widget';                      Id = 'widget';                   Title = 'Shared widgets';                  Relevance = 'core';      Purpose = 'The controls every screen is built from: button, checkbox, slider, text field, tab, scroller and the page arrows. This is the single most reusable group in the file and almost all of it is nine-sliced.'; Each = 'Shared widget: {0}.' },
    @{ Source = 'sprites/world_list';                  Id = 'world_list';               Title = 'World list';                      Relevance = 'core';      Purpose = 'Row icons for the single-player world picker: join, warning, error.'; Each = 'World list icon: {0}.' }
)

# ---------------------------------------------------------------------------
# Purposes worth a real sentence. Everything not named here gets its group's
# Each template, which is honest but generic. Prefer adding a line here over
# writing prose in a document nothing checks.
# ---------------------------------------------------------------------------
$curated = @{
    'widget/button'                    = 'The standard button. Nine-sliced with a 3px border from a 200x20 sheet, so any width and any height at all work from the one file.'
    'widget/button_highlighted'        = 'The standard button under the pointer. Same geometry as widget/button.'
    'widget/button_disabled'           = 'The standard button when it cannot be pressed. Border drops to 1px, because the disabled art has no inner bevel to preserve.'
    'widget/checkbox'                  = 'An unticked checkbox, 20x20. No .mcmeta, so it is a stretch sprite: draw it at its own size or it distorts.'
    'widget/checkbox_selected'         = 'A ticked checkbox, same 20x20.'
    'widget/cross_button'              = 'The close X used in the top corner of a panel.'
    'widget/slider'                    = 'The slider trough. Nine-sliced at 1px so the trough can be any width.'
    'widget/slider_handle'             = 'The slider knob. Its border is asymmetric - 2px on three sides, 3px at the bottom - because the knob art has a taller shadow under it than highlight above.'
    'widget/scroller'                  = 'The scrollbar thumb, 6x32, nine-sliced at 1px so it shortens as the list grows.'
    'widget/scroller_background'       = 'The scrollbar trough behind the thumb, same 6x32 geometry.'
    'widget/tab'                       = 'A screen tab. Its bottom border is 0, which is what makes a tab join the panel below it instead of closing against it - the single most instructive nine-slice in the whole set.'
    'widget/tab_selected'              = 'The active screen tab. Same 130x24 geometry and same open bottom edge.'
    'widget/text_field'                = 'A text input box, nine-sliced at 1px.'
    'widget/text_field_highlighted'    = 'A focused text input box.'
    'widget/preedit'                   = 'The IME composition strip drawn under a text field mid-input.'
    'widget/slot_frame'                = 'A standalone slot frame for screens that are not containers.'
    'widget/page_forward'              = 'The book page-turn arrow, right.'
    'widget/page_backward'             = 'The book page-turn arrow, left.'
    'widget/locked_button'             = 'The padlock toggle used for difficulty locking.'
    'tooltip/background'               = 'The flat fill behind tooltip text. Nine-sliced at 9px from a 100x100 sheet.'
    'tooltip/frame'                    = 'The gradient border drawn over the tooltip fill. Nine-sliced at 10px, and the only sprite in the set with stretch_inner set - its middle is stretched rather than tiled, which is why a tall tooltip keeps a clean gradient.'
    'container/slot'                   = 'The 18x18 inventory slot well, with no margin of its own - so slots tile at an 18px pitch and a 9x3 grid is exactly 162x54. This one number sets the geometry of every container screen, and our own Bedrock crop measures 18x18 too.'
    'container/slot_highlight_front'   = 'The white hover wash drawn over the item in a slot. 24x24 nine-sliced at 4px, so it overhangs the 18x18 well by 3px on every side.'
    'container/slot_highlight_back'    = 'The hover wash drawn under the item, same 24x24 geometry.'
    'container/inventory/effect_background' = 'The panel behind one status effect in the inventory sidebar. 32x32 nine-sliced at 4px, so it stretches to the longest effect name.'
    'hud/hotbar'                       = 'The hotbar frame. 182x22 for nine slots means a 20px pitch with a 1px frame all round - the selection ring is 24x23 and deliberately overhangs it.'
    'hud/hotbar_selection'             = 'The ring around the selected hotbar slot. Larger than the slot pitch on purpose, so it reads as raised.'
    'hud/crosshair'                    = 'The centre crosshair, drawn with an inverting blend so it is legible against any background.'
    'hud/experience_bar_background'    = 'The experience bar trough, 182x5 - the same width as the hotbar, which is what aligns them.'
    'hud/experience_bar_progress'      = 'The filled part of the experience bar, cropped from the left to the fraction earned.'
    'hud/armor_full'                   = 'One full armour point icon, 9x9. Ten of them is a full bar.'
    'hud/food_full'                    = 'One full hunger point, 9x9. The hunger bar fills from the right, which is why the shank faces the opposite way from the heart.'
    'hud/air'                          = 'One air bubble, 9x9, shown only while submerged.'
    'hud/heart/container'              = 'The empty socket every health heart is drawn into. 9x9, like every other sprite in this group.'
    'hud/heart/full'                   = 'A full heart, 9x9.'
    'hud/heart/half'                   = 'A half heart. The half is baked into the art, not clipped at draw time - which is why there are 47 files here and not a dozen.'
    'hud/heart/full_blinking'          = 'The brighter heart flashed on the tick damage is taken. Every condition has its own blinking pair.'
    'hud/locator_bar_background'       = 'The locator bar trough, 12x5. The only nine-slice in the reference gui tree whose left/right border (5) exceeds its top/bottom (1) - the sprite is almost all border and stretches through a 2x3 centre.'
    'recipe_book/button'               = 'The book-open toggle at the left of a crafting screen.'
    'recipe_book/filter_enabled'       = 'The craftable-only filter in its on state.'
    'recipe_book/slot_craftable'       = 'A recipe slot the player can afford right now.'
    'recipe_book/slot_uncraftable'     = 'A recipe slot the player cannot afford - the red slot our own Bedrock crop calls slot-red-uncraftable.'
    'recipe_book/overlay_recipe'       = 'The panel that fans out when a recipe is clicked. 32x32 nine-sliced at 4px, so it sizes to the ingredient grid.'
    'recipe_book/tab'                  = 'A recipe category tab down the side of the book.'
    'toast/system'                     = 'The system toast frame, and the most asymmetric nine-slice here: 17px left and 30px top against 4px right and bottom, because the icon well and the title bar are baked into the corners.'
    'toast/advancement'                = 'The advancement toast frame.'
    'toast/tutorial'                   = 'The tutorial hint toast frame.'
    'toast/now_playing'                = 'The music track toast frame.'
    'boss_bar/purple_background'       = 'The default boss bar trough. 182x5, the hotbar width again.'
    'boss_bar/purple_progress'         = 'The filled part of the default boss bar, cropped from the left.'
    'popup/background'                 = 'A modal panel. 236x34 nine-sliced at 6px.'
    'screen/inventory'                 = 'The survival inventory screen. Its drawn rect is 176x166, which is the size almost every container screen in the game shares.'
    'screen/crafting_table'            = 'The crafting table screen, 176x166 - the 3x3 grid, the result slot and the full player inventory below it.'
    'screen/generic_54'                = 'The double chest, 176x222 - the tallest of the standard containers, and the one that sets how much vertical room a screen may need.'
    'screen/hopper'                    = 'The hopper, 176x133 - the shortest screen here that still shows the full player inventory.'
    'screen/beacon'                    = 'The beacon, 230x219 - the widest of the non-trading screens.'
    'screen/villager'                  = 'The trading screen, 276x166, and the only sheet in the set that needs a 512-wide page to hold it.'
    'background/menu_background'       = 'The tiling backdrop behind a menu when no world is loaded.'
    'background/menu_list_background'  = 'The darker tiling panel behind a scrolling list.'
    'background/header_separator'      = 'The horizontal rule under a screen header, 32x2.'
    'background/book'                  = 'The written-book page background.'
    'background/recipe_book'           = 'The pre-split recipe book panel sheet, kept alongside the split sprites.'
}

# ---------------------------------------------------------------------------
# Measurement.
# ---------------------------------------------------------------------------

# The bounding box of every pixel with a non-zero alpha. This is what turns a
# 256x256 page into the 176x166 rectangle a screen is actually laid out in,
# without anyone having to remember the number.
function Measure-Sprite {
    param([string]$Path)

    $bitmap = New-Object System.Drawing.Bitmap $Path
    try {
        $width = $bitmap.Width
        $height = $bitmap.Height
        $rect = New-Object System.Drawing.Rectangle 0, 0, $width, $height
        $data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $stride = $data.Stride
            $bytes = New-Object byte[] ($stride * $height)
            [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        } finally {
            $bitmap.UnlockBits($data)
        }
    } finally {
        $bitmap.Dispose()
    }

    $minX = $width; $minY = $height; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $height; $y++) {
        $row = $y * $stride
        for ($x = 0; $x -lt $width; $x++) {
            if ($bytes[$row + $x * 4 + 3] -ne 0) {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }

    $opaque = $null
    if ($maxX -ge 0 -and -not ($minX -eq 0 -and $minY -eq 0 -and $maxX -eq $width - 1 -and $maxY -eq $height - 1)) {
        $opaque = [ordered]@{ x = $minX; y = $minY; width = ($maxX - $minX + 1); height = ($maxY - $minY + 1) }
    }

    return @{ Width = $width; Height = $height; Opaque = $opaque; Empty = ($maxX -lt 0) }
}

# vanilla writes a nine-slice border either as one number for all four sides or
# as an object naming each. Both are normalised to four sides here, so a reader
# never meets the scalar form.
function Read-Border {
    param($Border)

    if ($null -eq $Border) { return $null }
    if ($Border -is [System.Management.Automation.PSCustomObject]) {
        return [ordered]@{
            left   = [int]$Border.left
            top    = [int]$Border.top
            right  = [int]$Border.right
            bottom = [int]$Border.bottom
        }
    }
    $n = [int]$Border
    return [ordered]@{ left = $n; top = $n; right = $n; bottom = $n }
}

# ---------------------------------------------------------------------------
# JSON assembly.
#
# Hand-assembled rather than one big ConvertTo-Json, for one reason: a sprite
# lands on ONE line, so `findstr "widget/button"` on this file answers the
# question without a JSON parser and a diff shows exactly which sprites moved.
# The per-sprite objects still go through ConvertTo-Json, so escaping is never
# hand-rolled.
# ---------------------------------------------------------------------------

# PowerShell 5.1 escapes ' < > & as \uXXXX. All four are legal raw in JSON and
# this file is read by people as often as by code, so put them back. The
# lookbehind keeps a genuinely escaped backslash out of it.
function Restore-JsonText {
    param([string]$Text)

    $text = [regex]::Replace($Text, '(?<!\\)\\u0027', "'")
    $text = [regex]::Replace($text, '(?<!\\)\\u003c', '<')
    $text = [regex]::Replace($text, '(?<!\\)\\u003e', '>')
    $text = [regex]::Replace($text, '(?<!\\)\\u0026', '&')
    return $text
}

function Format-JsonValue {
    param($Value)
    return Restore-JsonText ($Value | ConvertTo-Json -Depth 12 -Compress)
}

# ---------------------------------------------------------------------------
# Walk the tree.
# ---------------------------------------------------------------------------

$byId = @{}
foreach ($group in $groups) { $byId[$group.Source] = $group }

# The anti-hole guard. Every folder that HOLDS PNGs must be described; a folder
# that only contains other folders (gui\sprites itself) is structure, not a
# group, and is exempt. Anything else throws, so a newer reference drop cannot
# quietly add sprites nobody records.
$described = New-Object System.Collections.Generic.HashSet[string]
foreach ($group in $groups) { [void]$described.Add($group.Source) }

$actual = New-Object System.Collections.Generic.List[string]
foreach ($dir in @(Get-Item $guiRoot) + @(Get-ChildItem -Path $guiRoot -Directory -Recurse)) {
    if (@(Get-ChildItem -Path $dir.FullName -Filter *.png -File).Count -eq 0) { continue }
    $rel = $dir.FullName.Substring($guiRoot.Length).TrimStart('\') -replace '\\', '/'
    $actual.Add($rel)
}
$missing = @($actual | Where-Object { -not $described.Contains($_) })
if ($missing.Count -gt 0) {
    throw ("Undescribed folder(s) in the reference gui tree: " + ($missing -join ', ') + ". Add each to `$groups in tools\extract-ui-spec.ps1 - a folder that is not described would be silently dropped from the atlas.")
}

$groupBlocks = New-Object System.Collections.Generic.List[string]
$totalSprites = 0
$totalNineSlice = 0
$totalAnimated = 0
$totalStretch = 0
$counts = New-Object System.Collections.Generic.List[object]

foreach ($group in $groups) {
    $folder = if ($group.Source -eq '') { $guiRoot } else { Join-Path $guiRoot ($group.Source -replace '/', '\') }
    $groupSource = if ($group.Source -eq '') { $guiRelative } else { "$guiRelative/$($group.Source)" }
    if (-not (Test-Path $folder)) { throw "Described folder is missing from the reference: $folder" }

    $files = @(Get-ChildItem -Path $folder -Filter *.png -File | Sort-Object Name)
    if ($files.Count -eq 0) { throw "Described folder holds no PNGs: $folder" }

    $spriteLines = New-Object System.Collections.Generic.List[string]
    $groupNineSlice = 0

    foreach ($file in $files) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
        $id = if ($group.Id -eq '') { $name } else { "$($group.Id)/$name" }
        $relative = if ($group.Source -eq '') { "$guiRelative/$($file.Name)" } else { "$guiRelative/$($group.Source)/$($file.Name)" }

        $measured = Measure-Sprite $file.FullName

        $scaling = 'stretch'
        $border = $null
        $stretchInner = $false
        $animation = $null
        $declared = $null
        $metaRelative = $null

        $metaPath = "$($file.FullName).mcmeta"
        if (Test-Path $metaPath) {
            $metaRelative = "$relative.mcmeta"
            $meta = Get-Content -Raw -Encoding UTF8 $metaPath | ConvertFrom-Json

            if ($null -ne $meta.gui -and $null -ne $meta.gui.scaling) {
                $scaling = [string]$meta.gui.scaling.type
                if ($scaling -eq 'nine_slice') {
                    $border = Read-Border $meta.gui.scaling.border
                    if ($null -ne $meta.gui.scaling.stretch_inner) { $stretchInner = [bool]$meta.gui.scaling.stretch_inner }
                }
                # The declared width/height is the size the border insets were
                # measured against. It should equal the PNG, and where it does
                # not, the difference is the fact worth keeping - so it is
                # written out rather than trusted silently either way.
                if ($null -ne $meta.gui.scaling.width -and $null -ne $meta.gui.scaling.height) {
                    $dw = [int]$meta.gui.scaling.width
                    $dh = [int]$meta.gui.scaling.height
                    if ($dw -ne $measured.Width -or $dh -ne $measured.Height) {
                        $declared = [ordered]@{ width = $dw; height = $dh }
                    }
                }
            }

            if ($null -ne $meta.animation) {
                # A frame is square unless the sidecar says otherwise, and the
                # strip runs down the image - so the frame height is what tells
                # a reader the sprite is 7x5 and not 7x10.
                $frameWidth = if ($null -ne $meta.animation.width) { [int]$meta.animation.width } else { $measured.Width }
                $frameHeight = if ($null -ne $meta.animation.height) { [int]$meta.animation.height } else { $frameWidth }
                $frameCount = if ($frameHeight -gt 0) { [int][math]::Floor($measured.Height / $frameHeight) } else { 1 }
                $animation = [ordered]@{ frameWidth = $frameWidth; frameHeight = $frameHeight; frameCount = $frameCount }
                $totalAnimated++
            }
        }

        if ($scaling -eq 'nine_slice') { $groupNineSlice++; $totalNineSlice++ } else { $totalStretch++ }

        $purpose = if ($curated.ContainsKey($id)) { $curated[$id] } else { $group.Each -f ($name -replace '_', ' ') }

        $sprite = [ordered]@{
            id     = $id
            path   = $relative
            width  = $measured.Width
            height = $measured.Height
            scaling = $scaling
        }
        if ($null -ne $border) {
            $sprite.border = $border
            # The centre is what actually stretches. Derived here so nobody
            # subtracts it by hand at the call site and gets it wrong once.
            $sprite.center = [ordered]@{
                width  = $measured.Width - $border.left - $border.right
                height = $measured.Height - $border.top - $border.bottom
            }
        }
        if ($stretchInner) { $sprite.stretchInner = $true }
        if ($null -ne $declared) { $sprite.declared = $declared }
        if ($null -ne $animation) { $sprite.animation = $animation }
        if ($null -ne $measured.Opaque) { $sprite.opaque = $measured.Opaque }
        if ($measured.Empty) { $sprite.fullyTransparent = $true }
        if ($null -ne $metaRelative) { $sprite.mcmeta = $metaRelative }
        $sprite.purpose = $purpose

        $spriteLines.Add('        ' + (Format-JsonValue ([PSCustomObject]$sprite)))
        $totalSprites++
    }

    $counts.Add([PSCustomObject]@{ Group = $group.Id; Count = $files.Count; NineSlice = $groupNineSlice; Relevance = $group.Relevance })

    $block = New-Object System.Collections.Generic.List[string]
    $block.Add('    {')
    $block.Add('      "id": ' + (Format-JsonValue $group.Id) + ',')
    $block.Add('      "title": ' + (Format-JsonValue $group.Title) + ',')
    $block.Add('      "source": ' + (Format-JsonValue $groupSource) + ',')
    $block.Add('      "relevance": ' + (Format-JsonValue $group.Relevance) + ',')
    $block.Add('      "count": ' + $files.Count + ',')
    $block.Add('      "nineSliceCount": ' + $groupNineSlice + ',')
    $block.Add('      "purpose": ' + (Format-JsonValue $group.Purpose) + ',')
    $block.Add('      "sprites": [')
    for ($i = 0; $i -lt $spriteLines.Count; $i++) {
        $comma = if ($i -lt $spriteLines.Count - 1) { ',' } else { '' }
        $block.Add($spriteLines[$i] + $comma)
    }
    $block.Add('      ]')
    $block.Add('    }')
    $groupBlocks.Add(($block -join "`n"))
}

# ---------------------------------------------------------------------------
# The Bedrock crops we extracted ourselves. Same schema, different provenance -
# and the provenance matters, because these came out of a lossy AVIF console
# screenshot at 3x and then got divided down, so their sizes are the grid we
# recovered rather than a clean rip. Recorded, not hidden.
# ---------------------------------------------------------------------------
$bedrockPurposes = @{
    'card-left-full'       = 'The whole left card of the Bedrock crafting screen - the recipe book, its tab strip and its search field, in one crop.'
    'card-right-full'      = 'The whole right card - the crafting grid, the result slot and the player inventory.'
    'slot-empty'           = 'An empty Bedrock slot, and it measures 18x18 - the same pitch as the Java container/slot, which is what lets the two layouts be compared directly.'
    'slot-output-red'      = 'The output slot when the recipe cannot be made.'
    'slot-red-uncraftable' = 'A catalogue slot for a recipe the player cannot afford.'
    'panel-corner'         = 'The top-left corner of a Bedrock card, the piece a nine-slice of our own panel has to match.'
    'scrollbar'            = 'The Bedrock scrollbar, trough and thumb together.'
    'toolbar-strip'        = 'The top-right toolbar: layout toggle, help and close, with the shoulder-button glyphs either side.'
    'armour-slot-glyphs'   = 'The four armour slot ghost glyphs in one column, at the pitch they are drawn at.'
    'toggle-craftable'     = 'The craftable-only filter. The crop is 28x19 because it was cut with a margin; the control inside it is the 26x16 that recipe_book/filter_enabled measures exactly.'
    'layout-book-inventory' = 'The layout switch in its book-and-inventory position.'
    'layout-inventory-only' = 'The layout switch in its inventory-only position.'
    'button-close'         = 'The close button from the top-right toolbar.'
    'button-help'          = 'The help button from the top-right toolbar.'
    'bumper-zl'            = 'The ZL shoulder-button glyph shown left of the toolbar on console.'
    'bumper-zr'            = 'The ZR shoulder-button glyph shown right of the toolbar on console.'
    'tab-construction'     = 'The construction category tab, whole tab rather than just the glyph.'
    'tab-equipment'        = 'The equipment category tab.'
    'tab-items'            = 'The items category tab.'
    'tab-nature'           = 'The nature category tab.'
    'tab-search'           = 'The search tab, which sits apart from the other four.'
}

$bedrockFiles = @(Get-ChildItem -Path $bedrockRoot -Filter *.png -File | Where-Object { $_.Name -notlike '*@3x.png' } | Sort-Object Name)
if ($bedrockFiles.Count -eq 0) { throw "No 1x crops in $bedrockRoot - run tools\extract-ui-icons.ps1." }

$bedrockLines = New-Object System.Collections.Generic.List[string]
foreach ($file in $bedrockFiles) {
    $name = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $measured = Measure-Sprite $file.FullName

    $sprite = [ordered]@{
        id      = "bedrock/$name"
        path    = "$bedrockRelative/$($file.Name)"
        width   = $measured.Width
        height  = $measured.Height
        scaling = 'stretch'
    }
    $x3 = Join-Path $bedrockRoot "$name@3x.png"
    if (Test-Path $x3) {
        $big = Measure-Sprite $x3
        $sprite.magnified = [ordered]@{ path = "$bedrockRelative/$name@3x.png"; width = $big.Width; height = $big.Height; scale = 3 }
    }
    if ($null -ne $measured.Opaque) { $sprite.opaque = $measured.Opaque }
    $sprite.purpose = if ($bedrockPurposes.ContainsKey($name)) { $bedrockPurposes[$name] } else { "Bedrock crafting screen crop: $($name -replace '-', ' ')." }

    $bedrockLines.Add('        ' + (Format-JsonValue ([PSCustomObject]$sprite)))
    $totalSprites++
    $totalStretch++
}
$counts.Add([PSCustomObject]@{ Group = 'bedrock'; Count = $bedrockFiles.Count; NineSlice = 0; Relevance = 'core' })

$bedrockBlock = New-Object System.Collections.Generic.List[string]
$bedrockBlock.Add('    {')
$bedrockBlock.Add('      "id": "bedrock",')
$bedrockBlock.Add('      "title": "Bedrock crafting screen crops",')
$bedrockBlock.Add('      "source": ' + (Format-JsonValue $bedrockRelative) + ',')
$bedrockBlock.Add('      "relevance": "core",')
$bedrockBlock.Add('      "count": ' + $bedrockFiles.Count + ',')
$bedrockBlock.Add('      "nineSliceCount": 0,')
$bedrockBlock.Add('      "purpose": ' + (Format-JsonValue 'Regions this project cut out of a 1280x720 Bedrock crafting-screen capture with tools\extract-ui-icons.ps1. The capture is vanilla at exactly 3x, so every crop divides back to whole units - but the source is lossy AVIF, so treat these sizes as the recovered grid rather than a clean rip. Each has a 3x companion recorded under `magnified`; the 1x is the one to measure.') + ',')
$bedrockBlock.Add('      "sprites": [')
for ($i = 0; $i -lt $bedrockLines.Count; $i++) {
    $comma = if ($i -lt $bedrockLines.Count - 1) { ',' } else { '' }
    $bedrockBlock.Add($bedrockLines[$i] + $comma)
}
$bedrockBlock.Add('      ]')
$bedrockBlock.Add('    }')
$groupBlocks.Add(($bedrockBlock -join "`n"))

# ---------------------------------------------------------------------------
# The document.
# ---------------------------------------------------------------------------
$document = New-Object System.Collections.Generic.List[string]
$document.Add('{')
$document.Add('  "schema": "ui-atlas",')
$document.Add('  "schemaVersion": ' + $schemaVersion + ',')
$document.Add('  "generated": ' + (Format-JsonValue (Get-Date -Format 'yyyy-MM-dd')) + ',')
$document.Add('  "generator": "tools/extract-ui-spec.ps1",')
$document.Add('  "note": ' + (Format-JsonValue 'GENERATED FILE - do not hand-edit. Every number here is measured from the art in reference/ by tools/extract-ui-spec.ps1; edit that script and re-run it, or the file and the art will disagree and nothing will notice. `-Check` fails when this file is stale.') + ',')
$document.Add('  "reference": ' + (Format-JsonValue $referenceVersion) + ',')
$document.Add('  "referenceRoot": "reference",')
$document.Add('  "paths": ' + (Format-JsonValue 'A sprite''s repository-relative path is `referenceRoot` + "/" + the entry''s `path`, joined as plain strings - no special knowledge, no rewriting. reference/ is gitignored and never ships. This file holds metadata only, no pixels. assets/ui/ui-palette.json and assets/ui/ui-screens.json name the same trees through a `roots` map; the strings agree, so `roots.java_gui` + "/container/anvil.png" and `referenceRoot` + "/" + `path` resolve to the same file.') + ',')
$document.Add('  "spriteCount": ' + $totalSprites + ',')
$document.Add('  "groupCount": ' + $groupBlocks.Count + ',')
$document.Add('  "nineSliceCount": ' + $totalNineSlice + ',')
$document.Add('  "stretchCount": ' + $totalStretch + ',')
$document.Add('  "animatedCount": ' + $totalAnimated + ',')
$document.Add('  "scalingModes": {')
$document.Add('    "stretch": ' + (Format-JsonValue 'The whole sprite is scaled to the box. This is the default and is what a sprite with no .mcmeta gets; it is written out explicitly on every such entry rather than left implied.') + ',')
$document.Add('    "nine_slice": ' + (Format-JsonValue 'Corners are drawn at 1:1 and the four edges repeat along one axis. `border` names the inset on each side and `center` is the leftover middle, derived. The middle is TILED, not stretched, unless `stretchInner` is set - and `tooltip/frame` is the only sprite in this drop that sets it.') + ',')
$document.Add('    "tile": ' + (Format-JsonValue 'The sprite repeats at 1:1 to fill the box. No sprite in this reference drop declares it; recorded here so the third mode is not a surprise later.') + '')
$document.Add('  },')
$document.Add('  "fields": {')
$document.Add('    "opaque": ' + (Format-JsonValue 'Present only when the drawn art does not fill the file: the measured bounding box of every non-transparent pixel. This is how the classic 176x166 inventory rectangle inside a 256x256 page is known - read off the art, not remembered.') + ',')
$document.Add('    "declared": ' + (Format-JsonValue 'Present only when a .mcmeta declares a size that disagrees with the PNG. The declared size is what the border insets were measured against.') + ',')
$document.Add('    "animation": ' + (Format-JsonValue 'Present when the sprite is a vertical strip of frames rather than one picture. `frameHeight` is the real drawn height.') + ',')
$document.Add('    "relevance": ' + (Format-JsonValue 'A group-level judgement, recorded rather than acted on. core = a screen this game has or wants; reference = worth reading, not on the route; unused = Realms and multiplayer, which is permanently cut. Nothing is filtered out on the strength of it.') + '')
$document.Add('  },')
$document.Add('  "groups": [')
for ($i = 0; $i -lt $groupBlocks.Count; $i++) {
    $comma = if ($i -lt $groupBlocks.Count - 1) { ',' } else { '' }
    $document.Add($groupBlocks[$i] + $comma)
}
$document.Add('  ]')
$document.Add('}')

$text = ($document -join "`n") + "`n"

# Prove it parses before it is written or compared. A file this size is not
# something anyone will eyeball, so the check has to be mechanical.
try {
    $null = $text | ConvertFrom-Json
} catch {
    throw "Generated document is not valid JSON: $($_.Exception.Message)"
}

# ---------------------------------------------------------------------------
# Write, or check.
#
# The `generated` date changes every day, so comparing raw text would call the
# file stale every morning and rewrite it for nothing. Both sides are compared
# with that one field blanked - which is also why a run that changes nothing
# leaves the file, and its date, alone.
# ---------------------------------------------------------------------------
$outPath = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
$normalise = { param($t) [regex]::Replace($t, '"generated": "[^"]*"', '"generated": ""') }

$existing = $null
if (Test-Path $outPath) {
    $existing = [System.IO.File]::ReadAllText($outPath)
}
$same = ($null -ne $existing) -and ((& $normalise $existing) -eq (& $normalise $text))

foreach ($row in $counts) {
    Write-Host ('  {0,-34} {1,4} sprites  {2,3} nine-slice  {3}' -f $row.Group, $row.Count, $row.NineSlice, $row.Relevance)
}
Write-Host ''
Write-Host ("{0} sprites in {1} groups: {2} nine-slice, {3} stretch, {4} animated" -f $totalSprites, $groupBlocks.Count, $totalNineSlice, $totalStretch, $totalAnimated)

if ($Check) {
    if ($same) {
        Write-Host "$Out up to date"
        exit 0
    }
    if ($null -eq $existing) {
        Write-Error "$Out does not exist. Run tools\extract-ui-spec.ps1 to generate it."
    } else {
        Write-Error "$Out is STALE - it disagrees with the art in reference/. Run tools\extract-ui-spec.ps1 to regenerate it."
    }
    exit 1
}

if ($same) {
    Write-Host "$Out up to date"
    exit 0
}

$outDir = Split-Path -Parent $outPath
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Force -Path $outDir | Out-Null }

# UTF-8 with no BOM. See the header.
[System.IO.File]::WriteAllText($outPath, $text, [System.Text.UTF8Encoding]::new($false))
Write-Host ("$Out written, {0:n0} bytes" -f (Get-Item $outPath).Length)
