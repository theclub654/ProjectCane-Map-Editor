# CID authoring usability audit

Reviewed 2026-11-06 against the local ProjectCane source and the editor working tree. This is an authoring-contract and interface audit of all 148 CID entries, not a claim that every gameplay path has been executed or that the editor is bug-free. No build or game test was performed. The original per-CID recommendations below remain a planning ledger; the implementation section records what is now implemented.

## Implemented CID authoring improvements

- **148 explicit entries:** CidAuthoringGuide.h has one named entry for every enum value, with a class-specific next step, caution and source entry point. Internal/runtime classes are documented without opening unsupported creation paths. An entry is guidance, not a certificate that an object is valid.
- **Consistent names:** the catalog, hierarchy and other EditorClassName consumers share these display names. The runtime ClassName table, CIDs, OIDs, Splice property tokens and custom editor names are unchanged.
- **Related-record navigation:** Inspector > Find related objects & references shows recorded parent/direct children, an explicit PROXY runtime parent when available, authored scalar object-reference candidates and equal recorded Splice indices. Click to inspect. This uses the existing reference-type schema and does not pretend to resolve every class's contextual lookup.
- **Honest reference results:** OID -1 is treated as a sentinel; duplicate OID candidates remain visible; missing candidates are informational rather than automatically reported as broken. Dynamic command arguments, omitted defaults, fixed-OID requirements and runtime-generated objects are outside this browser's coverage.
- **Existing tools, directly reachable:** Tools for this object links supported classes to their existing studios and Inspector pages. Opening a tool neither applies a preset nor changes serialized values.
- **Focused property discovery:** object-specific SetUser search can show class-owned fields without inherited noise. A search includes inherited fields, the Include inherited settings checkbox restores the full list, and classes without their own SetUser fields fall back to inherited controls. Native structured payload editors and Advanced remain available.
- **Task categories:** traversal markers, switches, launchers, security, progression and camera/dialog/animation are separated in the catalog. The encyclopedia groups and sorts its entries for browsing.
- **Corrections found during review:** MS descriptions no longer describe a reusable model-set asset. BLIPG's guide explicitly identifies a runtime pool, matching its exclusion from ordinary catalog creation. Prior HND and BUTTON/VOLBTN corrections are preserved.

Source-only checks verified the complete ordered enum coverage, unique names, source citation existence, creator/preset agreement, nil/class filtering, read-only relationship navigation and Visual Studio header registration. These checks do not establish compiler success or in-game behavior. No automatic dependency repair, script rewrite, invented payload default or new gameplay behavior was added.

## Findings and priorities

1. **Explain the assembly.** A visible model, physical collision, interaction marker, script module and controller can be separate records. Add an inspector setup card showing resolved dependencies, their owners, lookup scope and missing pieces. Select linked objects directly. Start with HND, BUTTON, JLO, guards and pickups.
2. **Make creation capability honest.** Distinguish a complete starter, a record needing setup, a template-based assembly, an edit-existing resource and a runtime-only record. A successful generic Create operation does not establish a working encounter. Avoid automatically disabling classes until their actual creation path is checked.
3. **Reuse existing studios.** Extend GuidedObjectStudiosPanel, Character Studio, dialog, camera and visibility tools rather than introducing a separate window for every CID. Place the next useful action next to the selected object's missing dependency.
4. **Use progressive disclosure.** Show task-specific controls first, then inherited settings and raw fields. Keep technical class names and IDs available for compatibility and search. Make friendly names consistent between catalog and hierarchy.
5. **Preview impact before structural changes.** Explain which model, collision, child markers and shared script references will move or remain shared. Preview duplication and reparenting effects; do not silently rewrite arbitrary Splice object lookups.
6. **Use contextual spatial editing.** Show real source-defined radii, direction, curves, volumes and coordinate spaces. A visual preview must not imply collision, a gameplay trigger or a valid navigation/BSP structure has been created.
7. **Explain persistence and lifecycle.** Show when an object is removed after setup, cloned into a pool, gated by progress or dependent on a fixed identity. Provide test recipes covering reload, death/checkpoint reset and existing save state.
8. **Retain native limits.** Ordered children, fixed slots, payload types, mask semantics and lookup rules remain authoritative. Suggestions need a reviewable preview and must never manufacture unsupported retail representations.

