#include "MapExport.h"
#include "BrxDocument.h"
#include <nlohmann/json.hpp>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996) // stb_image_write uses sprintf internally.
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
using json = nlohmann::json;
namespace
{
void Align(std::vector<std::uint8_t> &b)
{
    while (b.size() & 3)
        b.push_back(0);
}
template <class T> void Put(std::vector<std::uint8_t> &b, const T &v)
{
    const auto *p = reinterpret_cast<const std::uint8_t *>(&v);
    b.insert(b.end(), p, p + sizeof(T));
}
std::vector<std::uint8_t> Decode(const BrxBitmapRecord &bmp)
{
    const std::size_t count = static_cast<std::size_t>(bmp.width) * bmp.height;
    std::vector<std::uint8_t> out(count * 4);
    if (!count)
        return {};
    if (bmp.textureFormat == 0)
    {
        if (bmp.pixels.size() < out.size())
            return {};
        std::copy_n(bmp.pixels.begin(), out.size(), out.begin());
        return out;
    }
    const std::size_t blockBytes = bmp.textureFormat == 1 ? 8 : 16, blocksX = (bmp.width + 3) / 4,
                      blocksY = (bmp.height + 3) / 4;
    if (bmp.pixels.size() < blocksX * blocksY * blockBytes)
        return {};
    auto rgb = [](std::uint16_t c)
    { return glm::u8vec3(((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31); };
    for (std::size_t by = 0; by < blocksY; ++by)
        for (std::size_t bx = 0; bx < blocksX; ++bx)
        {
            const auto *p = bmp.pixels.data() + (by * blocksX + bx) * blockBytes;
            std::uint8_t alpha[16];
            std::fill(std::begin(alpha), std::end(alpha), 255);
            const std::uint8_t *cp = p;
            if (bmp.textureFormat == 2)
            {
                std::uint8_t a[8]{p[0], p[1]};
                for (int i = 2; i < 8; ++i)
                    a[i] = a[0] > a[1] ? static_cast<std::uint8_t>(((8 - i) * a[0] + (i - 1) * a[1]) / 7)
                                       : (i < 6 ? static_cast<std::uint8_t>(((6 - i) * a[0] + (i - 1) * a[1]) / 5)
                                                : (i == 6 ? 0 : 255));
                std::uint64_t bits = 0;
                for (int i = 0; i < 6; ++i)
                    bits |= std::uint64_t(p[2 + i]) << (8 * i);
                for (int i = 0; i < 16; ++i)
                    alpha[i] = a[(bits >> (3 * i)) & 7];
                cp = p + 8;
            }
            std::uint16_t c0{}, c1{};
            std::memcpy(&c0, cp, 2);
            std::memcpy(&c1, cp + 2, 2);
            glm::u8vec3 c[4]{rgb(c0), rgb(c1)};
            if (bmp.textureFormat == 1 && c0 <= c1)
            {
                c[2] = glm::u8vec3((static_cast<unsigned>(c[0].r) + c[1].r) / 2u,
                                   (static_cast<unsigned>(c[0].g) + c[1].g) / 2u,
                                   (static_cast<unsigned>(c[0].b) + c[1].b) / 2u);
                c[3] = glm::u8vec3(0);
            }
            else
            {
                c[2] =
                    glm::u8vec3((2u * c[0].r + c[1].r) / 3u, (2u * c[0].g + c[1].g) / 3u, (2u * c[0].b + c[1].b) / 3u);
                c[3] =
                    glm::u8vec3((c[0].r + 2u * c[1].r) / 3u, (c[0].g + 2u * c[1].g) / 3u, (c[0].b + 2u * c[1].b) / 3u);
            }
            std::uint32_t bits{};
            std::memcpy(&bits, cp + 4, 4);
            for (int i = 0; i < 16; ++i)
            {
                const std::size_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= bmp.width || y >= bmp.height)
                    continue;
                const auto palette = (bits >> (2 * i)) & 3;
                const auto col = c[palette];
                const auto d = (y * bmp.width + x) * 4;
                out[d] = col.r;
                out[d + 1] = col.g;
                out[d + 2] = col.b;
                out[d + 3] = (bmp.textureFormat == 1 && c0 <= c1 && palette == 3) ? 0 : alpha[i];
            }
        }
    return out;
}
bool Png(const BrxBitmapRecord &b, std::vector<std::uint8_t> &png)
{
    auto rgba = Decode(b);
    if (rgba.empty())
        return false;
    auto cb = [](void *c, void *d, int n)
    {
        auto &v = *static_cast<std::vector<std::uint8_t> *>(c);
        auto *p = static_cast<std::uint8_t *>(d);
        v.insert(v.end(), p, p + n);
    };
    return stbi_write_png_to_func(cb, &png, (int)b.width, (int)b.height, 4, rgba.data(), (int)b.width * 4) != 0;
}
glm::vec3 Hsv(float h, float s, float v)
{
    h = std::fmod(h, 360.f);
    if (h < 0)
        h += 360;
    float c = v * s, x = c * (1 - std::abs(std::fmod(h / 60, 2.f) - 1)), m = v - c;
    glm::vec3 r = h < 60    ? glm::vec3(c, x, 0)
                  : h < 120 ? glm::vec3(x, c, 0)
                  : h < 180 ? glm::vec3(0, c, x)
                  : h < 240 ? glm::vec3(0, x, c)
                  : h < 300 ? glm::vec3(x, 0, c)
                            : glm::vec3(c, 0, x);
    return r + glm::vec3(m);
}
glm::vec3 SafeNormalize(const glm::vec3 &value)
{
    const float lengthSquared = glm::dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f)
        return glm::vec3(0.0f);
    return value / std::sqrt(lengthSquared);
}
glm::vec3 Bake(const BrxDocument &d, const glm::vec3 &p, const glm::vec3 &n, const glm::vec3 &vc, float self)
{
    glm::vec3 light(d.WorldHeader().worldShadow + d.WorldHeader().worldMidtone + self / 32768.f);
    for (const auto &l : d.Lights())
    {
        glm::vec3 dir;
        float att = 1;
        if (l.kind == 0)
        {
            dir = -SafeNormalize(glm::make_vec3(l.direction));
            if (glm::dot(dir, dir) == 0.0f)
                continue;
        }
        else
        {
            glm::vec3 delta = glm::make_vec3(l.position) - p;
            float dist = glm::length(delta);
            if (!std::isfinite(dist) || dist < .001f)
                continue;
            dir = delta / dist;
            att = 1.f - glm::clamp((dist - l.falloff[0]) / (std::max)(1.f, l.falloff[1] - l.falloff[0]), 0.f, 1.f);
        }
        light += Hsv(l.highlightHsv[0], l.highlightHsv[1] / 255.0f, l.highlightHsv[2] / 255.0f) *
                 (std::max)(0.f, glm::dot(n, dir)) * att;
    }
    return glm::clamp(vc * light, 0.f, 1.f);
}
}
bool ExportMapTextures(const BrxDocument &d, const std::filesystem::path &folder, std::size_t &written,
                       std::string &error)
{
    written = 0;
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec)
    {
        error = ec.message();
        return false;
    }
    auto write = [&](const std::filesystem::path &path, const std::int32_t index, const bool flip)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= d.Bitmaps().size())
            return;
        const auto &b = d.Bitmaps()[static_cast<std::size_t>(index)];
        auto rgba = Decode(b);
        if (rgba.empty())
            return;
        stbi_flip_vertically_on_write(flip ? 1 : 0);
        const auto name = path.string();
        if (stbi_write_png(name.c_str(), (int)b.width, (int)b.height, 4, rgba.data(), (int)b.width * 4))
            ++written;
    };
    for (std::size_t shaderIndex = 0; shaderIndex < d.Shaders().size(); ++shaderIndex)
    {
        const auto &shader = d.Shaders()[shaderIndex];
        const std::size_t stageCount = (std::max)(std::size_t{1}, shader.textureStages.size());
        for (std::size_t stage = 0; stage < stageCount; ++stage)
        {
            const auto *textureStage = stage < shader.textureStages.size() ? &shader.textureStages[stage] : nullptr;
            const auto &ambient = textureStage ? textureStage->ambientBitmapIndices : shader.ambientBitmapIndices;
            const auto &diffuse = textureStage ? textureStage->diffuseBitmapIndices : shader.diffuseBitmapIndices;
            const auto &saturate = textureStage ? textureStage->saturateBitmapIndices : shader.saturateBitmapIndices;
            const auto frames = (std::max)({ambient.size(), diffuse.size(), saturate.size()});
            for (std::size_t frame = 0; frame < frames; ++frame)
            {
                char prefix[128]{};
                std::snprintf(prefix, sizeof(prefix), "shader_%03zu_tex_%02zu_frame_%02zu", shaderIndex, stage, frame);
                const auto a = frame < ambient.size() ? ambient[frame] : -1;
                const auto di = frame < diffuse.size() ? diffuse[frame] : a;
                const auto sat = frame < saturate.size() ? saturate[frame] : -1;
                write(folder / (std::string(prefix) + "_shadow.png"), a, true);
                write(folder / (std::string(prefix) + "_diffuse.png"), di, true);
                write(folder / (std::string(prefix) + "_saturate.png"), sat, true);
            }
        }
    }
    for (std::size_t font = 0; font < d.Fonts().size(); ++font)
    {
        char name[64]{};
        std::snprintf(name, sizeof(name), "font_%03zu.png", font);
        write(folder / name, d.Fonts()[font].bitmapIndex, false);
    }
    stbi_flip_vertically_on_write(0);
    if (!written)
    {
        error = "No decoded textures could be exported.";
        return false;
    }
    return true;
}
[[maybe_unused]] static bool ExportMapGlbLegacy(const BrxDocument &d, const std::filesystem::path &path, bool baked,
                                                std::string &error)
{
    error.clear();
    try
    {
        std::vector<std::uint8_t> bin;
        json j = {
            {"asset",
             {{"version", "2.0"}, {"generator", baked ? "ProjectCane Map Editor (baked)" : "ProjectCane Map Editor"}}}};
        json views = json::array(), access = json::array(), materials = json::array(), images = json::array(),
             textures = json::array(), meshes = json::array(), nodes = json::array(), roots = json::array();
        json defaultMaterial = {{"name", "Default"},
                                {"pbrMetallicRoughness",
                                 {{"baseColorFactor", {1, 1, 1, 1}}, {"metallicFactor", 0}, {"roughnessFactor", 1}}}};
        if (baked)
            defaultMaterial["extensions"] = {{"KHR_materials_unlit", json::object()}};
        materials.push_back(std::move(defaultMaterial));
        std::unordered_map<int, int> mats;
        auto accessor = [&](std::size_t off, std::size_t len, int target, int type, std::size_t count,
                            const char *shape, json extra = json::object())
        {
            int vi = (int)views.size();
            views.push_back({{"buffer", 0}, {"byteOffset", off}, {"byteLength", len}, {"target", target}});
            json a = {{"bufferView", vi}, {"componentType", type}, {"count", count}, {"type", shape}};
            a.update(extra);
            int ai = (int)access.size();
            access.push_back(a);
            return ai;
        };
        auto material = [&](int si)
        {
            if (si < 0 || static_cast<std::size_t>(si) >= d.Shaders().size())
                return 0;
            if (auto it = mats.find(si); it != mats.end())
                return it->second;
            const auto &s = d.Shaders()[si];
            json pbr = {{"baseColorFactor", {s.rgba[0], s.rgba[1], s.rgba[2], s.rgba[3]}},
                        {"metallicFactor", 0},
                        {"roughnessFactor", 1}};
            int bi = s.diffuseBitmapIndices.empty() ? (s.ambientBitmapIndices.empty() ? -1 : s.ambientBitmapIndices[0])
                                                    : s.diffuseBitmapIndices[0];
            if (bi >= 0 && static_cast<std::size_t>(bi) < d.Bitmaps().size())
            {
                std::vector<std::uint8_t> png;
                if (Png(d.Bitmaps()[bi], png))
                {
                    Align(bin);
                    auto off = bin.size();
                    bin.insert(bin.end(), png.begin(), png.end());
                    int vi = (int)views.size();
                    views.push_back({{"buffer", 0}, {"byteOffset", off}, {"byteLength", png.size()}});
                    int ii = (int)images.size();
                    images.push_back({{"bufferView", vi}, {"mimeType", "image/png"}});
                    int ti = (int)textures.size();
                    textures.push_back({{"source", ii}});
                    pbr["baseColorTexture"] = {{"index", ti}};
                }
            }
            json mat = {{"name", s.editorName}, {"pbrMetallicRoughness", pbr}, {"doubleSided", true}};
            if (baked)
                mat["extensions"] = {{"KHR_materials_unlit", json::object()}};
            int mi = (int)materials.size();
            materials.push_back(std::move(mat));
            mats[si] = mi;
            return mi;
        };
        const glm::mat4 yup = glm::rotate(glm::mat4(1), glm::radians(-90.f), glm::vec3(1, 0, 0));
        std::size_t exported = 0;
        for (std::size_t gi = 0; gi < d.Globs().size(); ++gi)
        {
            const auto &g = d.Globs()[gi];
            glm::mat4 model = glm::make_mat4(g.sourceToWorld);
            glm::mat3 normal = glm::inverseTranspose(glm::mat3(model));
            for (std::size_t si = 0; si < g.sourceSubglobs.size(); ++si)
            {
                const auto &s = g.sourceSubglobs[si];
                if (s.vertices.empty() || s.triangles.empty())
                    continue;
                for (const auto &t : s.triangles)
                    if (t[0] >= s.vertices.size() || t[1] >= s.vertices.size() || t[2] >= s.vertices.size())
                    {
                        error = "SUBGLOB contains an invalid triangle index.";
                        return false;
                    }
                Align(bin);
                auto po = bin.size();
                glm::vec3 mn(std::numeric_limits<float>::max()), mx(std::numeric_limits<float>::lowest());
                for (const auto &v : s.vertices)
                {
                    glm::vec3 p = glm::vec3(yup * model * glm::vec4(glm::make_vec3(v.position), 1));
                    mn = glm::min(mn, p);
                    mx = glm::max(mx, p);
                    Put(bin, p.x);
                    Put(bin, p.y);
                    Put(bin, p.z);
                }
                int pa = accessor(po, bin.size() - po, 34962, 5126, s.vertices.size(), "VEC3",
                                  {{"min", {mn.x, mn.y, mn.z}}, {"max", {mx.x, mx.y, mx.z}}});
                Align(bin);
                auto no = bin.size();
                for (const auto &v : s.vertices)
                {
                    glm::vec3 n = glm::normalize(glm::mat3(yup) * normal * glm::make_vec3(v.normal));
                    Put(bin, n.x);
                    Put(bin, n.y);
                    Put(bin, n.z);
                }
                int na = accessor(no, bin.size() - no, 34962, 5126, s.vertices.size(), "VEC3");
                Align(bin);
                auto uo = bin.size();
                for (const auto &v : s.vertices)
                {
                    Put(bin, v.uv[0]);
                    Put(bin, v.uv[1]);
                }
                int ua = accessor(uo, bin.size() - uo, 34962, 5126, s.vertices.size(), "VEC2");
                Align(bin);
                auto co = bin.size();
                for (const auto &v : s.vertices)
                {
                    glm::vec3 c = glm::make_vec3(v.color);
                    if (baked)
                    {
                        glm::vec3 wp = glm::vec3(model * glm::vec4(glm::make_vec3(v.position), 1)),
                                  wn = glm::normalize(normal * glm::make_vec3(v.normal));
                        c = Bake(d, wp, wn, c, s.selfIllumination);
                    }
                    Put(bin, c.r);
                    Put(bin, c.g);
                    Put(bin, c.b);
                    Put(bin, v.color[3]);
                }
                int ca = accessor(co, bin.size() - co, 34962, 5126, s.vertices.size(), "VEC4");
                Align(bin);
                auto io = bin.size();
                for (const auto &t : s.triangles)
                {
                    Put(bin, (std::uint32_t)t[0]);
                    Put(bin, (std::uint32_t)t[1]);
                    Put(bin, (std::uint32_t)t[2]);
                }
                int ia = accessor(io, bin.size() - io, 34963, 5125, s.triangles.size() * 3, "SCALAR");
                int mi = (int)meshes.size();
                meshes.push_back(
                    {{"name", d.SubglobEditorLabel(gi, si)},
                     {"primitives",
                      json::array(
                          {{{"attributes", {{"POSITION", pa}, {"NORMAL", na}, {"TEXCOORD_0", ua}, {"COLOR_0", ca}}},
                            {"indices", ia},
                            {"material", material(s.shaderIndex)},
                            {"mode", 4}}})}});
                int ni = (int)nodes.size();
                nodes.push_back({{"name", d.SubglobEditorLabel(gi, si)}, {"mesh", mi}});
                roots.push_back(ni);
                ++exported;
            }
        }
        if (!exported)
        {
            error = "The document contains no exportable SUBGLOB triangles.";
            return false;
        }
        Align(bin);
        j["buffers"] = json::array({{{"byteLength", bin.size()}}});
        j["bufferViews"] = views;
        j["accessors"] = access;
        j["materials"] = materials;
        if (baked)
            j["extensionsUsed"] = json::array({"KHR_materials_unlit"});
        if (!images.empty())
            j["images"] = images;
        if (!textures.empty())
            j["textures"] = textures;
        j["meshes"] = meshes;
        j["nodes"] = nodes;
        j["scenes"] = json::array({{{"nodes", roots}}});
        j["scene"] = 0;
        auto text = j.dump();
        while (text.size() & 3)
            text.push_back(' ');
        const std::uint64_t total = 12 + 8 + text.size() + 8 + bin.size();
        if (total > 0xffffffffu)
        {
            error = "GLB exceeds 4 GiB.";
            return false;
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            error = "Could not open the GLB output file.";
            return false;
        }
        auto u32 = [&](std::uint32_t v) { out.write((char *)&v, 4); };
        u32(0x46546c67);
        u32(2);
        u32((std::uint32_t)total);
        u32((std::uint32_t)text.size());
        u32(0x4e4f534a);
        out.write(text.data(), text.size());
        u32((std::uint32_t)bin.size());
        u32(0x004e4942);
        out.write((char *)bin.data(), bin.size());
        out.close();
        if (!out)
        {
            error = "Failed while writing the GLB.";
            return false;
        }
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}

namespace
{
constexpr std::size_t AllRecords = static_cast<std::size_t>(-1);

std::int32_t ExportRuntimeParent(const BrxObjectRecord &object)
{
    return object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
}

std::string ExportObjectName(const BrxObjectRecord &object)
{
    return object.editorName.empty()
               ? std::string(BrxDocument::ClassName(object.cid)) + "_" + std::to_string(object.oid)
               : object.editorName;
}

json ExportMatrix(const glm::mat4 &matrix)
{
    json result = json::array();
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            result.push_back(matrix[column][row]);
    return result;
}

glm::mat3 ExportNormalMatrix(const glm::mat4 &matrix)
{
    const glm::mat3 linear(matrix);
    return std::abs(glm::determinant(linear)) > 1.0e-12f ? glm::transpose(glm::inverse(linear)) : linear;
}

void RequireFiniteModelMatrix(const glm::mat4 &matrix)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                throw std::runtime_error("Model export contains a non-finite transform.");
}

glm::mat4 InvertModelMatrix(const glm::mat4 &matrix)
{
    RequireFiniteModelMatrix(matrix);
    const float determinant = glm::determinant(matrix);
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-12f)
        throw std::runtime_error("Model export requires an invertible placement/bone transform.");
    const glm::mat4 inverse = glm::inverse(matrix);
    RequireFiniteModelMatrix(inverse);
    return inverse;
}

