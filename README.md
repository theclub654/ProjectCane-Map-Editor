<p align="center">
  <img src="ProjectCane%20Map%20Editor/Resources/ProjectCane-logo.png" alt="ProjectCane Map Editor" width="620">
</p>

# ProjectCane Map Editor

A desktop editor for ProjectCane's native BRX maps, with a 3D viewport, object hierarchy, asset tools, and game-specific authoring controls. Available as a Windows application and a Linux CMake build.

The editor uses ProjectCane's reference source to guide native file layouts and object behavior. Its goal is to make map editing more accessible while preserving the representation expected by the game.

## Finding your way around

Start with **Help > Getting Started** for a step-by-step first edit. Use the top menus to open specialist tools.

### Creating shaped volumes

Use **Closed volumes - start with a box** in the object creation menu for damage regions, pits, activation regions, camera detection, water, collision gates, or a reusable VOL boundary. Existing sphere creation choices are still available.

Select the object and open **Volume shape - create or reshape** in its specialized controls. Draft a box or cylinder, or choose **Draw outline in viewport** and click gameplay collision around the boundary. Backspace removes a corner; Enter finishes the draft; Escape cancels drawing. Set the height and vertical center, inspect the yellow wireframe preview, then enable **Replace current boundary when applying** and click **Apply closed volume**. Drafting alone does not change the exported map. Clear the draft preview when finished.

Outlines must be convex, have 3-64 distinct corners, and are extruded along the object's local Z axis. Split concave areas into separate volumes. Use Transform to move, rotate, or scale the result. The shared controls also work with existing JLOVOL and decoded plane-volume records; they retain links and behavior settings. Replacing an SO boundary replaces gameplay collision and face mappings, removes its sphere override, and retains camera collision. Linked PROXY hierarchies must be made unique first.

Pit VOLZP regions require a closed collision volume and pit damage mode. The box pit creation choice configures both; for an existing VOLZP, apply the shape before choosing **Death surface**. ProjectCane tests for a falling player fully inside the pit. Configure recovery targets separately. A VOL boundary alone does not add gameplay behavior; it must be referenced by the appropriate object.

**AI / MCP:** optional local MCP integration exposes live records, all 148 CID schemas, source evidence, native operation previews, imports/exports and Undo/Redo. An opt-in image/input path operates existing specialist UI controls. See [setup](MCP.md) and [feature coverage](MCP-Coverage.md). Enable it under **Help > AI / MCP Connection** in a build containing the integration.

- **Scene** on the left lists the level's objects. Search by name, type, or object ID; right-click an entry for hierarchy actions.
- **Inspector** on the right shows the selection and whether you are editing an object, model, or model part. Use **Select owning object** to leave model selection. Advanced controls remain available.
- **Docking:** drag the Inspector title/tab to a docking target to place it on another side, tab it with a panel, or leave it floating inside the editor. Resize dock boundaries or floating window edges. ImGui saves the layout in `imgui.ini`; **View > Reset Layout** restores Scene left, Viewport center and Inspector right. Separate OS windows are not enabled.
- **Object setup** explains each CID's purpose, next step, cautions and source reference. Technical class names and IDs remain visible.
- **Find related objects & references** navigates parents, children, authored reference candidates and shared Splice indices. Candidates are not certified runtime bindings.
- **Tools for this object** opens relevant existing studios and Inspector pages. In object-specific settings, search includes inherited fields; **Include inherited settings** shows them without a search.
- **File > Save Editor Project** preserves your editing session. **File > Export rebuilt edited PCBRX** writes the playable level output. A saved editor project or exported GLB is not a playable level replacement.

Selecting an object focuses its hierarchy on the left; clear selection to return to the world tree. Friendly labels and guidance do not change native IDs, property tokens or file layouts. Existing custom object names are preserved.

