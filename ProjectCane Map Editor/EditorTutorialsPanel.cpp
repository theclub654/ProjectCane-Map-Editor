#include "EditorTutorialsPanel.h"

#include <imgui.h>

#include <iterator>

namespace
{
constexpr const char *kTopics[] = {"Map Editor Basics",
                                   "Objects & Hierarchies",
                                   "Model Import",
                                   "Collision",
                                   "PROXY & Reusable Hierarchies",
                                   "VISMAP & World Structure",
                                   "Textures, Materials & Shaders",
                                   "Lighting & Shadows",
                                   "Particle Effects",
                                   "Paths & Gameplay Movement",
                                   "SHAPE Authoring from Objects",
                                   "Characters & Guards",
                                   "Animation & State Machines",
                                   "Gameplay Object Recipes",
                                   "Level Rebuilding & Export",
                                   "Troubleshooting"};

void H(const char *text)
{
    ImGui::SeparatorText(text);
}
void P(const char *text)
{
    ImGui::TextWrapped("%s", text);
    ImGui::Spacing();
}
void B(const char *title, const char *body)
{
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(.82f, .9f, 1.0f, 1.0f), "%s", title);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", body);
}
void S(const int number, const char *title, const char *body)
{
    ImGui::TextColored(ImVec4(.35f, .78f, 1.0f, 1.0f), "%d. %s", number, title);
    ImGui::Indent(22.0f);
    ImGui::TextWrapped("%s", body);
    ImGui::Unindent(22.0f);
    ImGui::Spacing();
}
void W(const char *text)
{
    ImGui::TextColored(ImVec4(1.0f, .68f, .25f, 1.0f), "Important");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text);
    ImGui::Spacing();
}
void Check(const char *text)
{
    ImGui::BulletText("[ ] %s", text);
}

void Basics()
{
    H("The editor project and the game level");
    P("A PCBRX is the game's serialized world. An editor project is the editable workspace representation, including editor-only names and authoring metadata. Open the source level once, save an editor project while working, then use Export rebuilt edited PCBRX when you want a game-readable level.");
    S(1, "Open a level",
      "Use File > Open PCBRX. Wait for decoding and payload validation to finish. Read the Level Validation summary before editing; pre-existing source warnings should not be confused with changes you create.");
    S(2, "Navigate",
      "Hold the viewport movement controls shown at the top of the viewport. Use left mouse to look/select, F to frame the selected object, and the hierarchy search to locate objects by friendly name, CID, OID, GLOB, or SUBGLOB.");
    S(3, "Transform safely",
      "W selects Move, E selects Rotate, and R selects Scale. Confirm whether you selected a world object, model placement, or child hierarchy before dragging. Enable snapping when exact placement matters.");
    S(4, "Use history",
      "Make one logical change at a time and use Undo/Redo when the result is wrong. Save the editor project at stable milestones instead of relying only on the in-memory history.");
    S(5, "Validate and export",
      "Open Level Validation, resolve errors, export to a new filename, reopen the exported PCBRX in the editor, then test it in ProjectCane/the game.");
    H("First-session checklist");
    Check("Frame a known object and confirm camera controls.");
    Check("Move one disposable object and undo it.");
    Check("Save and reload an editor project.");
    Check("Run validation without editing the source map.");
    Check("Perform a no-edit rebuild and reopen the exported level before serious authoring.");
    W("Never overwrite your only known-good retail level. Keep source, editor project, and exported test level as separate files.");
}

void Hierarchies()
{
    H("The object foundation");
    B("CID", "Class ID: determines runtime type, inherited fields, loader, properties, events, and behavior.");
    B("OID",
      "Object ID: runtime identity used by hierarchy lookup, properties, Splice, PROXY overrides, and object references.");
    B("BASIC / LO / ALO / SO",
      "BASIC supplies identity; LO adds world hierarchy; ALO adds transforms/render models; SO adds collision and rigid-body state. Specialized CIDs inherit these foundations.");
    B("Editor name",
      "A friendly authoring label. It is not the original game's runtime identity and references serialize back to OIDs.");
    H("Parenting workflow");
    S(1, "Inspect the focused hierarchy",
      "Select the object and switch from World to Focused hierarchy. Verify its parent, descendants, splice module, model groups, and PROXY participation.");
    S(2, "Choose the correct parent",
      "Transforms are usually stored relative to an ALO parent. Reparenting with preserve-world enabled recalculates local transform so the object stays in place.");
    S(3, "Duplicate intentionally",
      "A normal hierarchy duplicate creates independent objects. A linked PROXY instance shares a reusable source and applies instance overrides. Do not substitute one operation for the other.");
    S(4, "Delete with impact review",
      "Delete the hierarchy root when all descendants should go. Review inbound OID references, Splice calls, PROXY roots, paths, shaders, and gameplay links before confirming.");
    H("Identity rules");
    Check("Every required object has a valid CID and loader payload.");
    Check("OIDs are valid for their runtime lookup context.");
    Check("Duplicate OIDs are intentional and separated by PROXY/PXR context.");
    Check("Parent indices are valid and acyclic.");
    Check("World transforms match local transforms after reparenting.");
}