## Confirmed correction

The catalog called CID 100 JLOC “Dialog Localization Helper.” ProjectCane's LoadJlocFromBrx reads candidate transforms and its JLOC implementation handles launcher selection and launch parameters (Sly1-Release/jlo.cpp:747, :854, :883). The catalog label was corrected to “Launcher Configuration.” No payload, IDs or runtime behavior changed.

## Existing editor surfaces reviewed

- CreateObjectCatalogPanel.cpp: friendly names, summaries, examples, category assignment, availability and authoring checklist. The checklist is largely based on inherited appearance/physics/gameplay capabilities; concrete dependencies should supplement it.
- ObjectTypeRegistry.cpp/.h and InspectorPanel.cpp: inherited capabilities and section routing. A specialized/details section may be schema-based; it does not necessarily mean a complete guided workflow exists.
- GuidedObjectStudiosPanel.cpp: existing breakable, security, mechanism, pickup, weapon, water and shadow tools.
- BrxDocument.cpp: EditorClassName versus catalog naming. These naming paths are separate and should eventually share one descriptor.
- EditorApplication.cpp: selection, hierarchy/transform and studio routing. Existing visual/collision movement handling must be preserved; the reported pole or hook behavior cannot be diagnosed for a specific map without its actual records and references.

## Complete CID review ledger

Every enum entry in Sly1-Release/spaprops.h is included, including internal/runtime classes. The source column is a lifecycle entry point for further review; inherited entries intentionally point to a base implementation. Detailed contracts also reside in the class vtables in basic.h and the corresponding class implementation. Suggestions are deliberately class-specific; they are not assertions that the listed workflow is currently broken.

