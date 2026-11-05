#include "DialogManagerPanel.h"

#include "AudioPreviewService.h"
#include "BrxDocument.h"
#include "EditorOptions.h"
#include "SceneRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
std::size_t SubtitleCharacterCount(const std::string &text)
{
    std::size_t count = 0;
    for (const unsigned char ch : text)
    {
        if (ch == 0)
            break;
        if ((ch & 0xc0u) != 0x80u)
            ++count;
    }
    return count;
}

const BrxPropertyRecord *FindProperty(const BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto found = std::find_if(object.properties.rbegin(), object.properties.rend(),
                                    [eopid](const BrxPropertyRecord &property) { return property.eopid == eopid; });
    return found == object.properties.rend() ? nullptr : &*found;
}

bool Enabled(const BrxObjectRecord &object, const std::int16_t eopid)
{
    const auto *property = FindProperty(object, eopid);
    return property != nullptr && (property->kind == BrxPropertyKind::Bool ? property->boolValue : true);
}

const char *ModeName(const BrxObjectRecord &object)
{
    if (FindProperty(object, 972) != nullptr)
        return "Confrontation";
    if (FindProperty(object, 971) != nullptr)
        return "Instruction";
    return "Standard";
}

const char *EventName(const BrxDialogEventKind kind)
{
    switch (kind)
    {
    case BrxDialogEventKind::PreloadAudio:
        return "Preload audio";
    case BrxDialogEventKind::SpeakerLeft:
        return "Left speaker";
    case BrxDialogEventKind::SpeakerRight:
        return "Right speaker";
    case BrxDialogEventKind::Talk:
        return "Talk";
    case BrxDialogEventKind::CallSplice:
        return "Call splice";
    case BrxDialogEventKind::ShowLetterbox:
        return "Show letterbox";
    case BrxDialogEventKind::HideLetterbox:
        return "Hide letterbox";
    case BrxDialogEventKind::ShowMovie:
        return "Show movie";
    default:
        return "Invalid";
    }
}

ImU32 EventColor(const BrxDialogEventKind kind)
{
    switch (kind)
    {
    case BrxDialogEventKind::Talk:
        return IM_COL32(70, 145, 235, 255);
    case BrxDialogEventKind::SpeakerLeft:
    case BrxDialogEventKind::SpeakerRight:
        return IM_COL32(160, 100, 235, 255);
    case BrxDialogEventKind::PreloadAudio:
        return IM_COL32(65, 190, 135, 255);
    case BrxDialogEventKind::ShowLetterbox:
    case BrxDialogEventKind::HideLetterbox:
        return IM_COL32(235, 165, 65, 255);
    case BrxDialogEventKind::ShowMovie:
        return IM_COL32(220, 85, 110, 255);
    default:
        return IM_COL32(135, 145, 160, 255);
    }
}

int IssueCount(const BrxObjectRecord &object)
{
    int count = object.dialog.present ? 0 : 1;
    if (object.dialog.events.size() > 127)
        ++count;
    std::int16_t left = -1, right = -1;
    bool letterbox = false;
    for (const auto &event : object.dialog.events)
    {
        const int kind = static_cast<int>(event.kind);
        if (kind < 0 || kind > 7)
        {
            ++count;
            continue;
        }
        if ((kind == 0 || kind == 7) && event.resourceName.empty())
            ++count;
        if (event.kind == BrxDialogEventKind::SpeakerLeft)
            left = event.primaryOid;
        if (event.kind == BrxDialogEventKind::SpeakerRight)
            right = event.primaryOid;
        if (event.kind == BrxDialogEventKind::Talk && event.speakerOid != -1 && event.speakerOid != left &&
            event.speakerOid != right)
            ++count;
        if (event.kind == BrxDialogEventKind::ShowLetterbox)
        {
            if (letterbox)
                ++count;
            letterbox = true;
        }
        if (event.kind == BrxDialogEventKind::HideLetterbox)
        {
            if (!letterbox)
                ++count;
            letterbox = false;
        }
    }
    if (letterbox)
        ++count;
    return count;
}

