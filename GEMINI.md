# WLWengine - C++ Graphics Engine

A modular C++ graphics engine with a focus on 3D rendering, using OpenGL as the primary backend.

## Architecture Overview

The engine follows a modular design with clear separation between core data structures, rendering logic, scene management, and platform-specific implementations.

### Core Modules
- **`root/core/`**: Fundamental building blocks.
  - `vector2.h`, `vector3.h`: Mathematical vector types.
  - `color.h`: RGBA color representation.
  - `vertex_2d.h`, `vertex_3d.h`: Vertex definitions.
  - `mesh.h`, `model.h`: 3D geometry and model containers.
  - `logger.h`: Logging utility.
- **`data_helper.h`**: Procedural mesh generation (Cube, Sphere, Pyramid Frustum).
- **`root/rendering/`**: Rendering abstractions and OpenGL implementation.
  - `rendering_driver.h`: Abstract interface for the renderer.
  - `rendering_driver_gl.cpp`: OpenGL-specific implementation.
  - `material.h`, `material_gl.cpp`: Material system with texture and lighting support.
  - `vertex_buffer.h`, `index_buffer.h`: GPU buffer abstractions.
  - `shaders/`: Contains shader definitions (embedded in `basic_shaders.h`).
- **`root/scene/`**: Scene graph and entity management.
  - `node.h`: Base class for the scene graph hierarchy (Node2D, Node3D).
  - `window.h`, `window.cpp`: Window management (uses GLFW).
  - `camera_3d.h`, `fps_camera.h`: Camera systems.
- **`root/os/`**: Operating system and hardware abstractions.
  - `input_engine.h`, `input_engine_gl.cpp`: Keyboard and mouse input handling.
- **`root/utils/`**: Utility libraries and loaders.
  - `image_loader.h`: Wrapper for `stb_image`.
  - `gltf_loader.h`: Loader for GLTF models using `tiny_gltf`.
  - `texture.h`: Texture resource management.

## Project Structure
- `External/`: Third-party dependencies (GLFW, GLAD, KHR).
- `root/`: Main source code directory.
- `random_model/`: Example GLTF model assets.
- `main.cpp`: Entry point for testing and demonstrations.

## Coding Standards & Conventions
- **C++ Version**: Modern C++ (C++17 or later recommended).
- **Memory Management**: Prefer `std::shared_ptr` and `std::unique_ptr` over raw pointers.
- **Abstractions**: Use abstract base classes (interfaces) for platform-dependent features (e.g., `RenderingDriver`, `Window`).
- **Namespaces**:
  - `wlw`: Root namespace.
  - `wlw::core`, `wlw::scene`, `wlw::rendering`, `wlw::os`, `wlw::utils`: Sub-namespaces for organization.
- **Naming**:
  - Classes: `PascalCase`.
  - Methods: `PascalCase` (e.g., `AddNode`, `SetPosition`).
  - Variables: `snake_case` (e.g., `last_window_id`, `nodes_3d_`).
  - Private Members: `snake_case_` with trailing underscore.

## Building and Running
- The project uses a Visual Studio solution (`WLWengine.vcxproj`).
- Depends on GLFW and GLAD.
- Asset paths (like `cube.jpg`, `pyramid.jpg`, `random_model/scene.gltf`) are relative to the working directory.

## Current Status
The engine supports:
- Loading and rendering 3D models (including GLTF).
- Scene graph hierarchy with parent-child relationships.
- Basic lighting (ambient, shininess) and texturing.
- FPS-style camera.
- Multiple window support.
