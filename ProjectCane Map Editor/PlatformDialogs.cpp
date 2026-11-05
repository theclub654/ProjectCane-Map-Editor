#include "PlatformDialogs.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include "Resources/AppIcon.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>

void SetWindowsApplicationIcon(GLFWwindow *window)
{
    if (!window)
        return;
    const HWND nativeWindow = glfwGetWin32Window(window);
    const HINSTANCE application = GetModuleHandleW(nullptr);
    // LR_SHARED keeps these resource handles valid for the process lifetime.
    // Load both sizes so the title bar and Alt-Tab/taskbar use the embedded icon
    // without relying on the working directory or an external PNG file.
    const auto largeIcon = static_cast<HICON>(LoadImageW(application, MAKEINTRESOURCEW(IDI_PROJECTCANE), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    const auto smallIcon = static_cast<HICON>(LoadImageW(application, MAKEINTRESOURCEW(IDI_PROJECTCANE), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (largeIcon)
        SendMessageW(nativeWindow, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
    if (smallIcon)
        SendMessageW(nativeWindow, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
}

std::filesystem::path ChooseBrxFile(GLFWwindow *window)
{
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"ProjectCane levels (*.brx)\0*.brx\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseEditorProjectFile(GLFWwindow *window)
{
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"ProjectCane editor projects (*.caneproject)\0*.caneproject\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseHierarchyFile(GLFWwindow *window)
{
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"ProjectCane hierarchy packages (*.canehierarchy)\0*.canehierarchy\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseHierarchySaveFile(GLFWwindow *window, const std::string &suggestedName)
{
    wchar_t fileName[32768]{};
    std::wstring suggested(suggestedName.begin(), suggestedName.end());
    suggested += L".canehierarchy";
    std::copy_n(suggested.data(), (std::min)(suggested.size(), std::size(fileName) - 1), fileName);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"ProjectCane hierarchy packages (*.canehierarchy)\0*.canehierarchy\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrDefExt = L"canehierarchy";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseBrxSaveFile(GLFWwindow *window, const std::filesystem::path &sourcePath)
{
    wchar_t fileName[32768]{};
    const std::wstring suggested = sourcePath.stem().wstring() + L".roundtrip.brx";
    const std::wstring initialDirectory = sourcePath.parent_path().wstring();
    const auto copyCount = (std::min)(suggested.size(), std::size(fileName) - 1);
    std::copy_n(suggested.data(), copyCount, fileName);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"ProjectCane levels (*.brx)\0*.brx\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrDefExt = L"brx";
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseTextureFile(GLFWwindow *window)
{
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter =
        L"Images (*.png;*.bmp;*.jpg;*.jpeg;*.tif;*.tiff)\0*.png;*.bmp;*.jpg;*.jpeg;*.tif;*.tiff\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseGlbSaveFile(GLFWwindow *window, const std::filesystem::path &sourcePath, const bool baked)
{
    wchar_t fileName[32768]{};
    const std::wstring suggested = sourcePath.stem().wstring() + (baked ? L"_Baked.glb" : L".glb");
    std::copy_n(suggested.data(), (std::min)(suggested.size(), std::size(fileName) - 1), fileName);
    const std::wstring initialDirectory = sourcePath.parent_path().wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"Binary glTF (*.glb)\0*.glb\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrDefExt = L"glb";
    dialog.lpstrInitialDir = initialDirectory.empty() ? nullptr : initialDirectory.c_str();
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseCharacterGlbSaveFile(GLFWwindow *window, const std::string &suggestedName)
{
    wchar_t fileName[32768]{};
    std::wstring suggested(suggestedName.begin(), suggestedName.end());
    suggested += L"_Rigged.glb";
    std::copy_n(suggested.data(), (std::min)(suggested.size(), std::size(fileName) - 1), fileName);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window != nullptr ? glfwGetWin32Window(window) : nullptr;
    dialog.lpstrFilter = L"Rigged binary glTF (*.glb)\0*.glb\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrDefExt = L"glb";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseNamedGlbSaveFile(GLFWwindow *window, const std::string &suggestedName)
{
    wchar_t fileName[32768]{};
    std::wstring suggested(suggestedName.begin(), suggestedName.end());
    if (std::filesystem::path(suggested).extension() != L".glb")
        suggested += L".glb";
    std::copy_n(suggested.data(), (std::min)(suggested.size(), std::size(fileName) - 1), fileName);
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window != nullptr ? glfwGetWin32Window(window) : nullptr;
    dialog.lpstrFilter = L"Binary glTF (*.glb)\0*.glb\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrDefExt = L"glb";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseExportFolder(GLFWwindow *window, const wchar_t *title)
{
    BROWSEINFOW info{};
    info.hwndOwner = glfwGetWin32Window(window);
    info.lpszTitle = title;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (!item)
        return {};
    wchar_t path[MAX_PATH]{};
    const bool ok = SHGetPathFromIDListW(item, path) != FALSE;
    CoTaskMemFree(item);
    return ok ? std::filesystem::path(path) : std::filesystem::path{};
}

std::filesystem::path ChooseStaticModelFile(GLFWwindow *window)
{
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter =
        L"Static models (*.fbx;*.obj;*.glb;*.gltf;*.dae;*.ply;*.stl)\0*.fbx;*.obj;*.glb;*.gltf;*.dae;*.ply;*.stl\0Autodesk FBX (*.fbx)\0*.fbx\0Wavefront OBJ (*.obj)\0*.obj\0glTF (*.glb;*.gltf)\0*.glb;*.gltf\0COLLADA (*.dae)\0*.dae\0Polygon File Format (*.ply)\0*.ply\0Stereolithography (*.stl)\0*.stl\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseAudioFile(GLFWwindow *window)
{
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"Wave audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrDefExt = L"wav";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseSoundBankFile(GLFWwindow *window)
{
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"989SND effect banks (*.bnk)\0*.bnk\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrDefExt = L"bnk";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseCutsceneFile(GLFWwindow *window)
{
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"ProjectCane cutscenes (*.m2v)\0*.m2v\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrDefExt = L"m2v";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

std::filesystem::path ChooseExecutableFile(GLFWwindow *window)
{
    wchar_t fileName[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFilter = L"Applications (*.exe)\0*.exe\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName;
    dialog.nMaxFile = static_cast<DWORD>(std::size(fileName));
    dialog.lpstrDefExt = L"exe";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(fileName) : std::filesystem::path{};
}

bool LaunchExecutable(const std::filesystem::path &executable)
{
    if (executable.empty())
        return false;
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, executable.parent_path().c_str(), SW_SHOWNORMAL));
    return result > 32;
}
#else
#include "PlatformDialogsLinux.inc"
#endif
