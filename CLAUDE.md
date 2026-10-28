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
              Music: Audio::PlayMusic(path, MusicParams{fade, volume trim, intro, restart}) streams a file (decode thread ->
              lock-free ring -> 4 Mixer stream slots, equal-power crossfades; gapless intro -> loop; restarted playthroughs
              freed when StreamSource::InUse() is false). Live mix: each bus mixed into its own buffer, then shaped:
              SetBusLowPass / SetBusHighPass (filter.h: TPT state-variable filter, cutoff glides in octaves, bypassed
              when open), SetBusGain (dB glide), Duck(bus, DuckParams) (deepest wins); Levels(bus) for panels
  anim/       ease.h (Penner easing curves), tween.h (Tween<T>: value-type tweens the game owns and samples),
              shake.h (trauma-based screen shake: trauma decays, offset = trauma^2 x smooth noise),
              timeline.h (Timeline: beats placed Then / With / At on one clock; cues polled with Crossed; Skip),
              spring.h (Spring<T>: damped spring by frequency + damping ratio, exact step: Kick, SnapTo, AtRest)
              curve.h (Curve: Hermite keys, Smooth (Catmull-Rom) / Linear / Constant, Evaluate exact, Sample from a 64-point
              baked table), gradient.h (Gradient: colour stops, Linear / Constant, baked the same way); Unity AnimationCurve /
              Gradient, Godot Curve / Gradient; bezier.h (CubicBezier<T>: Point / Tangent / Through(from, to, bend);
              ArcLength<T, N>: N + 1 even-t samples, TimeAt(fraction of the length) -> t for an even or eased pace)
  fx/         ParticleSystem: CPU simulation (fixed capacity, swap-remove, bursts, Emit = one exact particle,
              optional Floor rectangle in XY at a height in z they bounce on and fall past; gravity -Z; BurstSettings::spread /
              max_spin and Particle::angles / angular_velocity in radians); WriteTo(MultiMeshInstance3D&) = WriteInstances, rows of
              T*Rx*Ry*Rz*S in closed form with core::SinCos (5k: ~120 us MSVC Release). WriteTo(BillboardBatch&) = 40 B quads
              (stretch = velocity x stretch.velocity_scale, capped at max_length; rotation = angles.z; bounds fitted). System-wide
              size_over_life / intensity_over_life (anim::Curve) + color_over_life (anim::Gradient), sampled at LifeFraction;
              Particle::intensity / BurstSettings::intensity (HDR, billboards only). Shockwaves (shockwaves.h): up to
              kMaxRipples rings, Add(center, ShockwaveSettings) (a full set replaces the wave nearest its end), Update(dt),
              WriteTo(PostSettings&) = every ripple slot; ShockwaveAt(center, settings, age) = the shape (radius OutCubic,
              width linear, push fades in over 10% of the life and out as (1 - t)^2). Trail (trail.h): the head's last
              `lifetime` seconds of points (fixed capacity, min_distance), Write(span<BillboardInstance>) = one stretched
              quad per segment, head first, tail cut at exactly `lifetime`, width / intensity / colour tapering along
              it. FlowParticles (flow_particles.h):
              10-20k dots along paths (liquid in pipes; not a fluid sim), SoA arrays, paths resampled evenly (O(1)
              lookup), SetNext routing (round robin; none = fade out), SetStop = a gate dots queue behind (the line
              grows back up the path), emitters; WriteTo(SpriteBatch) packs 16 B/dot + bounds. SetDensity(0..1) scales emitters + queue packing (count only;
              the game sets dot_size, e.g. x 1/density^0.25; queues track their length, no gap on a change). Emulator, -O2, 20k dots:
              Update ~30 us, WriteTo ~95 us; Pixel 4 (-O2): Update ~195 us, WriteTo ~950 us (the phone's real cost)
  debug/      DebugUI: Dear ImGui (third_party/imgui) fed from Input, drawn as a window overlay (Window::Internal().AddOverlay); games add
              panels in System::OnDebugUI() (#if WLW_DEBUG_UI); the engine's own window is debug::EnginePanel (a
              System the Application adds first) then debug::CVarPanel (every cvar by prefix, typed widgets, reset).
              F1 / "Debug" button. Console (console_window.h, owned by DebugUI): ` key / "Console" button: the log
              (level + text filters), an input line with history and Tab completion (ConsoleModel = the testable
              logic); phones: no typing yet (GameTextInput, D9). ImGui gets everything from Input (imgui_input.h: every
              key by the layout's key, in order, + modifiers; 5 buttons, both wheel axes, focus) and its cursor shapes go to
              the window (Window::Internal().SetCursorOverride) while it has the mouse; while it wants text input the
              game's keyboard is blocked. Engine panel's Input section: held keys / buttons / wheel, cursor shapes, a 5 s
              cursor lock. Off in release builds
  app/        Application (final: owns the services, the RenderDevice + RenderingDriver (no separate engine object; the renderer keeps the window and does the GL shutdown order) and runs the frames; see the frame in application.h): Window(),
              Input(), Audio(), Screens(), Time(), Config(). engine_cvars.h: log.filter, r.gpu_timing,
              r.invalidate_framebuffers, r.target_fps, r.pacing.auto_interval, r.render_scale, quality.lock (Run applies the cvar sources first, wires these to the window /
              log / quality, saves changed Saved cvars at the end of a frame). Games add Systems (system.h: always running,
              like Unreal subsystems / Godot autoloads; hooks OnStart/OnFixedUpdate/OnUpdate/OnRender/OnRenderUI/
              OnDebugUI/OnBack/OnAppPaused/OnAppResumed/OnShutdown, run in the order added, then the screens'; App()
              = the services) and push Screens. SystemList = the testable dispatch. Time (time.h): Delta, RealDelta,
              FixedStep, Alpha, SinceStart, SetScale (hit-stop / slow motion via core/time_scale.h), Fps; filled by
              FrameClock (frame_clock.h: fed seconds, clamp, scale, fixed steps, restart after a resume; tested).
              The frame loop is three calls, Internal().Init() / Frame() / Shutdown() (entry points, editor, tests;
              application.h says which platform drives frames how); Run() = those (desktop + Android via WLW_MAIN).
              Checks: Frame before Init / after Shutdown = false; Init twice, Frame inside a frame, Shutdown from a
              hook refused; one thread; the destructor shuts a running app down. Quit(exit_code) -> Run's return.
              Save in OnAppPaused: OnShutdown isn't guaranteed on iOS / web.
              entry_point.h: WLW_MAIN(make_app) with the game's MakeApp() (builds the Application, adds its systems),
              Screen + ScreenStack (game states: deferred push/pop/replace, overlays, fades, Back routing; Screen::App()
              = the services, protected, valid from OnEnter). AppConfig = name (save folder + default window title) +
              groups window{title, size, stats_in_title, visible} / graphics{target_fps, screen_depth} / audio{enabled,
              max_voices} / time{fixed_step, max_frame_time} / ui{reference_size}; ResolveAppConfig (tested) fills the
              defaults (window title = name)
              Quality (quality.h, App().Quality(); like Unity QualitySettings + Adaptive Performance): the game defines
              N levels (Configure, OnChange = apply a tier; Lock/Unlock for menus/tests), the engine picks one: GPU ms
              (timer queries, turned on by Configure) or late frames > budget x down_at / slow_frame_at for 2 s = down,
              GPU < budget x up_at for 10 s = up (wait doubles per failure of that level, max 8x), 3 s settle after a
              change/resume, readings smoothed ~0.5 s and clamped at 2 budgets; no GPU timer = down only; level saved
              in <save dir>/quality.txt. Fed by Application after each frame (UpdateQuality). Engine panel: level, -/+/Auto.
              Budget (BudgetMs) = settings' budget_ms, or 0 (default) = the frame time of the paced rate (SetTargetFrameMs).
              QualitySettings::render_scales = the screen's render scale per level (Quality::RenderScale; the Application
              sets it on the screen when it changes, just before drawing; empty = left to the game)
              Frame pacing: AppConfig::graphics.target_fps (60; 0 = display rate, -1 = uncapped) / App().SetTargetFps / cvar
              r.target_fps -> Window::SetTargetFps (platform: Swappy on Android, vsync interval or limiter on desktop;
              core/frame_pacing.h). Window: PacedFps, RefreshRate, PacingMethod (engine panel: target buttons + these)
  version.h   GENERATED from project(VERSION) (template version.h.in): WLW_VERSION_MAJOR/MINOR/PATCH, WLW_VERSION_STRING,
              WLW_VERSION (major*10000+minor*100+patch), WLW_VERSION_AT_LEAST(maj, min, patch)
  core/       math.h (wlw::Vec2/3/4, IVec, Quat, Mat3/4 = glm; kPi/kTau/kDegToRad; Lerp, InverseLerp, Remap, Saturate, SmoothStep, MoveTowards, ExpDecay, WrapAngle/AngleDifference/LerpAngle, SafeNormalize, Perp, Cross(Vec2), Rotate(Vec2), DistanceToSegment; core::SinCos = one reduction + Cephes polynomials, 1.2e-7, branchless quadrants; glm config in third_party/CMakeLists.txt: GLM_ENABLE_EXPERIMENTAL, CTOR_INIT off = default Vec3/Mat4 uninitialised, measured), color.h (wlw::Color = floats as authored (sRGB-encoded, converted when 13.1 shades linear): Hex/HexRgba constexpr, Parse/ToHex, WithAlpha replaces, Faded multiplies, Darkened/Lightened, ToLinear/FromLinear exact curve, operators; Color32 RGBA8 Packed/FromPacked, UnitToByte; colors::kWhite...), geometry.h (wlw::AABB, Sphere, Ray, Plane, Frustum, Rect (= ui::Rect); Overlaps(a, b) incl. conservative frustum tests; Raycast(ray, plane/box (slab)/sphere/triangle (Möller-Trumbore)) = distance, 0 inside; TransformBox = Arvo: exact box of a moved box; Union), FixedTimestep, frame_pacing.h (PlanPacing(target, refresh) = vsync interval or limiter,
              2% tolerance; FrameLimiter = fixed schedule, exact average, restarts after a stall), Random (SplitMix64: deterministic everywhere, state saveable), log.h (WLW_LOG(Category, Level, fmt...): levels Verbose..Fatal, categories LogCore/App/Render/Shader/Audio/Platform/UI/FX (+ kit LogKit, games WLW_LOG_CATEGORY), compile-time stripping, SetLogFilter("Render=Verbose,*=Warning") / env WLW_LOG, sinks: platform, RecentLogs ring (512), StartLogFile (debug builds: <save dir>/log.txt, previous run log.txt.prev), AddLogSink -> core::Connection; LogThreadId), assert.h (WLW_ASSERT debug-only fatal, WLW_VERIFY always evaluated: logs + returns false, WLW_UNREACHABLE; SetAssertHandler for tests), cvar.h (CVar<bool/int/float/std::string>{"r.name", default, help, flags Saved/ReadOnly/Cheat, range}: dotted lowercase names; sources in order code default < wlw.cfg (assets) < <save dir>/cvars.cfg (Saved, written when changed) < command line +name value / Android --es wlw "a=1;b=2" < console; unknown names wait for registration; OnChange -> core::Connection; game thread), connection.h (Connection: copyable handle, Disconnect/Connected, safe after the source died; ScopedConnection = RAII; CallbackList<Args...> = one-thread list, safe to change while calling), console.h (ConsoleCommand RAII, ExecuteConsoleLine, built-ins help/find/set/reset/exec (+quit from Application), CompleteConsoleName, ConsoleModel), profile.h (WLW_PROFILE_SCOPE/FUNCTION/SCOPE_DYNAMIC/TEXT/FRAME/PLOT/THREAD_NAME/MESSAGE -> Tracy + Android ATrace; only with the CMake option WLW_PROFILE, else empty with unevaluated arguments; one zone per scope; see "Profiling"), type_name.h (TypeName<T>() at compile time: "wlw::app::System"; System::Name() for zones), build_info.h (GetBuildInfo: version, git commit + dirty, build type; BuildInfoText logged at startup and in the engine panel; cmake/GitInfo.cmake), deprecated.h (WLW_DEPRECATED(0.4, "Use X") shims: none today, the tool for later; WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN/END, option WLW_NO_DEPRECATED; see "Versioning"), normal_matrix.h (NormalMatrix(mat3/mat4) = cofactor normal matrix, sign-fixed for mirrors), rotation.h (EulerOrder,
              QuatFromEuler, EulerFromQuat (default kDefaultEulerOrder = ZXY: yaw, pitch, roll), LookRotation(forward, up) = +Y to
              forward, +Z towards up, with a fallback up), axes in math.h (kRight/kForward/kUp/..., kModelFront = -Y, FromYUp/ToYUp), mesh/model
  input/      Input: keys (pressed vs held; OS repeat ignored) = the full keyboard BY POSITION (D18: US names, Digit0-9,
              F1-F12, punctuation, numpad, left/right modifiers); HeldModifiers() mask, Modifiers::Primary = Ctrl / Cmd (macOS);
              WasShortcutPressed(key, mods) = by the LAYOUT's key (the platform reports both: OnKey(key, down, layout_key);
              KeyFromCharacter), exact mods; KeyName / KeyFromName / ShortcutName ("Ctrl+S" / "Cmd+S"); RawKeyEvents (in
              order); Text() (typed code points); pointer (mouse left button / first finger), PointerDelta (finger: only
              while touching), swipes, taps; MouseButton x5, ScrollDelta (notches, +x right, +y away); pointer and keyboard
              can be blocked (debug UI; blocking hides buttons / wheel / delta too), Raw* queries ignore that. Android:
              full key map + wheel, no mouse buttons yet (7.1)
  math/       ExactCover: Knuth's Algorithm X with Dancing Links (exact cover search, counting, hints);
              BitGrid<W,H> (bitboards: a grid as 64-bit words, fits = one AND); game_search.h (Minimax with
              alpha-beta, BestMove, Expectimax; a game = small class with Moves/Apply/Evaluate/MaxToMove);
              gf2.h (linear algebra over GF(2): BitVector, SolveGf2 = Gaussian elimination with null space,
              LightestSolution: Lights Out-style "which toggles clear it" problems)
  platform/   THE ONLY PER-OS CODE: platform.h interface + glfw/glfw_platform.cpp (desktop)
              + android/android_platform.cpp (NativeActivity + EGL). Init/Shutdown are counted (each renderer holds one). Also save files: GetSaveDirectory, WriteFileAtomic, ReadLocalFile. iOS goes here later.
              LaunchArguments() (launch_arguments.cpp; desktop argv via WLW_MAIN, Android the intent's "wlw" extra).
              Frame pacing: desktop = vsync interval when the target divides the monitor's refresh (the monitor the
              window's centre is on, re-checked on moves), else vsync off + FrameLimiter + PreciseSleep (glfw_sleep.cpp:
              high-resolution waitable timer, then a 1 ms spin); hidden windows unpaced. Android = Swappy
              (third_party/swappy: SwappyGL_swap, swap interval = 1e9/target, auto swap interval off unless
              r.pacing.auto_interval, auto pipelining on); without EGL_ANDROID_presentation_time: vsync +
              ANativeWindow_setFrameRate (dlsym, API 30) + the limiter, re-planned when the display's mode changes.
              Swappy's Java classes (third_party/swappy/java) go in each game's Gradle java.srcDirs + R8 keep rule
              (-keep class com.google.androidgamesdk.** { *; }); without them Swappy can't start on Android 9+.
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
              Sky (sky.h): drawn between the opaque and transparent draws (SkyDrawPosition; first without a depth buffer),
              one full-screen triangle at depth 1.0, depth test LEQUAL around that draw only, rays from SkyRayMatrix.
              GLStateCache (gl_state_cache.h) skips redundant program / blend / depth-write / texture calls; Reset per viewport
              (UI and ImGui change state too). Lists are reused every frame (no allocations once warm). RenderStats has program
              changes, texture binds, skipped calls and CPU gather / sort / submit us (engine panel). Measure with -O2: a debug
              (-O0) Android build is ~10x slower (Bluzzle: ~330 us gather at -O0, 24 us at -O2).
              SHADERS are files: rendering/shaders/** (mesh, shadow, skybox, ui .vert/.frag; wlw/frame.glsl, lighting.glsl,
              shadows.glsl, common.glsl, color.glsl, instance.glsl; custom/* = what wraps a game's shader), embedded at build time by
              cmake/EmbedShaders.cmake (shader_library.h: FindShaderFile, AddShaderFile for a game's own includes).
              shader_preprocessor.h: #include (once, cycles reported), #defines, #line + MapShaderLog (errors name file:line).
              gl_program.h: BuildProgram / BuildEngineProgram = the one way programs are made (binds the frame block).
              glsl_header.h: GlslHeader(dialect, stage) = #version + GLES default precisions (highp floats; every sampler
              type defaulted), ApplyMediumpSetting (cvar r.mediump 0 = shaders' `precision mediump float;` opt-ins
              off), CurrentGlslDialect() from the context; Window::FragmentPrecision() (log + engine panel).
              Per-frame UBO WlwFrame (frame.glsl) <-> FrameUniforms (frame_uniforms.h, std140 offsets tested): camera,
              lights by type (up to 4 directional + 4 point + 4 spot; SetFrameLights), shadow params.
              Variants (mesh_variant.h): INSTANCED, TEXTURED, LIT, SHADOWS, BLEND as #defines, one cached program per
              key; RenderingDriver::WarmUp (called by Application after OnStart) compiles what the scenes need, hidden and
              empty-instanced nodes included; later compiles are logged ("compiled during play"). Normals: cofactor matrix
              (core/normal_matrix.h NormalMatrix = cross products of the columns, negated when det < 0 so mirrored nodes
              stay lit outward; no inverse: finite at scale 0), once per node in CullScene -> uNormalMatrix for every mesh
              draw; instanced copies multiply in their own NormalMatrix() per vertex (wlw/common.glsl, product rule).
              Test "gpu: normals match inverse-transpose" = transformed vs CPU-baked pictures. Back-face culling is never on.
              NO DEVICE IN GAME CODE (gpu_resource.h): textures (WTexture2D::Create / FromImage, WCubemap::Create), shaders
              and render targets are plain CPU objects made with their own Create (no GL context: usable in tests), like
              Mesh; the renderer makes their GPU side on first use (gl_resources.h: GLTexture, GLShaderFor, GLFramebuffer)
              and WarmUp prepares every alive one (Resource::InternalAccess::ForEachAlive: an intrusive list, filled by Resource's constructor). RenderDevice = mesh buffers, engine-internal.
              Post passes (post_pass.h): Viewport::AddPostPass({material (shader_type post), scale, format, name, inputs});
              with passes the 3D draws into the scene picture, PlanPostChain (CPU, tested) ping-pongs pooled targets, the
              last pass writes the destination (screen or viewport texture), then the Overlay canvas layers (UI not post-processed).
              A pass's format nullopt = the scene picture's. HDR: Viewport::SetFormat(RGBA16F) (screen: 3D into an HDR scene
              picture, the surface stays 8-bit; offscreen: its texture follows), SetFormatOverride / cvar r.hdr, RGBA8
              fallback (RenderStats::picture_format). Post settings as data (post_settings.h, Post().Settings(), Godot
              Environment / Unity Volume shape): tone_map {Clamp | Shoulder (PBR Neutral's shoulder, no toe: we shade gamma-
              encoded), exposure, dither} -> the OUTPUT PASS (shaders/post/output.glsl, kOutputPass in the plan): after the
              game's passes, writes the destination, only when NeedsOutputPass(); renderer-owned material, params set per draw.
              Also in the output pass (cost: ALU, no extra pass): ripples[kMaxRipples = 4] (Ripple {center 0..1 top-left,
              radius, width, strength, chroma}, viewport heights: the picture under a ring is read from further in, push x
              (1 - x^2)^2 across it; chroma = red / blue pushed 1 +- chroma as far), chromatic_aberration {intensity at the
              corners, growing with distance^2}, vignette {intensity, smoothness, center, color; after exposure, before the
              tone map}. Distorts() also moves the bloom read; inactive ripples (strength 0) don't count for
              NeedsOutputPass. fx::Shockwaves animates ripples (fx/ above). The output pass is compiled in 8 variants
              (Shader::Create defines DISTORT = Distorts(), BLOOM, DISTORTION_BUFFER = the buffer drawn this frame; all made
              at start so the warm-up compiles them: MakeOutputPasses / OutputPass), ring params as vec4 arrays.
              DISTORTION BUFFER (distortion {enabled, scale 0.5, strength, chroma}; Unreal's distortion pass / HDRP's
              buffer): draws whose material has Material::distortion (billboards: BillboardDistortion quad program; mesh
              nodes only with a ShaderMaterial, FragColor.rg = the push; others skipped) leave the main pass for
              distortion_order_, drawn after the Picture layers (DrawDistortionPass) Additive, no depth test, into
              PostChain's RG16F DistortionTarget (scale x render size, made on the first frame that draws) cleared to 0;
              offsets = how far the picture moves, viewport fractions (+y up); the output pass reads uv - offset x strength.
              Off, no output pass or no RG16F (CanDrawDistortion, logged once): the draws are skipped. Frames with no
              distortion draws: no clear, no read (no DISTORTION_BUFFER). Stats: distortion_draws / _width / _height.
              bloom {enabled, threshold, knee, intensity, scatter, scale, max_levels} (cvar r.bloom = SetBloomOverride):
              steps kBloomDownPass levels 0..n-1 (shaders/post/bloom_down.glsl: Jimenez 13-tap; level 0 also soft-knee
              threshold per tap + Karis average per box) then kBloomUpPass n-2..0 (bloom_up.glsl: 3x3 tent of the level
              below, mix(own level, it, scatter)), each writing a target from the plan's pool; the output pass adds
              uBloom x intensity BEFORE exposure / tone map. Levels: BloomLevelSizes (scale x viewport, halved rounding
              up, stop at max_levels / 16 px). Renderer-owned materials (one per shader, a PostPass per level for profiler
              names). A moving spark's glow stays within 20% (GPU probe); the non-linear parts are threshold and Karis.
              Ready-made (scene/post_effects.h, shaders/post/*): AddBlur (separable 9-tap Gaussian in 5 reads), AddThreshold (density ->
              solid shapes coloured rgb/a: soft additive dots -> AddBlur -> AddThreshold = metaballs/liquid; keep the
              viewport AND the blur RGBA16F, RGBA8 clips colour x density and the middles go white). Blur::SetScale +
              PostChain::SetScale change resolution at runtime (passes found by material; re-planned, targets resized).
              Render scale (Viewport::SetRenderScale 0.25-2, like Unity URP renderScale / Godot scaling_3d_scale): the 3D +
              Picture layers go into the scene picture at RenderSize (viewport size x scale), stretched bilinearly to the
              destination by the last post pass (free) or, without passes, one copy step (kUpscalePass in PlanPostChain);
              Overlay layers after that at full size (UI sharp). Pass sizes stay fractions of the VIEWPORT's size (an effect
              looks the same at every scale: our blur kernels are in texels); the camera's aspect = the viewport's; capped
              at GL_MAX_TEXTURE_SIZE. Screen: QualitySettings::render_scales, cvar r.render_scale = SetRenderScaleOverride
              (wins while > 0), engine panel buttons. RenderStats::render_width/height. RTX 5080, 2560x1440, 24 blended
              lit layers: 3D 2.20 ms at 1, 1.21 at 0.75, 0.53 at 0.5 (= the pixel count); the upscale copy 0.017 ms.
              AppConfig::graphics.screen_depth / WindowOptions::depth = false: no depth buffer on the screen surface (memory saved
              when the screen is always scaled / post-processed); the screen then always draws through its scene picture.
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
              Custom shaders (shader.h): one text (shader_type canvas|mesh|post; fragment(), optional vertex()) -> Shader::Create(code, name,
              defines = a variant: #defines before the text, ShaderSource::defines -> BuildProgram) or Shader::FromFile(path) -> ShaderMaterial::Create(shader)
              + SetParam(name, float/vec/mat4/texture/vector<float>/vector<vec4> (uniform arrays from element 0); uploaded only when the
              material (id, version) changes); node/mesh SetMaterial or
              Canvas::ShaderRect. Per-node values: `instance uniform float uX;` in a mesh shader (float/vec2-4, max 8, parsed in
              SplitShaderSource) + GeometryInstance3D::SetInstanceParam (node > material > 0, uploaded per draw when changed). Engine inputs are prepended
              (documented in shader.h; may #include wlw/lighting.glsl and use SceneLighting, wlw/shadows.glsl and SceneShadow; mesh shaders get uNormalMatrix
              and, in vertex(), WorldPosition(local) / WorldNormal(normal) = the engine's transforms). Data textures: WTexture2D::Create(w, h, {R8/RGB8/RGBA8/R16F/RGBA16F}) + Update (copied, uploaded before the next draw)
              Blending (blend_mode.h): Material::blend = Opaque / Alpha / Additive / Multiply (ShaderMaterial too), depth_write
              (default: only Opaque writes depth). Every shader outputs PREMULTIPLIED alpha, so gl_blend.h has one table for
              3D, canvas and custom shaders. Opaque meshes draw first, then transparent ones back to front (by bounding-box
              centre along the view direction): both come from the draw list's sort keys (render_list.h); transparent ones
              are depth-tested but not written. An opaque material writes alpha 1 (no BLEND variant).
  scene/      Node3D (node.h + node_3d.cpp; a plain class, no 2D nodes): position + Quat + scale, local (T*R*S) and world
              matrices cached, a change marks the subtree dirty (stops at marked nodes; invariant dirty => descendants
              dirty); GetWorldMatrix (const ref), SetEulerAngles/GetEulerAngles (core/rotation.h EulerOrder, default ZXY =
              Rz*Rx*Ry; the angles set are kept as given), Rotate/RotateWorld, Translate (own axes)/TranslateWorld,
              LookAt, world getters/setters, Forward/Right/Up (+Y / +X / +Z). Children = vector of shared_ptr in order (AddChild,
              MoveChild, RemoveChild(node), RemoveFromParent, Reparent(keep_world); loops refused), raw parent; names
              (FindChild, GetNode("A/B/..")), random 64-bit ids (SetId, Scene::FindById), layers mask vs
              Camera3D::SetCullMask (CullScene). Scene::Root() holds the top-level nodes; ForEachNode = every node,
              depth first. Node3D draws nothing (280 B); node TYPES (Godot's tree) each set a 1-byte NodeKind in their
              constructor and the gather switches on Kind() (no virtual call per node): Node / MeshInstance / MultiMeshInstance / BillboardBatch /
              SpriteBatch / Light / Camera. geometry_instance.h: GeometryInstance3D = model + material (GetModel/GetMaterial
              const refs; the mesh's material wins) + SetCastsShadow / SetReceivesShadow (default off) + LocalBounds /
              WorldBounds (Arvo TransformBox per mesh / of the instance bounds; nothing to draw = 0.1 box at the node) +
              HasGeometry; MeshInstance3D = drawn once; MultiMeshInstance3D = one draw call for N copies (the copies live on
              the node, no MultiMesh resource): WriteInstances(count) -> span of rendering::Instance (instance_buffer.h, 52 B =
              the transform's top 3 rows + RGBA8 tint, Godot's GLES3 layout; GLSL twin wlw/instance.glsl: aInstanceRow0..2 at
              locations 4-6, aInstanceTint at 7, InstancePosition / InstanceNormal / InstanceTransform), first min(old, count)
              kept, version++ per write, 0 copies = not drawn; Instances(), ReserveInstances, ClearInstances; InstanceBounds()
              = node-space box of all copies, cached until a write or the model's box changes, SetInstanceBounds(box)
              overrides it (Godot custom_aabb).
              SpriteBatch (sprite_batch.h, a Node3D with its own material): N soft dots in one draw call, 16-byte
              SpriteInstance (xy on the node's z=0 plane, size, RGBA8; an Instance is 52 B), Write(count) fills in
              place, SetBounds/FitBounds = culling box (no per-dot bounds), Additive by default (never sorted). GL:
              GLSpriteBuffer (own VAO, no vertex buffer: corners from gl_VertexID, strip of 4 x instances),
              shaders/sprite.vert/.frag (TEXTURED = the material's texture, else a computed (1-d²)² dot). No shadows/lights.
              BillboardBatch (billboard_batch.h): camera-facing quads anywhere in 3D in one draw call, 40-byte
              BillboardInstance {vec3 position, size, vec3 stretch, rotation, RGBA8, float intensity (HDR)}; billboard.vert builds
              the quad in view space (round: turned by rotation; stretched: along the stretch's on-screen direction, longer by its
              on-screen length); 3D AABB bounds (SetBounds / FitBounds = rendering::BillboardBounds); material = blend (Additive
              default, never sorted) + shape texture (TEXTURED) or the computed dot. GL: GLQuadBuffer = the shared no-vertex-buffer
              instanced quad (GLSpriteBuffer, GLBillboardBuffer); driver: QuadProgramFor(Sprite|Billboard|BillboardDistortion,
              textured), AddQuadItem. A distortion material (Material::distortion): the DISTORTION variant writes offsets
              (texture rg = direction in the quad's axes, a = size; no texture: a lens) x the quad's on-screen half axes
              (vAxes) x color.a x intensity (negative pulls in) into the distortion buffer (post settings above).
              RenderStats::instance_bytes = per-copy data uploaded this frame.
              Scene (scene.h) = the world: nodes, lights, skybox; owned by nobody who looks at it. Viewport (viewport.h) =
              "draw this Scene from this camera into this picture" + a 2D Canvas over it (like Godot's SubViewport):
              Viewport::Create({format, depth, smooth, clear}) draws into a RenderTarget (rendering/render_target.h,
              an FBO; RGBA16F falls back to RGBA8 if the GPU can't), GetTexture() -> any ShaderMaterial; SetSizeFromScreen /
              SetSize; UpdateMode Always / Once / Disabled. Several viewports may share a Scene.
              Window = surface + input + MainViewport() (the screen: scene, camera, the game UI canvas) +
              AddViewport / RemoveViewport(viewport) / Viewports() (offscreen, drawn before the screen in the order added) + overlays (debug UI).
              Window::Create(size, title, {.visible = false}) = a hidden window (desktop): just a GL context.
              Window::SetCursor(Cursor: GLFW 3.4's standard shapes + Hidden), SetCursorLocked (GLFW_CURSOR_DISABLED + raw
              motion; positions forgotten across a change so PointerDelta doesn't jump), HasFocus; phones ignore the cursor.
              The driver: DrawWindow -> DrawViewport(each) -> gather, shadow pass, main pass, post passes, canvas.
              RenderingDriver::ReadPixels(target) = a target's picture as RGBA8, top row first (stalls: tests,
              screenshots); GpuName() = GL_RENDERER.
              Camera3D (camera_3d.h) is a node: looks along its +Y, +Z up on screen; view = inverse of its world transform
              orthonormalized (Gram-Schmidt from the back axis: no scale/shear from parents) and turned into GL's view space; SetPerspective(fov_y radians, near, far) / SetOrthographic(size,
              near, far); the aspect is never stored: GetProjectionMatrix(aspect), GetViewProjectionMatrix, GetFrustum(aspect),
              VisibleSize(aspect, distance), ScreenPointToRay(point, size) / WorldToScreen(world, size) (Input::SurfaceSize());
              fit modes on a frame (size tall, SetFrameAspect wide): KeepHeight (default) / KeepWidth / Contain / Cover; cull
              mask. Default viewport camera: origin, looking along +Y, 60 deg, 0.1-1000. The renderer passes the viewport's aspect
              (SetCameraMatrices) and never writes into cameras. camera_controllers.h: FlyController (FlyInput: move, look,
              boost; pitch clamp, damped velocity), OrbitController (OrbitInput: rotate, pan, zoom; Frame(box), Snap; ortho zoom
              = size), FollowController (offset world/target space, look offset, position/rotation lag): plain objects with input
              structs, ExpDecay smoothing (1 step = 10 steps), tested without a window.
              Lights are nodes (light.h: Light3D -> DirectionalLight3D / PointLight3D / SpotLight3D (SetInnerAngle /
              SetOuterAngle, radians)); geometry opts in to SetCastsShadow / SetReceivesShadow;
              shadow_map.h fits the light's box to the casters (texel-snapped)
  ui/         Two layers, like the big engines (Godot RenderingServer canvas vs Control nodes, Slate draw elements vs UMG):
              A. Canvas = the 2D DRAW LIST only: virtual units (reference size fits the screen), FillRect (rounded), Line,
                 Text (Font: TTF -> SDF atlas; batches hold the Font's shared_ptr until drawn; TextStyle: fill colour or
                 LinearGradient, outline, shadow, glow = a soft SDF layer (params.w softness, Additive by default), all one
                 atlas; an optional GlyphEffect moves / scales / turns / tints each glyph, Godot's CharFXTransform), Image(rect,
                 texture, ImageStyle{tint, uv, blend}) = sprites, ShaderRect (a
                 rect drawn by a material's shader). Batched by texture + blend (1000s of sprites = 1 draw call); UIRenderer
                 (GL) draws it. Blending belongs to what you draw with: Image's style, ShaderRect's material (canvas shaders
                 default Alpha, mesh shaders Opaque), UI shapes Alpha. No widgets, input or screen geometry in it.
                 Shapes: Circle, Triangle (edge distances interpolated, AA; optionally a colour per corner), Arc/Ring (sdArc), all in the one UI shader
                 (ui.frag: each pixel computes only its quad's kind, a flat varying; AA widths from the uv's derivatives by
                 the chain rule, text from the SDF's screen-pixel range = Font::DistanceRange in params.z; a new kind adds a
                 branch, never per-pixel work for the others).
              C. The UI layer: UIContext (ui_context.h: canvas + pointer + safe_area + interactive + active widget + theme +
                 dt + Memory(id) = per-widget floats kept while asked for each frame (NewFrame drops the rest); Application
                 owns one and passes it to OnRenderUI(ui) / Screen::RenderUI(ui)), theme.h (Theme: font + colour roles +
                 radii/depth; the game sets ui.theme), icons.h (Icon enum + DrawIcon: vector icons from Canvas shapes),
                 widgets.h (immediate mode: Pressable/WidgetId/RaisedRect building blocks; Button(variant, icon), IconButton,
                 IconToggle, Switch, SettingRow, LinkRow, Card (mode tiles: caller draws the art), Dialog (scrim, slide-in,
                 close X / tap outside), Segmented, Chip, Badge (round stat + progress ring), Stars; the older font+ButtonStyle Button/Toggle/Panel still work),
                 layout.h (Centered, Inset, Row, Column).
              The UI canvas is Window::MainViewport().GetCanvas(); an offscreen viewport's canvas is drawn into its texture.
              Canvas layers (viewport.h, like Unity Canvas render modes + sort order): AddCanvasLayer({mode, order}); Picture
              = part of the picture (after 3D, before post passes), Overlay = on top after them; GetCanvas() = Overlay 0.
  utils/      image/glTF loaders (glTF turned from Y up: models face -Y), texture cache, primitives.h (CubeMesh, BeveledCubeMesh, SphereMesh, CylinderMesh, GemMesh, PyramidFrustumMesh, PlaneMesh: procedural cube,
              bevelled cube, sphere, cylinder, gem, pyramid frustum, plane = XY ground; Z up, CCW outward faces),
              textures.h (procedural textures: RadialFalloffTexture = a soft (1-d²)² dot for glows, particles, metaballs;
              StarTexture (N tapered rays + a soft core), RingTexture (soft ring at a radius); AlphaShapePixels = the helper;
              RingDistortionTexture = a direction map (rg outwards, y up; a = Ripple's profile) for distortion billboards)
kit/wlw/kit/  wlw::kit: game-level building blocks shared by games (separate target on top of the engine; the
              engine never includes it). SaveStore (JSON file, atomic write, .bak fallback, versions + migrations),
              progress.h (DeviceId, Counters, Scores, LevelProgress: all mergeable, for cloud sync), daily.h (Date,
              Today, DailyLog streaks), cloud_sync.h (interface only),
              ads.h (Ads: AdMob via the bridge on Android, FakeAds on desktop; AdsConfig = test unit ids by default),
              ad_pacing.h (when an interstitial is due). kit/android/java/com/wlw/kit/AdMobService.kt = Kotlin side
              (UMP consent, load/show/retry); games add that dir to their sourceSets and depend on play-services-ads + UMP.
              leaderboards.h (Leaderboards: sign-in state + SignIn(), Submit (waits while signed out), Load(board, span,
              count) -> LeaderboardPage{top, player, players}; Play Games v2 via the bridge on Android, FakeLeaderboards
              on desktop). kit/android/playgames/java/com/wlw/kit/PlayGamesService.kt = Kotlin side (service
              "leaderboards"); its own dir so only games that use it need play-services-games-v2 + the manifest APP_ID
samples/      small games, built only when the engine is the top-level project (desktop):
              snake/ (Snake + its rules tests)
tests/        engine unit tests (no window/GPU: frame clock, conventions (axes, turns, camera on screen, lights, particles, primitive windings, glTF front), cameras (view = inverse world, fit modes, frustum, fly/orbit/follow controllers), node kinds, quality levels (down/up, back-off, settle, frame-time-only, lock, saved,
              budget from the target, render scales), frame pacing (plans per target/refresh, limiter schedule), draw order (sort keys), sky (rays, place in the order), scene gather + cull, transform (Euler orders, look rotation, matrix cache, hierarchy, names, ids, layers), normal matrices (cofactor vs inverse-transpose, mirrors, flat), shader system (preprocessor, embedded files in every variant, std140 layout, lights, variants, UI kinds), systems (order, starting, Back), blending (depth-write rule, canvas batches, images, falloff texture), resources (made without a GPU, updates, alive list: owned any way, made / destroyed during a walk), connections (disconnect, scoped, changes while calling), app config (groups, defaults), post passes (planning, ping-pong, named inputs, chain targets, effects, render-scale upscale), viewports (scenes, sizes, render scale, update modes), anim, audio, core (random, atomic files, bridge queue), exact cover, search (bitboards, minimax, expectimax), gf2 (Lights Out), shader text splitting, lighting (light nodes, shadow-box fitting), fx, instances (layout, Color32 = lround, SinCos, Arvo boxes vs corners, WriteInstances, cached bounds), input, kit, picking, primitives, ui (canvas, safe area, widgets), screens, profile macros + TypeName, version macros + build info + the deprecation macros; CTest "deprecations" runs cmake/CheckDeprecations.cmake). doctest: one binary wlw_tests, a TEST_SUITE per file, shared main test_main.cpp,
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
              wlw_app_tests (tests/gpu/app_tests.cpp, also "gpu: app ...", label gpu): a real Application on a hidden
              window driven by Init/Frame/Shutdown (hook order, Quit codes, lifecycle checks). Its own binary: GL objects
              belong to one context, and an app's warm-up prepares every alive resource (it would steal the golden tests' shaders).
              wlw_gpu_tests also has shader_compile_tests.cpp ("gpu: glsl 330", "gpu: glsl es 300"), which compiles + links every engine
              program and sample game shaders as GLSL 3.30 and in a desktop OpenGL ES 3.0 context (Mesa / NVIDIA both
              refuse a precision mismatch); --dump-shaders <dir> writes the final sources. Mali Offline Compiler:
              Arm Performance Studio's Linux archive is unpacked in WSL (~/arm/Arm_Performance_Studio_2026.5/
              mali_offline_compiler/malioc --core Mali-G78 x.frag; from Git Bash with MSYS_NO_PATHCONV=1).
benchmarks/   wlw_benchmarks (nanobench; top-level only, desktop + Android executable): no window/GPU, fixed seeds, one op =
              one frame's work. Groups: render (gather/cull/sort 1k, 10k, instance bounds 5k), transform (move flat/nested, read nested; 1k, 10k), fx (flow 20k,
              particles 5k: update, instances, billboards), ui (text 1k glyphs), audio (mix 24 voices, music sweep + duck). Add: WLW_BENCHMARK("group") { ctx.Run("group/name",
              [&] { ... }); } (names are stable: baselines match by name). --out JSON, --baseline compare (warning past
              --threshold %, never a failure), --smoke = CTest "bench: smoke" (label bench). Baselines in
              benchmarks/baselines/: windows-local (owner's Ryzen 7 9800X3D, MSVC Release), linux-ci (bench.yml
              artifact), optional per-phone ones (<device>.json, e.g. pixel4; see "Benchmarks" below).
third_party/  glm 1.0.1 (wlw_glm, public through wlw::wlw), glad (desktop only), stb, tinygltf + nlohmann json (also its own target wlw_json), miniaudio 0.11.25, stb_vorbis, Dear ImGui 1.92.9b,
              doctest 2.4.12, nanobench 4.3.11 (wlw_nanobench), Tracy 0.14.1 client (target wlw_tracy, only with WLW_PROFILE),
              Swappy 2.3.0-alpha01 (wlw_swappy, Android only; one local patch, see its README row) (vendored; versions and licences in third_party/README.md)
cmake/WLWHelpers.cmake  wlw_add_game(), wlw_add_assets(), wlw_set_warnings(), wlw_optimize_debug_flags()
cmake/ClangFormat.cmake the format check / fix (pinned clang-format, see "Building")
vcpkg/ + vcpkg.json     GLFW only (desktop); everything else is vendored in third_party/ (D13)
```

## Conventions (core/math.h; Roadmap 2.4, D1)
- **Z up, +Y forward, +X right, right-handed** (Right x Forward = Up), like Blender / O3DE / CryEngine. Meters,
  radians, seconds. Constants `kRight`, `kForward`, `kUp` (and `kLeft`, `kBack`, `kDown`); `kModelFront` = -Y.
- Positive turns are counter-clockwise looking down the axis (a positive yaw about +Z turns +Y towards -X). Euler
  default ZXY = yaw (z), pitch (x), roll (y). Nodes, lights and cameras face their +Y; cameras have +Z up on screen.
- Front faces wind counter-clockwise seen from outside (tested for every primitive; culling is still off).
- Y-up data is turned at the door: glTF on import (`FromYUp`: +Y -> +Z, -Z -> +Y, its +Z front -> -Y), cube maps
  sampled with `ToYUp` (skybox images keep the usual layout). Only GL's view space looks down -Z (Camera3D's view).
