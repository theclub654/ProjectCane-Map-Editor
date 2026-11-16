// Included inside DialogManagerPanel.cpp's helper namespace.
const BrxDialogWaveAsset *FindStagedVoice(const BrxDocument &document, std::uint32_t language,
                                         const std::string &resource)
{
    auto name = std::filesystem::path(resource).filename();
    name.replace_extension(".wav");
    const auto found = std::find_if(document.DialogWaves().begin(), document.DialogWaves().end(), [&](const auto &asset)
    { return asset.languageId == language && asset.resourceName == name.string() && asset.bytes; });
    return found == document.DialogWaves().end() ? nullptr : &*found;
}

void DrawPalVoices(BrxDocument &document, SceneRenderer &renderer, AudioPreviewService &audio, GLFWwindow *window,
                   std::uint32_t languageId, const void *identity, std::string &status)
{
    if (!ImGui::CollapsingHeader("Voice clips (Preload audio events)", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const bool localized = languageId != document.DefaultLanguage();
    struct Voice
    {
        std::uint32_t object, event;
        std::int16_t oid;
        std::string *resource;
        BrxLanguagePayload::VoiceLine *translation;
    };
    std::vector<Voice> voices;
    if (localized)
    {
        std::string error;
        if (!document.LoadLanguageSubtitles(languageId, error))
        { ImGui::TextWrapped("Cannot inspect this language: %s", error.c_str()); return; }
        for (auto &language : document.EditableLanguages())
            if (language.languageId == languageId)
                for (auto &voice : language.voiceLines)
                    voices.push_back({voice.objectIndex, voice.eventIndex, voice.dialogOid, &voice.resourceName, &voice});
    }
    else
        for (std::size_t index = 0; index < document.Objects().size(); ++index)
        {
            auto &object = document.EditableObjects()[index];
            if (!object.dialog.present || object.proxyClone) continue;
            for (std::size_t event = 0; event < object.dialog.events.size(); ++event)
                if (object.dialog.events[event].kind == BrxDialogEventKind::PreloadAudio)
                    voices.push_back({static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(event), object.oid,
                                      &object.dialog.events[event].resourceName, nullptr});
        }
    ImGui::TextWrapped("A Preload audio event selects a whole voice clip and can cover several Talk lines. Replacing it does not regenerate timing or lipsync. Imported WAVs get new filenames; original files and other languages are retained.");
    if (ImGui::Button("Choose extracted voice folder..."))
    {
        const auto root = AudioPreviewService::BrowseForAudioFolder();
        if (!root.empty()) audio.SetAdditionalSearchRoot(root);
    }
    static ImGuiTextFilter filter;
    filter.Draw("Find voice / DIALOG", -1);
    static std::string selectedKey;
    const auto keyFor = [&](const Voice &voice)
    {
        return std::to_string(reinterpret_cast<std::uintptr_t>(identity)) + ":voice:" + std::to_string(languageId) +
               ":" + std::to_string(voice.object) + ":" + std::to_string(voice.event) + ":" + std::to_string(voice.oid);
    };
    if (ImGui::BeginChild("##palVoiceList", ImVec2(0, 115), true))
        for (const auto &voice : voices)
        {
            const auto key = keyFor(voice);
            const std::string label = "DIALOG " + std::to_string(voice.oid) + " | object " + std::to_string(voice.object) +
                " | event " + std::to_string(voice.event + 1) +
                (voice.translation && voice.translation->modified ? " * | " : " | ") + *voice.resource;
            if (!filter.PassFilter(label.c_str())) continue;
            ImGui::PushID(key.c_str());
            if (ImGui::Selectable(label.c_str(), selectedKey == key)) selectedKey = key;
            ImGui::PopID();
        }
    ImGui::EndChild();
    auto selected = std::find_if(voices.begin(), voices.end(), [&](const auto &voice) { return keyFor(voice) == selectedKey; });
    if (selected == voices.end()) return;
    ImGui::PushID(selectedKey.c_str());
    static char resource[33]{};
    static std::string bufferKey, bufferValue;
    if (bufferKey != selectedKey || bufferValue != *selected->resource)
    {
        bufferKey = selectedKey; bufferValue = *selected->resource;
        const auto size = (std::min)(bufferValue.size(), sizeof(resource)-1);
        std::memcpy(resource, bufferValue.data(), size); resource[size] = '\0';
    }
    const auto assign = [&](const std::string &name)
    {
        if (*selected->resource != name) audio.Stop();
        *selected->resource = name;
        if (selected->translation)
            selected->translation->modified = name != selected->translation->originalResourceName;
        else
        {
            auto &dialog = document.EditableObjects()[selected->object].dialog;
            dialog.modified = dialog.events[selected->event].modified = true;
        }
    };
    ImGui::BeginDisabled(!localized && renderer.IsObjectLocked(static_cast<std::int32_t>(selected->object)));
    ImGui::InputText("Voice resource (32 bytes)", resource, sizeof(resource));
    if (ImGui::Button("Apply existing resource"))
    {
        std::string error;
        if (BrxDocument::ValidateDialogVoiceResource(resource, error))
        { assign(resource); status = "Changed this language's Preload audio reference. Existing WAVs remain external files."; }
        else status = error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Import replacement WAV..."))
    {
        const auto path = ChooseAudioFile(window);
        if (!path.empty())
        {
            std::string name, error;
            if (document.ImportDialogWave(languageId, path, name, error))
            { assign(name); status = "Imported a voice clip for " + std::string(BrxDocument::LanguageName(languageId)) + ". Save the project and export the BRX plus its language/Dialogs folder."; }
            else status = error;
        }
    }
    if (selected->translation && selected->translation->modified && ImGui::Button("Restore original voice reference"))
        assign(selected->translation->originalResourceName);
    ImGui::EndDisabled();
    const auto *imported = FindStagedVoice(document, languageId, *selected->resource);
    const auto path = audio.ResolveDialogWave(*selected->resource);
    ImGui::BeginDisabled(imported == nullptr && path.empty());
    if (ImGui::Button("Play selected voice"))
    {
        std::string error;
        status = (imported ? audio.PlayDialogWaveBytes(*imported->bytes, error) : audio.Play(path, error))
            ? "Playing " + std::string(BrxDocument::LanguageName(languageId)) + " voice clip." : error;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Stop voice")) audio.Stop();
    if (imported) ImGui::TextDisabled("Imported WAV is stored in the editor project (%zu bytes).", imported->bytes->size());
    else if (path.empty()) ImGui::TextWrapped("Missing voice in this language's Dialogs/Commentary folders. No other language is used as a fallback.");
    else ImGui::TextWrapped("%s", path.string().c_str());
    ImGui::PopID();
}
