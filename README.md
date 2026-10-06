# WLWengine

A small C++20 / OpenGL graphics engine for building simple games (Windows now, mobile later).

## Building (Windows)

Requirements: Visual Studio 2026 with the **Desktop development with C++** workload, and Git (with Git LFS, which ships with Git for Windows).

```powershell
git clone --recursive git@github.com:aboelbisher/WLWEngine.git
cd WLWEngine
git lfs pull
WLWengine\vcpkg\bootstrap-vcpkg.bat      # one-time, builds vcpkg.exe
```

Then open `WLWengine\WLWengine.slnx`, pick **Debug** or **Release** (x64) and build.
The first build downloads and installs the dependencies listed in `WLWengine\vcpkg.json`
(currently `glm`) into `WLWengine\vcpkg_installed\`, so it takes a bit longer.

Already cloned without `--recursive`? Run `git submodule update --init`.

Build output goes to `WLWengine\build\x64\<Config>\`. Assets are loaded relative to the
working directory, which Visual Studio sets to the project folder (`WLWengine\WLWengine\`).

## Layout

| Path | What |
|---|---|
| `WLWengine/WLWengine/root/` | Engine source (core, rendering, scene, os, utils) |
| `WLWengine/WLWengine/game/`, `platformer/` | Sample games |
| `WLWengine/WLWengine/External/` | Vendored GLFW + GLAD |
| `WLWengine/vcpkg/` | vcpkg (git submodule, pinned) |
| `WLWengine/vcpkg.json` | Dependency manifest |

## Dependencies

Add a library by putting its vcpkg port name in `WLWengine/vcpkg.json` → `dependencies`.
Visual Studio installs it automatically on the next build.

## Git workflow

- Make a branch per feature (`git switch -c feature/snake`), commit small and often, merge to `main`.
- Avoid `git commit --amend` on commits you've already pushed (it forces a force-push).
- Binary assets (`*.png`, `*.jpg`, `*.bin`, `*.glb`, `*.lib`) are stored with Git LFS (see `.gitattributes`).
