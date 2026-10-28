# WLWengine roadmap

The engine is being brought to big-engine quality in architecture, practices and features: mobile
first (Android, iOS, web), with desktop as a full target, and an editor (the Studio) to make the games with, like
Godot's. This plan came out of the October 2026 architecture review and was reordered on 2026-10-21
around the editor and iOS ("Why this order"). Work goes one step at a time, in order. Step ids are
stable: commits name them ("Roadmap 4.3b: ..."). Commits and CHANGELOG entries from before 2026-10-21
use the old numbers: "Old numbering" at the end maps them.

**Current step:** 3.2a Studio shell, engine side (plan in chat first) · **Updated:** 2026-10-28

**Next session:** plan 3.2a in chat. Then, in order:
  3.2b / c, 3.3, 4.1-4.5, 5.1 (scenes edited in the Studio), 5.2-5.6. Threadlock moves to the current engine
  before 5.6 (its 2.8 migration redone on its main). Before Bluzzle's next Play release: tag v0.3.1 from main (D19); it points at an untagged
  commit today.

Where things stand (2026-10-28):
- Done: Phase 0 = v0.1.0 (2026-10-15), Phase 1 = v0.2.0 (2026-10-18), Phase 2 = v0.3.0 (2026-10-24), each step one squashed commit on main named
  "Roadmap x.y: ..." with its details and numbers in CHANGELOG.md (0.8 also has a reformat commit, listed in
  `.git-blame-ignore-revs`). Since v0.2.0, two small additions Threadlock asked for (CHANGELOG 0.3.0):
  `platform::KeepScreenOn`, `ui::TextStyle::rotation`.
- Since v0.3.0 (2026-10-25 to 10-27, 33 commits, CHANGELOG "Unreleased"): features Bluzzle asked for, landed ahead
  of their phases and recorded there as early slices: HDR picture + output pass + bloom + ripples + distortion
  buffer (13.1, 13.7), instance uniforms (9.5), billboards, particle curves and trails (13.11), canvas and text
  styles (15.4), music and live mix (16.2, 16.4), anim tools (17.6), leaderboards (kit; 6.2 for iOS),
  `platform::ReduceMotion` (7.5).
- Games: Bluzzle (live in Play testing; desktop + Android) is on main after v0.3.0 (86c4c3a, untagged). Threadlock
  (Android) is still on c39718e (v0.2.0 + 2): its 2.8 migration has to be redone on its main. Snake (`samples/snake`)
  only has to keep compiling. Epicycles, Silhouette and Ripple are on old
  pre-0.1 commits and are not targets (the owner dropped them).
- How the owner works: the plan goes in chat first; code only after an OK. Work on a branch (`<topic>/setup`, or
  `bench/<topic>` so `bench.yml` runs) pushed to GitHub so CI checks it, then show the result and squash-merge to
  main only on approval. Delete the work branch after. Ask before deleting files, and before using the Android
  emulator (the owner may be playing on the PC and it can crash; a phone is separate).
- CI: GitHub Actions (`.github/workflows/ci.yml`). Every push runs clang-format (check), Windows MSVC Debug, Linux
  clang ASan+UBSan (+ golden images on llvmpipe under xvfb) and Android arm64 (NDK 30, builds engine + Snake,
  WLW_PROFILE on, WLW_PCH off). Windows Release runs only on main and manual runs (private repo: the owner wants
  zero cost; GitHub stops at the free minutes). Markdown-only pushes don't run CI. Warnings are errors in every
  preset.
- Formatting: `cmake -P cmake/ClangFormat.cmake` before committing (pinned clang-format 23.1.3 from its PyPI
  wheel, downloaded into build/tools).
- GitHub CLI: `C:\Program Files\GitHub CLI\gh.exe` (not on the bash PATH: call it by its full path).
  `gh run list/watch/view --log-failed`, `gh run download <id> --name gpu-output`.
- Golden images: a rendering change that changes pictures on purpose gets new references from CI's `gpu-output`
  artifact (llvmpipe), looked at and reviewed like code. They're plain git, not LFS.
- Tests: cases run in parallel processes (no shared temp files); global state (log filters, cvars) is restored per
  case, and test cvars are static with unique "test.<case>.*" names.
