#include "CreateObjectCatalogPanel.h"

#include "BrxDocument.h"
#include "EditorDocumentOperations.h"
#include "EditorWorkspace.h"
#include "ObjectTypeRegistry.h"
#include "SceneRenderer.h"

#include <imgui.h>

#include <array>
#include <string>
#include <vector>

namespace
{
const char *CategoryForCid(const std::int16_t cid)
{
    if (cid <= 6 || cid == 43 || cid == 79 || cid == 83 || cid == 115)
        return "Foundations & hierarchy";
    if ((cid >= 7 && cid <= 29) || (cid >= 64 && cid <= 76) || cid == 95 || cid == 104 || cid == 105)
        return "Characters, enemies & encounters";
    if ((cid >= 30 && cid <= 39) || (cid >= 77 && cid <= 82) || cid == 118 || (cid >= 119 && cid <= 122) ||
        cid == 135 || cid == 141 || cid == 142 || cid == 145 || cid == 146)
        return "Effects, rendering & destruction";
    if ((cid >= 40 && cid <= 63) || (cid >= 84 && cid <= 94) || cid == 116 || cid == 117 ||
        (cid >= 123 && cid <= 130) || cid == 139 || cid == 140 || cid == 144)
        return "Gameplay, interaction & progression";
    if ((cid >= 96 && cid <= 103) || (cid >= 131 && cid <= 134) || cid == 143 || cid == 147)
        return "Logic, dialog & animation";
    if (cid == 106 || cid == 107 || cid == 110 || cid == 111 || cid == 112 || cid == 113 || cid == 114 ||
        (cid >= 136 && cid <= 138))
        return "World, paths & visibility";
    return "Runtime systems";
}

const char *FriendlyName(const std::int16_t cid)
{
    switch (cid)
    {
    case 0:
        return "Runtime Object Foundation";
    case 1:
        return "Level Object";
    case 2:
        return "Renderable Object";
    case 3:
        return "Physical Object";
    case 4:
        return "Model Set";
    case 5:
        return "Physical Gameplay Object";
    case 6:
        return "Step Platform";
    case 7:
        return "Sly Player Rig";
    case 8:
        return "Patrol Guard";
    case 9:
        return "Smart Guard";
    case 10:
        return "Gomer Guard";
    case 23:
        return "Vault";
    case 30:
        return "Runtime Particle Group";
    case 31:
        return "Water Surface";
    case 32:
        return "Breakable Base";
    case 33:
        return "Standard Breakable";
    case 37:
        return "Fragile Object";
    case 40:
        return "Physical Switch";
    case 41:
        return "Volume Button";
    case 46:
        return "Clue Bottle";
    case 47:
        return "Alarm Controller";
    case 48:
        return "Detector Base";
    case 49:
        return "Laser Detector";
    case 50:
        return "Camera Detector";
    case 52:
        return "Barrier";
    case 56:
        return "Checkpoint";
    case 57:
        return "Conveyor";
    case 59:
        return "Bomb";
    case 60:
        return "Missile";
    case 61:
        return "Accelerating Missile";
    case 62:
        return "Targeted Missile";
    case 63:
        return "Mine";
    case 76:
        return "Decoy";
    case 77:
        return "Particle Group";
    case 78:
        return "Camera";
    case 80:
        return "Particle Emitter";
    case 81:
        return "Light";
    case 83:
        return "Reusable Object Instance";
    case 84:
        return "Sky";
    case 85:
        return "Dynamic Prize Base";
    case 86:
        return "Charm Award";
    case 87:
        return "Life Token";
    case 88:
        return "Coin";
    case 89:
        return "Key";
    case 90:
        return "Timed-Challenge Gold";
    case 91:
        return "Vehicle Boost";
    case 92:
        return "Lock";
    case 97:
        return "Dynamic Shadow Helper";
    case 98:
        return "Waypoint";
    case 100:
        return "Dialog Localization Helper";
    case 101:
        return "Dialog Sequence";
    case 102:
        return "Dialog Speaker";
    case 108:
        return "Scene World";
    case 109:
        return "Camera Manager";
    case 110:
        return "General Path";
    case 111:
        return "Hiding Path";
    case 112:
        return "Climbable Pipe";
    case 113:
        return "Traversal Rail";
    case 114:
        return "Enemy Landing Path";
    case 115:
        return "Transform Marker";
    case 116:
        return "Warp Destination";
    case 117:
        return "Target Marker";
    case 118:
        return "Grab Handhold";
    case 119:
        return "Explosion Marker Base";
    case 120:
        return "Explosion Group";
    case 121:
        return "Explosion Emitter Base";
    case 122:
        return "Particle Explosion";
    case 123:
        return "Trigger Volume";
    case 125:
        return "Level Exit";
    case 126:
        return "Point Marker";
    case 127:
        return "Hide Point";
    case 128:
        return "Jump Marker";
    case 129:
        return "Ninja Spire Point";
    case 130:
        return "Binocular Scan Label";
    case 131:
        return "Animation Segment";
    case 132:
        return "Runtime Animation Blend";
    case 133:
        return "Visibility Zone";
    case 134:
        return "Visibility Map";
    case 136:
        return "State Machine";
    case 137:
        return "Guard Squad Group";
    case 138:
        return "Navigation Zone";
    case 139:
        return "Reach Pose Map";
    case 140:
        return "Reusable Weapon Manager";
    case 141:
        return "Geometry Warp Renderer";
    case 142:
        return "Keyhole";
    case 143:
        return "Player Choreography";
    case 144:
        return "Rhythm Sequence";
    case 145:
        return "Screen Flash";
    case 147:
        return "Animation State Machine";
    default:
        return nullptr;
    }
}

bool IsCatalogCid(const std::int16_t cid)
{
    // UNK11 is ProjectCane's anti-cheat/runtime integrity class. It is not level-authoring
    // content and must never be offered as a creatable map object.
    // BLIPG is likewise allocated and configured exclusively by the runtime EMITTER pool.
    return cid != 77 && cid != 107;
}

bool IsGuideCid(const std::int16_t cid)
{
    // UNK11 is intentionally omitted: it is runtime integrity infrastructure rather
    // than level-authoring content. Runtime-only classes such as BLIPG remain in the
    // guide because understanding why they are not directly authored is useful.
    return cid != 107;
}

const char *ObjectSummary(const std::int16_t cid)
{
    switch (cid)
    {
    case 0:
        return "Abstract root of the runtime object system. It supplies class identity and property dispatch, but it is not a standalone level object and has no BRX creation contract.";
    case 1:
        return "Base world-hierarchy object. It adds OID identity, parenting, world membership, events, and Splice attachment, but has no transform or visible model of its own.";
    case 2:
        return "Base transformable and renderable level object. ALO owns position, orientation, scale, model groups, animation state, bounds, visibility, lighting, and sound-facing behavior.";
    case 3:
        return "Base physical object. SO extends a renderable object with collision surfaces, rigid-body state, mass, gravity, contacts, constraints, and camera-collision behavior.";
    case 4:
        return "Model-set container used to group shared render resources and model hierarchies for the world. It is normally infrastructure owned by the level rather than ordinary placed scenery.";
    case 5:
        return "Physical gameplay-object foundation. PO extends collision-capable objects with interaction, damage, state, and gameplay filters inherited by many concrete world objects.";
    case 6:
        return "Stepping/platform gameplay object derived from the physical foundation. It provides support and stepping behavior used by moving or interactive platform-style classes.";
    case 7:
        return "Sly's player hierarchy root. It depends on a complete fixed player package, animation graph, abilities, camera, and known runtime identities; it cannot be created as an isolated map object.";
    case 8:
        return "Standard patrol guard character. It requires its guard model/skeleton, animations, state machine, AI references, patrol data, effects, and usually a reusable PROXY package.";
    case 9:
        return "Advanced smart guard character with awareness and flashlight/project-volume behavior. Import or create it only with its complete character, AI, animation, and PROXY dependencies.";
    case 10:
        return "Gomer guard archetype. It is a complete animated AI character, not just a mesh, and needs the matching skeleton, states, paths, effects, and gameplay references.";
    case 11:
        return "UBG guard/enemy specialization. It inherits the guard gameplay foundation and expects its original character package, animation state, AI configuration, and dependent objects.";
    case 12:
        return "MBG guard/enemy specialization. Use it for the corresponding original-game enemy package and preserve its model, skeleton, AI, animation, and effect dependencies.";
    case 13:
        return "BHG guard/enemy specialization. It is an authored character class whose behavior depends on the matching runtime package rather than a generic replacement model.";
    case 14:
        return "Murray character object. It requires Murray's fixed character assets, skeleton, animations, state logic, and mission references; it is not a generic NPC template.";
    case 15:
        return "PUFFC enemy/character specialization. It combines animated character rendering with class-specific gameplay behavior and must retain the matching dependent package.";
    case 16:
        return "Clockwerk-related CRFOD enemy object. It is a specialized animated gameplay character with class-specific state, references, and effects.";
    case 17:
        return "CRFODB variant of the CRFOD enemy family. Preserve the variant's original hierarchy, animation, AI, and effect relationships when transferring it.";
    case 18:
        return "CRFODK variant of the CRFOD enemy family. It is package-driven and should be authored through its verified hierarchy rather than as an empty isolated class.";
    case 19:
        return "Ruby boss/encounter character. It depends on boss-specific models, animations, scripts, effects, arena logic, and mission state.";
    case 20:
        return "Tank encounter object. It coordinates a specialized vehicle/enemy package and its gameplay logic, animation, collision, weapons, and effects.";
    case 21:
        return "ROV encounter or enemy specialization. It uses class-specific runtime behavior and must be paired with its expected render, animation, and gameplay dependencies.";
    case 22:
        return "Turret gameplay object. It combines aiming/animation with target logic, projectiles, collision, and effects; configure its references before expecting it to operate.";
    case 23:
        return "Vault gameplay object used by vault/mission interactions. It has specialized state and child/reference expectations and should be created from a verified template.";
    case 24:
        return "Puffer enemy specialization. It is an animated gameplay character with class-specific attacks, states, effects, and dependency requirements.";
    case 25:
        return "SUV vehicle/encounter object. It combines a vehicle model with physical and scripted gameplay behavior and requires its supporting hierarchy.";
    case 26:
        return "LGN encounter specialization. It is a concrete original-game gameplay class whose useful behavior depends on its matching authored package.";
    case 27:
        return "Jetpack character/gameplay object. It coordinates airborne movement, animation, effects, and mission logic rather than acting as a plain render model.";
    case 28:
        return "Rhythm encounter gameplay object. It participates in timing-driven mission logic and requires its sequence, animation, audio, and Splice connections.";
    case 29:
        return "MGV encounter specialization. Use it only with the class-specific hierarchy and runtime dependencies identified by its source level.";
    case 30:
        return "Runtime owner for live RIP/model-particle instances. SW creates a reusable pool of ten RIPGs; EMITTER definitions acquire those groups while effects run. RIPG is not an authored particle payload.";
    case 31:
        return "Water surface object with water-specific rendering and gameplay behavior. It needs valid surface geometry, material/shader state, bounds, visibility assignment, and water options.";
    case 32:
        return "Breakable physical object that owns intact/broken behavior. Complete setups also need break controllers or pieces, collision, damage filters, animations, effects, and sounds.";
    case 33:
        return "Standard authored breakable. BREAK derives directly from BRK and changes only the retail defaults: automatic coin count (-1) and player-or-bomb break responses. Its direct children are the intact/remain/fixed pieces managed by the inherited BRK rules.";
    case 34:
        return "Alarm-disabling breakable. It behaves as a standard BREAK, but an ALARM that lists its OID becomes its runtime owner; breaking it contributes to disabling that alarm.";
    case 35:
        return "Dart-gun gameplay object. It combines a weapon model with targeting/firing state, projectile references, collision, animation, sound, and effects.";
    case 36:
        return "Script-driven swooping projectile. A splice supplies a SHAPE path and duration; it follows that curve, enters its swoop state, damages the player near the end, triggers its crash explosion, and can also be broken.";
    case 37:
        return "Fragile object that reacts to supported damage or contact conditions. Configure its break state, fragments, collision, filters, effects, and reset/persistence behavior.";
    case 38:
        return "Contact-damage fragile object. On an accepted physical touch it schedules the inherited FRAGILE break and applies the selected ZPK damage kind to the current playable character; the damage need not be electric.";
    case 39:
        return "Break-piece physical object used by destruction setups. It may also be activated directly by MSGID_break_piece; an owning BRK, render model, collision, mass, and delayed fade are all optional in retail, depending on the intended piece behavior.";
    case 40:
        return "Physical push/switch button. It detects configured pushers, drives on/off animation and state, can invoke dialog or events, and supports reset/checkpoint rules.";
    case 41:
        return "Volume-triggered button. It activates from objects entering its trigger volume and uses filtering, repush, animation, state, and event settings.";
    case 42:
        return "JLO volume trigger specialization. It combines volume detection with JLO-specific gameplay behavior and should be connected to its intended target logic.";
    case 43:
        return "Physical skeleton/attachment node. BONE adds no class payload beyond SO; skin binding is authored in the owning ALO's GLOBSET BND table, which resolves bone OIDs contextually at runtime.";
    case 44:
        return "Standard prize/collectible foundation used by pickup classes. It coordinates collection state, interaction, render effects, sound, and persistence.";
    case 45:
        return "Checkpoint-persistent collectible. It behaves like SPRIZE, allocates one CHKMGR bit at runtime, records that bit when collected, and removes itself on later checkpoint restores while that bit remains set.";
    case 46:
        return "Clue bottle collectible. It requires the collectible model/effects and valid pickup, count, persistence, and level-progression behavior.";
    case 47:
        return "Alarm controller. It coordinates alarm state, connected detectors, guard reactions, effects, audio, and reset behavior for a security setup.";
    case 48:
        return "Detector/sensor foundation. It provides target filtering and detection behavior inherited by laser, camera, pressure, and other security sensors.";
    case 49:
        return "Laser detector and beam hazard. It uses detector endpoints/geometry, target filtering, alarm references, beam rendering, collision tests, and activation state.";
    case 50:
        return "Security camera detector. It combines a view/detection region with aiming or animation, target filters, alarm linkage, and visual feedback.";
    case 51:
        return "Pressure or proximity sensor specialization. It detects compatible targets in its configured region and forwards state to alarm or gameplay logic.";
    case 52:
        return "Barrier gameplay object controlling passage to a WARP destination. Its target is post-load resolved, so destination OID/CID and hierarchy must be valid at load time.";
    case 53:
        return "Runtime inverse-kinematics handle. IKH adds a transient target object/vector to SO, but no authored class options; gameplay or animation code assigns and clears that target at runtime.";
    case 54:
        return "TZP contact-zap hazard. Collision on this SO or a descendant SO inflicts the selected damage response, fires its zap event, and can relocate Sly using PNT, SHAPE, HND, PIPE, RAIL, or SPIRE recovery targets.";
    case 55:
        return "VOLZP active damage volume. Every update it tests the current player against its sphere or collision BSP, applies the inherited TZP zap response, and uses a closed BSP for pit/death containment.";
    case 56:
        return "Checkpoint controller. A full checkpoint normally includes its trigger/point children, animations, WARP destination, state machine, persistence, and respawn references.";
    case 57:
        return "Conveyor physical object. It applies signed belt velocity to physics contacts using cross(world Y, contact normal), and synchronizes the first SCROLLER animation from its mandatory baked UV averages.";
    case 58:
        return "Hide basket physics object. Sly can jump into and carry it; while carried it is parented and locked to Sly, while available it uses free physics and an upright spring. It also catches and limits attached darts.";
    case 59:
        return "Bomb physics/explosion object. It supports impact, timed, player/prop-impact, or manual priming; separate world/player blast radii; contact-only damage; impulse, debris, explosion visuals, sound, and reclaim behavior.";
    case 60:
        return "Launchable BOMB projectile. FireMissile supplies its runtime target object/local point; authored controls provide launch speed, trajectory-facing, four ignored object names, and four ignored class hierarchies. Optional ASEG OID 791 plays on launch.";
    case 61:
        return "Accelerating missile specialization. At launch it keeps the initial velocity direction, chooses a desired velocity at the authored cruise speed, disables gravity, and approaches that velocity using the authored acceleration cap; it does not home toward the target.";
    case 62:
        return "Target-guided missile specialization. Its launcher supplies a runtime target and local aim point; TARMISS predicts that target, follows a cubic intercept for an automatic or authored guidance duration, optionally banks into turns, then continues ballistically after guidance expires.";
    case 63:
        return "Sly's reusable deployed hat mine. JT infers one MINE at OID 0x192, copies an active hat slot or OID 0x050 fallback transform, rolls forward with hardcoded search motion until first contact/water entry, ignores JT collision, and uses inherited BOMB explosion settings.";
    case 64:
        return "Small flying SO creature with insect and pigeon modes. It wanders through an authored cylinder using fixed child-OID render frames; pigeons can seek LANDING curves, animate on the ground, flee Sly, and emit droppings, while insects continuously cycle airborne frames and play a buzz loop.";
    case 65:
        return "Physical rat creature. It scurries around its spawn, stops and squeaks, alerts nearby rats when Sly approaches, selects a world RATHOLE within its group radius, flees into that volume, hides with physics disabled, and later re-emerges. Movement frames are selected by distance travelled rather than elapsed animation time.";
    case 66:
        return "ROB harvester/crab template. ROB clones and configures pooled ROHs at runtime; each one spawns from a ROST tunnel, wanders or collects a ROC chest, grabs it with an OID override, returns it to a tunnel, and can enter happy, sad, dying, and inactive states.";
    case 67:
        return "ROB collectible/chest template. ROB clones a pool of physical ROCs, releases them into the arena, and lets ROH harvesters claim and carry them. Free ROCs take cane-hit damage through up to eight hit poses, can explode when destroyed, and optionally scale a bubble emitter from downward speed.";
    case 68:
        return "ROB spawn/return tunnel station. ROB registers authored ROST OIDs into available and unavailable tunnel lists, opens a station when dispatching or receiving a harvester, and uses its first direct PNT child as the exact ROH spawn/return position.";
    case 69:
        return "ROB encounter prize platform. It stays locked until the owning ROB finishes, optionally plays a reveal ASEG and explosion, detaches its KEY child into normal prize collection, then pulls that key toward the ROB vehicle until the level-state collected flag completes the encounter.";
    case 70:
        return "Reusable homing dart projectile managed by DARTGUN. The gun detaches and launches a pooled DART toward Sly or an authored RAT target; it continuously repredicts the target, damages ordinary contacts, embeds in HBSK baskets, can play a sticking ASEG, then fades and returns to the global free pool.";
    case 71:
        return "Jump-and-launch encounter actor. The globally activated JLO tracks triggered JLOVOLs, jumps between their landing points, faces Sly, chooses targets from each volume's JLOC, and uses a child RWM to fire straight, accelerated, lobbed, or spline projectiles on animation labels.";
    case 72:
        return "Puffer boss ignition target. The PUFFER vehicle aims at its required fire point, then activates its required fire emitter after the retail 0.25-second ignition delay.";
    case 73:
        return "MRKV marker volume. It describes a directed/depth-based marked region used by gameplay logic; its normal, alpha range, depth, and transform must be valid.";
    case 74:
        return "Clockwerk boss body. It blends authored movement and tracking ASEGs, owns the boss-health HUD state, and accepts qualifying damage only when a worker hit targets its assigned weak-point SO.";
    case 75:
        return "Final Clockwerk damage encounter. It consumes qualifying worker impacts, selects one of three optional hit-reaction animations, drives the boss HUD, and completes/autosaves the Clockwerk world after defeat.";
    case 76:
        return "Sly's reusable decoy power-up object. Retail removes it after load, deploys it from JT, lets physical/worker contacts consume its hit counter, and destroys it when hit out, forced, or more than 2000 units from Sly.";
    case 77:
        return "Runtime particle batch allocated from the world's reusable BLIPG pool. An EMITTER configures its shader, curves, colors, texture frames, ownership, and live particles; BLIPG has no authored BRX definition of its own.";
    case 78:
        return "Script-controlled animated camera policy. Its ALO transform supplies the final camera position; it either aims at a live ALO/PNT target or builds orientation from authored local view/up vectors.";
    case 79:
        return "Logical bone transform. LBONE is data-identical to an ALO and adds only optional debug bone-line rendering to its ALO children; it owns no skin weights or class-specific authored fields.";
    case 80:
        return "Particle emitter. It controls spawn timing, placement, velocity, lifetime, color/scale evolution, grouping, render payload references, and optional SHAPE-driven emission.";
    case 81:
        return "Localized light object. It defines type, color, intensity, direction, attenuation/range, visibility context, and optional shadow participation.";
    case 82:
        return "ALO-compatible class tag. Retail SCH uses the base ALO allocator, payload, clone, bind, update, transform, and render paths and adds no class-owned options or serialized tail.";
    case 83:
        return "Reusable PROXY instance/source wrapper. It instantiates a reusable hierarchy with contextual OID lookup, source slots, clone capacity, transforms, and per-instance overrides.";
    case 84:
        return "Camera-relative background root. Retail treats SKY as an ALO visual hierarchy, moves it to the active camera's X/Y every update, optionally follows camera Z, and forces it to remain unfrozen.";
    case 85:
        return "Shared dynamic-prize foundation. It drives collectible physics and states, contextual OID 607 state-machine animation, optional OID 603/604 glints and OID 605/606 effects, collection eligibility, reuse, and attraction effects.";
    case 86:
        return "Runtime charm-award prize. It adds no authored fields beyond DPRIZE: collection plays SFX 116, emits Splice event 2, then follows Swirl -> Stick before incrementing Sly's charm count. Retail normally reuses a pooled CHARM through CreateSwCharm.";
    case 87:
        return "Reusable life-token prize. It adds only the signed two-endpoint reuse-delay range EOPID 986 to DPRIZE, awards a life through the SMACK flow, checkpoints collection, and returns reusable tokens to the removed pool after their countdown.";
    case 88:
        return "Reusable coin prize. It adds only the signed two-endpoint maximum-lifetime range EOPID 985 to DPRIZE, awards one coin through the SMACK flow, checkpoints collection, and gives the 100-coin charm/life reward.";
    case 89:
        return "Level key prize. It adds no authored fields beyond DPRIZE; collection sets level flag 0x2 once, increments the world key count, notifies the player prize handler, emits Splice event 2, updates difficulty, and autosaves.";
    case 90:
        return "Timed-challenge gold prize. It adds no writable class field beyond DPRIZE; post-load binds it to NOTE timing, it expires against SW's gold deadline, and collection activates the timed-challenge NOTE state. EOPID 987 is runtime-only.";
    case 91:
        return "Vehicle boost prize. It adds no authored fields beyond DPRIZE and defaults ordinary player collection off. Every update it tests SUV collision around a point 200 units above the object, awards one boost, plays fixed effects/sound, and removes itself.";
    case 92:
        return "LOCK member visual driven by a LOCKG. It owns no class EOPIDs: LoadLockFromBrx contextually binds SM OID 555, PostLockLoad binds key visual ALO OID 557, and LOCKG requests locked/unlocked SM states 558/559.";
    case 93:
        return "LOCKG controller for up to eight LOCKs. It contextually binds SM OID 556, derives its save-world bit as 1 << ((index + 1) & 31), discovers descendant LOCKs first, appends explicit EOPID 1000 OIDs, and drives state 558 locked or 559 unlocked.";
    case 94:
        return "Procedural TAIL chain segment. The root follows only its first direct TAIL child, builds one solver point per segment plus a final local tip, and applies authored gravity, signed spring/damping, bend angle, iteration count, and optional unlocked-root rotation.";
    case 95:
        return "Rooftop robbery encounter controller. Retail selects one Primary/Secondary/Tertiary ROB, binds ROV/ROC/ROH/optional ROP templates plus up to sixteen ROST stations, builds reusable collectible/harvester pools, tunes difficulty, owns the encounter HUD, dialogs, win/lose flow, and prize reveal.";
    case 96:
        return "Smartguard flashlight volume, not a screen-flash effect. FLASH is a renderable ALO plus an inline TBSP containment volume; SMARTGUARD keeps the matching FLASH child, aims it, and tests targets with the Position-within query while runtime scale smoothly changes the detection volume.";
    case 97:
        return "Dynamic projected-shadow silhouette helper. ProjectCane renders the DYSH model into a runtime texture layer; ordinary GLOB geometry receives that projection through shadow-kind flags. The caster ALO's SHADOW settings hold the DYSH OID, shader, radii, cast distances, and projection controls.";
    case 98:
        return "STEPGUARD waypoint state node. Post-load binds contextual ASEG OID 747, enters Passive, removes the visible object from the world, and later cycles Passive/Sync/Active through its WPSG group. It can pause after activation and drive up to three unique root/state-machine goal routes.";
    case 99:
        return "Tunnel-camera volume. TN detects when the player enters its convex trigger BSP, activates a camera policy, and places the camera from either a fixed frame or a linear/cubic path.";
    case 100:
        return "JLO cannon-location controller, unrelated to dialog localization. It is an ALO removed after load that gathers up to sixteen descendant XFM targets and defines target choice, firing cadence, projectile travel time, straight/lobbed/spline mode, acceleration ratios, tilt, and launch/target speeds for a JLOVOL stop.";
    case 101:
        return "Dialog sequence object. It coordinates lines, speakers, timing, audio, camera/gameplay state, and Splice events for a conversation.";
    case 102:
        return "Dialog speaker definition. It identifies a speaker and its presentation/audio context for dialog sequences.";
    case 103:
        return "World-map controller. It owns the animated level map, cursor navigation, level-layer fades, task markers and map-to-level warp presentation.";
    case 104:
        return "CRFODK spawn controller. It creates or coordinates the corresponding CRFODK encounter package and its spawn-state dependencies.";
    case 105:
        return "Hub selection object. It participates in hub/level selection UI or progression and depends on the matching world-flow setup.";
    case 106:
        return "Hub progress display. Its OID selects one of the five game worlds, and up to nine fixed descendant models show completed approach/hub/job/boss progress.";
    case 108:
        return "Scene World container. It owns global world settings, resources, root hierarchy, lighting/environment data, and level infrastructure; the loader creates exactly one.";
    case 109:
        return "Camera Manager singleton. It coordinates active cameras, transitions, targets, and world camera state and is created by the game during level loading.";
    case 110:
        return "General-purpose SHAPE curve. It stores ordered control points, tangents/parameters, direction, and open/closed state for consumers such as effects or gameplay paths.";
    case 111:
        return "HSHAPE specialized curve used with hierarchy/gameplay ownership rules. It retains SHAPE geometry while adding the runtime semantics expected by its consumer.";
    case 112:
        return "Climbable PIPE path. Its ordered curve defines the route and direction used for attachment, traversal, and dismount behavior.";
    case 113:
        return "Traversal RAIL path. Its ordered curve controls rail movement, orientation, entry/exit direction, and related traversal behavior.";
    case 114:
        return "Flying-enemy landing locus. FLY enemies choose the closest point across every registered LANDING curve, then search straight down for physical collision.";
    case 115:
        return "Transform marker/helper. XFM provides a named position, orientation, and scale for consumers without requiring visible geometry.";
    case 116:
        return "Warp destination/action object. It stores the destination transform and velocity/camera options and teleports the player when its trigger action is invoked.";
    case 117:
        return "Target marker. It provides a transformable target used by aiming, cameras, movement, effects, or scripted gameplay systems.";
    case 118:
        return "Grab handhold target. HND is an XFM/TARGET registered in the global target list and used by Sly's hook, hang, recovery, and jump-target selection; it is not a character-hand bone attachment.";
    case 119:
        return "Inert explosion transform base. EXPL contributes only an XFM and parent-group link; its retail vtable has no explode callback, so useful effects come from EXPLG or EXPLS descendants.";
    case 120:
        return "Explosion group. EXPLG loads authored records, extracts nested EXPLG/EXPLS entries (including entries inside one ALO wrapper), keeps at most eight, and fires them as one effect.";
    case 121:
        return "Explosion-emitter definition base. EXPLO owns origin, direction, velocity and particle-source configuration, but its retail explode callback is null; EXPLS is the directly fireable particle implementation.";
    case 122:
        return "Fireable particle explosion. EXPLS extends EXPLO with particle representation, lifetime/motion, RIP or BLIP rendering, optional sound, delay, grouping and render/touch references.";
    case 123:
        return "Convex point-containment volume. VOL stores a local transform and a plane/BSP tree only; it has no filters, events, or callbacks of its own. Other objects reference it and call the retail point-within test.";
    case 124:
        return "Rat escape-hole volume. RATHOLE adds no authored fields to VOL; it registers itself in the world's rat-hole list so fleeing RAT objects can select it, run toward its origin, enter its containment volume, hide, and later re-emerge.";
    case 125:
        return "Level exit object. It connects a trigger/interaction to destination level metadata, optional alternate destination, transition behavior, and world-flow state.";
    case 126:
        return "Point marker. PNT stores a position used by paths, destinations, scripts, AI, and other gameplay consumers.";
    case 127:
        return "Hide point. HPNT extends PNT with a flattening radius, hide posture, optional tunnel-camera reference, detection/tunnel flags, and a maximum vertical jump-target difference. Its facing is computed from Sly relative to the point, not from an authored orientation.";
    case 128:
        return "Jump-match target. JMT is a globally registered point with a capture radius and an optional SO whose collision zone is set to Ignore while the jump target is used; it does not store orientation or a movement path.";
    case 129:
        return "Ninja-spire interaction point. It marks a valid spire perch/target used by Sly's unlocked circle-button traversal ability.";
    case 130:
        return "Binoc-u-com scan label anchor. SCAN is a PNT plus one display string: the binocular overlay searches SCAN objects directly, projects their positions, replaces underscores with spaces, and selects the label nearest the reticle.";
    case 131:
        return "Animation segment. ASEG stores animation timing, playback, events, and model/skeleton connections used by state machines and animated objects.";
    case 132:
        return "Runtime animation blend. ASEGBL is allocated by EnsureAsegBlendDynamic, removed from the authored hierarchy, and populated from active ASEG blend records and marker groups. Retail does not load an authored ASEG payload for this CID.";
    case 133:
        return "Runtime visibility-zone class shell. VISZONE has only LO data and no spatial payload; authored zone membership is represented by 32-bit masks in VISMAP leaves, one-hop masks, objects, and geometry rather than standalone VISZONE volumes.";
    case 134:
        return "World visibility map. The singleton VISMAP owns a plane BSP whose leaves contain 32-bit zone masks plus one-hop expansion masks used to classify points/spheres and cull objects and geometry.";
    case 135:
        return "Freeze-group merger. FRZG resolves up to eight descendant ALO OIDs beneath its parent, registers them as one merge group, and merges their existing freeze roots so visibility/update freezing is coordinated.";
    case 136:
        return "State machine. SM stores goals/states and transitions that drive animation or gameplay behavior for its owning hierarchy.";
    case 137:
        return "Guard squad group. SGG groups guards and coordinates squad membership, shared state, activation, and editor squad visualization.";
    case 138:
        return "Navigation/path zone. PATHZONE defines a spatial region used by AI or movement systems and connects to navigation/path data.";
    case 139:
        return "Reach/swing pose map. RCHM maps a target position around an animated host through a tetrahedral volume into weights for 24 ASEG pose samples used by Sly's reach and pipe-swing joint tracking.";
    case 140:
        return "Reusable weapon manager. RWM pools reusable SO ammunition, selects firing and target transforms, predicts or homes toward targets, and optionally controls an aiming transform.";
    case 141:
        return "Geometry warp renderer. WR builds up to eight ordered circular-wave or bend/swivel operators and deforms the positions and UVs of GLOB geometry that references it.";
    case 142:
        return "Keyhole transition mask. KEYHOLE stores the five-layer 2D iris mesh drawn by scene/death transitions; it is unrelated to collectible keys or lock interaction objects.";
    case 143:
        return "Player choreography script. JSG executes an ordered command stream that can change lookup context, cut or focus Sly, enter tunnels, pause, jump, run, climb, attack, play an ASEG, alter the clock, or hide.";
    case 144:
        return "Ruby rhythm timeline. RYTHMSEQUENCE dispatches ordered measure/beat commands to add weapon matches, emit its splice event, and enable or disable rhythm input and visual effects.";
    case 145:
        return "Thunder/screen-flash effect. It applies a timed full-screen tint/flash using configured color and alpha behavior, commonly for lightning or dramatic events.";
    case 146:
        return "Runtime animation-segment application. ASEGA is created when an ASEG is applied to an ALO hierarchy and owns playback time, active event state, speed, priority, subscriptions, and retraction state.";
    case 147:
        return "Runtime state-machine application. SMA is created by applying an authored SM to an ALO hierarchy and tracks the current/goal state, route, transition, and active ASEGA playback.";
    default:
        return "Registered ProjectCane runtime class. Review its inherited properties, source hierarchy, loader, and references before creating it as standalone content.";
    }
}

void DrawObjectTooltip(const ObjectTypeDescriptor &type, const char *friendlyLabel, const bool unavailable,
                       const char *unavailableReason)
{
    if (!ImGui::BeginItemTooltip())
        return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 38.0f);
    ImGui::TextColored(ImVec4(.45f, .82f, 1.0f, 1.0f), "%s", friendlyLabel);
    ImGui::TextDisabled("Runtime class %s | CID %d | %s", BrxDocument::ClassName(type.cid), type.cid,
                        CategoryForCid(type.cid));
    ImGui::Separator();
    ImGui::TextWrapped("%s", ObjectSummary(type.cid));
    ImGui::Separator();
    ImGui::Text("Capabilities: transform/render %s | collision/physics %s | gameplay %s",
                type.hasAppearance ? "yes" : "no", type.hasPhysics ? "yes" : "no", type.hasGameplay ? "yes" : "no");
    ImGui::Text("Authoring support: %zu writable field%s | specialized inspector %s", type.writableOptions.size(),
                type.writableOptions.size() == 1 ? "" : "s", type.hasSpecializedInspector ? "yes" : "no");
    ImGui::TextDisabled("The CID controls runtime behavior. The friendly name is editor-only.");
    if (unavailable)
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, .68f, .25f, 1.0f), "Not directly creatable");
        ImGui::TextWrapped("%s", unavailableReason);
    }
    else
    {
        ImGui::Separator();
        ImGui::TextWrapped(
            "After creation, satisfy the object's hierarchy, required references, assets, and class-specific validation before export. An empty CID record is not automatically a complete gameplay setup.");
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool IsUnavailable(const std::int16_t cid, const char *&reason)
{
    switch (cid)
    {
    case 0:
        reason = "Abstract runtime foundation; no BRX constructor.";
        return true;
    case 4:
        reason =
            "MS is the world's singleton merged-scene hierarchy. Edit or replace the existing MS instead of creating a second one.";
        return true;
    case 7:
        reason = "Requires Sly's complete fixed-OID player hierarchy.";
        return true;
    case 30:
        reason =
            "RIPG is allocated into SW's reusable runtime particle-group pool. Author model-particle behavior on an EMITTER instead.";
        return true;
    case 83:
        reason =
            "A PROXY requires an inline reusable source or a valid earlier source slot. Use Create reusable source from the selected hierarchy, then Create linked instance.";
        return true;
    case 107:
        reason = "Retail runtime integrity/anti-cheat infrastructure; intentionally excluded from level authoring.";
        return true;
    case 108:
        reason = "The Scene World is represented by the BRX world container.";
        return true;
    case 109:
        reason = "The game creates its single Camera Manager while loading the world.";
        return true;
    case 132:
        reason =
            "ASEGBL is an in-memory blended animation object allocated by the animation blender. Author ASEG records and blend bindings instead.";
        return true;
    case 133:
        reason =
            "VISZONE is a runtime class-table shell. Authored visibility zones are structural records inside the singleton VISMAP, not standalone world objects.";
        return true;
    case 134:
        reason =
            "VISMAP is the world's singleton visibility structure. Edit or rebuild the existing VISMAP through the VISMAP workflow.";
        return true;
    case 139:
        reason =
            "RCHM requires a complete reach lattice, tetrahedra, surface geometry, category BSP, host/touch bindings, and 24 animation samples. Edit or duplicate a validated retail RCHM until the dedicated topology builder is available.";
        return true;
    case 146:
        reason = "ASEGA is created at runtime from authored ASEG data and has no world-object BRX loader.";
        return true;
    case 147:
        reason = "SMA is created at runtime from an authored SM definition and has no world-object BRX loader.";
        return true;
    default:
        reason = nullptr;
        return false;
    }
}

EditorOperationResult CreateCatalogObject(BrxDocument &document, const std::int16_t cid, const std::int32_t parent,
                                          const float *transform)
{
    switch (cid)
    {
    case 31:
        return EditorDocumentOperations::CreateWater(document, parent, transform);
    case 40:
        return EditorDocumentOperations::CreateButton(document, EditorDocumentOperations::ButtonPreset::Push, parent,
                                                      transform);
    case 41:
        return EditorDocumentOperations::CreateVolbtn(document, parent, transform);
    case 42:
        return EditorDocumentOperations::CreateJlovol(document, parent, transform);
    case 49:
        return EditorDocumentOperations::CreateLasen(document, parent, transform);
    case 50:
        return EditorDocumentOperations::CreateCamsen(document, parent, transform);
    case 52:
        return EditorDocumentOperations::CreateBarrier(document, parent, transform);
    case 54:
        return EditorDocumentOperations::CreateTzp(document, parent, transform);
    case 55:
        return EditorDocumentOperations::CreateVolzp(document, parent, transform);
    case 56:
        return EditorDocumentOperations::CreateCheckpoint(document, parent, transform);
    case 99:
        return EditorDocumentOperations::CreateTn(document, EditorDocumentOperations::TnPreset::StaticFrame, parent,
                                                  transform);
    case 101:
        return EditorDocumentOperations::CreateDialog(document, parent, transform);
    case 110:
        return EditorDocumentOperations::CreateShape(document, false, parent, transform);
    case 111:
        return EditorDocumentOperations::CreateHshape(document, false, parent, transform);
    case 112:
        return EditorDocumentOperations::CreatePipe(document, false, parent, transform);
    case 113:
        return EditorDocumentOperations::CreateRail(document, false, parent, transform);
    case 114:
        return EditorDocumentOperations::CreateLanding(document, false, parent, transform);
    case 115:
        return EditorDocumentOperations::CreateXfm(document, parent, transform);
    case 116:
        return EditorDocumentOperations::CreateWarp(document, parent, transform);
    case 123:
        return EditorDocumentOperations::CreateVolume(document, parent, transform);
    case 125:
        return EditorDocumentOperations::CreateExit(document, parent, transform);
    case 126:
        return EditorDocumentOperations::CreatePnt(document, parent, transform);
    case 127:
        return EditorDocumentOperations::CreateHpnt(document, parent, transform);
    case 128:
        return EditorDocumentOperations::CreateJmt(document, parent, transform);
    case 129:
        return EditorDocumentOperations::CreateSpire(document, parent, transform);
    case 130:
        return EditorDocumentOperations::CreateScan(document, parent, transform);
    case 138:
        return EditorDocumentOperations::CreatePathZone(
            document,
            std::vector<std::array<float, 3>>{
                {-200.0f, -200.0f, 0.0f}, {200.0f, -200.0f, 0.0f}, {200.0f, 200.0f, 0.0f}, {-200.0f, 200.0f, 0.0f}},
            parent);
    default:
        return EditorDocumentOperations::CreateObject(document, cid, parent, transform);
    }
}

const char *ObjectExample(const std::int16_t cid)
{
    switch (cid)
    {
    case 0:
        return "Do not create BASIC directly. Use it only to understand the identity and property foundation inherited by every concrete object.";
    case 1:
        return "Use an LO-derived concrete class when you need a hierarchy/event object without assuming that it has a transform or visible model.";
    case 2:
        return "Import a decorative statue as an ALO when it needs a transform and model but should not block or physically react to the player.";
    case 3:
        return "Use an SO-derived concrete object for a static platform that must render and provide gameplay collision.";
    case 4:
        return "Keep the level's MS as the shared model-resource container; do not create a second ordinary scenery object in its place.";
    case 5:
        return "Choose a PO-derived concrete CID for an object that must collide and respond to damage, cane, bomb, or other gameplay interactions.";
    case 6:
        return "Use a STEP-derived platform class for a surface whose stepping or platform behavior is part of gameplay rather than plain static collision.";
    case 7:
        return "Import the complete retail Sly package only for player-system research; never use JT as a shortcut for placing a player-shaped model.";
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 15:
    case 16:
    case 17:
    case 18:
    case 24:
        return "Export a known-working guard/enemy as a portable PROXY hierarchy, import the whole package, then assign its patrol/path and encounter references. Creating only this CID will not produce a working character.";
    case 14:
    case 19:
    case 20:
    case 21:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
        return "Start from the complete original encounter hierarchy, duplicate it into a controlled test map, and change placement or links incrementally while preserving boss/mission scripts and dependencies.";
    case 22:
        return "Place a turret with its aiming target, projectile class, firing animation, collision, effects, and activation event to build a stationary ranged hazard.";
    case 23:
        return "Use VAULT inside a verified vault mission setup where scripts, state, children, and progression references already establish the complete interaction.";
    case 30:
        return "Do not create RIPG directly. Configure RIP/model-particle definitions on an EMITTER; the runtime acquires a reusable RIPG from SW's pool when that emitter plays.";
    case 31:
        return "Create WATER over a pool mesh, assign the water material and options, calculate bounds, and place it in the correct VISMAP zone.";
    case 32:
    case 33:
    case 34:
    case 36:
    case 37:
    case 38:
    case 39:
        return "Build a breakable crate with an intact BRK/FRAGILE object, BREAK controller, BRKP fragments, valid collision, damage filters, burst effects, and sound; test both break and cleanup states.";
    case 35:
        return "Create a dart-gun trap, point it at the play space, connect its DART projectile and trigger logic, then test firing cadence and collision filters.";
    case 40:
        return "Create a push button that plays its on animation and triggers a linked door, barrier, Splice callback, or state change when Sly presses it.";
    case 41:
    case 42:
        return "Place the volume over the activation area, select allowed target CIDs, connect its trigger event to the intended action, and test enter, exit, and repeat behavior.";
    case 43:
        return "Add BONE under the intended rig hierarchy, give it the OID referenced by the model's GLOBSET BND entry, then validate the GLOB vertex bone indices and contextual OID resolution.";
    case 44:
    case 45:
    case 86:
    case 90:
    case 91:
        return "Use the corresponding prize template to place a collectible or power-up, then verify model, pickup effect, sound, value/progression state, and persistence.";
    case 85:
        return "Use DPRIZE as the shared collectible foundation only when the hierarchy supplies its required SM at contextual OID 607. Optional child roles are PNT 603/604 and EXPLO 605/606; concrete rewards normally use CHARM, LIFETKN, COIN, KEY, GOLD, or BOOST.";
    case 46:
        return "Place a CLUE bottle in a reachable location, preserve its clue-count/progression identity, assign pickup effects and sound, and test save-state collection.";
    case 47:
        return "Use one ALARM controller to coordinate several LASEN/CAMSEN detectors, guard reactions, alarm audio/effects, and reset logic for a security area.";
    case 48:
        return "Use SENSOR as the verified base of a concrete detector setup; normally choose LASEN, CAMSEN, or PRSEN for an actual placed security sensor.";
    case 49:
        return "Create LASEN endpoints across a corridor, enable its beam preview, connect it to ALARM, and test that crossing the beam triggers only the intended targets.";
    case 50:
        return "Mount CAMSEN on a wall, aim its detection cone, set target filters and scan behavior, then connect detection to the local ALARM.";
    case 51:
        return "Use PRSEN for a pressure/proximity trigger region that activates security or gameplay logic when a compatible object enters it.";
    case 52:
        return "Place a BARRIER as a fixed collision gate. A destination is optional: use exactly one direct PNT for a local teleport, or reference a valid WARP; with neither, it remains a filterable solid/pass-through barrier.";
    case 53:
        return "Preserve IKH only inside a verified hierarchy whose runtime code drives SetIkhTarget. Its target pointer/vector are transient and cannot be authored as BRX properties; use inherited SO/ALO data only for its physical placement/appearance.";
    case 54:
        return "Use TZP for a physical hazard surface or hierarchy: provide contact-producing SO collision, select the damage kind, and optionally assign recovery targets used after Sly is hit.";
    case 55:
        return "Use VOLZP for continuously tested hazard space. Use a sphere or BSP for ordinary damage; pit/death mode needs a closed BSP that fully encloses a falling player before it fires.";
    case 56:
        return "Use the Checkpoint recipe to create CHKPNT with trigger children, WARP, animation/state logic, and respawn data, then test death and reload from both sides.";
    case 57:
        return "Turn a collision-bearing SO into CNVO, set signed belt speed and baked UV averages, then test contact direction on every relevant surface. Lock it only when the intended platform itself must remain fixed.";
    case 58:
        return "Give HBSK direct collision so darts can strike it, tune its hide-position radius and signed entry-height cutoff, and choose the attached-dart limit. Test both free/upright and carried/locked states.";
    case 59:
        return "Give BOMB collision when using impact/contact behavior, choose its trigger and orientation modes, tune both blast radii and impulse limits, and attach direct BRKP debris or an EXPL hierarchy. Timed delay is supplied by the runtime owner when it primes the bomb; it is not an authored fuse field.";
    case 60:
    case 61:
    case 62:
        return "Use the missile with a valid runtime launcher/owner, initial transform, collision exclusions, and inherited BOMB payload. For TARMISS, supply a live target and test both its guided interval and the ballistic motion that follows expiration.";
    case 63:
        return "Author MINE inside its owning JT hierarchy at OID 0x192, retain an ALO-compatible OID 0x050 deployment origin and optional OIDs 0x18e-0x191 hat slots, provide collision, then configure its inherited BOMB trigger, blast, effects, sound, and reclaim behavior.";
    case 64:
        return "Create FLY as a complete child package: airborne ALO frames OID 572-587, death appearance 598, and TARGET 600. Pigeons additionally need ground frames 589-596 and may use a soaring frame from the airborne range, EXPLS 601, PNT 602, and nearby LANDING curves; insects use only airborne cycling and buzzing behavior.";
    case 65:
        return "Create RAT with TARGET child OID 615, stopped ALO 616, scurry frames 617-632, and flee frames 633-647. Add at least one world RATHOLE inside its group radius—retail dereferences the selected hole immediately on Flee—then tune wander distance, speeds, distance-per-frame, leap speed, stop minimum, rewards, collision, and inherited explosion payload.";
    case 66:
        return "Author ROH as the reusable harvester hierarchy named by a ROB slot, not as a standalone creature—PostRohLoad removes the source template. Preserve ASEG children 668 move, 669 carry, 670 grab, 671 death; optional SM 673 happy and 674 sad; collision/explosion payload; and the ROB/ROC/ROST runtime links supplied to each clone.";
    case 67:
        return "Author ROC as the reusable collectible hierarchy named by ROB, with optional SM OID 675 and its state labels 676-685, optional bubble EMITTER 686, inherited collision, and explosion payload. Tune hit count and hit-up/down timing; the owning ROB creates runtime copies and supplies ownership, while carried copies reject cane damage.";
    case 68:
        return "Place ROST as an authored world station, register its OID through the owning ROB's repeatable Spawn tunnel option, add exactly one direct PNT child for harvester movement, and optionally provide SM OID 687 with close/open goals 688 and 689. Test tunnel availability as ROHs spawn, return cargo, die, and recycle.";
    case 69:
        return "Create ROP as a locked SO referenced by the owning ROB prize option. Add a KEY-derived descendant at OID 257, optional reveal ASEG OID 691, and optional inferred EXPL payload. Test StayPut, reveal, KEY detachment/collection, attraction toward the ROB vehicle, and the final collected level-state transition.";
    case 70:
        return "Place the source DART inside its DARTGUN hierarchy and point the gun's Dart template OID at it. Give it positive homing speed, fire/impact cloud radii, fade and maximum-stuck times, optional sticking ASEG OID 700, collision/model data, and optional inferred EXPL; the gun clones the requested pool and supplies target, height offset, launch transform, and ownership at runtime.";
    case 71:
        return "Build JLO with required SM 750 and states 751-758, feet PNT 759, and child RWM OID 1; optional reticle 760, tracer 761, tracer-start PNT 762, and reticle ASEGs 763-764. Add JLOVOL activation regions with valid landing PNT/JLOC references, select an optional starting volume, then test activation, jump/land timing, projectile modes, animation labels 765-768, reticle behavior, and ambient audio distances.";
    case 72:
        return "Place PUFFT targets for the PUFFER encounter with EMITTER OID 996 and PNT OID 997 beneath each target. EXPLO OID 998 is optional and redirects the projectile spline effect. Keep the encounter at 32 targets or fewer: retail searches only a 32-entry array even though every PUFFT increments the completion total.";
    case 73:
        return "Place MRKV as a directed marked volume, set a finite normal, depth, and alpha range, then connect it to the gameplay system that interprets that region.";
    case 74:
        return "Author at least one ASEG in each fixed blend group: phase zero OIDs 1128-1131, phase one 1132-1135, and tracking 1136-1140. Scripts select phase, tracking, weak point, and damage animation; test worker-hit targeting and the boss HUD lifecycle.";
    case 75:
        return "Create the CLKWRKDMG damage target with its authored hit count and optional damage ASEGs, then test activation, qualifying worker impacts, the delayed defeated transition, and mission scripts.";
    case 76:
        return "Build the fixed JT package: DECOY OID 337 beneath JT, required destruction EXPL OID 338 beneath the decoy, and deployment ASEG OID 339 beneath JT. Hits and destruction-effect references are runtime splice properties, not SetUser options.";
    case 77:
        return "Do not author BLIPG directly. Create and edit an EMITTER; retail allocates a BLIPG automatically while that emitter is playing and recycles the group afterward.";
    case 78:
        return "Place CAMERA at the final viewpoint, optionally assign a target OID, and invoke ENABLE/DISABLE from Splice or ASEG events. With a target, local eye affects aiming while view/up are unused; without one, view/up must form a valid basis.";
    case 79:
        return "Use LBONE as an ordinary transform node where the hierarchy should be visualized as a logical bone chain. Author its inherited ALO transform, children, animation, and constraints; skinning data remains in model GLOB/GLOBSET records.";
    case 80:
        return "Create an EMITTER for smoke, sparks, aura, rain, or debris; configure its RIP/model and material payload, tune lifetime/motion/color, and optionally distribute it along a SHAPE. Runtime RIPGs are acquired automatically.";
    case 81:
        return "Place LIGHT near a lamp, choose color/intensity/range and attenuation, assign visibility, and enable shadows only for intended casters.";
    case 82:
        return "Use SCH wherever a script or contextual class lookup specifically expects CID 82; edit it exactly like an ALO through inherited transform, hierarchy, model, animation, and constraint controls.";
    case 83:
        return "Create a linked PROXY instance when several guards or props should share one reusable source hierarchy while retaining per-instance transforms and overrides.";
    case 84:
        return "Parent background-phase ALO geometry beneath SKY, choose whether it follows camera height, and keep its authored Z at the desired backdrop height when Z-follow is disabled.";
    case 87:
    case 88:
    case 89:
        return "Place the collectible at a reachable transform, verify its model/effect/sound, connect progression where required, and test collection plus save/reload persistence.";
    case 92:
        return "Create LOCK only as a complete hierarchy with contextual SM OID 555 containing states 558/559 and key visual ALO OID 557, then include it beneath or explicitly reference it from a LOCKG.";
    case 93:
        return "Create LOCKG with contextual SM OID 556, assign its world-state bit index, and add up to eight LOCK members either as descendants or explicit OID entries.";
    case 94:
        return "Build a single-child TAIL chain under the intended animated owner. Tune the root's solver fields; the root owns the shared simulation, while each segment contributes its transform/length and the last segment uses the root Tip position.";
    case 95:
        return "Create ROB with a unique mission variant; assign its ROC collectible template, ROH harvester template, ROV vehicle, optional ROP prize, and up to sixteen ROST stations with PNT children. Author tuning set 0, switch EOPID 802 to set 1 and author it, then tune arena geometry, total collectible count, and difficulty curve.";
    case 96:
        return "Create FLASH beneath its SMARTGUARD hierarchy, give it the OID selected by the guard's flashlight setting, author its inherited visible cone/model, and shape the inline TBSP to the same local-space beam. Use the live point test and volume overlay to verify detection; use THNDFLASH for a screen flash.";
    case 97:
        return "Create or select the DYSH silhouette helper, then configure the caster ALO's Dynamic Shadow Object OID, shadow shader, radii, and cast distances. Enable the matching shadow-kind receiver flag on world GLOBs and inspect the projected result from several angles.";
    case 98:
        return "Place WAYPOINT under the guard/path hierarchy, optionally supply contextual ASEG OID 747, choose a synchronization key shared with its SGG group, and author trigger/untrigger state-machine goals. Retail accepts signed pause values (negative means the waypoint becomes passive immediately) and stores only three unique route pairs because of its original capacity check.";
    case 99:
        return "Create a tunnel-camera trigger from the fixed-camera, linear-path, or cubic-path preset. Resize its local trigger volume, author the camera path, then tune direction, framing, panning, priority, and activation options in the TN Camera Manager.";
    case 100:
        return "Place JLOC beneath the intended target-marker hierarchy, add one to sixteen descendant XFM firing targets, choose Closest or Random target selection, and tune the JLO projectile mode. Reference it from JLOVOL; ensure travel time is positive and accelerated-missile ratios do not create a zero divisor.";
    case 101:
        return "Create DIALOG, assign SPEAKER entries and lines/audio, connect its trigger event, and preview the complete sequence before runtime testing.";
    case 102:
        return "Define SPEAKER for a dialog participant, then reference it from DIALOG lines with the correct voice/presentation context.";
    case 103:
        return "Use WM as the root of a retail-style world-map hierarchy. Its open/close and per-level warp animations, primary/secondary map models, and cursor anchor points are bound by fixed contextual OIDs; it is not a weapon object or ordinary visible prop.";
    case 104:
        return "Use CRFODKSPAWN to instantiate the matching CRFODK encounter package from a scripted or mission-controlled spawn point.";
    case 105:
        return "Use HUBSEL as the hideout world-selection controller, with its retail fixed state machines, world animations/models, and progress dialogs.";
    case 106:
        return "Use HUBPROG with OID 1301..1305 to display progress for Underwater through Clockwerk. Descendant model OIDs 1306..1314 are optional indicators; retail adds only the entries whose corresponding level or world is complete.";
    case 108:
        return "Edit the existing Scene World panel for ambient lighting, music, fog, and global settings; do not create another SW object.";
    case 109:
        return "Inspect the existing CM for camera-system diagnostics; do not create a second manager because the runtime owns the singleton.";
    case 110:
        return "Build a closed SHAPE from marker origins around a tower, smooth/reparameterize it, then assign it to an EMITTER for an aura path.";
    case 111:
        return "Create HSHAPE under the object that owns the hiding/gameplay curve, build its ordered control points, and verify the consuming reference.";
    case 112:
        return "Draw PIPE from bottom to top along a pole or drainpipe, inspect direction, and test attach, climb, and dismount behavior.";
    case 113:
        return "Draw RAIL along a wire or ledge, keep point order intentional, and test entry, movement direction, corners, and exit.";
    case 114:
        return "Draw LANDING above surfaces where FLY enemies may land. Keep every useful locus point within 500 world units above physical collision; curve direction is ignored by the retail selection code.";
    case 115:
        return "Place XFM as an invisible destination/attachment transform for a camera, spawn, effect, animation, or script.";
    case 116:
        return "Place WARP at the destination, set orientation/velocity behavior, then trigger it from BARRIER, EXIT, BUTTON, VOL, or Splice action 162.";
    case 117:
        return "Place TARGET where a camera, turret, missile, look-at controller, or effect should aim, then assign its OID to the compatible consumer.";
    case 118:
        return "Place HND at a grabbable hook or hanging point, orient its local +X axis for forward/backward approach, then tune hook radius, allowed grab direction, sounds, and maximum vertical jump-target difference.";
    case 119:
        return "Use plain EXPL only as an inert transform/base expected by an existing hierarchy; it cannot emit an effect by itself.";
    case 120:
        return "Create EXPLG when one trigger must fire several nested EXPLG/EXPLS entries together. Keep the effective accepted-entry count at eight or fewer.";
    case 121:
        return "Use EXPLO as a reusable emitter-definition base only where the retail hierarchy expects it; use EXPLS for a directly fireable particle effect.";
    case 122:
        return "Create EXPLS for a fireable particle explosion, configure its origin/direction and RIP or BLIP payload, then connect it to an EXPLG, projectile, breakable, hazard, or script trigger.";
    case 123:
        return "Shape the local TBSP around the region a consuming object must test, then reference the VOL from that object's own option or hierarchy. VOL itself does not poll objects or fire events; preview point containment and verify the consumer separately.";
    case 124:
        return "Place the RATHOLE origin where rats should run and reappear, and shape its inherited VOL around the final entrance area. Keep it in the same world as the RAT population; retail discovers it from the global rat-hole list and requires no target, animation, or mission references on RATHOLE itself.";
    case 125:
        return "Place EXIT in its activation area, choose destination level and optional alternate route, then test transition, spawn destination, and return flow.";
    case 126:
        return "Place PNT as a simple destination or path marker and reference it from the AI, camera, effect, warp, or script that consumes a position.";
    case 127:
        return "Place HPNT at the center of a hide position, give it a positive flattening radius, choose the retail hide posture, and optionally bind a TN used while hiding. Tune tunnel/detection and vertical target limits; no orientation is serialized.";
    case 128:
        return "Place JMT at the desired jump-match position, tune its capture radius, and optionally reference the SO that should be ignored during the maneuver. Selection comes from the global JMT list; no orientation or path is serialized.";
    case 129:
        return "Place SPIRE at the exact perch position and tune the maximum vertical target difference used by traversal selection. Its second option is retained as the retail boolean/history field; no orientation is serialized by SPIRE.";
    case 130:
        return "Place the SCAN point where its dossier label should anchor (usually on or near the guard), then author the underscore-separated display text. Retail considers at most 32 SCAN objects, only within 4000 world units and inside the camera frustum; no target-object reference is serialized.";
    case 131:
        return "Create or edit ASEG for an animation clip, set timing/loop/rate and events, then connect it to the owning skeleton and state machine.";
    case 132:
        return "Do not author ASEGBL directly. Author the source ASEGs and the object's blend configuration; retail allocates and fills ASEGBL dynamically when that blend is evaluated.";
    case 133:
        return "Do not create a standalone VISZONE volume. Define and assign zone bits through the existing VISMAP workflow; CID 133 is only the retail class-table shell.";
    case 134:
        return "Edit the singleton VISMAP BSP leaves and one-hop masks, assign matching zone masks to objects/geometry, then test point and sphere classification at seams while watching culling diagnostics. Retail supports at most 32 zone bits.";
    case 135:
        return "Add two to eight descendant ALO OIDs that must share one runtime freeze root. OIDs resolve below FRZG's parent context; FRZG has no explicit freeze/unfreeze event of its own, so test camera-zone freezing and remerging of every member.";
    case 136:
        return "Use SM to author goals and transitions for a compatible owner, connect ASEGs/actions, and test every transition plus fallback behavior.";
    case 137:
        return "Create SGG for a coordinated guard group, add compatible guard members, configure shared activation/state, and preview the squad overlay.";
    case 138:
        return "Draw PATHZONE over walkable navigation space, repair topology/BSP, connect neighboring regions, and assign guards or path consumers.";
    case 139:
        return "Use RCHM for character reach or swing posing: assign the animated host and touch joint, bind its 24 ASEG samples below that host, and preserve a validated grid, tetrahedra, and category BSP. It is not an AI navigation map.";
    case 140:
        return "Configure reusable SO ammunition caches, firing source, target mode, prediction curves, and optional aim controls; then validate every hierarchy-relative OID and ammunition capacity.";
    case 141:
        return "Add circular waves or bend/swivel operators in serialized order, tune their envelopes and noise, then assign this WR's OID to compatible GLOB warp bindings to preview animated geometry deformation.";
    case 142:
        return "Edit the screen-space points, bounds, and five triangle layers that form the transition iris. KEYHOLE has no world transform, key requirement, lock state, or interaction event.";
    case 143:
        return "Author JSG commands in execution order. Use Context to control later OID lookup, choose synchronous/asynchronous movement commands deliberately, and validate every TUNNEL, ASEG, point, shape, or transform target.";
    case 144:
        return "Author commands in nondecreasing measure/beat order against Ruby's live rhythm clock. Add-match and splice-event commands carry a weapon/event argument; input/effect commands carry only timing.";
    case 145:
        return "Create THNDFLASH with a tinted yellow color and short alpha envelope, then trigger it alongside lightning audio/effects.";
    case 146:
        return "Do not place ASEGA in a level. Author ASEG records and runtime operations create and manage ASEGA instances automatically.";
    case 147:
        return "Do not place SMA in a level. Author an SM and its state/transition ASEGs; applying that SM creates the SMA runtime instance automatically.";
    default:
        return "Create this CID only from a verified source/template, inspect all inherited operations and references, then validate and runtime-test it in isolation.";
    }
}
}

void DrawCreateObjectCatalogPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace,
                                  SceneRenderer &renderer, const std::int32_t selectedObjectIndex, std::string &status)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(720.0f, 650.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Create Object Catalog", &open))
    {
        ImGui::End();
        return;
    }

    static ImGuiTextFilter filter;
    static bool parentToSelection = true;
    static bool closeAfterCreate = false;
    filter.Draw("Search name, class, or CID", -1.0f);
    const bool validSelection =
        selectedObjectIndex >= 0 && static_cast<std::size_t>(selectedObjectIndex) < document.Objects().size();
    ImGui::Checkbox("Parent new object to selection", &parentToSelection);
    ImGui::SameLine();
    ImGui::Checkbox("Close after creating", &closeAfterCreate);
    if (parentToSelection && !validSelection)
        ImGui::TextDisabled("Nothing is selected; objects will be created at the world root.");
    ImGui::TextDisabled(
        "Friendly names are shown first. Hover an object name or runtime class for its purpose, requirements, capabilities, and authoring support.");
    ImGui::Separator();

    static constexpr std::array categories{"Foundations & hierarchy",
                                           "Characters, enemies & encounters",
                                           "Effects, rendering & destruction",
                                           "Gameplay, interaction & progression",
                                           "Logic, dialog & animation",
                                           "World, paths & visibility",
                                           "Runtime systems"};
    const auto &types = workspace.ObjectTypes().Types();
    std::size_t totalVisible = 0;
    for (const auto &type : types)
    {
        if (!IsCatalogCid(type.cid))
            continue;
        const char *friendly = FriendlyName(type.cid);
        const std::string searchable =
            std::string(friendly ? friendly : type.name.c_str()) + " " + type.name + " CID " + std::to_string(type.cid);
        if (filter.PassFilter(searchable.c_str()))
            ++totalVisible;
    }
    if (totalVisible == 0)
    {
        ImGui::TextDisabled("No object types match this search.");
        ImGui::End();
        return;
    }
    for (const char *category : categories)
    {
        std::size_t visibleCount = 0;
        for (const auto &type : types)
        {
            if (!IsCatalogCid(type.cid))
                continue;
            if (std::string(CategoryForCid(type.cid)) != category)
                continue;
            const char *friendly = FriendlyName(type.cid);
            const std::string searchable = std::string(friendly ? friendly : type.name.c_str()) + " " + type.name +
                                           " CID " + std::to_string(type.cid);
            if (filter.PassFilter(searchable.c_str()))
                ++visibleCount;
        }
        if (visibleCount == 0)
            continue;
        const std::string header = std::string(category) + " (" + std::to_string(visibleCount) + ")";
        const auto headerFlags = filter.IsActive() ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        if (!ImGui::CollapsingHeader(header.c_str(), headerFlags))
            continue;
        if (ImGui::BeginTable(
                category, 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch, 0.56f);
            ImGui::TableSetupColumn("Technical", ImGuiTableColumnFlags_WidthStretch, 0.27f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 86.0f);
            for (const auto &type : types)
            {
                if (!IsCatalogCid(type.cid))
                    continue;
                if (std::string(CategoryForCid(type.cid)) != category)
                    continue;
                const char *friendly = FriendlyName(type.cid);
                const std::string friendlyLabel = friendly ? friendly : type.name;
                const std::string searchable = friendlyLabel + " " + type.name + " CID " + std::to_string(type.cid);
                if (!filter.PassFilter(searchable.c_str()))
                    continue;
                const char *unavailableReason = nullptr;
                const bool unavailable = IsUnavailable(type.cid, unavailableReason);
                ImGui::TableNextRow();
                ImGui::PushID(type.cid);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(friendlyLabel.c_str());
                DrawObjectTooltip(type, friendlyLabel.c_str(), unavailable, unavailableReason);
                if (unavailable)
                    ImGui::TextDisabled("%s", unavailableReason);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", BrxDocument::ClassName(type.cid));
                DrawObjectTooltip(type, friendlyLabel.c_str(), unavailable, unavailableReason);
                ImGui::TextDisabled("CID %d | %zu editable fields", type.cid, type.writableOptions.size());
                ImGui::TableSetColumnIndex(2);
                ImGui::BeginDisabled(unavailable);
                if (ImGui::Button("Create", ImVec2(-1.0f, 0.0f)))
                {
                    const std::int32_t parent = parentToSelection && validSelection ? selectedObjectIndex : -1;
                    const float *transform =
                        validSelection
                            ? document.Objects()[static_cast<std::size_t>(selectedObjectIndex)].worldTransform
                            : nullptr;
                    const auto result = CreateCatalogObject(document, type.cid, parent, transform);
                    status = result.message;
                    if (result.succeeded)
                    {
                        workspace.Refresh(document);
                        renderer.Upload(document);
                        renderer.SelectObject(result.primaryObjectIndex);
                        if (closeAfterCreate)
                            open = false;
                    }
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void DrawObjectUseGuidePanel(bool &open, EditorWorkspace &workspace, bool &openCreateObjectCatalog,
                             bool &openObjectOperations)
{
    if (!open)
        return;
    ImGui::SetNextWindowSize(ImVec2(1260.0f, 800.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("CID Object Guide", &open))
    {
        ImGui::End();
        return;
    }

    static ImGuiTextFilter filter;
    static std::int16_t selectedCid = 2;
    static bool showCategoryHeaders = true;
    ImGui::TextColored(ImVec4(.35f, 1.0f, .5f, 1.0f), "ProjectCane CID Object Encyclopedia");
    ImGui::TextWrapped(
        "Every authorable and runtime-support CID is documented here using its retail behavior. Choose an object to learn what it is, how the game uses it, how to build a complete setup, and what commonly goes wrong. Friendly names and labels are editor-only; exported levels continue to use runtime CIDs and OIDs.");
    filter.Draw("Search friendly name, runtime class, CID, category, or purpose", -190.0f);
    ImGui::SameLine();
    ImGui::Checkbox("Category headers", &showCategoryHeaders);
    ImGui::Separator();

    const auto &types = workspace.ObjectTypes().Types();
    ImGui::BeginChild("ObjectUseList", ImVec2(365.0f, -1.0f), true);
    std::size_t visibleCount = 0;
    const char *previousCategory = nullptr;
    for (const auto &type : types)
    {
        if (!IsGuideCid(type.cid))
            continue;
        const char *friendly = FriendlyName(type.cid);
        const std::string friendlyLabel = friendly ? friendly : type.name;
        const std::string haystack = friendlyLabel + " " + type.name + " CID " + std::to_string(type.cid) + " " +
                                     CategoryForCid(type.cid) + " " + ObjectSummary(type.cid) + " " +
                                     ObjectExample(type.cid);
        if (!filter.PassFilter(haystack.c_str()))
            continue;
        const char *category = CategoryForCid(type.cid);
        if (showCategoryHeaders && (previousCategory == nullptr || std::string(previousCategory) != category))
        {
            if (previousCategory != nullptr)
                ImGui::Spacing();
            ImGui::TextColored(ImVec4(.55f, .72f, .9f, 1.0f), "%s", category);
            ImGui::Separator();
            previousCategory = category;
        }
        ++visibleCount;
        const std::string label = friendlyLabel + "##use" + std::to_string(type.cid);
        if (ImGui::Selectable(label.c_str(), selectedCid == type.cid))
            selectedCid = type.cid;
        ImGui::SameLine();
        ImGui::TextDisabled("%s | %d", BrxDocument::ClassName(type.cid), type.cid);
    }
    if (visibleCount == 0)
        ImGui::TextDisabled("No object uses match this search.");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("ObjectUseDetails", ImVec2(0.0f, -1.0f), true);
    const ObjectTypeDescriptor *selected = workspace.ObjectTypes().Find(selectedCid);
    if (selected == nullptr || !IsGuideCid(selectedCid))
    {
        ImGui::TextDisabled("Select an object class from the list.");
    }
    else
    {
        const char *friendly = FriendlyName(selectedCid);
        const std::string friendlyLabel = friendly ? friendly : selected->name;
        ImGui::TextColored(ImVec4(.45f, .82f, 1.0f, 1.0f), "%s", friendlyLabel.c_str());
        ImGui::TextDisabled("Runtime class %s | CID %d | %s", BrxDocument::ClassName(selectedCid), selectedCid,
                            CategoryForCid(selectedCid));

        if (ImGui::BeginTabBar("CidGuideTabs"))
        {
            if (ImGui::BeginTabItem("Overview & example"))
            {
                ImGui::SeparatorText("What this object is");
                ImGui::TextWrapped("%s", ObjectSummary(selectedCid));
                ImGui::SeparatorText("Example use in a level");
                ImGui::TextWrapped("%s", ObjectExample(selectedCid));
                ImGui::SeparatorText("Identity rules");
                ImGui::BulletText(
                    "CID %d selects the runtime class and its behavior; changing the name does not change the class.",
                    selectedCid);
                ImGui::BulletText(
                    "OID identifies a particular object and is what hierarchy, Splice, and gameplay references resolve.");
                ImGui::BulletText(
                    "Editor display names are organizational labels only and are never substituted for runtime OIDs.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Authoring checklist"))
            {
                ImGui::SeparatorText("What a complete setup needs");
                ImGui::BulletText("A valid parent/hierarchy and OID context for this CID.");
                if (selected->hasAppearance)
                    ImGui::BulletText(
                        "Renderable geometry and materials, finite transforms and bounds, and correct VISMAP assignment when it should be visible.");
                if (selected->hasPhysics)
                    ImGui::BulletText(
                        "Valid collision surfaces, normals, edges/BSP, material flags, camera-group mapping, and intentional static or dynamic constraints.");
                if (selected->hasGameplay)
                    ImGui::BulletText(
                        "Compatible object references, filters, state, events or Splice callbacks, effects, audio, and lifecycle testing.");
                if (!selected->hasAppearance && !selected->hasPhysics && !selected->hasGameplay)
                    ImGui::BulletText(
                        "The loader/runtime infrastructure expected by this foundational or manager class; an empty record is not automatically useful.");
                ImGui::BulletText(
                    "All required children, contextual OIDs, and external assets from the retail class contract.");
                ImGui::SeparatorText("Verification before export");
                ImGui::BulletText("Run Level Validation and resolve serialization-critical errors.");
                ImGui::BulletText(
                    "Save and reopen the editor project, then rebuild and reopen the PCBRX to catch missing authored state.");
                ImGui::BulletText(
                    "Test the actual interaction in game, including reset, checkpoint, death, and level reload when relevant.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Editor support & cautions"))
            {
                ImGui::SeparatorText("Editor coverage");
                ImGui::Text("Transform/render foundation: %s", selected->hasAppearance ? "yes" : "no");
                ImGui::Text("Collision/physics foundation: %s", selected->hasPhysics ? "yes" : "no");
                ImGui::Text("Gameplay foundation: %s", selected->hasGameplay ? "yes" : "no");
                ImGui::Text("Writable properties: %zu", selected->writableOptions.size());
                ImGui::Text("Specialized inspector: %s", selected->hasSpecializedInspector ? "yes" : "no");

                const char *unavailableReason = nullptr;
                if (IsUnavailable(selectedCid, unavailableReason))
                {
                    ImGui::SeparatorText("Creation restriction");
                    ImGui::TextColored(ImVec4(1.0f, .68f, .25f, 1.0f), "Not directly creatable");
                    ImGui::TextWrapped("%s", unavailableReason);
                }
                else
                {
                    ImGui::SeparatorText("Authoring caution");
                    ImGui::TextWrapped(
                        "Creating the CID only creates its editor-supported initial record. Complex characters, encounters, managers, effects, and gameplay systems still need their required hierarchy and dependencies; the example tab describes the intended complete workflow.");
                }
                ImGui::SeparatorText("Common failure checks");
                ImGui::BulletText("Wrong CID or an OID that resolves to an incompatible class.");
                ImGui::BulletText(
                    "A required child, contextual lookup target, model, shader, sound, animation, or Splice module was omitted.");
                ImGui::BulletText(
                    "The object exists but has invalid bounds, zone assignment, collision topology, or an inactive event/state path.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Separator();
        if (ImGui::Button("Open Create Object Catalog"))
            openCreateObjectCatalog = true;
        ImGui::SameLine();
        if (ImGui::Button("Open Object Operations Reference"))
            openObjectOperations = true;
        ImGui::SameLine();
        if (ImGui::Button("Copy runtime identity"))
        {
            const std::string identity =
                std::string(BrxDocument::ClassName(selectedCid)) + " CID " + std::to_string(selectedCid);
            ImGui::SetClipboardText(identity.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::End();
}
