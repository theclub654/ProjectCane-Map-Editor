#include "SoundManagerPanel.h"
#include "AudioPreviewService.h"
#include "BrxDocument.h"
#include "EditorWorkspace.h"
#include "PlatformDialogs.h"
#include "SceneRenderer.h"
#include "SoundBank.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>

namespace
{
constexpr std::array<int, 8> kStarts{0, 169, 234, 370, 491, 593, 691, 771};
constexpr std::array<const char *, 7> kNames{"Common.bnk", "Intro.bnk", "Underwater.bnk", "Muggshot.bnk",
                                             "Voodoo.bnk", "Snow.bnk",  "Clockwerk.bnk"};
std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
int RetailIndex(const std::filesystem::path &path)
{
    const auto name = Lower(path.filename().string());
    for (int i = 0; i < int(kNames.size()); ++i)
        if (name == Lower(kNames[i]))
            return i;
    return -1;
}
const char *GrainName(std::uint32_t type)
{
    switch (type)
    {
    case 1:
        return "Tone";
    case 2:
        return "Xref ID";
    case 3:
        return "Xref number";
    case 4:
        return "LFO";
    case 5:
        return "Start child";
    case 6:
        return "Stop child";
    case 7:
        return "Plugin";
    case 8:
        return "Branch";
    case 9:
        return "Tone 2";
    default:
        return "Unknown";
    }
}
}

void DrawSoundManagerPanel(bool &open, BrxDocument &document, EditorWorkspace &, SceneRenderer &, GLFWwindow *window,
                           std::string &status)
{
    if (!open || !document.IsOpen())
        return;
    ImGui::SetNextWindowSize(ImVec2(1050, 720), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Sound Manager", &open))
    {
        ImGui::End();
        return;
    }
    static SoundBank bank;
    static AudioPreviewService audio;
    static ImGuiTextFilter filter;
    static int selected = 0;
    ImGui::TextColored(ImVec4(.35f, 1, .5f, 1), "Arbitrary 989SND bank browser");
    ImGui::TextWrapped(
        "Open any valid type-3 .bnk. Compatibility is determined from its binary SBlk metadata, never from a hardcoded filename list.");
    if (ImGui::Button("Open BNK..."))
    {
        const auto path = ChooseSoundBankFile(window);
        if (!path.empty())
        {
            std::string error;
            if (bank.Open(path, error))
            {
                selected = 0;
                status = "Opened " + path.filename().string() + ".";
            }
            else
                status = error;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Level bank: %s", document.WorldHeader().soundBankFile.c_str());
    if (!bank.IsOpen())
    {
        ImGui::TextDisabled("No bank open.");
        ImGui::End();
        return;
    }

    const int retail = RetailIndex(bank.Path());
    const bool mapped = retail >= 0 && int(bank.Sounds().size()) == kStarts[retail + 1] - kStarts[retail];
    ImGui::SeparatorText("Bank metadata");
    ImGui::TextWrapped("%s", bank.Path().string().c_str());
    ImGui::Text("Bank ID %u | version %u | %zu sounds | %u sample bytes", bank.BankId(), bank.Version(),
                bank.Sounds().size(), bank.SampleSize());
    if (mapped)
        ImGui::TextColored(ImVec4(.4f, .85f, 1, 1), "Known retail mapping: global base %d", kStarts[retail]);
    else
        ImGui::TextDisabled("Local-ID mode: no global SFX mapping is invented for this bank.");

    filter.Draw("Search", 300);
    ImGui::BeginChild("sounds", ImVec2(380, 500), true);
    for (int i = 0; i < int(bank.Sounds().size()); ++i)
    {
        std::string label =
            "Sound " + std::to_string(i) + " | " + std::to_string(bank.Sounds()[i].grains.size()) + " grain(s)";
        if (mapped)
            label += " | global " + std::to_string(kStarts[retail] + i);
        if (filter.PassFilter(label.c_str()) && ImGui::Selectable(label.c_str(), selected == i))
            selected = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("details", ImVec2(0, 500), true);
    if (selected >= 0 && selected < int(bank.Sounds().size()))
    {
        const auto &sound = bank.Sounds()[selected];
        ImGui::Text("Local sound %d", selected);
        ImGui::Text("Volume %d | group %d | pan %d | limit %d | flags 0x%04X%s", int(sound.volume),
                    int(sound.volumeGroup), int(sound.pan), int(sound.instanceLimit), unsigned(sound.flags),
                    (sound.flags & 1) ? " | looper" : "");
        ImGui::SeparatorText("Grains");
        for (int i = 0; i < int(sound.grains.size()); ++i)
        {
            const auto &grain = sound.grains[i];
            ImGui::PushID(i);
            if (ImGui::TreeNode("grain", "Grain %d: %s (type %u), delay %d", i, GrainName(grain.type), grain.type,
                                grain.delay))
            {
                if (grain.hasTone)
                {
                    const auto &t = grain.tone;
                    ImGui::Text("Volume %d | pan %d | priority %d | note %d.%u", int(t.volume), int(t.pan),
                                int(t.priority), int(t.centerNote), unsigned(t.centerFine));
                    ImGui::Text("Map %d..%d | bend %d..%d", int(t.mapLow), int(t.mapHigh), int(t.pitchBendLow),
                                int(t.pitchBendHigh));
                    ImGui::Text("ADSR %04X/%04X | flags %04X", t.adsr1, t.adsr2, t.flags);
                    ImGui::Text("Sample range [%u, %u)", t.sampleOffset, t.sampleEndOffset);
                }
                if (grain.hasTone)
                {
                    if (ImGui::Button("Play tone"))
                    {
                        const auto wave = std::filesystem::temp_directory_path() /
                                          ("ProjectCane_BNK_" + std::to_string(bank.BankId()) + "_" +
                                           std::to_string(selected) + "_" + std::to_string(i) + ".wav");
                        std::string error;
                        if (bank.ExportToneWave(std::size_t(selected), std::size_t(i), wave, error))
                            status = audio.Play(wave, error) ? "Playing bank tone." : error;
                        else
                            status = error;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Stop"))
                        audio.Stop();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::SeparatorText("ID semantics");
        if (mapped)
            ImGui::Text("This sound's ProjectCane global SFX ID is %d.", kStarts[retail] + selected);
        else
            ImGui::TextWrapped(
                "This is local sound %d. Arbitrary banks remain fully inspectable, but writing this number into a BRX property would be unsafe until the bank is explicitly mapped into the game's global SFX table.",
                selected);
    }
    ImGui::EndChild();
    ImGui::End();
}
