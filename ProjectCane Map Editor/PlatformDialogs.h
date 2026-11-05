#pragma once

#include <filesystem>
#include <string>

struct GLFWwindow;

#ifdef _WIN32
void SetWindowsApplicationIcon(GLFWwindow *window);
#endif

[[nodiscard]] std::filesystem::path ChooseBrxFile(GLFWwindow *window);
[[nodiscard]] std::filesystem::path ChooseEditorProjectFile(GLFWwindow *window);
[[nodiscard]] std::filesystem::path ChooseHierarchyFile(GLFWwindow *window);
[[nodiscard]] std::filesystem::path ChooseHierarchySaveFile(GLFWwindow *window, const std::string &suggestedName);
[[nodiscard]] std::filesystem::path ChooseBrxSaveFile(GLFWwindow *window, const std::filesystem::path &sourcePath);
[[nodiscard]] std::filesystem::path ChooseGlbSaveFile(GLFWwindow *window, const std::filesystem::path &sourcePath,
                                                      bool baked);
[[nodiscard]] std::filesystem::path ChooseCharacterGlbSaveFile(GLFWwindow *window, const std::string &suggestedName);
[[nodiscard]] std::filesystem::path ChooseNamedGlbSaveFile(GLFWwindow *window, const std::string &suggestedName);
[[nodiscard]] std::filesystem::path ChooseExportFolder(GLFWwindow *window, const wchar_t *title);
[[nodiscard]] std::filesystem::path ChooseTextureFile(GLFWwindow *window);
[[nodiscard]] std::filesystem::path ChooseStaticModelFile(GLFWwindow *window);
[[nodiscard]] std::filesystem::path ChooseAudioFile(GLFWwindow *window);
[[nodiscard]] std::filesystem::path ChooseSoundBankFile(GLFWwindow *window);
[[nodiscard]] std::filesystem::path ChooseCutsceneFile(GLFWwindow *window);
[[nodiscard]] std::filesystem::path ChooseExecutableFile(GLFWwindow *window);
[[nodiscard]] bool LaunchExecutable(const std::filesystem::path &executable);