void SetMode(BrxObjectRecord &object, const std::int16_t modeOption)
{
    object.properties.erase(std::remove_if(object.properties.begin(), object.properties.end(),
                                           [](const BrxPropertyRecord &property)
                                           { return property.eopid == 971 || property.eopid == 972; }),
                            object.properties.end());
    if (modeOption < 0)
        return;
    const auto definition = EditorOptionDefinitionForCid(object.cid, modeOption);
    BrxPropertyRecord property;
    property.eopid = modeOption;
    property.kind = definition.kind;
    property.flags = definition.flags;
    property.argumentCount = definition.argumentCount;
    property.editable = true;
    property.modified = true;
    ResetPropertyValue(property);
    object.properties.push_back(std::move(property));
}

BrxDialogEvent Talk(const char *subtitle)
{
    BrxDialogEvent event;
    event.kind = BrxDialogEventKind::Talk;
    event.zoom = -1.0f;
    event.pauseSeconds = -1.0f;
    event.subtitle = subtitle;
    event.modified = true;
    return event;
}

void AppendTemplate(BrxObjectRecord &object, const int kind)
{
    auto &events = object.dialog.events;
    const std::size_t required = kind == 0 ? 2u : 6u;
    if (events.size() + required > 127u)
        return;
    if (kind == 0)
    {
        SetMode(object, 971);
        events.push_back({BrxDialogEventKind::SpeakerLeft});
        events.push_back(Talk("New instruction"));
    }
    else
    {
        SetMode(object, kind == 2 ? 972 : -1);
        events.push_back({BrxDialogEventKind::SpeakerLeft});
        events.push_back({BrxDialogEventKind::SpeakerRight});
        events.push_back({BrxDialogEventKind::ShowLetterbox});
        events.push_back(Talk(kind == 2 ? "Confrontation opening" : "First speaker line"));
        events.push_back(Talk("Second speaker line"));
        events.push_back({BrxDialogEventKind::HideLetterbox});
    }
    object.dialog.present = true;
    object.dialog.modified = true;
}

struct DialogPreviewState
{
    std::int32_t objectIndex = -1;
    std::size_t eventIndex = 0;
    bool playing = false;
    bool initialized = false;
    bool waitingForInput = false;
    bool letterbox = false;
    float elapsed = 0.0f;
    float holdSeconds = 0.0f;
    std::int16_t leftSpeaker = -1;
    std::int16_t rightSpeaker = -1;
    std::int16_t leftStaging = -1;
    std::int16_t rightStaging = -1;
    std::int16_t activeSpeaker = -1;
    std::filesystem::path queuedWave;
    std::string subtitle;
    std::string sandboxEvent;
};

void ResetPreview(DialogPreviewState &preview, AudioPreviewService &audio, const std::int32_t objectIndex)
{
    audio.Stop();
    preview = {};
    preview.objectIndex = objectIndex;
}
}

