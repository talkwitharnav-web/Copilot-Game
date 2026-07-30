# SYSTEM_MEMORY.md

Current technical truth for this voxel sandbox project: what exists, where it lives, what version it is, and how to build and run it.

Narrative history, rejected approaches, and debugging lessons live in `CLAUDE.md`. **This file is factual and current-state only** — when something changes, replace the old fact in place rather than appending.

> **Status:** Milestone 1 (toolchain proof). No gameplay, no voxels, no world generation exists yet, by design.

---

## Critical Invariants

- **`engine/` never references `game/`.** The engine is a static library with no knowledge of the game. Violating this breaks the ability to reuse the engine for tools/servers later.
- **Build only through CMake Presets.** Never hand-invoke `cl.exe` or write build commands by hand; the presets are the single source of truth for compiler flags and output paths.
- **A plain shell cannot build this project — the MSVC environment must be loaded first.** `cl.exe` is not on the global `PATH` by design, and CMake's Ninja generator finds the compiler through `PATH`/`INCLUDE`/`LIB`. Verified by direct test: configuring from a scrubbed shell fails with `No CMAKE_CXX_COMPILER could be found`. Run `. .\tools\dev-env.ps1` first (leading dot required), or build through VS Code's CMake Tools, which loads that environment itself.
- **`CMakePresets.json` must keep its `architecture`/`toolset` fields with `"strategy": "external"`.** That is the signal VS Code's CMake Tools uses to set up the MSVC x64 environment before invoking CMake. Plain CMake ignores them for the Ninja generator, so they cost nothing on the command line. Removing them breaks the entire in-editor build.
- **Validation layers are enabled in Debug builds and disabled in Release.** They are a correctness tool with a real performance cost. A Debug build that reports zero validation errors is the acceptance bar for any rendering change.
- **Every Vulkan object must be destroyed in reverse creation order.** Vulkan does not reference-count. Destroying a device before the things allocated from it is undefined behavior and typically manifests as a driver crash on exit, not at the point of the mistake.
- **PowerShell 5.1 on this machine.** Use `;` to chain, never `&&`. Never send multi-line scripts to the integrated terminal (see `CLAUDE.md`).
- **The Windows user account is not an administrator.** Anything requiring elevation must be launched via `Start-Process -Verb RunAs` for the user to authorize at Windows' own prompt.

---

## Verified Environment

Confirmed working on this machine as of 2026-07-30.

| Component | What it is, in plain terms | Version / Location |
|---|---|---|
| OS | — | Windows 11 Home, build 10.0.26200 |
| CPU | — | Intel Core Ultra 7 155H — 16 cores, 22 threads |
| GPU (discrete) | The fast graphics card | NVIDIA GeForce RTX 4070 Laptop, driver 32.0.16.1047 |
| GPU (integrated) | The power-saving graphics chip built into the CPU | Intel Arc Graphics, driver 32.0.101.8247 |
| Git | Version control | `%LOCALAPPDATA%\Programs\Git` |
| CMake | Describes *how* to build the project; generates the actual build files | 4.4.1, `%LOCALAPPDATA%\Programs\CMake` (portable, user-scope) |
| Ninja | The program that actually runs the compiler, in parallel, as fast as possible | 1.13.2, `%LOCALAPPDATA%\Programs\Ninja` (portable, user-scope) |
| MSVC | Microsoft's C++ compiler — turns source code into an `.exe` | Build Tools 2022 v17.14.37516.0, toolset 14.44.35207, compiler `cl.exe` 19.44.35228 |
| Windows SDK | Header files that let a program talk to Windows itself | 10.0.26100.0 |
| Vulkan runtime | The driver-side piece that actually talks to the GPU | `C:\Windows\System32\vulkan-1.dll` (shipped by GPU drivers, predates SDK) |
| Vulkan SDK | Developer tooling: error-checking layers + shader compiler | LunarG 1.4.357.0, `C:\VulkanSDK\1.4.357.0` |
| VS Code | Editor | CMake Tools 1.23.52, C/C++ (cpptools) 1.32.2 |

`CMake` and `Ninja` were installed as extracted archives with `%LOCALAPPDATA%\Programs\CMake\bin` and `%LOCALAPPDATA%\Programs\Ninja` appended to the **user** `Path` variable. There is nothing to uninstall; upgrading means replacing the folder.

### Two GPUs