void Models()
{
    H("Supported authoring path");
    P("FBX imports binary or ASCII scene meshes with node transforms, material sections, UVs, normals, vertex colors, and external diffuse textures; animated/skinned FBX content is currently imported in its static authored pose. OBJ is useful for static geometry; GLB and glTF preserve modern mesh/material interchange; DAE supports scene transforms and static bind poses; PLY is useful for vertex-colored meshes and scans; STL is intended for plain geometry or collision. The importer converts external coordinates and supported materials into editor-owned render geometry, so BRX export does not retain a dependency on the source file.");
    S(1, "Prepare the asset",
      "Apply transforms in the modeling tool, triangulate, remove degenerate faces, provide normals and UVs, use reasonable material counts, and place the intended pivot at the local origin.");
    S(2, "Import as a new model object",
      "Use the Asset Browser import command when the model should become a new ALO. Use Replace selected model only when preserving the selected object's identity, hierarchy, references, and gameplay role.");
    S(3, "Inspect conversion",
      "Frame the new object. Check scale, handedness, up axis, pivot, bounds, normals, UV orientation, material slots, transparency, and texture dependencies before adding collision.");
    S(4, "Assign materials",
      "Import or select textures, create compatible material/shader stages, and map each imported material slot. White rendering usually means missing texture assignment, unresolved image path, or an unsupported material conversion.");
    S(5, "Choose gameplay behavior",
      "Leave the object as ALO for render-only scenery. Convert/create an SO only when gameplay collision or physical behavior is needed. Static collision does not require dynamic physics.");
    H("Common failures");
    B("Gizmo far away",
      "The source pivot or node transform is far from the mesh. Recenter the source asset or use the editor pivot/bounds repair workflow.");
    B("Disappears up close",
      "Bounds, visibility assignment, radius, or culling metadata do not contain the mesh. Recalculate bounds and review VISMAP placement.");
    B("Invisible after export",
      "Check model ownership, GLOB/SUBGLOB serialization, shader references, bounds, and whether the object is in a visible zone.");
    B("White model", "Inspect bitmap import, shader stages, UVs, and texture-state count validation.");
}

void Collision()
{
    H("Collision is separate from render geometry");
    P("A visible model does not automatically block the player. SO collision contains vertices, surfaces, edges, material/surface flags, normals, camera-group mapping, and BSP acceleration data. Dynamic physics is a separate choice from having collision.");
    S(1, "Decide static or dynamic",
      "For scenery that must never fall, use locked/static SO constraints and gameplay collision without dynamic gravity. Use dynamic physics only for objects intentionally simulated by the game.");
    S(2, "Generate or import",
      "In the Asset Browser, bounded generation creates a safe box from the selected render model. Import custom gameplay collision mesh replaces only the SO collision with a dedicated low-poly, closed convex hull. The importer merges triangulated coplanar faces into the polygonal surfaces ProjectCane expects, and the visible model is preserved. Split a concave design into multiple fixed SO hulls; open or non-manifold meshes are rejected rather than exported with unsafe BSP behavior.");
    S(3, "Terrain and environments",
      "Do not generate one bounding box or convex hull around a whole level: that places the playable space inside a giant solid. Author a separate simplified collision mesh for playable floors, walls, stairs, and barriers. Split concave environments into multiple closed convex SO collision pieces, then import each piece with Import custom gameplay collision mesh. Keep retail fixed-physics/world-space storage disabled unless the collision vertices themselves are already authored in final world coordinates.");
    S(3, "Repair topology",
      "Remove zero-area triangles, duplicate vertices, non-finite coordinates, open accidental seams, and inconsistent winding. Recalculate normals and inspect the collision overlay from both sides.");
    S(4, "Assign surfaces",
      "Paint the appropriate gameplay material and flags. Surface meaning affects traction, contacts, camera behavior, damage, and character interaction—not just visualization.");
    S(5, "Build acceleration data",
      "Rebuild edges, BSP nodes, and the one-entry-per-node camera-group map. Validation must pass before export.");
    S(6, "Test",
      "Approach from multiple directions, stand on edges, jump against walls, test camera movement, and confirm the object remains fixed after level start.");
    H("Collision diagnostics");
    B("Gameplay vs camera collision",
      "Gameplay GEOM/BSP drives physical contacts, standing, walls, triggers, and shadow-surface lookup. Camera GEOM/BSP is a separate solid used by CM clear-interval tests and camera-eye sphere squishing; it does not make the player stand on a surface. The gameplay BSP camera-fade group map is separate again: it selects render GLOBs to fade when a camera ray crosses gameplay collision.");
    B("Fixed/world-space storage",
      "Retail fFixedPhys means both gameplay and camera collision vertices are already in final world coordinates. It is not the same as making an object immovable. Use force/torque constraints and No gravity for a static local-space object; use the world-space toggle only when both collision payloads are intentionally converted together.");
    B("Falls through",
      "Collision is absent, not attached to the expected SO, has bad winding/normals, or is not included in the rebuilt BSP.");
    B("Object falls", "Rigid-body constraints or gravity are active. Make the gameplay object static/locked.");
    B("Screen/coordinates become NaN",
      "Reject non-finite transforms, bounds, vertices, normals, and zero-length normal calculations before export.");
}

