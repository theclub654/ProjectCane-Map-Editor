#include "EditorMcpFiles.h"
#include "EditorWorkspace.h"
#include "EditorProject.h"
#include "EditorMcpOperations.h"
#include "MapExport.h"
#include <cctype>

namespace EditorMcp
{
Json FileOperations()
{
    return Json::array({
        {{"name", "save_project"}, {"parameters", "absolute .caneproject output path"}, {"handler", "EditorProject::Save"}},
        {{"name", "save_hierarchy"}, {"parameters", "index, absolute output path"}, {"handler", "EditorProject::SaveHierarchy"}},
        {{"name", "save_proxy"}, {"parameters", "index, absolute output path"}, {"handler", "EditorProject::SavePortableProxy"}},
        {{"name", "export_brx"}, {"parameters", "absolute .brx output path"}, {"handler", "BrxCompiler::WriteRebuiltPayload"}},
        {{"name", "export_map_glb"}, {"parameters", "absolute .glb output path, bake_lights=false"}, {"handler", "ExportMapGlb"}},
        {{"name", "export_glob_glb"}, {"parameters", "resource_index, absolute .glb output path"}, {"handler", "ExportGlobGlb"}},
        {{"name", "export_globset_glb"}, {"parameters", "resource_index, absolute .glb output path"}, {"handler", "ExportGlobsetGlb"}},
        {{"name", "export_textures"}, {"parameters", "absolute NEW folder path"}, {"handler", "ExportMapTextures"}}
    });
}
std::filesystem::path OutputPath(const Json &a)
{
    const auto path = PathFromUtf8(a.at("path").get<std::string>()).lexically_normal();
    if (!path.is_absolute() || !std::filesystem::is_directory(path.parent_path()) ||
        std::filesystem::exists(path) || std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
        throw std::runtime_error("Choose an absolute, unused output filename in an existing folder; MCP does not overwrite existing files");
    return path;
}
std::filesystem::path ValidateFileRequest(const BrxDocument &doc, const std::string &operation, const Json &a)
{
    if (!doc.IsOpen()) throw std::runtime_error("Open a map first");
    const auto catalog = FileOperations();
    if (std::none_of(catalog.begin(), catalog.end(), [&](const auto &row) { return row.at("name") == operation; }))
        throw std::runtime_error("Unknown file operation");
    const auto destination = OutputPath(a);
    auto extension = destination.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if ((operation == "save_project" && extension != ".caneproject") ||
        (operation == "export_brx" && extension != ".brx") ||
        (operation.starts_with("export_") && operation != "export_brx" && operation != "export_textures" && extension != ".glb"))
        throw std::runtime_error("Output extension does not match operation");
    // Saving reads the selected hierarchy; it does not edit a resolved clone.
    // SavePortableProxy explicitly accepts both wrappers and their members.
    if (operation == "save_hierarchy" || operation == "save_proxy") doc.Objects().at(Integer(a, "index"));
    if (operation == "export_glob_glb") doc.Globs().at(Integer(a, "resource_index"));
    if (operation == "export_globset_glb") doc.Globsets().at(Integer(a, "resource_index"));
    if (a.contains("bake_lights") && !a.at("bake_lights").is_boolean()) throw std::runtime_error("bake_lights must be boolean");
    return destination;
}
Json WriteFile(const BrxDocument &doc, const std::string &operation, const Json &a)
{
    const auto destination = ValidateFileRequest(doc, operation, a);
    if (operation == "export_textures")
    {
        if (!std::filesystem::create_directory(destination)) throw std::runtime_error("Texture output folder already exists");
        std::size_t written = 0; std::string error;
        if (!ExportMapTextures(doc, destination, written, error)) throw std::runtime_error(error + "; new output folder retained for inspection");
        return {{"path", Utf8Path(destination)}, {"written", written}, {"document_changed", false}};
    }
    // Publish only a successful file. A hard link is an atomic, no-replace
    // publication on supported filesystems; an existing destination is never
    // deleted or overwritten, even if it appeared after the preview.
    const auto staging = destination.parent_path() / (".mcp-" + EditorMcpBridge::NewSession() + destination.extension().string());
    std::string error; bool ok = false;
    try
    {
        if (operation == "save_project") ok = EditorProject::Save(staging, doc, error);
        else if (operation == "save_hierarchy") ok = EditorProject::SaveHierarchy(staging, doc, Integer(a, "index"), error);
        else if (operation == "save_proxy") ok = EditorProject::SavePortableProxy(staging, doc, Integer(a, "index"), error);
        else if (operation == "export_map_glb") ok = ExportMapGlb(doc, staging, a.value("bake_lights", false), error);
        else if (operation == "export_glob_glb") { const int i = Integer(a, "resource_index"); doc.Globs().at(i); ok = ExportGlobGlb(doc, i, staging, error); }
        else if (operation == "export_globset_glb") { const int i = Integer(a, "resource_index"); doc.Globsets().at(i); ok = ExportGlobsetGlb(doc, i, staging, error); }
        else if (operation == "export_brx")
        {
            EditorWorkspace audit; audit.Refresh(doc); audit.Validation().Run(doc, audit.Scene());
            ok = audit.Compiler().WriteRebuiltPayload(doc, audit.Validation(), staging, error);
        }
        else throw std::runtime_error("Unknown file operation");
        if (!ok) throw std::runtime_error(error.empty() ? "Output rejected by native writer" : error);
        std::filesystem::create_hard_link(staging, destination);
        std::error_code ignored; std::filesystem::remove(staging, ignored);
    }
    catch (...)
    {
        std::error_code ignored; std::filesystem::remove(staging, ignored);
        throw;
    }
    return {{"path", Utf8Path(destination)}, {"bytes", std::filesystem::file_size(destination)}, {"document_changed", false}};
}
}