| CID | Class | User-facing improvement | Source entry point |
| --- | --- | --- | --- |
| 0 | BASIC | Explain abstract base; offer a concrete class chooser instead of a blank object. | Sly1-Release/basic.h / spaprops.h |
| 1 | LO | Show events and shared script ownership together; link each callback to its module. | Sly1-Release/lo.cpp:14 |
| 2 | ALO | Group visual transform, attached models and animation; show local versus world coordinates. | Sly1-Release/alo.cpp:29 |
| 3 | SO | Show collision owner separately from model owner; preview visual and collision movement together. | Sly1-Release/so.cpp:37 |
| 4 | MS | Identify scene-root role and singleton expectations; direct users to world settings. | Sly1-Release/so.cpp:37 |
| 5 | PO | Explain inherited actor physics separately from character-specific behavior. | Sly1-Release/po.cpp:15 |
| 6 | STEP | Show foot/contact bindings and moving-platform dependencies before creation. | Sly1-Release/step.cpp:11 |
| 7 | JT | Offer a validated player assembly template rather than implying a bare record creates Sly. | Sly1-Release/jt.cpp:101 |
| 8 | STEPGUARD | Show patrol path, squad and animation bindings in one dependency checklist. | Sly1-Release/stepguard.cpp:24 |
| 9 | SMARTGUARD | Visualize detection, sneak and barrier relationships with selectable links. | Sly1-Release/smartguard.cpp:11 |
| 10 | GOMER | Display detection and abandonment volumes together with their owning guard. | Sly1-Release/gomer.cpp:12 |
| 11 | UBG | Explain patrol goals and world-state dependencies in a character setup card. | Sly1-Release/ub.cpp:11 |
| 12 | MBG | List required attack and movement animation bindings with missing-target navigation. | Sly1-Release/mb.cpp:33 |
| 13 | BHG | Present this variant beside its inherited guard settings; explain variant-specific animation dependencies. | Sly1-Release/mb.cpp:592 |
| 14 | MURRAY | Show fixed animation dependencies and distinguish character setup from ordinary props. | Sly1-Release/murray.cpp:11 |
| 15 | PUFFC | Link the controller to its PUFFER assembly and expose missing owner context. | Sly1-Release/stepguard.cpp:81 |
| 16 | CRFOD | Explain inherited guard behavior and require a known working encounter template for starter creation. | Sly1-Release/stepguard.cpp:24 |
| 17 | CRFODB | Show crusher encounter dependencies and linked animation requirements. | Sly1-Release/crusher.cpp:88 |
| 18 | CRFODK | Describe this encounter variant alongside its spawner; navigate between both. | Sly1-Release/stepguard.cpp:24 |
| 19 | RUBY | Provide an encounter dependency map before exposing advanced tuning. | Sly1-Release/ruby.cpp:66 |
| 20 | TANK | Show head, body and physics relationships; preview whole assembly selection. | Sly1-Release/tank.cpp:13 |
| 21 | ROV | Explain ROB ownership and why an unowned part may disappear after load. | Sly1-Release/rog.cpp:18 |
| 22 | TURRET | Show state machine and target-character relationships in the weapon setup. | Sly1-Release/turret.cpp:19 |
| 23 | VAULT | Expose world/progression gating alongside dialog dependencies. | Sly1-Release/pzo.cpp:20 |
| 24 | PUFFER | Show shoot-state machine and emitter dependencies together. | Sly1-Release/puffer.cpp:17 |
| 25 | SUV | Surface state machine and waypoint dependencies before offering a vehicle starter. | Sly1-Release/suv.cpp:26 |
| 26 | LGN | Show state machine, body transforms and sweep helper relationships. | Sly1-Release/lgn.cpp:55 |
| 27 | JETPACK | Display weapon, effects and state-machine dependencies as an assembly. | Sly1-Release/jetpack.cpp:77 |
| 28 | RYTHM | Show rhythm sequence ownership and linked encounter context. | Sly1-Release/rythm.cpp:36 |
| 29 | MGV | Group aiming state machine and weapon dependencies in an encounter card. | Sly1-Release/mgv.cpp:87 |
| 30 | RIPG | Mark as a runtime particle pool; direct creation to emitters. | Sly1-Release/rip.cpp:379 |
| 31 | WATER | Reuse Water Studio with a visible rendering-versus-collision explanation. | Sly1-Release/water.cpp:14 |
| 32 | BRK | Show explosion and collision dependencies; explain the base-class role. | Sly1-Release/break.cpp:14 |
| 33 | BREAK | Provide a break-response summary and pickup outcome preview. | Sly1-Release/break.cpp:873 |
| 34 | ALBRK | Link alarm ownership to the breakable; distinguish local damage from network effects. | Sly1-Release/break.cpp:873 |
| 35 | DARTGUN | Show dart template, pool and target references with go-to actions. | Sly1-Release/dartgun.cpp:15 |
| 36 | SWP | Explain script-driven sweep shape and duration with a path preview. | Sly1-Release/lgn.cpp:739 |
| 37 | FRAGILE | Show break-on-contact behavior and inherited collision settings together. | Sly1-Release/break.cpp:901 |
| 38 | ZAPBREAK | Describe actual contact damage behavior without implying unsupported electrical effects. | Sly1-Release/break.cpp:901 |
| 39 | BRKP | Show which breakable owns a piece; separate debris from a complete breakable. | Sly1-Release/so.cpp:37 |
| 40 | BUTTON | Preview mechanism type and ordered stop children; identify missing endpoints. | Sly1-Release/button.cpp:15 |
| 41 | VOLBTN | Show accepted pushers by resolved names; explain the empty-list fallback. | Sly1-Release/button.cpp:890 |
| 42 | JLOVOL | Link volume, launcher configuration and landing marker in one setup. | Sly1-Release/jlo.cpp:14 |
| 43 | BONE | Show skeleton hierarchy and collision role; explain that this record alone does not author skin weights. | Sly1-Release/so.cpp:37 |
| 44 | SPRIZE | Show collection animation and explosion dependencies alongside reward settings. | Sly1-Release/pzo.cpp:610 |
| 45 | SCPRIZE | Explain checkpoint/progress association using named entries and retained raw IDs. | Sly1-Release/pzo.cpp:888 |
| 46 | CLUE | Show persistent clue identity and saved-progress effects before duplication. | Sly1-Release/pzo.cpp:940 |
| 47 | ALARM | Display alarm listeners, state machine and reset behavior as a network. | Sly1-Release/alarm.cpp:15 |
| 48 | SENSOR | Preview detection extent and explain inherited sensor settings. | Sly1-Release/sensor.cpp:16 |
| 49 | LASEN | Show beam path, emitter and scorch dependencies; preview beam geometry. | Sly1-Release/sensor.cpp:288 |
| 50 | CAMSEN | Preview view/damage area and linked effects in Security Network. | Sly1-Release/sensor.cpp:1063 |
| 51 | PRSEN | Show sensor state and scrolling/effect dependencies together. | Sly1-Release/sensor.cpp:1597 |
| 52 | BARRIER | Link warp destination and barrier state; explain physical versus visual opening. | Sly1-Release/barrier.cpp:11 |
| 53 | IKH | Explain its contextual IK target role; navigate to the consuming rig. | Sly1-Release/so.cpp:37 |
| 54 | TZP | Show recovery target and destination relationships in hazard setup. | Sly1-Release/zap.cpp:13 |
| 55 | VOLZP | Preview actual sphere/BSP hazard volume and recovery behavior. | Sly1-Release/zap.cpp:156 |
| 56 | CHKPNT | Show checkpoint region, animation, state machine and progress association. | Sly1-Release/chkpnt.cpp:15 |
| 57 | CNVO | Preview conveyor direction separately from visible texture scrolling. | Sly1-Release/cnvo.cpp:9 |
| 58 | HBSK | Explain carry/hide constraints and show object dimensions in context. | Sly1-Release/hide.cpp:26 |
| 59 | BOMB | Show explosion payload, damage and timing together with a safe static preview. | Sly1-Release/bomb.cpp:15 |
| 60 | MISSILE | Distinguish launch-time target assignment from persistent authored references. | Sly1-Release/missile.cpp:8 |
| 61 | ACCMISS | Label acceleration separately from homing; preview initial direction and speed curve. | Sly1-Release/missile.cpp:218 |
| 62 | TARMISS | Explain guided/intercept versus ballistic phases; show target context. | Sly1-Release/missile.cpp:270 |
| 63 | MINE | Distinguish runtime player mine behavior from a freely placed static prop. | Sly1-Release/bomb.cpp:15 |
| 64 | FLY | Show landing paths and animation/frame dependencies. | Sly1-Release/fly.cpp:14 |
| 65 | RAT | Link rat holes and movement dependencies from the creature inspector. | Sly1-Release/rat.cpp:17 |
| 66 | ROH | Show ROB ownership and template/pool lifecycle instead of offering a complete enemy promise. | Sly1-Release/rog.cpp:485 |
| 67 | ROC | Show owner, emitter and state-machine links together. | Sly1-Release/rog.cpp:934 |
| 68 | ROST | Preview the spawn tunnel and first-point dependency. | Sly1-Release/rog.cpp:1186 |
| 69 | ROP | Show ROB ownership and contained key relationship. | Sly1-Release/rog.cpp:1291 |
| 70 | DART | Link pooled darts back to their dartgun and show launch direction. | Sly1-Release/dart.cpp:16 |
| 71 | JLO | Display volume, configuration, RWM and targeting dependencies as one launcher assembly. | Sly1-Release/jlo.cpp:120 |
| 72 | PUFFT | Show the owning PUFFER and its fire-point relationship. | Sly1-Release/so.cpp:37 |
| 73 | MRKV | Visualize the slab and its direction; explain the marker role. | Sly1-Release/mrkv.cpp:8 |
| 74 | CLKWORK | Show phase and weak-point relationships in an encounter overview. | Sly1-Release/clkwork.cpp:62 |
| 75 | CLKWRKDMG | Link the damage helper to the boss and HUD/progression context. | Sly1-Release/clkwork.cpp:220 |
| 76 | DECOY | Explain runtime decoy lifecycle and provide a reference-template path. | Sly1-Release/so.cpp:37 |
| 77 | BLIPG | Keep runtime particle groups out of ordinary scene creation. | Sly1-Release/blip.cpp:44 |
| 78 | CAMERA | Preview target and camera transform; distinguish this from camera-policy tunnels. | Sly1-Release/xform.cpp:979 |
| 79 | LBONE | Show debug-bone hierarchy; do not imply skin binding can be edited here. | Sly1-Release/alo.cpp:29 |
| 80 | EMITTER | Offer emitter presets only with verified assets; show missing sprite/curve dependencies. | Sly1-Release/emitter.cpp:18 |
| 81 | LIGHT | Use light-type-specific controls and a viewport range/direction preview. | Sly1-Release/light.cpp:22 |
| 82 | SCH | Explain this inherited ALO tag and avoid promising extra payload controls. | Sly1-Release/alo.cpp:29 |
| 83 | PROXY | Show shared source versus instance overrides; explain Make Unique restrictions before the action. | Sly1-Release/proxy.cpp:9 |
| 84 | SKY | Explain camera-follow behavior so a static viewport position is not misleading. | Sly1-Release/alo.cpp:29 |
| 85 | DPRIZE | Show collection target, state machine and effect dependencies. | Sly1-Release/coin.cpp:25 |
| 86 | CHARM | Explain runtime award behavior and point users toward supported pickup authoring. | Sly1-Release/coin.cpp:990 |
| 87 | LIFETKN | Show lifetime/collection rules and animation dependencies. | Sly1-Release/coin.cpp:1244 |
| 88 | COIN | Show lifetime, collection and saved-state context in Pickup Studio. | Sly1-Release/coin.cpp:1043 |
| 89 | KEY | Expose level-progress gating and key identity before duplication. | Sly1-Release/coin.cpp:1406 |
| 90 | GOLD | Explain timed-challenge availability and its world-state dependency. | Sly1-Release/coin.cpp:1478 |
| 91 | BOOST | Label explicitly as a vehicle boost and show its SUV context. | Sly1-Release/boost.cpp:14 |
| 92 | LOCK | Show lock animation and key-visual dependencies with direct navigation. | Sly1-Release/alo.cpp:29 |
| 93 | LOCKG | Present the ordered lock slots and progress association together. | Sly1-Release/alo.cpp:29 |
| 94 | TAIL | Preview the ordered child chain and show which child starts it. | Sly1-Release/tail.cpp:8 |
| 95 | ROB | Provide a parts/templates overview and explain inactive-part removal. | Sly1-Release/rog.cpp:1449 |
| 96 | FLASH | Describe detection-surface/BSP behavior; distinguish it from screen tint flashes. | Sly1-Release/flash.cpp:8 |
| 97 | DYSH | Show caster/helper/receiver relationships using the existing Shadow Studio. | Sly1-Release/dysh.cpp:31 |
| 98 | WAYPOINT | Show waypoint links and explain that the helper is consumed during setup. | Sly1-Release/waypoint.cpp:10 |
| 99 | TN | Use policy-specific forms and camera previews for tunnel behavior. | Sly1-Release/tn.cpp:42 |
| 100 | JLOC | Rename misleading dialog label; show launcher mode, candidate transforms and launch timing. | Sly1-Release/jlo.cpp:747 |
| 101 | DIALOG | Preview event order, anchor resolution and speaker references; retain exact event data. | Sly1-Release/dialog.cpp:41 |
| 102 | SPEAKER | Show idle state machine and dialog speaker association; explain setup-helper lifecycle. | Sly1-Release/speaker.cpp:10 |
| 103 | WM | Show world-specific references and state-machine ownership before changing IDs. | Sly1-Release/alo.cpp:29 |
| 104 | CRFODKSPAWN | Show spawn waves, templates and state-machine triggers together. | Sly1-Release/crusher.cpp:479 |
| 105 | HUBSEL | Explain hub-selection relationships and provide navigation to hub configuration. | Sly1-Release/alo.cpp:29 |
| 106 | HUBPROG | Explain the source-defined world identity derived from the object ID. | Sly1-Release/alo.cpp:29 |
| 107 | UNK11 | Keep internal integrity object out of ordinary creation; document its unsupported authoring role. | Sly1-Release/unk11.cpp:8 |
| 108 | SW | Expose world resources in world settings rather than ordinary object creation. | Sly1-Release/sw.cpp:104 |
| 109 | CM | Show camera-manager settings through camera tools; explain runtime singleton role. | Sly1-Release/cm.cpp:399 |
| 110 | SHAPE | Provide curve editing with explicit coordinate space and a reparent preview. | Sly1-Release/shape.cpp:8 |
| 111 | HSHAPE | Preview hiding path and linked camera tunnel. | Sly1-Release/hide.cpp:251 |
| 112 | PIPE | Explain local traversal distance versus world scale; preview climb endpoints. | Sly1-Release/pipe.cpp:18 |
| 113 | RAIL | Preview rail direction, slope and traversal properties. | Sly1-Release/rail.cpp:8 |
| 114 | LANDING | Show the path consumer and distinguish enemy landing behavior from player traversal. | Sly1-Release/shape.cpp:8 |
| 115 | XFM | Use a named marker role and orientation gizmo; show references to it. | Sly1-Release/xform.cpp:18 |
| 116 | WARP | Show destination transform and embedded animation entries together. | Sly1-Release/xform.cpp:18 |
| 117 | TARGET | Preview targeting radius and masks with resolved target relationships. | Sly1-Release/target.cpp:18 |
| 118 | HND | Show grab radius, parent physics and grab/release scripts; separate handhold from visible hook model. | Sly1-Release/hnd.cpp:8 |
| 119 | EXPL | Explain explosion-marker base role; steer users toward concrete effect types. | Sly1-Release/xform.cpp:18 |
| 120 | EXPLG | Show ordered nested effects and source-supported capacity before adding entries. | Sly1-Release/xform.cpp:18 |
| 121 | EXPLO | Explain base emitter role and show which concrete effect supplies behavior. | Sly1-Release/explo.cpp:8 |
| 122 | EXPLS | Preview particle effect settings and linked rendering/touch dependencies. | Sly1-Release/expls.cpp:9 |
| 123 | VOL | Show volume geometry and consumers; do not imply that every volume triggers events independently. | Sly1-Release/xform.cpp:18 |
| 124 | RATHOLE | Link the rat consumer and preview the real volume. | Sly1-Release/xform.cpp:18 |
| 125 | EXIT | Provide destination/warp and progression checklist; explain how to test the exported exit. | Sly1-Release/alo.cpp:29 |
| 126 | PNT | Present position-only editing; hide unsupported rotation/scale controls. | Sly1-Release/lo.cpp:14 |
| 127 | HPNT | Preview hiding point and linked camera tunnel. | Sly1-Release/hide.cpp:465 |
| 128 | JMT | Show launch target and ignored collision object with a trajectory suggestion preview. | Sly1-Release/lo.cpp:14 |
| 129 | SPIRE | Preview acquisition point and explain traversal use. | Sly1-Release/spire.cpp:8 |
| 130 | SCAN | Edit label text with an in-game display preview and underscore behavior explained. | Sly1-Release/lo.cpp:14 |
| 131 | ASEG | Show target bindings, tick-based timing and event timeline; validate references against the intended rig. | Sly1-Release/aseg.cpp:17 |
| 132 | ASEGBL | Mark runtime blend role and direct users to authored animation segments. | Sly1-Release/aseg.cpp:17 |
| 133 | VISZONE | Explain runtime zone shell; create/edit retail zones through VISMAP. | Sly1-Release/basic.h / spaprops.h |
| 134 | VISMAP | Reuse Visibility Studio; show BSP leaves, masks and one-hop neighbors without replacing them with invented box zones. | Sly1-Release/vis.cpp:10 |
| 135 | FRZG | Show resolved freeze members and source capacity; preview scope before regrouping. | Sly1-Release/lo.cpp:14 |
| 136 | SM | Provide a state/transition table and initial-state summary with linked animations/events. | Sly1-Release/lo.cpp:14 |
| 137 | SGG | Show squad membership and lookup scope using resolved guard names. | Sly1-Release/stepguard.cpp:3174 |
| 138 | PATHZONE | Preview navigation topology and connectivity; keep topology edits distinct from visual path curves. | Sly1-Release/lo.cpp:14 |
| 139 | RCHM | Explain animation-sample, tetrahedron and grid requirements; offer inspected template editing before any generation promise. | Sly1-Release/rchm.cpp:12 |
| 140 | RWM | Show fire/aim bindings, ammo templates and pool dependencies. | Sly1-Release/rwm.cpp:15 |
| 141 | WR | Preview ordered deformation operations and affected GLOBs. | Sly1-Release/wr.cpp:9 |
| 142 | KEYHOLE | Label as a screen iris effect rather than a gameplay key lock. | Sly1-Release/keyhole.cpp:20 |
| 143 | JSG | Present ordered choreography commands with per-command fields and target links. | Sly1-Release/jsg.cpp:16 |
| 144 | RYTHMSEQUENCE | Use measure/beat timeline with explicit encounter ownership. | Sly1-Release/rythm.cpp:329 |
| 145 | THNDFLASH | Label as thunder/screen tint effect and distinguish it from FLASH detection. | Sly1-Release/lo.cpp:14 |
| 146 | ASEGA | Explain runtime animation application; navigate to its source ASEG. | Sly1-Release/basic.h / spaprops.h |
| 147 | SMA | Explain runtime state-machine application; navigate to its source SM. | Sly1-Release/basic.h / spaprops.h |

## Suggested implementation sequence

**First:** shared object setup cards, consistent labels, resolved-reference navigation and truthful creation status. These improve most classes without changing retail payloads.

**Next:** assembly-aware selection and action previews; ordered mechanism/animation/state tables; spatial overlays inside existing tools. Add class rules only when the actual source lookup and lifecycle are recorded.

**Later:** validated starter assemblies and optional suggestions. Copy all required native resources and expose shared scripts explicitly. Complex bosses, RCHM and navigation/BSP generation require separate design and verification; do not ship generic automatic generators as complete game-ready setups.

## Acceptance checks for follow-up changes

- Selecting a CID explains its purpose, prerequisites and next action without requiring raw-ID knowledge.
- Required, optional, inherited and runtime-resolved dependencies are distinguished using source evidence.
- Display labels do not change serialization, EOPIDs, object IDs, child ordering or script syntax.
- Structural actions preserve unrelated data and report affected dependencies before applying.
- Existing advanced controls remain reachable. Empty or missing references retain their actual sentinel semantics.
- Source checks are followed by an explicitly approved build and representative in-game tests before release claims.