void Proxy()
{
    H("What PROXY/PXR solves");
    P("PROXY provides reusable object hierarchies. A source record can be instantiated more than once, while each instance supplies transforms and property overrides. PXR boundaries also give duplicate OIDs a contextual meaning; flat global lookup is incorrect inside nested instances.");
    S(1, "Identify the reusable root",
      "Inspect the PROXY wrapper, source record/slot, instantiated roots, clone capacity, and descendants. Character packages often include model, skeleton, animations, AI, project volumes, effects, and Splice dependencies.");
    S(2, "Create linked instances",
      "Use the linked-instance workflow rather than ordinary duplicate. The new instance must refer to the same reusable source and receive a valid source slot/root mapping.");
    S(3, "Author overrides",
      "Target an object using contextual PXR lookup. If duplicate OIDs exist, verify the override browser resolves to the exact descendant in the selected instance.");
    S(4, "Portable hierarchy transfer",
      "Export the complete portable PROXY definition when moving a guard or reusable object between levels. Import dependencies, model/skeleton data, animation, Splice symbols, and referenced children—not just the visible root.");
    S(5, "Validate",
      "Check root ranges, source-record/slot agreement, clone capacity, nested PXR boundaries, override target types, OID context, and all external dependencies.");
    W("Ordinary hierarchy replacement is unsafe for PROXY, SW, and CM infrastructure. Use the specialized portable PROXY workflow.");
}

void Vismap()
{
    H("Purpose");
    P("VISMAP controls spatial visibility. Zones and BSP decisions determine which object/model batches are submitted. Incorrect assignment causes missing geometry, severe overdraw, or objects that appear only from certain distances or camera positions.");
    S(1, "Visualize zones",
      "Enable VISMAP planes/zones and inspect the camera's current zone. Compare the selected object's bounds and center against the zone volumes.");
    S(2, "Classify objects",
      "Use sphere/bounds-based classification as a suggestion, then review large objects crossing zone boundaries. Multi-zone scenery may need deliberate duplication/assignment strategy rather than center-only classification.");
    S(3, "Inspect BSP",
      "Visualize BSP planes and traversal. Geometry-driven suggestions should partition space without producing invalid child indices, unreachable nodes, or excessive tiny leaves.");
    S(4, "Assign in batches",
      "Select related models/objects, preview the proposed zone changes, apply, and validate. Avoid reclassifying gameplay infrastructure blindly.");
    S(5, "Test camera paths",
      "Fly through doorways, rooftops, tunnels, and zone seams. Watch the batch/cull counters and look for popping.");
    H("When an imported model disappears");
    Check("Bounds and radius enclose every transformed vertex.");
    Check("The object/model is assigned to the camera-reachable zone.");
    Check("Near/far clipping is not the cause.");
    Check("The BSP path can reach the assigned visibility node.");
    Check("PROXY instance transforms were included when classifying bounds.");
}

void Materials()
{
    H("Bitmap, material, shader stage");
    P("Bitmaps hold image payloads. Materials/shaders reference one or more texture stages and rendering state. Model batches reference shaders. Animated shader state can change UVs, color, frame, transparency, or stage selection at runtime.");
    S(1, "Import a texture",
      "Use Textures & Materials or the Asset Browser. Check dimensions, alpha, channel layout, format, mip expectations, and PS2-compatible limits.");
    S(2, "Create or replace a material",
      "Choose the intended shader, map texture stages, and preview the material on its model. Replacement preserves references while changing the resource payload.");
    S(3, "Configure transparency",
      "Select the correct blend/test behavior and inspect sorting. Transparent geometry should not be treated like an opaque batch.");
    S(4, "Edit animation",
      "Use friendly shader-animation controls, then verify the underlying stage records and counts. Preview timing, UV movement, frame sequence, and wrap behavior.");
    S(5, "Audit dependencies",
      "Use dependency inspection and duplicate detection before cleanup. Remove an unused bitmap or shader only when no model, stage, animation, emitter, or imported package references it.");
    H("Validation");
    Check("Every shader stage references a valid bitmap.");
    Check("Decoded and serialized texture-state counts agree.");
    Check("Indices and stage counts fit the target format.");
    Check("Alpha/transparency preview matches the intended game behavior.");
}