std::int32_t ResolveExportBone(const std::vector<BrxObjectRecord> &objects, const std::int16_t oid,
                               const std::int32_t context)
{
    if (context < 0 || static_cast<std::size_t>(context) >= objects.size())
        return -1;
    const auto &contextObject = objects[static_cast<std::size_t>(context)];
    const auto sameInstance = [&](const BrxObjectRecord &candidate)
    {
        if (contextObject.proxyRecordIndex < 0)
            return candidate.proxyRecordIndex < 0;
        if (candidate.proxyRecordIndex != contextObject.proxyRecordIndex)
            return false;
        return contextObject.proxyRootIndex < 0 || candidate.proxyRootIndex == contextObject.proxyRootIndex;
    };
    std::unordered_map<std::int32_t, int> depths;
    std::int32_t ancestor = context;
    for (int depth = 1; ancestor >= 0 && static_cast<std::size_t>(ancestor) < objects.size(); ++depth)
    {
        if (!depths.emplace(ancestor, depth).second)
            break;
        ancestor = ExportRuntimeParent(objects[static_cast<std::size_t>(ancestor)]);
    }
    std::int32_t best = -1;
    int bestDepth = (std::numeric_limits<int>::max)();
    for (std::size_t index = 0; index < objects.size(); ++index)
    {
        if (objects[index].oid != oid || !sameInstance(objects[index]))
            continue;
        std::int32_t current = static_cast<std::int32_t>(index);
        for (std::size_t guard = 0;
             current >= 0 && static_cast<std::size_t>(current) < objects.size() && guard++ <= objects.size();
             current = ExportRuntimeParent(objects[static_cast<std::size_t>(current)]))
        {
            const auto found = depths.find(current);
            if (found == depths.end())
                continue;
            if (found->second < bestDepth)
            {
                best = static_cast<std::int32_t>(index);
                bestDepth = found->second;
            }
            break;
        }
    }
    return best;
}

