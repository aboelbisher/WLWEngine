# WLWengine: notes for Claude Code

C++20 / OpenGL game engine, built as a CMake static library (`wlw::wlw`). Targets Windows,
macOS, Linux and Android today; iOS is planned. Games live in **separate repos** and include
this repo as a git submodule. The current game is Bluzzle
(github.com/aboelbisher/Bluzzle). Its `CLAUDE.md` holds the overall project context, the
roadmap and the owner's working preferences: read it too when working from there.
`samples/snake/` is the first game (Snake), kept here as a sample and engine smoke test.

Remote: `git@github.com:aboelbisher/WLWEngine.git`, branch `main`.

## Layout
```
engine/wlw/
  audio/      Audio (miniaudio device: WASAPI / AAudio...) + Mixer (decoded PCM, lock-free command queue,
              voices with pitch/pan, per-sound caps, buses). Application::Audio(); silent in background.
              Music: Audio::PlayMusic streams a file (decode thread -> lock-free ring -> Mixer stream slots with crossfades)
  anim/       ease.h (Penner easing curves), tween.h (Tween<T>: value-type tweens the game owns and samples),
              shake.h (trauma-based screen shake: trauma decays, offset = trauma^2 x smooth noise)
  fx/         ParticleSystem: CPU simulation (fixed capacity, swap-remove, bursts, Emit = one exact particle,
              optional Floor rectangle they bounce on and fall past) -> instances
  debug/      DebugUI: Dear ImGui (third_party/imgui) fed from Input, drawn via Window::SetOverlay; games add
              panels in Application::OnDebugUI() (#if WLW_DEBUG_UI). F1 / "Debug" button. Off in release builds
  app/        Application (game loop: OnStart/OnFixedUpdate/OnUpdate/OnRender; SetTimeScale = hit-stop / slow motion
              on game time via core/time_scale.h), entry_point.h (WLW_MAIN),
              Screen + ScreenStack (game states: deferred push/pop/replace, overlays, fades, Back routing)
  core/       math types, FixedTimestep, Random (SplitMix64: deterministic everywhere, state saveable), log.h (WLW_LOG_*), logger.h (FAIL_IF), mesh/model, collision.h (AABB, Ray, Plane, IntersectPlane)
  input/      Input: keys (pressed vs held), pointer (mouse/first finger), swipes, taps
  math/       ExactCover: Knuth's Algorithm X with Dancing Links (exact cover search, counting, hints);
              BitGrid<W,H> (bitboards: a grid as 64-bit words, fits = one AND); game_search.h (Minimax with
              alpha-beta, BestMove, Expectimax; a game = small class with Moves/Apply/Evaluate/MaxToMove);
              gf2.h (linear algebra over GF(2): BitVector, SolveGf2 = Gaussian elimination with null space,
              LightestSolution: Lights Out-style "which toggles clear it" problems)
  platform/   THE ONLY PER-OS CODE: platform.h interface + glfw/glfw_platform.cpp (desktop)
              + android/android_platform.cpp (NativeActivity + EGL). Also save files: GetSaveDirectory, WriteFileAtomic, ReadLocalFile. iOS goes here later.
              Native bridge (bridge.h/.cpp): BridgeCall(service, method, json) C++ -> Kotlin; Kotlin posts back
              (WlwBridge.post) -> queue -> SetBridgeHandler callbacks on the GAME thread (Application dispatches
              once per frame). Kotlin side: android/java/com/wlw/ (WlwBridge, WlwService, WlwActivity = NativeActivity
              + services; games subclass it and add its java dir to their Gradle sourceSets).
  rendering/  RenderingDriver/RenderDevice interfaces + one GL backend for GL 3.3 core AND GLES 3.0. Per frame: gather lights +
              drawables, shadow pass (depth from the shadow light into a 2D depth texture), main pass (up to 4 lights, PCF shadows)
              Custom shaders (shader.h): one text (shader_type canvas|mesh; fragment(), optional vertex()) -> RenderDevice::CreateShader(code) -> ShaderMaterial::Create(shader)
              + SetParam(name, float/vec/mat4/texture); node/mesh SetMaterial or Canvas::ShaderRect. Engine inputs are prepended
              (documented in shader.h). Data textures: CreateTexture2D(w, h, {R8/RGB8/RGBA8/R16F/RGBA16F}) + WTexture2D::Update
              Blending (blend_mode.h): Material::blend = Opaque / Alpha / Additive / Multiply (ShaderMaterial too), depth_write
              (default: only Opaque writes depth). Every shader outputs PREMULTIPLIED alpha, so gl_blend.h has one table for
              3D, canvas and custom shaders. Render queues (render_queue.h): each viewport draws opaque meshes first, then
              transparent ones sorted back to front (SortBackToFront, by bounding-box centre along the view direction),
              depth-tested but not written. An opaque material forces alpha 1 (uniform `opaque`).
  scene/      Node (scene graph, local transforms, visibility, instancing: SetInstances = one draw call for N copies).
              Scene (scene.h) = the world: nodes, lights, skybox; owned by nobody who looks at it. Viewport (viewport.h) =
              "draw this Scene from this camera into this picture" + a 2D Canvas over it (like Godot's SubViewport):
              Viewport::Create(device, {format, depth, smooth, clear}) draws into a RenderTarget (rendering/render_target.h,
              an FBO; RGBA16F falls back to RGBA8 if the GPU can't), GetTexture() -> any ShaderMaterial; SetSizeFromScreen /
              SetSize; UpdateMode Always / Once / Disabled. Several viewports may share a Scene.
              Window = surface + input + GetScreen() (its root viewport: scene, camera, the game UI canvas) +
              AddViewport (offscreen, drawn before the screen in the order added) + overlays (debug UI).
              The driver: DrawWindow -> DrawViewport(each) -> gather, shadow pass, main pass, canvas.
              cameras (FollowCamera = perspective, OrthographicCamera = parallel/top-down;
              Camera3D::ScreenPointToRay for picking, fed with Input::SurfaceSize()).
              Lights are nodes (light.h: DirectionalLight / PointLight / SpotLight, added with AddNode); nodes opt in to
              SetCastsShadow / SetReceivesShadow; shadow_map.h fits the light's box to the casters (texel-snapped)
  ui/         2D UI layer: Font (TTF -> SDF atlas, stb_truetype), Canvas (virtual units, safe area, text,
              rounded rects, batched quads, ShaderRect = a rect drawn by a material's shader), UIRenderer (GL).
              Canvas::Image(rect, texture, ImageStyle{tint, uv, blend}) = sprites (batched by texture + blend: 1000s = 1 draw
              call). Blending belongs to what you draw with: Image's style, ShaderRect's material (canvas shaders default
              Alpha, mesh shaders Opaque), UI shapes Alpha; no canvas-wide state. Canvas = primitives; shapes come from helpers.
              Canvas::Button/Toggle/Panel = immediate-mode widgets,
              layout.h (Centered, Inset, Row, Column). Games draw in Application::OnRenderUI() (the canvas is
              Window::GetScreen().GetCanvas(); an offscreen viewport's canvas is drawn into its texture)
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
tests/        engine unit tests (no window/GPU: blending (depth-write rule, queues, back-to-front, canvas batches, images, falloff texture), viewports (scenes, sizes, update modes), anim, audio, core (random, atomic files, bridge queue), exact cover, search (bitboards, minimax, expectimax), gf2 (Lights Out), shader text splitting, lighting (light nodes, shadow-box fitting), fx, input, kit, picking, primitives, ui, screens); test data in tests/data/, also top-level only; ctest
third_party/  glad (desktop only), stb, tinygltf + nlohmann json (also its own target wlw_json), miniaudio 0.11.25, stb_vorbis, Dear ImGui 1.92.9b (vendored)
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
- **Logging:** use `WLW_LOG_INFO/WARN/ERROR`, not `std::cout`, which goes nowhere on Android.
  Android logcat tag: `WLW`.
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
ctest --test-dir build\windows-debug --output-on-failure
```
**Don't add features to the samples** (owner's rule): engine features are shown and verified
through Bluzzle and the engine tests. Snake only has to keep compiling.
Samples build only when the engine is the top-level project (`WLW_BUILD_SAMPLES`), so a game
that uses the engine as a submodule doesn't compile them. Snake is a quick check that engine
changes didn't break anything; keep it building when the engine API changes.

## Known gaps / tech debt
- The glTF loader still reads files with `fopen`, so glTF models don't load from APK assets yet.
- Only one directional light casts shadows (point/spot shadows need cube/perspective maps). A material's own
  `Lighting` is the old way, used only when the scene has no light nodes (Snake).
- A viewport's default camera is an `FPSCamera`; games normally replace it with `SetCamera()`.
- Shadows: one shadow map shared by every viewport (each viewport's shadow pass redraws it).
- Touch input uses only the first finger (no multi-touch yet).