void Lights()
{
    H("Lighting layers");
    P("Scene World settings provide environment-wide lighting. LIGHT objects add localized color, intensity, attenuation, direction, and influence. Shadow rendering is a separate pass driven by light/model eligibility and shadow settings.");
    S(1, "Set the environment",
      "Open Scene World Lights and establish ambient/directional conditions before judging local lights. This prevents compensating for a bad world baseline with many extreme lights.");
    S(2, "Create a LIGHT",
      "Place it near the target, select the correct light type, set color/intensity, then adjust range and attenuation while watching its influence preview.");
    S(3, "Assign visibility",
      "Ensure the light and affected geometry are reachable in the appropriate zones. A correctly configured light cannot affect batches that are culled before lighting.");
    S(4, "Enable shadows deliberately",
      "Confirm the caster model supports the shadow path, the light participates, shadow geometry is valid, and bias/range avoids acne or detached shadows.");
    S(5, "Compare exports",
      "Use normal and baked-light map exports for their intended workflows. Verify in-game because editor approximations and PS2 rendering constraints can differ.");
    H("Performance guidance");
    B("Keep ranges tight", "Large overlapping lights increase work and make authoring harder.");
    B("Use shadows selectively",
      "Reserve dynamic-looking shadow work for objects where it materially improves the scene.");
}

void Particles()
{
    H("EMITTER versus RIP/model particles");
    P("EMITTER owns particle generation, timing, motion, lifetime, color/scale evolution, references, groups, and optional SHAPE-driven placement. RIP/model-particle objects provide renderable model-particle behavior. A complete effect can involve both plus shaders, textures, shapes, and proxy context.");
    S(1, "Choose the render payload",
      "Select a sprite/material or model-particle object. Validate every referenced OID from the emitter's contextual hierarchy.");
    S(2, "Create the EMITTER",
      "Set rate/burst behavior, lifetime, initial velocity, acceleration, spread, size, color/alpha, and limits. Start conservatively so a bad rate cannot flood the preview.");
    S(3, "Place particles",
      "Use the emitter transform for a point source or attach an HSHAPE/SHAPE for path/surface distribution. Confirm the shape's local space and parent transform.");
    S(4, "Configure groups",
      "Groups coordinate related particle operators and shared behavior. Preserve group membership and ordering when duplicating or importing effects.");
    S(5, "Preview selection",
      "Play only the selected emitter when isolating an effect. Deselecting must not silently switch to every world emitter.");
    S(6, "Validate and test",
      "Check missing render OIDs, incompatible CIDs, invalid ranges, negative lifetimes, excessive counts, shape references, and PROXY resolution.");
    H("Visibility failures");
    B("Effect appears elsewhere",
      "The OID resolved in a different PROXY/PXR context or the placement transform is wrong.");
    B("Shape visible but aura absent",
      "Inspect emitter-to-shape ownership, render payload, playback state, material, and contextual source instance.");
}

void Paths()
{
    H("Path object roles");
    B("SHAPE/HSHAPE", "Curve geometry and Hermite-style control data used by multiple systems.");
    B("PNT/HPNT", "Point and oriented/extended point records used as children, destinations, or path controls.");
    B("RAIL / PIPE", "Traversal-specific paths with runtime movement, attachment, and dismount semantics.");
    B("LANDING",
      "A FLY-enemy landing locus. It is registered globally; the nearest point is projected up to 500 units straight down onto physical collision.");
    B("PATHZONE", "Navigation/spatial path region used by AI and world movement systems.");
    B("JMT / SPIRE", "Gameplay traversal markers for jump movement and the unlocked ninja-spire interaction.");
    S(1, "Create in the viewport",
      "Choose the path type and parent, place points in order, then finish creation. Use snapping for surfaces and explicit insertion when refining a segment.");
    S(2, "Edit control data",
      "Move points, edit tangents/orientation, reverse direction, normalize parameters, and reparameterize by distance when speed should be spatially uniform.");
    S(3, "Inspect direction",
      "Enable arrows and point indices. Direction affects traversal, animation, entry/exit behavior, and event ordering.");
    S(4, "Assign consumers",
      "Connect guards, movement objects, rails, pipes, or scripts using verified OID/CID references. A visible curve alone has no gameplay consumer.");
    S(5, "Validate",
      "Reject empty/one-point paths where unsupported, invalid child types, duplicate/self links, non-finite tangents, impossible lengths, and broken parent transforms.");
}

