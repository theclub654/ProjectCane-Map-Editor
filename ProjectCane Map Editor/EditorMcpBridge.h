#pragma once

#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

// The companion owns MCP protocol handling. This local mailbox is drained only
// by the UI thread: no background thread ever holds document or GL references.
class EditorMcpBridge
{
  public:
    using Json = nlohmann::json;
    EditorMcpBridge() = default;
    EditorMcpBridge(const EditorMcpBridge &) = delete;
    EditorMcpBridge &operator=(const EditorMcpBridge &) = delete;
    bool enabled = false;
    bool allowEdits = false;
    bool allowFileWrites = false;
    bool showPanel = false;
    std::string lastActivity = "Disconnected";
    std::string session;
    std::filesystem::path directory;

    static std::string NewSession()
    {
        std::random_device random;
        std::string result;
        constexpr char hex[] = "0123456789abcdef";
        for (int i = 0; i < 32; ++i)
            result += hex[random() & 15u];
        return result;
    }

    bool Start()
    {
        if (enabled) return true;
        try
        {
            session = NewSession();
            directory = std::filesystem::temp_directory_path() / "projectcane-mcp" / session;
            std::filesystem::create_directories(directory);
#ifndef _WIN32
            std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                                         std::filesystem::perm_options::replace);
#endif
            enabled = true;
            allowEdits = false;
            allowFileWrites = false;
            Publish();
            lastActivity = "Connected locally; read-only";
            return true;
        }
        catch (const std::exception &error)
        {
            enabled = false;
            lastActivity = error.what();
            return false;
        }
    }

    void Stop()
    {
        enabled = false;
        allowEdits = false;
        allowFileWrites = false;
        std::error_code ignored;
        if (!directory.empty())
        {
            std::filesystem::remove(directory / "session.json", ignored);
            // Responses can contain UI images. Delete only this protocol's
            // flat mailbox files, never recurse through a directory tree.
            try
            {
                for (const auto &entry : std::filesystem::directory_iterator(directory))
                {
                    const auto name = entry.path().filename().string();
                    const auto id = name.substr(0, (std::min)(name.size(), std::size_t{32}));
                    if (!entry.is_symlink() && entry.is_regular_file() && id.size() == 32 &&
                        id.find_first_not_of("0123456789abcdef") == std::string::npos &&
                        (name == id + ".request.json" || name == id + ".response.json" || name == id + ".tmp" || name == id + ".response.json.tmp"))
                        std::filesystem::remove(entry.path(), ignored);
                }
                std::filesystem::remove(directory / "session.json.tmp", ignored);
                std::filesystem::remove(directory, ignored); // succeeds only if empty
            }
            catch (...) {} // Disconnect/destruction must not throw on stale files.
        }
        lastActivity = "Disconnected";
    }

    ~EditorMcpBridge() { Stop(); }

    void Publish()
    {
        Write(directory / "session.json", {{"session", session}, {"protocol", 1},
              {"allow_edits", allowEdits}, {"application", "ProjectCane Map Editor"}});
        lastPublish_ = std::chrono::steady_clock::now();
    }

    void Poll(const std::function<Json(const Json &)> &handler)
    {
        if (!enabled)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastPoll_ < std::chrono::milliseconds(50))
            return;
        lastPoll_ = now;
        try
        {
            if (now - lastPublish_ > std::chrono::seconds(2))
                Publish();
            // At most one bounded request per frame. A cancelled/timed-out
            // request is removed by the companion and is never replayed.
            for (const auto &entry : std::filesystem::directory_iterator(directory))
            {
                const auto name = entry.path().filename().string();
                if (name.ends_with(".response.json") && !entry.is_symlink() && entry.is_regular_file() &&
                    std::filesystem::file_time_type::clock::now() - entry.last_write_time() > std::chrono::minutes(2))
                {
                    std::error_code ignored; std::filesystem::remove(entry.path(), ignored);
                    continue;
                }
                if (!name.ends_with(".request.json") || !entry.is_regular_file() || entry.is_symlink())
                    continue;
                const std::string id = name.substr(0, name.size() - 13);
                if (id.size() != 32 || id.find_first_not_of("0123456789abcdef") != std::string::npos)
                    continue;
                Json response;
                try
                {
                    if (entry.file_size() > 1024u * 1024u)
                        throw std::runtime_error("Request exceeds 1 MiB");
                    std::ifstream stream(entry.path(), std::ios::binary);
                    const Json request = Json::parse(stream);
                    if (request.value("session", "") != session)
                        throw std::runtime_error("Editor session changed; reconnect");
                    const auto deadline = request.at("deadline_ms").get<std::int64_t>();
                    const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    if (deadline < time || deadline > time + 60000)
                        throw std::runtime_error("Request expired or deadline invalid");
                    response = {{"ok", true}, {"result", handler(request.at("request"))}};
                    lastActivity = request.at("request").value("command", "request") + ": completed";
                }
                catch (const std::exception &error)
                {
                    response = {{"ok", false}, {"error", error.what()}};
                    lastActivity = error.what();
                }
                std::error_code ignored;
                std::filesystem::remove(entry.path(), ignored);
                if (std::chrono::steady_clock::now() - lastPublish_ > std::chrono::seconds(2)) Publish();
                Write(directory / (id + ".response.json"), response);
                break;
            }
        }
        catch (const std::exception &error) { lastActivity = error.what(); }
    }

  private:
    std::chrono::steady_clock::time_point lastPoll_{}, lastPublish_{};
    static void Write(const std::filesystem::path &path, const Json &value)
    {
        auto staging = path;
        staging += ".tmp";
        {
            std::ofstream stream(staging, std::ios::binary | std::ios::trunc);
            stream << value.dump(-1, ' ', false, Json::error_handler_t::replace);
            stream.flush();
            if (!stream)
                throw std::runtime_error("Cannot write MCP response");
        }
        // Windows rename cannot replace an existing destination.
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::rename(staging, path);
    }
};
