# WLWengine roadmap

The engine is being brought to big-engine quality in architecture, practices and features: mobile
first (Android, iOS, web), with desktop as a full target. This plan came out of the October 2026
architecture review. Work goes one step at a time, in order. Step ids are stable: commits name
them ("Roadmap 4.3b: ...").

**Current step:** 1.5 Skybox after opaques (not planned yet) · **Updated:** 2026-10-16

**Next session:** research and plan 1.5 (concept + exact plan in chat, wait for the OK), then implement it on a
branch. 1.4 is done: merged 2026-10-16 ("Next session plan: 1.4" below has its plan and outcome).
1.3 is done: 1.3a and 1.3b merged 2026-10-15.
1.3b (canvas shader): each pixel computes only its quad's kind. Mali-G78 longest path 2.62 -> 1.38 arithmetic cycles,
images 0.16; Pixel 4 Threadlock canvas GPU 4.96 -> 2.30 ms (-54%), frame 7.08 -> 5.0 ms (CHANGELOG 1.3b). The owner
chose derivatives + `texture()` inside the branches (quad-uniform flow) over the plan's `textureGrad`. Golden references
canvas, render_scale and render_scale_post were renewed from CI. Verified: 263 tests, CI green, Bluzzle desktop UI, Threadlock
on the Pixel 4. Not run: the emulator (see the next rule).
New rule (owner, 2026-10-15): always ask before using the Android emulator (the owner may be playing a game on the PC
and it can crash); the Pixel 4 is separate. Games are still on v0.1.0 and get Phase 1 with v0.2.0.
1.3a (2026-10-15): the Pixel 4 (Adreno 640) reversed the mediump fragment default (Bluzzle's lit 3D 8% slower): GLES
stays highp, shaders opt in only where a phone measures a gain (none in the engine yet). Numbers: CHANGELOG 1.3a.

Where things stand (2026-10-15):
- Done: 0.1 doctest, 0.2 CI, 0.3 golden images, 0.4 logging and asserts, 0.5 cvars and console, 0.6 Tracy, 0.7 benchmarks,
  0.8 build hygiene, 0.9 versioning (Phase 0 = engine v0.1.0, tagged). Each is one squashed commit on main named "Roadmap 0.x: ...", with details in CHANGELOG.md
  (0.8 also has a separate reformat commit, listed in `.git-blame-ignore-revs`). Phase 1: 1.1 frame pacing (Swappy vendored in
  `third_party/swappy`; games must add its Java dir + an R8 keep rule when they move to the next engine version),
  1.2 render scale (no game uses it yet: `QualitySettings::render_scales` or `SetRenderScale` when a game wants it).
- How the owner works: the plan goes in chat first; code only after an OK. Work on a branch (`<topic>/setup`)
  pushed to GitHub so CI checks it, then show the result and squash-merge to main only on approval. Delete the
  work branch after. Ask before deleting files.
- CI: GitHub Actions (`.github/workflows/ci.yml`). Every push runs clang-format (check), Windows MSVC Debug, Linux
  clang ASan+UBSan (+ golden images on llvmpipe under xvfb) and Android arm64 (NDK 30, builds engine + Snake,
  WLW_PROFILE on, WLW_PCH off). Windows Release runs only on main and manual runs (private repo: the owner wants
  zero cost; GitHub stops at the free minutes). Markdown-only pushes don't run CI. Warnings are errors in every
  preset; actions are on their Node 24 versions.
- Formatting (from 0.8): `cmake -P cmake/ClangFormat.cmake` before committing (pinned clang-format 23.1.3 from its
  PyPI wheel, downloaded into build/tools; no install). `.clang-format` = the style the code was already in.
- GitHub CLI: installed and logged in (`C:\Program Files\GitHub CLI\gh.exe`, not on the bash PATH: call it by
  its full path). `gh run list/watch/view --log-failed`, `gh run download <id> --name gpu-output`.
- Golden images: a rendering change that changes pictures on purpose gets new references from CI's `gpu-output`
  artifact (llvmpipe), looked at and reviewed like code. They're plain git, not LFS (a .gitattributes rule).
- Tests: `wlw_tests` + `snake_game_tests` (242 cases) + `wlw_gpu_tests` (9, label gpu) + `bench: smoke` + 5 `deprecations` (the script and its tests) = 257 CTest tests. Cases
  run in parallel processes (no shared temp files); global state (log filters, cvars) is restored per case, and
  test cvars are static with unique "test.<case>.*" names.
- Benchmarks (from 0.7): `wlw_benchmarks` + baselines in `benchmarks/baselines/` (windows-local, pixel4, linux-ci);
  CLAUDE.md "Benchmarks" has the commands. Performance steps run it before and after with `--baseline` on
  desktop + CI; phone runs are optional (owner's decision 2026-10-14: the engine isn't tied to one phone). Phone
  runs must be pinned to one fast core (Pixel 4: `taskset 80`): unpinned runs jump ~17% between cores. `bench.yml` runs on
  main, manual runs and `bench/**` branches (name a perf work branch `bench/...` to get CI numbers). Known hot
  spots: render/gather 10k = 1.86 ms on the Pixel 4 (GetAABB + world matrices recomputed per node: Phases 2.2,
  4.5), fx/particles instances 5k = 440 us (1.6), transform/move flat 10k = 1.3 ms (2.2).
- Games: Bluzzle and Threadlock are on engine v0.1.0 (2026-10-15; glm dropped from their vcpkg.json). Bluzzle
  (desktop + Android) and Threadlock (Android) build with zero warnings, their tests pass, Threadlock runs on the Pixel 4. They get
  `WLW_OPTIMIZE_ENGINE_IN_DEBUG` (engine -O2 in their Debug builds) and the `/W4` warnings from 0.8. Header-only engine code (node.h transforms, templates) stays -O0 in a game's Debug
  build: moving hot code into .cpp files (Phase 2.2) is what makes it fast there.
- From 0.6: on desktop NVIDIA the frame's first glGet waits for the driver thread (frame pacing). The renderer
  calls `glGetIntegerv(GL_FRAMEBUFFER_BINDING)` every frame; caching it would stop that wait landing in our code
  (it moves, it isn't removed: worth checking in Phase 4). Tracy in a phone game needs the INTERNET permission
  in the game's profile build; nobody has profiled on the Pixel 4 yet.
- Open items: typing in the console on
  phones waits for GameTextInput (D9). Nobody has typed into the new console yet: the owner was asked to
  try ` in Snake.
- Local machine: use `build/win-debug` and `build/win-release` (`build/windows-debug` is stale), from a VS 18
  dev shell. The Android preset builds locally with
  `ANDROID_NDK_HOME=%LOCALAPPDATA%\Android\Sdk\ndk\30.0.16248370`. WSL Ubuntu has no compilers: Linux is checked
  by CI only.
- 1.3 was researched and planned on 2026-10-15 and the owner approved the plan with the recommended decisions (next
  section). Findings that belong to later steps are written into those steps: 3.6, 4.2, 6.6, 8.2, 10.2, 10.4.

## Next session plan: 1.4 (approved 2026-10-15)
Branch `bench/normals` (so `bench.yml` runs), one squashed commit "Roadmap 1.4: cofactor normals". The owner took
every recommended decision. *Outcome: done as below, merged 2026-10-16 (numbers: CHANGELOG 1.4). Verified: 269 tests, CI
green with unchanged golden references, Bluzzle on the Pixel 4. Not done: a Bluzzle desktop screenshot (the capture
came back blank).*

**Concept:** normals need `transpose(inverse(M3))`. Before 1.4: a 4x4 inverse on the CPU per visible plain node
(`CullScene`), `inverse(uModel * aInstanceTransform)` per vertex in `mesh.vert` INSTANCED and in the game shaders'
default vertex stage. The cofactor matrix (columns b×c, c×a, a×b) has the same directions with no division (finite
when a scale is 0), negated when det < 0 so mirrored nodes stay lit outward (the bare cofactor points inward).
Filament does the same; its prescale is skipped (we normalize per vertex in highp).

**Plan:**
1. `core/normal_matrix.h`: `core::NormalMatrix(mat3)` + a mat4 overload.
2. `CullScene`: `NormalMatrix(model)` for every visible node, instanced ones included (the node's part). The driver
   uploads `uNormalMatrix` for every mesh draw, game shader materials too.
3. Shaders: `wlw/common.glsl` gets `NormalMatrix(mat3)` (a `dot < 0 ? -1 : 1` sign, not `sign()`: det 0 must keep
   the normal) and `SafeNormalize`. `mesh.vert`: `uNormalMatrix` in every variant; INSTANCED = node part x the
   copy's `NormalMatrix()` per vertex, positions through matrix-vector products. `shadow.vert` INSTANCED the same
   (the plain one stays: compilers hoist the uniform-only product). Game shaders: `uNormalMatrix` in both stages,
   `WorldPosition()` / `WorldNormal()` helpers in `vertex()`, the default vertex stage uses them.
4. `shader.h` docs (additive: no game has mesh shaders; all 5 game repos checked). glTF loader uses
   `core::NormalMatrix`.
5. Tests: `normal_matrix` suite against double-precision inverse-transpose (random transforms, mirrors, flat
   transforms, product rule); `cull normal matrices` updated; GPU `normals match inverse-transpose` (transformed
   vs CPU-baked pictures, engine shader + game shader; no stored reference); the shader compile test's game shader
   uses the helpers. Existing golden references must not change.
6. Measure: malioc before/after, `render/cull` before/after on Windows + Linux CI, Bluzzle desktop before/after,
   Android build of Bluzzle + Threadlock (Pixel 4 if connected; ask before the emulator).
7. Docs: CLAUDE.md, CHANGELOG, this file (notes for 1.5, 7.3, 8.2), the benchmark's wrong "sees about half".

**Decisions after review (owner, 2026-10-16):** back-face culling stays with 8.2 (noted there). Game shaders' default
vertex stage keeps the `WorldPosition` / `WorldNormal` helper form (Mali-G78: 26 registers, full occupancy, load/store
14 -> 16 cycles) over one branch around all of `main()` (load/store 14, 48 registers, half occupancy).

## How a step runs
1. Concept and plan in chat: why it exists, the trade-offs, how the big engines do it, exactly
   what will change. Wait for the owner's OK.
2. Implement. Big steps are split into sub-steps (4.3a, 4.3b...) when they start; each one is a
   reviewable change that leaves `main` working.
3. Verify against the definition of done, show the changes, commit only after approval.
4. Tick the step here with its commit(s); record any decision that changed.

## Definition of done (every step)
- CI green on every platform that exists at that point (from 0.2 on).
- New logic has unit tests; rendering changes have golden-image tests (from 0.3 on); performance
  work has benchmark numbers before and after on desktop (`windows-local`) and Linux CI. Phone
  numbers (any Android device, compared only with its own baseline) are optional: taken when a
  phone is at hand, never a blocker. Firebase Test Lab (6.6) brings a device matrix later.
- Bluzzle (desktop and Android) and Threadlock (Android) build and run with the change; Snake
  compiles.
- CLAUDE.md and CHANGELOG.md updated; breaking changes come with migration notes.
- Whatever a step replaces is deleted within the same phase (file deletions confirmed with the
  owner first). A renamed public API may keep a thin `WLW_DEPRECATED` forwarding shim through one
  release (CLAUDE.md "Versioning"); the CTest test `deprecations` makes sure it goes after that.

## Principles
- Hot paths are data-oriented: dense arrays, handles, no per-frame allocations, no virtual call
  per object per frame.
- Everything is testable without a GPU or a window; GPU output is covered by golden images.
- Per-OS code only in `platform/<os>/`; per-graphics-API code only in `rendering/<backend>/`.
- Explicit GPU work: pipelines are objects, passes say how they load and store their targets,
  resource lifetimes are owned.
- Measure on devices; every quality tier has a budget; no battery spent for nothing.
- Deterministic where it matters: simulation, random numbers, draw order.
- Every API says which thread may call it.

## Decisions (confirmed when their step starts)
- **D1 Math:** glm is the only math library, behind `wlw` aliases (`Vec3`, `Quat`, `Mat4`...).
  Radians, Y-up, right-handed, -Z forward, meters.
- **D2 Tests:** doctest; golden images (llvmpipe / lavapipe in CI); nanobench benchmarks.
- **D3 Profiling:** Tracy, plus ATrace sections on Android.
- **D4 Graphics:** our own RHI shaped like WebGPU (pipelines, bind groups, passes with load/store
  actions); backends GL 3.3 / GLES 3.0 / WebGL 2, Vulkan 1.1 (1.3 features when present), Metal 2;
  a frame graph on top.
- **D5 Shaders:** GLSL 4.50 source -> glslang -> SPIR-V -> SPIRV-Cross (GLSL 330, ESSL 300, MSL),
  compiled at build time; a runtime compiler only for shaders that games build from strings.
- **D6 Threads:** our own work-stealing job system; optional render thread.
- **D7 Assets:** build-time cooker; KTX2 + Basis Universal (direct ASTC/BC where better); cgltf +
  meshoptimizer + MikkTSpace; pak files with zstd.
- **D8 Colour:** linear HDR with tonemapping by default; gamma mode kept as an opt-out.
- **D9 Android:** GameActivity + AGDK (Swappy, GameTextInput, Paddleboat) + ADPF.
- **D10 Physics:** Jolt (3D), Box2D v3 (2D).
- **D11 Text:** FreeType + HarfBuzz + SheenBidi + msdfgen + libunibreak.
- **D12 Editor:** an in-process ImGui (docking) editor build of the game, driven by reflection.
- **D13 Dependencies:** vendored, pinned sources in `third_party/`, built by our CMake on every
  platform (versions and licences in `third_party/README.md`); vcpkg only for desktop-only GLFW.

## New third-party libraries (all permissive)
doctest (MIT), nanobench (MIT), Tracy (BSD-3), volk (MIT), Vulkan-Headers (Apache-2.0), Vulkan
Memory Allocator (MIT), glslang (BSD-3), SPIRV-Tools (Apache-2.0), SPIRV-Cross (Apache-2.0), AGDK
Swappy / GameActivity / GameTextInput / Paddleboat (Apache-2.0), zstd (BSD), cgltf (MIT),
meshoptimizer (MIT), MikkTSpace (zlib), Basis Universal (Apache-2.0), astc-encoder (Apache-2.0),
tinyexr (BSD-3), FreeType (FTL: credit required), HarfBuzz (MIT), SheenBidi (Apache-2.0),
libunibreak (zlib), msdfgen (MIT), Jolt (MIT), Box2D v3 (MIT), ImGuizmo (MIT), FidelityFX FSR 1
(MIT).

## Constraints
- Metal, macOS and iOS need a Mac (and an iPhone; the Apple Developer Program to ship). Until then
  CI can only compile them.
- Vulkan on Windows: the Vulkan SDK installed (validation layers, tools).
- Devices: development isn't tied to any phone the owner has. Phone checks use whatever Android
  device is at hand (a Pixel 4 / Adreno 640 was used so far); Firebase Test Lab (6.6) for a matrix
  (Adreno, Mali, low end) without owning them.
- Bluzzle is live in testing: its releases pin a tagged engine version (from v0.1.0; one minor
  version per phase, CLAUDE.md "Versioning").

## Phase 0: Safety net and tooling
- [x] **0.1 doctest.** *(done 2026-10-14, commit "Roadmap 0.1: doctest"; the CHANGELOG has the details)* The 24 suites move to doctest: one `TEST_CASE` per behaviour, the same
  assertions (compound conditions in doctest's double parentheses), a shared test main, cases
  discovered by CTest. Snake's tests too.
- [x] **0.2 CI.** *(done 2026-10-14, commit "Roadmap 0.2: CI"; the first runs were not checked yet: no `gh` on the dev machine)* GitHub Actions: Windows MSVC Debug + Release, Linux clang with ASan + UBSan,
  Android NDK arm64 build of engine + Snake; dependency caching; every test. Later phases add TSan,
  web, macOS and iOS jobs.
- [x] **0.3 Golden images.** *(done 2026-10-14, commit "Roadmap 0.3: golden images"; references from llvmpipe 20.1.2; real-GPU threshold 16 / 0.5% of pixels)* `tests/gpu`: reference scenes (lit cubes, shadows, transparency,
  instancing, sprites, post chain, canvas text) rendered offscreen and compared with PNG references
  (tolerance, diff images on failure, `--update`); references from llvmpipe under xvfb in CI; the
  real GPU locally with a looser threshold.
- [x] **0.4 Logging and asserts.** *(done 2026-10-14, commit "Roadmap 0.4: logging and asserts"; file log in debug builds only, logger.h deleted)* Levels (Verbose to Fatal), categories, compile-time stripping,
  runtime filters, sinks (console/logcat, file, an in-memory ring for the console, callbacks for
  crash breadcrumbs), thread ids and timestamps. `WLW_ASSERT`, `WLW_VERIFY` and `WLW_UNREACHABLE`
  replace `FAIL_IF`.
- [x] **0.5 CVars and console.** *(done 2026-10-14, commit "Roadmap 0.5: cvars and console"; dotted lowercase names; command line and adb work in release except Cheat cvars; typing in the console on phones waits for GameTextInput, D9)* Typed console variables (name, default, help, flags) set from
  code, a config file and the command line (adb intent extras on Android); a debug-UI panel listing
  them; an ImGui console with log, commands and completion.
- [x] **0.6 Tracy.** *(done 2026-10-14, commit "Roadmap 0.6: Tracy"; Tracy 0.14.1, TRACY_ON_DEMAND, off unless `WLW_PROFILE`; profile presets; GPU zones via Tracy's OpenGL timestamps (GLES: EXT_disjoint_timer_query); Pixel 4 run pending)* `WLW_PROFILE_*` macros (zones, frame marks, plots, thread names, GPU zones
  where supported), ATrace on Android, compiled out of shipping builds; zones on every Application
  phase and renderer stage.
- [x] **0.7 Benchmarks.** *(done 2026-10-14, commit "Roadmap 0.7: benchmarks"; nanobench 4.3.11; gather/cull moved to GL-free `rendering/scene_gather.h`; CI reports, warns at +25%, never fails; Pixel 4 baseline pinned to the prime core)* nanobench target: gather/cull/sort at 1k and 10k nodes, transform updates,
  FlowParticles 20k, ParticleSystem 5k, canvas text 1k glyphs, mixer 24 voices; baselines committed;
  CI reports them.
- [x] **0.8 Build hygiene.** *(done 2026-10-15, commit "Roadmap 0.8: build hygiene" + a reformat commit; engine-in-Debug 10-35x faster; PCH -33% clean build; glm vendored; clang-format 23.1.3 pinned via its PyPI wheel; C4100 and missing-field-initializers stay off)* `WLW_OPTIMIZE_ENGINE_IN_DEBUG` (engine and third_party at -O2 with debug
  info, game code at -O0); `/W4 /permissive-` and `-Wall -Wextra -Wshadow`; warnings as errors in
  CI; precompiled headers; dependencies vendored per D13; clang-format check.
- [x] **0.9 Versioning.** *(done 2026-10-15, commit "Roadmap 0.9: versioning"; tag v0.1.0; one minor version per phase; git commit in BuildInfo; shims checked by the CTest test `deprecations`)* `wlw/version.h` from `project(VERSION)`, CHANGELOG.md, `WLW_DEPRECATED`
  shims kept for one minor version, a tag per milestone.

## Phase 1: Quick mobile wins on the current renderer
- [x] **1.1 Frame pacing.** *(done 2026-10-15, commit "Roadmap 1.1: frame pacing"; Swappy 2.3.0-alpha01 vendored (AGDK archived its repo in 2025); default 60 fps; auto swap interval off; desktop 240 Hz: 60 fps sd 0.12 ms, limiter 100 fps sd < 0.02 ms)* `AppConfig::target_fps` (30/60/90/120/display) with a runtime setter.
  Android: Swappy (GL) for pacing and refresh-rate choice, `ANativeWindow_setFrameRate` as fallback.
  Desktop: vsync plus a precise limiter. Quality's budget follows the target.
- [x] **1.2 Render scale.** *(done 2026-10-15, commit "Roadmap 1.2: render scale"; 0.25-2, Picture layers follow the scale, post pass sizes stay relative to the viewport (constant look), upscale free with passes / one copy without; cvar r.render_scale; RTX 5080 2560x1440: 3D 2.20 ms at 1, 1.21 at 0.75, 0.53 at 0.5)* `Viewport::SetRenderScale` for the 3D picture (UI stays native),
  bilinear upscale, a Quality hook, shown in the engine panel.
- [x] **1.3 Fragment precision.** *(done 2026-10-15, commits "Roadmap 1.3a: fragment precision" + "Roadmap 1.3b: canvas shader"; planned and approved 2026-10-15: "Next session plan: 1.3" above; decision changed by
  the Pixel 4 measurements: GLES stays highp by default, shaders opt in to `mediump` where a phone measures a gain)*
  Explicit precisions: `highp` for positions, UVs, pixel and canvas coordinates, depth and shadow
  coordinates and the specular term; every uniform, block member and varying names its precision (GLSL ES needs both
  stages to agree); sampler defaults for every sampler type; game shaders highp unless they opt in; cvar `r.mediump`; a
  GLSL ES compile check in CI through a desktop ES context; Mali Offline Compiler numbers, GPU time on a real phone
  when one is at hand (or Test Lab). Split: **1.3a** precision *(done 2026-10-15, commit "Roadmap 1.3a: fragment precision")*, **1.3b** canvas shader *(done 2026-10-15, commit "Roadmap 1.3b: canvas shader"; Pixel 4 Threadlock canvas 4.96 -> 2.30 ms)* (each pixel computes only its
  own quad kind).
- [x] **1.4 Cofactor normals.** *(done 2026-10-16, commit "Roadmap 1.4: cofactor normals"; render/cull 10k 54.7 -> 39.5 us; Mali-G78 instanced mesh vertex 6.50 -> 2.40 arithmetic cycles, 64 -> 32 registers)* Replace the per-vertex `inverse()` and the CPU inverse; correct for
  mirrored transforms; tested against inverse-transpose.
- [ ] **1.5 Skybox after opaques.** Depth test `LEQUAL`; golden image. (From 1.4: `skybox.vert` multiplies
  `uProjection * mat4(mat3(uView))` per vertex: one matrix-vector chain, or the product on the CPU, instead.)
- [ ] **1.6 Instances.** 52-byte instances (3x4 matrix + RGBA8), instance bounds cached on change,
  a `WriteInstances(count)` span API; Bluzzle's particles use it.

## Phase 2: Core API (breaking, while the games are small)
- [ ] **2.1 Math, colour, geometry.** `wlw/core/math.h` (glm configuration, aliases);
  `core::Vector2/3` removed. `Color`: linear RGBA, hex and sRGB constructors, operators, `Lerp`,
  `WithAlpha`, `Color32`. Geometry (AABB, Sphere, Ray, Plane, Frustum) with intersection tests in
  one namespace.
- [ ] **2.2 Transform and Node3D.** Position + quaternion + scale; local and world matrices cached
  with dirty propagation; children in a vector; `SetParent(keep_world)`, `LookAt`, `Rotate`, world
  getters and setters, Euler helpers; names, `FindChild` and `GetNode(path)`; a render layer mask.
  The `Node<T>` template, Node2D, Vertex2D and Triangle2D are removed; Scene gets a root node.
- [ ] **2.3 Node types.** Node3D keeps only transform, hierarchy, visibility and layers. New:
  MeshInstance3D, MultiMeshInstance3D, SpriteBatch, DirectionalLight3D, PointLight3D, SpotLight3D,
  Camera3D (perspective and orthographic with fit modes, `ScreenPointToRay`, `WorldToScreen`,
  frustum). Fly, orbit and follow camera controllers.
- [ ] **2.4 Conventions.** Y-up, right-handed, -Z forward, meters, radians; the default camera
  follows them; documented and tested.
- [ ] **2.5 API consistency.** `GetScreen()` -> `MainViewport()`; every `Create()` returns
  `shared_ptr`; const-ref getters; removal by object instead of int ids; renderer-only methods
  behind an internal accessor; Screens get `App()`; AppConfig grouped (window, graphics, audio).
- [ ] **2.6 Legacy removal.** `WLWEngine`, `RenderingDriver::SetViewport/Clear/AttachWindow`,
  `Window::ProcessMouseMovement/SetIsActive`, FPSCamera and FollowCamera, TextureManager, GEMINI.md
  (each deletion confirmed with the owner).
- [ ] **2.7 Platform-driven frame.** `Application::Init/Frame/Shutdown`; `Run()` loops on desktop
  and Android; `WLW_MAIN` unchanged.
- [ ] **2.8 Migrate the games.** Bluzzle, Threadlock and Snake updated; a migration guide for
  Epicycles, Silhouette and Ripple in the CHANGELOG.

## Phase 3: Runtime infrastructure
- [ ] **3.1 Job system.** Workers (cores - 1), work-stealing deques plus an injection queue,
  dependency counters, `ParallelFor` with a grain size, the waiting thread helps, priorities, Tracy
  zones, an inline mode (0 workers); stress tests and a TSan CI job.
- [ ] **3.2 Memory and handles.** Per-frame and per-thread linear allocators, pools, generational
  `Handle<T>` with dense storage, memory accounting by category (engine panel, Tracy).
- [ ] **3.3 Signals.** `Signal<void(Args...)>` with scoped connections; queued signals delivered on
  the game thread.
- [ ] **3.4 File system and async IO.** Mounts `res://` (folder, APK, bundle, pak), `user://`,
  `cache://`; sync and async reads completed on the game thread; pak archives (index, per-file
  zstd, aligned for memory mapping); a desktop file watcher; replaces `platform::ReadFile`.
- [ ] **3.5 Resource manager.** Typed ref-counted handles; sync and async loading with states and
  dependencies; a cache by path; unload on last release; a per-frame GPU upload budget; source
  tracking to rebuild after device loss; desktop hot reload.
- [ ] **3.6 Web.** Emscripten: WebGL 2 through the GL backend, HTML5 input, `Frame()` from
  `emscripten_set_main_loop`, files preloaded or fetched, saves in IndexedDB, WebAudio unlocked on
  the first input. A single-threaded build by default, a threaded build for cross-origin-isolated
  hosts; a CI job; WebGL 2 limits added to the CLAUDE.md rules. WebGL 2 shaders are GLSL ES 3.00:
  1.3's precision rules (both stages agree on every uniform and block member) and its GLSL ES
  compile check already apply.

## Phase 4: Rendering architecture
- [ ] **4.1 RHI and null backend.** Device (caps, limits, frames in flight, deferred destruction),
  Buffer, Texture and views, Sampler, ShaderModule, bind group layouts and bind groups (frame /
  view / material / draw), graphics and compute pipelines, a command encoder with render, compute
  and copy passes (load/store actions, clear values, dynamic offsets, labels, timestamps),
  Swapchain (acquire, present, resize, pre-rotation), clip-space and y-flip conventions per backend.
  The null backend records commands for tests.
- [ ] **4.2 Shader pipeline.** Engine shaders in GLSL 4.50 with includes and permutation keys; a
  `wlw_shaderc` build tool (glslang, spirv-opt, SPIRV-Cross, reflection); per-platform shader
  archives. The game shader language (`shader_type ...`) goes the same way, with a runtime compiler
  and disk cache only when games create shaders from strings. A precision strategy (mediump /
  RelaxedPrecision / half) that keeps 1.3's rules: every uniform, block member and varying names its
  precision, fragment code defaults to mediump, game shaders stay highp unless they opt in, and the
  GLSL ES compile check moves into the shader compiler (game shaders compiled at runtime keep it).
  Variant lists recorded in debug builds for warm-up.
- [ ] **4.3 GL backend.** The RHI on GL 3.3 / GLES 3.0 / WebGL 2: tracked state (no `glGet` in a
  frame), sampler objects, immutable texture storage, a VAO cache with the index buffer inside the
  VAO, per-frame upload rings (unsynchronized mapping + fences; `glBufferSubData` on WebGL), per-draw
  data in one UBO with dynamic offsets, an FBO cache, load/store -> clear/invalidate, MSAA
  (`EXT_multisampled_render_to_texture` on mobile, blit resolve elsewhere), a program binary
  cache, `KHR_debug` labels, timer queries.
- [ ] **4.4 Meshes and vertex formats.** Mesh as a Resource (GPU side in the backend, CPU copy
  optional); vertex layouts with streams (a position-only stream for depth and shadow passes); a
  packed 24-byte default (float3 position, packed normal + tangent, half/unorm16 UV, RGBA8 colour);
  skinning attributes; 16-bit indices when they fit; uploads through the resource manager and the
  warm-up.
- [ ] **4.5 Render world and frontend.** A RenderWorld per Scene: dense proxies (instances,
  multi-meshes, sprites, lights, cameras) updated from node change lists; parallel frustum and layer
  culling; draw packets with 64-bit keys (pipeline, material, mesh, depth); automatic instancing of
  identical mesh + material runs; materials as resources with parameter blocks and bind groups
  (Unlit, SimpleLit and custom ShaderMaterials on one path).
- [ ] **4.6 Frame graph.** Passes declare transient and imported resources; compiling culls unused
  passes, aliases transient textures, derives load/store actions and barriers, and orders
  execution. Shadow, opaque, sky, transparent, sprites, picture canvas, post chain (PostPlan folded
  in; the post-effect API unchanged), overlay canvas, debug UI and readback all become passes. A
  graph view in the debug UI.
- [ ] **4.7 Switch over.** Every current feature on the RHI (variants, instancing, sprites, sky,
  shadows, post effects, canvas renderer, ImGui renderer, GPU timing through RHI timestamps, Quality
  hooks); golden images match; the old driver is deleted.
- [ ] **4.8 Render thread.** A thread-model setting (single / render thread). The game thread
  extracts a frame packet; the render thread culls, builds the graph and submits, at most one frame
  behind. The GL context lives on the render thread; web stays single-threaded.

## Phase 5: Vulkan
- [ ] **5.1 Bootstrap.** volk, an instance with validation and debug utils in debug builds, device
  selection, queues, VMA, swapchain (Win32 through GLFW, Android surface), frames in flight, present
  modes, resize / out-of-date / suboptimal handling, Android pre-rotation, backend choice with
  automatic GL fallback.
- [ ] **5.2 Resources.** VMA buffers and images, staging uploads, per-frame upload rings, layout
  transitions, compressed formats by device caps, samplers, deferred destruction.
- [ ] **5.3 Pipelines and descriptors.** Pipelines from RHI descriptions (dynamic rendering when
  available, a render-pass cache otherwise), layouts from reflection, per-frame descriptor pools for
  transient sets, persistent material sets, dynamic offsets for per-draw data; `VkPipelineCache`
  saved to disk and validated; background pipeline compilation.
- [ ] **5.4 Passes and synchronization.** Render passes from the frame graph with load/store
  actions; lazily allocated transient depth and MSAA attachments on tilers; in-pass MSAA resolve;
  barriers (synchronization2 when present); timestamps; debug labels for RenderDoc and AGI.
- [ ] **5.5 Vulkan on Android.** A device policy (Vulkan 1.1+, Android 10+, a driver denylist, GL
  fallback), Swappy for Vulkan, validation layers in debug APKs, AGI profiling.
- [ ] **5.6 Parity and default.** Golden images on lavapipe (CI) and real GPUs; Bluzzle and
  Threadlock on Vulkan (Windows, a real phone or Test Lab, emulator); CPU, GPU and power compared with GL; the
  default backend per platform chosen from the numbers.

## Phase 6: Android platform
- [ ] **6.1 GameActivity.** AGDK GameActivity replaces NativeActivity (native glue, `WlwActivity`);
  input from its buffers (all pointers, history, timestamps); insets without JNI reflection; the
  games' Gradle files updated.
- [ ] **6.2 Refresh rate.** Swappy on both backends; the refresh rate chosen per screen (menus vs
  gameplay); checked with frame-time graphs.
- [ ] **6.3 ADPF and thermal.** Performance hint sessions for the game and render threads, thermal
  headroom forecasts feeding Quality (step down before throttling), the Game Mode API, low-power
  reactions.
- [ ] **6.4 Device and context loss.** Recovery from `EGL_CONTEXT_LOST` and `VK_ERROR_DEVICE_LOST` by
  rebuilding GPU state from resource sources; a debug command that simulates it; tests.
- [ ] **6.5 Display and lifecycle.** Orientation lock and changes, foldable and multi-window resizes,
  immersive mode, keep-screen-on, density (dp) for UI scale.
- [ ] **6.6 Device lab.** Firebase Test Lab game-loop tests with screenshots on a device matrix
  (Mali, Adreno, PowerVR, low end); native symbols for Play Console crash reports
  (`debugSymbolLevel`); a vitals checklist. This is the only place precision artifacts show (no
  software GPU has fp16, 1.3): the screenshots include sharp specular highlights, large textures,
  full-screen effects and the canvas, and compare `r.mediump` 1 with 0. Also measure mediump GPU time on
  Mali (1.3: Adreno 640 got slower, Mali's offline compiler predicts lit meshes at 31 instead of 44-48 work
  registers and the post effects at 100% fp16): opt the shaders that gain in, per GPU family if needed.
- [ ] **6.7 Size and delivery.** An AAB size report in CI; texture-format targeting and asset packs
  once assets grow.

## Phase 7: Content pipeline
- [ ] **7.1 Cooker.** A `wlw_cook` host tool, an asset database (`.meta` sidecars: GUID, import
  settings), content-hash incremental cooking, per-platform output, CMake and Gradle integration,
  pak output, clear errors.
- [ ] **7.2 Textures.** Decoding (PNG, JPG, 16-bit, HDR, EXR); sRGB-correct mips (normal
  renormalization, alpha-coverage preservation); KTX2 with Basis Universal (UASTC / ETC1S) and
  direct ASTC/BC7 per platform; runtime transcoding by device caps; cubemaps and arrays; sprite and
  UI atlas packing.
- [ ] **7.3 Models.** A cgltf glTF 2.0 importer: hierarchy, primitives, PBR materials and samplers,
  vertex colours, tangents (MikkTSpace), skins, animations, morph targets, `KHR_lights_punctual`,
  `KHR_texture_transform`, `KHR_mesh_quantization`, `KHR_texture_basisu`, `EXT_meshopt_compression`.
  meshoptimizer for cache / fetch / overdraw order, quantization and LODs. Output: a binary
  ModelAsset instantiated into nodes (this also fixes loading models from the APK). (From 1.4: a glTF node
  with a mirroring transform (det < 0) flips its triangles' winding; glTF says to reverse it when importing.
  The baked normals already point out: `core::NormalMatrix`.)
- [ ] **7.4 Fonts.** Cooked fonts: the TTF, prebaked MSDF atlas pages for chosen ranges, metrics.
- [ ] **7.5 Audio.** Decode and resample at cook time; load types Decompress / CompressedInMemory /
  Streaming; PCM16 or ADPCM for effects, Vorbis for long clips; mono stays mono.
- [ ] **7.6 Shaders and paks.** Per-platform shader archives and paks in the cooked output,
  versioned.
- [ ] **7.7 Hot reload.** Desktop watcher -> re-cook -> live reload of textures, meshes, materials,
  shaders, fonts and audio.

## Phase 8: Rendering features
- [ ] **8.1 Colour pipeline.** Linear workflow (sRGB textures and targets), HDR scene colour
  (RGBA16F or R11G11B10F by caps), manual and auto exposure, tonemapping (ACES fitted, AgX, PBR
  Neutral), 3D LUT grading, dithering, UI composited in sRGB after tonemapping; the gamma opt-out;
  Bluzzle and Threadlock retuned with the owner.
- [ ] **8.2 PBR materials.** Metallic-roughness (GGX, Smith, Schlick; Lambert or Burley diffuse),
  base / normal / ORM / emissive maps, alpha opaque / mask (alpha to coverage) / blend,
  double-sided; Lit, SimpleLit and Unlit tiers; live material tweaking in the debug UI. From 1.3:
  fp16 can't hold N·H near 1, so the specular terms stay highp or use Filament's form (1 - (N·H)²
  as |N×H|², exact in mediump); malioc checks the result. A way to turn specular off (a specular
  strength or colour, 0 = no highlight): Bluzzle's floor gets "no shine" from shininess 1000 today
  and moves to it then. From 1.4: the engine never turns on back-face culling. Single-sided materials want it,
  and a mirrored node (det < 0) then needs the front face flipped (`glFrontFace(GL_CW)`), and instanced copies
  that mirror split into their own draw (Godot: `CULL_VARIANT_REVERSED`). Normals already stay outward.
- [ ] **8.3 Environment lighting.** Skybox (cube, equirect), procedural sky, IBL (prefiltered
  specular, SH9 irradiance, BRDF LUT; cooked offline, computed at runtime for dynamic skies),
  reflection probes (baked, box-projected, blended).
- [ ] **8.4 Clustered lights.** Forward+ clusters, binned on the CPU with jobs (GLES 3.0) or in
  compute (Vulkan, Metal); inverse-square falloff with a range window; light layers; configurable
  limits. (Found in 1.3: malioc shows every lit mesh variant load/store bound, 10 LS cycles vs ~3.8
  arithmetic on Mali-G78, from the light loops reading `WlwFrame` with a runtime count and index; light
  lists per cluster, or loops with a compile-time bound, should fix it.)
- [ ] **8.5 Shadows.** Cascaded sun shadows (2-4 cascades, stable fit, texel snapping, blending,
  fade), an atlas for point and spot shadows, per-cascade caster culling with jobs, shadow data per
  viewport, PCF (PCSS on the high tier), presets per tier.
- [ ] **8.6 Anti-aliasing.** MSAA with on-tile resolve on every backend, FXAA fallback, TAA on the
  high tier (jitter, motion vectors from previous transforms, history clamping, sharpening).
- [ ] **8.7 Post stack.** Progressive bloom (Karis average), vignette, chromatic aberration, film
  grain, half-res bokeh depth of field, half-res GTAO (high tier), camera motion blur, SSR (desktop
  high tier); global and per-viewport settings with blending.
- [ ] **8.8 Dynamic resolution.** Render scale driven by GPU time, FSR 1 (EASU + RCAS) upscaling with
  a bilinear fallback, tied into Quality.
- [ ] **8.9 LOD and culling.** Screen-size LOD selection with hysteresis and dithered crossfade, LOD
  bias per tier, distance culling per layer, a static BVH for large scenes, occlusion culling (CPU
  software depth everywhere, Hi-Z compute on Vulkan/Metal).
- [ ] **8.10 Debug draw and views.** Immediate debug drawing (lines, boxes, spheres, frustums, text;
  timed; depth-tested or on top). Views: wireframe, overdraw, mip level, normals, albedo, lighting
  only, cascades, clusters, LODs.
- [ ] **8.11 GPU particles.** Compute emit / update / sort / indirect draw on Vulkan, Metal and GLES
  3.1+, with a CPU fallback (the current systems); curves and gradients over lifetime; flipbooks;
  depth-buffer collisions.
- [ ] **8.12 2D rendering.** World-space sprites with atlases, 9-slice, flipbook animation and
  sorting layers for 2D games.

## Phase 9: Input
- [ ] **9.1 Event core.** Timestamped events (keys, text, pointers with ids, wheel, gamepad); the full
  key set, all mouse buttons, cursor modes (hidden, locked, confined), multi-pointer state; the
  polling API built on the events.
- [ ] **9.2 Gestures.** Recognizers as a layer: tap, double tap, long press, swipe, pan, pinch,
  rotate; thresholds in dp; conflict rules. Today's built-in swipe and tap move here.
- [ ] **9.3 Actions.** Named actions (button, axis, 2D) with bindings and composites, contexts with
  priorities, runtime rebinding saved to `user://`, dead zones, input buffering.
- [ ] **9.4 Gamepads.** GLFW gamepads with the SDL mapping database, Paddleboat on Android, rumble.
- [ ] **9.5 Text input and IME.** UTF-8 text and composition events: GLFW on desktop, GameTextInput
  on Android; showing and hiding the soft keyboard.
- [ ] **9.6 Haptics.** Patterns and amplitude (Android VibrationEffect compositions; iOS with its
  platform).

## Phase 10: UI and text
- [ ] **10.1 Text layout.** UTF-8 everywhere, line breaking (libunibreak), wrapping, multi-line
  alignment, ellipsis, simple rich-text tags (colour, size, bold), cached shaped runs.
- [ ] **10.2 Fonts.** FreeType rasterization, a dynamic SDF/MSDF atlas with LRU pages, prebaked pages
  from the cooker, fallback chains (Latin -> Arabic -> CJK -> emoji), weights and styles, colour
  emoji. Text anti-aliasing keeps 1.3b's screen-pixel-range formula (the one MSDF uses), with the
  distance range per atlas page.
- [ ] **10.3 Shaping and bidi.** HarfBuzz (ligatures, kerning, Arabic and Indic scripts), SheenBidi
  for mixed directions, mirrored RTL layout.
- [ ] **10.4 Canvas.** Clip-rect (scissor) and transform stacks per batch, 9-slice images, gradients,
  borders and strokes, soft shadows, a UI atlas, backdrop blur. New quad kinds keep 1.3b's shape:
  derivatives taken once in uniform control flow, each pixel computes only its own kind, textures
  read only by the kinds that need them, per-quad constants in flat varyings, precision named per
  input (canvas coordinates highp, colour and coverage mediump).
- [ ] **10.5 Widgets and layout.** An ID stack, scroll views (inertia, overscroll), virtualized lists,
  a text field (IME), slider, progress bar, checkbox and radio, dropdown, a modal stack,
  keyboard/gamepad focus navigation, flexbox-style immediate-mode layout, accessibility hooks
  through the bridge.
- [ ] **10.6 Localization.** String tables cooked per locale, locale detection, runtime switching,
  CLDR plural rules for shipped languages, number and date formatting, pseudo-localization, font
  fallback per locale.

## Phase 11: Audio
- [ ] **11.1 Voices.** `Play` returns a handle (stop, fade, volume, pitch and pan while playing);
  priorities, voice limits with stealing, virtual voices for inaudible sounds.
- [ ] **11.2 Mixer graph.** User-defined buses (volume, mute, solo) with effects (biquad filters,
  compressor, limiter, reverb, delay), sends, ducking, snapshots with transitions.
- [ ] **11.3 Spatial audio.** A listener on the camera; an AudioSource3D node (attenuation curves,
  cone, doppler, spread); equal-power panning; occlusion low-pass from physics raycasts.
- [ ] **11.4 Clips and streaming.** Load types from the cooker, compressed-in-memory decoding, any
  clip streamable, ref-counted unload, music playlists with crossfades and stingers.
- [ ] **11.5 Platform audio.** AAudio performance mode, output-device changes (headphones), latency
  reporting; iOS and web specifics arrive with their platforms.

## Phase 12: Animation
- [ ] **12.1 Skeletons and skinning.** A skeleton resource, GPU skinning (joint palette in a UBO or a
  texture; dual quaternions optional), skinned bounds, bone attachments.
- [ ] **12.2 Clips and AnimationPlayer.** Compressed clips (quantized rotations, key reduction),
  sampling on jobs, play / crossfade / speed / loop / events, node TRS tracks.
- [ ] **12.3 Blending and state machines.** 1D and 2D blend spaces, layers with masks, additive
  layers, root motion, a state machine with conditional transitions.
- [ ] **12.4 Morph targets.** GPU blend shapes with animated weights.
- [ ] **12.5 IK and constraints.** Two-bone IK, look-at / aim, FABRIK chains.
- [ ] **12.6 Property tracks and tween sequences.** Tracks for material params, colours and
  visibility (any property once 15.1 exists); tween sequences and parallel groups with callbacks.

## Phase 13: Physics
- [ ] **13.1 Jolt (3D).** A physics system on the fixed step; body nodes (static, rigid, kinematic,
  character through CharacterVirtual); shapes (primitives, cooked convex hulls and meshes,
  heightfields); layers and filters; ray / shape / overlap queries; contact signals on the game
  thread; joints; CCD; sleeping; Jolt jobs on our job system; interpolated transforms; a
  cross-platform determinism option; physics materials.
- [ ] **13.2 Box2D v3 (2D).** The same integration for 2D games: bodies, shapes, sensors, joints,
  queries, events; its tasks on our job system.
- [ ] **13.3 Physics tooling.** Debug drawing, a stats panel, profiling zones.

## Phase 14: Apple platforms (needs a Mac; can move up to right after Phase 5)
- [ ] **14.1 Metal and macOS.** The RHI on Metal (MSL from the shader pipeline, memoryless
  attachments, MTLBinaryArchive caching, timestamps, debug groups); a macOS window through GLFW +
  CAMetalLayer; golden images on macOS.
- [ ] **14.2 iOS.** UIKit/UIScene lifecycle; CAMetalLayer + CADisplayLink (ProMotion ranges);
  multi-touch with coalesced touches; safe areas; orientation; AVAudioSession (interruptions, other
  audio); haptics; file system (bundle, Application Support); text input; game controllers; thermal
  state into Quality; the Swift side of the native bridge; an Xcode project from CMake with plist,
  entitlements and privacy manifest.
- [ ] **14.3 Kit on iOS.** Google Mobile Ads + UMP, the App Tracking Transparency prompt, Game Center
  / iCloud for CloudSync.
- [ ] **14.4 CI.** A macOS runner builds macOS and iOS (unsigned) and runs the desktop tests.

## Phase 15: Editor and tools
- [ ] **15.1 Reflection.** Type and property registration (ranges, flags, enums, resource
  references), used by the inspector, serialization and animation tracks.
- [ ] **15.2 Scenes and prefabs.** Text (diffable) and binary formats, GUID references, versioned
  migrations, prefab instances with overrides.
- [ ] **15.3 Editor shell.** A `WLW_EDITOR` desktop build of the game: ImGui docking with multiple
  viewports, saved layouts, play / pause / step / stop with scene snapshot and restore, an editor
  camera.
- [ ] **15.4 Scene editing.** Hierarchy, a reflection-driven inspector, undo/redo, gizmos (ImGuizmo,
  snapping, local/world), GPU ID picking, multi-select, duplicate/delete, prefab editing.
- [ ] **15.5 Assets.** An asset browser over the asset database, thumbnails, drag into the scene,
  import-settings editing, reimport with hot reload.
- [ ] **15.6 Tool panels.** Console, profiler (CPU, GPU, memory, frame graph), render and physics
  debug views, animation preview, particle editor (curves, gradients), material editor, a device
  simulator (resolutions, safe areas, notches).

## Not planned (ask to add)
A scripting language, networking and multiplayer, consoles (NDA platforms), Direct3D 12 (Vulkan
covers Windows), hardware ray tracing / virtualized geometry / dynamic GI, terrain and vegetation,
navigation meshes (Recast/Detour), video playback, XR, in-app purchases.
