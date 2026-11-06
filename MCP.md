# ProjectCane Map Editor MCP

The MCP companion lets an AI client inspect the open map, consult the editor's
actual CID/property tables, and search both editor and ProjectCane source with
file/line evidence. The client supplies the AI model. No model, API key, remote
service or HTTP listener is embedded in the editor.

The integration has two paths: structured native operations for bulk work, and
optional observed UI control for specialist controls. UI control operates the
existing ImGui widgets; it does not introduce a second implementation of each CID.
See [coverage and limits](MCP-Coverage.md) for the distinction.

## Setup

Use an editor build containing `EditorMcpBridge.h` and `EditorMcpTools.h`.
Previously built executables do not gain these features automatically.
No build is performed by the companion.
After updating an existing installation, rebuild the editor and restart your MCP
client. Keep the existing virtual environment and server registration.

Install the companion's dependencies once using Python 3.11 or later. From the
repository root on Windows:

```powershell
python -m venv mcp/.venv
./mcp/.venv/Scripts/python.exe -m pip install -r mcp/requirements.txt
```

Use your installed Python executable's full path if `python` is not in PATH.
Skip environment creation if `mcp/.venv` already exists.

On Linux:

```sh
python3 -m venv mcp/.venv
mcp/.venv/bin/python -m pip install -r mcp/requirements.txt
```

In the editor, open **Help > AI / MCP Connection**, then enable the local
connection. It starts read-only. Editing has a separate checkbox and resets to
off when reconnecting. Selection/camera framing does not require document editing.
Native file output has a separate permission. UI control has a third permission
and requires both other permissions, because existing UI controls can save or
delete. All permissions start off on a new connection.

Configure your MCP client to launch the companion over **stdio**. For clients
using a JSON `mcpServers` configuration, adapt these absolute paths:

```json
{
  "mcpServers": {
    "projectcane-editor": {
      "command": "C:/Users/denar/source/repos/ProjectCane Map Editor/mcp/.venv/Scripts/python.exe",
      "args": [
        "C:/Users/denar/source/repos/ProjectCane Map Editor/mcp/server.py",
        "--reference-root", "C:/Users/denar/source/repos/ProjectCane"
      ]
    }
  }
}
```

The script finds the editor source relative to itself. Use `--editor-root` when
launching a copied companion. `--reference-root` is optional; without it, the
authoritative runtime source is unavailable. On Linux, use Linux paths and the
Linux virtual environment executable. Run editor and companion in the same OS
environment; native Windows and WSL have different temporary directories.

With multiple open editors, copy the desired bridge folder from the panel and
add `--bridge`, followed by that full path, to the companion arguments. No
connection is silently selected when multiple live sessions exist.

## What the tools know

| Tools | Data and behavior |
| --- | --- |
| `editor_state` | Live selection, source map path, session, revision, exact capabilities and edit gate |
| `list_cids`, `cid_schema` | All 148 CID labels; inherited legal options, native descriptor types, flags, bindings, descriptions, reference contracts, append semantics, raw default bits and source-linked authoring guidance |
| `list_objects`, `inspect_object` | Native OID and separate object index, hierarchy, ordered properties including duplicates/dynamic arguments, matrices, decoded details, SHAPE control points/tangents, effect colors and summaries |
| `inspect_world` | World option stream and major content counts |
| `list_assets` | Asset metadata and owning/using objects, paginated |
| `inspect_splice` | Canonical source, shared users, imports, callbacks and property accesses using existing Splice analysis |
| `inspect_visibility` | Zone names, one-hop records and BSP planes/children/masks |
| `validate_map` | Existing validation and export blockers; does not export or build |
| `search_source`, `read_source` | Confined, read-only editor and optional ProjectCane source/documentation with line numbers |
| `select_object` | Select and optionally frame in the viewport |
| `rename_object` | Undoable editor alias; native OID remains unchanged |
| `transform_object` | Existing transform operation, including its supported geometry/collision updates |
| `reparent_object` | Existing operation preserving world position with native parent restrictions |
| `create_object` | Same class-specific starter presets and unavailable-class rules as the Add catalog |
| `list_operations`, `preview_operation`, `inspect_preview`, `apply_preview`, `discard_preview` | Discover native authoring actions, run one or a batch on a copy, inspect that exact copy, then commit as one Undo entry |
| `list_record_types`, `inspect_record` | Nested decoded records: mesh vertices/bones, SO collision/BSPs, animation controllers/events, effects, materials, dialogue, state machines, navigation, volumes, PROXY records, world options and more |
| `preview_open_map` | Load a BRX/project separately; applying replaces the open document and clears history, matching the Open workflow |
| `edit_history` | Existing Undo/Redo, including UI-created history entries |
| `select_objects`, `viewport`, `query_visibility` | Existing multi-selection, camera/view options and retail-compatible VISMAP point/one-hop/sphere queries |
| `editor_panels` | Open or close existing specialist panels |
| `list_file_operations`, `preview_file_operation`, `write_previewed_file` | Native project/hierarchy/PROXY saving, BRX/GLB export and texture export to new destinations |
| `capture_editor_ui`, `editor_ui_input` | Image and coordinate metadata, then single-use, observed input to existing in-app controls |