This laptop has both a discrete NVIDIA GPU and an integrated Intel GPU, and **Vulkan will happily pick either one.** Physical-device selection therefore prefers a discrete GPU explicitly rather than taking whatever Vulkan lists first. This is not theoretical: the Intel Arc iGPU enumerates as `GPU0` and the RTX 4070 as `GPU1`, so "take the first device" would silently select the slow one. The startup log prints the chosen GPU — check it before investigating any performance complaint.

### Third-party Vulkan layers are present on this machine

`vulkaninfo` reports `VK_LAYER_OBS_HOOK` (OBS) and `VK_LAYER_RTSS` (RivaTuner/MSI Afterburner) installed system-wide, both built against Vulkan 1.3 while this project requests 1.4. The loader warns about the mismatch on every launch. They are harmless so far, but if a bizarre rendering or present-mode bug ever appears that cannot be reproduced logically, disabling these overlays is a cheap early test.

---

## Repository Layout

```
/
├── CMakeLists.txt          Root build script: project name, C++ standard, dependencies
├── CMakePresets.json       Named build configurations (debug/release)
├── .gitignore              Keeps build output and downloaded deps out of Git
├── .gitattributes          Line-ending normalization (Windows/CRLF safety)
├── CLAUDE.md               Why decisions were made (narrative)
├── SYSTEM_MEMORY.md        What currently exists (this file)
├── .vscode/                Editor config + recommended extensions
├── tools/
│   └── dev-env.ps1         Loads the MSVC environment into the current shell
├── engine/                 The reusable engine — a static library, knows nothing about the game
│   ├── CMakeLists.txt
│   ├── include/engine/     Public headers: what the game is allowed to use
│   └── src/                Implementation: private to the engine
└── game/                   The game executable — uses the engine
    ├── CMakeLists.txt
    └── src/
```

**Why the `include/` vs `src/` split:** headers under `engine/include/engine/` are the engine's public surface — anything the game can call. Everything in `engine/src/` is internal. This makes it structurally obvious when the game starts depending on engine internals it should not.

---

## Build System

### Targets

| Target | Type | Purpose |
|---|---|---|
| `engine` | Static library | All reusable engine code. Produces `engine.lib`, which is linked into the game. |
| `game` | Executable | The actual runnable program. Currently just opens a window and clears it. |

A **static library** means the engine's compiled code is copied into the final `.exe` at link time — one self-contained executable, no separate `.dll` to ship. This is the right default until there is a reason (hot-reloading, plugins) to change it.

### Presets

Defined in `CMakePresets.json`. Run these from the repository root:

```powershell
. .\tools\dev-env.ps1        # once per terminal: load the MSVC environment
cmake --preset debug          # configure (only needed after changing CMakeLists.txt)
cmake --build --preset debug  # compile
.\build\debug\bin\game.exe    # run
```

Replace `debug` with `release` for an optimized build. In VS Code, CMake Tools does all of this from the status bar; select the `Debug (validation layers on)` configure preset once and it is remembered.

| Preset | Compiler flags | Validation layers | Use for |
|---|---|---|---|
| `debug` | `/Od /Zi` — no optimization, full debug info | On | Everyday development |
| `release` | `/O2` — optimized | Off | Measuring real performance |

All build output goes to `build/`, which is git-ignored. Deleting `build/` is always safe and is the correct first response to a build that has gone strange.

---

## Dependencies

Fetched by CMake at configure time via `FetchContent`, pinned to exact Git tags. Nothing is committed to the repository.

| Dependency | Version | Why it is here |
|---|---|---|
| GLFW | 3.4 | Creates the OS window, handles keyboard/mouse input, and creates the Vulkan drawing surface. Without it we would hand-write several hundred lines of raw Win32 code. |
| Vulkan | SDK 1.4.357.0 | Found via `find_package(Vulkan)`; not fetched. Provides the headers and the loader import library. |

**Adding a dependency requires a one-sentence justification of why the current milestone needs it.** "We'll want it eventually" is not sufficient.

### Dependency sources are nested Git repositories, and that is expected

`FetchContent` clones GLFW into `build/<preset>/_deps/glfw-src`, which is a real Git repository carrying GLFW's own upstream branches and sitting at `HEAD detached at 3.4`. It is **not** part of this project's history — `build/` is gitignored, so nothing there is ever committed or pushed. `.vscode/settings.json` adds `build` to `git.repositoryScanIgnoredFolders` so VS Code's Source Control panel does not list it alongside the real repository. This project has exactly one branch: `main`.

---

## Engine Architecture (current)

Milestone 1 only. Each type owns its Vulkan resources and destroys them in its destructor.