- 2D (Canvas, screen points, Input): x right, y down, origin top-left; texture uv (0, 0) = the image's top row.
- Tests: the `conventions` suite (CPU) + `gpu: axes` / `gpu: cube faces` (pixel probes).

## Rules
- **Platform code stays in `engine/wlw/platform/<os>/`.** Nothing else may include GLFW, EGL,
  Android or other OS headers. Shared code talks to `platform.h`, `scene::Window` and `input::Input`.
- **Keep everything GLES 3.0 compatible**, because one renderer serves desktop and mobile:
  - Include GL through `wlw/rendering/gl.h` only (never `<glad/glad.h>` or `<GLES3/gl3.h>` directly).
  - Shader sources have **no `#version` line**. The driver prepends `GlslHeader` (rendering/glsl_header.h:
    `#version 330 core` / `#version 300 es` + default precisions). They must compile as both GLSL 3.30
    and GLSL ES 3.00 (no `texture2D`, no `gl_FragColor`, no desktop-only built-ins); the GPU tests
    `gpu: glsl 330` / `gpu: glsl es 300` compile every program both ways (a desktop ES context, `WindowOptions::gles`).
  - **Precision:** GLES floats and ints default to highp in both stages. A shader opts in to fp16 with its own
    `precision mediump float;` only where a phone measured it faster: none in the engine yet (Adreno 640: a
    mediump fragment default made Bluzzle's lit 3D 8% slower, the post effects' opt-in 2%; Mali unmeasured on a
    phone). Cvar `r.mediump` 0 turns every opt-in off for A/B.
    Every uniform, block/struct member and varying in an engine shader file
    names `highp` or `mediump` (both stages must agree; the "precision lint" test checks): highp for positions,
    matrices, texture/pixel/canvas coordinates, shadow coordinates, distances, cone cosines, specular; mediump for
    colours, normals, coverage. Locals that hold those need `highp` too. Games' shaders are highp unless their
    text says `precision mediump float;` (shader.h). Precision artifacts only show on phones (no desktop or
    software GPU has fp16).
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
  methods `PascalCase`; variables `snake_case`; private members `snake_case_`. Formatting is `.clang-format`
  (Google-based, 120 columns, `T& x`) and CI checks every file: run `cmake -P cmake/ClangFormat.cmake` before
  committing (it fixes everything in place; `-DCHECK=ON` only checks).
