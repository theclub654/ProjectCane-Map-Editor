// GTK is confined to this platform implementation; GLFW owns the editor window.
#include <gtk/gtk.h>
#include <initializer_list>
#include <string>

namespace
{
std::filesystem::path LinuxChooseFile(const char *title, const GtkFileChooserAction action,
                                    const std::initializer_list<const char *> patterns = {},
                                    const std::filesystem::path &suggested = {})
{
    // Native numeric parsers use the C decimal separator. Do not let opening
    // a GTK dialog silently change the process-wide numeric locale.
    gtk_disable_setlocale();
    if (!gtk_init_check(nullptr, nullptr))
        return {};
    GtkWidget *dialog = gtk_file_chooser_dialog_new(title, nullptr, action,
        "Cancel", GTK_RESPONSE_CANCEL, action == GTK_FILE_CHOOSER_ACTION_SAVE ? "Save" : "Open",
        GTK_RESPONSE_ACCEPT, nullptr);
    auto *chooser = GTK_FILE_CHOOSER(dialog);
    if (action == GTK_FILE_CHOOSER_ACTION_SAVE)
    {
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
        if (!suggested.empty())
            gtk_file_chooser_set_current_name(chooser, suggested.filename().c_str());
    }
    if (!suggested.parent_path().empty())
        gtk_file_chooser_set_current_folder(chooser, suggested.parent_path().c_str());
    if (patterns.size() != 0)
    {
        auto *filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, "Supported files");
        for (const auto *pattern : patterns)
            gtk_file_filter_add_pattern(filter, pattern);
        gtk_file_chooser_add_filter(chooser, filter);
        auto *all = gtk_file_filter_new();
        gtk_file_filter_set_name(all, "All files");
        gtk_file_filter_add_pattern(all, "*");
        gtk_file_chooser_add_filter(chooser, all);
    }
    std::filesystem::path result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        char *filename = gtk_file_chooser_get_filename(chooser);
        if (filename)
        {
            result = filename;
            g_free(filename);
            if (action == GTK_FILE_CHOOSER_ACTION_SAVE && result.extension().empty() &&
                !suggested.extension().empty())
            {
                result += suggested.extension().string();
                std::error_code existsError;
                if (std::filesystem::exists(result, existsError))
                {
                    auto *confirm = gtk_message_dialog_new(GTK_WINDOW(dialog), GTK_DIALOG_MODAL,
                        GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Replace existing file '%s'?", result.c_str());
                    if (gtk_dialog_run(GTK_DIALOG(confirm)) != GTK_RESPONSE_YES)
                        result.clear();
                    gtk_widget_destroy(confirm);
                }
                else if (existsError)
                    result.clear();
            }
        }
    }
    gtk_widget_destroy(dialog);
    while (gtk_events_pending())
        gtk_main_iteration();
    return result;
}
}

std::filesystem::path ChooseBrxFile(GLFWwindow *)
{ return LinuxChooseFile("Open ProjectCane level", GTK_FILE_CHOOSER_ACTION_OPEN, {"*.brx", "*.BRX"}); }
std::filesystem::path ChooseEditorProjectFile(GLFWwindow *)
{ return LinuxChooseFile("Open editor project", GTK_FILE_CHOOSER_ACTION_OPEN, {"*.caneproject"}); }
std::filesystem::path ChooseHierarchyFile(GLFWwindow *)
{ return LinuxChooseFile("Open hierarchy package", GTK_FILE_CHOOSER_ACTION_OPEN, {"*.canehierarchy"}); }
std::filesystem::path ChooseHierarchySaveFile(GLFWwindow *, const std::string &name)
{ return LinuxChooseFile("Save hierarchy package", GTK_FILE_CHOOSER_ACTION_SAVE, {"*.canehierarchy"}, name + ".canehierarchy"); }
std::filesystem::path ChooseBrxSaveFile(GLFWwindow *, const std::filesystem::path &source)
{ return LinuxChooseFile("Export ProjectCane level", GTK_FILE_CHOOSER_ACTION_SAVE, {"*.brx"}, source.parent_path() / (source.stem().string() + ".roundtrip.brx")); }
std::filesystem::path ChooseGlbSaveFile(GLFWwindow *, const std::filesystem::path &source, const bool baked)
{ return LinuxChooseFile("Export binary glTF", GTK_FILE_CHOOSER_ACTION_SAVE, {"*.glb"}, source.parent_path() / (source.stem().string() + (baked ? "_Baked.glb" : ".glb"))); }
std::filesystem::path ChooseCharacterGlbSaveFile(GLFWwindow *, const std::string &name)
{ return LinuxChooseFile("Export rigged glTF", GTK_FILE_CHOOSER_ACTION_SAVE, {"*.glb"}, name + "_Rigged.glb"); }
std::filesystem::path ChooseNamedGlbSaveFile(GLFWwindow *, const std::string &name)
{
    auto suggested = std::filesystem::path(name);
    if (suggested.extension() != ".glb") suggested += ".glb";
    return LinuxChooseFile("Export binary glTF", GTK_FILE_CHOOSER_ACTION_SAVE, {"*.glb"}, suggested);
}
std::filesystem::path ChooseExportFolder(GLFWwindow *, const wchar_t *title)
{
    std::string caption;
    if (title)
        for (; *title; ++title) caption.push_back(*title < 128 ? static_cast<char>(*title) : '?');
    return LinuxChooseFile(caption.empty() ? "Choose export folder" : caption.c_str(), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
}
std::filesystem::path ChooseTextureFile(GLFWwindow *)
{ return LinuxChooseFile("Import texture", GTK_FILE_CHOOSER_ACTION_OPEN, {"*.png", "*.bmp", "*.jpg", "*.jpeg", "*.tif", "*.tiff", "*.PNG", "*.JPG"}); }
std::filesystem::path ChooseStaticModelFile(GLFWwindow *)
{ return LinuxChooseFile("Import model", GTK_FILE_CHOOSER_ACTION_OPEN, {"*.fbx", "*.FBX", "*.obj", "*.glb", "*.gltf", "*.dae", "*.ply", "*.stl"}); }
std::filesystem::path ChooseAudioFile(GLFWwindow *)
{ return LinuxChooseFile("Open WAV audio", GTK_FILE_CHOOSER_ACTION_OPEN, {"*.wav", "*.WAV"}); }
std::filesystem::path ChooseSoundBankFile(GLFWwindow *)
{ return LinuxChooseFile("Open effect bank", GTK_FILE_CHOOSER_ACTION_OPEN, {"*.bnk", "*.BNK"}); }
std::filesystem::path ChooseCutsceneFile(GLFWwindow *)
{ return LinuxChooseFile("Open cutscene", GTK_FILE_CHOOSER_ACTION_OPEN, {"*.m2v", "*.M2V"}); }
std::filesystem::path ChooseExecutableFile(GLFWwindow *)
{ return LinuxChooseFile("Choose Linux executable", GTK_FILE_CHOOSER_ACTION_OPEN); }
bool LaunchExecutable(const std::filesystem::path &executable)
{
    if (executable.empty()) return false;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(executable, error);
    if (error) return false;
    const auto filename = absolute.string();
    char *arguments[]{const_cast<char *>(filename.c_str()), nullptr};
    GError *failure = nullptr;
    const bool launched = g_spawn_async(absolute.parent_path().c_str(), arguments, nullptr,
        G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &failure);
    if (failure) g_error_free(failure);
    return launched;
}