Double-click an editable number in a drag field or horizontal slider to type an exact value, then press Enter. This includes emitter ranges, vector components, transforms and other CID settings; dragging and Ctrl-click entry remain available. For an unchecked **source default** or **game default** setting, enable its override checkbox first. Read-only fields stay locked, and existing validation and input-clamping rules still apply.

### PAL dialogue languages

For a multilingual PCBRX v18/v19 level, open **Dialog Manager > PAL dialogue languages (subtitles & voices)**, choose an available language, select a DIALOG/Talk event and edit **Subtitle text**. English, French, German, Spanish and Italian are supported when present in the input. Each language lists its own events; matching event numbers in different languages are not assumed to identify the same line. Undo/Redo tracks subtitle edits, and **Restore original translation** resets a non-default line.

Under **Voice clips (Preload audio events)**, select an event and use **Apply existing resource** to reassign its native 32-byte filename, or **Import replacement WAV...** to stage a new clip just for that language. A Preload event can cover several Talk lines; replacing audio does not generate timing or lipsync. Imports accept ProjectCane-compatible integer PCM WAVs, including extensible PCM. Floating-point/compressed WAV audio must be converted to integer PCM first.

Imported clips use unique language/content-based filenames, are included in new `.caneproject` saves, and participate in Undo/Redo. **Export rebuilt edited PCBRX** writes imported files beside the exported level under `English/Dialogs`, `French/Dialogs`, etc., matching ProjectCane's localized resolver (including legacy levels already inside a language folder). Deploy those WAV files along with the BRX. Original external voice files are not embedded automatically or overwritten. Identical existing imported files are reused; a differing file at the generated destination blocks export. Existing resource assignments require their external WAVs in the corresponding language's folders.

Voice previews check only the selected language's `Dialogs`/`Commentary` directories; they never fall back to another language. **Choose extracted voice folder...** accepts the level root, a language root, or that language's `Dialogs` folder. Imported clips can be previewed before export. Restoring an original reference leaves the imported bytes staged for project saving and Undo; it does not delete files on disk.

New `.caneproject` saves preserve every language world and its subtitle edits without requiring the source level. **Export rebuilt edited PCBRX** writes a ProjectCane-compatible v19 PUNI container with every original language and the original default selection. Untouched non-default language payloads are copied byte-for-byte. Edited language worlds use the existing structured writer and are reopened and checked for stable serialization before export. Native subtitle lengths remain limited to 65535 bytes; changing text does not translate voice recordings or generate font glyphs.

Translation-only edits also preserve the untouched default language's original payload when its authored-world comparison matches the input. Redundant translation edits equal to the original text do not force a language rebuild. This comparison uses a non-authored cache; inspecting or exporting a language does not itself dirty the editor project.

Project saves also preserve the default world's optional **PLOC** localized UI-string table. Rebuilds retain that footer for edited language worlds as well as the default world. Malformed footers are rejected using ProjectCane's size, count and string-boundary rules. Partially decoded language worlds remain preserved but cannot be edited/rebuilt through the subtitle tool.

The viewport and other world-editing tools continue to edit the default language's world. Geometry, dialogue timing and other world edits are not automatically propagated to the other language worlds. Projects saved by older editor versions did not retain the other languages: reopen the original multilingual PCBRX to use this feature. Hierarchy/portable-PROXY packages contain the selected world's objects, not complete extra language worlds or imported dialogue WAVs; deploy any required voice files separately for those packages.

Enable **Reference lens** in the viewport toolbar to inspect the existing decoded property-reference graph for the selected object's owner. Cyan arrows are outgoing; green arrows are incoming. Dashed amber lines identify type-mismatch candidates, and unresolved references remain in the lens list without invented endpoints. The panel filters by property/object name and controls direction, labels and the viewport line limit. Click a list row or **Alt-click** a viewport connection to inspect the other object. Connections are overlays through geometry. Curves anchor at their first point; non-spatial children may anchor at an ALO ancestor, while records without a spatial anchor remain list-only. This is read-only navigation, not a complete runtime graph: Splice lookups and other implicit dependencies are not inferred. Optional nil and engine-owned targets retain the existing resolver's exclusions.