void ShapeAuthoring()
{
    H("What building a SHAPE from objects means");
    P("The editor can sample the world-space origins of ordinary objects and bake those positions into a SHAPE-family curve. The source objects act like temporary layout markers: their visible meshes, bounds, and rotations are not copied. Only their resolved origins become control points. The resulting curve is normal BRX curve data and does not keep a live link to the marker objects.");
    B("SHAPE",
      "A general reusable curve. It is data, not a rendered tube or ribbon by itself; another object or system must consume it.");
    B("HSHAPE",
      "A SHAPE-family path with gameplay-specific ownership/attachment behavior. Use its existing object-specific controls rather than replacing it with a plain SHAPE.");
    B("RAIL / PIPE / LANDING",
      "Specialized SHAPE-family objects. Their curve can be rebuilt from object origins, but their CID and class payload must remain RAIL, PIPE, or LANDING so the game keeps the intended behavior.");
    W("This operation bakes a snapshot. Moving or deleting a source marker after generation does not move or delete the generated curve. Run the operation again when the layout changes.");

    H("Workflow A: create a new plain SHAPE from selected objects");
    S(1, "Prepare the parent",
      "Choose the ALO-derived object that should own the SHAPE. Its transform defines the curve's local coordinate system. Avoid zero scale or any singular parent transform.");
    S(2, "Place marker objects",
      "Move at least two objects so their pivots/origins lie on the desired path. The mesh can be anywhere around its pivot, so enable object origins/gizmos and judge the pivot—not the visible silhouette.");
    S(3, "Select the sources",
      "Ctrl-select the marker objects and make sure the intended ALO parent is the creation context shown by the editor. Multi-selection is a set; click order is not stored as path order.");
    S(4, "Run the creation command",
      "Open the Create Object SHAPE/path commands and choose Create linear SHAPE from selected object origins. The editor samples each source's resolved world origin, converts it through the inverse world transform of the new SHAPE's parent, and writes local control vertices.");
    S(5, "Inspect immediately",
      "Frame the new SHAPE, display point indices and direction arrows, then compare every point against the source origins. If the parent context was wrong, undo and recreate it under the intended owner instead of compensating with arbitrary offsets.");
    S(6, "Finish the curve",
      "Choose open or closed, select linear or cubic/Hermite interpolation, reverse direction if needed, then normalize or reparameterize by distance. Run validation before assigning a gameplay consumer.");

    H("Workflow B: rebuild an existing SHAPE-family object");
    S(1, "Select the target curve",
      "Select the SHAPE, HSHAPE, RAIL, PIPE, or LANDING that must keep its existing CID, OID, parent, references, and class payload.");
    S(2, "Add source objects",
      "Ctrl-select at least two marker/source objects in addition to the target. In Object-specific > Shape curve > Build curve from objects, choose an ordering mode and whether the result should be closed.");
    S(3, "Replace from selected objects",
      "Use Replace from selected objects to bake only the current source selection. The selected SHAPE itself is excluded from the sampled origins.");
    S(4, "Or use parent siblings",
      "Use Replace from parent siblings when the intended markers are the target's direct siblings. Review the sibling list first: every eligible sibling origin participates, so unrelated children can create stray points.");
    S(5, "Preserve specialization",
      "Confirm the hierarchy still reports the original specialized CID. Rebuilding RAIL geometry must not turn it into SHAPE; rebuilding PIPE geometry must not discard PIPE-specific options or references.");

    H("Point ordering modes");
    B("Hierarchy order",
      "Uses stable editor/document hierarchy order. Choose this when marker objects were deliberately arranged in the hierarchy from start to finish. It does not use Ctrl-click order.");
    B("Nearest neighbor",
      "Starts from the first stable marker and repeatedly chooses the nearest unused origin. It is useful for a chain of spatially adjacent points, but branches, crossings, and equally spaced rings can produce an unintended route.");
    B("Around center (XY)",
      "Computes the marker centroid and sorts by angle around it in the XY plane. Use it for rings, auras, tower perimeters, circular hazards, and closed outlines. Each point keeps its original Z; only ordering is based on XY angle.");
    P("If the generated line crosses itself, do not repair every point by hand first. Undo or regenerate with a more appropriate ordering mode, or organize the markers explicitly in hierarchy order.");

    H("Open, closed, linear, and cubic curves");
    B("Open curve",
      "The first and last points are independent endpoints. Use this for rails, pipes, patrol routes, ledges, jump approaches, and one-way effect paths.");
    B("Closed loop",
      "The editor creates a seam that returns from the last unique point to the first. Use this for rings and perimeter effects. A closed loop supports at most 254 unique source points because the serialized seam occupies the remaining control-point slot; an open curve supports 255.");
    B("Linear",
      "Segments travel exactly from origin to origin. Use it for hard corners, mechanical routes, diagnostic construction, and paths whose shape must match markers precisely.");
    B("Cubic/Hermite",
      "The editor generates initial tangents and distance parameters, producing a smoother path. Inspect every corner after conversion: automatic tangents are a starting point and can overshoot near sharp turns or uneven spacing.");
    S(1, "Repair the seam",
      "For a closed curve, use the closed-loop control and seam repair tools rather than manually creating mismatched duplicate endpoints.");
    S(2, "Normalize parameters", "Use Normalize parameters when the parameter range is malformed or inconsistent.");
    S(3, "Reparameterize by distance",
      "Use Reparameterize by distance when approximately uniform movement or emission along physical path length is desired.");
    S(4, "Refine points",
      "Insert, delete, or move control points; edit tangent handles on cubic curves; then preview direction and traversal again.");

    H("Practical recipes");
    B("Aura around a tower",
      "Place lightweight marker objects around the tower at the exact aura height, use Around center (XY), enable Closed loop, convert to cubic/Hermite, repair the seam, and reparameterize by distance. Then assign the finished shape to the intended emitter in the correct PROXY/PXR context.");
    B("Rail or ledge",
      "Create/select a RAIL, lay markers from entry to exit, use Hierarchy order, keep the curve open, and inspect direction arrows. Reverse it if entry and exit behavior are backwards.");
    B("Climbable pipe",
      "Create/select a PIPE, arrange origins from bottom to top, use Hierarchy order or Nearest neighbor, keep it open, and test both attachment and dismount behavior in game.");
    B("HSHAPE gameplay path",
      "Create/select the required HSHAPE under its correct owner, rebuild it from markers, then verify the owner's HSHAPE reference and any emitter/gameplay consumer. A visually correct curve with no valid consumer does nothing.");
    B("Fly landing locus",
      "Place the curve above surfaces where FLY enemies may land. Direction and endpoint order do not affect retail selection. Keep the locus at most 500 units above physical collision and avoid smoothing it below or laterally away from the intended surface.");

    H("Coordinate-space rules");
    P("Each marker origin is resolved in world space, then transformed into the SHAPE parent's local space. This lets the curve stay attached when its parent moves. It also means a wrong parent, stale instance transform, or zero-scale parent can make an apparently correct set of world markers generate an offset or invalid curve.");
    Check("The target parent is the intended ALO and has a finite, invertible world transform.");
    Check("Every marker origin is finite and visually positioned where the control point should be.");
    Check("For PROXY content, the target and references resolve inside the intended PXR instance.");
    Check(
        "After reparenting, preserve-world conversion keeps the curve in place and validation reports no singular transform.");

    H("Editing and troubleshooting");
    B("The command is disabled",
      "Select the target plus at least two usable source objects, or ensure the creation parent is ALO-derived. Linked PROXY data may need Make unique before it can be edited.");
    B("Points collapse together",
      "Two or more source objects have coincident origins even if their meshes look separate. Move their pivots/origins or remove duplicates.");
    B("Curve is offset or huge",
      "Inspect the parent transform, source pivots, imported model scale, and local/world conversion. Do not move the finished curve as a blind workaround for a bad parent.");
    B("Curve runs backward",
      "Use Reverse direction, then recheck any entry/exit events and consumers whose behavior depends on point order.");
    B("Loop twists or crosses",
      "Regenerate with Around center for a ring, explicitly arrange hierarchy order for a designed route, or reduce ambiguous/duplicate markers.");
    B("Curve exists but nothing appears",
      "SHAPE is normally non-rendered gameplay data. Enable its editor overlay and verify that an emitter, rail, pipe, landing system, or other compatible object actually references it.");
    B("Aura appears in the wrong instance",
      "Resolve the emitter-to-shape reference with PROXY/PXR context. Matching OID alone is insufficient when reusable hierarchies contain duplicate OIDs.");
    B("Export validation fails",
      "Repair non-finite points/tangents, parameter-count mismatches, bad closed seams, unsupported point counts, invalid parent transforms, and incompatible references before export.");

    H("Safe cleanup and export");
    P("Once the curve is correct, the source markers are no longer required by the generated SHAPE. Delete them only if the Reference Browser confirms they have no independent gameplay, Splice, hierarchy, PROXY, or asset purpose. The rebuilt BRX serializes the SHAPE control data; it does not serialize an editor-only dependency on those marker objects.");
    Check("Preview point indices, direction, and closed seam.");
    Check("Verify the specialized CID and all consumer references.");
    Check(
        "Run path and level validation, export to a new PCBRX, reopen it, and test the full gameplay/effect behavior.");
}