// Consume the same generated scene as GLB, rather than maintaining a second
// BRX geometry/material converter. OBJ flattens the exported bind pose.
bool WriteSceneObj(const json &gltf, const std::vector<std::uint8_t> &binary,
                   const std::filesystem::path &path, std::string &error)
{
    std::string sidecar;
    for (const auto byte : path.stem().u8string())
    {
        const char c = static_cast<char>(byte);
        if (sidecar.size() == 48)
            break;
        sidecar.push_back((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                 (c >= '0' && c <= '9') || c == '_' ? c : '_');
    }
    if (sidecar.empty())
        sidecar = "Model";
    // Sanitizing alone makes Ladder-red.obj and Ladder_red.obj overwrite the
    // same companion files. Retain a stable identity from the actual filename.
    std::uint64_t filenameHash = 14695981039346656037ull;
    for (const auto byte : path.filename().u8string())
    {
        filenameHash ^= static_cast<std::uint8_t>(byte);
        filenameHash *= 1099511628211ull;
    }
    std::ostringstream identity;
    identity << std::hex << filenameHash;
    sidecar += '_' + identity.str();
    const std::string materialFile = sidecar + ".mtl";
    std::ostringstream obj, mtl;
    obj.imbue(std::locale::classic());
    mtl.imbue(std::locale::classic());
    obj << std::setprecision(std::numeric_limits<float>::max_digits10);
    mtl << std::setprecision(std::numeric_limits<float>::max_digits10);
    obj << "# ProjectCane selected model; game Z-up; game units; bind pose; no gameplay or collision\nmtllib "
        << materialFile << '\n';
    const auto &views = gltf.at("bufferViews");
    const auto &accessors = gltf.at("accessors");
    const auto read = [&](const int accessorIndex, const std::size_t element, const std::size_t component,
                          const std::size_t components, const bool integer)
    {
        const auto &accessor = accessors.at(accessorIndex);
        if (accessor.at("componentType").get<int>() != (integer ? 5125 : 5126) ||
            element >= accessor.at("count").get<std::size_t>() || component >= components)
            throw std::runtime_error("Invalid generated OBJ accessor.");
        const auto &view = views.at(accessor.at("bufferView").get<int>());
        const std::size_t offset = view.value("byteOffset", std::size_t{0}) +
                                   accessor.value("byteOffset", std::size_t{0}) +
                                   (element * components + component) * sizeof(float);
        if (offset > binary.size() || binary.size() - offset < sizeof(float))
            throw std::runtime_error("OBJ accessor exceeds the generated buffer.");
        if (integer)
        {
            std::uint32_t value;
            std::memcpy(&value, binary.data() + offset, sizeof(value));
            return static_cast<double>(value);
        }
        float value;
        std::memcpy(&value, binary.data() + offset, sizeof(value));
        if (!std::isfinite(value))
            throw std::runtime_error("Model contains a non-finite export value.");
        return static_cast<double>(value);
    };
    std::vector<std::pair<std::filesystem::path, std::vector<std::uint8_t>>> imageFiles;
    for (std::size_t index = 0; index < gltf.at("materials").size(); ++index)
    {
        const auto &material = gltf.at("materials")[index];
        const auto &pbr = material.at("pbrMetallicRoughness");
        const auto &color = pbr.at("baseColorFactor");
        mtl << "newmtl Material_" << index << "\nKd " << color[0].get<float>() << ' ' << color[1].get<float>()
            << ' ' << color[2].get<float>() << "\nd " << color[3].get<float>() << "\nillum 1\n";
        if (pbr.contains("baseColorTexture"))
        {
            const int texture = pbr["baseColorTexture"].at("index").get<int>();
            const int image = gltf.at("textures").at(texture).at("source").get<int>();
            const int viewIndex = gltf.at("images").at(image).at("bufferView").get<int>();
            const auto &view = views.at(viewIndex);
            const std::size_t offset = view.value("byteOffset", std::size_t{0});
            const std::size_t length = view.at("byteLength").get<std::size_t>();
            if (offset > binary.size() || length > binary.size() - offset)
                throw std::runtime_error("OBJ texture exceeds the generated buffer.");
            const std::string name = sidecar + "_material_" + std::to_string(index) + ".png";
            imageFiles.emplace_back(path.parent_path() / name,
                                    std::vector<std::uint8_t>(binary.begin() + offset, binary.begin() + offset + length));
            mtl << "map_Kd " << name << '\n';
        }
        mtl << '\n';
    }
    std::vector<std::pair<int, glm::mat4>> pending;
    // OBJ has no mandated up axis. Keep native game coordinates for direct
    // reimport; cancel the GLB scene's external Y-up conversion exactly once.
    const glm::mat4 yUpToZUp = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0));
    for (const auto &node : gltf.at("scenes")[0].at("nodes"))
        pending.emplace_back(node.get<int>(), yUpToZUp);
    std::unordered_set<int> visited;
    std::size_t vertexBase = 1;
    while (!pending.empty())
    {
        const auto [nodeIndex, parent] = pending.back();
        pending.pop_back();
        if (!visited.insert(nodeIndex).second)
            throw std::runtime_error("Exported model hierarchy contains a repeated node or cycle.");
        const auto &node = gltf.at("nodes").at(nodeIndex);
        glm::mat4 local(1.0f);
        if (node.contains("matrix"))
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    local[column][row] = node["matrix"].at(column * 4 + row).get<float>();
        const glm::mat4 transform = parent * local;
        if (node.contains("children"))
            for (const auto &child : node["children"])
                pending.emplace_back(child.get<int>(), transform);
        if (!node.contains("mesh"))
            continue;
        const glm::mat3 normalMatrix = ExportNormalMatrix(transform);
        const bool mirrored = glm::determinant(glm::mat3(transform)) < 0.0f;
        const auto &mesh = gltf.at("meshes").at(node.at("mesh").get<int>());
        std::size_t part = 0;
        for (const auto &primitive : mesh.at("primitives"))
        {
            const auto &attributes = primitive.at("attributes");
            const int positions = attributes.at("POSITION").get<int>();
            const int normals = attributes.at("NORMAL").get<int>();
            const int uvs = attributes.at("TEXCOORD_0").get<int>();
            const int colors = attributes.at("COLOR_0").get<int>();
            const std::size_t count = accessors.at(positions).at("count").get<std::size_t>();
            obj << "\no Model_" << nodeIndex << "_Part_" << part++ << "\nusemtl Material_"
                << primitive.at("material").get<int>() << '\n';
            for (std::size_t i = 0; i < count; ++i)
            {
                const glm::vec3 position(transform * glm::vec4(read(positions, i, 0, 3, false),
                    read(positions, i, 1, 3, false), read(positions, i, 2, 3, false), 1.0f));
                const glm::vec3 normal = SafeNormalize(normalMatrix * glm::vec3(read(normals, i, 0, 3, false),
                    read(normals, i, 1, 3, false), read(normals, i, 2, 3, false)));
                if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
                    throw std::runtime_error("Model has a non-finite export transform.");
                obj << "v " << position.x << ' ' << position.y << ' ' << position.z << ' '
                    << read(colors, i, 0, 4, false) << ' ' << read(colors, i, 1, 4, false) << ' '
                    << read(colors, i, 2, 4, false) << ' ' << read(colors, i, 3, 4, false) << '\n';
                // glTF image/UV origin is top-left; OBJ UV origin is bottom-left.
                obj << "vt " << read(uvs, i, 0, 2, false) << ' ' << 1.0 - read(uvs, i, 1, 2, false) << '\n';
                obj << "vn " << normal.x << ' ' << normal.y << ' ' << normal.z << '\n';
            }
            const int indices = primitive.at("indices").get<int>();
            const std::size_t indexCount = accessors.at(indices).at("count").get<std::size_t>();
            if (indexCount % 3 != 0)
                throw std::runtime_error("Model contains an incomplete export triangle.");
            for (std::size_t i = 0; i < indexCount; i += 3)
            {
                obj << "f";
                for (const int corner : {0, mirrored ? 2 : 1, mirrored ? 1 : 2})
                {
                    const auto index = static_cast<std::size_t>(read(indices, i + corner, 0, 1, true));
                    if (index >= count)
                        throw std::runtime_error("OBJ triangle has an invalid vertex index.");
                    const std::size_t value = vertexBase + index;
                    obj << ' ' << value << '/' << value << '/' << value;
                }
                obj << '\n';
            }
            vertexBase += count;
        }
    }
    const auto write = [&](const std::filesystem::path &file, const char *data, const std::size_t size)
    {
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        output.write(data, static_cast<std::streamsize>(size));
        output.close();
        if (!output)
        {
            error = "Could not write model export file: " + file.string();
            return false;
        }
        return true;
    };
    // Validate and assemble the scene before opening any output files.
    for (const auto &[file, bytes] : imageFiles)
        if (!write(file, reinterpret_cast<const char *>(bytes.data()), bytes.size()))
            return false;
    const std::string materialText = mtl.str(), objectText = obj.str();
    return write(path.parent_path() / materialFile, materialText.data(), materialText.size()) &&
           write(path, objectText.data(), objectText.size());
}

