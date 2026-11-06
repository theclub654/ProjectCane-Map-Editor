#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Editor-only authoring guidance. Never used to infer defaults or serialize data.
struct CidAuthoringGuide
{
    const char *name;
    const char *nextStep;
    const char *caution;
    const char *source;
};

inline constexpr std::array<CidAuthoringGuide, 148> kCidAuthoringGuides{{
    // CID 0: BASIC
    {"Runtime Object Foundation", "Choose a concrete CID instead of BASIC.", "BASIC supplies the vtable foundation, not a standalone BRX object constructor.", "Sly1-Release/basic.h / spaprops.h"},
    // CID 1: LO
    {"Level Object", "Review identity, parent and attached events before changing an OID.", "LO has no model transform; use its children and the object's reference fields to understand its role.", "Sly1-Release/lo.cpp :: LoadLoFromBrx"},
    // CID 2: ALO
    {"Renderable Object", "Check model ownership, transform space and visibility before editing appearance.", "An ALO's visible geometry and its gameplay owner's collision need not be the same record.", "Sly1-Release/alo.cpp :: LoadAloFromBrx"},
    // CID 3: SO
    {"Physical Object", "Compare visible geometry with both gameplay and camera collision.", "Fixed geometry and dynamic local geometry follow different runtime paths; use the existing object move operation rather than moving a model instance alone.", "Sly1-Release/so.cpp :: LoadSoFromBrx"},
    // CID 4: MS
    {"Merged Scene", "Edit the existing merged scene and inspect its contained resources.", "This is scene infrastructure; the catalog intentionally prevents a second MS.", "Sly1-Release/so.cpp :: LoadSoFromBrx"},
    // CID 5: PO
    {"Gameplay Actor Base", "Review inherited physical settings and playable registration before making an actor.", "A PO record is a foundation, not a complete character rig or encounter.", "Sly1-Release/po.cpp :: InitPo"},
    // CID 6: STEP
    {"Stepping Object", "Inspect support/contact and animation dependencies in the existing mechanism or character tools.", "STEP is inherited by several character classes; its presence does not mean the object is merely a moving platform.", "Sly1-Release/step.cpp :: InitStep"},
    // CID 7: JT
    {"Sly Player Rig", "Inspect the existing complete player hierarchy and its fixed identities.", "A bare JT lacks the required rig children and animation package. Direct creation is intentionally blocked.", "Sly1-Release/jt.cpp :: InitJt"},
    // CID 8: STEPGUARD
    {"Patrol Guard", "Review state animations, physics bindings and optional PATHZONE assignment.", "An unset PATHZONE is not automatically an error. Preserve the actual guard package rather than copying only its visible mesh.", "Sly1-Release/stepguard.cpp :: InitStepGuard"},
    // CID 9: SMARTGUARD
    {"Smart Guard", "Inspect flashlight, sneak-volume and barrier relationships with the guard.", "Inherited guard tuning alone does not recreate its class-specific helpers.", "Sly1-Release/smartguard.cpp :: InitSmartGuard"},
    // CID 10: GOMER
    {"Gomer Guard", "Inspect detection and abandonment volumes alongside guard animations.", "Review the actual configured volumes before changing patrol or awareness settings.", "Sly1-Release/gomer.cpp :: InitGomer"},
    // CID 11: UBG
    {"UBG Guard Variant", "Check the patrol-goal setup and world-state relationship.", "This variant inherits guard binding rules; preserve its own goal and animation package.", "Sly1-Release/ub.cpp :: InitUbg"},
    // CID 12: MBG
    {"MBG Guard Variant", "Review running blends and attack-animation bindings as one package.", "Copying the render model does not supply the attack animation table used by the class.", "Sly1-Release/mb.cpp :: InitMbg"},
    // CID 13: BHG
    {"BHG Guard Variant", "Inspect variant settings alongside inherited guard options.", "Keep its original animation and physics context; changing CID is not a character conversion.", "Sly1-Release/mb.cpp :: InitBhg"},
    // CID 14: MURRAY
    {"Murray Character", "Inspect Murray's animations and mission context before moving or cloning him.", "Post-load looks up fixed celebration, death and stun animations; a new mesh does not replace those dependencies.", "Sly1-Release/murray.cpp :: InitMurray"},
    // CID 15: PUFFC
    {"PUFFC Character", "Review this character together with its PUFFER encounter.", "Its own post-load behavior supplements the inherited guard setup.", "Sly1-Release/stepguard.cpp :: LoadStepGuardFromBrx"},
    // CID 16: CRFOD
    {"CRFOD Guard Variant", "Review inherited guard animations, physics and squad context.", "This uses the guard lifecycle; preserve its encounter-specific package and scripts.", "Sly1-Release/stepguard.cpp :: InitStepGuard"},
    // CID 17: CRFODB
    {"CRFODB Crusher Variant", "Inspect the crusher-specific post-load setup and guard package.", "Use a complete known-working hierarchy when transferring this encounter component.", "Sly1-Release/crusher.cpp :: InitCrfodb"},
    // CID 18: CRFODK
    {"CRFODK Guard Variant", "Inspect the guard package and any CRFODKSPAWN controller using it.", "A spawn controller and a character template are different records.", "Sly1-Release/stepguard.cpp :: InitStepGuard"},
    // CID 19: RUBY
    {"Ruby Encounter", "Review the rhythm encounter, attack animations and state machine together.", "Ruby resolves attack animations during loading and subscribes to encounter state; a bare actor is insufficient.", "Sly1-Release/ruby.cpp :: InitRuby"},
    // CID 20: TANK
    {"Tank Encounter", "Inspect body, head and physics-template relationships.", "Post-load clones configured physics and recaches body/head animation actions.", "Sly1-Release/tank.cpp :: InitTank"},
    // CID 21: ROV
    {"Rooftop ROV Component", "Locate the ROB controller that owns this encounter component.", "An unowned ROV is removed during post-load; its disappearance can be intentional runtime behavior.", "Sly1-Release/rog.cpp :: InitRov"},
    // CID 22: TURRET
    {"Turret", "Inspect the turret state machine and its target-character context.", "The runtime includes a contextual Murray lookup; placement alone is not a complete encounter.", "Sly1-Release/turret.cpp :: InitTurret"},
    // CID 23: VAULT
    {"Vault", "Review combo dialog, goad trigger and level-progress state.", "Saved level progress can change behavior even when the authored object has not changed.", "Sly1-Release/pzo.cpp :: InitVault"},
    // CID 24: PUFFER
    {"Puffer Encounter", "Inspect shoot and encounter state machines with its child resources.", "Keep attack helpers and controller state together when transferring the encounter.", "Sly1-Release/puffer.cpp :: InitPuffer"},
    // CID 25: SUV
    {"SUV Vehicle", "Review vehicle state machine, waypoint and pickup relationships.", "Runtime vehicle logic has its own waypoint lookup; moving a visible part is not moving the whole vehicle setup.", "Sly1-Release/suv.cpp :: InitSuv"},
    // CID 26: LGN
    {"LGN Encounter", "Inspect state machine, body transforms and sweep helper together.", "The class recaches body actions; preserve its animation and controller relationships.", "Sly1-Release/lgn.cpp :: InitLgn"},
    // CID 27: JETPACK
    {"Jetpack Encounter", "Review effect, reusable weapon manager and state machine links.", "A replacement model does not supply the weapon/effect setup.", "Sly1-Release/jetpack.cpp :: InitJetpack"},
    // CID 28: RYTHM
    {"Rhythm Encounter Object", "Review its encounter data with the associated rhythm sequence.", "Timing and encounter state are separate from the visible model transform.", "Sly1-Release/rythm.cpp :: InitRythm"},
    // CID 29: MGV
    {"MGV Encounter", "Inspect the aiming state machine and weapon assembly.", "Preserve its configured physics and animation context when transferring it.", "Sly1-Release/mgv.cpp :: InitMgv"},
    // CID 30: RIPG
    {"Runtime Model-Particle Pool", "Author model-particle settings on EMITTER instead.", "RIPG is allocated as a reusable runtime pool, not a standalone placed particle effect.", "Sly1-Release/rip.cpp :: InitRipg"},
    // CID 31: WATER
    {"Water Surface", "Use Water Studio to compare rendering, bounds and collision.", "The visible water surface alone does not prove the intended physical interaction is configured.", "Sly1-Release/water.cpp :: InitWater"},
    // CID 32: BRK
    {"Breakable Base", "Review break effects, collision and damage response together.", "Explosion lookup may be inferred when no explicit explosion OID is supplied.", "Sly1-Release/break.cpp :: InitBrk"},
    // CID 33: BREAK
    {"Standard Breakable", "Inspect reward and break-response settings in Breakable Studio.", "Test intact and broken states; changing only one visual part may leave the other state unchanged.", "Sly1-Release/break.cpp :: InitBreak"},
    // CID 34: ALBRK
    {"Alarm Breakable", "Inspect the breakable alongside its alarm controller.", "Local break effects and alarm-network behavior are separate relationships.", "Sly1-Release/break.cpp :: InitBreak"},
    // CID 35: DARTGUN
    {"Dartgun", "Inspect the child DART template, targeting bindings and state machine.", "Post-load builds a reusable pool from the dart template. A missing template is not repaired by changing the gun mesh.", "Sly1-Release/dartgun.cpp :: InitDartgun"},
    // CID 36: SWP
    {"Sweep Helper", "Review the scripted shape and duration alongside its encounter owner.", "This is an encounter sweep component, not a generic independent moving platform.", "Sly1-Release/lgn.cpp :: InitSwp"},
    // CID 37: FRAGILE
    {"Fragile Object", "Inspect contact break behavior, damage filters and effects.", "This inherits the breakable loader and needs its actual collision/effect context.", "Sly1-Release/break.cpp :: InitFragile"},
    // CID 38: ZAPBREAK
    {"Contact-Damage Breakable", "Review its break/contact response in the existing breakable controls.", "Do not infer an electrical visual effect from the technical class name ZAPBREAK.", "Sly1-Release/break.cpp :: InitFragile"},
    // CID 39: BRKP
    {"Breakable Piece", "Locate the breakable or effect assembly using this piece.", "A debris/piece record is not a complete breakable object with an intact state.", "Sly1-Release/basic.h / spaprops.h"},
    // CID 40: BUTTON
    {"Physical Switch", "Review operation type, direct-child stops and activation filters.", "Push uses PNT children, lever uses XFM children excluding TARGET, and hang uses HND. Do not count arbitrary nested descendants.", "Sly1-Release/button.cpp :: InitButton"},
    // CID 41: VOLBTN
    {"Volume Button", "Inspect accepted-object references, trigger shape and reset settings.", "No authored accepted OIDs enables the player fallback; unresolved authored OIDs do not enable that fallback.", "Sly1-Release/button.cpp :: InitVolbtn"},
    // CID 42: JLOVOL
    {"Launcher Activation Volume", "Review its JLOC configuration and landing-point lookup.", "This extends VOLBTN, but the launcher configuration and landing target remain separate records.", "Sly1-Release/jlo.cpp :: InitJlovol"},
    // CID 43: BONE
    {"Physics Bone", "Inspect the skeleton/physics hierarchy around the BONE.", "A BONE record does not itself define imported mesh weights; preserve the existing skin and rig bindings.", "Sly1-Release/so.cpp :: LoadSoFromBrx"},
    // CID 44: SPRIZE
    {"Static Prize Base", "Review collection animations, reward behavior and inferred explosion.", "ASEG OIDs are resolved in object context; retain the surrounding assembly.", "Sly1-Release/pzo.cpp :: InitSprize"},
    // CID 45: SCPRIZE
    {"Checkpoint Prize", "Inspect the progress/checkpoint association with collection settings.", "A progress identity is not an arbitrary interchangeable object name.", "Sly1-Release/pzo.cpp :: InitScprize"},
    // CID 46: CLUE
    {"Clue Bottle", "Review clue identity, render-frame children and saved clue state.", "Already collected clues can behave differently with an existing save. Test with the intended progress state.", "Sly1-Release/pzo.cpp :: InitClue"},
    // CID 47: ALARM
    {"Alarm Controller", "Inspect controlled breakables, state machine and reset behavior.", "The alarm network includes relationships beyond the selected visible object.", "Sly1-Release/alarm.cpp :: InitAlarm"},
    // CID 48: SENSOR
    {"Detector Base", "Review detection settings and the concrete detector implementation.", "A SENSOR foundation is not a complete laser or camera assembly by itself.", "Sly1-Release/sensor.cpp :: InitSensor"},
    // CID 49: LASEN
    {"Laser Detector", "Inspect beam shapes, emitter and damage/scorch resources.", "The loader/binder consumes helper shapes. Seeing them removed at runtime is not proof of lost authored data.", "Sly1-Release/sensor.cpp :: InitLasen"},
    // CID 50: CAMSEN
    {"Camera Detector", "Review detection geometry and damage/zap render helpers.", "Configured effect helpers and runtime fallbacks are separate from camera positioning.", "Sly1-Release/sensor.cpp :: InitCamsen"},
    // CID 51: PRSEN
    {"PRSEN Detector", "Review its initial sensor state and scrolling/effect setup.", "This has a class-specific post-load path; do not treat it as a generic physical pressure switch.", "Sly1-Release/sensor.cpp :: InitPrsen"},
    // CID 52: BARRIER
    {"Barrier", "Inspect warp/destination links and state-dependent behavior.", "A visible opening, collision change and destination lookup are not the same operation.", "Sly1-Release/barrier.cpp :: InitBarrier"},
    // CID 53: IKH
    {"IK Handle", "Locate the rig or animation that consumes this helper.", "This is an inverse-kinematics helper, not a visible model or a complete handhold interaction.", "Sly1-Release/so.cpp :: LoadSoFromBrx"},
    // CID 54: TZP
    {"Touch Hazard", "Review damage kind and recovery-target lookup.", "Recovery targets have class and lookup rules; a nearby marker is not automatically the destination.", "Sly1-Release/zap.cpp :: InitTzp"},
    // CID 55: VOLZP
    {"Volume Hazard", "Inspect the sphere/BSP volume and damage/recovery settings.", "Hazard detection uses actual volume data, not the size of the rendered model.", "Sly1-Release/zap.cpp :: InitVolzp"},
    // CID 56: CHKPNT
    {"Checkpoint", "Inspect trigger volumes, animation bindings, warp and state machine.", "Test activation, death and reload. A checkpoint's visible model is only one part of the respawn setup.", "Sly1-Release/chkpnt.cpp :: InitChkpnt"},
    // CID 57: CNVO
    {"Conveyor", "Review physical motion direction separately from shader scrolling.", "The serialized UV averages and physical behavior are distinct; visual scrolling does not prove collision movement.", "Sly1-Release/cnvo.cpp :: InitCnvo"},
    // CID 58: HBSK
    {"Hiding Basket", "Review hide/carry interaction and physical constraints.", "The full interaction depends on actor state and geometry, not just a visible basket mesh.", "Sly1-Release/hide.cpp :: InitHbsk"},
    // CID 59: BOMB
    {"Bomb", "Inspect damage/explosion configuration and physical behavior.", "Explosion templates and collision settings must remain compatible with the bomb assembly.", "Sly1-Release/bomb.cpp :: InitBomb"},
    // CID 60: MISSILE
    {"Missile", "Review the projectile's launch context and inherited bomb settings.", "Launch-time targeting is runtime behavior; a placed missile record is not automatically a functioning weapon.", "Sly1-Release/missile.cpp :: InitMissile"},
    // CID 61: ACCMISS
    {"Accelerating Missile", "Review initial direction and acceleration settings.", "Acceleration is not homing. Keep the launching weapon's configuration in view.", "Sly1-Release/missile.cpp :: InitAccmiss"},
    // CID 62: TARMISS
    {"Targeting Missile", "Review target prediction and the guided/ballistic phases.", "This is a different behavior from the accelerating missile; do not swap CIDs as a visual replacement.", "Sly1-Release/missile.cpp :: InitTarmiss"},
    // CID 63: MINE
    {"Mine", "Review the bomb payload and the player/weapon deployment context.", "Runtime deployment can differ from simply placing the source object in a level.", "Sly1-Release/bomb.cpp :: InitBomb"},
    // CID 64: FLY
    {"Flying Creature", "Inspect landing resources and the configured render-frame children.", "LoadFlyFromBrx uses fixed render OID ranges. Preserve them when transferring the creature.", "Sly1-Release/fly.cpp :: InitFly"},
    // CID 65: RAT
    {"Rat", "Review rat-hole relationships, collision and effects.", "A RATHOLE volume and a RAT are separate objects; inspect the consumer relationship.", "Sly1-Release/rat.cpp :: InitRat"},
    // CID 66: ROH
    {"Rooftop Harvester Template", "Locate the ROB encounter using this template.", "Post-load removes the template from the active world for runtime use; that is not ordinary prop behavior.", "Sly1-Release/rog.cpp :: InitRoh"},
    // CID 67: ROC
    {"Rooftop Collectible Component", "Review ROB context, bubble emitter and state machine.", "This encounter component uses its own pool/controller lifecycle.", "Sly1-Release/rog.cpp :: InitRoc"},
    // CID 68: ROST
    {"Rooftop Spawn Station", "Inspect its state machine and first direct PNT spawn-tunnel marker.", "Child role and order matter; a visible station model alone does not supply the spawn path.", "Sly1-Release/rog.cpp :: InitRost"},
    // CID 69: ROP
    {"Rooftop Prize Component", "Locate the owning ROB and any contained key.", "An unowned component is removed during post-load; inspect encounter ownership first.", "Sly1-Release/rog.cpp :: InitRop"},
    // CID 70: DART
    {"Dart Projectile", "Locate the dartgun/template pool that launches this DART.", "A pooled projectile is not a persistent static prop; retain its inherited bomb/effect data.", "Sly1-Release/dart.cpp :: InitDart"},
    // CID 71: JLO
    {"Launcher Controller", "Inspect RWM, initial JLOVOL and state-machine relationships.", "The controller, activation volumes and target configurations form an assembly.", "Sly1-Release/jlo.cpp :: InitJlo"},
    // CID 72: PUFFT
    {"Puffer Fire Point", "Inspect this point together with the PUFFER encounter.", "Post-load finds a PUFFER and contributes to its fire-point count; this is not an independent generic turret.", "Sly1-Release/puffer.cpp :: PostPufftLoad"},
    // CID 73: MRKV
    {"MRKV Slab Marker", "Review slab orientation and the consuming gameplay code.", "Treat the oriented marker separately from visible geometry and collision ownership.", "Sly1-Release/mrkv.cpp :: InitMrkv"},
    // CID 74: CLKWORK
    {"Clockwerk Encounter", "Inspect phase animation blends and weak-point relationships.", "This is a complete encounter package; moving or importing a mesh does not supply its phase logic.", "Sly1-Release/clkwork.cpp :: InitClkWork"},
    // CID 75: CLKWRKDMG
    {"Clockwerk Damage Helper", "Locate its boss and inspect damage/HUD context.", "Keep the helper with its encounter rather than using it as a generic damage volume.", "Sly1-Release/clkwork.cpp :: InitClkwrkDmg"},
    // CID 76: DECOY
    {"Decoy Template", "Review its player-powerup deployment and runtime lifecycle.", "The authored template is removed after loading; it is not an ordinary permanently placed actor.", "Sly1-Release/decoy.cpp :: PostDecoyLoad"},
    // CID 77: BLIPG
    {"Runtime Sprite-Particle Pool", "Author sprite-particle behavior through EMITTER.", "BLIPG is created and managed by the runtime pool rather than the ordinary catalog.", "Sly1-Release/blip.cpp :: InitBlipg"},
    // CID 78: CAMERA
    {"Authored Camera", "Review camera frame and target lookup in the camera controls.", "This is an authored shot, distinct from the single runtime CM manager and TN policy volumes.", "Sly1-Release/xform.cpp :: InitCamera"},
    // CID 79: LBONE
    {"Debug Bone", "Inspect the ALO hierarchy and debug-bone role.", "LBONE is not a replacement for skin-weight or skeleton-palette authoring.", "Sly1-Release/alo.cpp :: LoadAloFromBrx"},
    // CID 80: EMITTER
    {"Particle Emitter", "Review particle type, curves and reference/shape bindings.", "Different emission modes need different resources. A preview is not a guarantee of equivalent runtime behavior.", "Sly1-Release/emitter.cpp :: InitEmitter"},
    // CID 81: LIGHT
    {"Light", "Choose light settings appropriate to the light type and preview its placement.", "Lighting a model and moving that model's collision are unrelated operations.", "Sly1-Release/light.cpp :: InitLight"},
    // CID 82: SCH
    {"SCH Render Tag", "Inspect its inherited ALO resources and hierarchy.", "SCH uses inherited object behavior; do not assume a separate class-owned payload or standalone tool is required.", "Sly1-Release/alo.cpp :: LoadAloFromBrx"},
    // CID 83: PROXY
    {"Reusable Object Instance", "Use the existing source/linked-instance workflow and inspect shared ownership.", "The PROXY wrapper is removed/reparented during runtime setup. Make Unique restrictions protect relationships that cannot be safely detached.", "Sly1-Release/proxy.cpp :: InitProxy"},
    // CID 84: SKY
    {"Sky", "Review camera-follow behavior alongside the sky geometry.", "Runtime sky placement follows the camera; its authored transform is not a promise of a fixed world-space prop.", "Sly1-Release/sky.cpp :: PostSkyLoad"},
    // CID 85: DPRIZE
    {"Dynamic Prize Base", "Inspect the TARGET child, state machine and collection effects.", "Prize physics/collection is managed by this class rather than ordinary static-prop collision.", "Sly1-Release/coin.cpp :: InitDprize"},
    // CID 86: CHARM
    {"Charm Award", "Review the award behavior and inherited dynamic-prize resources.", "A charm award is not interchangeable with a coin merely because both derive from DPRIZE.", "Sly1-Release/coin.cpp :: InitCharm"},
    // CID 87: LIFETKN
    {"Life Token", "Review collection settings, lifetime and required prize resources.", "Test with the actual player and save state rather than only the viewport model.", "Sly1-Release/coin.cpp :: InitLifeTkn"},
    // CID 88: COIN
    {"Coin", "Review collection radius, lifetime and bounce/award behavior.", "Preserve the class's prize resources; physical SO collision is not its whole collection system.", "Sly1-Release/coin.cpp :: InitCoin"},
    // CID 89: KEY
    {"Key", "Review level-progress gating before duplicating or changing identity.", "The key can be removed based on saved level flags during initialization.", "Sly1-Release/coin.cpp :: InitKey"},
    // CID 90: GOLD
    {"Timed-Challenge Gold", "Review challenge state and inherited prize configuration.", "Gold availability depends on world/timed-challenge state, not only its existence in the file.", "Sly1-Release/coin.cpp :: InitGold"},
    // CID 91: BOOST
    {"Vehicle Boost", "Inspect the SUV interaction and dynamic-prize resources.", "This is a vehicle pickup, not a generic Sly speed boost.", "Sly1-Release/boost.cpp :: InitBoost"},
    // CID 92: LOCK
    {"Lock", "Inspect the state machine and key-visual resources.", "Load/post-load uses contextual assets; the lock model alone does not define the full progression interaction.", "Sly1-Release/pzo.cpp :: LoadLockFromBrx"},
    // CID 93: LOCKG
    {"Lock Group", "Review ordered lock references and saved-progress association.", "Do not treat its slots or progress identity as interchangeable arbitrary OIDs.", "Sly1-Release/pzo.cpp :: LoadLockgFromBrx"},
    // CID 94: TAIL
    {"Tail Chain", "Inspect the direct TAIL chain beginning at the root's first matching child.", "Sibling order and chain topology affect the solver; a collection of unrelated tail meshes is not a valid chain.", "Sly1-Release/tail.cpp :: InitTail"},
    // CID 95: ROB
    {"Rooftop Robbery Controller", "Inspect selected encounter kind, templates, stations and prize flow.", "Only the current ROB kind remains active; inactive encounter components can be removed intentionally.", "Sly1-Release/rog.cpp :: InitRob"},
    // CID 96: FLASH
    {"Guard Flashlight Volume", "Inspect SMARTGUARD ownership and the local TBSP beam.", "FLASH is a detection volume with render data, not a screen-flash effect. Use THNDFLASH for screen tint.", "Sly1-Release/flash.cpp :: InitFlash"},
    // CID 97: DYSH
    {"Dynamic Shadow Helper", "Inspect caster OID, projection shader and receiver flags in Shadow Studio.", "A DYSH mesh alone does not configure its caster or enable receiving on world geometry.", "Sly1-Release/dysh.cpp :: InitDysh"},
    // CID 98: WAYPOINT
    {"Guard Waypoint", "Review synchronization and state-machine routes in guard context.", "The waypoint helper is removed from the active world after setup; negative pause values have intentional runtime meaning.", "Sly1-Release/waypoint.cpp :: InitWaypoint"},
    // CID 99: TN
    {"Camera Tunnel", "Choose the intended policy and inspect the trigger and path in TN tools.", "An ordinary CAMERA and a TN policy trigger have different payloads and activation behavior.", "Sly1-Release/tn.cpp :: InitTn"},
    // CID 100: JLOC
    {"Launcher Configuration", "Review candidate XFM targets, target-selection mode and firing parameters.", "This has nothing to do with dialog localization. JLOC is removed after gathering setup data.", "Sly1-Release/jlo.cpp :: InitJloc"},
    // CID 101: DIALOG
    {"Dialog Sequence", "Review event order, speakers, audio and played-state anchor.", "A nil played-state anchor uses the dialog's own OID; changing identity can change progress tracking.", "Sly1-Release/dialog.cpp :: InitDialog"},
    // CID 102: SPEAKER
    {"Dialog Speaker", "Review voice/presentation and optional idle state machine.", "The setup helper is removed after post-load. Keep DIALOG references consistent.", "Sly1-Release/speaker.cpp :: InitSpeaker"},
    // CID 103: WM
    {"World Map Controller", "Inspect the fixed animation/model/cursor relationships.", "WM is the in-game map controller, not a weapon manager or ordinary world prop.", "Sly1-Release/wm.cpp :: BindWm"},
    // CID 104: CRFODKSPAWN
    {"CRFODK Spawn Controller", "Review wave/template and state-machine relationships.", "A spawner does not substitute for the complete guard package it uses.", "Sly1-Release/crusher.cpp :: InitCrfodkSpawn"},
    // CID 105: HUBSEL
    {"Hub Selection Controller", "Inspect hideout selection animations and state-machine dependencies.", "Use the matching world-flow assembly rather than treating this as a generic menu item.", "Sly1-Release/hubsel.cpp :: PostHubSelLoad"},
    // CID 106: HUBPROG
    {"Hub Progress Controller", "Review the source-defined world identity before changing OID.", "Post-load derives world index from OID minus 1300 and accepts 1301 through 1305.", "Sly1-Release/hubprog.cpp :: PostHubProgLoad"},
    // CID 107: UNK11
    {"Internal Runtime Integrity", "Leave this infrastructure out of level-authoring workflows.", "The editor intentionally excludes UNK11 from ordinary creation.", "Sly1-Release/unk11.cpp :: InitUnk11"},
    // CID 108: SW
    {"Scene World Container", "Use Scene World tools for persistent world resources and defaults.", "SW is represented by the world container, not a standalone child record.", "Sly1-Release/sw.cpp :: InitSw"},
    // CID 109: CM
    {"Runtime Camera Manager", "Use persistent world camera defaults or an authored CAMERA/TN.", "The game creates its CM manager; an additional empty CM is not a supported authored shot.", "Sly1-Release/cm.cpp :: InitCm"},
    // CID 110: SHAPE
    {"General Curve Path", "Inspect curve type, coordinates and the object that consumes the path.", "Reparenting converts the curve between parent spaces; this is not simply moving a rendered model.", "Sly1-Release/shape.cpp :: InitShape"},
    // CID 111: HSHAPE
    {"Hiding Path", "Inspect path geometry and optional TN hiding-camera link.", "An unset TN is not necessarily an error. Keep camera and hiding behavior distinct.", "Sly1-Release/hide.cpp :: InitHshape"},
    // CID 112: PIPE
    {"Climbable Pipe", "Review climb endpoints, curve direction and optional rope link.", "Traversal progresses along local curve distance; changing parent scale does not promise proportionally longer climb duration.", "Sly1-Release/pipe.cpp :: InitPipe"},
    // CID 113: RAIL
    {"Traversal Rail", "Review balance/run properties, path direction and optional rope.", "A visible rail model and its traversal curve are separate resources.", "Sly1-Release/rail.cpp :: InitRail"},
    // CID 114: LANDING
    {"Enemy Landing Path", "Inspect the FLY or other class consuming this curve.", "LANDING is a path helper, not a player handhold or generic collision strip.", "Sly1-Release/shape.cpp :: InitShape"},
    // CID 115: XFM
    {"Transform Marker", "Inspect position/orientation and references using this XFM.", "XFM supplies a marker transform, not a visible model or physical collision payload.", "Sly1-Release/xform.cpp :: InitXfm"},
    // CID 116: WARP
    {"Warp Destination", "Review arrival orientation and embedded animations.", "A WARP is a destination helper; it does not itself create a level-exit trigger.", "Sly1-Release/xform.cpp :: InitXfm"},
    // CID 117: TARGET
    {"Target Marker", "Review category mask, target radius and hit-testing settings.", "Being a target does not automatically make a marker a handhold or a visible gameplay prop.", "Sly1-Release/target.cpp :: InitTarget"},
    // CID 118: HND
    {"Grab Handhold", "Inspect contact radius, hang offset, parent and grab/release scripts.", "TARGET contact radius and HND hook-surface radius serve different purposes; HND alone supplies no visible hook mesh.", "Sly1-Release/hnd.cpp :: InitHnd"},
    // CID 119: EXPL
    {"Explosion Marker Base", "Inspect the concrete EXPLG/EXPLS effect that uses this origin.", "The base EXPL vtable has no explode callback; a plain marker alone does not produce particles.", "Sly1-Release/xform.cpp :: InitXfm"},
    // CID 120: EXPLG
    {"Explosion Group", "Review embedded effects and their serialized order.", "Preserve the source-supported group types and capacity rather than adding arbitrary world children.", "Sly1-Release/xform.cpp :: InitXfm"},
    // CID 121: EXPLO
    {"Explosion Emitter Base", "Inspect curve/reference bindings and the derived effect implementation.", "EXPLO is a base for concrete explosion behavior; do not assume every base record visibly explodes.", "Sly1-Release/explo.cpp :: InitExplo"},
    // CID 122: EXPLS
    {"Particle Explosion", "Review render, touch and reference resources with particle settings.", "Effect origins and referenced geometry can be separate; inspect bindings before changing the visible model.", "Sly1-Release/expls.cpp :: InitExpls"},
    // CID 123: VOL
    {"Volume Helper", "Inspect the actual BSP volume and the class that queries it.", "A VOL has no automatic trigger action merely because it encloses an area.", "Sly1-Release/xform.cpp :: InitXfm"},
    // CID 124: RATHOLE
    {"Rat-Hole Volume", "Inspect the RAT interaction and native volume geometry.", "This is not an independent rat-spawner definition just because of its name.", "Sly1-Release/xform.cpp :: InitXfm"},
    // CID 125: EXIT
    {"Level Exit", "Review destination names, warp IDs, trigger volume and embedded animations.", "Test the actual transition and progress conditions; a viewport preview cannot verify level loading.", "Sly1-Release/xform.cpp :: LoadExitFromBrx"},
    // CID 126: PNT
    {"Point Marker", "Edit position and inspect the object that consumes this point.", "PNT serializes position only. Rotation and scale are not independent native point settings.", "Sly1-Release/pnt.cpp :: LoadPntFromBrx"},
    // CID 127: HPNT
    {"Hide Point", "Review position, hiding settings and optional TN camera.", "This is a point helper; visual geometry and camera behavior are configured separately.", "Sly1-Release/hide.cpp :: InitHpnt"},
    // CID 128: JMT
    {"Jump Marker", "Review target position and optional ignored SO reference.", "The ignored object binding uses contextual lookup; proximity alone does not establish it.", "Sly1-Release/pnt.cpp :: LoadPntFromBrx"},
    // CID 129: SPIRE
    {"Ninja Spire Point", "Review acquisition limits and jump-target-history setting.", "SPIRE is a position-only PNT-derived marker, not a curve path or a visible spire mesh.", "Sly1-Release/spire.cpp :: InitSpire"},
    // CID 130: SCAN
    {"Binocular Scan Label", "Edit the label and its marker position together.", "The runtime display replaces underscores with spaces; keep this separate from object identity.", "Sly1-Release/binoc.cpp :: LoadScanFromBrx"},
    // CID 131: ASEG
    {"Animation Segment", "Review root/target bindings, channels and event timing.", "Native key timing uses 60 Hz ticks. ASEGA is the live application, not another authored segment.", "Sly1-Release/aseg.cpp :: InitAseg"},
    // CID 132: ASEGBL
    {"Runtime Animation Blend", "Edit source ASEGs and blend bindings instead.", "ASEGBL is an in-memory blend product with no ordinary authored world-object payload.", "Sly1-Release/aseg.cpp :: InitAseg"},
    // CID 133: VISZONE
    {"Runtime Visibility Zone Shell", "Use Visibility Studio and the VISMAP structure.", "Standalone VISZONE records do not create authored spatial zones.", "Sly1-Release/basic.h / spaprops.h"},
    // CID 134: VISMAP
    {"Visibility Map", "Inspect BSP split planes, leaf masks and one-hop adjacency together.", "Preserve native mask and BSP semantics; a drawn box is not a replacement for the runtime representation.", "Sly1-Release/vis.cpp :: InitVismap"},
    // CID 135: FRZG
    {"Freeze Group", "Review OID members in the group's parent context.", "Post-load resolves child ALOs relative to its parent and merges freeze roots; inspect scope before regrouping.", "Sly1-Release/frzg.cpp :: PostFrzgLoad"},
    // CID 136: SM
    {"State Machine Definition", "Review initial state, ordered state/transition records and animation bindings.", "Runtime SMA stores live state; preserve that distinction when editing the SM definition.", "Sly1-Release/sm.cpp :: LoadSmFromBrx"},
    // CID 137: SGG
    {"Guard Squad Group", "Review member references and synchronization context.", "Members are resolved by contextual nearest lookup, not solely by matching the first same-OID object.", "Sly1-Release/stepguard.cpp :: InitSgg"},
    // CID 138: PATHZONE
    {"Navigation Zone", "Inspect convex graph connectivity and the authored BSP/topology.", "This is structured navigation data, not an ordinary SHAPE curve or a visibility zone.", "Sly1-Release/path.cpp :: LoadPathZoneFromBrx"},
    // CID 139: RCHM
    {"Reach Pose Map", "Inspect host/touch bindings and all 24 animation samples.", "The lattice, tetrahedra, grid and category BSP form one structure. Edit a validated map rather than creating an empty replacement.", "Sly1-Release/rchm.cpp :: InitRchm"},
    // CID 140: RWM
    {"Reusable Weapon Manager", "Review fire/aim bindings, ammo templates and pool configuration.", "Target context and pooled ammunition are part of the weapon assembly, not just its visible muzzle.", "Sly1-Release/rwm.cpp :: InitRwm"},
    // CID 141: WR
    {"Geometry Deformer", "Review ordered operations and the GLOBs referencing the WR.", "This changes geometry deformation; it is not an independent collision or navigation authoring tool.", "Sly1-Release/wr.cpp :: InitWr"},
    // CID 142: KEYHOLE
    {"Screen Iris Geometry", "Inspect screen-space points and the five triangle layers.", "KEYHOLE is a transition iris mesh, not a key/lock gameplay interaction.", "Sly1-Release/keyhole.cpp :: InitKeyhole"},
    // CID 143: JSG
    {"Player Choreography", "Review command order, context changes and each target reference.", "Synchronous and asynchronous commands change sequencing; preserve native command arguments and lookup scope.", "Sly1-Release/jsg.cpp :: InitJsg"},
    // CID 144: RYTHMSEQUENCE
    {"Rhythm Sequence", "Review measure/beat ordering and the linked Ruby encounter.", "Post-load finds Ruby for its clock/sequence context; a standalone sequence is not an independent music player.", "Sly1-Release/rythm.cpp :: InitRythmSequence"},
    // CID 145: THNDFLASH
    {"Thunder Screen Flash", "Review tint, layer and fade/hold timing in the existing preview.", "This is a screen effect, distinct from FLASH's guard-detection volume.", "Sly1-Release/thndflash.cpp :: RenderThndFlash"},
    // CID 146: ASEGA
    {"Runtime Animation Application", "Edit the source ASEG and preview its application.", "ASEGA is created at runtime with a root, time and speed; it has no standalone world-object loader.", "Sly1-Release/basic.h / spaprops.h"},
    // CID 147: SMA
    {"Runtime State-Machine Application", "Edit the source SM definition and its bindings.", "SMA is created at runtime when an SM is applied; live state is not authored as a separate world object.", "Sly1-Release/basic.h / spaprops.h"},
}};

inline const CidAuthoringGuide *FindCidAuthoringGuide(const std::int16_t cid)
{
    return cid >= 0 && static_cast<std::size_t>(cid) < kCidAuthoringGuides.size()
        ? &kCidAuthoringGuides[static_cast<std::size_t>(cid)] : nullptr;
}
