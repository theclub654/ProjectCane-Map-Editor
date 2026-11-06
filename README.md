<p align="center">
  <img src="ProjectCane%20Map%20Editor/Resources/ProjectCane-logo.png" alt="ProjectCane Map Editor" width="620">
</p>

# ProjectCane Map Editor

A desktop editor for ProjectCane's native BRX maps, with a 3D viewport, object hierarchy, asset tools, and game-specific authoring controls. Available as a Windows application and a Linux CMake build.

The editor uses ProjectCane's reference source to guide native file layouts and object behavior. Its goal is to make map editing more accessible while preserving the representation expected by the game.

## Finding your way around

Start with **Help > Getting Started** for a step-by-step first edit. Use the top menus to open specialist tools.

**AI / MCP:** optional local MCP integration exposes live records, all 148 CID schemas, source evidence, native operation previews, imports/exports and Undo/Redo. An opt-in image/input path operates existing specialist UI controls. See [setup](MCP.md) and [feature coverage](MCP-Coverage.md). Enable it under **Help > AI / MCP Connection** in a build containing the integration.

- **Scene** on the left lists the level's objects. Search by name, type, or object ID; right-click an entry for hierarchy actions.
- **Inspector** on the right shows the selection and whether you are editing an object, model, or model part. Use **Select owning object** to leave model selection. Advanced controls remain available.
- **Docking:** drag the Inspector title/tab to a docking target to place it on another side, tab it with a panel, or leave it floating inside the editor. Resize dock boundaries or floating window edges. ImGui saves the layout in `imgui.ini`; **View > Reset Layout** restores Scene left, Viewport center and Inspector right. Separate OS windows are not enabled.
- **Object setup** explains each CID's purpose, next step, cautions and source reference. Technical class names and IDs remain visible.
- **Find related objects & references** navigates parents, children, authored reference candidates and shared Splice indices. Candidates are not certified runtime bindings.
- **Tools for this object** opens relevant existing studios and Inspector pages. In object-specific settings, search includes inherited fields; **Include inherited settings** shows them without a search.
- **File > Save Editor Project** preserves your editing session. **File > Export rebuilt edited PCBRX** writes the playable level output. A saved editor project or exported GLB is not a playable level replacement.

Selecting an object focuses its hierarchy on the left; clear selection to return to the world tree. Friendly labels and guidance do not change native IDs, property tokens or file layouts. Existing custom object names are preserved.

## Features

- Browse and edit map objects through the viewport, hierarchy, and Inspector.
- Create, move, duplicate, delete, and replace supported object hierarchies.
- Edit GLOB/subGLOB geometry, materials, textures, and related properties.
- Import models, including FBX meshes, skeletons, skin weights, and animation through the supported import paths.
- Import standalone textures as RGBA8, BC1, or BC3. BC1 is treated as opaque by the game; use BC3 or RGBA8 for transparency.
- Author visibility zones, inspect BSP structures, and test visibility through the Visibility & Zones tools.
- Work with collision, cameras, paths, lights, shadows, and supported CID-specific properties.
- Edit Splice scripts/events, DIALOG timelines, and SPEAKER presentation profiles.
- Preview supported animation and audio behavior.
- Save editor projects, export native BRX data, and export supported geometry/characters as GLB.
- Review validation diagnostics for unsupported structures, missing references, and other authoring problems.

Support varies by object class and asset. A successful import or editor preview does not guarantee identical in-game behavior.

## Requirements

- A 64-bit Windows or Linux system is recommended; the Linux build requires 64-bit little-endian architecture.
- A graphics driver supporting desktop OpenGL 4.3.
- Your own supported map files and assets. Game content is not included.

Windows x64 and Ubuntu/WSL builds have been produced. Linux desktop integration, audio devices, graphics drivers, and individual game features still need testing in their target environments.

## Run a packaged build

### Windows

Extract the complete Windows package and run **ProjectCane Map Editor.exe**. Keep any supplied runtime DLLs beside the executable. Visual Studio is not needed to run the packaged application.

### Linux

Extract the Linux package, open a terminal in its folder, and run:

```bash
chmod +x ./projectcane-map-editor
./projectcane-map-editor
```

Read the package's runtime requirements. The current packaged Ubuntu/WSL executable requires **GLIBC 2.43** and **GLIBCXX 3.4.31**; it is not a universal binary for older Linux distributions. Building from source uses your local toolchain and libraries.

To inspect missing shared libraries:

```bash
ldd ./projectcane-map-editor
```

## Build from source

### Windows — Visual Studio

