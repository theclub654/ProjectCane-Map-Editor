# MCP feature coverage

The companion exposes 35 MCP tools. `list_operations` currently discovers 59
native authoring operations, while `list_file_operations` discovers eight output
operations. The catalog is authoritative for operation names and arguments.

| Editor feature | Structured access | Existing UI access |
| --- | --- | --- |
| All 148 CIDs | Schemas, inherited properties, source-linked guidance, creation where the Add catalog permits it, typed scalar/vector property editing | Specialist controls through observed UI input |
| Hierarchy | Selection/multi-selection, rename, transform, reparent, duplicate, delete, hierarchy import/replace | Existing context menus and child picker |
| PROXY | Native source/instance/uniqueness/import/save operations; decoded roots/overrides | Advanced instance/override controls |
| Models and characters | Existing static/FBX character import, model replacement/alignment, mesh/bone/pose inspection | Asset browser and character studio |
| Collision | Native build/import/repair/normal flip; SO geometry/BSP inspection | Detailed physics, mappings and geometry controls |
| Textures/materials | RGBA8/BC1/BC3 import/replacement, resource duplication/deletion/remapping, native export, material/texture inspection | Material settings and animation controls |
| Animation | Detailed controllers, channels, keys, events and skin/pose inspection; native character import | Timeline/controller/key editing and playback |
| Splice | Source, callbacks, imports, users, reads/writes, source application and native authoring helpers | Remaining event/template/editor controls |
| SHAPE/PIPE/RAIL/HND | Native catalog creation, object transform path and parent-local SHAPE delta transform, complete control-point/tangent inspection | Curve point/interpolation/seam editing and helpers |
| PATHZONE | Native polygon/from-collision creation, vertex/edge helpers, BSP/edge rebuilding, detailed inspection | Remaining navigation editor controls |
| VISMAP | Native point/one-hop/sphere queries; shared single-zone and multi-object/GLOB membership operations; row/name edits; tree inspection | Split-plane/tree authoring, classification and suggestions |
| Particles/effects | Typed CID properties and detailed curves/mesh/colors inspection | Existing origin/color/curve editors and preview controls |
| Dialogue/audio | CID properties, complete decoded dialogue events, scan text and source evidence | Dialogue timelines, speakers and sound controls |
| State machines/reach maps/KEYHOLE/VOL | Decoded nested records and CID properties | Existing graph, topology and plane editors |
| Scene World | SW/CID 108 typed properties, existing header-preview conversion, decoded header/options | Remaining world/progression controls |
| Camera/viewport | Existing camera, multi-selection, collision/path/effect display settings, panel activation, image capture | Existing viewport widgets and menus |
| Projects and output | Preview/open BRX/project; native project/hierarchy/PROXY save; validated BRX, GLB and texture exports | Native UI save/export workflows |
| History | Existing Undo/Redo; copied native operations commit as one Undo entry | Normal UI history behavior |

“Existing UI access” means the model observes an editor capture and operates the
same ImGui controls a person uses, with a separate explicit permission. It does
not mean every field has an independently implemented typed RPC. This avoids a
parallel set of game rules drifting away from the real editor.

Limits: OS-native modal dialogs require manual interaction; structured output
requires new destinations; screenshot capture requires a restored/rendered window;
UI input requires focus and stops for local input. Unknown source sections remain
unknown. Existing editor bugs or unsupported retail classes are not made valid by
MCP. No shell, code execution or build command is exposed.

Implementation verification so far is source/companion verification. The new C++
operations and OpenGL/input path require approved builds and live Windows/Linux
testing before they can be described as runtime-verified or release-ready.

The follow-up audit corrected property-edit synchronization for Hidden, ALO
render distances and particle origin kinds; rejected incomplete camera requests
without moving the camera; shared output argument checks between preview and
write; and allowed native hierarchy/PROXY saving from resolved members. State
inspection now includes the dirty flag, project save path and Undo/Redo labels.
The offline suite has 28 tests (27 passed; one Windows symlink-permission skip).
Native C++ behavior remains source-reviewed rather than runtime-tested for these
changes. Specialist controls listed under UI access still require observed UI
interaction; they are not all available as structured property operations.