bool ExportSceneGlb(const BrxDocument &document, const std::filesystem::path &path, const bool baked,
                    const std::size_t onlyGlob, const std::size_t onlyGlobset, const bool includeLights,
                    std::string &error, const ModelExportOptions *modelOptions = nullptr)
{
    error.clear();
    try
    {
        const auto &globs = document.Globs();
        const auto &globsets = document.Globsets();
        const auto &objects = document.Objects();
        if (onlyGlob != AllRecords && onlyGlob >= globs.size())
        {
            error = "Selected GLOB index is invalid.";
            return false;
        }
        if (onlyGlobset != AllRecords && onlyGlobset >= globsets.size())
        {
            error = "Selected GLOBSET index is invalid.";
            return false;
        }

        std::unordered_set<std::size_t> includedGlobs;
        if (onlyGlob != AllRecords)
            includedGlobs.insert(onlyGlob);
        else if (onlyGlobset != AllRecords)
            includedGlobs.insert(globsets[onlyGlobset].globIndices.begin(), globsets[onlyGlobset].globIndices.end());
        else
            for (std::size_t index = 0; index < globs.size(); ++index)
                includedGlobs.insert(index);

        std::vector<std::int32_t> globToSet(globs.size(), -1);
        for (std::size_t setIndex = 0; setIndex < globsets.size(); ++setIndex)
            for (const auto globIndex : globsets[setIndex].globIndices)
                if (globIndex < globToSet.size() && globToSet[globIndex] < 0)
                    globToSet[globIndex] = static_cast<std::int32_t>(setIndex);

        std::vector<std::uint8_t> binary;
        json gltf = {{"asset",
                      {{"version", "2.0"},
                       {"generator", baked ? "ProjectCane Map Editor GLOBSET exporter (baked)"
                                           : "ProjectCane Map Editor GLOBSET exporter"}}}};
        json views = json::array(), accessors = json::array(), materials = json::array();
        json images = json::array(), textures = json::array(), meshes = json::array();
        json nodes = json::array(), skins = json::array();
        materials.push_back(
            {{"name", "Default"},
             {"pbrMetallicRoughness",
              {{"baseColorFactor", {1.0, 1.0, 1.0, 1.0}}, {"metallicFactor", 0.0}, {"roughnessFactor", 1.0}}}});
        if (baked)
            materials[0]["extensions"] = {{"KHR_materials_unlit", json::object()}};
        std::unordered_map<int, int> materialCache;

        const auto addAccessor = [&](const std::size_t offset, const std::size_t length, const int target,
                                     const int componentType, const std::size_t count, const char *type,
                                     json extra = json::object())
        {
            const int viewIndex = static_cast<int>(views.size());
            json view = {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
            if (target != 0)
                view["target"] = target;
            views.push_back(std::move(view));
            json accessor = {
                {"bufferView", viewIndex}, {"componentType", componentType}, {"count", count}, {"type", type}};
            accessor.update(extra);
            const int accessorIndex = static_cast<int>(accessors.size());
            accessors.push_back(std::move(accessor));
            return accessorIndex;
        };
        const auto getMaterial = [&](const int shaderIndex)
        {
            if (shaderIndex < 0 || static_cast<std::size_t>(shaderIndex) >= document.Shaders().size())
            {
                // Inspector uses 0xffff for an explicitly unassigned material.
                // Keep that default-material export path valid.
                if (modelOptions && shaderIndex >= 0 && shaderIndex != 0xffff)
                    throw std::runtime_error("Selected model references a missing material.");
                return 0;
            }
            if (const auto found = materialCache.find(shaderIndex); found != materialCache.end())
                return found->second;
            const auto &shader = document.Shaders()[static_cast<std::size_t>(shaderIndex)];
            if (modelOptions)
                for (const auto channel : shader.rgba)
                    if (!std::isfinite(channel))
                        throw std::runtime_error("Selected model has a non-finite material color.");
            json pbr = {{"baseColorFactor", {shader.rgba[0], shader.rgba[1], shader.rgba[2], shader.rgba[3]}},
                        {"metallicFactor", 0.0},
                        {"roughnessFactor", 1.0}};
            const int bitmap = shader.diffuseBitmapIndices.empty()
                                   ? (shader.ambientBitmapIndices.empty() ? -1 : shader.ambientBitmapIndices[0])
                                   : shader.diffuseBitmapIndices[0];
            if (modelOptions && bitmap >= 0 && static_cast<std::size_t>(bitmap) >= document.Bitmaps().size())
                throw std::runtime_error("Selected model references a missing texture.");
            if (bitmap >= 0 && static_cast<std::size_t>(bitmap) < document.Bitmaps().size())
            {
                std::vector<std::uint8_t> png;
                if (Png(document.Bitmaps()[static_cast<std::size_t>(bitmap)], png))
                {
                    Align(binary);
                    const std::size_t offset = binary.size();
                    binary.insert(binary.end(), png.begin(), png.end());
                    const int view = static_cast<int>(views.size());
                    views.push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", png.size()}});
                    const int image = static_cast<int>(images.size());
                    images.push_back({{"bufferView", view}, {"mimeType", "image/png"}});
                    const int texture = static_cast<int>(textures.size());
                    textures.push_back({{"source", image}});
                    pbr["baseColorTexture"] = {{"index", texture}};
                }
                else if (modelOptions)
                    throw std::runtime_error("Could not decode a selected model texture for export.");
            }
            json material = {
                {"name", shader.editorName.empty() ? "Shader_" + std::to_string(shaderIndex) : shader.editorName},
                {"pbrMetallicRoughness", std::move(pbr)},
                {"doubleSided", true}};
            if (shader.rgba[3] < 0.999f)
                material["alphaMode"] = "BLEND";
            if (baked)
                material["extensions"] = {{"KHR_materials_unlit", json::object()}};
            const int result = static_cast<int>(materials.size());
            materials.push_back(std::move(material));
            materialCache[shaderIndex] = result;
            return result;
        };

        glm::mat4 exportBasis = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0));
        glm::mat4 placement(1.0f), worldToModel(1.0f);
        if (modelOptions && !modelOptions->worldSpace)
        {
            if (onlyGlob != AllRecords)
                placement = glm::make_mat4(globs[onlyGlob].sourceToWorld);
            else if (onlyGlobset != AllRecords)
            {
                const auto owner = globsets[onlyGlobset].ownerObjectIndex;
                if (owner < 0 || static_cast<std::size_t>(owner) >= objects.size())
                {
                    error = "Model-local export requires a valid model owner. Try world coordinates.";
                    return false;
                }
                placement = glm::make_mat4(objects[static_cast<std::size_t>(owner)].worldTransform);
            }
            const float determinant = glm::determinant(placement);
            if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-12f)
            {
                error = "Model placement cannot be inverted. Try world coordinates or restore nonzero scale.";
                return false;
            }
            worldToModel = InvertModelMatrix(placement);
            exportBasis *= worldToModel;
        }
        if (modelOptions)
            RequireFiniteModelMatrix(exportBasis);
        const int basisNode = static_cast<int>(nodes.size());
        nodes.push_back(
            {{"name", "ProjectCane_Z_Up"}, {"matrix", ExportMatrix(exportBasis)}, {"children", json::array()}});
        std::size_t primitiveCount = 0;

        const auto appendPrimitive = [&](const BrxSubglobRecord &subglob, const glm::mat4 &sourceToOutput,
                                         const glm::mat4 &sourceToWorld, const bool skinned, json &primitiveArray)
        {
            if (modelOptions)
            {
                RequireFiniteModelMatrix(sourceToOutput);
                RequireFiniteModelMatrix(sourceToWorld);
                if (subglob.vertices.empty() && !subglob.triangles.empty())
                    throw std::runtime_error("Selected model has triangles without vertices.");
                for (const auto &vertex : subglob.vertices)
                {
                    const auto finite = [](const auto &values)
                    {
                        return std::all_of(std::begin(values), std::end(values),
                                           [](const float value) { return std::isfinite(value); });
                    };
                    if (!finite(vertex.position) || !finite(vertex.normal) || !finite(vertex.uv) ||
                        !finite(vertex.color) || (skinned && !finite(vertex.boneWeights)))
                        throw std::runtime_error("Selected model contains non-finite vertex data.");
                }
            }
            if (subglob.vertices.empty() || subglob.triangles.empty())
                return false;
            for (const auto &triangle : subglob.triangles)
                if (triangle[0] >= subglob.vertices.size() || triangle[1] >= subglob.vertices.size() ||
                    triangle[2] >= subglob.vertices.size())
                {
                    if (modelOptions)
                        throw std::runtime_error("Selected model has a triangle with an invalid vertex index.");
                    return false;
                }
            const glm::mat3 normalToOutput = ExportNormalMatrix(sourceToOutput);
            // Baked lighting must reproduce ProjectCane's glob.vert, which
            // transforms runtime GLOB normals with mat3(model), not an
            // inverse-transpose. Keep the conventional inverse-transpose only
            // for the normals written to the external glTF mesh itself.
            const glm::mat3 normalToWorld = glm::mat3(sourceToWorld);
            Align(binary);
            const std::size_t positionOffset = binary.size();
            glm::vec3 minimum((std::numeric_limits<float>::max)());
            glm::vec3 maximum((std::numeric_limits<float>::lowest)());
            for (const auto &vertex : subglob.vertices)
            {
                const glm::vec3 value(sourceToOutput * glm::vec4(glm::make_vec3(vertex.position), 1.0f));
                if (modelOptions && (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)))
                    throw std::runtime_error("Selected model position overflowed its export transform.");
                minimum = glm::min(minimum, value);
                maximum = glm::max(maximum, value);
                Put(binary, value.x);
                Put(binary, value.y);
                Put(binary, value.z);
            }
            const int positions = addAccessor(
                positionOffset, binary.size() - positionOffset, 34962, 5126, subglob.vertices.size(), "VEC3",
                {{"min", {minimum.x, minimum.y, minimum.z}}, {"max", {maximum.x, maximum.y, maximum.z}}});
            Align(binary);
            const std::size_t normalOffset = binary.size();
            for (const auto &vertex : subglob.vertices)
            {
                const glm::vec3 value = SafeNormalize(normalToOutput * glm::make_vec3(vertex.normal));
                Put(binary, value.x);
                Put(binary, value.y);
                Put(binary, value.z);
            }
            const int normals =
                addAccessor(normalOffset, binary.size() - normalOffset, 34962, 5126, subglob.vertices.size(), "VEC3");
            Align(binary);
            const std::size_t uvOffset = binary.size();
            for (const auto &vertex : subglob.vertices)
            {
                Put(binary, vertex.uv[0]);
                Put(binary, vertex.uv[1]);
            }
            const int uvs =
                addAccessor(uvOffset, binary.size() - uvOffset, 34962, 5126, subglob.vertices.size(), "VEC2");
            Align(binary);
            const std::size_t colorOffset = binary.size();
            for (const auto &vertex : subglob.vertices)
            {
                glm::vec3 color = glm::make_vec3(vertex.color);
                if (baked)
                {
                    const glm::vec3 worldPosition(sourceToWorld * glm::vec4(glm::make_vec3(vertex.position), 1.0f));
                    const glm::vec3 worldNormal = SafeNormalize(normalToWorld * glm::make_vec3(vertex.normal));
                    color = Bake(document, worldPosition, worldNormal, color, subglob.selfIllumination);
                }
                Put(binary, color.r);
                Put(binary, color.g);
                Put(binary, color.b);
                Put(binary, vertex.color[3]);
            }
            const int colors =
                addAccessor(colorOffset, binary.size() - colorOffset, 34962, 5126, subglob.vertices.size(), "VEC4");
            json attributes = {{"POSITION", positions}, {"NORMAL", normals}, {"TEXCOORD_0", uvs}, {"COLOR_0", colors}};
            if (skinned)
            {
                Align(binary);
                const std::size_t jointOffset = binary.size();
                for (const auto &vertex : subglob.vertices)
                    for (int influence = 0; influence < 4; ++influence)
                    {
                        // glTF still requires unused joint indices to be in
                        // range. The all-zero-weight fallback also needs a
                        // valid joint when its first weight becomes one.
                        const std::uint16_t joint = modelOptions && vertex.boneWeights[influence] <= 0.0f
                                                        ? std::uint16_t{0} : vertex.boneIndices[influence];
                        Put(binary, joint);
                    }
                attributes["JOINTS_0"] =
                    addAccessor(jointOffset, binary.size() - jointOffset, 34962, 5123, subglob.vertices.size(), "VEC4");
                Align(binary);
                const std::size_t weightOffset = binary.size();
                for (const auto &vertex : subglob.vertices)
                {
                    const float total =
                        (std::max)(0.0f, vertex.boneWeights[0]) + (std::max)(0.0f, vertex.boneWeights[1]) +
                        (std::max)(0.0f, vertex.boneWeights[2]) + (std::max)(0.0f, vertex.boneWeights[3]);
                    if (modelOptions && !std::isfinite(total))
                        throw std::runtime_error("Selected model skin weights overflow during normalization.");
                    for (int influence = 0; influence < 4; ++influence)
                    {
                        const float value = total > 0.000001f ? (std::max)(0.0f, vertex.boneWeights[influence]) / total
                                                              : (influence == 0 ? 1.0f : 0.0f);
                        Put(binary, value);
                    }
                }
                attributes["WEIGHTS_0"] = addAccessor(weightOffset, binary.size() - weightOffset, 34962, 5126,
                                                      subglob.vertices.size(), "VEC4");
            }
            Align(binary);
            const std::size_t indexOffset = binary.size();
            const bool reverseWinding = modelOptions && glm::determinant(glm::mat3(sourceToOutput)) < 0.0f;
            for (const auto &triangle : subglob.triangles)
                for (const int corner : {0, reverseWinding ? 2 : 1, reverseWinding ? 1 : 2})
                    Put(binary, static_cast<std::uint32_t>(triangle[corner]));
            const int indices = addAccessor(indexOffset, binary.size() - indexOffset, 34963, 5125,
                                            subglob.triangles.size() * 3u, "SCALAR");
            primitiveArray.push_back({{"attributes", std::move(attributes)},
                                      {"indices", indices},
                                      {"material", getMaterial(subglob.shaderIndex)},
                                      {"mode", 4}});
            ++primitiveCount;
            return true;
        };

        std::unordered_map<std::size_t, int> staticSetNodes;
        for (const std::size_t globIndex : includedGlobs)
        {
            if (globIndex >= globs.size())
                continue;
            const auto &glob = globs[globIndex];
            const std::int32_t setIndex = globToSet[globIndex];
            const bool skinnedSet = setIndex >= 0 && !globsets[static_cast<std::size_t>(setIndex)].boneOids.empty();
            const glm::mat4 sourceToWorld = glm::make_mat4(glob.sourceToWorld);
            json primitives = json::array();
            for (const auto &subglob : glob.sourceSubglobs)
            {
                if (skinnedSet && subglob.boneCount > 0)
                    continue;
                // Put selected static parts in one shared model frame. This
                // also keeps GLOBSET-relative offsets when a static importer
                // reads mesh accessors without interpreting glTF scene nodes.
                appendPrimitive(subglob, modelOptions ? worldToModel * sourceToWorld : glm::mat4(1.0f),
                                sourceToWorld, false, primitives);
            }
            if (primitives.empty())
                continue;
            const int meshIndex = static_cast<int>(meshes.size());
            meshes.push_back({{"name", document.GlobEditorLabel(globIndex)}, {"primitives", std::move(primitives)}});
            const int globNode = static_cast<int>(nodes.size());
            nodes.push_back({{"name", document.GlobEditorLabel(globIndex)},
                             {"mesh", meshIndex},
                             {"matrix", ExportMatrix(modelOptions ? placement : sourceToWorld)}});
            int parent = basisNode;
            if (setIndex >= 0)
            {
                const std::size_t key = static_cast<std::size_t>(setIndex);
                auto found = staticSetNodes.find(key);
                if (found == staticSetNodes.end())
                {
                    const int node = static_cast<int>(nodes.size());
                    nodes.push_back({{"name", "GLOBSET_" + std::to_string(key)}, {"children", json::array()}});
                    nodes[basisNode]["children"].push_back(node);
                    staticSetNodes[key] = node;
                    parent = node;
                }
                else
                    parent = found->second;
            }
            if (parent == basisNode)
                nodes[basisNode]["children"].push_back(globNode);
            else
                nodes[parent]["children"].push_back(globNode);
        }

        for (std::size_t setIndex = 0; setIndex < globsets.size(); ++setIndex)
        {
            const auto &set = globsets[setIndex];
            if (set.boneOids.empty() || (onlyGlobset != AllRecords && setIndex != onlyGlobset))
                continue;
            std::vector<std::size_t> selectedSetGlobs;
            for (const auto globIndex : set.globIndices)
                if (includedGlobs.contains(globIndex))
                    selectedSetGlobs.push_back(globIndex);
            if (selectedSetGlobs.empty())
                continue;
            if (set.ownerObjectIndex < 0 || static_cast<std::size_t>(set.ownerObjectIndex) >= objects.size())
            {
                error = "Skinned GLOBSET " + std::to_string(setIndex) + " has no valid owner.";
                return false;
            }
            std::vector<std::int32_t> bones;
            for (const auto oid : set.boneOids)
            {
                const auto bone = ResolveExportBone(objects, oid, set.ownerObjectIndex);
                if (bone < 0)
                {
                    error = "Skinned GLOBSET " + std::to_string(setIndex) + " cannot resolve bone OID " +
                            std::to_string(oid) + ".";
                    return false;
                }
                bones.push_back(bone);
            }
            // Selected exports use the same frame for static and skinned
            // accessors. Otherwise world-space and individual-GLOB exports
            // reimport skin meshes in owner-local coordinates beside static
            // parts in the chosen output frame.
            const glm::mat4 meshBind = modelOptions ? placement :
                glm::make_mat4(objects[static_cast<std::size_t>(set.ownerObjectIndex)].worldTransform);
            const glm::mat4 worldToMesh = modelOptions ? InvertModelMatrix(meshBind) : glm::inverse(meshBind);
            const int rigRoot = static_cast<int>(nodes.size());
            nodes.push_back({{"name", "GLOBSET_" + std::to_string(setIndex) + "_Rig"},
                             {"matrix", ExportMatrix(meshBind)},
                             {"children", json::array()}});
            nodes[basisNode]["children"].push_back(rigRoot);
            std::unordered_map<std::int32_t, std::size_t> boneSlots;
            for (std::size_t slot = 0; slot < bones.size(); ++slot)
                boneSlots[bones[slot]] = slot;
            if (modelOptions)
                for (std::size_t slot = 0; slot < bones.size(); ++slot)
                {
                    std::unordered_set<std::size_t> ancestors;
                    std::size_t current = slot;
                    while (true)
                    {
                        if (!ancestors.insert(current).second)
                            throw std::runtime_error("Selected model bone hierarchy contains a cycle.");
                        const auto parent = ExportRuntimeParent(objects[static_cast<std::size_t>(bones[current])]);
                        const auto parentSlot = boneSlots.find(parent);
                        if (parentSlot == boneSlots.end())
                            break;
                        current = parentSlot->second;
                    }
                }
            std::vector<int> jointNodes(bones.size(), -1);
            for (std::size_t slot = 0; slot < bones.size(); ++slot)
            {
                const auto bone = bones[slot];
                const auto parent = ExportRuntimeParent(objects[static_cast<std::size_t>(bone)]);
                const auto parentSlot = boneSlots.find(parent);
                const glm::mat4 parentWorld =
                    parentSlot == boneSlots.end()
                        ? meshBind
                        : glm::make_mat4(objects[static_cast<std::size_t>(parent)].worldTransform);
                const glm::mat4 local =
                    (modelOptions ? InvertModelMatrix(parentWorld) : glm::inverse(parentWorld)) *
                    glm::make_mat4(objects[static_cast<std::size_t>(bone)].worldTransform);
                if (modelOptions)
                    RequireFiniteModelMatrix(local);
                jointNodes[slot] = static_cast<int>(nodes.size());
                nodes.push_back({{"name", ExportObjectName(objects[static_cast<std::size_t>(bone)])},
                                 {"matrix", ExportMatrix(local)},
                                 {"children", json::array()}});
            }
            for (std::size_t slot = 0; slot < bones.size(); ++slot)
            {
                const auto parent = ExportRuntimeParent(objects[static_cast<std::size_t>(bones[slot])]);
                const auto parentSlot = boneSlots.find(parent);
                if (parentSlot == boneSlots.end())
                    nodes[rigRoot]["children"].push_back(jointNodes[slot]);
                else
                    nodes[jointNodes[parentSlot->second]]["children"].push_back(jointNodes[slot]);
            }
            Align(binary);
            const std::size_t bindOffset = binary.size();
            for (const auto bone : bones)
            {
                const glm::mat4 boneWorld = glm::make_mat4(objects[static_cast<std::size_t>(bone)].worldTransform);
                const glm::mat4 inverseBind =
                    (modelOptions ? InvertModelMatrix(boneWorld) : glm::inverse(boneWorld)) * meshBind;
                if (modelOptions)
                    RequireFiniteModelMatrix(inverseBind);
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        Put(binary, inverseBind[column][row]);
            }
            const int bindAccessor = addAccessor(bindOffset, binary.size() - bindOffset, 0, 5126, bones.size(), "MAT4");
            json primitives = json::array();
            for (const auto globIndex : selectedSetGlobs)
            {
                if (globIndex >= globs.size())
                    continue;
                const auto &glob = globs[globIndex];
                const glm::mat4 sourceToWorld = glm::make_mat4(glob.sourceToWorld);
                for (const auto &subglob : glob.sourceSubglobs)
                {
                    if (subglob.boneCount == 0)
                        continue;
                    for (const auto &vertex : subglob.vertices)
                        for (int influence = 0; influence < 4; ++influence)
                            if (vertex.boneWeights[influence] != 0.0f && vertex.boneIndices[influence] >= bones.size())
                            {
                                error = "Skinned GLOBSET has a weighted joint index outside its palette.";
                                return false;
                            }
                    appendPrimitive(subglob, worldToMesh * sourceToWorld, sourceToWorld, true, primitives);
                }
            }
            if (primitives.empty())
                continue;
            const int skinIndex = static_cast<int>(skins.size());
            skins.push_back({{"name", "GLOBSET_" + std::to_string(setIndex) + "_Skin"},
                             {"joints", jointNodes},
                             {"skeleton", rigRoot},
                             {"inverseBindMatrices", bindAccessor}});
            const int meshIndex = static_cast<int>(meshes.size());
            meshes.push_back({{"name", ExportObjectName(objects[static_cast<std::size_t>(set.ownerObjectIndex)])},
                              {"primitives", std::move(primitives)}});
            const int meshNode = static_cast<int>(nodes.size());
            nodes.push_back({{"name", "GLOBSET_" + std::to_string(setIndex) + "_Skinned"},
                             {"mesh", meshIndex},
                             {"skin", skinIndex}});
            nodes[rigRoot]["children"].push_back(meshNode);
        }

        if (includeLights && onlyGlob == AllRecords && onlyGlobset == AllRecords && !document.Lights().empty())
        {
            json lightDefinitions = json::array();
            for (std::size_t lightIndex = 0; lightIndex < document.Lights().size(); ++lightIndex)
            {
                const auto &light = document.Lights()[lightIndex];
                if (light.kind < 0 || light.kind > 3)
                    continue;
                const glm::vec3 color =
                    Hsv(light.highlightHsv[0], glm::clamp(light.highlightHsv[1] / 255.0f, 0.0f, 1.0f),
                        glm::clamp(light.highlightHsv[2] / 255.0f, 0.0f, 1.0f));
                const bool directional = light.kind == 0;
                const bool point = light.kind == 1;
                json definition = {{"name", "LIGHT_" + std::to_string(lightIndex)},
                                   {"type", directional ? "directional" : (point ? "point" : "spot")},
                                   {"color", {color.r, color.g, color.b}},
                                   {"intensity", directional ? 5.0f : 50.0f}};
                if (!directional && light.falloff[1] > 0.0f)
                    definition["range"] = light.falloff[1];
                if (!directional && !point)
                {
                    const float outer = glm::radians(glm::clamp(light.coneAngle * 0.5f, 0.01f, 89.9f));
                    const float inner =
                        glm::radians(glm::clamp(light.hotSpotAngle * 0.5f, 0.0f, light.coneAngle * 0.5f));
                    definition["spot"] = {{"innerConeAngle", inner}, {"outerConeAngle", outer}};
                }
                definition["extras"] = {{"projectCaneKind", light.kind},
                                        {"dynamic", light.dynamic},
                                        {"excludeDynamicObjects", light.excludeDynamicObjects},
                                        {"fullStrengthDistance", light.falloff[0]}};
                const int definitionIndex = static_cast<int>(lightDefinitions.size());
                lightDefinitions.push_back(std::move(definition));
                json node = {{"name", "LIGHT_" + std::to_string(lightIndex) + "_Node"},
                             {"extensions", {{"KHR_lights_punctual", {{"light", definitionIndex}}}}}};
                if (!directional)
                    node["translation"] = {light.position[0], light.position[1], light.position[2]};
                if (directional || !point)
                {
                    glm::vec3 direction = SafeNormalize(glm::make_vec3(light.direction));
                    if (glm::dot(direction, direction) == 0.0f)
                        direction = glm::vec3(0, 0, -1);
                    const glm::quat rotation = glm::rotation(glm::vec3(0, 0, -1), direction);
                    node["rotation"] = {rotation.x, rotation.y, rotation.z, rotation.w};
                }
                const int nodeIndex = static_cast<int>(nodes.size());
                nodes.push_back(std::move(node));
                nodes[basisNode]["children"].push_back(nodeIndex);
            }
            if (!lightDefinitions.empty())
            {
                gltf["extensionsUsed"].push_back("KHR_lights_punctual");
                gltf["extensionsRequired"].push_back("KHR_lights_punctual");
                gltf["extensions"]["KHR_lights_punctual"]["lights"] = std::move(lightDefinitions);
            }
        }
        if (primitiveCount == 0)
        {
            error = "The selected export scope contains no exportable triangles.";
            return false;
        }
        Align(binary);
        gltf["buffers"] = json::array({{{"byteLength", binary.size()}}});
        gltf["bufferViews"] = std::move(views);
        gltf["accessors"] = std::move(accessors);
        gltf["materials"] = std::move(materials);
        gltf["meshes"] = std::move(meshes);
        gltf["nodes"] = std::move(nodes);
        if (!skins.empty())
            gltf["skins"] = std::move(skins);
        if (!images.empty())
            gltf["images"] = std::move(images);
        if (!textures.empty())
            gltf["textures"] = std::move(textures);
        if (baked)
        {
            if (!gltf.contains("extensionsUsed"))
                gltf["extensionsUsed"] = json::array();
            gltf["extensionsUsed"].push_back("KHR_materials_unlit");
        }
        gltf["scenes"] = json::array({{{"nodes", json::array({basisNode})}}});
        gltf["scene"] = 0;
        if (modelOptions && modelOptions->format == ModelExportFormat::Obj)
            return WriteSceneObj(gltf, binary, path, error);
        std::string text = gltf.dump();
        while ((text.size() & 3u) != 0)
            text.push_back(' ');
        const std::uint64_t total = 12u + 8u + text.size() + 8u + binary.size();
        if (total > 0xffffffffu)
        {
            error = "GLB exceeds 4 GiB.";
            return false;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "Could not open the GLB output file.";
            return false;
        }
        const auto writeU32 = [&](const std::uint32_t value)
        { output.write(reinterpret_cast<const char *>(&value), sizeof(value)); };
        writeU32(0x46546c67u);
        writeU32(2u);
        writeU32(static_cast<std::uint32_t>(total));
        writeU32(static_cast<std::uint32_t>(text.size()));
        writeU32(0x4e4f534au);
        output.write(text.data(), text.size());
        writeU32(static_cast<std::uint32_t>(binary.size()));
        writeU32(0x004e4942u);
        output.write(reinterpret_cast<const char *>(binary.data()), binary.size());
        output.close();
        if (!output)
        {
            error = "Failed while writing the GLB.";
            return false;
        }
        return true;
    }
    catch (const std::exception &exception)
    {
        error = exception.what();
        return false;
    }
}
}