void Characters()
{
    H("A character is a dependency package");
    P("A guard is not just a mesh. It can require a PROXY source, skeleton/bones, skin weights, GLOBSET data, ASEG animations, state machines, AI objects, patrol/path references, project volumes, effects, materials, sounds, and Splice symbols.");
    S(1, "Inspect before import",
      "Use the dedicated character/proxy dependency view. Record every internal and external OID, source slot, animation, skeleton binding, model, shader, path, and Splice dependency.");
    S(2, "Import the portable package",
      "Bring the reusable PROXY definition and dependencies into the destination level. Remap collisions only where identities genuinely conflict; preserve internal relationships.");
    S(3, "Verify the skeleton",
      "Check bone count/order, parent hierarchy, bind matrices, joint references, skin indices, and normalized weights. Invalid joint pointers can crash UpdateAloInfluences.");
    S(4, "Connect animation and AI",
      "Validate ASEG/state-machine OIDs, goals, transitions, patrol paths, detection/project volumes, effects, and gameplay references.");
    S(5, "Place and test",
      "Instantiate the source, place its root, ensure visibility and collision, then test idle, movement, detection, combat, damage, death, and despawn behavior.");
    W("Seeing the guard model is not proof of a valid character import. Exercise animation and AI paths that touch every dependency.");
}