- **Warnings are errors** in the engine's presets and CI (`/W4 /permissive-`, `-Wall -Wextra -Wshadow`). Fix the
  cause; never silence a warning for engine code. Third-party implementation files are the only exception.
- **Include what you use:** the engine has a precompiled header (`WLW_PCH`) that would hide a missing `#include`;
  the Android CI job builds without it and fails on one.
- **Don't delete files without asking the owner first.**

## API rules (Roadmap 2.5)
- **Every `Create()` / factory returns `std::unique_ptr`** (`FromFile`, `FromImage`, `Load`, `LoadModel`, the
  primitives' `*Mesh` too): the caller picks unique or shared (`std::shared_ptr<T> x = T::Create()`; the reverse
  conversion doesn't exist). Only a function returning something another owner also keeps (a cache, the material
  a viewport holds) returns `shared_ptr`. Registration of live objects happens in constructors, never through a
  `shared_ptr` / `weak_ptr` made at creation (Resource's alive list). "Use Create" constructors are private.
- **Getters return const references** to what the object holds (`const std::shared_ptr<T>&`, `const T&`), not copies.
- **Remove by object**, like `Node3D::RemoveChild`: `RemoveViewport(viewport)` (pointer in, the removed
  `shared_ptr` out), `RemoveCanvasLayer(canvas)`; no int ids.
- **Callbacks return a `core::Connection`** (`AddLogSink`, `CVar::OnChange`, window overlays): a plain handle;
  dropping it keeps the callback; `Disconnect()` removes it (safe after the source is gone); `ScopedConnection`
  for RAII. A source keeps its callbacks in a `core::CallbackList` (one thread) or its own locked list.
- **Engine-only methods go behind `x.Internal()`**: a small `InternalAccess` class at the bottom of the same
  header (nested, so it reaches the private members) for what the platform, the renderer or the Application call
  (Window, Viewport, Canvas::BeginFrame, Resource / textures / shaders / render targets, Mesh buffers, instance and
  sprite buffers, PostChain, Input feeding, ScreenStack's frame, Quality's feeding). Games never call
  `Internal()`; tests may.
- Accessor naming is left as it is (`GetCanvas` / `GetInput` next to `Root` / `Post` / `Settings`).

## Versioning
SemVer below 1.0 (minor = may break, patch = fixes only); **one minor version per finished roadmap phase**
(Phase 0 = v0.1.0, Phase 1 = v0.2.0...). The version lives in one place: `project(WLWengine VERSION x.y.z)` in
the top-level `CMakeLists.txt` -> generated `wlw/version.h` + `core::GetBuildInfo()`. Between tags it stays at
the last release (the build info's git commit tells builds apart).
- **Changing an API: no deprecation shims** (owner, 2026-10-24: nothing published uses the engine yet). Rename or
  replace it directly, update every caller (engine, tests, Snake), and list it in CHANGELOG.md under **Breaking**
  with a **Migration** line; the games move in their own step. The shims made before that rule were deleted in 2.6.
  The tooling stays for when a game ships on a tagged engine (`WLW_DEPRECATED` in core/deprecated.h,
  `#if !WLW_NO_DEPRECATED`, the CTest test `deprecations`): engine code never calls a shim; tests of one wrap it in
  `WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN/END`.
- **Releasing** (after the phase's last step is merged): bump `project(VERSION)`, delete expired shims, turn
  CHANGELOG's "Unreleased" into `## x.y.z (date): Phase N, <name>` with a summary + **Breaking** / **Deprecated**
  lists, commit "Release vx.y.z", then `git tag -a vx.y.z -m "WLWengine x.y.z"`, `git push origin vx.y.z` and
  `gh release create vx.y.z --title "WLWengine x.y.z" --notes-file <that CHANGELOG section>`. Games move their
  submodule to the tag. A fix or a non-breaking addition a game needs before the next phase = a patch release from
  `main` (x.y.z+1; ROADMAP D19); anything breaking waits for the phase's minor version.

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
New code must pass all of them: no MSVC-only includes, no UB, no leaks. The Android job also turns on `WLW_PROFILE`
and turns off `WLW_PCH`. A `clang-format` job checks formatting (`cmake -DCHECK=ON -P cmake/ClangFormat.cmake`).

Build options (CMake cache variables):
- `WLW_WARNINGS_AS_ERRORS`: on in every engine preset, off for games unless they turn it on.
- `WLW_OPTIMIZE_ENGINE_IN_DEBUG`: Debug builds compile engine + kit + third_party at -O2 -g, game code at -O0
  (10-35x faster engine work in Debug). On when the engine is a game's submodule, off when it's top-level. Turn it
  off to step through engine code; header-only engine code always gets the caller's flags.
- `WLW_PCH` (on): precompiled standard library + glm headers for the engine target.
- `WLW_PROFILE`, `WLW_DEBUG_UI`, `WLW_ENABLE_ASSERTS`, `WLW_SANITIZE`: see "Profiling" and the CMake files.

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
Firebase Test Lab (roadmap 7.6) is the planned way to get numbers from phones the owner doesn't have.

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
- Node world bounds (WorldBounds) are recomputed once per frame per node (world matrices are cached since 2.2); render lists are
  rebuilt every frame (no cached draw commands for static nodes, unlike Unreal). Fine for hundreds of nodes.
- No shader program cache on disk yet (glProgramBinary): every launch compiles its variants (~25-60 ms on the emulator).
- Shadows: one shadow map shared by every viewport (each viewport's shadow pass redraws it).
- Touch input uses only the first finger (no multi-touch yet).
- GPU time needs EXT_disjoint_timer_query on phones (Pixel 4 / Adreno 640 has it; the emulator doesn't). Phone
  clocks scale with load (DVFS): light frames read slower, so compare GPU ms between levels only roughly.
  2D is post-processed only in Picture canvas layers.
- Quality: no thermal input yet (AThermal_getThermalHeadroom, API 31+). Without a GPU timer it only steps down
  (vsync hides headroom), and that saved level never goes back up. The budget follows the frame-rate target (Swappy's swap interval on Android).
- Textures free their CPU pixels once uploaded, so a lost GL context (if the platform ever loses it) can't re-upload.
