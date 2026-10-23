# Changelog

Steps refer to ROADMAP.md. Breaking changes come with migration notes. Entries before the 2026-10-21 reorder use
the old step numbers (ROADMAP.md "Old numbering" maps them).

Versions follow SemVer below 1.0: a minor version may break the API, a patch only fixes. One minor version per
finished roadmap phase, each tagged `vX.Y.Z` on `main` with a GitHub Release; games pin their engine submodule
to a tag. Until a game ships, APIs change without deprecation shims (the owner's call, 2026-10-24): every change is
listed under **Breaking** with its **Migration**. Shims added before that (`WLW_DEPRECATED`, listed under
**Deprecated**) are deleted (listed under **Removed**).

## Unreleased

- ROADMAP.md reordered around the editor (2026-10-21): Phase 2 Core API, then the editor (3 shell and game
  tools, 4 reflection, data and scenes, 5 scene editor), 6 iOS on the current GL renderer, 7 Android platform,
  then runtime infrastructure, the rendering rewrite (RHI, Vulkan, Metal) and the feature phases; 19 = the editor
  as a product, if still wanted. New decisions D14-D17 (reflection, scene files, undo, iOS renderer); D12 revised
  (a `<game>_editor` build per game). Phase N is still released as v0.(N+1).0.
- `platform::KeepScreenOn(bool)`: keeps the screen awake while the player watches without touching (Android:
  FLAG_KEEP_SCREEN_ON; no-op on desktop). Asked for by Threadlock, whose runs outlast a 30 s screen timeout.
- `ui::TextStyle::rotation`: Canvas text turned about its alignment point (radians, clockwise on screen, like
  Arc). Only the glyph corners turn; the SDF anti-aliasing holds (abs-derivative width: ~8% softer at 5°,
  ~16% at 10°, ~41% at 45°). The shadow keeps its on-screen offset. Asked for by Threadlock (World 2's pinned order slips).

### API consistency (Roadmap 2.5)
One set of rules for the public API (CLAUDE.md "API rules"): factories hand over `unique_ptr`, getters return const
references, things are removed by object, callbacks return a `core::Connection`, and what only the engine calls
sits behind `x.Internal()`.
- **Factories return `std::unique_ptr`** (the owner's rule: the call site picks unique or shared; the reverse
  conversion doesn't exist): `Material`, `ShaderMaterial`, `Shader::Create` / `FromFile`, `WTexture2D::Create` /
  `FromImage`, `WCubemap::Create`, `RenderTarget::Create`, `Font::Load` / `FromMemory`, `GLTFLoader::LoadModel`,
  `Scene`, `Viewport`, `Camera3D`, `MeshInstance3D`, `MultiMeshInstance3D`, `OrthographicCamera` (shim),
  `utils::RadialFalloffTexture` and the new primitive `*Mesh` functions. Shared caches keep `shared_ptr`
  (`scene::AddThreshold`'s material, `TextureManager`). Their "use Create" constructors are private now.
- **Resources register in their constructor**: the alive list (`RenderingDriver::WarmUp`) is an intrusive list of
  raw pointers, filled by `Resource`'s constructor and emptied by its destructor, so a resource is found however
  it is owned (before, one made with `make_shared` was missed). A walk may make or destroy resources.
- **Fonts**: held by a `shared_ptr`, a font is still kept alive by the Canvas until its text is drawn; held by a
  `unique_ptr` (what `Load` returns), the batch only points at it, so it must outlive the frame's draw.
- **`core::Connection`** (`core/connection.h`, like boost.signals2 / entt): a copyable handle with `Disconnect()` /
  `Connected()`, safe after its source is gone; dropping it keeps the callback. `ScopedConnection` disconnects
  when destroyed; `CallbackList<Args...>` is the one-thread list behind them (callbacks may disconnect or add while
  it calls). `AddLogSink` (thread-safe: once `Disconnect` returns, the sink isn't called again), `CVar::OnChange`
  and window overlays return one.
- **`Window::MainViewport()`** (was `GetScreen()`); `RemoveViewport(const Viewport*)` returns the removed
  `shared_ptr` (like `Node3D::RemoveChild`); `Viewports()` lists them (was `GetViewports()`, pairs with ids).
- **Const-ref getters**: `Mesh::GetMaterial`, `Material::GetTexture` (was a raw pointer), `Viewport::GetTexture`.
- **`x.Internal()`**: engine-only methods moved into a small `InternalAccess` class per type: `Window`
  (screen depth, GPU timing results, fragment precision, `ReleaseScenes`, overlays), `Viewport` (`Id`,
  `GetTarget`, `BeginDraw`, render / GPU stats, canvas layers' frame), `Canvas::BeginFrame`, `Resource`
  (`Gpu` / `SetGpu`, `ForEachAlive`), `WTexture2D` / `WCubemap` (pixels, faces, version), `RenderTarget::SetFormat`,
  `Shader` (`Source`, `MarkFailed`), `Mesh` buffers, `MultiMeshInstance3D` / `SpriteBatch` buffers and versions,
  `PostChain` (`Prepare`, targets), `Input` feeding (`BeginFrame`, `OnKey`, `OnText`, `OnPointer*`,
  `SetSurfaceSize`, `SetTouchScreen`), `ScreenStack`'s frame (`Update`, `Render`, `Back`, `EndFrame`...) and
  `Quality`'s feeding (`SetSaveFile`, `SetTargetFrameMs`, `Update`). Games called none of them.
- **`Screen::App()`**: the app's services from a screen (protected, valid from `OnEnter`), like `System::App()`.
- **`AppConfig` grouped**: `name` (the save folder for quality.txt, cvars.cfg and the debug log, and the default
  window title) + `window` {`title`, `size`, `stats_in_title`}, `graphics` {`target_fps`, `screen_depth`}, `audio`
  {`enabled` (false = no device), `max_voices`}, `time` {`fixed_step`, `max_frame_time`}, `ui` {`reference_size`}.
  `ResolveAppConfig` (tested) fills the defaults; `audio::Audio` takes an `AudioConfig`.
- **Primitives**: `utils::CubeMesh`, `BeveledCubeMesh`, `SphereMesh`, `CylinderMesh`, `GemMesh`,
  `PyramidFrustumMesh`, `PlaneMesh` (were `createXxxWithNormals`).
- Tests: suites `connection` and `app_config`; the alive list (owned any way, made / destroyed during a walk),
  removal by object, a uniquely owned font in a Canvas. Every golden image is unchanged.
- No deprecation shims (the owner's call: nothing published uses the engine yet); every rename is breaking.
- **Breaking:** factory return types; `AddLogSink` / `OnChange` return a
  `Connection` (`RemoveLogSink`, `RemoveOnChange` are gone); `Window::AddOverlay` / `RemoveOverlay`,
  `GetViewports`, `ReleaseScenes` and the other engine-only methods above moved behind `Internal()`; reading
  `Window::GetScreen` and `RemoveViewport(int)` are gone (`AddViewport` returns nothing); the flat `AppConfig`
  fields are gone; `utils::createXxxWithNormals` are gone; `Material::GetTexture`
  returns `const std::shared_ptr<WTexture>&`; `Audio()` takes an `AudioConfig`.
- **Migration:** `auto x = T::Create()` that is then shared (passed to `SetScene`, `SetCamera`, `AddViewport`,
  `ShaderMaterial::Create`, kept twice...): write `std::shared_ptr<T> x = T::Create()`, or pass
  `std::move(x)`. Members already declared `shared_ptr` take the result as they are. `int id = AddLogSink(...)`
  -> `core::Connection c = AddLogSink(...)`, `RemoveLogSink(id)` -> `c.Disconnect()` (same for `OnChange`).
  `GetScreen()` -> `MainViewport()`; `RemoveViewport(id)` -> `RemoveViewport(viewport.get())`;
  `config.title = "X"` -> `config.name = "X"` (also the window title), `window_size` -> `window.size`,
  `ui_reference_size` -> `ui.reference_size`, `fixed_timestep` -> `time.fixed_step`, `target_fps` ->
  `graphics.target_fps`, `screen_depth` -> `graphics.screen_depth`, `show_stats_in_title` ->
  `window.stats_in_title` (reading them too: `App().Config().fixed_timestep` -> `.time.fixed_step`);
  `createCubeWithNormals(...)` -> `CubeMesh(...)` (and the others; they return `unique_ptr`).

### Conventions (Roadmap 2.4)
The engine is **Z up** now: +Z up, +Y forward, +X right, right-handed (Right x Forward = Up), meters, radians, like
Blender, O3DE and CryEngine (D1 rewritten; the owner's choice). One quarter turn about X takes the old axes to the
new ones (+Y up -> +Z, -Z forward -> +Y), and every system moved by exactly that turn, so the golden scenes, turned
the same way, draw the same pictures. Documented in `core/math.h` and CLAUDE.md "Conventions".
- `core/math.h`: `kRight`, `kLeft`, `kForward`, `kBack`, `kUp`, `kDown`, `kModelFront` (-Y: the way a model
  faces), `FromYUp` / `ToYUp` (Y-up files and GL's view space).
- Nodes: `Forward()` = +Y, `Up()` = +Z; `LookAt` / `LookRotation` turn +Y to the target with +Z as up (straight
  down = a pitch of -90 degrees); Euler angles default to `kDefaultEulerOrder` = ZXY (yaw about Z, pitch, roll).
- `Camera3D` looks along its +Y with +Z up on screen; its view matrix also turns the axes into GL's view space
  (looking down -Z). The default camera: the origin, looking along +Y.
- Lights shine along their +Y; directional and spot lights point straight down (-Z) by default. The shadow map
  keeps world up (or, for a light straight down, +Y) at the top of its picture.
- Controllers: fly and orbit yaw about +Z (orbit yaw 0 = the camera on -Y of the pivot); `FlyInput::move` is
  (right, forward, up); `FollowController::offset` defaults to (0, -10, 5), `up` to +Z.
- `fx::ParticleSystem` (all radians now): gravity (0, 0, -9.8); `Floor` is a rectangle in XY at a height in z;
  `BurstSettings::direction` +Z, `spread` (half-angle, radians) and `max_spin` (radians per second) replace
  `spread_degrees` / `spin_max`; `Particle::angles` / `angular_velocity` (radians) replace `rotation` / `spin`.
- Primitives stand on Z: the plane is the XY ground facing +Z; the cube, cylinder and gem are turned (the cube's
  old +Z front faces -Y); the sphere and pyramid frustum already had their axis on Z. Fixed while testing every
  mesh's winding: the pyramid frustum's top and bottom and the cylinder's caps were wound inwards (invisible with
  culling off).
- glTF models are turned from Y up on import (they face -Y); skyboxes are sampled with `ToYUp`, so cube-map images
  keep the usual Y-up layout. `wlw::Ray` defaults along +Y, `wlw::Plane` to the ground z = 0.
- Tests: suite `conventions` (axes, turn directions, the default camera on screen, lights, particles, windings and
  axes of every primitive, a glTF triangle's front), GPU probes `gpu: axes` and `gpu: cube faces`.
- **Breaking** (no shim can exist: the same calls mean other directions): positions, directions, `LookAt` ups,
  Euler angles in the default order, `FlyInput::move`, particle gravity and floors, the plane and turned
  primitives, imported glTF models, the default camera and lights.
- **Migration:** turn a Y-up world by `FromYUp` (x, y, z) -> (x, -z, y): positions and directions; `LookAt(t, {0,
  1, 0})` -> `LookAt(FromYUp(t))`; a ground plane at y = h -> z = h; gravity -Y -> -Z; Euler angles in the old
  default (YXZ: x pitch, y yaw, z roll) -> ZXY with (pitch, -roll, yaw); non-uniform scales swap y and z. A game
  that only looks straight down at the XY plane (a camera on +Z, no up given) keeps its picture: `LookRotation`'s
  fallback up puts +Y at the top. `spread_degrees = d` -> `spread = d * kDegToRad`; `spin_max` -> `max_spin`;
  `Particle::rotation` / `spin` (degrees) -> `angles` / `angular_velocity` (radians).
- **Deprecated** (removed in 0.4): `BurstSettings::spread_degrees` and `spin_max` (degrees; when set they win over
  the radians).

### Node types (Roadmap 2.3)
`Node3D` draws nothing now: what a node is comes from its type, like Godot's node tree. Each type sets a 1-byte
`NodeKind` in its constructor (`Node3D::Kind()`), and the renderer's gather switches on it instead of making two
virtual calls (`AsSpriteBatch`, `AsLight`) per node per frame.
- `scene/geometry_instance.h`: `GeometryInstance3D` (model, material, `SetCastsShadow` / `SetReceivesShadow`,
  `LocalBounds`, `WorldBounds`, `HasGeometry`) -> `MeshInstance3D` (drawn once) and `MultiMeshInstance3D` (1.6's
  `WriteInstances` API and cached `InstanceBounds`; no copies = not drawn). The mesh's material still wins over the
  node's and shadows stay opt-in: every golden image is unchanged.
- Lights: `Light3D` (was `LightNode`) -> `DirectionalLight3D`, `PointLight3D`, `SpotLight3D` (cone in radians:
  `SetInnerAngle` / `SetOuterAngle`). `SpriteBatch` keeps its API and has its own material.
- `Camera3D` is a node: placed, turned and parented like any other; looks down -Z; the view is the inverse of its
  world transform, orthonormalized (a scaled parent doesn't squash the picture). `SetPerspective(fov_y, near, far)`
  (radians) / `SetOrthographic(size, near, far)`. The aspect is passed in, never stored:
  `GetProjectionMatrix(aspect)`, `GetViewProjectionMatrix`, `GetFrustum(aspect)`, `VisibleSize(aspect, distance)`,
  `ScreenPointToRay(point, size)`, `WorldToScreen(world, size)`, so they are right before the first frame and the
  renderer no longer writes each viewport's size into the camera. Fit modes on a frame (`size` tall,
  `SetFrameAspect` wide): `KeepHeight` (default), `KeepWidth`, `Contain`, `Cover`. The default viewport camera is
  a `Camera3D` at the origin looking down -Z, 60 degrees, 0.1-1000 (was an `FPSCamera` with Z up).
- `scene/camera_controllers.h`: `FlyController`, `OrbitController` (`Frame(box)`, orthographic zoom = size),
  `FollowController` (offset in world or target space, look offset, position / rotation lag). Plain objects fed
  input structs (a System or the editor maps keys to them); frame-rate independent smoothing (`ExpDecay`).
- `fx::ParticleSystem::WriteTo` takes a `MultiMeshInstance3D&`.
- Sizes (MSVC x64): `Node3D` 448 -> 280 B, `MeshInstance3D` 320, `DirectionalLight3D` 496 -> 328, `SpriteBatch`
  512 -> 360, `Camera3D` 312; `MultiMeshInstance3D` 448.
- Benchmarks (Windows MSVC Release, alternating runs against main): render/gather 10k 175.6 -> 168.3 us (-4%, median
  of 6; within run-to-run noise), everything else unchanged.
- **Breaking:**
  - `Node3D` loses `SetModel/GetModel`, `SetMaterial/GetMaterial`, `SetCastsShadow`, `SetReceivesShadow`, the
    instancing API, `GetAABB` and `IsInstanced` (no shim: a thin one can't exist, see ROADMAP 2.3; Bluzzle's patch
    is ~10 lines). `GetAABB` is `WorldBounds`.
  - `Camera3D` is a concrete node, not an interface: `GetProjectionMatrix` takes the aspect, `GetViewMatrix` is
    const and returns a value. `CameraMovement`, `ProcessKeyboard`, `ProcessMouseMovement`, `UpdateSize` and
    `Window::ProcessMouseMovement` (GLFW's mouse-look hook) are gone.
  - `LightNode`, `PointLight`, `SpotLight` (`SetCone` in degrees) renamed without a shim (no game used them).
  - `SpriteBatch::GetMaterial` / `SetMaterial` are its own (it isn't a mesh node).
- **Migration:** a drawn `make_shared<Node3D>()` -> `make_shared<MeshInstance3D>()`; a node fed by `WriteInstances`
  or `ParticleSystem::WriteTo` -> `MultiMeshInstance3D` (drop `WriteInstances(0)` used to switch it to instanced);
  `DirectionalLight` -> `DirectionalLight3D`; `SetCone(inner_deg, outer_deg)` -> `SetInnerAngle` / `SetOuterAngle`
  in radians; `OrthographicCamera` -> `Camera3D` with `SetOrthographic(height, near, far)`,
  `SetFrameAspect(width / height)`, `SetFitMode(Contain)`, `SetPosition` + `LookAt(target, up)`; `FollowCamera` ->
  a `Camera3D` + `FollowController` (or `LookAt` once for a fixed target).
- **Deprecated** (removed in 0.4): `DirectionalLight` (alias), `OrthographicCamera` (a `Camera3D` subclass with the
  old `SetVisibleArea` / `SetTarget` / `SetUp`; matches the 0.3 matrices).
- **Removed:** `FPSCamera` (`fps_camera.h/.cpp`), `FollowCamera` (`follow_camera.h`), the 0.2 shims
  `Node3D::SetInstances` / `GetInstances` (`ParticleSystem::BuildInstances` stays until 0.3).

### Transform and Node3D (Roadmap 2.2)
`Node3D` is a plain class now (the `Node<T>` template, `Node2D`, `Triangle2D` and `Vertex2D` are gone; 2D lives on
the Canvas), with a cached quaternion transform, an ordered hierarchy, names, ids and render layers, and its code in
`scene/node_3d.cpp` (so games' Debug builds get it optimized, `WLW_OPTIMIZE_ENGINE_IN_DEBUG`).
- Transform: position + `Quat` + scale. The local matrix (T * R * S, built directly from the quaternion) and the
  world matrix are cached; a change marks the node and its subtree, stopping at nodes already marked, so moving a
  node costs nothing until a matrix is read. `GetWorldMatrix()` (const ref), `GetLocalMatrix()`,
  `GetWorldPosition/Rotation/Scale`, `SetWorldPosition/Rotation`, `Forward/Right/Up` (world), `Rotate(axis, rad)`
  (own axis) / `RotateWorld`, `Translate` (own axes) / `TranslateWorld`, `LookAt(target, up)`.
- Euler angles (`wlw/core/rotation.h`): `EulerOrder` XYZ...ZYX, named by the product (YXZ = Ry * Rx * Rz, the
  default, like Godot and Unity), `QuatFromEuler`, `EulerFromQuat`, `LookRotation(forward, up)` (a fallback up
  when they're parallel, so straight up/down works). `Node3D::SetEulerAngles(radians, order)` /
  `GetEulerAngles(order)`: the angles set come back exactly as given (an inspector shows 190°, not (180, -10, 180))
  until the rotation changes another way.
- Hierarchy: children in a `std::vector` (insertion order; `MoveChild(child, index)`), `AddChild` (takes the node
  from its old parent; loops refused), `RemoveChild(node)` / `RemoveFromParent()` (return the node),
  `Reparent(new_parent, keep_world = true)`, `GetIndex`, `ChildCount`.
- Names: `SetName`, `FindChild(name, recursive = true)`, `GetNode("Arm/Hand")` with `..` and `.`. Ids: a random
  64-bit id per node (never 0, thread-safe generator), `SetId` for loading, `Scene::FindById`; unique in
  practice, checked by scene loading in Phase 4. Layers: `Node3D::SetLayers(mask)` (default bit 0) +
  `Camera3D::SetCullMask(mask)` (default all), checked in `CullScene`; lights and shadows ignore them.
- `Scene::Root()`: top-level nodes are its children. `Scene::ForEachNode(f)` = every node, hidden too, depth
  first. The renderer's gather walks the tree directly (raw pointers, hidden branches skipped at once) instead of
  a level-by-level walk with `shared_ptr` copies through `std::function`.
- `LightNode::SetDirection` is a `LookRotation` (the default straight-down light keeps the old orientation);
  `GetDirection()` = `Forward()`.
- Benchmarks (Windows MSVC Release, median of alternating runs against main): render/gather 10k 467 -> 217 us
  (-54%), gather 1k -46%, transform/move flat 10k 1167 -> 335 us (-71%), move nested 10k -23%, new
  transform/read nested 10k (nothing moved) 230 -> 30 us (-87%); cull and sort unchanged; render/instance bounds
  5k reads ~10% slower with its hot loop (instance_buffer.cpp) untouched (code placement).
- Draw order: the gather is depth first in child order (was level by level, and top-level nodes came out of an
  `unordered_map` in an order that differed between compilers). Equal sort keys and lights past 4 per type
  follow it.
- **Breaking:**
  - `GetRotation()` returns a `Quat` (was degrees). `GetChildren()` returns the vector of children. `Translate`
    moves along the node's own axes (was the parent's: use `TranslateWorld` or `SetPosition` for that).
  - Gone without a shim: `Node::RemoveNode(int)` and `Scene::RemoveNode(int)` (use `RemoveChild(node)` /
    `node->RemoveFromParent()`), `Scene::GetNodes()`, `Nodes2DMap`, `Nodes3DMap`, `Node<T>`, `Node2D`,
    `Triangle2D`, `core::Vertex2D` and its vertex-buffer path. `AddNode` returns nothing (ids are gone).
  - With `WLW_NO_DEPRECATED`, `SetRotation(Vec3)` is deleted (glm would otherwise turn a Vec3 into a quaternion
    from radians silently).
- **Migration:** `SetRotation({x, y, z})` in degrees -> `SetEulerAngles(glm::radians(Vec3(x, y, z)),
  EulerOrder::XYZ)` (the same matrix) or, for one axis, `SetRotation(glm::angleAxis(radians, axis))`;
  `GetModelMatrix()` -> `GetWorldMatrix()`; `scene->AddNode(n)` -> `scene->Root().AddChild(n)`;
  `node->AddNode(n)` -> `node->AddChild(n)`; `IterateOverNodes` -> `ForEachNode([](Node3D& node) {...})`.
- **Deprecated** (removed in 0.4): `Node3D::SetRotation(Vec3 degrees)` (the old T * Rx * Ry * Rz * S matrix within
  float rounding), `Node3D::GetModelMatrix`, `Node3D::AddNode`, `Scene::AddNode`, `Scene::IterateOverNodes`.

### Math, colour, geometry (Roadmap 2.1)
One set of value types in namespace `wlw`, replacing the hand-written `core::Vector2/3` and the bare `core::Color`,
plus the helpers both games had written again (Threadlock had 5 `Distance`, 7 normalise-with-fallback and 3 `Box`
structs; `kPi` was spelled 6 ways across the engine and games).
- `wlw/core/math.h`: `Vec2/3/4`, `IVec2/3/4`, `Quat`, `Mat3`, `Mat4` (= glm's types, D1); `kPi`, `kTau`,
  `kHalfPi`, `kDegToRad`, `kRadToDeg`; `Lerp` (floats, vectors, colours; `anim::Lerp` is now this one),
  `InverseLerp`, `Remap`, `Saturate`, `SmoothStep`, `MoveTowards`, `ExpDecay` (frame-rate independent smoothing),
  `WrapAngle`, `AngleDifference`, `LerpAngle`, `IsNearlyEqual/Zero`; `SafeNormalize(v, fallback)`, `Perp`,
  `Cross(Vec2, Vec2)`, `Rotate(Vec2, radians)`, `ClosestPointOnSegment`, `DistanceToSegment` (2D and 3D).
  glm is configured in CMake for the engine and every game (`GLM_ENABLE_EXPERIMENTAL`, for gtx in 2.2).
  `GLM_FORCE_CTOR_INIT` was measured and left off: with it nested transforms ran 21-30% slower and the draw sort
  ~8% slower (MSVC Release, A/B of the same branch), because glm's operators then build each result twice.
- `wlw/core/color.h`: `Color` holds the values **as authored** (sRGB-encoded, like Unity's and Godot's `Color`);
  the renderer uses them as they are until it shades in linear space (13.1, D8), and converts them then. Maths
  that must be physically right goes through `ToLinear()` / `Color::FromLinear()` (the exact sRGB curve; every
  byte value survives the round trip). `Color(r, g, b, a = 1)`, `Color()` = transparent black as before;
  `Color::Hex(0xRRGGBB, alpha)` (constexpr: colour tables), `HexRgba`, `Parse("#rgb" / "#rgba" / "#rrggbb" /
  "#rrggbbaa")`, `ToHex()`; `WithAlpha(a)` (replaces alpha), `Faded(k)` (multiplies it), `Darkened(k)`,
  `Lightened(k)`; `+ - * /`, `==`; `ToVec4()`, `Rgb()`; `colors::kWhite` and friends. `Color32` = RGBA8 bytes
  (`Packed()` / `FromPacked()` / `ToColor()`, rounding by `UnitToByte`, the same bytes as 0.2's `PackColor`).
- `wlw/core/geometry.h`: `AABB` (`Center`, `Size`, `Extents`, `Contains`, `Grown`, `Expanded`, `FromCenterSize`,
  `FromCenterExtents`, `Empty()` / `IsEmpty()`), `Sphere` (`BoundingSphere(box)`), `Ray`, `Plane`
  (`FromPointAndNormal`, `FromPoints`), `Frustum` (`FromMatrix`, moved out of camera_3d.h), `Rect` (2D; `ui::Rect`
  is now this type). `Overlaps(a, b)` for box/box, rect/rect, sphere/sphere, box/sphere and frustum/box,
  frustum/sphere (conservative: never drops a visible shape); `Raycast(ray, shape)` = the distance along the ray
  (0 when it starts inside) for planes, boxes (slab method, zero direction components handled), spheres and
  triangles (Möller-Trumbore); `TransformBox`, `Union`. Tested against brute force (points marched along random
  rays, sampled inside shapes, fixed seeds).
- Engine APIs take the new types: Node transforms, cameras, `Input::PointerPosition/SurfaceSize`, `Window::GetSize/
  Create`, `AppConfig::window_size`, `Vertex3D` (layout checked by static_asserts), `Light::SetColor(Color)`,
  `ShaderMaterial::SetParam(name, Color)` (a vec4). Golden images unchanged; benchmarks within noise of main
  (Windows, alternating runs: gather/cull/sort/transform/fx all within ±6%).
- **Migration** (all of it compiles today through the shims, with deprecation warnings):
  - `core::Vector2` / `core::Vector3` -> `Vec2` / `Vec3`; `v.Cross(w)` -> `glm::cross(v, w)`, `v.Normalize()` ->
    `glm::normalize(v)` (or `SafeNormalize`); `ToVector3(glm::vec3)` helpers go.
  - `core::Color` -> `Color`. A game's `Hex` / `WithAlpha(c, k)` / `Mixed` / `Faded` -> `Color::Hex`,
    `c.Faded(k)` (**`Color::WithAlpha` replaces alpha**, it doesn't multiply), `Lerp`.
  - `scene::AABB` / `Ray` / `Plane` / `Frustum` -> `wlw::` (include `wlw/core/geometry.h`, not collision.h);
    `scene::IntersectPlane(ray, plane)` -> `Raycast(ray, plane)` (a distance: `ray.At(*t)` is the point);
    `box.Intersects(b)` -> `Overlaps(box, b)`; `AABB::FromPositionAndSize(p, s)` -> `AABB::FromCenterSize(p, Vec3(s))`;
    `frustum.TestAABB(box)` -> `Overlaps(frustum, box)`.
  - `rendering::PackColor(c)` -> `Color32(c).Packed()`, `UnpackColor(u)` -> `Color32::FromPacked(u).ToColor()`,
    `PackUnitByte` -> `UnitToByte`.
  - A camera subclass overrides `SetPosition(const Vec3&)`, `UpdateSize(const Vec2&)`, `const Vec3& GetPosition()`.
- **Deprecated** (removed in 0.4): `core::Vector2`, `core::Vector3`, `core::Color`, `scene::AABB`, `scene::Ray`,
  `scene::Plane`, `scene::Frustum`, `scene::IntersectPlane`, `scene::TransformBox`, `scene::Union`,
  `AABB::Intersects`, `AABB::FromPositionAndSize`, `AABB::FromPositionAndScale`, `Frustum::TestAABB`,
  `rendering::PackColor`, `UnpackColor`, `PackUnitByte`. The headers `core/vector2.h`, `vector3.h` and
  `collision.h` only hold shims now.

## 0.2.0 (2026-10-18): Phase 1, rendering performance

Quick mobile wins on the current renderer: frame pacing (Swappy on Android, vsync interval or a precise limiter on
desktop, 60 fps by default), render scale with a Quality hook, explicit shader precisions (GLES stays highp; a
GLSL ES compile check in CI), a canvas shader that computes only each quad's own kind (Pixel 4 canvas GPU -54%),
cofactor normals (no inverse; mirrors stay lit outward), the skybox drawn after the opaques, and 52-byte instances
with cached bounds and a `WriteInstances` span API. Details and measurements per step below.

- **Breaking** (migration notes in the steps):
  - Games run at 60 fps by default instead of the display's rate (`AppConfig::target_fps = 0` for the old
    behaviour) (1.1).
  - Android games add Swappy's Java classes to their Gradle source set
    (`../../external/WLWengine/third_party/swappy/java`) and the R8 rule `-keep class com.google.androidgamesdk.** { *; }`;
    without them the engine falls back to vsync + the limiter (1.1).
  - Game mesh shaders' `aInstanceTransform` (mat4) is gone: `aInstanceRow0..2` or `InstanceTransform()` /
    `InstancePosition()` / `InstanceNormal()` (1.6). Instance tints are clamped to 0..1 (RGBA8) (1.6).
- **Deprecated** (removed in 0.3): `rendering::InstanceData`, `Node::SetInstances(vector)`, `Node::GetInstances()`,
  `ParticleSystem::BuildInstances(out)` (1.6).

### Frame pacing (Roadmap 1.1)
- `AppConfig::target_fps` (default 60): frames per second the screen gets; `core::kTargetDisplayRate` (0) = the
  display's own rate, `core::kTargetUncapped` (-1) = no limit (desktop profiling). Changed at runtime with
  `Application::SetTargetFps` (a settings menu: 30 saves battery) or the cvar `r.target_fps`; the engine panel has
  buttons for it and shows the rate frames are paced to, how, and the display's refresh rate.
- Android: **Swappy** (AGDK games-frame-pacing 2.3.0-alpha01, the last source before AGDK's repo was archived;
  vendored in `third_party/swappy`, OpenGL part, Apache-2.0) presents each frame at the vsync it belongs to and
  picks the display mode for the target (a 90 Hz phone switches to 60 Hz for 60 fps). Its auto swap interval is
  off (`r.pacing.auto_interval` turns it on): Quality lowers detail instead of halving the rate. Where Swappy
  isn't available (no `EGL_ANDROID_presentation_time`): vsync + `ANativeWindow_setFrameRate` (Android 11+) +
  the limiter, re-planned when the display changes mode.
- Desktop: a target that divides the monitor's refresh rate uses a vsync interval (60 fps on 240 Hz = every 4th
  refresh); any other target turns vsync off and uses a precise limiter (`platform::PreciseSleep`: a
  high-resolution waitable timer on Windows, then a spin for the last millisecond). The monitor is the one the
  window is on, checked again when the window moves. Measured on a 240 Hz monitor (Snake, Release): 60 fps =
  16.667 ms per frame, standard deviation 0.12 ms; 100 fps (limiter) = 10.000 ms, 0.006-0.017 ms; 30 fps =
  33.32 ms, 0.27 ms. Before this step the game ran at the monitor's 240 fps.
- `core/frame_pacing.h` (tested without a window): `PlanPacing(target, refresh)` (vsync interval or limiter,
  2% tolerance for displays that report 59.94 Hz as 59) and `FrameLimiter` (fixed schedule with an exact
  average; restarts after a stall instead of rushing frames).
- Quality: `QualitySettings::budget_ms` now defaults to 0 = the frame time of the rate frames are paced to
  (`Quality::BudgetMs()`, kept in step by the Application; a change settles the readings). A game that set
  `budget_ms` keeps it.
- `scene::Window`: `SetTargetFps`, `TargetFps`, `SetAutoSwapInterval`, `RefreshRate`, `PacedFps`, `PacingMethod`.
- **Games on Android** add Swappy's Java classes (it calls them over JNI) to their Gradle source set and keep
  them from R8 (see CLAUDE.md "Layout", platform/):
  `java.srcDirs(..., "../../external/WLWengine/third_party/swappy/java")` and, in `proguard-rules.pro`,
  `-keep class com.google.androidgamesdk.** { *; }`. Without them Swappy can't start on Android 9+ (it
  needs its `SwappyDisplayManager`) and the engine falls back to vsync + the limiter (logged as a warning).
- **Behaviour change:** games now run at 60 fps by default instead of the display's rate (120 Hz phones, fast
  desktop monitors). Set `target_fps = 0` for the old behaviour.

### Render scale (Roadmap 1.2)
- `Viewport::SetRenderScale(scale)` (0.25-2, default 1; like Unity URP's renderScale, Godot's scaling_3d_scale):
  the viewport's 3D and its Picture canvas layers are drawn at `RenderSize()` (its size x the scale), then
  stretched to fill it with a bilinear filter (nearest if the viewport isn't `smooth`). Overlay layers, the game
  UI included, are drawn after the stretch at full size and stay sharp. Below 1 the GPU colours fewer pixels
  (0.75 = 56% of them) and the 3D gets softer; above 1 = supersampling. The camera keeps the viewport's shape.
- Post passes keep their sizes (fractions of the viewport's size), so an effect looks the same at every render
  scale. The first pass reads the smaller picture and the last one writes the full-size picture, so with passes
  the stretch costs nothing extra; without passes one copy does it (`rendering::kUpscalePass` in the plan).
- Measured on an RTX 5080 (2560 x 1440, 24 blended lit layers): the 3D's GPU time is 2.20 ms at 1, 1.21 ms at
  0.75, 0.53 ms at 0.5 (the pixel count: 56% and 25%); the upscale copy 0.017 ms. Phones: not measured yet.
- Quality hook: `QualitySettings::render_scales`, one per level, lowest first (levels past the end use the last
  one). The Application sets the screen's render scale when the level changes, just before drawing
  (`Quality::RenderScale()`). Empty (the default) = the engine leaves the scale to the game.
- Cvar `r.render_scale`: 0 (default) = the game's / the quality level's; anything else forces the screen's scale
  (`Viewport::SetRenderScaleOverride`, `EffectiveRenderScale()`), like Unreal's `r.ScreenPercentage`. The engine
  panel has 0.5 / 0.75 / 1 / Auto buttons for it and shows where the scale comes from and the 3D's size;
  offscreen viewports show theirs, and the upscale copy is listed with its GPU time.
- `RenderStats::render_width/height`. `PlanPostChain` and `PostChain::Prepare` take the scene picture's size as
  an optional last argument (existing calls are unchanged). The renderer never makes a scene picture larger than
  the GPU's largest texture (a warning, then scaled down to fit).
- `AppConfig::screen_depth` / `WindowOptions::depth` (default true): false creates the screen surface without a
  depth buffer (EGL / GLFW), saving its memory (~17 MB on a 1440p phone) for games whose screen always has post
  passes or a render scale below 1; such a screen draws its 3D through its scene picture, at scale 1 too (one
  copy). `Window::ScreenHasDepth()`; the engine panel says so.
- Fixed: `Canvas::Text` kept a raw pointer to its `Font` until the frame was drawn, so text from a font the caller
  had let go of drew nothing (or read freed memory). `UIBatch::font` is now a `shared_ptr<const Font>` (`Font` is
  `enable_shared_from_this`; fonts only exist in shared_ptrs) and the canvas keeps its fonts until the next frame.
- Fixed: a canvas shader's `uSize` in a Picture layer of a render-scaled viewport was in full-size pixels; it is
  now in the pixels the layer is drawn at.

### Fragment precision (Roadmap 1.3a)
- GLES shaders stay highp by default (floats and ints, both stages); a shader opts in to fp16 with its own
  `precision mediump float;`. The plan was a mediump fragment default, and the Mali Offline Compiler favoured
  it (lit meshes 44-48 -> 31 work registers on Mali-G76/G78), but the Pixel 4 (Adreno 640) measured it slower:
  Bluzzle's lit 3D pass 6.21 ms vs 5.75 ms highp (+8%; its UI pass identical, so the clocks were equal), and
  shaders that start mediump and switch back to highp were slower still (6.96 ms). Opting the post effects in
  made Threadlock's blur + threshold 2% slower (1.27 vs 1.24 ms). So no engine shader opts in until a phone
  shows a gain (a Mali phone or Test Lab, 6.6). Desktop GL ignores precision (golden images identical).
- `rendering/glsl_header.h`: `GlslHeader(dialect, stage)` builds the text before every stage (GL-free, tested);
  `kGLSLHeader` (gl.h) is gone. Every sampler type has a default precision, the same in both stages: sampler2D,
  3D, Cube, 2DArray mediump; shadow and integer samplers highp. Fixed: a game shader using `sampler3D`,
  `sampler2DArray`, a shadow or an integer sampler failed to compile on phones only (ES has no default for
  them), and `sampler2D`'s ES default was lowp (spec range +-2: HDR reads from RGBA16F pictures could clip).
- Every engine uniform, uniform-block member and varying names its precision (GLSL ES needs both stages to
  agree, whatever default each uses): highp for positions, matrices, texture / pixel / canvas coordinates,
  shadow coordinates, distances, attenuation, spot cones and the specular term; mediump for colours, normals
  and light colours and directions. A test lints the shader files for it. `wlw/lighting.glsl` keeps its
  positions, distances and specular highp inside, so a game shader that opts in still lights correctly.
- **Games' shaders**: highp unless the text says `precision mediump float;` after `shader_type`; `shader.h` has
  a Precision section (what fp16 can't hold, measure before keeping it). The preludes say each input's precision.
- Cvar `r.mediump` (default 1, read-only after start): 0 turns every `precision mediump float;` opt-in into
  highp (`ApplyMediumpSetting`), to A/B a phone's GPU time or check whether an artifact is a precision one. The
  startup log and the engine panel show what mediump is on the GPU (`Window::FragmentPrecision()`: "16-bit",
  "32-bit: no gain here", "ignored (desktop GL)").
- `WindowOptions::gles` (desktop): an OpenGL ES 3.0 context, and `platform::CurrentContextIsES()`; the GPU tests
  (`gpu: glsl 330`, `gpu: glsl es 300`) compile and link every engine program (32 mesh variants, shadow, sprite,
  skybox, UI, post effects) and sample game shaders (a uniform and a sampler in both stages, a mediump opt-in,
  3D / array / integer / shadow samplers) in both languages, on CI's Mesa too; both Mesa and NVIDIA refuse a
  precision mismatch. `wlw_gpu_tests --dump-shaders <dir>` writes the final sources (`SetShaderSourceObserver`)
  for the Mali Offline Compiler.

### Canvas shader (Roadmap 1.3b)
- `ui.frag` computed all four coverage functions (text's texture read, rect, triangle, the arc with a `sin` and
  `cos`) for every pixel of every quad, because each one called `fwidth()`. Now each pixel computes only its own
  quad's kind: the kind is a flat varying, and each kind gets its anti-aliasing width from the uv's derivatives
  through the chain rule (shapes: their distance's gradient; triangles: the nearest edge's derivatives). The
  arc's `sin`/`cos` of its half sweep moved to `ui.vert` (flat). Derivatives and texture reads sit inside the
  branches: a 2x2 pixel block always comes from one triangle, so all four of its pixels take the same branch
  (quad-uniform, what Vulkan requires; GLSL's spec calls it undefined, every GPU handles it). The owner chose
  this over `textureGrad` with derivatives before the branch (the plan): it makes images 4.7x cheaper on Mali.
- Text: the anti-aliasing width comes from the distance field's range in screen pixels (the formula MSDF uses,
  so 10.2 keeps it): `Font::DistanceRange()` (atlas pixels for the field's 0 -> 1, ~2 x padding) goes to the
  shader in `UIVertex::params.z`, the atlas size from `textureSize`.
- Mali Offline Compiler, arithmetic cycles before -> after (Mali-G78): longest path 2.62 -> 1.38, image 2.62 ->
  0.16, text 1.25, rect 1.38, triangle 1.25, arc 1.62; work registers 28 -> 22. Mali-G52: longest 2.63 -> 2.08,
  registers 31 -> 24. `ui.vert` writes two more varyings (G78 load/store 6 -> 7 cycles, 4 vertices per quad).
- Pixel 4 (Adreno 640), Threadlock uncapped, alternating runs of engine main and this step (averages of 25
  samples per run): canvas GPU time 4.96 -> 2.30 ms (-54%), GPU frame 7.08 -> 5.0 ms. Its post passes read
  1.24 -> 1.56 ms although their shaders didn't change: the lighter frame lets the GPU clock down (DVFS).
- Pictures: edges move by less than a pixel's shading (max 48/255 on 2% of the canvas golden image's pixels;
  identical to the eye); new `canvas` golden reference from CI.

### Cofactor normals (Roadmap 1.4)
- Normals are carried by the cofactor matrix instead of `transpose(inverse(m))`: three cross products of the
  transform's columns, negated when the transform mirrors (det < 0). Same directions once normalized, but no
  division by the determinant: a node scaled to 0 on an axis gets finite normals (the inverse gives inf / NaN), and
  mirrored nodes are lit on their outside (the bare cofactor would point into them). Like Filament.
  `core::NormalMatrix(mat3 / mat4)` (`core/normal_matrix.h`) and its GLSL twin `NormalMatrix()` in
  `wlw/common.glsl` (with `SafeNormalize`). `NormalMatrix(a * b) == NormalMatrix(a) * NormalMatrix(b)`, so an
  instanced draw gets the node's part from the CPU and multiplies in each copy's part per vertex.
- CPU: `CullScene` computes it for every visible node (was a 4x4 `inverse` per plain node; MSVC 15.5 -> ~4 ns).
  `render/cull` benchmark, Windows Release (Ryzen 7 9800X3D, alternating runs, medians of 3): 10k nodes 54.7 ->
  39.5 us (-28%), 1k 6.0 -> 4.2 us (-30%). The benchmark's comment said the camera sees half the grid: it sees
  about a tenth (917 of 10k).
- GPU: `uNormalMatrix` is uploaded for every mesh draw. `mesh.vert` INSTANCED no longer inverts a 4x4 per vertex,
  and positions go through two matrix-vector products instead of a per-vertex matrix product (`shadow.vert`
  INSTANCED too). Mali Offline Compiler, varying variant arithmetic cycles / work registers (Mali-G78): instanced
  mesh 6.50 -> 2.40 / 64 -> 32 (lit + shadows 6.97 -> 2.90), position variant 3.40 -> 1.90; instanced shadow
  4.90 -> 1.90; plain mesh unchanged. Mali-G76: instanced mesh 8.08 -> 3.00 / 60 -> 30; instanced shadow 5.70
  (arithmetic-bound) -> 1.87. 64 registers ran at half occupancy; 32 run at full.
- Game mesh shaders: `uniform highp mat3 uNormalMatrix` in both stages; `vertex()` gets the helpers
  `WorldPosition(local)` and `WorldNormal(normal)` (instancing included: what the engine's vertex stage does), and
  the default vertex stage uses them. `vNormal` is unit length at the vertices. Mali-G78, default vertex stage:
  arithmetic 6.31 -> 2.28 (plain path 4.31 -> 0.97), registers 64 -> 26 (full occupancy); load/store, the bound,
  14 -> 16 cycles on the instanced path, 10 -> 11 on the plain one. A form with one branch over all of main()
  keeps load/store at 14 but needs 48 registers (half occupancy).
- glTF loading bakes node transforms into normals with `core::NormalMatrix` too.
- Tests: `normal_matrix` suite (1000 random rotations x shears x scales, a quarter of the axes mirrored, against
  double-precision inverse-transpose; mirrors point outward; flat transforms stay finite; the product rule);
  `gpu: normals match inverse-transpose` draws unevenly scaled, mirrored, sheared (parent / child) and
  instanced squashed + mirrored shapes with the engine's lit shader and a game shader, against the same shapes
  baked on the CPU with inverse-transpose normals (no stored reference; turning off the mirror flip on the CPU or
  in GLSL fails it with 3% of pixels). Golden references unchanged.

### Skybox after opaques (Roadmap 1.5)
- The skybox is drawn after the opaque draws and before the transparent ones, at depth 1.0 with the depth test
  `LEQUAL` (set only around the sky draw; the engine's default stays `LESS`) and no depth writes, like Unreal,
  Unity URP and Godot: the early depth test skips every sky pixel an opaque mesh covers, and transparent meshes
  still blend over the sky. A viewport without a depth buffer draws the sky first, as before.
- The sky is one triangle over the viewport (`skybox.vert`, no vertex buffer; was a 36-vertex cube) whose corners
  carry their view rays from `rendering::SkyRayMatrix(view, projection)` (`rendering/sky.h`, computed on the CPU
  once per draw: `inverse(mat3(view)) * inverse(projection)`; the rays are linear on the far plane, so the
  interpolation is exact). `SkyDrawPosition(sorted draws, has_depth)` picks its place in the draw order.
- Orthographic cameras now get a sky over the whole viewport (the cube used to show as a small square in the middle).
- RTX 5080, Windows Release, 2560x1440, an opaque wall over 89% of the picture, alternating runs (3 x 3 rounds of
  1000 frames): the sky's GPU cost 0.0124-0.0139 ms -> -0.0001-0.0019 ms (about 90% less = the covered share).
  Mali Offline Compiler (Mali-G78), `skybox.vert` position variant: arithmetic 0.78 -> 0.47 cycles, load/store
  3 -> 2, work registers 16 -> 11, and 3 vertices instead of 36; `skybox.frag` unchanged.
- Tests: `sky` suite (rays through the pixels for a moved and turned perspective camera, exact interpolation over
  the triangle, translation dropped, orthographic rays ahead, the sky's place in the draw order); golden images
  `gpu: skybox` (sky behind an opaque cube, a see-through cube blending over both), `gpu: skybox orthographic`,
  `gpu: skybox without depth buffer`.

### Instances (Roadmap 1.6)
- `rendering::Instance` (`rendering/instance_buffer.h`): 52 bytes per instanced copy (was 80: a `mat4` + a float
  colour), the layout of Godot's GLES3 renderer: the top 3 rows of the copy's affine transform (`.w` = translation)
  + an RGBA8 tint. `Instance::FromTransform(m, tint)`, `SetTransform`, `Transform()`. The GPU reads the tint as 4
  bytes scaled to 0..1.
- `Node::WriteInstances(count)` returns the copies as a span to fill in place, like `SpriteBatch::Write`: the first
  min(old, count) keep their values, new ones start as identity + white, each call is one change (one upload, one
  bounds update). `Instances()`, `InstanceCount()`, `ReserveInstances(n)`, `ClearInstances()`, `IsInstanced()`.
- Instance bounds: `Node::InstanceBounds()` = the node-space box of every copy (each copy's transform applied to the
  model's box by Arvo's method), cached until the next write or until the model's box changes, so a moving node
  costs nothing and copies written once are measured once. `SetInstanceBounds(box)` sets a fixed box instead (like
  Godot's `custom_aabb`: a particle field's volume, or displacement a shader adds); `nullopt` computes it again.
  `GetAABB()` moves boxes to world space with `scene::TransformBox` (Arvo, `core/collision.h`, exact for a box:
  2 matrix-vector products instead of 8 corners), per mesh for plain nodes (as tight as before), the cached bounds
  for instanced ones. `scene::Union(a, b)` too.
- `fx::ParticleSystem::WriteTo(node)` writes the particles straight into the node's copies (like
  `FlowParticles::WriteTo`), reserving its capacity there once. The rows of T * Rx * Ry * Rz * S are written in
  closed form instead of 4 matrix products, sines and cosines by `core::SinCos` (new `core/math.h`: one Cody-Waite
  reduction for both, Cephes polynomials, quadrants without branches; max error 1.2e-7 over +-60 rad), colours by a
  `PackColor` without `std::lround` (the same bytes for every float in 0..1, checked exhaustively; NaN = 0).
- Windows Release (Ryzen 7 9800X3D, MSVC, alternating runs against main, medians of 3): `fx/particles instances 5k`
  315 -> 120 us (-62%); `render/gather` 10k 659 -> 403 us (-39%), 1k 57.6 -> 31.1 us (-46%) (Arvo boxes + no
  reference counting in `GetModel`); new `render/instance bounds 5k` = 20 us (one write + the bounds after it; before,
  every frame multiplied each copy's matrix by the node's and moved its 8 corners). Bytes uploaded per copy 80 -> 52.
  `transform/move flat` 1k reads 4-9% slower (the node is ~90 bytes bigger: the bounds cache; Phase 2.2 rewrites
  the node). The plan's scratch replica estimated 73 us for the particles: MSVC's `SinCos` costs ~4 ns a call here.
- Shaders: `wlw/instance.glsl` (attributes `aInstanceRow0..2` at locations 4-6, `aInstanceTint` at 7;
  `InstancePosition(p)` = 3 dot products, `InstanceNormal(n)` = the cofactor of the rows (cross products of rows),
  `InstanceTransform()`), used by `mesh.vert` and `shadow.vert` INSTANCED and by game mesh shaders. Mali Offline
  Compiler, longest path load/store cycles (the bound) and work registers, before -> after: Mali-G78 instanced mesh
  varying 14 -> 13 (32 registers, full occupancy), lit + shadows 15 -> 14, position variant 7 -> 6 (29 -> 24
  registers), instanced shadow 7 -> 6, game default vertex stage 16 -> 13; Mali-G76 instanced mesh 17 -> 16,
  lit + shadows 19 -> 18, position 5 -> 4, shadow 5 -> 4, game default 19 -> 16. Plain mesh variants keep their
  cycles. `mesh.vert` declares its varyings with `vUV` first: with `vColor` first the G78 unlit instanced variant
  needs 50 registers (half occupancy).
- `Node::GetModel()` / `GetMaterial()` return `const std::shared_ptr<...>&` (were copies: an atomic increment and
  decrement per call, per node per frame in the scene walk). Source-compatible; a reference kept across `SetModel`
  / `SetMaterial` now sees the new one.
- Tests: `instances` suite (layout and transforms, `PackColor` against `lround` on every 997th float in 0..1 and at
  each byte's halves, `SinCos` against double precision over +-1000 rad and at the quadrant edges, `TransformBox` and
  `InstanceBounds` against brute-force corners for random affine transforms with mirrors and shears,
  `WriteInstances` keeping / adding / trimming copies and keeping its storage, the cache following writes, a new
  model, new vertices and the override, plain boxes against each mesh's corners, the shims); `write to matches the
  matrices glm builds` (2000-degree rotations, 1e-5). Golden references unchanged (`gpu: instancing`, `gpu: normals
  match inverse-transpose`).
- **Deprecated** (removed in 0.3): `rendering::InstanceData` -> `rendering::Instance`; `Node::SetInstances(vector)`
  -> `WriteInstances(count)` and fill the span; `Node::GetInstances()` (now converted copies, by value) ->
  `Instances()`; `ParticleSystem::BuildInstances(out)` -> `WriteTo(node)`.
- **Breaking:** game mesh shaders' `aInstanceTransform` (mat4) is gone: use `aInstanceRow0..2`, or
  `InstanceTransform()` / `InstancePosition()` / `InstanceNormal()` (`WorldPosition` / `WorldNormal` are unchanged,
  and no game shader used the attribute). Tints are clamped to 0..1 (8 bits each).
- **Migration:** particles: `particles.WriteTo(*node)` once per frame instead of `BuildInstances` +
  `SetInstances`, and `node->WriteInstances(0)` at setup instead of `SetInstances({})`. Copies that don't move
  (a grid of cubes turning together): put the motion on the node's transform and write the copies once: uploaded
  once, measured once.

## 0.1.0 (2026-10-15): Phase 0, safety net and tooling

The first tagged version: tests (doctest, golden images, benchmarks), CI on Windows, Linux and Android, logging,
asserts, cvars and a console, the Tracy profiler, build hygiene (warnings, optimized engine in Debug, PCH,
vendored dependencies, clang-format) and versioning. Details per step below.

- **Breaking** (migration notes in the steps): `wlw/core/logger.h` and `FAIL_IF` removed (0.4); `tests/check.h`
  removed (0.1). Code that got `<sstream>` through `wlw/scene/node.h` includes it itself (0.4).
- **Deprecated:** nothing yet.

### Versioning (Roadmap 0.9)
- `wlw/version.h`, generated from `project(VERSION)` in the top-level `CMakeLists.txt` (template
  `engine/wlw/version.h.in`): `WLW_VERSION_MAJOR/MINOR/PATCH`, `WLW_VERSION_STRING`, `WLW_VERSION` (one comparable
  number: major * 10000 + minor * 100 + patch), `WLW_MAKE_VERSION` and `WLW_VERSION_AT_LEAST(major, minor, patch)`
  for game code that has to build against two engine versions.
- `wlw/core/build_info.h`: `GetBuildInfo()` (version, git commit, dirty flag, build type) and `BuildInfoText()`
  ("WLWengine 0.1.0 (dfda300c49-dirty, Debug)"). The git commit is read on every build by `cmake/GitInfo.cmake`
  (target `wlw_git_info`), which rewrites its generated source only when the commit or dirty flag changes, so an
  unchanged tree rebuilds nothing. "unknown" without git. The application logs it at startup (first line of a
  debug build's `log.txt`); the engine panel shows it.
- `wlw/core/deprecated.h`: `WLW_DEPRECATED(0.3, "Use X()")` (a `[[deprecated]]` naming the version and the
  replacement), `WLW_DISABLE_DEPRECATION_WARNINGS_BEGIN/END` for code that must still use a shim (its tests), and
  the CMake option `WLW_NO_DEPRECATED` (off) that compiles every shim out, so a game can check it has migrated.
- `cmake/CheckDeprecations.cmake` (CTest test `deprecations`, plus tests of the script itself): every shim in
  `engine/` and `kit/` must name the current or the next minor version, so a shim ships in exactly one release
  and has to be deleted before the one after.
- Fixed (on `main` before this step): MSVC Release warning C4723 in `IntersectPlane` broke the Windows Release
  CI job since 0.8.

Migration: none needed.

### Build hygiene (Roadmap 0.8)
- Warnings: `wlw_set_warnings` (engine, kit, tests, benchmarks, and games through `wlw_add_game`) is now MSVC
  `/W4 /permissive-`, clang/gcc `-Wall -Wextra -Wshadow`. Two warnings stay off: unused parameters (virtual hooks)
  and missing field initializers (configs filled with designated initializers that leave defaults out on purpose).
  The warnings this found are fixed (`primitives.h` double->float, `ShaderMaterial::SetParam` parameter hiding
  `Material::name`, `audio.cpp` bool vs `ma_bool32`, an unused variable in non-profile builds).
- `WLW_WARNINGS_AS_ERRORS` (`/WX`, `-Werror`): off by default, on in every engine preset (so in CI). Files that
  only compile third-party implementations (`miniaudio_impl.cpp`, `libs_impl.cpp`) build without warnings.
- `WLW_OPTIMIZE_ENGINE_IN_DEBUG`: in Debug builds, the engine, kit and third_party compile at `-O2 -g` (MSVC
  `/O2 /Ob2`, no `/RTC1`), while the game, tests, samples and benchmarks stay at -O0 (like Unreal's DebugGame). The
  Debug runtime and `WLW_ASSERT` stay on. On by default when the engine is a submodule; off when it's the
  top-level project. Debug-build benchmarks on the owner's PC: `render/gather 10k` 16.2 ms -> 0.81 ms,
  `fx/particles instances 5k` 8.7 ms -> 0.62 ms, `ui/text 1k glyphs` 0.57 ms -> 0.18 ms. Header-only code (`node.h`
  transforms, templates) compiles inside the caller and keeps the caller's flags.
- `WLW_PCH` (on): the engine precompiles the standard library and glm headers it uses most. Clean `wlw` Debug
  build 6.8 s -> 4.6 s on the owner's PC (one job: 41 s -> 22 s). The Android CI job builds with it off, so a
  missing `#include` still fails there. ccache in CI gets the sloppiness settings PCH needs.
- glm 1.0.1 is vendored in `third_party/glm` (target `wlw_glm`, public through `wlw::wlw`), per D13. vcpkg now
  provides only GLFW; Android no longer downloads glm with FetchContent.
- clang-format: `cmake -P cmake/ClangFormat.cmake` formats `engine/`, `kit/`, `tests/`, `benchmarks/` and
  `samples/` with a pinned clang-format 23.1.3 (downloaded once into `build/tools/`, nothing to install);
  `-DCHECK=ON` only checks. A new CI job `clang-format` runs the check. `.clang-format` now describes the style
  the code was written in (Google-based, 120 columns, `T& x`, short `if`/`case`/functions on one line, includes
  not reordered). Every file was reformatted once, in its own commit, listed in `.git-blame-ignore-revs`.
- CI: actions moved to their Node 24 versions (checkout v7, cache v6, upload-artifact v7, ccache-action 1.2.24);
  `ilammy/msvc-dev-cmd` (still Node 20) replaced by a step that runs VS's `vcvars64.bat`.

Migration: none needed. Games get the stricter warnings as warnings (not errors) and the optimized engine in
Debug builds; `-DWLW_OPTIMIZE_ENGINE_IN_DEBUG=OFF` steps through engine code at -O0. Games can drop `glm`
from their `vcpkg.json`.

### Benchmarks (Roadmap 0.7)
- `benchmarks/`: `wlw_benchmarks`, engine microbenchmarks on nanobench 4.3.11 (vendored in `third_party/nanobench`,
  MIT). No window, no GPU, fixed seeds; one op = one frame's worth of the work. Covered so far: `render/gather`,
  `render/cull` and `render/sort` at 1k and 10k nodes, `transform/move flat|nested` at 1k and 10k,
  `fx/flow update|write 20k`, `fx/particles update|instances 5k`, `ui/text 1k glyphs`, `audio/mix 24 voices`.
  A file adds a group with `WLW_BENCHMARK("group") { ctx.Run("group/name", [&] { ... }); }`.
- `wlw_benchmarks [filter] --out run.json --baseline base.json [--markdown file] [--threshold 25]` writes the
  results as JSON and compares them with a baseline (baseline, now, % change). A regression past the threshold
  is a warning, never a failure. `--smoke` runs each benchmark a few times without measuring: the CTest test
  `bench: smoke` (label `bench`) does that in every test run.
- Baselines in `benchmarks/baselines/`: `windows-local.json` (Ryzen 7 9800X3D, MSVC Release), `pixel4.json`
  (Pixel 4, NDK clang Release, pinned to the prime core with `taskset 80`), `linux-ci.json` (GitHub runner,
  clang Release).
- CI: a new workflow `bench.yml` (Linux clang Release) runs the benchmarks on main, on manual runs and on
  `bench/**` branches. It puts the comparison in the job summary and uploads the JSON as the `bench-linux-ci` artifact.
- Android: `wlw_benchmarks` builds as an arm64 executable, to run over `adb push` + `adb shell`.
- Renderer: the GL-free part of drawing a viewport moved out of the GL driver into
  `wlw/rendering/scene_gather.h`. `GatherScene` walks the scene (lights, drawables with world boxes, sprite
  batches, shadow caster / receiver boxes). `CullScene` does frustum culling, model and normal matrices, and view
  depths. The driver keeps the program and texture lookups. Pictures are unchanged (golden images).

Migration: none needed.

### Tracy profiler (Roadmap 0.6)
- `wlw/core/profile.h`: `WLW_PROFILE_SCOPE("name")`, `WLW_PROFILE_FUNCTION()`, `WLW_PROFILE_SCOPE_DYNAMIC(name)`,
  `WLW_PROFILE_TEXT`, `WLW_PROFILE_FRAME`, `WLW_PROFILE_PLOT`, `WLW_PROFILE_THREAD_NAME`, `WLW_PROFILE_MESSAGE`.
  They feed Tracy 0.14.1 (vendored in `third_party/tracy`, BSD-3) and, on Android, ATrace sections and counters
  (Perfetto / Android Studio). Built in only with the CMake option `WLW_PROFILE` (off by default everywhere);
  without it the macros are empty and their arguments aren't evaluated. `TRACY_ON_DEMAND`: nothing is recorded
  until a viewer connects. A profile build takes ~0.5 s longer to start (Tracy calibrates its timer).
- New presets `windows-profile` and `android-arm64-profile`: RelWithDebInfo (optimized, with symbols), Tracy on,
  debug UI on.
- Zones: every Application phase; each system's hooks, named after the system's type (`System::Name()`, set by
  `AddSystem` through the new `core::TypeName<T>()`); the renderer (draw window, each viewport, gather, sort,
  upload, shadow pass, main pass, canvas, each post pass by name, overlays, present, shader warm-up and every
  program compile); input polling; the audio callback and decode thread (named threads); particle updates; file
  reads, image and glTF loads. Frame marks after present. Plots: frame us, draw calls, program changes, texture
  binds, instance bytes, mixer voices, quality level, GPU us. Log records show on the timeline while a viewer is
  connected.
- GPU zones (`wlw/rendering/gl_gpu_profile.h`, GL backend): Tracy's OpenGL timestamp zones for the shadow pass,
  main pass, canvas, post passes and overlays. On GLES they need EXT_disjoint_timer_query (looked up at run time).
  The query context is made when a viewer first connects. The cvar `profile.gpu_zones` turns them off.
- Engine panel: "Profiler: Tracy connected / waiting" in profile builds.
- CI: the Android job builds with `WLW_PROFILE=ON`.

Migration: none needed. `SystemList::Add` takes an optional name.

### Console variables and console (Roadmap 0.5)
- `wlw/core/cvar.h`: `CVar<bool|int|float|std::string>{"r.name", default, "help", flags, range}`, with dotted
  lowercase names. Flags: `Saved` (kept in `<save dir>/cvars.cfg`), `ReadOnly` (only before the game starts),
  `Cheat` (changeable from outside the code only in debug builds). `Get`, `Set`, `Reset`, `OnChange`;
  `FindCVar`, `AllCVars`, `SetCVar`.
- Where values come from, later ones winning: the code default, `wlw.cfg` in the game's assets, the player's
  `cvars.cfg`, the command line (`game.exe +r.name 0 +log.filter=Render=Verbose`) or, on Android,
  `adb shell am start -n <package>/<activity> --es wlw "r.name=0;log.filter=Render=Verbose"`, then the console.
  A value for a name that doesn't exist yet waits for it; at startup an unknown name is logged as a warning.
- `wlw/core/console.h`: `ConsoleCommand` (registered while it exists), `ExecuteConsoleLine` (a command,
  `cvar`, `cvar value`, `cvar = value`), built-in `help`, `find`, `set`, `reset`, `exec`, and `quit` from
  the Application. Command output goes to the log (category Console).
- Engine cvars (`wlw/app/engine_cvars.h`): `log.filter`, `r.gpu_timing`, `r.invalidate_framebuffers`,
  `quality.lock`. The Engine panel's checkboxes and quality buttons now set them.
- Debug UI: a console window (the ` key or the "Console" button) with the log, level and text filters, and
  an input line with history and Tab completion. A "CVars" panel lists every cvar by prefix, with widgets
  for its type and a reset button. ImGui now gets the keyboard and typed text. On phones the console only
  shows the log; typing there needs GameTextInput (roadmap D9).
- Input: new keys `Tab`, `Backspace`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`, `Control`, `Shift`,
  `Grave`; `Text()` (typed characters); `SetKeyboardBlocked` and `Raw*` key queries (the game's keys are
  blocked while the console has the keyboard).
- Platform: `LaunchArguments()` (desktop `argv`; Android the launch intent's `wlw` extra). `WLW_MAIN`'s
  `main` now takes `argc` and `argv`.

Migration: none needed. Code that switches over `input::Key` without a `default` gets new cases to handle.

### Logging and asserts (Roadmap 0.4)
- `WLW_LOG(Category, Level, "fmt", ...)`: levels Verbose, Debug, Info, Warning, Error, Fatal; engine categories
  `LogCore`, `LogApp`, `LogRender`, `LogShader`, `LogAudio`, `LogPlatform`, `LogUI`, `LogFX` (kit: `LogKit`); games
  declare their own with `WLW_LOG_CATEGORY(LogMyGame)`. Every engine message now has its category.
- Levels below `WLW_LOG_COMPILE_LEVEL` (Debug in debug builds, Info in release) compile to nothing.
- Runtime filters: `SetLogFilter("Render=Verbose,Audio=Warning,*=Info")`, `SetLogLevel`; on desktop the
  `WLW_LOG` environment variable sets one at startup.
- Records carry time, thread id (`T0` = main), category, level and file:line. Desktop lines read
  `[12.345][T0][Render][warning] text`; logcat gets `[Render] text` with the matching priority.
- Sinks: the platform output, an in-memory history of the last 512 records (`RecentLogs`), a log file
  (`StartLogFile`, flushed every line; debug builds write `<save dir>/log.txt` and keep the run before as
  `log.txt.prev`), and `AddLogSink` callbacks. Any thread may log. A Fatal record stops the program after
  it's written everywhere.
- `wlw/core/assert.h`: `WLW_ASSERT` (debug builds; release with the CMake option `WLW_ENABLE_ASSERTS`),
  `WLW_VERIFY` (always evaluated: logs an Error and returns false), `WLW_UNREACHABLE`, and
  `SetAssertHandler` for tests. A debugger, if attached, breaks on failures.
- Platform: `platform::WriteLog(const LogRecord&)`, `IsDebuggerAttached()`, `BreakIntoDebugger()`.
- Removed `wlw/core/logger.h` (`FAIL_IF`).

Migration:
- `WLW_LOG_INFO/WARN/ERROR` and `wlw::Log` still work (category General).
- `if (FAIL_IF(bad, "msg " << x)) return;` becomes `if (!WLW_VERIFY(!bad, "msg %s", x)) return;`. The condition
  is inverted (VERIFY states what must hold), and the message is printf-style. Include `wlw/core/assert.h`.
- Code that got `<sstream>` through `wlw/scene/node.h` (it included `logger.h`) must include it itself.

### Golden images (Roadmap 0.3)
- `tests/gpu`: a new test binary `wlw_gpu_tests` (CTest label `gpu`) draws 7 fixed scenes (lit cubes, shadows,
  transparency, instancing, sprites, bloom, canvas shapes + text + images) into an offscreen viewport of a
  hidden window and compares them with `tests/gpu/references/*.png`. On a mismatch the actual picture and a
  diff (bad pixels in red) go to `build/<preset>/tests/gpu/gpu_output/`; `wlw_gpu_tests --update` rewrites
  the references. References come from Mesa llvmpipe (strict); a real GPU compares more loosely.
- CI runs them on Linux under xvfb with llvmpipe and uploads the output when they fail. Windows CI skips them.
  CI no longer runs for pushes that change only Markdown files.
- `tests/support` (`wlw_test_support`): PNG load/save and image comparison, unit-tested in `wlw_tests`.
- Engine: `Window::Create(size, title, WindowOptions{.visible = false})` makes a hidden window (desktop);
  `RenderingDriver::ReadPixels(RenderTarget&)` reads a target back as RGBA8, top row first;
  `RenderingDriver::GpuName()`.

Migration: none; `Window::Create`'s new argument has a default. Code that implements `RenderingDriver` (only
the engine's GL driver does) must add `ReadPixels` and `GpuName`.

### CI (Roadmap 0.2)
- GitHub Actions (`.github/workflows/ci.yml`) on every push: Windows MSVC Debug (Release too on `main` and
  manual runs), Linux clang Debug with ASan + UBSan, Android arm64 (NDK 30, API 24: engine, kit and Snake).
  Every test runs (`ctest -j`); vcpkg packages and ccache are cached; a newer push cancels the older run.
- New presets: `linux-asan`, `android-arm64` (needs `ANDROID_NDK_HOME`) and test presets
  (`ctest --preset <name>`), so each CI job runs the same way locally.
- `WLW_SANITIZE` (`address;undefined`, off by default) builds every target with those sanitizers.
- `WLW_BUILD_SAMPLES` works on Android (off by default there); Snake builds as `libwlw_snake.so`.
- Linux: the engine links pthread, dl and m (miniaudio).

### Tests (Roadmap 0.1)
- The engine tests use doctest 2.4.12 (vendored in `third_party/doctest`). The 24 suites are one binary,
  `wlw_tests` (one `TEST_SUITE` per file, a shared main in `tests/test_main.cpp`); every `TEST_CASE` is
  its own CTest test (`doctest_discover_tests`), so `ctest -j` runs cases in parallel and a failure
  names the case and prints both values.
- Snake's rule tests use doctest too (`snake_game_tests`, CTest names `snake: <case>`).
- Removed `tests/check.h`; `Near()` moved to `tests/test_helpers.h`.

Migration (only for code that wrote tests against `check.h`): include `test_helpers.h`, turn each test
function into `TEST_CASE("...")`, drop `main()`, and put compound conditions in double parentheses:
`CHECK((a && b))`.