### Finding parts and understanding ownership

- In the world Scene tree, expand **Object type summary** and check multiple CID types to filter them together. Text search narrows those results further. **Clear all filters** restores the full list. Selecting an object temporarily shows its focused hierarchy so filters cannot hide the selection.
- Right-click a Scene object and choose **Help for this object** to open its CID guide directly. The Inspector offers the same link under **Tools for this object**.
- Enable **Select collision** beside **Show collision** in the viewport toolbar, then click a face. The nearest enabled gameplay/camera collision surface is selected, outlined in yellow, and its owner is shown in the Inspector. Turn this mode off to resume model picking. The **Collision ownership** section distinguishes native collision ownership from visual-part selection.
- **Review dependencies and duplicate...**, available in the Scene context menu and Inspector, lists decoded outgoing property references before using the existing hierarchy duplicate operation. Internal references and shared external targets are distinguished. This review does not resolve every Splice lookup or certify a complete guard setup; test patrol, navigation and script behavior in game.
- An MS object's **MS components** section lists its individual GLOBs. Select a component to use the existing transform, instance duplication and model export controls. Isolation and visibility controls affect the editor preview. **Remove this MS visual...** removes exported visual geometry while preserving its native slot and shared collision, with Undo support. Removal is blocked for source parts used by other instances, collision mappings, linked PROXY ownership, or warp bindings. Removing visuals does not remove a platform's physical collision. This is component editing within the native MS, not an automatic conversion into independent gameplay objects.

## Features

- Browse and edit map objects through the viewport, hierarchy, and Inspector.
- Create, move, duplicate, delete, and replace supported object hierarchies.
- Edit GLOB/subGLOB geometry, materials, textures, and related properties.
- Export one GLOB from its hierarchy row: right-click the GLOB and open **Export GLOB...** for GLB/OBJ and placement options.
- You can also right-click in the viewport with an object or GLOB selected and choose **Export GLOB...**. An object with multiple GLOBs offers a list of its parts.
- **Delete GLOB...** is available alongside Copy and Export in hierarchy and viewport menus. Confirm to remove the GLOB and all its SUBGLOBs, with Undo support. The CID owner, collision geometry and shared materials/textures stay in place. Direct instance-source and collision-mapping dependencies must be resolved first; linked PROXY hierarchies must be made unique. Surviving native GLOBSET slot references are updated without changing other parts' placement.
- **Copy GLOB to object...**, available in GLOB hierarchy/viewport context menus and the Inspector, appends an independent static visual to another compatible CID object's GLOBSET while preserving authored world placement. Choose a destination, **Preview copy**, then **Apply copy**. Existing visuals remain, materials/textures stay shared, and collision/scripts are not copied. Undo restores the prior scene. Bone/pose tables, linked PROXY ownership, warps, GLEAM, animated materials, camera tracking and sheared destination frames are excluded; the preview explains incompatibilities. Render bounds may expand to include the new visual. No new CID object or object OID is created.
- Export one model without removing the rest of the map: right-click its thumbnail in **Asset Browser > Models**,
  or use **Export model...** in the Inspector's Model page. Choose **GLB** (embedded textures; rig when present)
  or **OBJ** (matching MTL and PNG files). Individual GLOB controls export all that GLOB's SUBGLOBs;
  model-group controls export the complete GLOBSET. Model-local coordinates are the default; world placement is optional.
  Exports contain visual assets, not gameplay, collision or animation clips. OBJ is a static bind-pose mesh.
  Keep its companion files together. For static reimport, set **Game scale** to **1**; set **Flip texture V**
  off for GLB or on for OBJ. OBJ retains the game's Z-up axes; GLB uses a Y-up scene root for external tools.
  Static GLB reimport does not recreate the exported rig or skin weights. Vertex colors are retained by both
  import paths. OBJ companion filenames include an identity suffix so similarly named exports do not share them.
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
