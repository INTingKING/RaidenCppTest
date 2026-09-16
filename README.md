# RaidenCppTest

Cross-platform C++ Vulkan renderer (Windows, macOS, Linux) built with CMake.

The app opens a window, loads shaders / a mesh / a texture from `Assets/`, and draws a rotating cube.

## Layout

```
Assets/
  models/cube.obj
  shaders/mesh.vert
  shaders/mesh.frag
  textures/panel.png
cmake/                 CMake helpers (Vulkan SDK, deps, shader compile)
src/                   C++ sources
third_party/stb/       stb_image
CMakeLists.txt
```

Compiled SPIR-V and a copy of `Assets/` are placed next to the `Raiden` binary at build time.

## Prerequisites

### All platforms

- CMake 3.21+
- A C++20 compiler (MSVC 2022, Apple Clang, GCC 11+, or Clang 14+)
- [LunarG Vulkan SDK](https://vulkan.lunarg.com/) (loader, headers, `glslc` / `glslangValidator`)

GLFW and GLM are found on the system if present, otherwise CMake fetches them.

### Windows

1. Install Visual Studio 2022 with the “Desktop development with C++” workload.
2. Install the Vulkan SDK. The installer adds `VULKAN_SDK` and puts `vulkan-1.dll` on `PATH`.

### macOS

1. Xcode Command Line Tools: `xcode-select --install`
2. LunarG Vulkan SDK (includes MoltenVK)
3. Optional: `brew install cmake glfw`

CMake will look in `~/VulkanSDK/<version>/macOS` if `VULKAN_SDK` is not set.

### Linux

Ubuntu / Debian:

```bash
sudo apt install build-essential cmake libvulkan-dev glslc \
    libx11-dev libxcursor-dev libxi-dev libxinerama-dev libxrandr-dev libwayland-dev pkg-config
```

Fedora:

```bash
sudo dnf install cmake gcc-c++ vulkan-loader-devel glslc \
    libX11-devel libXcursor-devel libXi-devel libXinerama-devel libXrandr-devel wayland-devel
```

A distro Vulkan ICD (NVIDIA / AMD / Intel / MoltenVK) must be installed so a GPU is visible to `vulkaninfo`.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Visual Studio (Windows):

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Ninja:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run

```bash
./build/bin/Raiden
```

Windows: `build\bin\Raiden.exe`

The working directory does not matter; the app locates `Assets/` next to the executable. Override with `RAIDEN_ASSETS` if you keep assets elsewhere.

## CMake options

| Option | Default | Meaning |
| --- | --- | --- |
| `RAIDEN_ENABLE_VALIDATION` | `ON` | Enable `VK_LAYER_KHRONOS_validation` when the layer is present |

```bash
cmake -B build -DRAIDEN_ENABLE_VALIDATION=OFF
```

## Renderer

Vulkan 1.1 instance, swapchain, depth buffer, SPIR-V mesh pipeline, host-visible uniform buffer, and a sampled texture. macOS uses MoltenVK via `VK_KHR_portability_enumeration` / `VK_KHR_portability_subset`.
