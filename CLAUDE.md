# WLWengine: notes for Claude Code

C++20 / OpenGL game engine, built as a CMake static library (`wlw::wlw`). Targets Windows,
macOS, Linux and Android today; iOS is planned. Games live in **separate repos** and include
this repo as a git submodule. The current game is Bluzzle
(github.com/aboelbisher/Bluzzle). Its `CLAUDE.md` holds the overall project context, the
roadmap and the owner's working preferences: read it too when working from there.
`samples/snake/` is the first game (Snake), kept here as a sample and engine smoke test.

Remote: `git@github.com:aboelbisher/WLWEngine.git`, branch `main`.

## Roadmap
The engine is being brought to big-engine quality step by step: **ROADMAP.md** holds the current
step, the phases, the decisions and the definition of done. For every step: explain the concept and
the exact plan in chat and wait for the owner's OK before writing code; show the changes and commit
only after approval; then tick the step in ROADMAP.md.

## Layout
```
engine/wlw/
  audio/      Audio (miniaudio device: WASAPI / AAudio...) + Mixer (decoded PCM, lock-free command queue,
              voices with pitch/pan, per-sound caps, buses). App().Audio(); silent in background (Suspend), during calls /
              alarms (SetInterrupted); music steps aside for the player's own (SetOtherMusicPlaying); fed by
              android/java/com/wlw/AudioWatcher.kt (bridge "audio"; watches playback, never takes audio focus).
              Music: Audio::PlayMusic streams a file (decode thread -> lock-free ring -> Mixer stream slots with crossfades)
  anim/       ease.h (Penner easing curves), tween.h (Tween<T>: value-type tweens the game owns and samples),
              shake.h (trauma-based screen shake: trauma decays, offset = trauma^2 x smooth noise)
  fx/         ParticleSystem: CPU simulation (fixed capacity, swap-remove, bursts, Emit = one exact particle,
              optional Floor rectangle they bounce on and fall past) -> instances. FlowParticles (flow_particles.h):
              10-20k dots along paths (liquid in pipes; not a fluid sim), SoA arrays, paths resampled evenly (O(1)
              lookup), SetNext routing (round robin; none = fade out), SetStop = a gate dots queue behind (the line
              grows back up the path), emitters; WriteTo(SpriteBatch) packs 16 B/dot + bounds. SetDensity(0..1) scales emitters + queue packing (count only;
              the game sets dot_size, e.g. x 1/density^0.25; queues track their length, no gap on a change). Emulator, -O2, 20k dots:
              Update ~30 us, WriteTo ~95 us; Pixel 4 (-O2): Update ~195 us, WriteTo ~950 us (the phone's real cost)
  debug/      DebugUI: Dear ImGui (third_party/imgui) fed from Input, drawn via Window::SetOverlay; games add
              panels in System::OnDebugUI() (#if WLW_DEBUG_UI); the engine's own window is debug::EnginePanel (a
              System the Application adds first) then debug::CVarPanel (every cvar by prefix, typed widgets, reset).
              F1 / "Debug" button. Console (console_window.h, owned by DebugUI): ` key / "Console" button: the log
              (level + text filters), an input line with history and Tab completion (ConsoleModel = the testable
              logic); phones: no typing yet (GameTextInput, D9). ImGui gets keys + text from Input; while it wants
              text input the game's keyboard is blocked. Off in release builds
  app/        Application (final: owns the services and runs the loop; see the frame in application.h): Window(),
              Input(), Audio(), Screens(), Time(), Config(). engine_cvars.h: log.filter, r.gpu_timing,
              r.invalidate_framebuffers, quality.lock (Run applies the cvar sources first, wires these to the window /
              log / quality, saves changed Saved cvars at the end of a frame). Games add Systems (system.h: always running,
              like Unreal subsystems / Godot autoloads; hooks OnStart/OnFixedUpdate/OnUpdate/OnRender/OnRenderUI/
              OnDebugUI/OnBack/OnAppPaused/OnAppResumed/OnShutdown, run in the order added, then the screens'; App()
              = the services) and push Screens. SystemList = the testable dispatch. Time (time.h): Delta, RealDelta,
              FixedStep, Alpha, SinceStart, SetScale (hit-stop / slow motion via core/time_scale.h), Fps.
              entry_point.h: WLW_MAIN(make_app) with the game's MakeApp() (builds the Application, adds its systems),
              Screen + ScreenStack (game states: deferred push/pop/replace, overlays, fades, Back routing)
              Quality (quality.h, App().Quality(); like Unity QualitySettings + Adaptive Performance): the game defines
              N levels (Configure, OnChange = apply a tier; Lock/Unlock for menus/tests), the engine picks one: GPU ms
              (timer queries, turned on by Configure) or late frames > budget x down_at / slow_frame_at for 2 s = down,
              GPU < budget x up_at for 10 s = up (wait doubles per failure of that level, max 8x), 3 s settle after a
              change/resume, readings smoothed ~0.5 s and clamped at 2 budgets; no GPU timer = down only; level saved
              in <save dir>/quality.txt. Fed by Application after each frame (UpdateQuality). Engine panel: level, -/+/Auto
  core/       math types, FixedTimestep, Random (SplitMix64: deterministic everywhere, state saveable), log.h (WLW_LOG(Category, Level, fmt...): levels Verbose..Fatal, categories LogCore/App/Render/Shader/Audio/Platform/UI/FX (+ kit LogKit, games WLW_LOG_CATEGORY), compile-time stripping, SetLogFilter("Render=Verbose,*=Warning") / env WLW_LOG, sinks: platform, RecentLogs ring (512), StartLogFile (debug builds: <save dir>/log.txt, previous run log.txt.prev), AddLogSink; LogThreadId), assert.h (WLW_ASSERT debug-only fatal, WLW_VERIFY always evaluated: logs + returns false, WLW_UNREACHABLE; SetAssertHandler for tests), cvar.h (CVar<bool/int/float/std::string>{"r.name", default, help, flags Saved/ReadOnly/Cheat, range}: dotted lowercase names; sources in order code default < wlw.cfg (assets) < <save dir>/cvars.cfg (Saved, written when changed) < command line +name value / Android --es wlw "a=1;b=2" < console; unknown names wait for registration; OnChange; game thread), console.h (ConsoleCommand RAII, ExecuteConsoleLine, built-ins help/find/set/reset/exec (+quit from Application), CompleteConsoleName, ConsoleModel), profile.h (WLW_PROFILE_SCOPE/FUNCTION/SCOPE_DYNAMIC/TEXT/FRAME/PLOT/THREAD_NAME/MESSAGE -> Tracy + Android ATrace; only with the CMake option WLW_PROFILE, else empty with unevaluated arguments; one zone per scope; see "Profiling"), type_name.h (TypeName<T>() at compile time: "wlw::app::System"; System::Name() for zones), mesh/model, collision.h (AABB, Ray, Plane, IntersectPlane)
  input/      Input: keys (pressed vs held; text-editing keys + Grave), Text() (typed code points), pointer (mouse/first
              finger), swipes, taps; pointer and keyboard can be blocked (debug UI), Raw* queries ignore that
  math/       ExactCover: Knuth's Algorithm X with Dancing Links (exact cover search, counting, hints);
              BitGrid<W,H> (bitboards: a grid as 64-bit words, fits = one AND); game_search.h (Minimax with
              alpha-beta, BestMove, Expectimax; a game = small class with Moves/Apply/Evaluate/MaxToMove);
              gf2.h (linear algebra over GF(2): BitVector, SolveGf2 = Gaussian elimination with null space,
              LightestSolution: Lights Out-style "which toggles clear it" problems)
  platform/   THE ONLY PER-OS CODE: platform.h interface + glfw/glfw_platform.cpp (desktop)
              + android/android_platform.cpp (NativeActivity + EGL). Also save files: GetSaveDirectory, WriteFileAtomic, ReadLocalFile. iOS goes here later.
              LaunchArguments() (launch_arguments.cpp; desktop argv via WLW_MAIN, Android the intent's "wlw" extra).
              Native bridge (bridge.h/.cpp): BridgeCall(service, method, json) C++ -> Kotlin; Kotlin posts back
              (WlwBridge.post) -> queue -> SetBridgeHandler callbacks on the GAME thread (Application dispatches
              once per frame). Kotlin side: android/java/com/wlw/ (WlwBridge, WlwService, WlwActivity = NativeActivity
              + services; games subclass it and add its java dir to their Gradle sourceSets).
  rendering/  RenderingDriver/RenderDevice interfaces + one GL backend for GL 3.3 core AND GLES 3.0. Per viewport: gather lights +
              drawables, upload the per-frame block, shadow pass (depth from the shadow light), main pass (PCF shadows).
              Scene pass without GL (scene_gather.h, benchmarked): GatherScene = one scene walk (lights, drawables + world
              boxes, sprite batches, shadow boxes), CullScene = frustum test + model/normal matrices + view depth per node.
              Draw order (render_list.h): BuildDrawList makes one DrawItem per visible mesh (each node culled once, its world
              box and model matrix computed once) with a 64-bit sort key: opaque = program, texture, depth front to back;
              transparent = depth back to front (Ericson's "Order your graphics draw calls around!"); one SortDrawOrder.
              GLStateCache (gl_state_cache.h) skips redundant program / blend / depth-write / texture calls; Reset per viewport
              (UI and ImGui change state too). Lists are reused every frame (no allocations once warm). RenderStats has program
              changes, texture binds, skipped calls and CPU gather / sort / submit us (engine panel). Measure with -O2: a debug
              (-O0) Android build is ~10x slower (Bluzzle: ~330 us gather at -O0, 24 us at -O2).
              SHADERS are files: rendering/shaders/** (mesh, shadow, skybox, ui .vert/.frag; wlw/frame.glsl, lighting.glsl,
              shadows.glsl, common.glsl, color.glsl; custom/* = what wraps a game's shader), embedded at build time by
              cmake/EmbedShaders.cmake (shader_library.h: FindShaderFile, AddShaderFile for a game's own includes).
              shader_preprocessor.h: #include (once, cycles reported), #defines, #line + MapShaderLog (errors name file:line).
              gl_program.h: BuildProgram / BuildEngineProgram = the one way programs are made (binds the frame block).
              Per-frame UBO WlwFrame (frame.glsl) <-> FrameUniforms (frame_uniforms.h, std140 offsets tested): camera,
              lights by type (up to 4 directional + 4 point + 4 spot; SetFrameLights), shadow params.
              Variants (mesh_variant.h): INSTANCED, TEXTURED, LIT, SHADOWS, BLEND as #defines, one cached program per
              key; RenderingDriver::WarmUp (called by Application after OnStart) compiles what the scenes need, hidden and
              empty-instanced nodes included; later compiles are logged ("compiled during play"). Normal matrix on the CPU
              for plain nodes (instanced: per vertex, copies may be squashed).
              NO DEVICE IN GAME CODE (gpu_resource.h): textures (WTexture2D::Create / FromImage, WCubemap::Create), shaders
              and render targets are plain CPU objects made with their own Create (no GL context: usable in tests), like
              Mesh; the renderer makes their GPU side on first use (gl_resources.h: GLTexture, GLShaderFor, GLFramebuffer)
              and WarmUp prepares every alive one (Resource::ForEachAlive). RenderDevice = mesh buffers, engine-internal.
              Post passes (post_pass.h): Viewport::AddPostPass({material (shader_type post), scale, format, name, inputs});
              with passes the 3D draws into the scene picture, PlanPostChain (CPU, tested) ping-pongs pooled targets, the
              last pass writes the destination (screen or viewport texture), then the Overlay canvas layers (UI not post-processed).
              Ready-made (scene/post_effects.h, shaders/post/*): AddBlur (separable 9-tap Gaussian in 5 reads), AddBloom
              (bright pass with soft knee -> blur at 1/4 -> combine onto the picture before it), AddThreshold (density ->
              solid shapes coloured rgb/a: soft additive dots -> AddBlur -> AddThreshold = metaballs/liquid; keep the
              viewport AND the blur RGBA16F, RGBA8 clips colour x density and the middles go white). Bloom/Blur::SetScale +
              PostChain::SetScale change resolution at runtime (passes found by material; re-planned, targets resized).
              Framebuffer invalidation (GLES glInvalidateFramebuffer, nothing on desktop GL 3.3): depth after the 3D,
              each post output before it's drawn (DONT_CARE load), the screen's depth/stencil before swap;
              Window::SetInvalidateFramebuffers (panel checkbox), RenderStats::invalidations. Pixel 4: no measurable GPU ms
              change (Adreno; saves bandwidth timers don't show).
              GPU timing (gpu_timer.h): GpuTimer = timer queries in a ring of kLatency+1 frames, read 3 frames late (no
              stall), disjoint frames dropped (logic tested with a fake GpuQueries); GL: core on desktop, GLES needs
              EXT_disjoint_timer_query (the Android emulator lacks it; platform::GetGLProcAddress). Sections per viewport:
              3D, each post pass, canvas -> Viewport::GetGpuStats (smoothed ms), Window::GpuFrameMs / SetGpuTiming (on in
              debug builds) / GpuTimingSupported. Engine panel shows them.
              Profiler GPU zones (gl_gpu_profile.h, WLW_PROFILE builds): WLW_PROFILE_GPU_ZONE(_DYNAMIC) = Tracy's OpenGL
              timestamp zones (shadow, main pass, canvas, post passes, overlays); GLES: EXT_disjoint_timer_query functions
              looked up at run time and #defined to the desktop names before TracyOpenGL.hpp; context made when a viewer
              connects; cvar profile.gpu_zones. Separate from GpuTimer.
              Custom shaders (shader.h): one text (shader_type canvas|mesh|post; fragment(), optional vertex()) -> Shader::Create(code) or Shader::FromFile(path) -> ShaderMaterial::Create(shader)
              + SetParam(name, float/vec/mat4/texture); node/mesh SetMaterial or Canvas::ShaderRect. Engine inputs are prepended
              (documented in shader.h; may #include wlw/lighting.glsl and use SceneLighting). Data textures: WTexture2D::Create(w, h, {R8/RGB8/RGBA8/R16F/RGBA16F}) + Update (copied, uploaded before the next draw)
              Blending (blend_mode.h): Material::blend = Opaque / Alpha / Additive / Multiply (ShaderMaterial too), depth_write
              (default: only Opaque writes depth). Every shader outputs PREMULTIPLIED alpha, so gl_blend.h has one table for
              3D, canvas and custom shaders. Opaque meshes draw first, then transparent ones back to front (by bounding-box
              centre along the view direction): both come from the draw list's sort keys (render_list.h); transparent ones
              are depth-tested but not written. An opaque material writes alpha 1 (no BLEND variant).
  scene/      Node (scene graph, local transforms, visibility, instancing: SetInstances = one draw call for N copies).
              SpriteBatch (sprite_batch.h, a Node3D like LightNode; AsSpriteBatch): N soft dots in one draw call, 16-byte
              SpriteInstance (xy on the node's z=0 plane, size, RGBA8; was 80 B per InstanceData), Write(count) fills in
              place, SetBounds/FitBounds = culling box (no per-dot bounds), Additive by default (never sorted). GL:
              GLSpriteBuffer (own VAO, no vertex buffer: corners from gl_VertexID, strip of 4 x instances),
              shaders/sprite.vert/.frag (TEXTURED = the material's texture, else a computed (1-d²)² dot). No shadows/lights.
              RenderStats::instance_bytes = per-copy data uploaded this frame.
              Scene (scene.h) = the world: nodes, lights, skybox; owned by nobody who looks at it. Viewport (viewport.h) =
              "draw this Scene from this camera into this picture" + a 2D Canvas over it (like Godot's SubViewport):
              Viewport::Create({format, depth, smooth, clear}) draws into a RenderTarget (rendering/render_target.h,
              an FBO; RGBA16F falls back to RGBA8 if the GPU can't), GetTexture() -> any ShaderMaterial; SetSizeFromScreen /
              SetSize; UpdateMode Always / Once / Disabled. Several viewports may share a Scene.
              Window = surface + input + GetScreen() (its root viewport: scene, camera, the game UI canvas) +
              AddViewport (offscreen, drawn before the screen in the order added) + overlays (debug UI).
              Window::Create(size, title, {.visible = false}) = a hidden window (desktop): just a GL context.
              The driver: DrawWindow -> DrawViewport(each) -> gather, shadow pass, main pass, post passes, canvas.
              RenderingDriver::ReadPixels(target) = a target's picture as RGBA8, top row first (stalls: tests,
              screenshots); GpuName() = GL_RENDERER.
              cameras (FollowCamera = perspective, OrthographicCamera = parallel/top-down;
              Camera3D::ScreenPointToRay for picking, fed with Input::SurfaceSize()).
              Lights are nodes (light.h: DirectionalLight / PointLight / SpotLight, added with AddNode); nodes opt in to
              SetCastsShadow / SetReceivesShadow; shadow_map.h fits the light's box to the casters (texel-snapped)
  ui/         Two layers, like the big engines (Godot RenderingServer canvas vs Control nodes, Slate draw elements vs UMG):
              A. Canvas = the 2D DRAW LIST only: virtual units (reference size fits the screen), FillRect (rounded), Line,
                 Text (Font: TTF -> SDF atlas), Image(rect, texture, ImageStyle{tint, uv, blend}) = sprites, ShaderRect (a
                 rect drawn by a material's shader). Batched by texture + blend (1000s of sprites = 1 draw call); UIRenderer
                 (GL) draws it. Blending belongs to what you draw with: Image's style, ShaderRect's material (canvas shaders
                 default Alpha, mesh shaders Opaque), UI shapes Alpha. No widgets, input or screen geometry in it.
                 Shapes: Circle, Triangle (edge distances interpolated, AA), Arc/Ring (sdArc), all in the one UI shader.
              C. The UI layer: UIContext (ui_context.h: canvas + pointer + safe_area + interactive + active widget + theme +
                 dt + Memory(id) = per-widget floats kept while asked for each frame (NewFrame drops the rest); Application
                 owns one and passes it to OnRenderUI(ui) / Screen::RenderUI(ui)), theme.h (Theme: font + colour roles +
                 radii/depth; the game sets ui.theme), icons.h (Icon enum + DrawIcon: vector icons from Canvas shapes),
                 widgets.h (immediate mode: Pressable/WidgetId/RaisedRect building blocks; Button(variant, icon), IconButton,
                 IconToggle, Switch, SettingRow, LinkRow, Card (mode tiles: caller draws the art), Dialog (scrim, slide-in,
                 close X / tap outside), Segmented, Chip, Stars; the older font+ButtonStyle Button/Toggle/Panel still work),
                 layout.h (Centered, Inset, Row, Column).
              The UI canvas is Window::GetScreen().GetCanvas(); an offscreen viewport's canvas is drawn into its texture.
              Canvas layers (viewport.h, like Unity Canvas render modes + sort order): AddCanvasLayer({mode, order}); Picture
              = part of the picture (after 3D, before post passes), Overlay = on top after them; GetCanvas() = Overlay 0.
  utils/      image/glTF loaders, texture cache, primitives.h (procedural cube, bevelled cube, sphere, gem meshes),
              textures.h (procedural textures: RadialFalloffTexture = a soft (1-d²)² dot for glows, particles, metaballs)
kit/wlw/kit/  wlw::kit: game-level building blocks shared by games (separate target on top of the engine; the
              engine never includes it). SaveStore (JSON file, atomic write, .bak fallback, versions + migrations),
              progress.h (DeviceId, Counters, Scores, LevelProgress: all mergeable, for cloud sync), daily.h (Date,
              Today, DailyLog streaks), cloud_sync.h (interface only),
              ads.h (Ads: AdMob via the bridge on Android, FakeAds on desktop; AdsConfig = test unit ids by default),
              ad_pacing.h (when an interstitial is due). kit/android/java/com/wlw/kit/AdMobService.kt = Kotlin side
              (UMP consent, load/show/retry); games add that dir to their sourceSets and depend on play-services-ads + UMP
samples/      small games, built only when the engine is the top-level project (desktop):
              snake/ (Snake + its rules tests)
tests/        engine unit tests (no window/GPU: quality levels (down/up, back-off, settle, frame-time-only, lock, saved), draw order (sort keys), scene gather + cull, shader system (preprocessor, embedded files in every variant, std140 layout, lights, variants, UI kinds), systems (order, starting, Back), blending (depth-write rule, canvas batches, images, falloff texture), resources (made without a GPU, updates, alive list), post passes (planning, ping-pong, named inputs, chain targets, effects), viewports (scenes, sizes, update modes), anim, audio, core (random, atomic files, bridge queue), exact cover, search (bitboards, minimax, expectimax), gf2 (Lights Out), shader text splitting, lighting (light nodes, shadow-box fitting), fx, input, kit, picking, primitives, ui (canvas, safe area, widgets), screens, profile macros + TypeName). doctest: one binary wlw_tests, a TEST_SUITE per file, shared main test_main.cpp,
              test_helpers.h (Near = absolute tolerance); compound checks in double parentheses CHECK((a && b));
              each TEST_CASE is its own CTest test (cases run in parallel processes: no shared temp files);
              test data in tests/data/, also top-level only. tests/support/ = wlw_test_support (image_compare.h: PNG
              load/save, CompareImages with a channel tolerance + allowed bad fraction + diff image).
              GOLDEN IMAGES (tests/gpu, binary wlw_gpu_tests, CTest label "gpu", names "gpu: <case>"): each case builds a
              fixed scene in an offscreen 256x256 viewport of a hidden window (golden.h MakeViewport / CheckGolden) and
              compares it with tests/gpu/references/<name>.png. References come from Mesa llvmpipe (CI, Linux, xvfb):
              strict there (tolerance 2, 0.1% of pixels); real GPUs get 16 / 0.5%. Failures write <name>.actual.png +
              .diff.png to build/<preset>/tests/gpu/gpu_output/ (CI: the gpu-output artifact). wlw_gpu_tests --update
              rewrites references from this GPU: only commit llvmpipe ones. A rendering change that changes pictures
              on purpose = new references from CI's artifact, reviewed like code. Windows CI skips them (-LE gpu).
benchmarks/   wlw_benchmarks (nanobench; top-level only, desktop + Android executable): no window/GPU, fixed seeds, one op =
              one frame's work. Groups: render (gather/cull/sort 1k, 10k), transform (flat/nested 1k, 10k), fx (flow 20k,
              particles 5k), ui (text 1k glyphs), audio (mix 24 voices). Add: WLW_BENCHMARK("group") { ctx.Run("group/name",
              [&] { ... }); } (names are stable: baselines match by name). --out JSON, --baseline compare (warning past
              --threshold %, never a failure), --smoke = CTest "bench: smoke" (label bench). Baselines in
              benchmarks/baselines/: windows-local (owner's Ryzen 7 9800X3D, MSVC Release), linux-ci (bench.yml
              artifact), optional per-phone ones (<device>.json, e.g. pixel4; see "Benchmarks" below).
third_party/  glad (desktop only), stb, tinygltf + nlohmann json (also its own target wlw_json), miniaudio 0.11.25, stb_vorbis, Dear ImGui 1.92.9b,
              doctest 2.4.12, nanobench 4.3.11 (wlw_nanobench), Tracy 0.14.1 client (target wlw_tracy, only with WLW_PROFILE) (vendored; versions and licences in third_party/README.md)
cmake/WLWHelpers.cmake  wlw_add_game(), wlw_add_assets(), wlw_set_warnings()
vcpkg/ + vcpkg.json     desktop deps (glm, glfw3). Android fetches glm via FetchContent instead.
```

## Rules
- **Platform code stays in `engine/wlw/platform/<os>/`.** Nothing else may include GLFW, EGL,
  Android or other OS headers. Shared code talks to `platform.h`, `scene::Window` and `input::Input`.
- **Keep everything GLES 3.0 compatible**, because one renderer serves desktop and mobile:
  - Include GL through `wlw/rendering/gl.h` only (never `<glad/glad.h>` or `<GLES3/gl3.h>` directly).
  - Shader sources have **no `#version` line**. The driver prepends `kGLSLHeader`
    (`#version 330 core` / `#version 300 es` + precision). They must compile as both GLSL 3.30
    and GLSL ES 3.00 (no `texture2D`, no `gl_FragColor`, no desktop-only built-ins).
  - Texture uploads use sized internal formats (`GL_RGB8`, `GL_RGBA8`, `GL_R8`).
  - No desktop-only GL calls (`glPolygonMode`, `GL_QUADS`, double-precision functions, ...).
- **Logging:** use `WLW_LOG(LogRender, Warning, "fmt", ...)` with the system's category (`WLW_LOG_INFO/WARN/ERROR` =
  category General, fine for games), not `std::cout`, which goes nowhere on Android. Android logcat tag: `WLW`.
  Checks: `WLW_ASSERT` for programmer errors (gone in release), `if (!WLW_VERIFY(cond, "fmt", ...)) return ...;` for
  failures the code handles. Formats are printf-style string literals (the compiler checks them).
- **Files and assets:** read through `platform::ReadFile()`, because Android assets live inside the APK.
- **GPU resource destructors** must check `platform::HasCurrentGLContext()` before calling GL.
- **New source files** must be added to the explicit list in `engine/CMakeLists.txt`.
- **Conventions:** namespaces `wlw::{core,scene,rendering,input,platform,utils}`; classes and
  methods `PascalCase`; variables `snake_case`; private members `snake_case_`; `.clang-format`
  (LLVM, 2 spaces, 120 columns) for new code.
- **Don't delete files without asking the owner first.**

## Building the engine on its own
Windows (from a "Developer PowerShell for VS"):
```
cmake --preset windows-debug
cmake --build --preset windows-debug
.\build\windows-debug\samples\snake\wlw_snake.exe
ctest --test-dir build\windows-debug --output-on-failure -j8
ctest --test-dir build\windows-debug -R tween                 # cases whose name matches
.\build\windows-debug\tests\wlw_tests.exe -ts=anim            # one suite (-tc="tween*" = cases)
```
CI (`.github/workflows/ci.yml`, GitHub Actions on every push) runs the same presets; reproduce a job with
`cmake --preset <name>` + `cmake --build --preset <name>` + `ctest --preset <name>`:
`windows-debug` / `windows-release` (Release only on main), `linux-asan` (clang, ASan + UBSan via
`WLW_SANITIZE`), `android-arm64` (build only: set `ANDROID_NDK_HOME` to NDK 30.0.16248370, the games' version).
New code must pass all of them: no MSVC-only includes, no UB, no leaks. The Android job also turns on `WLW_PROFILE`.

## Benchmarks
Numbers only from optimized builds (a Debug run prints a warning). `.github/workflows/bench.yml` runs them on Linux
clang Release on main, manual runs and `bench/**` branches (summary table + `bench-linux-ci` artifact; refresh
`linux-ci.json` from that artifact). Performance work: run before and after, compare with `--baseline`. The required
comparison is desktop (`windows-local.json`) + Linux CI; phone numbers are optional, taken when a phone is at hand.
```
cmake --preset windows-release -B build\win-release ; cmake --build build\win-release --target wlw_benchmarks
.\build\win-release\benchmarks\wlw_benchmarks.exe [filter] --baseline benchmarks\baselines\windows-local.json [--out run.json]
```
Any Android phone (optional): one baseline per device, `benchmarks/baselines/<device>.json` (e.g. `pixel4.json`), and a
phone is only ever compared with its own baseline. Build: `cmake --preset android-arm64 -B build\android-release
-DCMAKE_BUILD_TYPE=Release`, then `cmake --build build\android-release --target wlw_benchmarks`. Run: `adb push` the binary,
`tests/data/LilitaOne-Regular.ttf` and the device's baseline to `/data/local/tmp/wlw/`, then
`adb shell "cd /data/local/tmp/wlw && taskset <mask> ./wlw_benchmarks --data . --baseline <device>.json"`. Pin to one fast
core, or the scheduler moves it between core types (Pixel 4: `taskset 80` = the prime core; unpinned runs differ ~17%).
Git Bash needs `MSYS_NO_PATHCONV=1` for adb paths. The emulator runs on the PC's x86 CPU: its numbers don't count.
Firebase Test Lab (roadmap 6.6) is the planned way to get numbers from phones the owner doesn't have.

## Profiling
Build with a profile preset: `windows-profile` or `android-arm64-profile` (RelWithDebInfo = optimized with symbols,
`WLW_PROFILE=ON`, debug UI on). A game's own build passes `-DWLW_PROFILE=ON` and RelWithDebInfo the same way.
- Desktop: run the game, then the viewer `tracy-profiler.exe` from Tracy's GitHub release **v0.14.1** (it must match
  `third_party/tracy`) and connect to localhost. Command line: `tracy-capture -a 127.0.0.1 -o run.tracy -s 5`, then
  `tracy-csvexport run.tracy` (`-g` GPU zones, `-m` messages, `-u -p` plots).
- Android: the app needs the INTERNET permission (add it in the game's profile build only), then
  `adb forward tcp:8086 tcp:8086` and connect the viewer to localhost. ATrace sections and counters show in a
  Perfetto / Android Studio system trace.
- Nothing is recorded until a viewer connects (TRACY_ON_DEMAND); zones cost a branch until then. Startup is ~0.5 s
  longer (timer calibration).
- Zones (all with WLW_PROFILE_*): Application phases + each system's hook named after its type, renderer stages,
  program compiles, input polling, audio callback + decode thread, particles, file/image/glTF loads. Plots: Frame us,
  draw calls, program changes, texture binds, instance bytes, mixer voices, quality level, GPU us. Log lines go to the
  timeline while connected.
- Desktop NVIDIA: the driver thread makes the frame's first glGet wait (frame pacing). It shows in
  `tracy::GpuCtx::Collect` (GPU zones on) or "Screen framebuffer" (off); not a real cost.

**Don't add features to the samples** (owner's rule): engine features are shown and verified
through Bluzzle and the engine tests. Snake only has to keep compiling.
Samples build only when the engine is the top-level project (`WLW_BUILD_SAMPLES`), so a game
that uses the engine as a submodule doesn't compile them. Snake is a quick check that engine
changes didn't break anything; keep it building when the engine API changes.

## Known gaps / tech debt
- The glTF loader still reads files with `fopen`, so glTF models don't load from APK assets yet.
- Only one directional light casts shadows (point/spot shadows need cube/perspective maps).
- glTF materials ignore baseColorFactor (materials have no colour yet; it used to go into the removed per-material light).
- Node world bounds (GetAABB) are recomputed once per frame per node, not cached with dirty flags; render lists are
  rebuilt every frame (no cached draw commands for static nodes, unlike Unreal). Fine for hundreds of nodes.
- No shader program cache on disk yet (glProgramBinary): every launch compiles its variants (~25-60 ms on the emulator).
- A viewport's default camera is an `FPSCamera`; games normally replace it with `SetCamera()`.
- Shadows: one shadow map shared by every viewport (each viewport's shadow pass redraws it).
- Touch input uses only the first finger (no multi-touch yet).
- GPU time needs EXT_disjoint_timer_query on phones (Pixel 4 / Adreno 640 has it; the emulator doesn't). Phone
  clocks scale with load (DVFS): light frames read slower, so compare GPU ms between levels only roughly.
  2D is post-processed only in Picture canvas layers.
- Quality: no thermal input yet (AThermal_getThermalHeadroom, API 31+). Without a GPU timer it only steps down
  (vsync hides headroom), and that saved level never goes back up. Default budget is 60 fps (90/120 Hz games set budget_ms).
- Textures free their CPU pixels once uploaded, so a lost GL context (if the platform ever loses it) can't re-upload.