void Animation()
{
    H("ASEG and state machines");
    P("ASEG records describe animation segments and events. State machines choose goals/states and connect transitions. Splice callbacks and serialized animation events can trigger gameplay at precise frames.");
    S(1, "Preview the segment",
      "Select the owning object and ASEG. Inspect duration, playback range/rate, looping, root motion, bound model/skeleton, and all animation events.");
    S(2, "Edit connections",
      "Use the state-machine view to inspect state OIDs, goal transitions, conditions, and referenced ASEGs. Preserve expected OIDs when retail logic refers to fixed identities.");
    S(3, "Author events",
      "Place sound, effect, dialog, or Splice events at intentional times. For Splice animation-frame event 10, confirm the target module actually defines the callback.");
    S(4, "Import animation",
      "Match skeleton topology or retarget explicitly. Validate bone mapping, bind pose, key ranges, interpolation, and skin influence compatibility.");
    S(5, "Test transitions",
      "Preview every transition in both directions, interruption points, loop boundaries, and fallback states. In-game test AI-driven transitions that the isolated preview cannot reproduce.");
}

void Recipes()
{
    H("Guided creation pattern");
    P("Each recipe should be approached as: create the correct CID, satisfy its required hierarchy, assign render/collision data, configure properties, connect references, attach events/Splice if needed, validate, then test its complete lifecycle.");
    B("Door + button",
      "Create the physical door/SO and animation or motion controller, create BUTTON with collision/filter data, connect Trigger logic, then test repeated open/close and save-state behavior.");
    B("Collectible",
      "Create the correct collectible CID/model, configure pickup interaction and effects, place it in a visible zone, and test collection/state persistence.");
    B("Breakable",
      "Create BRK/BREAK/FRAGILE hierarchy, intact and fragment payloads, collision, damage filters, effects, sounds, and death/break events.");
    B("Hazard",
      "Create trigger/collision volume, allowed target filters, damage/explosion/missile behavior, warning effects, and reset rules.");
    B("Checkpoint",
      "Create CHKPNT with required child volumes, animations, WARP, and state machine; validate contextual warp resolution and respawn behavior.");
    B("WARP",
      "Place WARP transform, velocity/camera options, alias/default behavior, and trigger it with a verified action such as EOPID 162.");
    B("Light / emitter",
      "Create the object, assign spatial/visibility context, configure payload and preview, then validate referenced shaders, textures, shapes, and objects.");
    B("Rail / pipe / spire",
      "Create path/marker geometry, direction and parent relationships, assign gameplay consumers, and test entry, traversal, exit, and failure recovery.");
    H("Use the specialist tools");
    P("The Create Object catalog, World Builder recipes, Object Operations Reference, Splice Tutorial, path tools, managers, and validation panel should be used together. A recipe is a guided start, not permission to skip CID-specific validation.");
}

void Export()
{
    H("From-scratch rebuild pipeline");
    P("The rebuilt PCBRX is authored from the editor document: world header/options, object hierarchy and CID payloads, properties, PROXY/PXR data, Splice modules/symbols, render geometry, bitmaps, shaders, animation, collision surfaces/edges/BSP, VISMAP, and other decoded sections. Export must not silently depend on unavailable source bytes.");
    S(1, "Freeze a test scope",
      "Save the editor project and choose a small intentional edit. Keep the original and last known-good export.");
    S(2, "Run validation",
      "Resolve writer blockers first: unsupported payloads, illegal options, invalid references, non-finite transforms/bounds, broken GLOB instances, collision/BSP mismatches, shader-state mismatches, and Splice errors.");
    S(3, "Export to a new file",
      "Choose an explicit destination and verify the success message names the final path. The editor should reopen the exported file for structural validation.");
    S(4, "Round-trip",
      "Open the exported PCBRX as a new document. Compare object/root counts, models, geometry counts, textures, shaders, collision, visibility, Splice modules, and validation report against the editor project.");
    S(5, "Runtime test",
      "Load through ProjectCane/the game. Test spawn, camera, collision, rendering, interactions, scripts, checkpoints, transitions, and level unload/reload.");
    S(6, "Expand gradually",
      "After a no-edit rebuild works, test transform, property edit, deletion, new object, imported model, collision, script, proxy package, and resource replacement separately.");
    H("Export acceptance checklist");
    Check("No writer blockers or non-finite data.");
    Check("Export exists at the selected path and reopens.");
    Check("Object/root and critical resource counts are expected.");
    Check("No unexpected loss of CID payloads or hierarchy.");
    Check("ProjectCane loads, plays, and unloads the edited level without invalid pointers.");
}