The `projectcane://guide` resource provides this document. The
`explain_selection` prompt provides a read-only inspection starting point.

## Evidence and limits

MCP gives a model access to facts; it does not guarantee that a model understands
every behavior. Ask for ProjectCane file/function evidence before accepting a
runtime claim. Schema defaults are **raw bits**, not automatically converted
effective values. An omitted property is not necessarily false or zero.
Property descriptions may be incomplete; the source tools supply deeper evidence.
Property float arrays also include `float_bits` so non-finite values and exact
binary encodings survive inspection even when JSON's numeric value is null.

`inspect_object` is a friendly summary. Use `list_record_types` and
`inspect_record` for detailed decoded records. An empty JSON pointer returns a
field inventory without converting mesh data. Select a nested field, for example
`/so/collisionGeometry/positions` on an object or `/channels/0/position` on an
animation. Arrays paginate; use a deeper pointer if a page exceeds 128 KiB.
`bitmap_bytes` reads the native compressed byte stream, not a decoded RGBA image.
Only property inspection supplies float bit patterns; JSON numeric non-finite
values in other record views become null. Unknown/undecoded source sections are
not invented. Source inspection remains separate from live data inspection.

The native catalog includes hierarchy deletion, duplication, replacement/import,
PROXY operations, Splice source/events/imports, model/FBX character import,
collision building/import/repair, RGBA8/BC1/BC3 textures, material/resource
duplication/deletion with remapping, GLOB uniqueness/instancing, PATHZONE helpers,
SHAPE transforms, world/object property edits and VISMAP setup/membership.
Typed property edits preserve occurrence order, descriptor flags and type tags.
Dynamic/expression property records need their native specialized UI or Splice
operation, not a guessed scalar conversion.

No arbitrary payload JSON patching, shell commands, compilation or builds are
exposed. Specialist fine-grained controls are available through the existing UI
rather than a separate typed method for every widget.

Creating an object does not guarantee all gameplay dependencies are supplied.
Inspect its setup guidance, related objects and export diagnostics, then test in
the game. Reparenting does not bypass retail serialization restrictions.
Transforms have the same class-specific behavior as the UI; PNT is position-only
and SHAPE stores curve data rather than a native object matrix.

Every result from the editor contains `session`, an opaque `revision`, and `data`.
Pass the latest revision to each write. A changed document rejects stale writes.
Object indices can change after hierarchy edits; locate/inspect again afterward.
Edits run on the UI thread and enter existing Undo history. Failure restores the
captured document state. Locked objects and resolved PROXY clones are protected.
Validation is source/editor validation, not proof of game execution correctness.

## Preview and apply

Call `list_operations`, then `preview_operation` with its operation name, argument
object and the latest revision. The operation runs on a copy; failures discard it.
The result includes validation, counts and the first changed object rows.
`inspect_preview` reads the staged copy, including `kind=splice_source` for staged
script source. Applying consumes the exact copy without re-reading an import.
Tokens expire after five minutes or a changed document/history generation. An
edit/Undo cycle invalidates an old preview even if the content hash matches again.
Resource-ID allocation never rewinds when a staged copy commits. One preview is held at a
time and disconnecting discards it. Locked documents reject these operations
because shared references/resources can affect objects outside the selected tree.

`batch` accepts 1..32 catalog operations and commits as one history entry. Indices
are interpreted sequentially, so deletion/replacement can remap later indices.
For example:

```json
{
  "operation": "batch",
  "arguments": {
    "operations": [
      {"operation": "property_set", "arguments": {"index": 10, "eopid": 125, "value": true}},
      {"operation": "property_set", "arguments": {"index": 11, "eopid": 125, "value": true}}
    ]
  },
  "revision": "COPY THE LATEST OPAQUE REVISION"
}
```

Inspect each CID schema first; the example IDs are not substitutes for checking
that a property is legal for the actual objects.

Opening a different document also uses a preview, but clears history instead of
creating an Undo entry. When a map is already open, applying that preview requires
`replace_current_document=true`. Save work before authorizing replacement.

## File output

File output requires its separate toggle and a reviewed file-preview token. Native
writers keep their existing BRX validation/export blockers. Single files publish
only after a successful write, using a hard link to a new destination; the target
filesystem must support hard links. Existing files are never overwritten by these
structured tools. Choose a new name on filesystems that support this workflow.
Texture export reserves a new folder; failures can leave that new folder for
inspection. File output is not undoable. Saving a project updates the editor's
project path/saved history marker; exporting does not mark the project saved.

## Existing UI control

This path reaches specialist controls such as timeline keys, dialogue events,
particle curves, material settings and advanced CID editors through their actual
UI. It requires a vision-capable MCP client. Enable **Allow MCP to operate the
existing UI** as well as the edit and file-output permissions.

1. Open a panel using `editor_panels` and select the relevant object.
2. Call `capture_editor_ui`. It returns a real image plus a capture token and
   coordinate metadata. It captures only the editor's rendered client area.
3. Call `editor_ui_input` with that token and an observed control. Coordinates
   are normalized fractions of the image, from its top left.
4. Capture again after input finishes and verify the outcome.

Click, double-click, drag, scroll, literal text and a restricted key set are
supported. Ctrl-click enters native numeric text editing. Clicks/drags/keys are
queued across frames and include release events. Input is delivered after GLFW's
frame update and before ImGui's frame begins. Capture requests complete after
ImGui draws and before buffer swapping, so both platforms read the rendered back
buffer. The companion polls a read-only ticket rather than blocking the UI thread.
Captures wait for input to settle; GPU readback is performed only on request.
Tokens are single-use, expire after 30 seconds, and reject changed maps/windows.
The editor requests focus before delivering input; detected local input/focus changes stop
UI control. Leave the computer untouched while it operates and re-enable control
after an interruption.

UI control follows **native UI behavior**, including native Undo support, Save,
Delete and overwrite behavior. It is not copy-preview authoring and does not
inherit the structured file tools' no-overwrite restriction. It cannot operate
OS-native file dialogs or other applications. If a modal dialog blocks the editor,
close/complete it manually; prefer structured import/output tools for file work.

The local mailbox is bounded to 1 MiB requests, expiring requests and one request
per frame. The editor must be responsive. Timeouts are not automatically retried:
an edit that already started may finish after a timeout, so inspect before retrying.
The connection is for trusted local clients running as your user, not a sandbox
against other software under the same account. Close the connection when finished.

## Example requests to your AI client

- “Explain the selected object and cite the ProjectCane functions that load it.”
- “Find the PIPE objects, inspect their parents and report validation problems. Don't edit.”
- “Explain which callbacks this object's Splice module defines and whether it is shared.”
- “Compare this emitter's authored settings with its CID schema and source defaults.”
- “Create a PIPE using the existing Add preset at this explicit position, then validate.”

## Source-only checks

```sh
python mcp/test_connection.py
python mcp/test_sdk.py
```

These tests cover the companion transport, discovery, expiry, errors and source
path confinement, native record/operation contracts and the real MCP stdio
handshake/tool/image-content handling. The SDK test uses fake image bytes; it does
not test OpenGL capture or inject input into the editor. They do not compile or
certify the C++ integration. Approved Windows/Linux builds and live preview,
Undo/Redo, output and UI-control smoke tests are still required before release.

`editor_state` includes `dirty`, `project_path`, `undo_label` and `redo_label` so
clients can distinguish an unsaved edit, the map's source path and its project
save destination. Camera position and direction must be supplied together.
File previews check operation names, destinations, format extensions and required
record indices; the native writer still performs the final serialization checks.
Saving a hierarchy or portable PROXY can read a resolved PROXY member, as in the
editor UI, without granting permission to edit that clone directly.