void DrawDialogManagerPanel(bool &open, BrxDocument &document, SceneRenderer &renderer, std::string &status,
                            const DialogLipsyncPreviewRequest &previewLipsync)
{
    static AudioPreviewService audioPreview;
    static DialogPreviewState preview;
    audioPreview.SetDocumentPath(document.Path());
    if (!open)
    {
        if (preview.objectIndex >= 0)
            ResetPreview(preview, audioPreview, -1);
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(760.0f, 610.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dialog Manager", &open))
    {
        ImGui::End();
        return;
    }

    static char search[128]{};
    static int modeFilter = 0;
    static bool issuesOnly = false;
    ImGui::InputTextWithHint("##dialogSearch", "Search OID, subtitle, or resource", search, sizeof(search));
    ImGui::SameLine();
    const char *modes[]{"All modes", "Standard", "Instruction", "Confrontation"};
    ImGui::SetNextItemWidth(140.0f);
    ImGui::Combo("##dialogMode", &modeFilter, modes, 4);
    ImGui::SameLine();
    ImGui::Checkbox("Issues only", &issuesOnly);

    std::string needle(search);
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    std::vector<std::int32_t> dialogs;
    for (std::size_t index = 0; index < document.Objects().size(); ++index)
    {
        const auto &object = document.Objects()[index];
        if (object.cid != 101)
            continue;
        const std::string mode = ModeName(object);
        if (modeFilter != 0 && mode != modes[modeFilter])
            continue;
        if (issuesOnly && IssueCount(object) == 0)
            continue;
        std::string haystack = std::to_string(object.oid) + " " + mode;
        for (const auto &event : object.dialog.events)
            haystack += " " + event.subtitle + " " + event.resourceName;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                       [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (!needle.empty() && haystack.find(needle) == std::string::npos)
            continue;
        dialogs.push_back(static_cast<std::int32_t>(index));
    }

    ImGui::Text("%zu conversation(s)", dialogs.size());
    ImGui::BeginChild("dialogList", ImVec2(245.0f, 0.0f), true);
    for (const auto index : dialogs)
    {
        const auto &object = document.Objects()[static_cast<std::size_t>(index)];
        const int issues = IssueCount(object);
        const std::string label = "DIALOG OID " + std::to_string(object.oid) + "\n" + ModeName(object) + " | " +
                                  std::to_string(object.dialog.events.size()) + " events" +
                                  (issues ? " | " + std::to_string(issues) + " issue(s)" : "");
        if (ImGui::Selectable(label.c_str(), renderer.SelectedObjectIndex() == index,
                              ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, 42.0f)))
        {
            renderer.SelectObject(index);
            status = "Selected DIALOG OID " + std::to_string(object.oid);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("dialogDetail", ImVec2(0.0f, 0.0f), true);
    const auto selected = renderer.SelectedObjectIndex();
    if (selected < 0 || static_cast<std::size_t>(selected) >= document.Objects().size() ||
        document.Objects()[static_cast<std::size_t>(selected)].cid != 101)
    {
        if (preview.objectIndex >= 0)
            ResetPreview(preview, audioPreview, -1);
        ImGui::TextDisabled("Select a DIALOG to inspect its timeline.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    auto &object = document.EditableObjects()[static_cast<std::size_t>(selected)];
    if (preview.objectIndex != selected)
        ResetPreview(preview, audioPreview, selected);
    ImGui::Text("DIALOG OID %d", object.oid);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ModeName(object));
    if (Enabled(object, 968))
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1, 1), "DEFAULT");
    }
    if (Enabled(object, 969))
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.7f, 0.25f, 1), "CRUCIAL");
    }
    if (Enabled(object, 970))
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "NO SKIP");
    }

    if (ImGui::CollapsingHeader("Activation graph", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int users = 0;
        for (std::size_t candidateIndex = 0; candidateIndex < document.Objects().size(); ++candidateIndex)
        {
            if (candidateIndex == static_cast<std::size_t>(selected))
                continue;
            const auto &candidate = document.Objects()[candidateIndex];
            const bool references = std::any_of(
                candidate.properties.begin(), candidate.properties.end(), [&](const BrxPropertyRecord &property)
                { return IsObjectReferenceOption(property.eopid) && property.intValue == object.oid; });
            if (!references)
                continue;
            ++users;
            const std::string label = std::string(BrxDocument::ClassName(candidate.cid)) + " OID " +
                                      std::to_string(candidate.oid) + "##dialogGraph" + std::to_string(candidateIndex);
            if (ImGui::SmallButton(label.c_str()))
                renderer.SelectObject(static_cast<std::int32_t>(candidateIndex));
        }
        if (users == 0)
            ImGui::TextDisabled(
                "No direct object references. Activation may still come from splice code or Default playback.");
    }

    ImGui::SeparatorText("Quick templates (append)");
    if (object.proxyClone)
        ImGui::TextWrapped("This linked DIALOG inherits its timeline. Make its hierarchy unique or edit the source DIALOG before changing events.");
    ImGui::BeginDisabled(object.proxyClone || object.dialog.events.size() + 2u > 127u);
    if (ImGui::Button("Instruction"))
    {
        AppendTemplate(object, 0);
        ResetPreview(preview, audioPreview, selected);
        status = "Appended instruction dialog template.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(object.proxyClone || object.dialog.events.size() + 6u > 127u);
    if (ImGui::Button("Two-speaker exchange"))
    {
        AppendTemplate(object, 1);
        ResetPreview(preview, audioPreview, selected);
        status = "Appended two-speaker dialog template.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Confrontation"))
    {
        AppendTemplate(object, 2);
        ResetPreview(preview, audioPreview, selected);
        status = "Appended confrontation dialog template.";
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Templates use unassigned speaker slots so you can choose the correct scene characters.");

    ImGui::SeparatorText("Conversation preview");
    if (ImGui::Button(preview.playing ? "Pause full dialogue" : "Play full dialogue"))
    {
        if (preview.eventIndex >= object.dialog.events.size())
            ResetPreview(preview, audioPreview, selected);
        preview.playing = !preview.playing;
        if (audioPreview.IsPaused() && preview.playing)
            audioPreview.Resume();
        else if (audioPreview.IsPlaying() && !preview.playing)
            audioPreview.Pause();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart"))
    {
        ResetPreview(preview, audioPreview, selected);
        preview.playing = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Choose WAV folder"))
    {
        const auto folder = AudioPreviewService::BrowseForAudioFolder();
        if (!folder.empty())
        {
            audioPreview.SetAdditionalSearchRoot(folder);
            status = "Dialogue WAV folder: " + folder.string();
        }
    }
    if (preview.waitingForInput)
    {
        ImGui::SameLine();
        if (ImGui::Button("Press prompt"))
        {
            preview.waitingForInput = false;
            preview.initialized = false;
            ++preview.eventIndex;
            preview.elapsed = 0.0f;
            preview.playing = true;
        }
    }

    if (preview.playing && preview.eventIndex < object.dialog.events.size())
    {
        preview.elapsed += ImGui::GetIO().DeltaTime;
        for (std::size_t guard = 0; guard < object.dialog.events.size() + 1u && preview.playing; ++guard)
        {
            if (preview.eventIndex >= object.dialog.events.size())
            {
                preview.playing = false;
                audioPreview.Stop();
                preview.sandboxEvent = "Conversation finished";
                break;
            }
            const auto &event = object.dialog.events[preview.eventIndex];
            if (!preview.initialized)
            {
                preview.initialized = true;
                preview.elapsed = 0.0f;
                preview.sandboxEvent.clear();
                switch (event.kind)
                {
                case BrxDialogEventKind::PreloadAudio:
                    preview.queuedWave = audioPreview.ResolveDialogWave(event.resourceName);
                    preview.sandboxEvent = preview.queuedWave.empty()
                                               ? "Missing WAV: " + event.resourceName
                                               : "Loaded " + preview.queuedWave.filename().string();
                    break;
                case BrxDialogEventKind::SpeakerLeft:
                    preview.leftSpeaker = event.primaryOid;
                    preview.leftStaging = event.replacementOid;
                    break;
                case BrxDialogEventKind::SpeakerRight:
                    preview.rightSpeaker = event.primaryOid;
                    preview.rightStaging = event.replacementOid;
                    break;
                case BrxDialogEventKind::ShowLetterbox:
                    preview.letterbox = true;
                    break;
                case BrxDialogEventKind::HideLetterbox:
                    preview.letterbox = false;
                    break;
                case BrxDialogEventKind::CallSplice:
                    preview.sandboxEvent = "Sandboxed Splice label OID " + std::to_string(event.primaryOid);
                    break;
                case BrxDialogEventKind::ShowMovie:
                    preview.sandboxEvent = "Sandboxed movie: " + event.resourceName;
                    break;
                case BrxDialogEventKind::Talk:
                    preview.subtitle = event.subtitle;
                    preview.activeSpeaker = event.speakerOid;
                    preview.holdSeconds = event.pauseSeconds >= 0.0f
                                              ? event.pauseSeconds
                                              : std::max(1.0f, static_cast<float>(SubtitleCharacterCount(event.subtitle) + 10u) / 15.0f);
                    if (!preview.queuedWave.empty())
                    {
                        std::string error;
                        if (audioPreview.Play(preview.queuedWave, error))
                            preview.holdSeconds = std::max(preview.holdSeconds, audioPreview.DurationSeconds());
                        else
                            preview.sandboxEvent = error;
                        preview.queuedWave.clear();
                    }
                    if (preview.activeSpeaker >= 0 && previewLipsync)
                        previewLipsync(selected, preview.activeSpeaker,
                                       event.lipsyncAnimationOid >= 0 ? event.lipsyncAnimationOid : 1354,
                                       event.lipsyncAnimationOid >= 0);
                    break;
                default:
                    break;
                }
            }
            if (event.kind == BrxDialogEventKind::Talk)
            {
                const bool timedOut = preview.elapsed >= preview.holdSeconds && !audioPreview.IsPlaying();
                if (!timedOut)
                    break;
                if (event.waitForInput)
                {
                    preview.waitingForInput = true;
                    preview.playing = false;
                    break;
                }
            }
            preview.initialized = false;
            ++preview.eventIndex;
        }
    }

    ImGui::Text("Event %zu / %zu",
                object.dialog.events.empty() ? 0u : std::min(preview.eventIndex + 1u, object.dialog.events.size()),
                object.dialog.events.size());
    ImGui::TextDisabled("Left SPEAKER %d | Right SPEAKER %d | Active SPEAKER %d", preview.leftSpeaker,
                        preview.rightSpeaker, preview.activeSpeaker);
    if (preview.leftStaging >= 0 || preview.rightStaging >= 0)
        ImGui::TextDisabled("Confrontation staging ALO: left %d | right %d", preview.leftStaging, preview.rightStaging);
    if (!preview.subtitle.empty())
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(std::max(240.0f, ImGui::GetContentRegionAvail().x), 92.0f);
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(12, 18, 28, 255), 5.0f);
        if (preview.letterbox)
        {
            draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + 15.0f), IM_COL32(0, 0, 0, 255));
            draw->AddRectFilled(ImVec2(origin.x, origin.y + size.y - 15.0f),
                                ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(0, 0, 0, 255));
        }
        ImU32 subtitleColor = IM_COL32(255, 255, 255, 128);
        const auto activeSpeaker =
            std::find_if(document.Objects().begin(), document.Objects().end(), [&](const BrxObjectRecord &candidate)
                         { return preview.activeSpeaker != -1 && candidate.cid == 102 && candidate.oid == preview.activeSpeaker; });
        if (activeSpeaker != document.Objects().end())
        {
            subtitleColor = IM_COL32(255, 255, 255, 255); // InitSpeaker's default text color.
            if (const auto *color = FindProperty(*activeSpeaker, 988))
                subtitleColor = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(std::clamp(color->floatValue[0], 0.0f, 1.0f), std::clamp(color->floatValue[1], 0.0f, 1.0f),
                           std::clamp(color->floatValue[2], 0.0f, 1.0f), std::clamp(color->floatValue[3], 0.0f, 1.0f)));
        }
        draw->AddText(ImVec2(origin.x + 12.0f, origin.y + 36.0f), subtitleColor, preview.subtitle.c_str());
        ImGui::Dummy(size);
    }
    if (!preview.sandboxEvent.empty())
        ImGui::TextDisabled("%s", preview.sandboxEvent.c_str());
    ImGui::TextDisabled("Gameplay, movie and Splice actions remain sandboxed.");

    ImGui::SeparatorText("Visual timeline");
    if (!audioPreview.PlayingPath().empty())
    {
        ImGui::TextDisabled("Playing: %s", audioPreview.PlayingPath().filename().string().c_str());
        ImGui::SameLine();
        if (audioPreview.IsPaused())
        {
            if (ImGui::SmallButton("Resume"))
                audioPreview.Resume();
        }
        else if (audioPreview.IsPlaying())
        {
            if (ImGui::SmallButton("Pause"))
                audioPreview.Pause();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop audio"))
            audioPreview.Stop();
        float position = audioPreview.PositionSeconds();
        const float duration = audioPreview.DurationSeconds();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##dialogAudioPosition", &position, 0.0f, std::max(duration, 0.01f), "%.2f s"))
            audioPreview.Seek(position);
        ImGui::TextDisabled("%.2f / %.2f seconds", position, duration);
        static float previewVolume = 1.0f;
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::SliderFloat("Dialogue volume", &previewVolume, 0.0f, 1.0f, "%.0f%%"))
            audioPreview.SetVolume(previewVolume);
        const auto &waveform = audioPreview.Waveform();
        if (!waveform.empty())
        {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 size(std::max(180.0f, ImGui::GetContentRegionAvail().x), 54.0f);
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(15, 22, 32, 255), 4.0f);
            for (std::size_t i = 1; i < waveform.size(); ++i)
            {
                const float x0 =
                    origin.x + size.x * static_cast<float>(i - 1) / static_cast<float>(waveform.size() - 1);
                const float x1 = origin.x + size.x * static_cast<float>(i) / static_cast<float>(waveform.size() - 1);
                const float y0 = origin.y + size.y * (0.5f - waveform[i - 1] * 0.45f);
                const float y1 = origin.y + size.y * (0.5f - waveform[i] * 0.45f);
                draw->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(75, 190, 245, 255));
                draw->AddLine(ImVec2(x0, origin.y + size.y - (y0 - origin.y)),
                              ImVec2(x1, origin.y + size.y - (y1 - origin.y)), IM_COL32(75, 190, 245, 255));
            }
            if (duration > 0.0f)
            {
                const float marker = origin.x + size.x * std::clamp(position / duration, 0.0f, 1.0f);
                draw->AddLine(ImVec2(marker, origin.y), ImVec2(marker, origin.y + size.y), IM_COL32(255, 210, 80, 255),
                              2.0f);
            }
            ImGui::Dummy(size);
        }
    }
    std::size_t talkLines = 0;
    std::size_t subtitleBytes = 0;
    std::size_t subtitleCharacters = 0;
    for (const auto &event : object.dialog.events)
        if (event.kind == BrxDialogEventKind::Talk)
        {
            ++talkLines;
            subtitleBytes += event.subtitle.size();
            subtitleCharacters += SubtitleCharacterCount(event.subtitle);
        }
    ImGui::TextDisabled("%zu talk line(s) | %zu subtitle bytes | estimated %.1f s reading time", talkLines,
                        subtitleBytes, static_cast<float>(subtitleCharacters + talkLines * 10u) / 15.0f);
    if (object.dialog.events.empty())
        ImGui::TextDisabled("No events.");
    for (std::size_t eventIndex = 0; eventIndex < object.dialog.events.size(); ++eventIndex)
    {
        const auto &event = object.dialog.events[eventIndex];
        ImGui::PushID(static_cast<int>(eventIndex));
        ImGui::PushStyleColor(ImGuiCol_Button, EventColor(event.kind));
        std::string label = std::to_string(eventIndex + 1) + "  " + EventName(event.kind);
        if (event.kind == BrxDialogEventKind::Talk && !event.subtitle.empty())
            label += "\n" + event.subtitle.substr(0, 54);
        if (ImGui::Button(label.c_str(), ImVec2(-1.0f, event.kind == BrxDialogEventKind::Talk ? 46.0f : 28.0f)))
        {
            renderer.SelectObject(selected);
            status = "DIALOG event " + std::to_string(eventIndex + 1) + ": " + EventName(event.kind);
        }
        if (event.kind == BrxDialogEventKind::PreloadAudio)
        {
            const auto wavePath = audioPreview.ResolveDialogWave(event.resourceName);
            ImGui::BeginDisabled(wavePath.empty());
            if (ImGui::SmallButton("Play WAV"))
            {
                std::string error;
                status = audioPreview.Play(wavePath, error) ? "Playing dialogue WAV: " + wavePath.filename().string()
                                                            : error;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (wavePath.empty())
            {
                auto expected = std::filesystem::path(event.resourceName);
                expected.replace_extension(".wav");
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "Missing %s", expected.filename().string().c_str());
            }
            else
                ImGui::TextDisabled("%s", wavePath.filename().string().c_str());
            const auto matches = audioPreview.MatchCount(event.resourceName);
            if (matches > 1)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.25f, 1.0f), "%zu matching WAVs; using first", matches);
            }
        }
        if (!object.proxyClone && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("DIALOG_EVENT_INDEX", &eventIndex, sizeof(eventIndex));
            ImGui::Text("Move event %zu: %s", eventIndex + 1, EventName(event.kind));
            ImGui::EndDragDropSource();
        }
        if (!object.proxyClone && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("DIALOG_EVENT_INDEX");
                payload != nullptr && payload->DataSize == sizeof(std::size_t))
            {
                const auto sourceIndex = *static_cast<const std::size_t *>(payload->Data);
                if (sourceIndex < object.dialog.events.size() && sourceIndex != eventIndex)
                {
                    auto moved = std::move(object.dialog.events[sourceIndex]);
                    object.dialog.events.erase(object.dialog.events.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
                    const std::size_t destination = sourceIndex < eventIndex ? eventIndex - 1 : eventIndex;
                    object.dialog.events.insert(object.dialog.events.begin() + static_cast<std::ptrdiff_t>(destination),
                                                std::move(moved));
                    object.dialog.modified = true;
                    ResetPreview(preview, audioPreview, selected);
                    status = "Reordered DIALOG timeline.";
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::SeparatorText("Diagnostics");
    const int issues = IssueCount(object);
    if (issues == 0)
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.55f, 1.0f), "No immediate timeline issues.");
    else
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%d issue(s); see Level Validation for details.", issues);
    ImGui::EndChild();
    ImGui::End();
}