1. Install Visual Studio 2022 with **Desktop development with C++**, the **v143** toolset, and a Windows SDK.
2. Keep the bundled `Libraries` directory intact, including its GLFW libraries.
3. Supply the additional headers described below.
4. Open `ProjectCane Map Editor.sln`.
5. Select **Release** and **x64**, then build the solution.

The executable is written to:

```text
x64/Release/ProjectCane Map Editor.exe
```

**Additional headers:** the existing Visual Studio project searches the sibling directory `../ProjectCane/Libraries/include` for dependencies such as `nlohmann/json.hpp`, `stb/stb_dxt.h`, and `stb/stb_image_write.h`. If that reference checkout is unavailable, provide those dependencies under `Libraries/include` with the same directory structure, or update the project's additional include directories. The editor does not automatically download them.

The CMake project also defines a Windows build, but requires installed GLFW, GLM, nlohmann/json, and stb development dependencies. The Visual Studio solution is the existing Windows build path.

### Linux — CMake

Requirements: CMake 3.20+, a C++20 compiler, a C17 compiler, pkg-config, GLFW 3.3+, GLM, nlohmann/json, stb, GTK 3.22+, SDL 2.0.7+, and OpenGL development libraries.

For Ubuntu/Debian, install the development dependencies:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libglfw3-dev libglm-dev \
  nlohmann-json3-dev libstb-dev libgtk-3-dev libsdl2-dev libgl1-mesa-dev
```

From the repository root:

```bash
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel 2
./build/linux/projectcane-map-editor
```

`EditorApplication.cpp` is a large translation unit. Modest build parallelism helps limit memory usage. CMake uses the bundled ImGui, GLAD, and ufbx sources and does not automatically fetch dependencies.

From Windows PowerShell, enter your WSL distribution first:

```powershell
wsl -d Ubuntu
```

Then run the Linux commands inside that shell. Launching the GUI under WSL requires WSLg or another working graphical environment.

## Basic workflow

1. Open a supported BRX map or an editor project.
2. Select objects through the hierarchy or viewport and inspect their properties.
3. Make edits, import assets, or use the relevant object tools.
4. Review validation diagnostics before export.
5. Save an editor project to retain your editable work, then export a BRX for native testing.

Keep backups of original maps and projects. Test exported maps in the intended game/runtime before replacing a working map.

## Important behavior and limitations

- **PROXY instances:** linked objects inherit source data. Use the supported override controls or make a hierarchy unique before editing data that cannot be stored as an instance override. Some specialized editors deliberately disable such edits.
- **Shared Splice modules:** editing a shared module can affect multiple objects and import users. Deleting an object does not automatically remove its script module; native loading initializes serialized modules even when no object directly references them.
- **Previews:** Splice, dialogue, animation, and other previews cover supported behavior. They do not reproduce every gameplay side effect, closure, event filter, scheduling rule, or timing condition.
- **Validation:** diagnostics help identify concrete problems but do not prove that an entire map is release-ready. Some runtime-created or contextual references cannot be fully resolved statically.
- **External assets:** move referenced assets with your project and check path capitalization on Linux. Windows drive paths may need to be reselected.
- **Project portability:** project snapshots contain raw POD records. Cross-compiler and 32-bit/64-bit project compatibility is not guaranteed. Preserve original copies when moving projects between platforms.
- **Generated schemas:** keep the committed `*.generated.inc` files. Normal builds consume them directly; the PowerShell generation tools are for maintaining them against the reference source.

## Repository layout

```text
ProjectCane Map Editor/   Editor source, generated schemas, and platform implementations
  Resources/             Application icon and logo
Libraries/               Bundled third-party headers, sources, and Windows GLFW libraries
CMakeLists.txt           CMake build definition
ProjectCane Map Editor.sln
Generate*.ps1            Reference-schema maintenance tools
```

Build directories and personal IDE/UI settings do not belong in source commits. Preserve `Libraries/lib/Debug` and `Libraries/lib/Release`; these are bundled dependencies, not ordinary editor build output.

## Reporting a problem

Include your operating system, build configuration, reproduction steps, and the first relevant error message. For map or import issues, include the affected CID/object type or asset format and describe whether the problem occurs during import, editing, preview, saving, or export. Share a minimal reproducing file when possible.

## Dependencies and acknowledgements

The editor uses GLFW, OpenGL/GLAD, Dear ImGui, GLM, nlohmann/json, stb, and ufbx. Linux platform services additionally use GTK/GdkPixbuf and SDL2. Third-party components retain their respective licenses.

ProjectCane's reference source is the authority for native map layouts and game-specific behavior. This repository is an editor/tool project, not a distribution of game assets.