bool ExportMapGlb(const BrxDocument &document, const std::filesystem::path &path, const bool bakeLights,
                  std::string &error)
{
    // Match ProjectCane: the regular map GLB carries punctual lights, while
    // the baked variant stores their contribution in vertex colors and must
    // not illuminate the scene a second time in Blender.
    return ExportSceneGlb(document, path, bakeLights, AllRecords, AllRecords, !bakeLights, error);
}

bool ExportGlobGlb(const BrxDocument &document, const std::size_t globIndex, const std::filesystem::path &path,
                   std::string &error)
{
    return ExportSceneGlb(document, path, false, globIndex, AllRecords, false, error);
}

bool ExportGlobsetGlb(const BrxDocument &document, const std::size_t globsetIndex, const std::filesystem::path &path,
                      std::string &error)
{
    return ExportSceneGlb(document, path, false, AllRecords, globsetIndex, false, error);
}

bool ExportGlobModel(const BrxDocument &document, const std::size_t globIndex, const std::filesystem::path &path,
                     const ModelExportOptions &options, std::string &error)
{
    if (globIndex == AllRecords)
    {
        error = "Select a GLOB to export.";
        return false;
    }
    return ExportSceneGlb(document, path, false, globIndex, AllRecords, false, error, &options);
}

bool ExportGlobsetModel(const BrxDocument &document, const std::size_t globsetIndex, const std::filesystem::path &path,
                        const ModelExportOptions &options, std::string &error)
{
    if (globsetIndex == AllRecords)
    {
        error = "Select a model group to export.";
        return false;
    }
    return ExportSceneGlb(document, path, false, AllRecords, globsetIndex, false, error, &options);
}
