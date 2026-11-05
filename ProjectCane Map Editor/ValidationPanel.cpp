#include "ValidationPanel.h"
#include "BrxDocument.h"
#include "EditorWorkspace.h"
#include "EditorCamera.h"
#include "SceneRenderer.h"
#include <imgui.h>
#include <algorithm>

namespace
{
const char *SeverityName(ValidationSeverity severity)
{
    return severity == ValidationSeverity::Error     ? "Errors"
           : severity == ValidationSeverity::Warning ? "Warnings"
                                                     : "Information";
}
ImVec4 SeverityColor(ValidationSeverity severity)
{
    return severity == ValidationSeverity::Error     ? ImVec4(1, .3f, .3f, 1)
           : severity == ValidationSeverity::Warning ? ImVec4(1, .72f, .25f, 1)
                                                     : ImVec4(.45f, .75f, 1, 1);
}
}

void DrawValidationPanel(bool &open, BrxDocument &document, EditorWorkspace &workspace, SceneRenderer &renderer,
                         EditorCamera &camera, std::string &status)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Level Validation", &open))
    {
        ImGui::End();
        return;
    }
    bool rerunValidation = false;
    if (ImGui::Button("Run validation"))
    {
        workspace.Refresh(document);
        status = "Validation refreshed.";
    }
    ImGui::SameLine();
    const auto plan = workspace.Compiler().Analyze(document, workspace.Validation());
    const auto errorCount = workspace.Validation().ErrorCount();
    const auto warningCount = workspace.Validation().WarningCount();
    ImGui::TextColored(plan.writerAvailable ? ImVec4(.35f, 1, .45f, 1) : ImVec4(1, .35f, .3f, 1),
                       "Compiler readiness: %s", plan.writerAvailable ? "rebuilt export ready" : "blocked");
    if (!plan.validationPassed)
        ImGui::TextColored(ImVec4(1.0f, .72f, .25f, 1.0f),
                           "Gameplay/editor diagnostics remain; only serialization-critical failures block export.");
    ImGui::TextColored(ImVec4(.35f, .8f, 1.0f, 1.0f),
                       "Export source: authoritative editor document (no source-payload fallback).");
    ImGui::TextColored(plan.cidLayoutsAudited ? ImVec4(.35f, 1.0f, .45f, 1.0f) : ImVec4(1.0f, .35f, .3f, 1.0f),
                       "CID payload-layout coverage: %s",
                       plan.cidLayoutsAudited ? "all serializable CIDs recognized"
                                              : "unsupported/runtime-only object found");
    for (const auto &blocker : plan.blockers)
        ImGui::BulletText("%s", blocker.c_str());
    if (workspace.Validation().Issues().empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(.35f, 1.0f, .48f, 1.0f), "No validation issues were found.");
    }
    else
        ImGui::TextDisabled("%zu error%s | %zu warning%s | %zu total diagnostic%s", errorCount,
                            errorCount == 1 ? "" : "s", warningCount, warningCount == 1 ? "" : "s",
                            workspace.Validation().Issues().size(),
                            workspace.Validation().Issues().size() == 1 ? "" : "s");
    ImGui::Separator();
    for (const auto severity : {ValidationSeverity::Error, ValidationSeverity::Warning, ValidationSeverity::Info})
    {
        const auto count = std::count_if(workspace.Validation().Issues().begin(), workspace.Validation().Issues().end(),
                                         [&](const ValidationIssue &issue) { return issue.severity == severity; });
        const std::string heading = std::string(SeverityName(severity)) + " (" + std::to_string(count) + ")";
        if (!ImGui::CollapsingHeader(heading.c_str(),
                                     severity == ValidationSeverity::Error ? ImGuiTreeNodeFlags_DefaultOpen : 0))
            continue;
        for (std::size_t issueIndex = 0; issueIndex < workspace.Validation().Issues().size(); ++issueIndex)
        {
            const auto &issue = workspace.Validation().Issues()[issueIndex];
            if (issue.severity != severity)
                continue;
            ImGui::PushID(static_cast<int>(issueIndex));
            ImGui::TextColored(SeverityColor(severity), "%s", issue.category.c_str());
            ImGui::SameLine();
            const bool validObject =
                issue.objectIndex >= 0 && static_cast<std::size_t>(issue.objectIndex) < document.Objects().size();
            if (validObject)
            {
                const auto &object = document.Objects()[issue.objectIndex];
                const std::string objectLabel = document.ObjectEditorLabel(issue.objectIndex);
                if (ImGui::SmallButton(objectLabel.c_str()))
                {
                    renderer.SelectObject(issue.objectIndex);
                    camera.FocusPoint({object.worldTransform[12], object.worldTransform[13], object.worldTransform[14]},
                                      (std::max)(object.renderRadiusAll * 2.5f, 100.0f));
                    status = "Selected and framed validation object.";
                }
            }
            ImGui::TextWrapped("%s", issue.message.c_str());
            const bool parentFix = issue.category == "Hierarchy" && validObject;
            const bool referenceFix = issue.category == "Reference" && validObject && issue.eopid >= 0;
            if (parentFix && ImGui::SmallButton("Safe fix: move to world root"))
            {
                document.EditableObjects()[issue.objectIndex].parentIndex = -1;
                rerunValidation = true;
                status = "Removed invalid parent link.";
            }
            if (referenceFix && ImGui::SmallButton("Safe fix: clear missing/incompatible reference"))
            {
                auto &properties = document.EditableObjects()[issue.objectIndex].properties;
                for (auto &property : properties)
                    if (property.eopid == issue.eopid)
                    {
                        property.intValue = -1;
                        property.modified = true;
                    }
                rerunValidation = true;
                status = "Cleared invalid object reference.";
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }
    if (rerunValidation)
        workspace.Refresh(document);
    ImGui::End();
}