void Troubleshooting()
{
    H("Use evidence, not guesses");
    S(1, "Reproduce minimally",
      "Identify the smallest edit that causes the failure. Reopen the last good editor project and repeat only that operation.");
    S(2, "Classify the stage",
      "Determine whether the failure occurs during editor rendering, validation, export, exported-file reopen, ProjectCane load, post-load, first update, rendering, collision, or gameplay interaction.");
    S(3, "Compare structure",
      "Use validation reports and reference browsers to compare counts, hierarchy, payload presence, options, models, resources, Splice, collision, and VISMAP.");
    S(4, "Trace the original path",
      "Follow the relevant ProjectCane loader, post-load resolver, setter/thunk, and runtime consumer. A serializer can look plausible while violating a cached pointer or post-load invariant.");
    S(5, "Fix the editor contract",
      "Correct decoding, authoring validation, serializer output, and diagnostics together so the same invalid state cannot be recreated silently.");
    H("Symptom index");
    B("Only a few objects decode",
      "Inspect object dispatcher termination, record sizes/offsets, option payload alignment, child/root counts, and CID-specific writer boundaries.");
    B("Model invisible",
      "Check hierarchy ownership, transform, bounds, GLOB/SUBGLOB, shader/texture, VISMAP, near/far culling, and PROXY context.");
    B("Model falls or has no collision",
      "Separate render ALO from SO collision; inspect constraints, gravity, surfaces, normals, BSP, and camera-group mapping.");
    B("Runtime null/invalid pointer",
      "Trace the post-load resolver for the object shown in the call stack. Validate referenced OIDs/CIDs and serialized child hierarchy before adding defensive runtime workarounds.");
    B("Splice does nothing",
      "Confirm event dispatch, module attachment, direct/imported definition, OID context, EOPID legality, Apply-to-graph state, and native-action sandbox limitations.");
    B("Editor slow",
      "Measure viewport submission, hierarchy traversal, validation frequency, resource uploads, menu-time recomputation, particle previews, collision overlays, and repeated full-document scans.");
    W("Do not patch ProjectCane to accept malformed editor output when the retail contract is known. Fix the map editor's data and writer unless the original runtime itself is demonstrably wrong.");
}
}

void DrawEditorTutorialsPanel(bool &open, int &selectedTopic, bool &openSpliceTutorial, bool &openObjectOperations)
{
    if (!open)
        return;
    if (selectedTopic < 0 || selectedTopic >= static_cast<int>(std::size(kTopics)))
        selectedTopic = 0;
    ImGui::SetNextWindowSize(ImVec2(1120.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Editor Tutorials", &open))
    {
        ImGui::End();
        return;
    }
    ImGui::TextColored(ImVec4(.35f, 1.0f, .5f, 1.0f), "ProjectCane Map Editor handbook");
    ImGui::SameLine();
    if (ImGui::Button("Splice Tutorial"))
        openSpliceTutorial = true;
    ImGui::SameLine();
    if (ImGui::Button("Object Operations"))
        openObjectOperations = true;
    ImGui::Separator();
    ImGui::BeginChild("TutorialTopics", ImVec2(260.0f, -1.0f), true);
    for (int topic = 0; topic < static_cast<int>(std::size(kTopics)); ++topic)
        if (ImGui::Selectable(kTopics[topic], selectedTopic == topic))
            selectedTopic = topic;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("TutorialContent", ImVec2(0.0f, -1.0f), true);
    ImGui::TextColored(ImVec4(.5f, .82f, 1.0f, 1.0f), "%s", kTopics[selectedTopic]);
    switch (selectedTopic)
    {
    case 0:
        Basics();
        break;
    case 1:
        Hierarchies();
        break;
    case 2:
        Models();
        break;
    case 3:
        Collision();
        break;
    case 4:
        Proxy();
        break;
    case 5:
        Vismap();
        break;
    case 6:
        Materials();
        break;
    case 7:
        Lights();
        break;
    case 8:
        Particles();
        break;
    case 9:
        Paths();
        break;
    case 10:
        ShapeAuthoring();
        break;
    case 11:
        Characters();
        break;
    case 12:
        Animation();
        break;
    case 13:
        Recipes();
        break;
    case 14:
        Export();
        break;
    default:
        Troubleshooting();
        break;
    }
    ImGui::EndChild();
    ImGui::End();
}
