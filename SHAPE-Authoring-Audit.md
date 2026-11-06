# SHAPE authoring audit

Source review and implementation: 2026-11-06. No build or game execution was performed.

## Native contract

- SHAPE (110) derives from LO, not ALO. Its payload is a curve kind followed by curve data and options; it has no native object transform. Source: shape.h; shape.cpp, LoadShapeFromBrx.
- HSHAPE (111), PIPE (112), RAIL (113) and LANDING (114) inherit that curve payload. A PIPE already contains its basic climbing curve; it does not require a second SHAPE child.
- Linear and cubic curves store U8 point counts and authored parameters. Cubic curves also store incoming/outgoing derivatives. Source: crvl.cpp, LoadCrvlFromBrx; crvc.cpp, LoadCrvcFromBrx.
- Points are in the parent coordinate space. SetShapeParent converts points between the old/new parent spaces. ConvertCrvc applies translation to points, but rotation/scale only to derivatives. It does not rewrite authored parameters.
- Runtime loaders measure the curve from its authored data. The existing editor keeps parameter, point and derivative arrays in BrxShapePathRecord and serializes those arrays through WriteShapeCurve. No synthetic SHAPE matrix should be substituted for those points.

## Consumers checked

| Consumer | Source-confirmed relationship | Authoring implication |
| --- | --- | --- |
| HSHAPE | hide.cpp, BindHshape and hiding-position queries | Curve plus class-specific hide limits and optional TN binding. |
| PIPE | pipe.cpp; steppipe.cpp | Own curve, facing settings and optional rope. Local traversal distance is distinct from model scale. |
| RAIL | rail.cpp, PostRailLoad and traversal properties | Own curve plus movement/balance settings and optional rope. |
| LANDING | fly.cpp, landing selection; basic.h VTLANDING | Enemy landing path, not a player climbing path or collision strip. |
| EMITTER | emitter.cpp, BindEmitter | Its SHAPE reference supplies a curve and the curve's parent reference space. |
| LASEN | sensor.cpp, BindLasen | Reads linear child curves for beams, with a source-defined beam capacity. Do not assume every cubic path is a valid beam. |
| TZP | zap.cpp, recovery-target collection/query | Can use SHAPE recovery targets. A drawn path does not independently create a hazard. |
| JSG | jsg.cpp, SHAPE movement-target branch | Uses the curve in choreography; command context and sequencing still matter. |
| EXPLO | explo.cpp, BindExplo; EditorOptions.cpp reference contract | Its similarly named oidShape reference is an XFM origin, not a copied SHAPE curve. |
| TN and inline emitter curves | tn.cpp; emitter.cpp; explo.cpp loaders | Some systems own inline curve data. Those are not automatically additional hierarchy SHAPE objects. |

## Concrete editor gaps addressed

1. Whole-curve mode was forced to a position-only gizmo. It now supports W/E/R. Single-point mode remains position-only.
2. Whole-curve dragging previously applied translation only. The new transform applies parent-conjugated world deltas to points and the linear delta to both cubic derivative arrays, always from the drag-start snapshot.
3. Generic object transforms could write a synthetic SHAPE matrix instead of its curve. The SHAPE branch now transforms native curve data and returns without authoring an object matrix. The Transform Inspector routes SHAPE-derived objects to the path editor.
4. Direct point dragging did not enforce selection locks in its own entry path. Locked or linked PROXY curves now reject that edit; the path controls also respect the lock.
5. New cursor placement explicitly participates in the existing document-history transaction mechanism. Otherwise a one-click viewport edit could be missed by its interaction filter.

## User-facing workflow

Select the SHAPE/PIPE/RAIL/HSHAPE/LANDING and open its object-specific settings. Choose Whole curve, then Move path, Rotate path or Scale path, or use W/E/R in the viewport. Choose Path center, First point or Selected point as the pivot. Local gizmo axes follow the parent basis.

Place pivot at cursor arms a one-shot placement operation: click gameplay collision in the viewport. The entire curve translates to put the chosen pivot at the surface hit. Existing path-grid snapping applies; camera-only collision is ignored. Esc or changing selection cancels. With no hit, the curve is unchanged and placement remains armed.

Precise whole-path transform accepts a world offset, world rotation in degrees and positive world-axis scale factors. Apply transforms once and resets the inputs after success. Frame complete path frames its control-point extent. Selecting a control point directly switches to point-edit mode.

Transforms stage all points/derivatives and reject non-finite results without partially updating the curve. They preserve parameters, point counts, curve kind and closed-seam records. They do not automatically rescale traversal limits, rotate PIPE facing settings, change bindings or move visible models/collision. Linked PROXY curves still require the existing Make unique workflow.

## Verification and limits

Source-only mathematical regression checks cover transformed cubic evaluation, translation-free derivatives, parent-space conversion and closed-endpoint preservation. Source wiring checks cover drag-start derivative snapshots, curve-aware gizmo mode, and document-history capture/restore of SHAPE arrays. Existing curve serialization and validation rules remain in place.

These checks do not prove compiler success, interactive layout, export/reopen behavior or in-game traversal. Those still require an explicitly approved build and runtime tests. Cursor placement currently requires gameplay collision; it does not infer placement on arbitrary uncollidable render geometry.