- Benchmarks: CLAUDE.md "Benchmarks". Performance steps run them before and after with `--baseline` on desktop +
  Linux CI; phone runs are optional and pinned to one fast core (Pixel 4: `taskset 80`). Known hot spots:
  render/gather 10k = 1.86 ms on the Pixel 4 before 2.2 (world matrices now cached; world boxes still recomputed
  per node per frame: 9.5). 2.2 moved the node transforms into node_3d.cpp (optimized in games' Debug builds).
- Local machine: `build/win-debug` and `build/win-release` from a VS 18 dev shell (`build/windows-debug` is stale).
  The Android preset builds locally with `ANDROID_NDK_HOME=%LOCALAPPDATA%\Android\Sdk\ndk\30.0.16248370`. WSL Ubuntu
  has no compilers: Linux is checked by CI only.
- Open items: typing in the console on phones waits for GameTextInput (D9, 7.1). On desktop NVIDIA the frame's
  first glGet waits for the driver thread: the renderer's per-frame `glGetIntegerv(GL_FRAMEBUFFER_BINDING)` moves
  that wait into our code (9.3's tracked state removes the call). Nobody has profiled on the Pixel 4 with Tracy yet
  (a game's profile build needs the INTERNET permission).

## Why this order (2026-10-21, Studio order revised 2026-10-28)
- **The owner wants an editor, the Studio: an IDE that edits scenes, like the Godot editor; for their own games
  first, maybe a product for other developers later** (decided 2026-10-21; on 2026-10-28: scene editing is the
  goal, Windows first, written in portable C++ so it runs on the Mac too, D20). Phases 3-5 build it; Phase 19 turns
  it into a product if that is still wanted then.
- **Scene editing first, game-specific tools after it (2026-10-28).** The 2026-10-21 order put game plugins first
  (Threadlock's level editor in Phase 3) and generic scene editing last; the owner wants the scene IDE, so Phase 4
  is now only what scene editing needs (reflection, serialization, `res://`, scene files, the inspector) and Phase 5
  edits scenes first (5.1), with resources, prefabs and signals after it and the games' own tools (Threadlock's
  level editor, Bluzzle's tuning) last (5.6), as plugins on the finished Studio. Scene files reference resources by
  path until the resource manager's uids arrive (5.2), which D15's path fallback already allows.
- **The editor needs the scene model, reflection, data files, resources and scenes, not the rendering rewrite.** It
  shows the game through a viewport texture (`Viewport::GetTexture()` in an ImGui panel), so the RHI, Vulkan and
  Metal (Phases 9-11) change one spot in it later. Phase 2 comes first because the editor shows and edits exactly
  what it changes (names, children order, node types, the platform-driven frame).
- **Editor features come in the order the games use them.** Research (2026-10-21): small engines that built a
  generic editor first (Hazel: docking, hierarchy, inspector, gizmos, then play/stop over two years; no undo in the
  public repo) spent years before a game used it; Godot's and Unity's value for a game is in game-specific plugins
  (EditorPlugin, EditorWindow, custom inspectors). The lesson kept from it: each Studio step ships something
  usable (the shell runs the game with Play / Pause / Launch; the inspector edits live values before the scene
  editor exists), undo comes from the start, and plugins stay first-class: the scene editor itself is a plugin,
  so Threadlock's level editor (72 JSON levels, ~35 to redesign, edited by hand as coordinates today) and Bluzzle's
  tuning (69 hand-written ImGui widgets, none saved) land on the same API (5.6).
- **iOS roughly doubles the market of a mobile puzzle game.** The owner has or will get a Mac (2026-10-21), and
  iOS can ship on today's GLES renderer, as Godot's Compatibility renderer does (D17), so it no longer waits for
  Metal (Phase 11). It can move ahead of
  Phases 3-5 if the Mac arrives first and Bluzzle wants iOS before the Studio. The Studio on the Mac (6.5) comes
  with it: same Mac, and the engine's desktop GL path already asks macOS for its core profile.
- **Android platform work (GameActivity, thermal, device lab) helps the live game now**, so it moves ahead of the
  rendering rewrite; its Vulkan parts stay with Vulkan (Phase 10).
- **Everything else stays planned in full** (the owner's decision of 2026-10-14: big-engine quality, Vulkan
  included), in the same relative order as before, after the work that ships games. Editor panels that belong to a
  feature (frame graph view, import settings, material and particle editors, animation timeline, physics shapes)
  come with that feature.

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
  phone is at hand, never a blocker. Firebase Test Lab (7.6) brings a device matrix later.
- Studio work (from Phase 3): the Studio targets build in CI on Windows and Linux (Linux proves the code isn't
  Windows-only; macOS by hand from 6.5); no OS header outside `platform/` (D20); Studio logic (documents, undo,
  reflection, serialization, commands) is tested without a window; file formats have round-trip tests and a corpus
  of old files that must keep loading.
- Bluzzle (desktop and Android) and Threadlock (Android) build and run with the change; Snake
  compiles. iOS joins the list once Phase 6 has run it.
- CLAUDE.md and CHANGELOG.md updated; breaking changes come with migration notes.
- Whatever a step replaces is deleted within the same phase (file deletions confirmed with the
  owner first). Until a game ships, renamed APIs get no deprecation shim (CLAUDE.md "Versioning"): the
  change is breaking, with migration notes.

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
- Every Studio feature names the game that uses it first. The Studio runs the game's own code: no Studio-only
  gameplay paths, and what plays in the editor plays the same in the shipped game.

## Decisions (confirmed when their step starts)
- **D1 Math:** glm is the only math library, behind `wlw` aliases (`Vec3`, `Quat`, `Mat4`...).
  Radians, meters, right-handed, **Z up, +Y forward, +X right** (revised 2026-10-23 in 2.4, the owner's choice; was
  Y up, -Z forward): like Blender, O3DE and CryEngine, so a level reads like a map (x, y on the ground, z = height).
  Y-up data (glTF, cube maps) is turned at the door; only GL's view space stays -Z (Camera3D's view matrix).
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
  Refined in 2.1: a `Color` holds the values as authored (sRGB-encoded, like Unity and Godot); the renderer
  converts them to linear when it shades in linear space (13.1). Unreal stores linear instead.
- **D9 Android:** GameActivity + AGDK (Swappy, GameTextInput, Paddleboat) + ADPF.
- **D10 Physics:** Jolt (3D), Box2D v3 (2D).
- **D11 Text:** FreeType + HarfBuzz + SheenBidi + msdfgen + libunibreak.
- **D12 Studio (revised 2026-10-21; named and widened 2026-10-28, confirmed):** the editor is called the Studio. One
  library, `wlw::studio`, statically linked into two kinds of executable: `wlw_studio` (engine types only: opens any
  project folder and edits its scenes, like a plain Godot editor) and, from `wlw_add_game(... STUDIO_SOURCES ...)`,
  `<game>_studio` = the same Studio + the game's own code (its node types, systems and Play), like Unreal's Editor
  target and Fyrox's editor crate; `WLW_STUDIO` defined. Dear ImGui's docking branch (multi-viewports off
  until Wayland can place windows). Two ways to play: in the Studio (on a copy of what is being edited) and Launch
  (the real game as its own process, on the desktop or a phone: the ground truth, and a crash can't take the editor
  down). No game DLLs and no engine shared library until Phase 19: they need one CRT, one copy of every engine
  global (glad's GL pointers, cvars, log sinks, resource lists, the shader library) and a stable ABI, and C++ hot
  reload is fragile (Godot's GDExtension reload and Fyrox's dylib reload both warn about it). Code changes mean a
  fast rebuild that restores the Studio's session; MSVC Hot Reload or Live++ cover small edits.
- **D13 Dependencies:** vendored, pinned sources in `third_party/`, built by our CMake on every
  platform (versions and licences in `third_party/README.md`); vcpkg only for desktop-only GLFW.
- **D14 Reflection (2026-10-21):** hand-written registration, no code generator (Godot ClassDB / O3DE
  SerializeContext style): a `Reflect(ClassBuilder<T>&)` function per type adds properties as member pointers or
  getter/setter pairs, with attributes as small structural types (`Range{min, max, step}`, hints) and flags
  (Storage, Editor, ReadOnly, NoUndo). Registration is called explicitly (engine types from Application, game
  types from `MakeApp`): a static library drops self-registering globals. C++26 reflection (P2996) can fill the
  same tables later, the attribute types becoming annotations; today only GCC 16 has it (MSVC 19.51 and NDK r30's
  clang 21 reject it, checked 2026-10-21).
- **D15 Scene and data files (2026-10-21):** JSON (nlohmann) as the text form, CBOR of the same tree as the
  binary form. Only non-default values, deterministic order, shortest round-trip floats, small vectors on one
  line. A random 64-bit id per node (unique in its scene) and per resource (uid); references store ids, resource
  references `{uid, path}` with the path as a fallback. Prefab instances store overrides as (target id chain,
  property, value). A file format number plus per-class versions, upgraded by functions run on the JSON before
  loading. A custom Godot-like text format only if JSON's diffs hurt in practice.
- **D16 Undo (2026-10-21):** one command stack per open document. Commands name their targets by id and property
  and keep old and new values as serialized JSON, never pointers, so they survive delete and re-create. Commands
  inside one gesture (mouse down to up) merge; each records the selection without marking the document unsaved;
  a version counter drives the unsaved mark. Playing in the editor runs on a copy, with undo off.
- **D17 iOS renderer (2026-10-21):** iOS ships on the current renderer through native OpenGL ES 3.0 (EAGLContext
  + CAEAGLLayer), as Godot's Compatibility renderer does on iOS today. Apple deprecated it in iOS 12 but hasn't
  removed it (it runs on iOS 26; no App Store rule bans it); it is frozen at ES 3.0 with no driver fixes, and any GL
  call after the app enters the background gets it killed. The GL surface sits behind a small interface so ANGLE's
  Metal backend (EGL, the Android code path; Safari's WebGL runs on it) can be a CMake option if Apple announces a
  removal or native bugs show; reports of ANGLE in games are mixed (jMonkeyEngine 2026: 14 fps vs 47 native; Godot
  rolled it back on macOS in 4.2.1). Native GLES doesn't run in the Apple-silicon Simulator (crashes since Xcode
  15.4), so a real iPhone (A13 or later) is needed. Metal replaces it with the RHI (11.3).
- **D18 Keys (2026-10-28, confirmed):** `input::Key` names a key's position (named by the US layout), like
  Unity's Input System keys, SDL scancodes and Godot's physical keycodes, so game controls stay put on AZERTY or
  Dvorak (WASD is where WASD is); GLFW already reports positions. Shortcuts use the key's label on the player's
  layout instead (Ctrl+Z = the key printed Z), as Dear ImGui's own GLFW backend does: the platform reports both, ImGui
  and `WasShortcutPressed` get the layout's key. Typed text stays `Input::Text()` (code points).
- **D19 Patch releases (2026-10-28, confirmed):** a patch (x.y.z+1) from `main` may carry fixes and additions a
  game needs to ship, as long as nothing breaks for existing callers; anything breaking waits for the phase's
  minor version. Games ship only on tags, so a game shipping between phases gets a patch tag.
- **D20 A portable Studio (2026-10-28, the owner's ask, confirmed):** Windows first, the Mac next, from the same C++. Rules:
  - All Studio code is portable C++20 in `studio/`: no OS or compiler-specific headers (no `<windows.h>`, no
    Objective-C), only the engine, Dear ImGui, the standard library and vendored portable libraries.
  - All Studio UI is Dear ImGui, menus included (no native menu bar or widgets), so it looks and behaves the same
    on both systems; windows, clipboard, cursors and HiDPI content scale come from GLFW.
  - The few OS services live in `engine/wlw/platform/<os>/` behind small interfaces, each with a Windows and a
    macOS implementation from the start (Linux too, for CI): file dialogs (nativefiledialog-extended, which ships all
    three), the file watcher (ReadDirectoryChangesW / FSEvents / inotify), starting processes and reading their
    output (CreateProcessW / posix_spawn), "show in Explorer / Finder", the config folder (%APPDATA% / ~/Library/
    Application Support). The macOS ones may be Objective-C++ (.mm) files there and nowhere else.
  - Paths are `std::filesystem` with UTF-8 strings; wide Windows strings only inside `platform/win`.
  - Shortcuts use `Modifiers::Primary` (Ctrl / Cmd, 3.1) and are shown per platform.
  - Rendering: the engine's GL 3.3 core path, which macOS runs today (a forward-compatible core context, GL 4.1 at
    most; deprecated by Apple but present on Apple silicon), so the Mac Studio doesn't wait for Metal (11); Metal
    replaces it later through the RHI with no Studio change.
  - CI: Linux builds and tests the Studio on every push (the proof nothing Windows-only crept in); a macOS build job
    by hand (Constraints: macOS minutes cost).

## New third-party libraries (all permissive)
doctest (MIT), nanobench (MIT), Tracy (BSD-3), volk (MIT), Vulkan-Headers (Apache-2.0), Vulkan
Memory Allocator (MIT), glslang (BSD-3), SPIRV-Tools (Apache-2.0), SPIRV-Cross (Apache-2.0), AGDK
Swappy / GameActivity / GameTextInput / Paddleboat (Apache-2.0), zstd (BSD), cgltf (MIT),
meshoptimizer (MIT), MikkTSpace (zlib), Basis Universal (Apache-2.0), astc-encoder (Apache-2.0),
tinyexr (BSD-3), FreeType (FTL: credit required), HarfBuzz (MIT), SheenBidi (Apache-2.0),
libunibreak (zlib), msdfgen (MIT), Jolt (MIT), Box2D v3 (MIT), ImGuizmo (MIT), FidelityFX FSR 1
(MIT). Editor (2026-10-21): Dear ImGui's docking branch (MIT; replaces the vendored master of the same version),
nativefiledialog-extended (zlib) for native file dialogs. Not permissive, ask first: ImGui Test Engine (its own
licence) for editor UI tests.

## Constraints
- Metal, macOS and iOS need a Mac (and an iPhone; the Apple Developer Program to ship). The owner has or will get
  one (2026-10-21); until then CI can only compile them.
- macOS CI runners cost minutes on a private repo (the owner wants zero CI cost): $0.062 a minute after the free
  minutes (2026; check the plan's macOS multiplier), so iOS and macOS jobs run only on main or by hand, and
  day-to-day iOS builds happen on the Mac. The Apple Developer Program is $99 a year. Without a Mac of one's own, a
  cloud Mac (Scaleway M1 from EUR 0.11 an hour, 24-hour minimum) covers signing setup and Instruments.
- Vulkan on Windows: the Vulkan SDK installed (validation layers, tools).
- Devices: development isn't tied to any phone the owner has. Phone checks use whatever Android
  device is at hand (a Pixel 4 / Adreno 640 was used so far); Firebase Test Lab (7.6) for a matrix
  (Adreno, Mali, low end) without owning them.
- Bluzzle is live in testing: its releases pin a tagged engine version (from v0.1.0; one minor
  version per phase, CLAUDE.md "Versioning").
- The Studio is desktop-only: Windows first, Linux in CI, macOS from 6.5 on the GL path (D20), Metal later (11).
  Threadlock's rule "desktop builds are only the engine's dev target" gets an exception for its Studio build (in
  Threadlock's repo).

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

## Phase 1: Quick mobile wins on the current renderer (released as v0.2.0, 2026-10-18)
- [x] **1.1 Frame pacing.** *(done 2026-10-15, commit "Roadmap 1.1: frame pacing"; Swappy 2.3.0-alpha01 vendored (AGDK archived its repo in 2025); default 60 fps; auto swap interval off; desktop 240 Hz: 60 fps sd 0.12 ms, limiter 100 fps sd < 0.02 ms)* `AppConfig::target_fps` (30/60/90/120/display) with a runtime setter.
  Android: Swappy (GL) for pacing and refresh-rate choice, `ANativeWindow_setFrameRate` as fallback.
  Desktop: vsync plus a precise limiter. Quality's budget follows the target.
- [x] **1.2 Render scale.** *(done 2026-10-15, commit "Roadmap 1.2: render scale"; 0.25-2, Picture layers follow the scale, post pass sizes stay relative to the viewport (constant look), upscale free with passes / one copy without; cvar r.render_scale; RTX 5080 2560x1440: 3D 2.20 ms at 1, 1.21 at 0.75, 0.53 at 0.5)* `Viewport::SetRenderScale` for the 3D picture (UI stays native),
  bilinear upscale, a Quality hook, shown in the engine panel.
- [x] **1.3 Fragment precision.** *(done 2026-10-15, commits "Roadmap 1.3a: fragment precision" + "Roadmap 1.3b: canvas shader"; decision changed by
  the Pixel 4 measurements: GLES stays highp by default, shaders opt in to `mediump` where a phone measures a gain)*
  Explicit precisions: `highp` for positions, UVs, pixel and canvas coordinates, depth and shadow
  coordinates and the specular term; every uniform, block member and varying names its precision (GLSL ES needs both
  stages to agree); sampler defaults for every sampler type; game shaders highp unless they opt in; cvar `r.mediump`; a
  GLSL ES compile check in CI through a desktop ES context; Mali Offline Compiler numbers, GPU time on a real phone
  when one is at hand (or Test Lab). Split: **1.3a** precision *(done 2026-10-15, commit "Roadmap 1.3a: fragment precision")*, **1.3b** canvas shader *(done 2026-10-15, commit "Roadmap 1.3b: canvas shader"; Pixel 4 Threadlock canvas 4.96 -> 2.30 ms)* (each pixel computes only its
  own quad kind).
- [x] **1.4 Cofactor normals.** *(done 2026-10-16, commit "Roadmap 1.4: cofactor normals"; render/cull 10k 54.7 -> 39.5 us; Mali-G78 instanced mesh vertex 6.50 -> 2.40 arithmetic cycles, 64 -> 32 registers)* Replace the per-vertex `inverse()` and the CPU inverse; correct for
  mirrored transforms; tested against inverse-transpose.
- [x] **1.5 Skybox after opaques.** *(done 2026-10-16, commit "Roadmap 1.5: skybox after opaques"; one full-screen triangle with rays from `SkyRayMatrix`, LEQUAL around the sky draw only, sky first without depth; RTX 5080 1440p, 89% covered: sky 0.013 -> 0.001 ms; Mali-G78 sky vertex 0.78 -> 0.47 cycles)* Depth test `LEQUAL`; golden image. (From 1.4: `skybox.vert` multiplies
  `uProjection * mat4(mat3(uView))` per vertex: one matrix-vector chain, or the product on the CPU, instead.)
- [x] **1.6 Instances.** *(done 2026-10-16, commit "Roadmap 1.6: instances"; fx/particles instances 5k 315 -> 120 us, render/gather 10k 659 -> 403 us, 52 B per copy; Mali-G78/G76 instanced vertex load/store -1 cycle at full occupancy)* 52-byte instances (3x4 matrix + RGBA8), instance bounds cached on change,
  a `WriteInstances(count)` span API; Bluzzle's particles use it (with its move to v0.2.0).

## Phase 2: Core API (breaking, while the games are small; released as v0.3.0, 2026-10-24)
The editor shows and edits what this phase defines, so it comes first.
- [x] **2.1 Math, colour, geometry.** *(done 2026-10-23, commit "Roadmap 2.1: math, colour, geometry"; benchmarks within noise of main; GLM_FORCE_CTOR_INIT measured +21-30% on nested transforms, left off)* `wlw/core/math.h` (glm configuration, aliases, helpers);
  `core::Vector2/3` replaced (0.3 shims). `Color`: RGBA as authored (sRGB-encoded; see D8), hex and text, exact sRGB
  conversions, operators, `Lerp`, `WithAlpha`, `Color32`. Geometry (AABB, Sphere, Ray, Plane, Frustum, Rect) with
  `Overlaps` and `Raycast` in namespace `wlw`.
- [x] **2.2 Transform and Node3D.** *(done 2026-10-23, commit "Roadmap 2.2: transform and Node3D"; Windows A/B vs main: render/gather 10k -54%, transform/move flat 10k -71%, read nested 10k -87%)* Position + quaternion + scale; local and world matrices cached
  with dirty propagation; children in a vector (a stable order: the hierarchy panel and scene files show it);
  `Reparent(keep_world)`, `LookAt`, `Rotate`, world getters and setters, Euler helpers; names, `FindChild` and
  `GetNode(path)`; a 64-bit id per node, unique in its scene (scene files, undo and node references use it: D15);
  a render layer mask. The `Node<T>` template, Node2D, Vertex2D and Triangle2D are removed; Scene gets a root node.
  Hot transform code moves into .cpp files.
- [x] **2.3 Node types.** *(done 2026-10-23, commit "Roadmap 2.3: node types"; Node3D 448 -> 280 B, lights 496 -> 328 B; gather switches on a 1-byte NodeKind instead of 2 virtual calls per node; Windows A/B: gather 10k -4%, the rest unchanged)* Node3D keeps only transform, hierarchy, visibility and layers. New:
  MeshInstance3D, MultiMeshInstance3D, SpriteBatch, DirectionalLight3D, PointLight3D, SpotLight3D,
  Camera3D (perspective and orthographic with fit modes, `ScreenPointToRay`, `WorldToScreen`,
  frustum). Fly, orbit and follow camera controllers (the editor camera reuses fly and orbit).
- [x] **2.4 Conventions.** *(done 2026-10-23, commit "Roadmap 2.4: conventions"; Z up, +Y forward (D1 revised); every golden image unchanged by turning the test scenes; two primitive winding bugs found)* Z up, right-handed, +Y forward, meters, radians; the default camera
  follows them; documented and tested.
- [x] **2.5 API consistency.** *(done 2026-10-23, commit "Roadmap 2.5: API consistency"; the owner chose
  `unique_ptr` from every factory; resources register in their constructor; `core::Connection` for callbacks; every
  golden image unchanged)* `GetScreen()` -> `MainViewport()`; every `Create()` returns
  `unique_ptr`; const-ref getters; removal by object instead of int ids; renderer-only methods
  behind an internal accessor; Screens get `App()`; AppConfig grouped (window, graphics, audio).
- [x] **2.6 Legacy removal.** *(done 2026-10-24, commit "Roadmap 2.6: legacy removal"; `Application` owns the device + renderer; 8 files deleted (wlw.h/.cpp, TextureManager, the 4 shim headers, GEMINI.md) and every 0.2/0.3 shim; also `AttachWindow`'s context sharing, `RenderingDriver::GetDevice`; every golden image unchanged)* `WLWEngine`, `RenderingDriver::SetViewport/Clear/AttachWindow`,
  `Window::SetIsActive`, TextureManager, GEMINI.md (FPSCamera, FollowCamera and
  `Window::ProcessMouseMovement` went in 2.3)
  (each deletion confirmed with the owner).
- [x] **2.7 Platform-driven frame.** *(done 2026-10-24, commit "Roadmap 2.7: platform-driven frame";
  `Application::Internal().Init/Frame/Shutdown` with lifecycle checks, `Quit(code)`, `app::FrameClock` with tests,
  counted `platform::Init`, `WindowConfig::visible`, `wlw_app_tests` on a hidden window; Application keeps owning
  the window (the editor's Restart decides in 3.2); no headless app until the null RHI backend (9.1))*
  `Application::Init/Frame/Shutdown`; `Run()` loops on desktop
  and Android; `WLW_MAIN` unchanged. The editor (3.2) drives frames through it, and the shape must also fit iOS
  (6.1: a game thread like `android_main`, or `Frame()` from CADisplayLink callbacks).
- [x] **2.8 Migrate the games.** *(done 2026-10-24, commit "Roadmap 2.8: migrate the games"; Bluzzle's world turned
  to Z up (no Y-up root node), Threadlock and Snake moved; no engine code changed)* Bluzzle, Threadlock and Snake
  updated; a short note for the dropped games (Epicycles, Silhouette, Ripple) in the CHANGELOG.

## Phase 3: Studio shell
The Studio (D12, D20) is the engine's editor: an IDE for scenes, like the Godot editor or Unity's, Windows first and
written in portable C++ so it moves to the Mac without a rewrite (6.5). Code is still written in Visual Studio /
Xcode; the Studio edits scenes and data. This phase builds the window, the panels, the play controls and undo.
- [x] **3.1 Keys and mouse for tools.** (done 2026-10-28; also `IntlBackslash`, the ISO key, and an Input section in
  the engine panel for the hand checks; checked on a real Windows window driven by SendInput (every cursor shape, the
  lock, buttons, wheel, Ctrl+A / X / V / Z / Shift+Z in an ImGui field) and Bluzzle on the Android emulator (keys, wheel); `Input::HeldModifiers()` instead of `Modifiers()`, a name the type already has) The full key set (letters, digits, function keys, punctuation, numpad,
  modifiers as keys and as a mask), every mouse button, the wheel (both axes), cursor shapes; Dear ImGui fed all of
  them (Ctrl+Z, Ctrl+S, right-drag, wheel zoom work). The polling API stays; timestamped events, several pointers
  and gamepads stay in 14.1.
  Found in the code (2026-10-28): `input::Key` has 26 keys (letters W A S D I P only, F1 only, one `Control` and one
  `Shift`); the mouse is only the left button as "the pointer" (no right / middle, no wheel, no movement delta);
  no cursor API; the debug UI feeds ImGui only the letter A (Ctrl+A works in the console, Ctrl+C / V / Z don't).
  The games use only the pointer, taps and swipes, which stay. Plan:
  - Keys (D18): A-Z, `Digit0-9`, F1-F12, punctuation (named by the US layout), Insert, the locks, PrintScreen, Pause,
    Menu, the numpad, left / right Shift, Control, Alt and Super (replacing `Key::Control` / `Key::Shift`: Breaking,
    only the debug UI used them). `Input::Modifiers()` = a mask from the held keys, with `Primary` = Ctrl on Windows
    and Linux, Cmd (Super) on macOS, so Studio shortcuts are written once (`Primary + S` = Ctrl+S / Cmd+S);
    `WasShortcutPressed(key, mods)` (exact mask: Ctrl+Z is not Ctrl+Shift+Z; matched on the layout's key);
    `KeyName` / `KeyFromName` (menus show "Ctrl+S" or "Cmd+S"; 14.3's saved bindings).
  - Mouse: `MouseButton` {Left, Right, Middle, Back, Forward} down / pressed / released (a real mouse only; the
    pointer API is unchanged and stays the left button or the first finger), `PointerDelta()` (movement this frame),
    `ScrollDelta()` (both axes, in notches, fractions from trackpads; +y = away from the player). Blocking (the debug
    UI has the mouse) hides buttons, wheel and delta too.
  - Window: `SetCursor(Cursor)` {Arrow, TextInput, Hand, Crosshair, ResizeEW / NS / NWSE / NESW, ResizeAll,
    NotAllowed, Hidden} (GLFW 3.4's standard cursors, the same on Windows and macOS; phones ignore it); the debug UI's
    cursor wins only while it has the mouse (`Internal()` override). `SetCursorLocked(bool)` (hidden, unbounded
    `PointerDelta`, raw motion where the OS has it): pulled from 14.1 because 5.1's fly camera (right-drag look)
    needs it. `HasFocus()`.
  - Dear ImGui fed everything: every key (letters by the layout), modifiers (`ConfigMacOSXBehaviors` on macOS),
    five buttons in order (a click inside one frame still lands), both wheel axes, focus; its cursor shape goes back
    to the window.
  - Android: the full keyboard map (Bluetooth keyboards, Chromebooks, the emulator) and the mouse wheel
    (`AMOTION_EVENT_ACTION_SCROLL`); mouse buttons wait for 7.1 (GameActivity rewrites Android input).
  - Tests (no window): every key and button pressed + released in one frame, OS repeat ignored, the wheel summed per
    frame and reset, modifiers, `Primary` per platform and exact shortcuts (layout keys too), blocking, `KeyName`
    round trip for every key, every key's own distinct ImGui key. Cursor shapes and lock checked by hand.
- [ ] **3.2 Studio shell.** `wlw::studio` (`studio/wlw/studio/`: a desktop-only library on top of the engine, like
  kit; the runtime never includes it), built into two executables from the same code (D12): `wlw_studio` (the
  engine's node types; opens any project folder) and, through `wlw_add_game(... STUDIO_SOURCES ...)`,
  `<game>_studio` (the same plus the game's own types, systems and Play). Dear ImGui on its docking branch of the
  same version (v1.92.9b-docking; 1.93's draw-list changes are a separate update): all Studio UI, menus included,
  is ImGui (no native menus or widgets: the same on Windows and macOS); layouts saved per project in the user's
  folder; HiDPI from the window's content scale (Windows scaling, Retina). Panels: Game (the game's screen drawn
  into a viewport texture at a chosen device size: phone presets, orientation, safe areas and notches = the device
  simulator's first part; the game's `Input` fed from the panel: pointer in the panel image's coordinates, keys
  only while the panel has focus), the engine panel, cvars, console and log, docked. The Studio owns the frame
  (2.7): Play / Pause / Step (one fixed step) / Stop; Launch runs the real game (the desktop executable with `+cvar`
  arguments, or the APK on a phone through adb with the intent extra). Native file dialogs. The Studio reopens its
  last session (layout, open scenes, camera) so a rebuild costs seconds.
  Engine changes it needs (found in the code 2026-10-28):
  - `Window::MainViewport()` is always the screen (`Viewport::CreateScreen`): it must be able to draw into a texture
    at a size of its own (the device size), with the real window drawing only the Studio.
  - The game's surface comes from the platform: `Input::SurfaceSize`, `platform::GetSafeAreaInsets()` (read by
    Application for `UIContext::safe_area`) and the UI reference fit follow the window. They move behind the Window
    (or an override on it) so the Studio can simulate a device's size, insets and touch.
  - `Application::Frame()` always updates the game: Pause needs frames that draw (Studio, Game panel) without
    updating systems and screens, and Step = one fixed step. `Quit()` and Escape-as-Back (Application closes the app
    when nothing handles Back) must stop the play session in the Studio, not close it.
  - One ImGui context: `DebugUI` creates it with no ini file and no docking, and draws the in-game toggle button.
    The Studio takes it over (docking, an ini per project, no toggle button); a Studio build forces `WLW_DEBUG_UI` on.
  - `wlw_add_game` takes only `SOURCES` / `ASSETS`: `STUDIO_SOURCES` adds the `<game>_studio` target (desktop only).
  - OS services the Studio needs, in `platform/` behind small interfaces (D20): native open / save / folder dialogs
    (nativefiledialog-extended: Windows IFileDialog, macOS NSOpenPanel, Linux portals), starting a process and
    reading its output (Launch, adb), "show in Explorer / Finder", the user's config folder.
  Sub-steps: **3.2a** engine side (offscreen main viewport, simulated surface and insets, paused frames, Quit in the
  Studio; tested with `wlw_app_tests`), **3.2b** the Studio library, both executables, docking and panels, **3.2c**
  Launch (desktop + adb), file dialogs, session restore. CI builds the Studio on Windows and Linux (Linux = the
  cheap proof that nothing is Windows-only).
- [ ] **3.3 Documents, undo and plugins.** Documents: open, save, save as, autosave, the unsaved mark, several open
  at once, reload when the file changes on disk. Undo/redo from the start (D16): one command stack per document,
  merging within a gesture (mouse down to up), an Edit menu with `Primary+Z` / `Primary+Shift+Z`, a history panel;
  the first command type keeps a snapshot of the document before and after. `StudioPlugin` (Godot's EditorPlugin,
  Unity's EditorWindow): a game's Studio build registers panels, menu items, toolbar buttons, tools drawn over a
  view and its own document types; the Studio's own scene editing (5.1) is built the same way. A file watcher on
  the project's folder reloads changed JSON, shaders and textures in the running game (the first piece of hot
  reload); OS code, so in `platform/` (Windows ReadDirectoryChangesW, macOS FSEvents, Linux inotify, a polling
  fallback) behind an interface the tests can fake.

## Phase 4: Scenes as data
What the Studio edits: reflected properties, scene files and the inspector. Done when a scene file made by hand
loads in a game and its nodes can be edited in the inspector.
- [ ] **4.1 Reflection.** D14: classes and plain structs register their properties (member pointers or
  getter/setter pairs; enums with names; base classes); attributes `Range`, hints (colour, angle, multiline, file,
  resource type), flags Storage / Editor / ReadOnly / NoUndo; `Version(n)` and `RenamedFrom`; types created by a
  stable name (scene files; `TypeName<T>()` isn't stable). Engine types (every node type, lights, cameras,
  materials, post settings) registered from Application, game types from `MakeApp`.
- [ ] **4.2 Serialization and data files.** D15: any reflected value to and from JSON and CBOR (nlohmann moves from
  kit into the engine); non-default values only, shortest round-trip floats (`std::to_chars`; nlohmann alone
  prints 0.1f as 0.10000000149011612); file format numbers, per-class versions and upgrade functions; unknown
  properties kept with a warning; a corpus of old files in tests/data. Data files: a reflected struct saved as its
  own file (Godot's custom Resource, Unity's ScriptableObject) for tuning, looks and settings.
- [ ] **4.3 File system and async IO.** Mounts `res://` (the project folder on desktop, the APK, the iOS bundle;
  paks in 12.6), `user://`, `cache://`; directory listing; the Studio writes into the project folder; the file
  watcher from 3.3 behind it; sync and async reads completed on the game thread; UTF-8 paths everywhere (wide
  Windows APIs only inside `platform/`); replaces `platform::ReadFile`. (This also lets glTF load from the APK.)
- [ ] **4.4 Scene files.** Scene files (D15): a flat node list in tree order (id, parent, name, type, non-default
  properties), node references by id; resources referenced by `res://` path for now (built-in primitives as
  `builtin://cube` etc.; uids join in 5.2, with the path kept as the fallback D15 already names); migrations; games
  load and instantiate scene files at runtime (`Scene::Load`, `Instantiate` under a node). Prefabs come in 5.3.
- [ ] **4.5 Inspector.** A reflection-driven panel: widgets per property type (numbers, ranges, bools, enums,
  colours, vectors, strings, arrays, resource and node references), custom property editors from plugins (Godot's
  EditorInspectorPlugin), each change a SetProperty command (D16), applied live to the running game. Also on data
  files and on any reflected object a game shows (Bluzzle's tuning structs: 5.6).

## Phase 5: Scene editor
The Studio edits scenes, like the Godot editor's 3D view. First users: Bluzzle's static stage (lights, floor,
camera) as a scene file, and the next games.
- [ ] **5.1 Scene editing.** Scene documents (open, save, several at once); a hierarchy (drag to reparent, rename,
  multi-select, filter, add node by type); the inspector on nodes; a Scene panel with a Studio camera (orbit, fly
  with the locked cursor from 3.1, frame the selection) beside the Game panel; a grid and axis gizmo (Z up, D1);
  ImGuizmo gizmos (translate, rotate, scale, snapping, local / world); picking (rays against world boxes, then GPU
  id picking); duplicate, delete, copy and paste; all undoable (create and delete subtree, reparent). Play: the
  scene is serialized and the game runs on a copy; Stop throws the copy away (a change made while playing can be
  copied back as one undoable command).
- [ ] **5.2 Resources and asset browser.** Resource manager: typed ref-counted handles; a uid per resource (D15: in
  its own file's header, or a `.uid` sidecar for PNG / glTF / OGG), a uid -> path table baked for the APK and the
  bundle; sync and async loading with states and dependencies; a cache by path; unload on last release; a per-frame
  GPU upload budget; source tracking to rebuild after device loss; hot reload in the Studio. Asset browser: the
  `res://` tree and search, thumbnails (images; models and scenes rendered offscreen), drag into the scene or onto a
  property, open scenes and data files, rename and move without breaking uids. Import settings and reimport arrive
  with the cooker (12.8).
- [ ] **5.3 Prefabs.** Prefab instances with overrides (target id chain, property, value; added and removed nodes),
  variants (a scene whose root is an instance), nesting. In the Studio: open a prefab; instances show their
  overrides, which can be reverted or applied to the source.
- [ ] **5.4 Signals.** `Signal<void(Args...)>` with scoped connections; queued signals delivered on the game
  thread; a property-changed signal for inspected objects (games react to live edits); connections saved in scene
  files (Godot's signal connections) when a game needs them.
- [ ] **5.5 Tool panels.** Profiler panel (CPU zones of the frame, GpuTimer sections; Tracy for depth), render
  stats, the device simulator completed (a device list with real resolutions, densities, safe areas and notches;
  orientation; the quality level), the log with filters. Studio UI smoke tests if ImGui Test Engine's licence fits.
- [ ] **5.6 Game tools on the Studio.** Bluzzle's rules and tuning structs move to the inspector and data files
  (its 69 hand-written sliders go). Threadlock's level editor as a `StudioPlugin` (planned and built in Threadlock's
  repo; this step is the engine fixes it needs): the level drawn by the game's own board view, nodes and pipe bends
  dragged with snapping, a palette of pieces, the validators and the solver run live (pars, timing), "play this
  level" and Launch on the phone, saved through the game's level writer. Needs Threadlock on the current engine
  (its 2.8 migration redone on its main).

## Phase 6: iOS (needs a Mac and an iPhone)
On the current GL renderer (D17); Metal comes with the RHI (11.3). Can move ahead of Phase 3 if the Mac arrives
first and Bluzzle wants iOS before the editor. Estimated size: 1,000-1,500 lines of Objective-C++ (Android's layer
is ~850 lines of C++ + ~225 of Kotlin).
- [ ] **6.1 iOS platform.** `platform/ios/`: UIKit / UIScene lifecycle; the loop chosen first (the game loop on its
  own thread, as `android_main` does, or `Frame()` (2.7) from CADisplayLink callbacks); the EAGL surface behind the
  D17 interface (glFinish before the app goes to the background); CADisplayLink `preferredFrameRateRange` for frame
  pacing (Swappy's job on Android; 120 Hz on iPhones needs `CADisableMinimumFrameDurationOnPhone`, Low Power Mode
  caps at 60); multi-touch, `safeAreaInsets`, `nativeScale`, orientation; audio through miniaudio's Core Audio
  backend with an ambient session (mixes with the player's music), interruptions and
  `secondaryAudioShouldBeSilencedHint` (what AudioWatcher.kt does on Android); assets from the bundle (plain file
  reads: glTF loads there), saves in Application Support; `idleTimerDisabled` = KeepScreenOn; haptics; thermal state
  into Quality.
- [ ] **6.2 Native bridge and kit on iOS.** The Swift side of `BridgeCall`; Google Mobile Ads + UMP, the App
  Tracking Transparency prompt; Game Center / iCloud for CloudSync; Game Center leaderboards behind
  `kit::Leaderboards`.
- [ ] **6.3 Build and ship.** An Xcode project from CMake (`-G Xcode -DCMAKE_SYSTEM_NAME=iOS`, the signing team as
  an Xcode attribute) with Info.plist, launch screen, asset catalog, entitlements and the privacy manifest
  (PrivacyInfo.xcprivacy); App Store uploads need Xcode 26 and the iOS 26 SDK (since 2026-04-28; the iOS 27 SDK from
  April 2027) and iOS 13 or later as the minimum; TestFlight; Bluzzle on an iPhone.
- [ ] **6.4 CI.** An `ios-arm64` build-only job on a GitHub macOS runner (like `android-arm64`), on main and by hand
  only (Constraints).
- [ ] **6.5 The Studio on macOS.** The Studio (Phases 3-5) built and run on the Mac on the GL path (D20): the
  macOS implementations of the platform services (FSEvents watcher, NSOpenPanel dialogs, posix_spawn, Finder) checked
  for real, Cmd shortcuts, Retina scale, trackpad scrolling, a `.app` bundle from CMake; the desktop golden images on
  macOS's GL; a `macos` build job (by hand). Small by design: everything else is the same C++ as on Windows.

## Phase 7: Android platform
- [ ] **7.1 GameActivity.** AGDK GameActivity replaces NativeActivity (native glue, `WlwActivity`);
  input from its buffers (all pointers, history, timestamps); insets without JNI reflection; GameTextInput (the
  console can be typed into on phones); the games' Gradle files updated.
- [ ] **7.2 Refresh rate.** Swappy (GL; Vulkan in 10.5); the refresh rate chosen per screen (menus vs
  gameplay); checked with frame-time graphs.
- [ ] **7.3 ADPF and thermal.** Performance hint sessions for the game and render threads, thermal
  headroom forecasts feeding Quality (step down before throttling), the Game Mode API, low-power
  reactions.
- [ ] **7.4 Context loss.** Recovery from `EGL_CONTEXT_LOST` by rebuilding GPU state from resource sources (5.2);
  a debug command that simulates it; tests. (`VK_ERROR_DEVICE_LOST` in 10.5.)
- [ ] **7.5 Display and lifecycle.** Orientation lock and changes, foldable and multi-window resizes,
  immersive mode, density (dp) for UI scale. (Done: keep-screen-on `platform::KeepScreenOn`, the "Remove animations"
  setting `platform::ReduceMotion`.)
- [ ] **7.6 Device lab.** Firebase Test Lab game-loop tests with screenshots on a device matrix
  (Mali, Adreno, PowerVR, low end); native symbols for Play Console crash reports
  (`debugSymbolLevel`); a vitals checklist. This is the only place precision artifacts show (no
  software GPU has fp16, 1.3): the screenshots include sharp specular highlights, large textures,
  full-screen effects and the canvas, and compare `r.mediump` 1 with 0. Also measure mediump GPU time on
  Mali (1.3: Adreno 640 got slower, Mali's offline compiler predicts lit meshes at 31 instead of 44-48 work
  registers and the post effects at 100% fp16): opt the shaders that gain in, per GPU family if needed.
- [ ] **7.7 Size and delivery.** An AAB size report in CI; texture-format targeting and asset packs
  once assets grow.

## Phase 8: Runtime infrastructure
- [ ] **8.1 Job system.** Workers (cores - 1), work-stealing deques plus an injection queue,
  dependency counters, `ParallelFor` with a grain size, the waiting thread helps, priorities, Tracy
  zones, an inline mode (0 workers); stress tests and a TSan CI job.
- [ ] **8.2 Memory and handles.** Per-frame and per-thread linear allocators, pools, generational
  `Handle<T>` with dense storage, memory accounting by category (engine panel, Tracy).
- [ ] **8.3 Web.** Emscripten: WebGL 2 through the GL backend, HTML5 input, `Frame()` from
  `emscripten_set_main_loop`, files preloaded or fetched (4.3), saves in IndexedDB, WebAudio unlocked on
  the first input. A single-threaded build by default, a threaded build for cross-origin-isolated
  hosts; a CI job; WebGL 2 limits added to the CLAUDE.md rules. WebGL 2 shaders are GLSL ES 3.00:
  1.3's precision rules (both stages agree on every uniform and block member) and its GLSL ES
  compile check already apply.

## Phase 9: Rendering architecture
- [ ] **9.1 RHI and null backend.** Device (caps, limits, frames in flight, deferred destruction),
  Buffer, Texture and views, Sampler, ShaderModule, bind group layouts and bind groups (frame /
  view / material / draw), graphics and compute pipelines, a command encoder with render, compute
  and copy passes (load/store actions, clear values, dynamic offsets, labels, timestamps),
  Swapchain (acquire, present, resize, pre-rotation), clip-space and y-flip conventions per backend.
  The null backend records commands for tests.
- [ ] **9.2 Shader pipeline.** Engine shaders in GLSL 4.50 with includes and permutation keys; a
  `wlw_shaderc` build tool (glslang, spirv-opt, SPIRV-Cross, reflection); per-platform shader
  archives. The game shader language (`shader_type ...`) goes the same way, with a runtime compiler
  and disk cache only when games create shaders from strings. A precision strategy (mediump /
  RelaxedPrecision / half) that keeps 1.3's rules: every uniform, block member and varying names its
  precision, fragment code defaults to mediump, game shaders stay highp unless they opt in, and the
  GLSL ES compile check moves into the shader compiler (game shaders compiled at runtime keep it).
  Variant lists recorded in debug builds for warm-up.
- [ ] **9.3 GL backend.** The RHI on GL 3.3 / GLES 3.0 / WebGL 2: tracked state (no `glGet` in a
  frame), sampler objects, immutable texture storage, a VAO cache with the index buffer inside the
  VAO, per-frame upload rings (unsynchronized mapping + fences; `glBufferSubData` on WebGL), per-draw
  data in one UBO with dynamic offsets, an FBO cache, load/store -> clear/invalidate, MSAA
  (`EXT_multisampled_render_to_texture` on mobile, blit resolve elsewhere), a program binary
  cache, `KHR_debug` labels, timer queries.
- [ ] **9.4 Meshes and vertex formats.** Mesh as a Resource (GPU side in the backend, CPU copy
  optional); vertex layouts with streams (a position-only stream for depth and shadow passes); a
  packed 24-byte default (float3 position, packed normal + tangent, half/unorm16 UV, RGBA8 colour);
  skinning attributes; 16-bit indices when they fit; uploads through the resource manager and the
  warm-up.
- [ ] **9.5 Render world and frontend.** A RenderWorld per Scene: dense proxies (instances,
  multi-meshes, sprites, lights, cameras) updated from node change lists; parallel frustum and layer
  culling; draw packets with 64-bit keys (pipeline, material, mesh, depth); automatic instancing of
  identical mesh + material runs; materials as resources with parameter blocks and bind groups
  (Unlit, SimpleLit and custom ShaderMaterials on one path).
  Early slice done (2026-10-25, for Bluzzle's per-block looks): per-node values for game mesh shaders
  (`instance uniform`, `GeometryInstance3D::SetInstanceParam`; Godot 4's instance uniforms, Unreal's Custom
  Primitive Data); a material's params uploaded only when its (id, version) differs from what the program has,
  game-shader draws grouped by material in the sort key, engine inputs' locations resolved after linking. Still
  to do here: parameter blocks in a UBO (and per-copy instance params for multi-meshes), the rest of the item.
- [ ] **9.6 Frame graph.** Passes declare transient and imported resources; compiling culls unused
  passes, aliases transient textures, derives load/store actions and barriers, and orders
  execution. Shadow, opaque, sky, transparent, sprites, picture canvas, post chain (PostPlan folded
  in; the post-effect API unchanged), overlay canvas, debug UI and readback all become passes. A
  graph view in the debug UI and the editor.
- [ ] **9.7 Switch over.** Every current feature on the RHI (variants, instancing, sprites, sky,
  shadows, post effects, canvas renderer, ImGui renderer, GPU timing through RHI timestamps, Quality
  hooks, the editor's Game and Scene panels); golden images match; the old driver is deleted.
- [ ] **9.8 Render thread.** A thread-model setting (single / render thread). The game thread
  extracts a frame packet; the render thread culls, builds the graph and submits, at most one frame
  behind. The GL context lives on the render thread; web stays single-threaded.

## Phase 10: Vulkan
- [ ] **10.1 Bootstrap.** volk, an instance with validation and debug utils in debug builds, device
  selection, queues, VMA, swapchain (Win32 through GLFW, Android surface), frames in flight, present
  modes, resize / out-of-date / suboptimal handling, Android pre-rotation, backend choice with
  automatic GL fallback.
- [ ] **10.2 Resources.** VMA buffers and images, staging uploads, per-frame upload rings, layout
  transitions, compressed formats by device caps, samplers, deferred destruction.
- [ ] **10.3 Pipelines and descriptors.** Pipelines from RHI descriptions (dynamic rendering when
  available, a render-pass cache otherwise), layouts from reflection, per-frame descriptor pools for
  transient sets, persistent material sets, dynamic offsets for per-draw data; `VkPipelineCache`
  saved to disk and validated; background pipeline compilation.
- [ ] **10.4 Passes and synchronization.** Render passes from the frame graph with load/store
  actions; lazily allocated transient depth and MSAA attachments on tilers; in-pass MSAA resolve;
  barriers (synchronization2 when present); timestamps; debug labels for RenderDoc and AGI.
- [ ] **10.5 Vulkan on Android.** A device policy (Vulkan 1.1+, Android 10+, a driver denylist, GL
  fallback), Swappy for Vulkan, recovery from `VK_ERROR_DEVICE_LOST`, validation layers in debug APKs, AGI
  profiling.
- [ ] **10.6 Parity and default.** Golden images on lavapipe (CI) and real GPUs; Bluzzle and
  Threadlock on Vulkan (Windows, a real phone or Test Lab, emulator); CPU, GPU and power compared with GL; the
  default backend per platform chosen from the numbers.

## Phase 11: Metal and macOS
- [ ] **11.1 Metal backend.** The RHI on Metal (MSL from the shader pipeline, memoryless
  attachments, MTLBinaryArchive caching, timestamps, debug groups).
- [ ] **11.2 macOS.** A macOS window through GLFW + CAMetalLayer; golden images on macOS;
  the Studio moves to Metal with the rest (it runs on GL from 6.5).
- [ ] **11.3 iOS on Metal.** iOS moves from the GL path (D17) to Metal; CPU, GPU and power compared; the GL path
  kept or deleted from the numbers.
- [ ] **11.4 CI.** The macOS runner also builds macOS and runs the golden images there (main and by hand).

## Phase 12: Content pipeline
- [ ] **12.1 Cooker.** A `wlw_cook` host tool, an asset database (resources' uids from 5.2; `.meta` sidecars
  hold import settings), content-hash incremental cooking, per-platform output, CMake, Gradle and Xcode
  integration, pak output, clear errors.
- [ ] **12.2 Textures.** Decoding (PNG, JPG, 16-bit, HDR, EXR); sRGB-correct mips (normal
  renormalization, alpha-coverage preservation); KTX2 with Basis Universal (UASTC / ETC1S) and
  direct ASTC/BC7 per platform; runtime transcoding by device caps; cubemaps and arrays; sprite and
  UI atlas packing.
- [ ] **12.3 Models.** A cgltf glTF 2.0 importer: hierarchy, primitives, PBR materials and samplers,
  vertex colours, tangents (MikkTSpace), skins, animations, morph targets, `KHR_lights_punctual`,
  `KHR_texture_transform`, `KHR_mesh_quantization`, `KHR_texture_basisu`, `EXT_meshopt_compression`.
  meshoptimizer for cache / fetch / overdraw order, quantization and LODs. Output: a binary
  ModelAsset instantiated into nodes, or a scene file (4.4). (From 1.4: a glTF node
  with a mirroring transform (det < 0) flips its triangles' winding; glTF says to reverse it when importing.
  The baked normals already point out: `core::NormalMatrix`.)
- [ ] **12.4 Fonts.** Cooked fonts: the TTF, prebaked MSDF atlas pages for chosen ranges, metrics.
- [ ] **12.5 Audio.** Decode and resample at cook time; load types Decompress / CompressedInMemory /
  Streaming; PCM16 or ADPCM for effects, Vorbis for long clips; mono stays mono.
- [ ] **12.6 Shaders and paks.** Pak archives (index, per-file zstd, aligned for memory mapping) mounted under
  `res://` (4.3); per-platform shader archives in the cooked output, versioned.
- [ ] **12.7 Hot reload.** Desktop watcher -> re-cook -> live reload of textures, meshes, materials,
  shaders, fonts and audio.
- [ ] **12.8 Import in the editor.** Import settings in the inspector, reimport from the asset browser, cook
  errors in the log panel.

## Phase 13: Rendering features
- [ ] **13.1 Colour pipeline.** Linear workflow (sRGB textures and targets), HDR scene colour
  (RGBA16F or R11G11B10F by caps), manual and auto exposure, tonemapping (ACES fitted, AgX, PBR
  Neutral), 3D LUT grading, dithering, UI composited in sRGB after tonemapping; the gamma opt-out;
  Bluzzle and Threadlock retuned with the owner.
  Early slice done (2026-10-25, for Bluzzle's glow): RGBA16F scene picture on any viewport (`SetFormat`, `r.hdr`),
  post settings as data, the output pass (exposure, Clamp / Shoulder tone mapping, dithering). Still to do here:
  linear shading, R11G11B10F / RGB10A2 by caps, ACES / AgX / full PBR Neutral, auto exposure, LUT grading.
- [ ] **13.2 PBR materials.** Metallic-roughness (GGX, Smith, Schlick; Lambert or Burley diffuse),
  base / normal / ORM / emissive maps, alpha opaque / mask (alpha to coverage) / blend,
  double-sided; Lit, SimpleLit and Unlit tiers; a material editor (inspector + preview). From 1.3:
  fp16 can't hold N·H near 1, so the specular terms stay highp or use Filament's form (1 - (N·H)²
  as |N×H|², exact in mediump); malioc checks the result. A way to turn specular off (a specular
  strength or colour, 0 = no highlight). From 1.4: the engine never turns on back-face culling. Single-sided materials want it,
  and a mirrored node (det < 0) then needs the front face flipped (`glFrontFace(GL_CW)`), and instanced copies
  that mirror split into their own draw (Godot: `CULL_VARIANT_REVERSED`). Normals already stay outward.
  Game mesh shaders get a surface entry point too (Godot spatial shaders, Unreal materials, Unity Shader Graph):
  `surface()` sets albedo / normal / roughness / emission and the engine wraps it in its lighting and shadows, so
  lighting changes (13.1, cascades, the RHI) reach game shaders without edits. Today's path (`fragment()` calling
  `SceneLighting` / `SceneShadow`, like Unity's hand-written URP shaders) stays as the full-control option, like
  Godot's `render_mode unshaded`; a shadow-catcher option (unlit, only darkened by shadows) comes with the tiers.
- [ ] **13.3 Environment lighting.** Skybox (cube, equirect), procedural sky, IBL (prefiltered
  specular, SH9 irradiance, BRDF LUT; cooked offline, computed at runtime for dynamic skies),
  reflection probes (baked, box-projected, blended).
- [ ] **13.4 Clustered lights.** Forward+ clusters, binned on the CPU with jobs (GLES 3.0) or in
  compute (Vulkan, Metal); inverse-square falloff with a range window; light layers; configurable
  limits. (Found in 1.3: malioc shows every lit mesh variant load/store bound, 10 LS cycles vs ~3.8
  arithmetic on Mali-G78, from the light loops reading `WlwFrame` with a runtime count and index; light
  lists per cluster, or loops with a compile-time bound, should fix it.)
- [ ] **13.5 Shadows.** Cascaded sun shadows (2-4 cascades, stable fit, texel snapping, blending,
  fade), an atlas for point and spot shadows, per-cascade caster culling with jobs, shadow data per
  viewport, PCF (PCSS on the high tier), presets per tier.
- [ ] **13.6 Anti-aliasing.** MSAA with on-tile resolve on every backend, FXAA fallback, TAA on the
  high tier (jitter, motion vectors from previous transforms, history clamping, sharpening).
- [ ] **13.7 Post stack.** Progressive bloom (Karis average), vignette, chromatic aberration, film
  grain, half-res bokeh depth of field, half-res GTAO (high tier), camera motion blur, SSR (desktop
  high tier); global and per-viewport settings with blending.
  Early slice done (2026-10-25, for Bluzzle's glow): progressive bloom as a post setting (`PostSettings::bloom`,
  cvar `r.bloom`): 13-tap downsample with soft-knee threshold and Karis average on the first level, tent upsample
  with scatter, added in the output pass before tone mapping; it replaced `scene::AddBloom`. Second slice
  (2026-10-27, for Bluzzle's big clears): vignette, chromatic aberration and screen-space ripples (shockwaves,
  `fx::Shockwaves`) in the output pass. Third slice (2026-10-27): a distortion buffer, the general form of those
  ripples (Unreal's distortion pass, HDRP's distortion buffer): draws with `Material::distortion` (billboards,
  particles, game shaders) add screen offsets into a half-size RG16F target after the main pass and Picture layers;
  the output pass shifts its reads by it; nothing is cleared or read on frames without such draws. With it, shader
  variants (`Shader::Create` defines; the output pass in 8) and array params in `ShaderMaterial`. The analytic ripples
  stay as the cheap path (no target). First user: Bluzzle's shockwaves. Still to do here: lens dirt, the other
  effects, settings blending, a full-size depth-tested distortion option (for 3D scenes where a ring behind a wall
  must not bend it).
- [ ] **13.8 Dynamic resolution.** Render scale driven by GPU time, FSR 1 (EASU + RCAS) upscaling with
  a bilinear fallback, tied into Quality.
- [ ] **13.9 LOD and culling.** Screen-size LOD selection with hysteresis and dithered crossfade, LOD
  bias per tier, distance culling per layer, a static BVH for large scenes, occlusion culling (CPU
  software depth everywhere, Hi-Z compute on Vulkan/Metal).
- [ ] **13.10 Debug draw and views.** Immediate debug drawing (lines, boxes, spheres, frustums, text;
  timed; depth-tested or on top). Views: wireframe, overdraw, mip level, normals, albedo, lighting
  only, cascades, clusters, LODs; chosen in the editor's Scene panel too.
- [ ] **13.11 GPU particles.** Compute emit / update / sort / indirect draw on Vulkan, Metal and GLES
  3.1+, with a CPU fallback (the current systems); curves and gradients over lifetime; flipbooks;
  depth-buffer collisions; a particle editor (curves, gradients, live preview).
  Early slice done (2026-10-27, for Bluzzle's line clears), on the CPU systems: `BillboardBatch` (camera-facing and
  velocity-stretched quads in one draw), `ParticleSystem::WriteTo(BillboardBatch&)`, size / intensity / colour over
  life (`anim::Curve`, `anim::Gradient`), HDR intensity, `fx::Trail`.
- [ ] **13.12 2D rendering.** World-space sprites with atlases, 9-slice, flipbook animation and
  sorting layers for 2D games.

## Phase 14: Input
- [ ] **14.1 Event core.** Timestamped events (keys, text, pointers with ids, wheel, gamepad); the confined cursor
  mode, multi-pointer state; the polling API built on the events. (The full key set, mouse buttons, wheel, cursor
  shapes and the hidden / locked cursor came in 3.1.)
- [ ] **14.2 Gestures.** Recognizers as a layer: tap, double tap, long press, swipe, pan, pinch,
  rotate; thresholds in dp; conflict rules. Today's built-in swipe and tap move here.
- [ ] **14.3 Actions.** Named actions (button, axis, 2D) with bindings and composites, contexts with
  priorities, runtime rebinding saved to `user://`, dead zones, input buffering.
- [ ] **14.4 Gamepads.** GLFW gamepads with the SDL mapping database, Paddleboat on Android, game controllers
  on iOS, rumble.
- [ ] **14.5 Text input and IME.** UTF-8 text and composition events: GLFW on desktop, GameTextInput
  on Android, UIKit on iOS; showing and hiding the soft keyboard.
- [ ] **14.6 Haptics.** Patterns and amplitude (Android VibrationEffect compositions, iOS Core Haptics).

## Phase 15: UI and text
- [ ] **15.1 Text layout.** UTF-8 everywhere, line breaking (libunibreak), wrapping, multi-line
  alignment, ellipsis, simple rich-text tags (colour, size, bold), cached shaped runs.
- [ ] **15.2 Fonts.** FreeType rasterization, a dynamic SDF/MSDF atlas with LRU pages, prebaked pages
  from the cooker, fallback chains (Latin -> Arabic -> CJK -> emoji), weights and styles, colour
  emoji. Text anti-aliasing keeps 1.3b's screen-pixel-range formula (the one MSDF uses), with the
  distance range per atlas page.
- [ ] **15.3 Shaping and bidi.** HarfBuzz (ligatures, kerning, Arabic and Indic scripts), SheenBidi
  for mixed directions, mirrored RTL layout.
- [ ] **15.4 Canvas.** Clip-rect (scissor) and transform stacks per batch, 9-slice images, gradients,
  borders and strokes, soft shadows, a UI atlas, backdrop blur. New quad kinds keep 1.3b's shape:
  derivatives taken once in uniform control flow, each pixel computes only its own kind, textures
  read only by the kinds that need them, per-quad constants in flat varyings, precision named per
  input (canvas coordinates highp, colour and coverage mediump). In so far (2026-10-26/27, for Bluzzle's menu and
  banners): linear gradient rects, modulate / fade scopes, premultiplied images, text gradient fills, SDF glows (a
  soft layer, params.w), per-glyph effects (GlyphFx), per-corner triangle colours.
- [ ] **15.5 Widgets and layout.** An ID stack, scroll views (inertia, overscroll), virtualized lists,
  a text field (IME), slider, progress bar, checkbox and radio, dropdown, a modal stack,
  keyboard/gamepad focus navigation, flexbox-style immediate-mode layout, accessibility hooks
  through the bridge.
- [ ] **15.6 Localization.** String tables cooked per locale, locale detection, runtime switching,
  CLDR plural rules for shipped languages, number and date formatting, pseudo-localization, font
  fallback per locale; a string-table editor panel.

## Phase 16: Audio
- [ ] **16.1 Voices.** `Play` returns a handle (stop, fade, volume, pitch and pan while playing);
  priorities, voice limits with stealing, virtual voices for inaudible sounds.
- [ ] **16.2 Mixer graph.** User-defined buses (volume, mute, solo) with effects (biquad filters,
  compressor, limiter, reverb, delay), sends, ducking, snapshots with transitions; a mixer panel.
  Early slice done (2026-10-26, for Bluzzle's adaptive music): per-bus buffers; `audio::Filter` (TPT SVF low-pass /
  high-pass, cutoff glides in octaves, bypassed when open) as each bus's two filters; per-bus gain glides and ducking
  (attack / hold / release, deepest wins). Snapshots are the game's (it sets a bus's values per moment).
- [ ] **16.3 Spatial audio.** A listener on the camera; an AudioSource3D node (attenuation curves,
  cone, doppler, spread); equal-power panning; occlusion low-pass from physics raycasts.
- [ ] **16.4 Clips and streaming.** Load types from the cooker, compressed-in-memory decoding, any
  clip streamable, ref-counted unload, music playlists with crossfades and stingers.
  Early slice done (2026-10-26, for Bluzzle's music): `MusicParams` (fade, volume trim, a gapless intro, restart),
  equal-power crossfades over 4 stream slots (a fading stream is picked up again, not cut), restarted playthroughs
  freed once the mixer lets go (`StreamSource::InUse`).
- [ ] **16.5 Platform audio.** AAudio performance mode, output-device changes (headphones), latency
  reporting; web specifics with 8.3.

## Phase 17: Animation
- [ ] **17.1 Skeletons and skinning.** A skeleton resource, GPU skinning (joint palette in a UBO or a
  texture; dual quaternions optional), skinned bounds, bone attachments.
- [ ] **17.2 Clips and AnimationPlayer.** Compressed clips (quantized rotations, key reduction),
  sampling on jobs, play / crossfade / speed / loop / events, node TRS tracks.
- [ ] **17.3 Blending and state machines.** 1D and 2D blend spaces, layers with masks, additive
  layers, root motion, a state machine with conditional transitions.
- [ ] **17.4 Morph targets.** GPU blend shapes with animated weights.
- [ ] **17.5 IK and constraints.** Two-bone IK, look-at / aim, FABRIK chains.
- [ ] **17.6 Property tracks and tween sequences.** Tracks for any reflected property (4.1): material params,
  colours, visibility; tween sequences and parallel groups with callbacks; an animation timeline in the editor
  (keys, curves, preview).
  Early slice done (2026-10-25/27, for Bluzzle): `anim::Timeline` (beats placed Then / With / At, polled cues),
  `Spring<T>` (exact damped spring), `Curve` and `Gradient` (baked), `CubicBezier` + `ArcLength` (even pace along a
  path), next to the older `Tween<T>`, easing and shake. Still to do: tracks on reflected properties, callbacks,
  the editor timeline.

## Phase 18: Physics
- [ ] **18.1 Jolt (3D).** A physics system on the fixed step; body nodes (static, rigid, kinematic,
  character through CharacterVirtual); shapes (primitives, cooked convex hulls and meshes,
  heightfields); layers and filters; ray / shape / overlap queries; contact signals on the game
  thread; joints; CCD; sleeping; Jolt jobs on our job system; interpolated transforms; a
  cross-platform determinism option; physics materials.
- [ ] **18.2 Box2D v3 (2D).** The same integration for 2D games: bodies, shapes, sensors, joints,
  queries, events; its tasks on our job system.
- [ ] **18.3 Physics tooling.** Debug drawing, a stats panel, profiling zones; collision shapes edited with
  gizmos in the editor.

## Phase 19: Studio as a product (only if still wanted then; ask first)
For other developers' games: one prebuilt Studio that opens any project and loads its game code, like Godot's
(`wlw_studio` from 3.2 already opens any project, with engine types only).
- [ ] **19.1 Game modules.** The engine as a shared library and each game as a module the editor loads (one CRT,
  one copy of the engine's globals, a module API with a version); code reload with state saved across it, or
  Live++.
- [ ] **19.2 Projects.** A project manager (new from a template, open recent), project settings, templates for
  2D and 3D games.
- [ ] **19.3 Export.** Builds from the editor: Android APK / AAB (Gradle), iOS (Xcode), web, desktop; signing
  settings; export presets.
- [ ] **19.4 Remote link.** Inspect and tweak a running game on a phone or another process from the editor
  (Godot's remote scene tree; over adb forward or TCP): the hierarchy, the inspector and cvars live. (Can be pulled
  earlier for the owner's own games.)
- [ ] **19.5 Stability and docs.** Crash recovery and autosave, editor tests, a manual, samples; a decision on a
  scripting language.

## Not planned (ask to add)
A scripting language (revisit with 19.5), networking and multiplayer, consoles (NDA platforms), Direct3D 12 (Vulkan
covers Windows), hardware ray tracing / virtualized geometry / dynamic GI, terrain and vegetation,
navigation meshes (Recast/Detour), video playback, XR, in-app purchases.

## Old numbering (before 2026-10-21)
Commits and CHANGELOG entries before the reorder use these numbers. Phases 0-2 kept theirs.
Phases 3-5 were reordered again on 2026-10-28 (before any of their steps started); the table shows the current
numbers.

| Old | New |
|---|---|
| 3 Runtime infrastructure: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6 | 8.1, 8.2, 5.4, 4.3 (paks: 12.6), 5.2, 8.3 |
| 4 Rendering architecture: 4.1-4.8 | 9.1-9.8 |
| 5 Vulkan: 5.1-5.6 | 10.1-10.6 |
| 6 Android platform: 6.1-6.7 | 7.1-7.7 (Vulkan parts of 6.2 and 6.4: 10.5) |
| 7 Content pipeline: 7.1-7.7 | 12.1-12.7 (+ 12.8 import in the editor) |
| 8 Rendering features: 8.1-8.12 | 13.1-13.12 |
| 9 Input: 9.1, 9.2-9.6 | 3.1 (keys, mouse) + 14.1 (events), 14.2-14.6 |
| 10 UI and text: 10.1-10.6 | 15.1-15.6 |
| 11 Audio: 11.1-11.5 | 16.1-16.5 |
| 12 Animation: 12.1-12.6 | 17.1-17.6 |
| 13 Physics: 13.1-13.3 | 18.1-18.3 |
| 14 Apple platforms: 14.1, 14.2, 14.3, 14.4 | 11.1 + 11.2, 6.1 + 6.3, 6.2, 6.4 + 11.4 |
| 15 Editor and tools: 15.1, 15.2, 15.3, 15.4, 15.5, 15.6 | 4.1, 4.4 + 5.3, 3.2, 5.1 + 5.3, 5.2 + 12.8, 5.5 (+ panels in 9.6, 13.x, 17.6, 18.3) |
