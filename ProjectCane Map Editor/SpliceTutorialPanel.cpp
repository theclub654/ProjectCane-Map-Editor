#include "SpliceTutorialPanel.h"
#include "SpliceRuntime.h"

#include <imgui.h>

#include <string>
#include <iterator>

namespace
{
struct SyntaxEntry
{
    const char *name;
    const char *usage;
    const char *description;
};

constexpr SyntaxEntry syntaxEntries[]{
#include "SpliceSyntaxHelp.inc"
};
constexpr SyntaxEntry builtinEntries[]{
#include "SpliceBuiltinHelp.inc"
};
static_assert(std::size(builtinEntries) == 125);
constexpr SyntaxEntry eventEntries[]{
#include "SpliceEventHelp.inc"
};
static_assert(std::size(eventEntries) == 30);

void DrawSyntaxReference(const SyntaxEntry *entries, const std::size_t count, const char *source)
{
    static ImGuiTextFilter search;
    search.Draw("Search syntax or description", -1.0f);
    ImGui::TextDisabled("Source: %s", source);
    ImGui::TextWrapped("Usage lines describe argument order. [optional] and ... are notation; replace them with actual expressions. Names such as object, value and procedure are placeholders.");
    ImGui::BeginChild("ReferenceEntries", ImVec2(0.0f, 0.0f));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto &entry = entries[index];
        const auto haystack = std::string(entry.name) + " " + entry.usage + " " + entry.description;
        if (!search.PassFilter(haystack.c_str()))
            continue;
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::CollapsingHeader(entry.name, ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextWrapped("Usage: %s", entry.usage);
            ImGui::TextWrapped("%s", entry.description);
            if (ImGui::SmallButton("Copy usage"))
                ImGui::SetClipboardText(entry.usage);
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void Heading(const char *text)
{
    ImGui::SeparatorText(text);
}

void Paragraph(const char *text)
{
    ImGui::TextWrapped("%s", text);
    ImGui::Spacing();
}

void Step(const int number, const char *title, const char *body)
{
    ImGui::TextColored(ImVec4(.35f, .78f, 1.0f, 1.0f), "%d. %s", number, title);
    ImGui::Indent(20.0f);
    ImGui::TextWrapped("%s", body);
    ImGui::Unindent(20.0f);
    ImGui::Spacing();
}

void CodeExample(const char *id, const char *title, const char *explanation, const char *source)
{
    if (!ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::TextWrapped("%s", explanation);
    if (ImGui::Button((std::string("Copy script##") + id).c_str()))
        ImGui::SetClipboardText(source);
    ImGui::SameLine();
    ImGui::TextDisabled("Paste into Splice Events > Source code, then Validate and Apply to graph.");
    ImGui::BeginChild((std::string("code_") + id).c_str(), ImVec2(-1.0f, 170.0f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(source);
    ImGui::EndChild();
}

void Bullet(const char *label, const char *text)
{
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(.88f, .9f, .95f, 1.0f), "%s", label);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text);
}
}

void DrawSpliceTutorialPanel(bool &open, bool &openSpliceEvents, bool &openObjectOperations)
{
    if (!open)
        return;
    ImGui::SetNextWindowSize(ImVec2(1050.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Splice Syntax & Property Reference", &open))
    {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(.35f, 1.0f, .5f, 1.0f),
                       "ProjectCane Splice scripting: source, graph, validation, and safe export");
    ImGui::SameLine();
    if (ImGui::Button("Open Splice Events"))
        openSpliceEvents = true;
    ImGui::SameLine();
    if (ImGui::Button("Browse object operations"))
        openObjectOperations = true;
    ImGui::TextWrapped(
        "This reference describes the serialized scripting system used by the original game. Editor object/property names are authoring aliases; exported scripts contain numeric OIDs, native option IDs, symbols, and expression nodes.");

    if (ImGui::BeginTabBar("SpliceTutorialTabs", ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if (ImGui::BeginTabItem("Syntax reference"))
        {
            DrawSyntaxReference(syntaxEntries, std::size(syntaxEntries), "ProjectCane eval.cpp, ref.h; editor SpliceRuntime.cpp");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("All 125 built-ins"))
        {
            DrawSyntaxReference(builtinEntries, std::size(builtinEntries), "ProjectCane eval.cpp / g_mpbifkbif and bif.cpp");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("All 30 event symbols"))
        {
            DrawSyntaxReference(eventEntries, std::size(eventEntries), "ProjectCane lo.h / lo.cpp and native class dispatch sites");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Properties & actions"))
        {
            Heading("Named properties, queries and methods");
            Paragraph("Open Object Operations Reference to browse every class's inherited operations. Each entry shows its exact source token, native script option ID, value type, access mode, usage and native descriptor binding. A property can be stored in BRX yet have no runtime setter; the two access modes are different.");
            if (ImGui::Button("Open searchable property reference"))
                openObjectOperations = true;
            Paragraph("Hidden is a writable Boolean: (seto! @Target ALO.Hidden true) hides it; false shows it. Frozen is read-only: (geto @Target ALO.Frozen). Neither changes the syntax of geto/seto!. Qualified names are editor authoring tokens and export as native numeric option IDs.");
            Paragraph("For a native method, retrieve and then call it: ((geto @Destination WARP.Triggerwarp)). The target must expose that operation. Changing a script's owner does not change the class of @Destination. Raw numeric operands remain supported and retain their original value.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Start here"))
        {
            Heading("What Splice is");
            Paragraph(
                "Splice is ProjectCane's compact expression language for object behavior. A world object can reference one Splice module. That module contains definitions, event callbacks, local symbols, imports, object lookups, property operations, conditions, and calls to built-in game operations. The source editor and visual graph are two views of the same serialized expression tree.");
            Bullet("Module:", "A reusable expression tree attached to one or more world objects.");
            Bullet("Event:",
                   "A symbol reserved by the game. Trigger is event 2, Touch is 6, Death is 8, Post-load is 0.");
            Bullet("Callback:", "A lambda assigned to an event symbol. ProjectCane invokes it when that event occurs.");
            Bullet(
                "OID:",
                "The runtime identity used to find another object. Duplicate OIDs are resolved through hierarchy and PROXY/PXR context.");
            Bullet("Property or method:", "An operation exposed by the target's class, such as ALO.Hidden or WARP.Triggerwarp. Use names from the reference; the editor emits the native script option ID.");
            Bullet(
                "Editor alias:",
                "A friendly @Name accepted by the editor and compiled back to the object's numeric OID. Use @{Name With Spaces} when the editor name is not one word.");

            Heading("Your first script");
            Step(
                1, "Select the controller",
                "Select the BUTTON, volume, guard, object, or other world object that should receive the event. The script belongs to this controller, not necessarily to the object it changes.");
            Step(
                2, "Open Splice Events",
                "Use View > Splice Events, or the button above. In Selected object authoring, create an empty module if the object does not already have one.");
            Step(
                3, "Open Source code",
                "Choose the source-authoring view. The temporary text buffer cannot change the live graph until it validates and you press Apply to graph.");
            Step(
                4, "Write or paste",
                "Start with (begin ...). Define an event callback, then place actions and a final return value inside its lambda.");
            Step(
                5, "Validate",
                "Press Validate. Fix every diagnostic at its reported line and column. Validation checks syntax, expression shape, symbols, property compatibility, and references the editor can resolve.");
            Step(
                6, "Apply and inspect",
                "Press Apply to graph. Open the visual graph to inspect the same expressions as connected nodes. Use Preview only for sandbox-safe behavior inspection.");
            Step(
                7, "Save, rebuild, test",
                "Save the editor project, run level validation, export the rebuilt PCBRX, then test in ProjectCane/the game. A safe preview is not a substitute for runtime testing.");

            ImGui::TextColored(
                ImVec4(1.0f, .72f, .25f, 1.0f),
                "Important: never guess EOPIDs. Choose properties from the editor or verify them against ProjectCane's CID option table.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Language"))
        {
            Heading("Core forms");
            Bullet("(begin A B C)", "Runs expressions in order and returns the last result.");
            Bullet(
                "(define $name value)",
                "Creates a friendly local symbol. Event callbacks are definitions whose name is an event symbol such as sym#2.");
            Bullet("(lambda ($self) body)",
                   "Creates a callback. $self is the friendly local symbol for the object receiving the event.");
            Bullet("(if condition when-true when-false)", "Evaluates exactly one branch.");
            Bullet("(set! $name value)", "Changes a friendly local/module symbol that was already defined.");
            Bullet("(import N)",
                   "Imports definitions from Splice module N. Imports share behavior; they do not clone it.");
            Bullet("true / false", "Boolean literals. Event callbacks commonly finish with one of these.");
            Bullet("integer / decimal", "Numeric literals. Property type still has to match the EOPID.");

            Heading("Object operations");
            Bullet(
                "(find-object OID basis)",
                "Uses ProjectCane's contextual object lookup. Use tag#0:0 for a null/global basis, or pass an object to search relative to that hierarchy. It may resolve differently inside separate PROXY instances.");
            Bullet(
                "@EditorName / @{Editor Name}",
                "Friendly source alias for a uniquely named object. Braces preserve spaces and punctuation; the editor converts either form to find-object/OID data.");
            Bullet(
                "object#0",
                "The only portable BASIC literal: a null object pointer. Nonzero object# values are rejected because ProjectCane loads them as raw pointers, not OIDs.");
            Bullet("(geto object PropertyName)", "Reads an exposed property or retrieves a native method from the target object.");
            Bullet(
                "(seto! object PropertyName value)",
                "Writes a property through its native setter. Retrieve and call native methods with ((geto object MethodName) arguments...) instead.");

            Heading("How to read nested code");
            Paragraph(
                "Read from the inside outward. In ((geto (find-object 1434 tag#0:0) WARP.Triggerwarp)), find-object resolves a WARP OID, geto retrieves its native Trigger method, and the outer parentheses call it with no arguments.");
            ImGui::TextColored(
                ImVec4(1.0f, .55f, .3f, 1.0f),
                "Do not use editor names as runtime identity. Renaming an object updates editor aliases, while export still emits the real OID.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Events"))
        {
            Heading("Common event callbacks");
            Bullet(
                "Post-load [0]",
                "Runs after the object has loaded. Use it for initialization, not for references that ProjectCane has not finished resolving.");
            Bullet("Trigger [2]",
                   "General activation event. Buttons, warps, barriers, and many scripted interactions use it.");
            Bullet("Touch [6]",
                   "Runs when the object's gameplay logic reports contact. Exact dispatch depends on the CID.");
            Bullet("Death [8]", "Runs when a compatible gameplay object dies or is destroyed.");
            Bullet("Apply [9] / Retract [11]", "Commonly connected to ASEG/state animation lifecycle behavior.");
            Bullet("Animation frame [10]", "Dispatched by serialized animation events at authored timing points.");
            Bullet("Dialog label [16]", "Dispatched by dialog CallSplice events.");
            Bullet(
                "Impact [29]",
                "Used by collision-aware objects such as BARRIER; callback arguments may include the impacted object.");
            Paragraph(
                "An event definition does nothing unless that object's runtime CID actually dispatches the event. The Splice editor can create any callback, but ProjectCane decides when it is called.");

            Heading("Callback anatomy");
            ImGui::TextUnformatted(
                "(define sym#2              ; define Trigger event 2\n  (lambda ($self)          ; $self is the receiving object\n    (begin                 ; run multiple expressions\n      ...actions...\n      true)))              ; callback result");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Recipes"))
        {
            static constexpr char trigger[] =
                "; Minimal Trigger callback\n(begin\n  (define sym#2\n    (lambda ($self)\n      true)))\n";
            static constexpr char toggle[] =
                "; Persistent Boolean toggle\n(begin\n  (define $enabled false)\n  (define sym#2\n    (lambda ($self)\n      (if $enabled\n          (begin\n            (set! $enabled false)\n            false)\n          (begin\n            (set! $enabled true)\n            true)))))\n";
            static constexpr char warp[] =
                "; Trigger WARP OID 1434 (replace 1434)\n(begin\n  (define sym#2\n    (lambda ($self)\n      (begin\n        ((geto (find-object 1434 tag#0:0) WARP.Triggerwarp))\n        true))))\n";
            static constexpr char namedWarp[] =
                "; Same operation with an editor-only alias\n; Rename the WARP to DestinationA first.\n(begin\n  (define sym#2\n    (lambda ($self)\n      (begin\n        ((geto @DestinationA WARP.Triggerwarp))\n        true))))\n";
            static constexpr char alternating[] =
                "; Alternate between two WARP destinations\n(begin\n  (define $useSecond false)\n  (define sym#2\n    (lambda ($self)\n      (if $useSecond\n          (begin\n            ((geto (find-object 2002 tag#0:0) WARP.Triggerwarp))\n            (set! $useSecond false)\n            true)\n          (begin\n            ((geto (find-object 2001 tag#0:0) WARP.Triggerwarp))\n            (set! $useSecond true)\n            true)))))\n";
            CodeExample(
                "trigger", "1. Minimal Trigger",
                "The smallest useful event definition. It proves that the selected object receives Trigger [2].",
                trigger);
            CodeExample(
                "toggle", "2. Persistent toggle",
                "Stores module state and flips it every time Trigger runs. Replace the branch results with real actions.",
                toggle);
            CodeExample(
                "warp", "3. Trigger a WARP",
                "Calls ProjectCane's WARP Trigger property. Replace 1434 with a WARP OID that resolves from the controller's context.",
                warp);
            CodeExample(
                "named_warp", "4. WARP using an editor alias",
                "Readable while authoring. The alias must be unique and becomes an OID expression when applied.",
                namedWarp);
            CodeExample("alternating", "5. Alternating WARP",
                        "A hand-written version of the guided alternating-WARP template. Replace both example OIDs.",
                        alternating);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Graph & source"))
        {
            Heading("One script, two representations");
            Paragraph(
                "The source editor parses text into the same compact pair/list graph serialized by PCBRX. The visual graph does not create a different scripting system. Blue links represent child/list-head structure; green links represent next/sibling structure. Friendly labels translate raw tags into concepts such as Event, Callback, find-object, and seto!.");
            Bullet(
                "Source is best for:",
                "Writing structured behavior, copying recipes, reviewing a complete callback, and reading exact diagnostics.");
            Bullet(
                "Graph is best for:",
                "Understanding nesting, following child/next relationships, locating malformed links, and inspecting imported or retail-authored trees.");
            Bullet("Graph selection -> source:",
                   "Moves the source cursor to the expression represented by the selected node.");
            Bullet("Source cursor -> graph:", "Selects the expression under the text cursor in the graph.");
            Bullet("Make unique:", "Clones a shared module before editing so other users are not changed.");
            Bullet("Attach:", "Makes an object share an existing module.");
            Bullet("Import:",
                   "Lets one module lexically reuse another module while keeping its own local definitions.");
            Paragraph(
                "After editing either view, validate and inspect the other. If generated-source coverage reports a mismatch, do not export until the graph can round-trip without losing node meaning.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Diagnostics"))
        {
            Heading("Safe authoring checklist");
            Bullet("Validate before Apply", "Invalid temporary source never replaces the live module.");
            Bullet("Apply before saving", "A text draft is not serialized until Apply to graph succeeds.");
            Bullet("Check shared users",
                   "Editing a shared module intentionally changes every attached object unless you Make unique.");
            Bullet("Resolve references",
                   "Confirm OIDs and aliases resolve to the intended CID from the controller's PROXY/PXR context.");
            Bullet("Match property types",
                   "Bool, float, integer, OID, vector, and action properties are not interchangeable.");
            Bullet(
                "Run Level Validation",
                "Fix broken imports, missing symbols, illegal EOPIDs, invalid references, cycles, and writer blockers.");
            Bullet("Test the dispatch", "A correct callback will not run if the selected CID never emits that event.");
            Bullet("Test in game", "Sandbox preview intentionally approximates or blocks native side effects.");

            Heading("Frequent errors");
            Bullet(
                "Unknown symbol",
                "Define it before use, correct the spelling, use sym#N for a fixed symbol, or import the module that owns it.");
            Bullet(
                "Wrong argument count",
                "Check the form. seto! needs object, EOPID, and value; find-object needs an OID and a search-basis object (tag#0:0 means no basis).");
            Bullet(
                "Illegal EOPID",
                "The property does not belong to the target CID, or a friendly property name was resolved against the wrong object type.");
            Bullet("Alias is ambiguous", "Give every referenced object a unique editor name, or use its numeric OID.");
            Bullet("Object not found",
                   "The OID is absent or resolves outside the controller's runtime hierarchy/PXR context.");
            Bullet("Imported event changed unexpectedly",
                   "A local definition can override an imported definition. Inspect event origins and import order.");
            Bullet("Preview did nothing",
                   "Native gameplay operations are sandboxed. Read the preview trace, then test the rebuilt level.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Export rules"))
        {
            Heading("What is serialized");
            Paragraph(
                "The from-scratch writer serializes the module table, compact expression nodes, child/next indices, tags, numeric payloads, symbols, imports, and each object's splice-module link. Source formatting, comments, editor aliases, graph positions, and tutorial text are editor conveniences and are not required by the game.");
            Bullet("Object names", "Remain editor metadata. @Name references compile to numeric object lookups.");
            Bullet("Comments", "Help source authoring but do not become runtime behavior.");
            Bullet("Local symbols",
                   "Are allocated into the world symbol table; the editor prevents collisions with existing symbols.");
            Bullet("Imports", "Serialize module indices and must remain valid after compaction/rebuilding.");
            Bullet("Events", "Serialize as ProjectCane's fixed event symbol IDs.");
            Bullet("Properties", "Serialize as EOPIDs and typed values expected by each CID descriptor.");
            Paragraph(
                "Before export: apply the draft, verify source coverage, inspect event origins and dependencies, run full level validation, save the editor project, then rebuild the PCBRX. Keep a known-good level while testing new scripts.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