| Component | Header | Responsibility |
|---|---|---|
| `Log` | `engine/core/Log.hpp` | Minimal timestamped console output at info/warn/error levels. Deliberately trivial — replaced when there is a real need. |
| `FrameLimiter` | `engine/core/FrameLimiter.hpp` | Paces the main loop to a target frame rate, adjustable at runtime. A target of `0` means uncapped. |
| `Window` | `engine/platform/Window.hpp` | Owns the GLFW window and its lifetime. Reports whether a close was requested, pumps OS events, and exposes a queue of key presses via the engine-level `Key` enum so the game never includes GLFW. |
| `VulkanContext` | `engine/render/VulkanContext.hpp` | Vulkan instance, debug messenger, window surface, physical device selection, logical device, and queues. The one-time setup that everything else needs. |
| `Swapchain` | `engine/render/Swapchain.hpp` | The set of images that get shown on screen, plus their views. Rebuilt when the window resizes. |
| `Renderer` | `engine/render/Renderer.hpp` | Command pool, command buffers, per-frame synchronization, and the per-frame record/submit/present cycle. |

**Vulkan vocabulary, briefly:**
- **Instance** — the connection between the program and the Vulkan library.
- **Physical device** — a GPU that exists on the machine.
- **Logical device** — the program's own handle onto that GPU, with the features it asked for.
- **Queue** — a lane for submitting work to the GPU. Graphics and presentation may or may not be the same lane.
- **Surface** — the bridge between Vulkan and the OS window.
- **Swapchain** — the small ring of images the GPU draws into and the OS displays, rotated each frame so drawing and displaying never touch the same image.
- **Command buffer** — a recorded list of GPU instructions, submitted in bulk rather than one call at a time.

---

## Frame Pacing

The loop is capped so it does not render frames nobody sees. **Default cap: 120 fps.** `F1` steps the cap down, `F2` steps it up, through `30 / 60 / 90 / 120 / 144 / 165 / 240 / uncapped`. Changes take effect immediately and are logged.

The cap list and key bindings are **game policy** and live in `game/src/Main.cpp`; `FrameLimiter` itself only knows about a target number. The keyboard control is a temporary stand-in until there is a real settings screen — it is not intended to be the permanent interface.

The limiter is deliberately independent of the swapchain present mode, which is `mailbox` (render as fast as possible, always show the newest finished frame, no tearing). Using vsync/`FIFO` instead would lock the frame rate to the monitor's refresh rate, which is not the same thing as a user-chosen cap.

**Implementation note:** the wait uses a Win32 high-resolution waitable timer plus a sub-millisecond spin, not a plain sleep — see `CLAUDE.md` for why a plain sleep produces roughly 64 fps when asked for 120.

---

## Runtime Behavior

Milestone 1 program flow:

1. Create the window.
2. Initialize Vulkan (instance → surface → physical device → logical device → swapchain).
3. Loop: poll OS events → apply any frame-cap key presses → acquire a swapchain image → record a command buffer that clears it to a solid color → submit → present → wait out the remainder of the frame budget.
4. On window close: wait for the GPU to go idle, then destroy everything in reverse order.

**Waiting for GPU idle before shutdown is mandatory**, not a nicety — destroying resources the GPU is still using is undefined behavior.

The window shows a solid dark blue and nothing else. That is the intended and complete result for this milestone.

---

## Current Validation Baseline

Verified 2026-07-30 on this machine.

- **Configure:** `cmake --preset debug` succeeds. GLFW 3.4 fetched, `Found Vulkan: 1.4.357` with `glslc` and `glslangValidator` components.
- **Compile:** clean build of all 31 steps, **zero warnings** at `/W4 /permissive-`.
- **Runtime:** window opens at 1280x720; validation layers report **no errors** across multiple runs including a continuous 77-second session.
- **GPU selection:** correctly picks `NVIDIA GeForce RTX 4070 Laptop GPU`, not the Intel iGPU that enumerates first.
- **Swapchain:** 1280x720, 3 images, `mailbox` present mode.
- **Frame pacing:** holds 120–121 fps continuously against a 120 fps target. Uncapped, the same scene runs 400–1600 fps, so the cap is doing real work.
- **Shutdown:** closing the window logs `Window closed. Shutting down.` and exits through the normal path with no validation errors and no crash.

---

## Update Discipline

Keep this file factual, current, and concise. Replace superseded facts in place — no changelog, no dated entries, no "previously this was X."

Anything that is a *story* (why a choice was made, what was tried and rejected, what fooled someone) belongs in `CLAUDE.md` instead.
