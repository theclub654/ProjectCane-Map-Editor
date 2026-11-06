#include "ModelImport.h"
#include "BrxDocument.h"
#include "EditorOptions.h"
#include "EditorDocumentOperations.h"
#include "TextureImport.h"
#include <ufbx.h>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <numeric>

namespace
{
struct MeshData
{
    std::string name, materialName;
    std::filesystem::path diffuseTexture;
    std::shared_ptr<const std::vector<std::uint8_t>> embeddedDiffuseTexture;
    std::string diffuseTextureKey;
    std::array<float, 4> materialColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<BrxSourceVertex> vertices;
    std::vector<std::array<std::uint16_t, 3>> triangles;
};

std::string ModelTextureKey(const MeshData &mesh)
{
    return mesh.diffuseTextureKey.empty() ? mesh.diffuseTexture.lexically_normal().string() : mesh.diffuseTextureKey;
}

std::filesystem::path ResolveFbxTexturePath(const std::filesystem::path &model, const std::filesystem::path &inputTexture)
{
#ifdef _WIN32
    const auto &texture = inputTexture;
#else
    auto textureName = inputTexture.string();
    std::replace(textureName.begin(), textureName.end(), '\\', '/');
    const std::filesystem::path texture(textureName);
#endif
    const auto original = texture.is_absolute() ? texture : model.parent_path() / texture;
    const std::array candidates{original, model.parent_path() / texture.filename(),
                                model.parent_path() / (model.stem().string() + ".fbm") / texture.filename(),
                                model.parent_path() / "textures" / texture.filename()};
    for (const auto &candidate : candidates)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
            return candidate;
    }
    return original;
}

struct FbxRigBone
{
    const ufbx_node *node = nullptr;
    std::string name;
    std::int32_t parent = -1;
    glm::mat4 world{1.0f};
};
struct FbxRigClip
{
    std::string name;
    float duration = 0.0f;
    std::vector<std::vector<BrxAsegVectorKey>> positions, rotations, scales;
};
struct FbxRigCapture
{
    float sampleRate = 60.0f;
    bool importAnimations = true;
    std::vector<FbxRigBone> bones;
    std::unordered_map<const ufbx_node *, std::uint16_t> palette;
    std::vector<FbxRigClip> clips;
};
thread_local FbxRigCapture *g_fbxRigCapture = nullptr;

glm::mat4 FbxMatrix(const ufbx_matrix &source, const float translationScale)
{
    glm::mat4 result(1.0f);
    for (int column = 0; column < 3; ++column)
        result[column] = glm::vec4(static_cast<float>(source.cols[column].x), static_cast<float>(source.cols[column].y),
                                   static_cast<float>(source.cols[column].z), 0.0f);
    result[3] = glm::vec4(static_cast<float>(source.cols[3].x) * translationScale,
                          static_cast<float>(source.cols[3].y) * translationScale,
                          static_cast<float>(source.cols[3].z) * translationScale, 1.0f);
    return result;
}
bool FbxRigidMatrix(const ufbx_matrix &source, const float translationScale, glm::mat4 &result, std::string &error)
{
    result = FbxMatrix(source, translationScale);
    for (int axis = 0; axis < 3; ++axis)
    {
        const float length = glm::length(glm::vec3(result[axis]));
        if (!std::isfinite(length) || length <= 1.0e-12f)
        {
            error = "FBX skeleton contains a collapsed or non-finite bone transform.";
            return false;
        }
        result[axis] /= length;
    }
    const float determinant = glm::determinant(glm::mat3(result));
    if (!std::isfinite(determinant) || std::abs(determinant - 1.0f) > 0.001f || !std::isfinite(result[3].x) ||
        !std::isfinite(result[3].y) || !std::isfinite(result[3].z))
    {
        error = "FBX skeleton contains shear or a mirrored basis that cannot be converted to ProjectCane joints.";
        return false;
    }
    return true;
}
ufbx_vec3 FbxProjectCaneEuler(const glm::mat4 &local)
{
    ufbx_matrix matrix{};
    for (int column = 0; column < 4; ++column)
    {
        matrix.cols[column].x = local[column].x;
        matrix.cols[column].y = local[column].y;
        matrix.cols[column].z = local[column].z;
    }
    const auto transform = ufbx_matrix_to_transform(&matrix);
    // ASEG rotation controllers use Z*Y*X, independently of FBX node order.
    return ufbx_quat_to_euler(transform.rotation, UFBX_ROTATION_ORDER_XYZ);
}
void GenerateMissingNormals(MeshData &mesh)
{
    bool missing = false;
    for (const auto &vertex : mesh.vertices)
    {
        const glm::vec3 n(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
        missing |= glm::dot(n, n) < 0.000001f;
    }
    if (!missing)
        return;
    std::vector<glm::vec3> sums(mesh.vertices.size());
    for (const auto &triangle : mesh.triangles)
    {
        const auto point = [&](std::uint16_t i)
        {
            const auto &v = mesh.vertices[i];
            return glm::vec3(v.position[0], v.position[1], v.position[2]);
        };
        const auto n = glm::cross(point(triangle[1]) - point(triangle[0]), point(triangle[2]) - point(triangle[0]));
        sums[triangle[0]] += n;
        sums[triangle[1]] += n;
        sums[triangle[2]] += n;
    }
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
    {
        auto n = glm::dot(sums[i], sums[i]) > 0.000001f ? glm::normalize(sums[i]) : glm::vec3(0, 1, 0);
        mesh.vertices[i].normal[0] = n.x;
        mesh.vertices[i].normal[1] = n.y;
        mesh.vertices[i].normal[2] = n.z;
    }
}
std::vector<std::uint8_t> ReadBytes(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return f ? std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), {}) : std::vector<std::uint8_t>{};
}
bool ParseObj(const std::filesystem::path &p, const ModelImportOptions &o, std::vector<MeshData> &out,
              std::string &error)
{
    std::ifstream f(p);
    if (!f)
    {
        error = "Could not open OBJ.";
        return false;
    }
    struct ObjMaterial
    {
        std::array<float, 4> color{1, 1, 1, 1};
        std::filesystem::path texture;
    };
    std::unordered_map<std::string, ObjMaterial> materials;
    const auto readMtl = [&](const std::filesystem::path &file)
    {
        std::ifstream stream(file);
        if (!stream)
            return;
        std::string line, current;
        while (std::getline(stream, line))
        {
            std::istringstream row(line);
            std::string tag;
            row >> tag;
            if (tag == "newmtl")
                row >> current;
            else if (!current.empty() && tag == "Kd")
                row >> materials[current].color[0] >> materials[current].color[1] >> materials[current].color[2];
            else if (!current.empty() && (tag == "d" || tag == "Tr"))
            {
                float alpha = 1;
                row >> alpha;
                materials[current].color[3] = tag == "Tr" ? 1 - alpha : alpha;
            }
            else if (!current.empty() && tag == "map_Kd")
            {
                std::string name;
                std::getline(row, name);
                const auto first = name.find_first_not_of(" \t");
                if (first != std::string::npos)
                    materials[current].texture = file.parent_path() / name.substr(first);
            }
        }
    };
    std::vector<glm::vec3> pos, nrm;
    std::vector<glm::vec2> uv;
    std::vector<MeshData> parts(1);
    parts.back().name = p.stem().string();
    std::vector<std::unordered_map<std::string, std::uint16_t>> caches(1);
    std::string line, objectName = p.stem().string();
    auto index = [&](int i, std::size_t n) -> int { return i > 0 ? i - 1 : i < 0 ? static_cast<int>(n) + i : -1; };
    while (std::getline(f, line))
    {
        std::istringstream s(line);
        std::string tag;
        s >> tag;
        if (tag == "mtllib")
        {
            std::string name;
            std::getline(s, name);
            const auto first = name.find_first_not_of(" \t");
            if (first != std::string::npos)
                readMtl(p.parent_path() / name.substr(first));
        }
        if (tag == "v")
        {
            glm::vec3 v;
            s >> v.x >> v.y >> v.z;
            pos.push_back(v * o.scale);
        }
        else if (tag == "vn")
        {
            glm::vec3 v;
            s >> v.x >> v.y >> v.z;
            nrm.push_back(glm::length(v) > 0 ? glm::normalize(v) : glm::vec3(0, 1, 0));
        }
        else if (tag == "vt")
        {
            glm::vec2 v;
            s >> v.x >> v.y;
            if (o.flipV)
                v.y = 1 - v.y;
            uv.push_back(v);
        }
        else if (tag == "o" || tag == "g")
        {
            std::string n;
            std::getline(s, n);
            const auto first = n.find_first_not_of(" \t");
            if (first != std::string::npos)
                objectName = n.substr(first);
        }
        else if (tag == "usemtl")
        {
            std::string material;
            s >> material;
            if (parts.back().triangles.empty() && parts.back().vertices.empty())
            {
                parts.back().materialName = material;
                parts.back().name = objectName + (material.empty() ? "" : " / " + material);
            }
            else
            {
                parts.push_back({});
                caches.emplace_back();
                parts.back().materialName = material;
                parts.back().name = objectName + (material.empty() ? "" : " / " + material);
            }
            const auto found = materials.find(material);
            if (found != materials.end())
            {
                parts.back().materialColor = found->second.color;
                parts.back().diffuseTexture = found->second.texture;
            }
        }
        else if (tag == "f")
        {
            std::vector<std::uint16_t> face;
            std::string tok;
            auto &mesh = parts.back();
            auto &cache = caches.back();
            while (s >> tok)
            {
                auto it = cache.find(tok);
                if (it != cache.end())
                {
                    face.push_back(it->second);
                    continue;
                }
                if (mesh.vertices.size() >= 65535)
                {
                    error = "OBJ primitive exceeds 65,535 vertices.";
                    return false;
                }
                int vi = 0, ti = 0, ni = 0;
                char slash = 0;
                std::istringstream q(tok);
                q >> vi;
                if (q.peek() == '/')
                {
                    q >> slash;
                    if (q.peek() != '/')
                        q >> ti;
                    if (q.peek() == '/')
                        q >> slash >> ni;
                }
                BrxSourceVertex v;
                int a = index(vi, pos.size()), b = index(ti, uv.size()), c = index(ni, nrm.size());
                if (a < 0 || a >= static_cast<int>(pos.size()))
                {
                    error = "OBJ face has invalid position index.";
                    return false;
                }
                std::copy(&pos[a].x, &pos[a].x + 3, v.position);
                if (b >= 0 && b < static_cast<int>(uv.size()))
                {
                    v.uv[0] = uv[b].x;
                    v.uv[1] = uv[b].y;
                }
                if (c >= 0 && c < static_cast<int>(nrm.size()))
                    std::copy(&nrm[c].x, &nrm[c].x + 3, v.normal);
                auto id = static_cast<std::uint16_t>(mesh.vertices.size());
                mesh.vertices.push_back(v);
                cache.emplace(tok, id);
                face.push_back(id);
            }
            for (std::size_t i = 2; i < face.size(); ++i)
                mesh.triangles.push_back({face[0], face[i - 1], face[i]});
        }
    }
    for (auto &mesh : parts)
        if (!mesh.triangles.empty())
        {
            if (mesh.triangles.size() > 65535)
            {
                error = "OBJ primitive exceeds 65,535 triangles; split the mesh before importing.";
                return false;
            }
            GenerateMissingNormals(mesh);
            out.push_back(std::move(mesh));
        }
    if (out.empty())
    {
        error = "OBJ contains no triangle faces.";
        return false;
    }
    return true;
}
template <class T> T Scalar(const std::uint8_t *p)
{
    T v{};
    std::memcpy(&v, p, sizeof(v));
    return v;
}
bool ParseGlb(const std::filesystem::path &p, const ModelImportOptions &o, std::vector<MeshData> &out,
              std::string &error)
{
    auto bytes = ReadBytes(p);
    if (bytes.size() < 20 || Scalar<std::uint32_t>(bytes.data()) != 0x46546c67)
    {
        error = "Not a GLB 2.0 file.";
        return false;
    }
    std::size_t at = 12;
    nlohmann::json j;
    const std::uint8_t *bin = nullptr;
    std::size_t binSize = 0;
    while (at + 8 <= bytes.size())
    {
        auto n = Scalar<std::uint32_t>(&bytes[at]);
        auto t = Scalar<std::uint32_t>(&bytes[at + 4]);
        at += 8;
        if (at + n > bytes.size())
        {
            error = "GLB chunk exceeds file.";
            return false;
        }
        if (t == 0x4e4f534a)
            j = nlohmann::json::parse(bytes.begin() + at, bytes.begin() + at + n);
        else if (t == 0x004e4942)
        {
            bin = &bytes[at];
            binSize = n;
        }
        at += n;
    }
    if (j.empty() || !bin)
    {
        error = "GLB lacks JSON or BIN chunk.";
        return false;
    }
    auto accessor = [&](int ai, int want, std::vector<float> &values) -> bool
    {
        auto &a = j["accessors"][ai];
        auto &v = j["bufferViews"][a["bufferView"].get<int>()];
        int ct = a["componentType"], count = a["count"],
            components = a["type"] == "VEC3"   ? 3
                         : a["type"] == "VEC2" ? 2
                                               : 1;
        if (components != want || ct != 5126)
            return false;
        std::size_t off = v.value("byteOffset", 0) + a.value("byteOffset", 0),
                    stride = v.value("byteStride", components * 4);
        if (off + stride * (count - 1) + components * 4 > binSize)
            return false;
        values.resize(count * components);
        for (int x = 0; x < count; ++x)
            std::memcpy(values.data() + x * components, bin + off + x * stride, components * 4);
        return true;
    };
    auto indices = [&](int ai, std::vector<std::uint32_t> &values) -> bool
    {
        auto &a = j["accessors"][ai];
        auto &v = j["bufferViews"][a["bufferView"].get<int>()];
        int ct = a["componentType"], count = a["count"];
        std::size_t sz = ct == 5125   ? 4
                         : ct == 5123 ? 2
                         : ct == 5121 ? 1
                                      : 0,
                    off = v.value("byteOffset", 0) + a.value("byteOffset", 0), stride = v.value("byteStride", sz);
        if (!sz || off + stride * (count - 1) + sz > binSize)
            return false;
        values.resize(count);
        for (int x = 0; x < count; ++x)
            values[x] = ct == 5125   ? Scalar<std::uint32_t>(bin + off + x * stride)
                        : ct == 5123 ? Scalar<std::uint16_t>(bin + off + x * stride)
                                     : bin[off + x * stride];
        return true;
    };
    for (std::size_t mi = 0; mi < j["meshes"].size(); ++mi)
        for (auto &pr : j["meshes"][mi]["primitives"])
        {
            if (pr.value("mode", 4) != 4)
                continue;
            MeshData m;
            m.name = j["meshes"][mi].value("name", "GLB Mesh " + std::to_string(mi));
            std::vector<float> ps, ns, ts;
            std::vector<std::uint32_t> is;
            if (!accessor(pr["attributes"]["POSITION"], 3, ps))
            {
                error = "GLB POSITION must be float VEC3.";
                return false;
            }
            if (ps.size() / 3 > 65535)
            {
                error = "GLB primitive exceeds 65,535 vertices.";
                return false;
            }
            if (pr["attributes"].contains("NORMAL"))
                accessor(pr["attributes"]["NORMAL"], 3, ns);
            if (pr["attributes"].contains("TEXCOORD_0"))
                accessor(pr["attributes"]["TEXCOORD_0"], 2, ts);
            if (pr.contains("indices"))
            {
                if (!indices(pr["indices"], is))
                {
                    error = "Unsupported GLB index accessor.";
                    return false;
                }
            }
            else
            {
                is.resize(ps.size() / 3);
                for (std::size_t i = 0; i < is.size(); ++i)
                    is[i] = static_cast<std::uint32_t>(i);
            }
            m.vertices.resize(ps.size() / 3);
            for (std::size_t i = 0; i < m.vertices.size(); ++i)
            {
                for (int k = 0; k < 3; ++k)
                    m.vertices[i].position[k] = ps[i * 3 + k] * o.scale;
                if (ns.size() >= i * 3 + 3)
                    for (int k = 0; k < 3; ++k)
                        m.vertices[i].normal[k] = ns[i * 3 + k];
                if (ts.size() >= i * 2 + 2)
                {
                    m.vertices[i].uv[0] = ts[i * 2];
                    m.vertices[i].uv[1] = o.flipV ? 1 - ts[i * 2 + 1] : ts[i * 2 + 1];
                }
            }
            for (std::size_t i = 0; i + 2 < is.size(); i += 3)
            {
                if (is[i] >= m.vertices.size() || is[i + 1] >= m.vertices.size() || is[i + 2] >= m.vertices.size())
                {
                    error = "GLB index is out of range.";
                    return false;
                }
                m.triangles.push_back({static_cast<std::uint16_t>(is[i]), static_cast<std::uint16_t>(is[i + 1]),
                                       static_cast<std::uint16_t>(is[i + 2])});
            }
            GenerateMissingNormals(m);
            out.push_back(std::move(m));
        }
    std::size_t importedPrimitive = 0;
    std::unordered_map<int, std::shared_ptr<const std::vector<std::uint8_t>>> embeddedGlbImages;
    if (j.contains("materials"))
        for (std::size_t meshIndex = 0; meshIndex < j["meshes"].size(); ++meshIndex)
            for (const auto &primitive : j["meshes"][meshIndex]["primitives"])
            {
                if (primitive.value("mode", 4) != 4)
                    continue;
                if (importedPrimitive >= out.size())
                    break;
                auto &mesh = out[importedPrimitive++];
                if (!primitive.contains("material"))
                    continue;
                const auto materialIndex = primitive["material"].get<std::size_t>();
                if (materialIndex >= j["materials"].size())
                    continue;
                const auto &material = j["materials"][materialIndex];
                mesh.materialName = material.value("name", "GLB Material " + std::to_string(materialIndex));
                if (!material.contains("pbrMetallicRoughness"))
                    continue;
                const auto &pbr = material["pbrMetallicRoughness"];
                if (pbr.contains("baseColorFactor"))
                {
                    const auto &factor = pbr["baseColorFactor"];
                    for (std::size_t channel = 0; channel < 4 && channel < factor.size(); ++channel)
                        mesh.materialColor[channel] = factor[channel].get<float>();
                }
                if (pbr.contains("baseColorTexture") && j.contains("textures") && j.contains("images"))
                {
                    const int textureIndex = pbr["baseColorTexture"].value("index", -1);
                    if (textureIndex >= 0 && static_cast<std::size_t>(textureIndex) < j["textures"].size())
                    {
                        const int imageIndex = j["textures"][textureIndex].value("source", -1);
                        if (imageIndex >= 0 && static_cast<std::size_t>(imageIndex) < j["images"].size())
                        {
                            const auto &image = j["images"][imageIndex];
                            if (image.contains("uri"))
                                mesh.diffuseTexture = p.parent_path() / image["uri"].get<std::string>();
                            else if (image.contains("bufferView"))
                            {
                                const int viewIndex = image["bufferView"].get<int>();
                                if (!j.contains("bufferViews") || viewIndex < 0 ||
                                    static_cast<std::size_t>(viewIndex) >= j["bufferViews"].size())
                                {
                                    error = "GLB material image references an invalid bufferView.";
                                    return false;
                                }
                                const auto &view = j["bufferViews"][viewIndex];
                                const auto offset = view.value("byteOffset", std::size_t{0});
                                const auto length = view.value("byteLength", std::size_t{0});
                                if (view.value("buffer", 0) != 0 || offset > binSize ||
                                    length == 0 || length > binSize - offset)
                                {
                                    error = "GLB material image exceeds its embedded BIN buffer.";
                                    return false;
                                }
                                auto found = embeddedGlbImages.find(imageIndex);
                                if (found == embeddedGlbImages.end())
                                    found = embeddedGlbImages.emplace(imageIndex,
                                        std::make_shared<const std::vector<std::uint8_t>>(
                                            bin + offset, bin + offset + length)).first;
                                mesh.embeddedDiffuseTexture = found->second;
                                mesh.diffuseTextureKey = p.lexically_normal().string() +
                                    "|GLB embedded " + std::to_string(imageIndex);
                            }
                        }
                    }
                }
            }
    if (out.empty())
    {
        error = "GLB contains no triangle primitives.";
        return false;
    }
    return true;
}

std::vector<std::uint8_t> DecodeBase64(const std::string &source)
{
    static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> result;
    int value = 0, bits = -8;
    for (const unsigned char character : source)
    {
        if (character == '=')
            break;
        const auto position = alphabet.find(static_cast<char>(character));
        if (position == std::string_view::npos)
            continue;
        value = (value << 6) | static_cast<int>(position);
        bits += 6;
        if (bits >= 0)
        {
            result.push_back(static_cast<std::uint8_t>((value >> bits) & 255));
            bits -= 8;
        }
    }
    return result;
}
std::string DecodeUri(std::string value);

bool ParseGltf(const std::filesystem::path &path, const ModelImportOptions &options, std::vector<MeshData> &out,
               std::string &error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "Could not open glTF file.";
        return false;
    }
    nlohmann::json json;
    try
    {
        stream >> json;
    }
    catch (const std::exception &exception)
    {
        error = std::string("Invalid glTF JSON: ") + exception.what();
        return false;
    }
    if (!json.contains("asset") || json["asset"].value("version", "").rfind("2", 0) != 0)
    {
        error = "Only glTF 2.x files are supported.";
        return false;
    }
    if (!json.contains("buffers") || json["buffers"].empty())
    {
        error = "glTF contains no buffers.";
        return false;
    }
    std::vector<std::vector<std::uint8_t>> buffers;
    for (const auto &descriptor : json["buffers"])
    {
        const std::string uri = descriptor.value("uri", "");
        if (uri.empty())
        {
            error = "A .gltf buffer has no URI; use GLB for an embedded BIN chunk.";
            return false;
        }
        std::vector<std::uint8_t> bytes;
        if (uri.rfind("data:", 0) == 0)
        {
            const auto comma = uri.find(',');
            if (comma == std::string::npos || uri.substr(0, comma).find(";base64") == std::string::npos)
            {
                error = "glTF uses an unsupported non-base64 data URI.";
                return false;
            }
            bytes = DecodeBase64(uri.substr(comma + 1));
        }
        else
            bytes = ReadBytes(path.parent_path() / DecodeUri(uri));
        if (bytes.size() < descriptor.value("byteLength", std::size_t(0)))
        {
            error = "A glTF buffer is missing or shorter than its declared byteLength.";
            return false;
        }
        buffers.push_back(std::move(bytes));
    }
    const auto accessorBytes = [&](const int accessorIndex, const int components, std::vector<float> &values) -> bool
    {
        if (!json.contains("accessors") || accessorIndex < 0 || std::size_t(accessorIndex) >= json["accessors"].size())
            return false;
        const auto &accessor = json["accessors"][accessorIndex];
        if (!accessor.contains("bufferView") || accessor.value("componentType", 0) != 5126)
            return false;
        const auto &view = json["bufferViews"][accessor["bufferView"].get<std::size_t>()];
        const int actual = accessor.value("type", "") == "VEC4"   ? 4
                           : accessor.value("type", "") == "VEC3" ? 3
                           : accessor.value("type", "") == "VEC2" ? 2
                                                                  : 1;
        if (actual != components)
            return false;
        const std::size_t bufferIndex = view.value("buffer", std::size_t(0));
        if (bufferIndex >= buffers.size())
            return false;
        const std::size_t count = accessor.value("count", std::size_t(0)),
                          stride = view.value("byteStride", std::size_t(components * 4));
        const std::size_t offset =
            view.value("byteOffset", std::size_t(0)) + accessor.value("byteOffset", std::size_t(0));
        if (count && (offset > buffers[bufferIndex].size() ||
                      stride * (count - 1) + components * 4 > buffers[bufferIndex].size() - offset))
            return false;
        values.resize(count * components);
        for (std::size_t i = 0; i < count; ++i)
            std::memcpy(values.data() + i * components, buffers[bufferIndex].data() + offset + i * stride,
                        components * 4);
        return true;
    };
    const auto indexBytes = [&](const int accessorIndex, std::vector<std::uint32_t> &values) -> bool
    {
        const auto &accessor = json["accessors"][accessorIndex];
        if (!accessor.contains("bufferView"))
            return false;
        const auto &view = json["bufferViews"][accessor["bufferView"].get<std::size_t>()];
        const int type = accessor.value("componentType", 0);
        const std::size_t size = type == 5125 ? 4 : type == 5123 ? 2 : type == 5121 ? 1 : 0;
        const std::size_t bufferIndex = view.value("buffer", std::size_t(0));
        if (!size || bufferIndex >= buffers.size())
            return false;
        const std::size_t count = accessor.value("count", std::size_t(0)), stride = view.value("byteStride", size);
        const std::size_t offset =
            view.value("byteOffset", std::size_t(0)) + accessor.value("byteOffset", std::size_t(0));
        if (count && (offset > buffers[bufferIndex].size() ||
                      stride * (count - 1) + size > buffers[bufferIndex].size() - offset))
            return false;
        values.resize(count);
        const auto *data = buffers[bufferIndex].data() + offset;
        for (std::size_t i = 0; i < count; ++i)
            values[i] = type == 5125   ? Scalar<std::uint32_t>(data + i * stride)
                        : type == 5123 ? Scalar<std::uint16_t>(data + i * stride)
                                       : data[i * stride];
        return true;
    };
    if (!json.contains("meshes"))
    {
        error = "glTF contains no meshes.";
        return false;
    }
    for (std::size_t meshIndex = 0; meshIndex < json["meshes"].size(); ++meshIndex)
        for (const auto &primitive : json["meshes"][meshIndex]["primitives"])
        {
            if (primitive.value("mode", 4) != 4)
                continue;
            MeshData mesh;
            mesh.name = json["meshes"][meshIndex].value("name", "glTF Mesh " + std::to_string(meshIndex));
            std::vector<float> positions, normals, texcoords, colors;
            std::vector<std::uint32_t> indices;
            if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION") ||
                !accessorBytes(primitive["attributes"]["POSITION"], 3, positions))
            {
                error = "glTF POSITION must be a float VEC3 accessor.";
                return false;
            }
            if (positions.size() / 3 > 65535)
            {
                error = "glTF primitive exceeds 65,535 vertices.";
                return false;
            }
            if (primitive["attributes"].contains("NORMAL"))
                accessorBytes(primitive["attributes"]["NORMAL"], 3, normals);
            if (primitive["attributes"].contains("TEXCOORD_0"))
                accessorBytes(primitive["attributes"]["TEXCOORD_0"], 2, texcoords);
            if (primitive["attributes"].contains("COLOR_0"))
            {
                if (!accessorBytes(primitive["attributes"]["COLOR_0"], 4, colors))
                    accessorBytes(primitive["attributes"]["COLOR_0"], 3, colors);
            }
            if (primitive.contains("indices"))
            {
                if (!indexBytes(primitive["indices"], indices))
                {
                    error = "Unsupported glTF index accessor.";
                    return false;
                }
            }
            else
            {
                indices.resize(positions.size() / 3);
                for (std::size_t i = 0; i < indices.size(); ++i)
                    indices[i] = static_cast<std::uint32_t>(i);
            }
            mesh.vertices.resize(positions.size() / 3);
            const std::size_t colorComponents = colors.size() == mesh.vertices.size() * 4   ? 4
                                                : colors.size() == mesh.vertices.size() * 3 ? 3
                                                                                            : 0;
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
            {
                for (int c = 0; c < 3; ++c)
                    mesh.vertices[i].position[c] = positions[i * 3 + c] * options.scale;
                if (normals.size() >= i * 3 + 3)
                    for (int c = 0; c < 3; ++c)
                        mesh.vertices[i].normal[c] = normals[i * 3 + c];
                if (texcoords.size() >= i * 2 + 2)
                {
                    mesh.vertices[i].uv[0] = texcoords[i * 2];
                    mesh.vertices[i].uv[1] = options.flipV ? 1.0f - texcoords[i * 2 + 1] : texcoords[i * 2 + 1];
                }
                if (colorComponents)
                    for (std::size_t c = 0; c < colorComponents; ++c)
                        mesh.vertices[i].color[c] = colors[i * colorComponents + c];
            }
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                if (indices[i] >= mesh.vertices.size() || indices[i + 1] >= mesh.vertices.size() ||
                    indices[i + 2] >= mesh.vertices.size())
                {
                    error = "glTF index is out of range.";
                    return false;
                }
                mesh.triangles.push_back(
                    {std::uint16_t(indices[i]), std::uint16_t(indices[i + 1]), std::uint16_t(indices[i + 2])});
            }
            if (mesh.triangles.size() > 65535)
            {
                error = "glTF primitive exceeds 65,535 triangles.";
                return false;
            }
            if (primitive.contains("material") && json.contains("materials"))
            {
                const auto materialIndex = primitive["material"].get<std::size_t>();
                if (materialIndex < json["materials"].size())
                {
                    const auto &material = json["materials"][materialIndex];
                    mesh.materialName = material.value("name", "glTF Material");
                    if (material.contains("pbrMetallicRoughness"))
                    {
                        const auto &pbr = material["pbrMetallicRoughness"];
                        if (pbr.contains("baseColorFactor"))
                            for (std::size_t c = 0; c < 4 && c < pbr["baseColorFactor"].size(); ++c)
                                mesh.materialColor[c] = pbr["baseColorFactor"][c].get<float>();
                        if (pbr.contains("baseColorTexture") && json.contains("textures") && json.contains("images"))
                        {
                            const int texture = pbr["baseColorTexture"].value("index", -1);
                            if (texture >= 0 && std::size_t(texture) < json["textures"].size())
                            {
                                const int image = json["textures"][texture].value("source", -1);
                                if (image >= 0 && std::size_t(image) < json["images"].size() &&
                                    json["images"][image].contains("uri"))
                                {
                                    const std::string uri = json["images"][image]["uri"];
                                    if (uri.rfind("data:", 0) != 0)
                                        mesh.diffuseTexture = path.parent_path() / DecodeUri(uri);
                                }
                            }
                        }
                    }
                }
            }
            GenerateMissingNormals(mesh);
            out.push_back(std::move(mesh));
        }
    if (out.empty())
    {
        error = "glTF contains no supported triangle primitives.";
        return false;
    }
    return true;
}

bool ParseStl(const std::filesystem::path &path, const ModelImportOptions &options, std::vector<MeshData> &out,
              std::string &error)
{
    const auto bytes = ReadBytes(path);
    if (bytes.empty())
    {
        error = "Could not open STL file.";
        return false;
    }
    MeshData mesh;
    mesh.name = path.stem().string();
    const bool binary =
        bytes.size() >= 84 && 84ull + std::uint64_t(Scalar<std::uint32_t>(bytes.data() + 80)) * 50ull <= bytes.size();
    const auto addTriangle = [&](const glm::vec3 &normal, const glm::vec3 points[3]) -> bool
    {
        if (mesh.vertices.size() > 65532 || mesh.triangles.size() >= 65535)
            return false;
        const auto base = static_cast<std::uint16_t>(mesh.vertices.size());
        for (int i = 0; i < 3; ++i)
        {
            BrxSourceVertex vertex{};
            vertex.position[0] = points[i].x * options.scale;
            vertex.position[1] = points[i].y * options.scale;
            vertex.position[2] = points[i].z * options.scale;
            vertex.normal[0] = normal.x;
            vertex.normal[1] = normal.y;
            vertex.normal[2] = normal.z;
            mesh.vertices.push_back(vertex);
        }
        mesh.triangles.push_back({base, std::uint16_t(base + 1), std::uint16_t(base + 2)});
        return true;
    };
    if (binary)
    {
        const std::uint32_t count = Scalar<std::uint32_t>(bytes.data() + 80);
        std::size_t at = 84;
        for (std::uint32_t i = 0; i < count; ++i, at += 50)
        {
            glm::vec3 n(Scalar<float>(bytes.data() + at), Scalar<float>(bytes.data() + at + 4),
                        Scalar<float>(bytes.data() + at + 8));
            glm::vec3 p[3];
            for (int v = 0; v < 3; ++v)
                for (int c = 0; c < 3; ++c)
                    p[v][c] = Scalar<float>(bytes.data() + at + 12 + v * 12 + c * 4);
            if (!addTriangle(n, p))
            {
                error = "STL exceeds the 65,535 triangle/vertex limit; split it before importing.";
                return false;
            }
        }
    }
    else
    {
        std::ifstream stream(path);
        std::string word;
        glm::vec3 normal(0), points[3];
        int vertex = 0;
        while (stream >> word)
        {
            if (word == "facet")
            {
                stream >> word >> normal.x >> normal.y >> normal.z;
            }
            else if (word == "vertex")
            {
                stream >> points[vertex].x >> points[vertex].y >> points[vertex].z;
                if (++vertex == 3)
                {
                    if (!addTriangle(normal, points))
                    {
                        error = "STL exceeds the 65,535 triangle/vertex limit; split it before importing.";
                        return false;
                    }
                    vertex = 0;
                }
            }
        }
        if (vertex != 0)
        {
            error = "ASCII STL ends with an incomplete facet.";
            return false;
        }
    }
    if (mesh.triangles.empty())
    {
        error = "STL contains no triangles.";
        return false;
    }
    GenerateMissingNormals(mesh);
    out.push_back(std::move(mesh));
    return true;
}

bool ParsePly(const std::filesystem::path &path, const ModelImportOptions &options, std::vector<MeshData> &out,
              std::string &error)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        error = "Could not open PLY file.";
        return false;
    }
    std::string line;
    if (!std::getline(stream, line))
    {
        error = "PLY header is missing.";
        return false;
    }
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    if (line != "ply")
    {
        error = "PLY header does not begin with 'ply'.";
        return false;
    }
    enum class Format
    {
        Ascii,
        BinaryLittle
    };
    Format format = Format::Ascii;
    bool formatSeen = false;
    struct Property
    {
        std::string type, name, listCountType, listValueType;
        bool list = false;
    };
    std::vector<Property> vertexProperties, faceProperties;
    std::size_t vertexCount = 0, faceCount = 0;
    std::string element;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (tag == "format")
        {
            std::string name, version;
            row >> name >> version;
            if (name == "ascii")
                format = Format::Ascii;
            else if (name == "binary_little_endian")
                format = Format::BinaryLittle;
            else
            {
                error = "Big-endian PLY is not supported.";
                return false;
            }
            formatSeen = true;
        }
        else if (tag == "element")
        {
            row >> element;
            std::size_t count = 0;
            row >> count;
            if (element == "vertex")
                vertexCount = count;
            else if (element == "face")
                faceCount = count;
        }
        else if (tag == "property")
        {
            Property property;
            std::string type;
            row >> type;
            if (type == "list")
            {
                property.list = true;
                row >> property.listCountType >> property.listValueType >> property.name;
            }
            else
            {
                property.type = type;
                row >> property.name;
            }
            if (element == "vertex")
                vertexProperties.push_back(property);
            else if (element == "face")
                faceProperties.push_back(property);
        }
        else if (tag == "end_header")
            break;
    }
    if (!formatSeen || vertexCount == 0)
    {
        error = "PLY has no valid format or vertices.";
        return false;
    }
    if (vertexCount > 65535)
    {
        error = "PLY exceeds 65,535 vertices; split it before importing.";
        return false;
    }
    const auto typeSize = [](const std::string &type) -> std::size_t
    {
        if (type == "char" || type == "int8" || type == "uchar" || type == "uint8")
            return 1;
        if (type == "short" || type == "int16" || type == "ushort" || type == "uint16")
            return 2;
        if (type == "int" || type == "int32" || type == "uint" || type == "uint32" || type == "float" ||
            type == "float32")
            return 4;
        if (type == "double" || type == "float64")
            return 8;
        return 0;
    };
    const auto binaryScalar = [&](const std::string &type, double &value) -> bool
    {
        const auto size = typeSize(type);
        std::array<std::uint8_t, 8> bytes{};
        if (!size || !stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size)))
            return false;
        if (type == "char" || type == "int8")
            value = Scalar<std::int8_t>(bytes.data());
        else if (type == "uchar" || type == "uint8")
            value = bytes[0];
        else if (type == "short" || type == "int16")
            value = Scalar<std::int16_t>(bytes.data());
        else if (type == "ushort" || type == "uint16")
            value = Scalar<std::uint16_t>(bytes.data());
        else if (type == "int" || type == "int32")
            value = Scalar<std::int32_t>(bytes.data());
        else if (type == "uint" || type == "uint32")
            value = Scalar<std::uint32_t>(bytes.data());
        else if (type == "float" || type == "float32")
            value = Scalar<float>(bytes.data());
        else
            value = Scalar<double>(bytes.data());
        return true;
    };
    MeshData mesh;
    mesh.name = path.stem().string();
    mesh.vertices.resize(vertexCount);
    for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
    {
        BrxSourceVertex &vertex = mesh.vertices[vertexIndex];
        for (const auto &property : vertexProperties)
        {
            if (property.list)
            {
                error = "PLY vertex list properties are unsupported.";
                return false;
            }
            double value = 0;
            if (format == Format::Ascii)
            {
                if (!(stream >> value))
                {
                    error = "PLY vertex table is truncated.";
                    return false;
                }
            }
            else if (!binaryScalar(property.type, value))
            {
                error = "PLY binary vertex table is truncated.";
                return false;
            }
            if (property.name == "x")
                vertex.position[0] = float(value) * options.scale;
            else if (property.name == "y")
                vertex.position[1] = float(value) * options.scale;
            else if (property.name == "z")
                vertex.position[2] = float(value) * options.scale;
            else if (property.name == "nx")
                vertex.normal[0] = float(value);
            else if (property.name == "ny")
                vertex.normal[1] = float(value);
            else if (property.name == "nz")
                vertex.normal[2] = float(value);
            else if (property.name == "u" || property.name == "s" || property.name == "texture_u")
                vertex.uv[0] = float(value);
            else if (property.name == "v" || property.name == "t" || property.name == "texture_v")
                vertex.uv[1] = options.flipV ? 1.0f - float(value) : float(value);
            else if (property.name == "red" || property.name == "r")
                vertex.color[0] = float(value > (1.0) ? value / 255.0 : value);
            else if (property.name == "green" || property.name == "g")
                vertex.color[1] = float(value > (1.0) ? value / 255.0 : value);
            else if (property.name == "blue" || property.name == "b")
                vertex.color[2] = float(value > (1.0) ? value / 255.0 : value);
            else if (property.name == "alpha" || property.name == "a")
                vertex.color[3] = float(value > (1.0) ? value / 255.0 : value);
        }
    }
    for (std::size_t faceIndex = 0; faceIndex < faceCount; ++faceIndex)
    {
        std::vector<std::uint32_t> polygon;
        for (const auto &property : faceProperties)
        {
            if (property.list)
            {
                double countValue = 0;
                if (format == Format::Ascii)
                {
                    if (!(stream >> countValue))
                    {
                        error = "PLY face table is truncated.";
                        return false;
                    }
                }
                else if (!binaryScalar(property.listCountType, countValue))
                {
                    error = "PLY binary face list is truncated.";
                    return false;
                }
                if (countValue < 0 || countValue > 1000000)
                {
                    error = "PLY face has an invalid list length.";
                    return false;
                }
                std::vector<std::uint32_t> values(static_cast<std::size_t>(countValue));
                for (auto &value : values)
                {
                    double decoded = 0;
                    if (format == Format::Ascii)
                    {
                        if (!(stream >> decoded))
                        {
                            error = "PLY face indices are truncated.";
                            return false;
                        }
                    }
                    else if (!binaryScalar(property.listValueType, decoded))
                    {
                        error = "PLY binary face indices are truncated.";
                        return false;
                    }
                    if (decoded < 0 || decoded >= double(vertexCount))
                    {
                        error = "PLY face index is out of range.";
                        return false;
                    }
                    value = static_cast<std::uint32_t>(decoded);
                }
                if (property.name == "vertex_indices" || property.name == "vertex_index")
                    polygon = std::move(values);
            }
            else
            {
                double ignored = 0;
                if (format == Format::Ascii)
                {
                    if (!(stream >> ignored))
                    {
                        error = "PLY face property is truncated.";
                        return false;
                    }
                }
                else if (!binaryScalar(property.type, ignored))
                {
                    error = "PLY binary face property is truncated.";
                    return false;
                }
            }
        }
        for (std::size_t i = 2; i < polygon.size(); ++i)
        {
            if (mesh.triangles.size() >= 65535)
            {
                error = "PLY exceeds 65,535 triangles; split it before importing.";
                return false;
            }
            mesh.triangles.push_back(
                {std::uint16_t(polygon[0]), std::uint16_t(polygon[i - 1]), std::uint16_t(polygon[i])});
        }
    }
    if (mesh.triangles.empty())
    {
        error = "PLY contains no polygon faces.";
        return false;
    }
    GenerateMissingNormals(mesh);
    out.push_back(std::move(mesh));
    return true;
}

struct XmlNode
{
    std::string name, text;
    std::unordered_map<std::string, std::string> attributes;
    std::vector<std::unique_ptr<XmlNode>> children;
    const XmlNode *Child(const std::string &wanted) const
    {
        for (const auto &child : children)
            if (child->name == wanted)
                return child.get();
        return nullptr;
    }
    std::vector<const XmlNode *> Children(const std::string &wanted) const
    {
        std::vector<const XmlNode *> result;
        for (const auto &child : children)
            if (child->name == wanted)
                result.push_back(child.get());
        return result;
    }
    std::string Attribute(const std::string &key) const
    {
        const auto found = attributes.find(key);
        return found == attributes.end() ? std::string{} : found->second;
    }
};

class XmlParser
{
  public:
    explicit XmlParser(std::string source) : source_(std::move(source)) {}
    std::unique_ptr<XmlNode> Parse(std::string &error)
    {
        while (SkipSpecial())
        {
        }
        auto root = ParseNode(error);
        if (!root && error.empty())
            error = "COLLADA XML has no root element.";
        return root;
    }

  private:
    bool SkipSpecial()
    {
        SkipSpace();
        if (source_.compare(at_, 4, "<!--") == 0)
        {
            const auto end = source_.find("-->", at_ + 4);
            at_ = end == std::string::npos ? source_.size() : end + 3;
            return true;
        }
        if (source_.compare(at_, 2, "<?") == 0)
        {
            const auto end = source_.find("?>", at_ + 2);
            at_ = end == std::string::npos ? source_.size() : end + 2;
            return true;
        }
        if (source_.compare(at_, 2, "<!") == 0)
        {
            const auto end = source_.find('>', at_ + 2);
            at_ = end == std::string::npos ? source_.size() : end + 1;
            return true;
        }
        return false;
    }
    void SkipSpace()
    {
        while (at_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[at_])))
            ++at_;
    }
    std::string Name()
    {
        const auto begin = at_;
        while (at_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[at_])) || source_[at_] == '_' ||
                                        source_[at_] == '-' || source_[at_] == ':'))
            ++at_;
        auto result = source_.substr(begin, at_ - begin);
        const auto colon = result.find(':');
        return colon == std::string::npos ? result : result.substr(colon + 1);
    }
    std::unique_ptr<XmlNode> ParseNode(std::string &error)
    {
        SkipSpace();
        if (at_ >= source_.size() || source_[at_] != '<')
        {
            error = "Malformed COLLADA XML element.";
            return {};
        }
        ++at_;
        auto node = std::make_unique<XmlNode>();
        node->name = Name();
        if (node->name.empty())
        {
            error = "COLLADA XML element has no name.";
            return {};
        }
        for (;;)
        {
            SkipSpace();
            if (at_ >= source_.size())
            {
                error = "Truncated COLLADA XML element.";
                return {};
            }
            if (source_.compare(at_, 2, "/>") == 0)
            {
                at_ += 2;
                return node;
            }
            if (source_[at_] == '>')
            {
                ++at_;
                break;
            }
            const auto key = Name();
            SkipSpace();
            if (key.empty() || at_ >= source_.size() || source_[at_] != '=')
            {
                error = "Malformed COLLADA XML attribute.";
                return {};
            }
            ++at_;
            SkipSpace();
            if (at_ >= source_.size() || (source_[at_] != '\'' && source_[at_] != '"'))
            {
                error = "Malformed COLLADA XML attribute value.";
                return {};
            }
            const char quote = source_[at_++];
            const auto begin = at_;
            const auto end = source_.find(quote, at_);
            if (end == std::string::npos)
            {
                error = "Truncated COLLADA XML attribute.";
                return {};
            }
            node->attributes[key] = source_.substr(begin, end - begin);
            at_ = end + 1;
        }
        for (;;)
        {
            if (at_ >= source_.size())
            {
                error = "Truncated COLLADA XML body.";
                return {};
            }
            if (source_.compare(at_, 4, "<!--") == 0)
            {
                SkipSpecial();
                continue;
            }
            if (source_.compare(at_, 2, "</") == 0)
            {
                at_ += 2;
                const auto close = Name();
                SkipSpace();
                if (at_ >= source_.size() || source_[at_] != '>' || close != node->name)
                {
                    error = "Mismatched COLLADA XML closing element.";
                    return {};
                }
                ++at_;
                return node;
            }
            if (source_[at_] == '<')
            {
                auto child = ParseNode(error);
                if (!child)
                    return {};
                node->children.push_back(std::move(child));
            }
            else
            {
                const auto begin = at_;
                const auto end = source_.find('<', at_);
                node->text += source_.substr(begin, end == std::string::npos ? source_.size() - begin : end - begin);
                at_ = end == std::string::npos ? source_.size() : end;
            }
        }
    }
    std::string source_;
    std::size_t at_ = 0;
};

template <class T> std::vector<T> ParseNumbers(const std::string &text)
{
    std::vector<T> result;
    std::istringstream stream(text);
    T value{};
    while (stream >> value)
        result.push_back(value);
    return result;
}
std::string StripHash(std::string value)
{
    if (!value.empty() && value.front() == '#')
        value.erase(value.begin());
    return value;
}
std::string DecodeUri(std::string value)
{
    std::string decoded;
    decoded.reserve(value.size());
    const auto hex = [](const char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (value[index] == '%' && index + 2 < value.size())
        {
            const int high = hex(value[index + 1]), low = hex(value[index + 2]);
            if (high >= 0 && low >= 0)
            {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        decoded.push_back(value[index]);
    }
    constexpr const char *filePrefix = "file://";
    if (decoded.rfind(filePrefix, 0) == 0)
        decoded.erase(0, 7);
    return decoded;
}

struct DaeSource
{
    std::vector<float> values;
    std::size_t stride = 1;
};
struct DaeGeometry
{
    std::string name;
    std::vector<MeshData> meshes;
};
struct DaeController
{
    std::string geometryId;
    glm::mat4 bindShape{1.0f};
};
struct DaeMaterial
{
    std::string name, imageId;
    std::array<float, 4> color{1, 1, 1, 1};
};

bool DecodeDaePrimitive(const XmlNode &primitive, const std::unordered_map<std::string, DaeSource> &sources,
                        const std::unordered_map<std::string, std::string> &verticesSources, MeshData &mesh,
                        const ModelImportOptions &options, std::string &error)
{
    struct Input
    {
        std::string semantic, source;
        int offset = 0, set = 0;
    };
    std::vector<Input> inputs;
    int stride = 0;
    for (const auto *node : primitive.Children("input"))
    {
        Input input;
        input.semantic = node->Attribute("semantic");
        input.source = StripHash(node->Attribute("source"));
        input.offset = node->Attribute("offset").empty() ? 0 : std::stoi(node->Attribute("offset"));
        input.set = node->Attribute("set").empty() ? 0 : std::stoi(node->Attribute("set"));
        if (input.semantic == "VERTEX")
        {
            const auto found = verticesSources.find(input.source);
            if (found != verticesSources.end())
            {
                input.semantic = "POSITION";
                input.source = found->second;
            }
        }
        stride = (std::max)(stride, input.offset + 1);
        inputs.push_back(std::move(input));
    }
    const auto *p = primitive.Child("p");
    if (!p || stride <= 0)
    {
        error = "COLLADA primitive has no usable index list.";
        return false;
    }
    const auto packed = ParseNumbers<int>(p->text);
    if (packed.size() % stride != 0)
    {
        error = "COLLADA primitive index stride is inconsistent.";
        return false;
    }
    std::vector<int> polygonSizes;
    if (primitive.name == "polylist")
    {
        const auto *counts = primitive.Child("vcount");
        if (!counts)
        {
            error = "COLLADA polylist has no vcount.";
            return false;
        }
        polygonSizes = ParseNumbers<int>(counts->text);
    }
    else
        polygonSizes.assign(packed.size() / (stride * 3), 3);
    std::unordered_map<std::string, std::uint16_t> cache;
    std::size_t cursor = 0;
    auto makeVertex = [&](std::size_t packedIndex, std::uint16_t &result) -> bool
    {
        std::string key;
        for (int lane = 0; lane < stride; ++lane)
            key += std::to_string(packed[packedIndex + lane]) + '/';
        const auto existing = cache.find(key);
        if (existing != cache.end())
        {
            result = existing->second;
            return true;
        }
        if (mesh.vertices.size() >= 65535)
        {
            error = "COLLADA primitive exceeds 65,535 unique vertices.";
            return false;
        }
        BrxSourceVertex vertex;
        for (const auto &input : inputs)
        {
            if (input.semantic == "TEXCOORD" && input.set != 0)
                continue;
            const auto found = sources.find(input.source);
            if (found == sources.end())
                continue;
            const int index = packed[packedIndex + input.offset];
            if (index < 0 || static_cast<std::size_t>(index) * found->second.stride >= found->second.values.size())
            {
                error = "COLLADA primitive references an invalid source index.";
                return false;
            }
            const auto *values = found->second.values.data() + static_cast<std::size_t>(index) * found->second.stride;
            if (input.semantic == "POSITION" && found->second.stride >= 3)
            {
                vertex.position[0] = values[0];
                vertex.position[1] = values[1];
                vertex.position[2] = values[2];
            }
            else if (input.semantic == "NORMAL" && found->second.stride >= 3)
            {
                vertex.normal[0] = values[0];
                vertex.normal[1] = values[1];
                vertex.normal[2] = values[2];
            }
            else if (input.semantic == "TEXCOORD" && found->second.stride >= 2)
            {
                vertex.uv[0] = values[0];
                vertex.uv[1] = options.flipV ? 1.0f - values[1] : values[1];
            }
            else if (input.semantic == "COLOR" && found->second.stride >= 3)
            {
                vertex.color[0] = values[0];
                vertex.color[1] = values[1];
                vertex.color[2] = values[2];
                if (found->second.stride >= 4)
                    vertex.color[3] = values[3];
            }
        }
        result = static_cast<std::uint16_t>(mesh.vertices.size());
        mesh.vertices.push_back(vertex);
        cache.emplace(std::move(key), result);
        return true;
    };
    for (const int polygonSize : polygonSizes)
    {
        if (polygonSize < 3 || cursor + static_cast<std::size_t>(polygonSize) * stride > packed.size())
        {
            error = "COLLADA polygon count exceeds its index list.";
            return false;
        }
        std::vector<std::uint16_t> polygon(static_cast<std::size_t>(polygonSize));
        for (int vertex = 0; vertex < polygonSize; ++vertex)
            if (!makeVertex(cursor + static_cast<std::size_t>(vertex) * stride, polygon[vertex]))
                return false;
        for (std::size_t vertex = 2; vertex < polygon.size(); ++vertex)
            mesh.triangles.push_back({polygon[0], polygon[vertex - 1], polygon[vertex]});
        cursor += static_cast<std::size_t>(polygonSize) * stride;
    }
    if (cursor != packed.size())
    {
        error = "COLLADA primitive contains trailing indices.";
        return false;
    }
    if (mesh.triangles.size() > 65535)
    {
        error = "COLLADA primitive exceeds 65,535 triangles; split the mesh before importing.";
        return false;
    }
    GenerateMissingNormals(mesh);
    return !mesh.triangles.empty();
}

glm::mat4 DaeTransform(const XmlNode &node)
{
    glm::mat4 result(1.0f);
    for (const auto &child : node.children)
    {
        const auto values = ParseNumbers<float>(child->text);
        glm::mat4 local(1.0f);
        if (child->name == "matrix" && values.size() >= 16)
            std::copy_n(values.data(), 16, glm::value_ptr(local));
        else if (child->name == "translate" && values.size() >= 3)
            local = glm::translate(glm::mat4(1), glm::vec3(values[0], values[1], values[2]));
        else if (child->name == "scale" && values.size() >= 3)
            local = glm::scale(glm::mat4(1), glm::vec3(values[0], values[1], values[2]));
        else if (child->name == "rotate" && values.size() >= 4)
            local = glm::rotate(glm::mat4(1), glm::radians(values[3]), glm::vec3(values[0], values[1], values[2]));
        else
            continue;
        result *= local;
    }
    return result;
}

void TransformMesh(MeshData &mesh, const glm::mat4 &transform)
{
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    for (auto &vertex : mesh.vertices)
    {
        const auto p = transform * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1);
        auto n = normalMatrix * glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
        if (glm::dot(n, n) > 0.000001f)
            n = glm::normalize(n);
        vertex.position[0] = p.x;
        vertex.position[1] = p.y;
        vertex.position[2] = p.z;
        vertex.normal[0] = n.x;
        vertex.normal[1] = n.y;
        vertex.normal[2] = n.z;
    }
    if (glm::determinant(glm::mat3(transform)) < 0.0f)
        for (auto &triangle : mesh.triangles)
            std::swap(triangle[1], triangle[2]);
}

bool ParseDae(const std::filesystem::path &path, const ModelImportOptions &options, std::vector<MeshData> &out,
              std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "Could not open COLLADA file.";
        return false;
    }
    std::string xml((std::istreambuf_iterator<char>(file)), {});
    XmlParser parser(std::move(xml));
    auto root = parser.Parse(error);
    if (!root)
        return false;
    if (root->name != "COLLADA")
    {
        error = "The DAE root element is not COLLADA.";
        return false;
    }
    float meter = 1.0f;
    std::string up = "Y_UP";
    if (const auto *asset = root->Child("asset"))
    {
        if (const auto *unit = asset->Child("unit"); unit && !unit->Attribute("meter").empty())
            meter = std::stof(unit->Attribute("meter"));
        if (const auto *axis = asset->Child("up_axis"))
        {
            std::istringstream s(axis->text);
            s >> up;
        }
    }
    glm::mat4 assetTransform = glm::scale(glm::mat4(1), glm::vec3(meter * options.scale));
    if (up == "Z_UP")
        assetTransform *= glm::rotate(glm::mat4(1), glm::radians(-90.0f), glm::vec3(1, 0, 0));
    else if (up == "X_UP")
        assetTransform *= glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(0, 0, 1));

    std::unordered_map<std::string, std::filesystem::path> images;
    if (const auto *library = root->Child("library_images"))
        for (const auto *image : library->Children("image"))
            if (const auto *source = image->Child("init_from"))
                images[image->Attribute("id")] = path.parent_path() / DecodeUri(source->text);
    std::unordered_map<std::string, DaeMaterial> effects;
    if (const auto *library = root->Child("library_effects"))
        for (const auto *effect : library->Children("effect"))
        {
            DaeMaterial material;
            material.name = effect->Attribute("id");
            const auto *profile = effect->Child("profile_COMMON");
            if (!profile)
            {
                effects[effect->Attribute("id")] = material;
                continue;
            }
            std::unordered_map<std::string, std::string> surfaces, samplers;
            for (const auto *parameter : profile->Children("newparam"))
            {
                if (const auto *surface = parameter->Child("surface"))
                    if (const auto *source = surface->Child("init_from"))
                        surfaces[parameter->Attribute("sid")] = source->text;
                if (const auto *sampler = parameter->Child("sampler2D"))
                    if (const auto *source = sampler->Child("source"))
                        samplers[parameter->Attribute("sid")] = source->text;
            }
            if (const auto *technique = profile->Child("technique"))
                for (const auto &shader : technique->children)
                {
                    if (shader->name != "lambert" && shader->name != "phong" && shader->name != "blinn" &&
                        shader->name != "constant")
                        continue;
                    if (const auto *diffuse = shader->Child("diffuse"))
                    {
                        if (const auto *texture = diffuse->Child("texture"))
                        {
                            const auto sampler = samplers.find(texture->Attribute("texture"));
                            if (sampler != samplers.end())
                            {
                                const auto surface = surfaces.find(sampler->second);
                                if (surface != surfaces.end())
                                    material.imageId = surface->second;
                            }
                        }
                        if (const auto *color = diffuse->Child("color"))
                        {
                            const auto values = ParseNumbers<float>(color->text);
                            for (std::size_t i = 0; i < values.size() && i < 4; ++i)
                                material.color[i] = values[i];
                        }
                    }
                    break;
                }
            effects[effect->Attribute("id")] = material;
        }
    std::unordered_map<std::string, DaeMaterial> materials;
    if (const auto *library = root->Child("library_materials"))
        for (const auto *material : library->Children("material"))
        {
            DaeMaterial decoded;
            decoded.name = material->Attribute("name");
            if (const auto *instance = material->Child("instance_effect"))
            {
                const auto found = effects.find(StripHash(instance->Attribute("url")));
                if (found != effects.end())
                {
                    decoded = found->second;
                    if (!material->Attribute("name").empty())
                        decoded.name = material->Attribute("name");
                }
            }
            materials[material->Attribute("id")] = decoded;
        }
    std::unordered_map<std::string, DaeGeometry> geometries;
    if (const auto *library = root->Child("library_geometries"))
        for (const auto *geometry : library->Children("geometry"))
        {
            const auto *meshNode = geometry->Child("mesh");
            if (!meshNode)
                continue;
            DaeGeometry decoded;
            decoded.name = geometry->Attribute("name");
            if (decoded.name.empty())
                decoded.name = geometry->Attribute("id");
            std::unordered_map<std::string, DaeSource> sources;
            for (const auto *source : meshNode->Children("source"))
            {
                DaeSource value;
                if (const auto *array = source->Child("float_array"))
                    value.values = ParseNumbers<float>(array->text);
                if (const auto *technique = source->Child("technique_common"))
                    if (const auto *accessor = technique->Child("accessor"))
                        if (!accessor->Attribute("stride").empty())
                            value.stride = std::stoul(accessor->Attribute("stride"));
                sources[source->Attribute("id")] = std::move(value);
            }
            std::unordered_map<std::string, std::string> vertices;
            for (const auto *vertex : meshNode->Children("vertices"))
                for (const auto *input : vertex->Children("input"))
                    if (input->Attribute("semantic") == "POSITION")
                        vertices[vertex->Attribute("id")] = StripHash(input->Attribute("source"));
            int primitiveIndex = 0;
            for (const auto &primitive : meshNode->children)
                if (primitive->name == "triangles" || primitive->name == "polylist")
                {
                    MeshData data;
                    data.materialName = primitive->Attribute("material");
                    data.name =
                        decoded.name + " / " +
                        (!data.materialName.empty() ? data.materialName : "Part " + std::to_string(primitiveIndex));
                    if (!DecodeDaePrimitive(*primitive, sources, vertices, data, options, error))
                        return false;
                    decoded.meshes.push_back(std::move(data));
                    ++primitiveIndex;
                }
            geometries[geometry->Attribute("id")] = std::move(decoded);
        }
    // A static-model import does not preserve skeletal animation, but a DAE
    // commonly places every visible mesh through instance_controller rather
    // than instance_geometry. Resolve each skin to its source geometry and
    // retain bind_shape_matrix so those files still produce their authored
    // bind-pose model instead of being rejected or silently importing nothing.
    std::unordered_map<std::string, DaeController> controllers;
    if (const auto *library = root->Child("library_controllers"))
        for (const auto *controller : library->Children("controller"))
        {
            const auto *skin = controller->Child("skin");
            if (!skin)
                continue; // Morph controllers are not static geometry sources.
            DaeController decoded;
            decoded.geometryId = StripHash(skin->Attribute("source"));
            if (const auto *matrix = skin->Child("bind_shape_matrix"))
            {
                const auto values = ParseNumbers<float>(matrix->text);
                if (values.size() != 16)
                {
                    error = "COLLADA skin has an invalid bind_shape_matrix.";
                    return false;
                }
                std::copy_n(values.data(), 16, glm::value_ptr(decoded.bindShape));
            }
            if (!decoded.geometryId.empty())
                controllers[controller->Attribute("id")] = decoded;
        }
    const auto *scenes = root->Child("library_visual_scenes");
    const XmlNode *visual = nullptr;
    if (scenes)
    {
        const auto list = scenes->Children("visual_scene");
        std::string requested;
        if (const auto *scene = root->Child("scene"))
            if (const auto *instance = scene->Child("instance_visual_scene"))
                requested = StripHash(instance->Attribute("url"));
        for (const auto *candidate : list)
            if (requested.empty() || candidate->Attribute("id") == requested)
            {
                visual = candidate;
                break;
            }
    }
    std::function<bool(const XmlNode &, const glm::mat4 &)> visit = [&](const XmlNode &node, const glm::mat4 &parent)
    {
        const glm::mat4 world = parent * DaeTransform(node);
        for (const auto *instance : node.Children("instance_geometry"))
        {
            const auto found = geometries.find(StripHash(instance->Attribute("url")));
            if (found == geometries.end())
            {
                error = "COLLADA scene references missing geometry.";
                return false;
            }
            for (auto mesh : found->second.meshes)
            {
                TransformMesh(mesh, assetTransform * world);
                out.push_back(std::move(mesh));
            }
        }
        for (const auto *instance : node.Children("instance_controller"))
        {
            const auto controller = controllers.find(StripHash(instance->Attribute("url")));
            if (controller == controllers.end())
            {
                error = "COLLADA scene references an unsupported or missing controller.";
                return false;
            }
            const auto found = geometries.find(controller->second.geometryId);
            if (found == geometries.end())
            {
                error = "COLLADA skin references missing source geometry.";
                return false;
            }
            for (auto mesh : found->second.meshes)
            {
                mesh.name += " (static bind pose)";
                TransformMesh(mesh, assetTransform * world * controller->second.bindShape);
                out.push_back(std::move(mesh));
            }
        }
        for (const auto *child : node.Children("node"))
            if (!visit(*child, world))
                return false;
        return true;
    };
    if (visual)
    {
        for (const auto *node : visual->Children("node"))
            if (!visit(*node, glm::mat4(1)))
                return false;
    }
    else
        for (auto &[id, geometry] : geometries)
            for (auto mesh : geometry.meshes)
            {
                TransformMesh(mesh, assetTransform);
                out.push_back(std::move(mesh));
            }
    if (out.empty())
    {
        error = "COLLADA contains no supported triangle or polylist geometry.";
        return false;
    }
    for (auto &mesh : out)
    {
        const auto material = materials.find(mesh.materialName);
        if (material == materials.end())
            continue;
        mesh.materialColor = material->second.color;
        if (!material->second.name.empty())
            mesh.materialName = material->second.name;
        const auto image = images.find(material->second.imageId);
        if (image != images.end())
            mesh.diffuseTexture = image->second;
    }
    return true;
}

std::string UfbxString(const ufbx_string value)
{
    return value.data != nullptr ? std::string(value.data, value.length) : std::string{};
}

bool ParseFbx(const std::filesystem::path &path, const ModelImportOptions &options, std::vector<MeshData> &out,
              std::string &error)
{
    const auto bytes = ReadBytes(path);
    if (bytes.empty())
    {
        error = "Could not open FBX file.";
        return false;
    }

    const auto utf8Path = path.u8string();
    ufbx_load_opts loadOptions{};
    loadOptions.filename.data = reinterpret_cast<const char *>(utf8Path.data());
    loadOptions.filename.length = utf8Path.size();
    loadOptions.generate_missing_normals = true;
    loadOptions.ignore_animation = g_fbxRigCapture == nullptr || !g_fbxRigCapture->importAnimations;
    loadOptions.ignore_missing_external_files = true;
    loadOptions.use_blender_pbr_material = true;
    loadOptions.target_axes = ufbx_axes_right_handed_z_up;
    loadOptions.target_unit_meters = 1.0;

    ufbx_error loadError{};
    ufbx_scene *scene = ufbx_load_memory(bytes.data(), bytes.size(), &loadOptions, &loadError);
    if (scene == nullptr)
    {
        error = "FBX loader rejected the file";
        const auto detail = UfbxString(loadError.description);
        if (!detail.empty())
            error += ": " + detail;
        return false;
    }
    const std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> sceneOwner(scene, &ufbx_free_scene);
    std::unordered_map<const void *, std::shared_ptr<const std::vector<std::uint8_t>>> embeddedDiffuseImages;

    if (g_fbxRigCapture != nullptr)
    {
        auto &rig = *g_fbxRigCapture;
        std::unordered_set<const ufbx_node *> required;
        for (std::size_t meshIndex = 0; meshIndex < scene->meshes.count; ++meshIndex)
        {
            const ufbx_mesh *mesh = scene->meshes.data[meshIndex];
            if (mesh == nullptr)
                continue;
            for (std::size_t deformerIndex = 0; deformerIndex < mesh->skin_deformers.count; ++deformerIndex)
            {
                const ufbx_skin_deformer *deformer = mesh->skin_deformers.data[deformerIndex];
                if (deformer == nullptr)
                    continue;
                for (std::size_t clusterIndex = 0; clusterIndex < deformer->clusters.count; ++clusterIndex)
                {
                    const ufbx_skin_cluster *cluster = deformer->clusters.data[clusterIndex];
                    for (const ufbx_node *bone = cluster != nullptr ? cluster->bone_node : nullptr; bone != nullptr;
                         bone = bone->parent)
                        required.insert(bone);
                }
            }
        }
        for (std::size_t nodeIndex = 0; nodeIndex < scene->nodes.count; ++nodeIndex)
        {
            const ufbx_node *node = scene->nodes.data[nodeIndex];
            if (node == nullptr || !required.contains(node))
                continue;
            if (rig.bones.size() >= 255u)
            {
                error = "FBX skeleton exceeds the retail GLOBSET limit of 255 bones.";
                return false;
            }
            const auto paletteIndex = static_cast<std::uint16_t>(rig.bones.size());
            rig.palette.emplace(node, paletteIndex);
            FbxRigBone bone;
            bone.node = node;
            bone.name = UfbxString(node->name);
            if (bone.name.empty())
                bone.name = "Bone_" + std::to_string(paletteIndex);
            // FBX unit conversion can live in an ancestor's scale. Retail
            // joints inherit position/rotation, not ACT scale: bake units and
            // axis conversion into world positions, then keep a rigid basis.
            if (!FbxRigidMatrix(node->node_to_world, options.scale, bone.world, error))
                return false;
            rig.bones.push_back(std::move(bone));
        }
        for (auto &bone : rig.bones)
        {
            const auto found = bone.node != nullptr ? rig.palette.find(bone.node->parent) : rig.palette.end();
            if (found != rig.palette.end())
                bone.parent = static_cast<std::int32_t>(found->second);
        }
        if (rig.bones.empty())
        {
            error = "FBX has no skin clusters. Use static model import for an unskinned mesh.";
            return false;
        }
    }

    for (std::size_t nodeIndex = 0; nodeIndex < scene->nodes.count; ++nodeIndex)
    {
        const ufbx_node *node = scene->nodes.data[nodeIndex];
        const ufbx_mesh *mesh = node != nullptr ? node->mesh : nullptr;
        if (node == nullptr || mesh == nullptr || !mesh->vertex_position.exists)
            continue;
        if (g_fbxRigCapture != nullptr && mesh->skin_deformers.count != 1)
        {
            error =
                mesh->skin_deformers.count == 0
                    ? "Every mesh in an FBX character import must have one skin deformer. Import rigid accessories separately."
                    : "ProjectCane supports one skin palette per mesh; FBX meshes with multiple skin deformers must be consolidated before import.";
            return false;
        }
        if (g_fbxRigCapture != nullptr)
        {
            const ufbx_skin_deformer *skin = mesh->skin_deformers.data[0];
            if (skin == nullptr)
            {
                error = "FBX mesh contains a broken skin deformer reference.";
                return false;
            }
            const auto method = skin->skinning_method;
            if (method != UFBX_SKINNING_METHOD_LINEAR && method != UFBX_SKINNING_METHOD_RIGID)
            {
                error =
                    "ProjectCane uses linear bone weights; convert dual-quaternion FBX skinning to linear before import.";
                return false;
            }
        }

        const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&node->geometry_to_world);
        const std::string nodeName =
            UfbxString(node->name).empty() ? "FBX Mesh " + std::to_string(nodeIndex) : UfbxString(node->name);
        const std::size_t firstNodePart = out.size();
        std::unordered_map<std::uint32_t, std::size_t> partForMaterial;
        std::vector<std::unordered_map<std::uint32_t, std::uint16_t>> vertexCaches;

        const auto ensurePart = [&](const std::uint32_t materialIndex) -> std::size_t
        {
            const auto found = partForMaterial.find(materialIndex);
            if (found != partForMaterial.end())
                return found->second;
            const std::size_t partIndex = out.size();
            partForMaterial.emplace(materialIndex, partIndex);
            vertexCaches.emplace_back();
            MeshData imported;
            imported.name = nodeName;
            if (materialIndex < node->materials.count)
            {
                const ufbx_material *material = node->materials.data[materialIndex];
                if (material != nullptr)
                {
                    imported.materialName = UfbxString(material->name);
                    if (material->pbr.base_color.has_value)
                    {
                        const auto color = material->pbr.base_color.value_vec4;
                        imported.materialColor = {static_cast<float>(color.x), static_cast<float>(color.y),
                                                  static_cast<float>(color.z), static_cast<float>(color.w)};
                    }
                    else if (material->fbx.diffuse_color.has_value)
                    {
                        const auto color = material->fbx.diffuse_color.value_vec3;
                        imported.materialColor = {static_cast<float>(color.x), static_cast<float>(color.y),
                                                  static_cast<float>(color.z), 1.0f};
                    }
                    const ufbx_texture *texture = material->pbr.base_color.texture;
                    if (texture == nullptr)
                        texture = material->fbx.diffuse_color.texture;
                    if (texture != nullptr)
                    {
                        ufbx_blob content = texture->content;
                        if (content.size == 0 && texture->video != nullptr)
                            content = texture->video->content;
                        std::string textureName = UfbxString(texture->relative_filename);
                        if (textureName.empty())
                            textureName = UfbxString(texture->filename);
                        if (!textureName.empty())
                        {
                            const std::u8string utf8TextureName(reinterpret_cast<const char8_t *>(textureName.data()),
                                                                textureName.size());
                            const std::filesystem::path texturePath{utf8TextureName};
                            imported.diffuseTexture =
                                content.size != 0 ? texturePath : ResolveFbxTexturePath(path, texturePath);
                        }
                        if (content.data != nullptr && content.size != 0)
                        {
                            auto found = embeddedDiffuseImages.find(content.data);
                            if (found == embeddedDiffuseImages.end())
                            {
                                const auto *begin = static_cast<const std::uint8_t *>(content.data);
                                found = embeddedDiffuseImages
                                            .emplace(content.data, std::make_shared<const std::vector<std::uint8_t>>(
                                                                       begin, begin + content.size))
                                            .first;
                            }
                            imported.embeddedDiffuseTexture = found->second;
                            const auto imageId =
                                texture->video != nullptr ? texture->video->element_id : texture->element_id;
                            imported.diffuseTextureKey =
                                path.lexically_normal().string() + "|FBX embedded " + std::to_string(imageId);
                            if (imported.diffuseTexture.empty())
                                imported.diffuseTexture = "Embedded texture " + std::to_string(imageId);
                        }
                    }
                    if (!imported.materialName.empty())
                        imported.name += " / " + imported.materialName;
                }
            }
            out.push_back(std::move(imported));
            return partIndex;
        };

        for (std::size_t faceIndex = 0; faceIndex < mesh->faces.count; ++faceIndex)
        {
            const ufbx_face face = mesh->faces.data[faceIndex];
            if (face.num_indices < 3)
                continue;
            const std::uint32_t materialIndex =
                faceIndex < mesh->face_material.count ? mesh->face_material.data[faceIndex] : 0u;
            const std::size_t partIndex = ensurePart(materialIndex);
            MeshData &imported = out[partIndex];
            auto &cache = vertexCaches[partIndex - firstNodePart];

            std::vector<std::uint32_t> corners(face.num_indices * 3u);
            const std::uint32_t triangleCount = ufbx_triangulate_face(corners.data(), corners.size(), mesh, face);
            if (triangleCount == 0)
            {
                error = "FBX contains a face that could not be triangulated.";
                return false;
            }

            for (std::uint32_t faceTriangle = 0; faceTriangle < triangleCount; ++faceTriangle)
            {
                std::array<std::uint16_t, 3> triangle{};
                for (std::size_t triangleCorner = 0; triangleCorner < 3; ++triangleCorner)
                {
                    const std::uint32_t sourceIndex = corners[faceTriangle * 3u + triangleCorner];
                    const auto cached = cache.find(sourceIndex);
                    if (cached != cache.end())
                    {
                        triangle[triangleCorner] = cached->second;
                        continue;
                    }
                    if (imported.vertices.size() >= 65535u)
                    {
                        error = "An FBX material section exceeds 65,535 vertices; split the mesh before importing.";
                        return false;
                    }

                    BrxSourceVertex vertex{};
                    const ufbx_vec3 sourcePosition = ufbx_get_vertex_vec3(&mesh->vertex_position, sourceIndex);
                    const ufbx_vec3 position = ufbx_transform_position(&node->geometry_to_world, sourcePosition);
                    vertex.position[0] = static_cast<float>(position.x) * options.scale;
                    vertex.position[1] = static_cast<float>(position.y) * options.scale;
                    vertex.position[2] = static_cast<float>(position.z) * options.scale;
                    if (mesh->vertex_normal.exists)
                    {
                        const ufbx_vec3 sourceNormal = ufbx_get_vertex_vec3(&mesh->vertex_normal, sourceIndex);
                        const ufbx_vec3 normal = ufbx_transform_direction(&normalMatrix, sourceNormal);
                        const glm::vec3 transformed(static_cast<float>(normal.x), static_cast<float>(normal.y),
                                                    static_cast<float>(normal.z));
                        const glm::vec3 normalized = glm::dot(transformed, transformed) > 0.000001f
                                                         ? glm::normalize(transformed)
                                                         : glm::vec3(0.0f);
                        vertex.normal[0] = normalized.x;
                        vertex.normal[1] = normalized.y;
                        vertex.normal[2] = normalized.z;
                    }
                    if (mesh->vertex_uv.exists)
                    {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, sourceIndex);
                        vertex.uv[0] = static_cast<float>(uv.x);
                        vertex.uv[1] = options.flipV ? 1.0f - static_cast<float>(uv.y) : static_cast<float>(uv.y);
                    }
                    if (mesh->vertex_color.exists)
                    {
                        const ufbx_vec4 color = ufbx_get_vertex_vec4(&mesh->vertex_color, sourceIndex);
                        vertex.color[0] = static_cast<float>(color.x);
                        vertex.color[1] = static_cast<float>(color.y);
                        vertex.color[2] = static_cast<float>(color.z);
                        vertex.color[3] = static_cast<float>(color.w);
                    }
                    if (g_fbxRigCapture != nullptr && mesh->skin_deformers.count > 0 &&
                        sourceIndex < mesh->vertex_indices.count)
                    {
                        const ufbx_skin_deformer *skin = mesh->skin_deformers.data[0];
                        const std::uint32_t logicalVertex = mesh->vertex_indices.data[sourceIndex];
                        if (skin != nullptr && logicalVertex < skin->vertices.count)
                        {
                            const ufbx_skin_vertex skinVertex = skin->vertices.data[logicalVertex];
                            float total = 0.0f;
                            std::size_t influence = 0;
                            for (std::uint32_t weightIndex = 0; weightIndex < skinVertex.num_weights && influence < 4;
                                 ++weightIndex)
                            {
                                const std::size_t index =
                                    static_cast<std::size_t>(skinVertex.weight_begin) + weightIndex;
                                if (index >= skin->weights.count)
                                    break;
                                const ufbx_skin_weight weight = skin->weights.data[index];
                                if (weight.cluster_index >= skin->clusters.count || weight.weight <= 0.0)
                                    continue;
                                const ufbx_skin_cluster *cluster = skin->clusters.data[weight.cluster_index];
                                const auto palette =
                                    g_fbxRigCapture->palette.find(cluster != nullptr ? cluster->bone_node : nullptr);
                                if (palette == g_fbxRigCapture->palette.end())
                                    continue;
                                vertex.boneIndices[influence] = palette->second;
                                vertex.boneWeights[influence] = static_cast<float>(weight.weight);
                                total += vertex.boneWeights[influence++];
                            }
                            if (total > 0.000001f)
                                for (std::size_t lane = 0; lane < influence; ++lane)
                                    vertex.boneWeights[lane] /= total;
                        }
                    }
                    const auto destinationIndex = static_cast<std::uint16_t>(imported.vertices.size());
                    imported.vertices.push_back(vertex);
                    cache.emplace(sourceIndex, destinationIndex);
                    triangle[triangleCorner] = destinationIndex;
                }
                imported.triangles.push_back(triangle);
                if (imported.triangles.size() > 65535u)
                {
                    error = "An FBX material section exceeds 65,535 triangles; split the mesh before importing.";
                    return false;
                }
            }
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const MeshData &mesh) { return mesh.triangles.empty(); }),
              out.end());
    if (out.empty())
    {
        error = "FBX contains no supported polygon meshes.";
        return false;
    }
    if (g_fbxRigCapture != nullptr && g_fbxRigCapture->importAnimations && scene->anim_curves.count != 0)
    {
        auto &rig = *g_fbxRigCapture;
        const double sampleRate = (std::max)(1.0, static_cast<double>(rig.sampleRate));
        for (std::size_t stackIndex = 0; stackIndex < scene->anim_stacks.count; ++stackIndex)
        {
            const ufbx_anim_stack *stack = scene->anim_stacks.data[stackIndex];
            if (stack == nullptr || stack->anim == nullptr || stack->time_end < stack->time_begin)
                continue;
            FbxRigClip clip;
            clip.name = UfbxString(stack->name);
            if (clip.name.empty())
                clip.name = "Animation_" + std::to_string(stackIndex + 1u);
            const double rawDuration = stack->time_end - stack->time_begin;
            if (!std::isfinite(rawDuration))
                continue;
            clip.duration = static_cast<float>((std::min)(rawDuration, 32767.0 / 60.0));
            clip.positions.resize(rig.bones.size());
            clip.rotations.resize(rig.bones.size());
            clip.scales.resize(rig.bones.size());
            const std::size_t frameCount =
                (std::min<std::size_t>)(32767u, static_cast<std::size_t>(std::ceil(clip.duration * sampleRate)) + 1u);
            for (std::size_t frame = 0; frame < frameCount; ++frame)
            {
                const double localTime = (std::min)(static_cast<double>(clip.duration), frame / sampleRate);
                const double time = stack->time_begin + localTime;
                ufbx_evaluate_opts evaluateOptions{};
                ufbx_error evaluateError{};
                ufbx_scene *evaluated = ufbx_evaluate_scene(scene, stack->anim, time, &evaluateOptions, &evaluateError);
                if (evaluated == nullptr)
                {
                    error = "Could not evaluate FBX animation: " + UfbxString(evaluateError.description);
                    return false;
                }
                const std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> evaluatedOwner(evaluated,
                                                                                             &ufbx_free_scene);
                std::vector<glm::mat4> world(rig.bones.size());
                for (std::size_t boneIndex = 0; boneIndex < rig.bones.size(); ++boneIndex)
                {
                    const auto *source = rig.bones[boneIndex].node;
                    if (source->typed_id >= evaluated->nodes.count)
                    {
                        error = "Evaluated FBX skeleton lost a required bone.";
                        return false;
                    }
                    const auto *node = evaluated->nodes.data[source->typed_id];
                    if (!FbxRigidMatrix(node->node_to_world, options.scale, world[boneIndex], error))
                        return false;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const auto &bindColumn = source->node_to_world.cols[axis];
                        const auto &posedColumn = node->node_to_world.cols[axis];
                        const double bindLength = std::sqrt(bindColumn.x * bindColumn.x + bindColumn.y * bindColumn.y +
                                                            bindColumn.z * bindColumn.z);
                        const double posedLength =
                            std::sqrt(posedColumn.x * posedColumn.x + posedColumn.y * posedColumn.y +
                                      posedColumn.z * posedColumn.z);
                        if (std::abs(posedLength - bindLength) > (std::max)(1.0e-8, bindLength * 1.0e-4))
                        {
                            error =
                                "FBX animation changes bone scale, which ProjectCane's unscaled skin palette cannot preserve. Use a rotation/translation rig.";
                            return false;
                        }
                    }
                }
                for (std::size_t boneIndex = 0; boneIndex < rig.bones.size(); ++boneIndex)
                {
                    const auto parent = rig.bones[boneIndex].parent;
                    const glm::mat4 local =
                        parent >= 0 ? glm::inverse(world[static_cast<std::size_t>(parent)]) * world[boneIndex]
                                    : world[boneIndex];
                    const auto euler = FbxProjectCaneEuler(local);
                    BrxAsegVectorKey position, rotation, scale;
                    position.time = rotation.time = scale.time = static_cast<float>(localTime);
                    position.value = {local[3].x, local[3].y, local[3].z};
                    rotation.value = {glm::radians(static_cast<float>(euler.x)),
                                      glm::radians(static_cast<float>(euler.y)),
                                      glm::radians(static_cast<float>(euler.z))};
                    if (!clip.rotations[boneIndex].empty())
                        for (std::size_t axis = 0; axis < 3; ++axis)
                        {
                            const float previous = clip.rotations[boneIndex].back().value[axis];
                            rotation.value[axis] =
                                previous + std::remainder(rotation.value[axis] - previous, 6.28318530718f);
                        }
                    scale.value = {1.0f, 1.0f, 1.0f};
                    clip.positions[boneIndex].push_back(position);
                    clip.rotations[boneIndex].push_back(rotation);
                    clip.scales[boneIndex].push_back(scale);
                }
            }
            rig.clips.push_back(std::move(clip));
        }
    }
    for (auto &mesh : out)
        GenerateMissingNormals(mesh);
    return true;
}
void Bounds(BrxSubglobRecord &s)
{
    glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
    for (auto &v : s.vertices)
    {
        glm::vec3 p(v.position[0], v.position[1], v.position[2]);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    glm::vec3 c = (lo + hi) * .5f;
    s.center[0] = c.x;
    s.center[1] = c.y;
    s.center[2] = c.z;
    for (auto &v : s.vertices)
        s.radius = (std::max)(s.radius, glm::length(glm::vec3(v.position[0], v.position[1], v.position[2]) - c));
    s.vertexCount = static_cast<std::uint16_t>(s.vertices.size());
    s.triangleCount = static_cast<std::uint16_t>(s.triangles.size());
}

bool ParseModelFile(const std::filesystem::path &path, const ModelImportOptions &options, std::vector<MeshData> &meshes,
                    std::string &error)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    bool parsed = false;
    try
    {
        parsed = extension == ".obj"    ? ParseObj(path, options, meshes, error)
                 : extension == ".glb"  ? ParseGlb(path, options, meshes, error)
                 : extension == ".gltf" ? ParseGltf(path, options, meshes, error)
                 : extension == ".fbx"  ? ParseFbx(path, options, meshes, error)
                 : extension == ".dae"  ? ParseDae(path, options, meshes, error)
                                        : false;
        if (extension == ".ply")
            parsed = ParsePly(path, options, meshes, error);
        else if (extension == ".stl")
            parsed = ParseStl(path, options, meshes, error);
    }
    catch (const std::exception &exception)
    {
        error = std::string("Model parser rejected the file: ") + exception.what();
    }
    if (!parsed && error.empty())
        error = "Choose an FBX, OBJ, GLB, glTF, COLLADA DAE, PLY, or STL file.";
    return parsed;
}

std::vector<MeshData> SpatiallyChunkRenderMeshes(std::vector<MeshData> meshes)
{
    constexpr std::size_t triangleBudget = 8192u;
    std::vector<MeshData> result;
    for (auto &source : meshes)
    {
        const bool requiresSpatialSplit = source.triangles.size() > triangleBudget;
        std::vector<std::vector<std::size_t>> pending{{}};
        pending.front().resize(source.triangles.size());
        std::iota(pending.front().begin(), pending.front().end(), std::size_t{0});
        std::size_t partNumber = 0;
        while (!pending.empty())
        {
            auto triangleIndices = std::move(pending.back());
            pending.pop_back();
            if (triangleIndices.size() > triangleBudget)
            {
                glm::vec3 lower((std::numeric_limits<float>::max)());
                glm::vec3 upper((std::numeric_limits<float>::lowest)());
                std::vector<std::pair<float, std::size_t>> centroids;
                centroids.reserve(triangleIndices.size());
                for (const auto triangleIndex : triangleIndices)
                {
                    const auto &triangle = source.triangles[triangleIndex];
                    glm::vec3 center(0.0f);
                    for (const auto vertexIndex : triangle)
                    {
                        const auto &p = source.vertices[vertexIndex].position;
                        center += glm::vec3(p[0], p[1], p[2]);
                    }
                    center /= 3.0f;
                    lower = glm::min(lower, center);
                    upper = glm::max(upper, center);
                    centroids.emplace_back(0.0f, triangleIndex);
                }
                const glm::vec3 extent = upper - lower;
                const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
                for (auto &[coordinate, triangleIndex] : centroids)
                {
                    const auto &triangle = source.triangles[triangleIndex];
                    coordinate = 0.0f;
                    for (const auto vertexIndex : triangle)
                        coordinate += source.vertices[vertexIndex].position[axis];
                    coordinate /= 3.0f;
                }
                const auto middle = centroids.begin() + static_cast<std::ptrdiff_t>(centroids.size() / 2u);
                std::nth_element(centroids.begin(), middle, centroids.end(),
                                 [](const auto &left, const auto &right) { return left.first < right.first; });
                std::vector<std::size_t> first, second;
                first.reserve(centroids.size() / 2u);
                second.reserve(centroids.size() - centroids.size() / 2u);
                for (auto iterator = centroids.begin(); iterator != middle; ++iterator)
                    first.push_back(iterator->second);
                for (auto iterator = middle; iterator != centroids.end(); ++iterator)
                    second.push_back(iterator->second);
                if (!first.empty() && !second.empty())
                {
                    pending.push_back(std::move(second));
                    pending.push_back(std::move(first));
                    continue;
                }
            }

            MeshData chunk;
            chunk.name =
                requiresSpatialSplit ? source.name + " / Spatial " + std::to_string(++partNumber) : source.name;
            chunk.materialName = source.materialName;
            chunk.diffuseTexture = source.diffuseTexture;
            chunk.embeddedDiffuseTexture = source.embeddedDiffuseTexture;
            chunk.diffuseTextureKey = source.diffuseTextureKey;
            chunk.materialColor = source.materialColor;
            std::unordered_map<std::uint16_t, std::uint16_t> remap;
            for (const auto triangleIndex : triangleIndices)
            {
                std::array<std::uint16_t, 3> triangle{};
                for (int lane = 0; lane < 3; ++lane)
                {
                    const auto sourceIndex = source.triangles[triangleIndex][lane];
                    auto found = remap.find(sourceIndex);
                    if (found == remap.end())
                    {
                        const auto newIndex = static_cast<std::uint16_t>(chunk.vertices.size());
                        chunk.vertices.push_back(source.vertices[sourceIndex]);
                        found = remap.emplace(sourceIndex, newIndex).first;
                    }
                    triangle[lane] = found->second;
                }
                chunk.triangles.push_back(triangle);
            }
            if (!chunk.triangles.empty())
                result.push_back(std::move(chunk));
        }
    }
    return result;
}
}
ModelImportResult ImportStaticModel(const std::filesystem::path &path, BrxDocument &document,
                                    const int ownerObjectIndex, const ModelImportOptions &options)
{
    ModelImportResult result;
    if (ownerObjectIndex < 0 || static_cast<std::size_t>(ownerObjectIndex) >= document.Objects().size())
    {
        result.message = "Select the object that should own the imported model.";
        return result;
    }
    if (!CidDerivesFrom(document.Objects()[static_cast<std::size_t>(ownerObjectIndex)].cid, 2))
    {
        result.message = "Model geometry must be attached to an ALO-derived object.";
        return result;
    }
    if (!std::isfinite(options.scale) || options.scale <= 0.0f)
    {
        result.message = "Model import scale must be finite and greater than zero.";
        return result;
    }

    std::vector<MeshData> meshes;
    std::string error;
    if (!ParseModelFile(path, options, meshes, error))
    {
        result.message = error;
        return result;
    }
    meshes = SpatiallyChunkRenderMeshes(std::move(meshes));
    if (meshes.size() > 65535u)
    {
        result.message = "The model has more SUBGLOB parts than the BRX U16 limit.";
        return result;
    }
    for (const auto &mesh : meshes)
        if (mesh.vertices.size() > 65535u || mesh.triangles.size() > 65535u)
        {
            result.message = "A model part exceeds the BRX U16 vertex or triangle limit.";
            return result;
        }
    if (!options.importMaterials)
        for (auto &mesh : meshes)
        {
            mesh.diffuseTexture.clear();
            mesh.embeddedDiffuseTexture.reset();
            mesh.diffuseTextureKey.clear();
            mesh.materialColor = {1, 1, 1, 1};
            mesh.materialName.clear();
        }
    std::unordered_map<std::string, BrxBitmapRecord> pendingBitmaps;
    for (const auto &mesh : meshes)
    {
        if (mesh.diffuseTexture.empty() && !mesh.embeddedDiffuseTexture)
            continue;
        const auto key = ModelTextureKey(mesh);
        if (pendingBitmaps.contains(key))
            continue;
        BrxBitmapRecord bitmap;
        const bool loaded = mesh.embeddedDiffuseTexture
                                ? LoadTextureRgbaMemory(std::span<const std::uint8_t>(*mesh.embeddedDiffuseTexture),
                                                        mesh.diffuseTexture, bitmap, error)
                                : LoadTextureRgba(mesh.diffuseTexture, bitmap, error);
        if (!loaded)
        {
            result.message = "Could not import material texture: " + error;
            return result;
        }
        pendingBitmaps.emplace(key, std::move(bitmap));
    }
    std::unordered_set<std::string> materialKeys;
    for (const auto &mesh : meshes)
    {
        std::string key = mesh.materialName + "|" + ModelTextureKey(mesh);
        for (const float channel : mesh.materialColor)
            key += "|" + std::to_string(channel);
        materialKeys.insert(std::move(key));
    }
    if (document.Shaders().size() + materialKeys.size() > 65535u)
    {
        result.message = "The level has no remaining U16 material slots for this import.";
        return result;
    }

    auto ownedGlobset =
        std::find_if(document.EditableGlobsets().begin(), document.EditableGlobsets().end(),
                     [&](const BrxGlobsetRecord &globset) { return globset.ownerObjectIndex == ownerObjectIndex; });
    const bool createdGlobset = ownedGlobset == document.EditableGlobsets().end();
    if (!createdGlobset &&
        std::count_if(document.Globsets().begin(), document.Globsets().end(), [&](const BrxGlobsetRecord &globset)
                      { return globset.ownerObjectIndex == ownerObjectIndex; }) != 1)
    {
        result.message = "The selected ALO has an invalid number of GLOBSET records.";
        return result;
    }

    std::unordered_map<std::string, std::int32_t> bitmapIndices;
    std::vector<std::size_t> addedBitmapIndices;
    for (auto &[key, bitmap] : pendingBitmaps)
    {
        const auto index = document.AddBitmap(std::move(bitmap));
        bitmapIndices[key] = static_cast<std::int32_t>(index);
        addedBitmapIndices.push_back(index);
    }
    std::unordered_map<std::string, std::uint16_t> shaderIndices;
    std::vector<std::size_t> addedShaderIndices;
    std::vector<std::uint16_t> meshShaderIndices;
    meshShaderIndices.reserve(meshes.size());
    for (const auto &mesh : meshes)
    {
        std::string key = mesh.materialName + "|" + ModelTextureKey(mesh);
        for (const float channel : mesh.materialColor)
            key += "|" + std::to_string(channel);
        auto found = shaderIndices.find(key);
        if (found == shaderIndices.end())
        {
            BrxShaderRecord shader;
            shader.kind = 1;
            shader.editorName = mesh.materialName.empty() ? path.stem().string() + " Material" : mesh.materialName;
            shader.renderPhase = 3;
            shader.animation.kind = BrxShaderAnimationKind::None;
            shader.animationKind = static_cast<std::int16_t>(shader.animation.kind);
            std::copy(mesh.materialColor.begin(), mesh.materialColor.end(), shader.rgba);
            if (!mesh.diffuseTexture.empty() || mesh.embeddedDiffuseTexture)
            {
                const auto bitmap = bitmapIndices.find(ModelTextureKey(mesh));
                if (bitmap != bitmapIndices.end())
                {
                    shader.ambientBitmapIndices.push_back(-1);
                    shader.diffuseBitmapIndices.push_back(bitmap->second);
                    shader.saturateBitmapIndices.push_back(-1);
                    BrxShaderRecord::TextureStage stage;
                    stage.ambientBitmapIndices.push_back(-1);
                    stage.diffuseBitmapIndices.push_back(bitmap->second);
                    stage.saturateBitmapIndices.push_back(-1);
                    shader.textureStages.push_back(std::move(stage));
                }
            }
            shader.textureStageCount = static_cast<std::uint8_t>(shader.textureStages.size());
            const auto index = document.AddShader(std::move(shader));
            addedShaderIndices.push_back(index);
            found = shaderIndices.emplace(std::move(key), static_cast<std::uint16_t>(index)).first;
        }
        meshShaderIndices.push_back(found->second);
    }
    BrxGlobRecord glob;
    glob.editorName = path.stem().string();
    glob.ownerObjectIndex = ownerObjectIndex;
    // Imported vertices are authored in object-local space. Render packets are
    // world-space, so seed the authoritative local-to-world matrix from the
    // owning ALO. Leaving this as identity separated the visible mesh from its
    // transform gizmo whenever the new object was placed away from world zero.
    std::copy(std::begin(document.Objects()[static_cast<std::size_t>(ownerObjectIndex)].worldTransform),
              std::end(document.Objects()[static_cast<std::size_t>(ownerObjectIndex)].worldTransform),
              glob.sourceToWorld);
    glob.renderPhase = 3; // RP_Opaque, matching an ordinary static model.
    for (const auto &existing : document.Globs())
        glob.renderGroup = (std::max)(glob.renderGroup, existing.renderGroup + 1);
    for (std::size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
    {
        auto &mesh = meshes[meshIndex];
        BrxSubglobRecord subglob;
        subglob.editorName = mesh.name;
        subglob.shaderIndex = meshShaderIndices[meshIndex];
        subglob.vertices = std::move(mesh.vertices);
        subglob.triangles = std::move(mesh.triangles);
        Bounds(subglob);
        result.triangleCount += subglob.triangles.size();
        glob.sourceSubglobs.push_back(std::move(subglob));
    }
    glm::vec3 lower(std::numeric_limits<float>::max()), upper(std::numeric_limits<float>::lowest());
    for (const auto &subglob : glob.sourceSubglobs)
    {
        const glm::vec3 center(subglob.center[0], subglob.center[1], subglob.center[2]);
        lower = glm::min(lower, center - glm::vec3(subglob.radius));
        upper = glm::max(upper, center + glm::vec3(subglob.radius));
    }
    const glm::vec3 center = (lower + upper) * 0.5f;
    glob.center[0] = center.x;
    glob.center[1] = center.y;
    glob.center[2] = center.z;
    glob.radius = glm::length(upper - center);
    glob.modified = true;
    auto &ownerObject = document.EditableObjects()[static_cast<std::size_t>(ownerObjectIndex)];
    if (ownerObject.so.present && ownerObject.so.fixedPhysics)
    {
        // ProjectCane's RenderAloGlobset consumes a fixed SO's GLOB center
        // directly in world space instead of applying the ALO transform.
        // Vertices remain object-local and are submitted with the ALO model
        // matrix; only the fixed-body culling sphere uses this convention.
        const glm::mat4 sourceToWorld = glm::make_mat4(glob.sourceToWorld);
        const glm::vec3 worldCenter = glm::vec3(sourceToWorld * glm::vec4(center, 1.0f));
        const float worldScale =
            (std::max)({glm::length(glm::vec3(sourceToWorld[0])), glm::length(glm::vec3(sourceToWorld[1])),
                        glm::length(glm::vec3(sourceToWorld[2]))});
        glob.center[0] = worldCenter.x;
        glob.center[1] = worldCenter.y;
        glob.center[2] = worldCenter.z;
        glob.radius *= worldScale;
    }
    // RenderAloAll centers its sphere on the ALO origin, not GLOB::posCenter.
    // Include the mesh's offset and the owner's scale for every ALO subtype.
    const glm::mat4 ownerWorld = glm::make_mat4(ownerObject.worldTransform);
    const float ownerScale = (std::max)({glm::length(glm::vec3(ownerWorld[0])),
        glm::length(glm::vec3(ownerWorld[1])), glm::length(glm::vec3(ownerWorld[2]))});
    const float originRadius = glm::length(glm::vec3(ownerWorld * glm::vec4(center, 1.0f)) -
        glm::vec3(ownerWorld[3])) + glm::length(upper - center) * ownerScale;

    result.globIndex = document.EditableGlobs().size();
    document.EditableGlobs().push_back(std::move(glob));
    if (createdGlobset)
    {
        BrxGlobsetRecord globset;
        globset.ownerObjectIndex = ownerObjectIndex;
        globset.globIndices.push_back(result.globIndex);
        document.EditableGlobsets().push_back(std::move(globset));
    }
    else
        ownedGlobset->globIndices.push_back(result.globIndex);

    if (!document.CommitGlobEdit(result.globIndex))
    {
        if (createdGlobset)
            document.EditableGlobsets().pop_back();
        else
            ownedGlobset->globIndices.pop_back();
        document.EditableGlobs().pop_back();
        for (auto iterator = addedShaderIndices.rbegin(); iterator != addedShaderIndices.rend(); ++iterator)
            document.DeleteShaderAndRemap(*iterator, -1);
        for (auto iterator = addedBitmapIndices.rbegin(); iterator != addedBitmapIndices.rend(); ++iterator)
            document.DeleteBitmapAndRemap(*iterator, -1);
        result.message = "Imported geometry could not build render packets.";
        return result;
    }
    ownerObject.renderRadiusSelf = (std::max)(ownerObject.renderRadiusSelf, originRadius);
    ownerObject.renderRadiusAll = (std::max)(ownerObject.renderRadiusAll, ownerObject.renderRadiusSelf);
    result.succeeded = true;
    result.message = "Imported " + std::to_string(result.triangleCount) + " triangles from " +
                     path.filename().string() + " (game-scale radius " + std::to_string(glm::length(upper - center)) +
                     ").";
    return result;
}

ModelImportResult ImportFbxCharacter(const std::filesystem::path &path, BrxDocument &document,
                                     const int ownerObjectIndex, const FbxCharacterImportOptions &options)
{
    ModelImportResult blocked;
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension != ".fbx")
    {
        blocked.message = "Animated character import requires an FBX file.";
        return blocked;
    }
    if (!std::isfinite(options.sampleRate) || options.sampleRate <= 0.0f || options.sampleRate > 240.0f)
    {
        blocked.message = "FBX animation sample rate must be between 1 and 240 Hz.";
        return blocked;
    }

    FbxRigCapture capture;
    capture.sampleRate = options.sampleRate;
    capture.importAnimations = options.importAnimations;
    struct CaptureScope
    {
        explicit CaptureScope(FbxRigCapture &value)
        {
            g_fbxRigCapture = &value;
        }
        ~CaptureScope()
        {
            g_fbxRigCapture = nullptr;
        }
    } scope(capture);
    auto result = ImportStaticModel(path, document, ownerObjectIndex, options.model);
    if (!result.succeeded)
        return result;

    if (capture.bones.empty())
    {
        result.succeeded = false;
        result.message = "FBX skin contained no importable bones.";
        return result;
    }
    if (document.Objects().size() + capture.bones.size() + capture.clips.size() > 32767u)
    {
        result.succeeded = false;
        result.message = "The level has insufficient signed object-index capacity for this FBX rig.";
        return result;
    }

    const glm::mat4 ownerWorld =
        glm::make_mat4(document.Objects()[static_cast<std::size_t>(ownerObjectIndex)].worldTransform);
    std::vector<std::int32_t> boneObjects(capture.bones.size(), -1);
    std::vector<std::int16_t> boneOids(capture.bones.size(), -1);
    std::vector<std::uint8_t> boneState(capture.bones.size(), 0);
    std::string boneError;
    std::function<bool(std::size_t)> createBone = [&](const std::size_t boneIndex)
    {
        if (boneState[boneIndex] == 2)
            return true;
        if (boneState[boneIndex] == 1)
        {
            boneError = "FBX skeleton contains a parent cycle.";
            return false;
        }
        boneState[boneIndex] = 1;
        const auto &source = capture.bones[boneIndex];
        std::int32_t parent = ownerObjectIndex;
        if (source.parent >= 0 && static_cast<std::size_t>(source.parent) < capture.bones.size())
        {
            if (!createBone(static_cast<std::size_t>(source.parent)))
                return false;
            parent = boneObjects[static_cast<std::size_t>(source.parent)];
        }
        const glm::mat4 world = ownerWorld * source.world;
        const auto created = EditorDocumentOperations::CreateObject(document, 79, parent, glm::value_ptr(world));
        if (!created.succeeded)
        {
            boneError = created.message;
            return false;
        }
        boneObjects[boneIndex] = created.primaryObjectIndex;
        auto &object = document.EditableObjects()[static_cast<std::size_t>(created.primaryObjectIndex)];
        object.editorName = source.name;
        boneOids[boneIndex] = object.oid;
        boneState[boneIndex] = 2;
        return true;
    };
    for (std::size_t boneIndex = 0; boneIndex < capture.bones.size(); ++boneIndex)
        if (!createBone(boneIndex))
        {
            result.succeeded = false;
            result.message = "FBX model imported, but its bone hierarchy could not be created: " + boneError;
            return result;
        }

    auto globset =
        std::find_if(document.EditableGlobsets().begin(), document.EditableGlobsets().end(),
                     [&](const BrxGlobsetRecord &record) { return record.ownerObjectIndex == ownerObjectIndex; });
    if (globset == document.EditableGlobsets().end())
    {
        result.succeeded = false;
        result.message = "Imported character lost its required GLOBSET record.";
        return result;
    }
    globset->boneOids = boneOids;
    if (result.globIndex >= document.EditableGlobs().size())
    {
        result.succeeded = false;
        result.message = "Imported character GLOB record is missing.";
        return result;
    }
    auto &importedGlob = document.EditableGlobs()[result.globIndex];
    for (auto &subglob : importedGlob.sourceSubglobs)
        subglob.boneCount = static_cast<std::uint8_t>(boneOids.size());
    importedGlob.modified = true;
    if (!document.CommitGlobEdit(result.globIndex))
    {
        result.succeeded = false;
        result.message = "FBX skin weights could not build ProjectCane render packets.";
        return result;
    }

    std::size_t importedClips = 0;
    for (const auto &clip : capture.clips)
    {
        const auto created = EditorDocumentOperations::CreateObject(
            document, 131, ownerObjectIndex,
            document.Objects()[static_cast<std::size_t>(ownerObjectIndex)].worldTransform);
        if (!created.succeeded)
            continue;
        auto &asegObject = document.EditableObjects()[static_cast<std::size_t>(created.primaryObjectIndex)];
        asegObject.editorName = clip.name;
        auto segment = std::find_if(document.EditableAnimationSegments().begin(),
                                    document.EditableAnimationSegments().end(), [&](const BrxAsegRecord &record)
                                    { return record.ownerObjectIndex == created.primaryObjectIndex; });
        if (segment == document.EditableAnimationSegments().end())
            continue;
        segment->rootOid = document.Objects()[static_cast<std::size_t>(ownerObjectIndex)].oid;
        segment->searchRootOids = {segment->rootOid};
        segment->maximumFrame = static_cast<std::int16_t>((std::min)(32767.0f, std::ceil(clip.duration * 60.0f)));
        for (std::size_t boneIndex = 0; boneIndex < boneOids.size(); ++boneIndex)
        {
            BrxAsegChannelRecord channel;
            channel.targetOid = boneOids[boneIndex];
            channel.flags = 0x07u;
            channel.position.kind = channel.rotation.kind = channel.scale.kind = 1;
            channel.position.vectorKeys = clip.positions[boneIndex];
            channel.rotation.vectorKeys = clip.rotations[boneIndex];
            channel.scale.vectorKeys = clip.scales[boneIndex];
            segment->channels.push_back(std::move(channel));
        }
        ++importedClips;
    }
    result.message =
        "Imported skinned FBX with " + std::to_string(capture.bones.size()) + " bones, " +
        std::to_string(result.triangleCount) + " triangles, and " + std::to_string(importedClips) +
        " ASEG animation clip(s). This is a visual rig; gameplay AI, state machines, patrol data, and effects remain separate character-package authoring.";
    if (options.importAnimations && capture.clips.empty())
        result.message +=
            " No embedded animation keys were found. For motion, download a Mixamo animation with With Skin enabled.";
    return result;
}

ModelImportResult ReplaceStaticModel(const std::filesystem::path &path, BrxDocument &document,
                                     const int ownerObjectIndex, const ModelImportOptions &options)
{
    ModelImportResult blocked;
    std::unordered_set<std::size_t> priorRecords;
    for (std::size_t index = 0; index < document.Globs().size(); ++index)
        if (document.Globs()[index].ownerObjectIndex == ownerObjectIndex)
            priorRecords.insert(index);
    const auto authoritativeSource = [&](const std::size_t instanceRecord)
    {
        if (instanceRecord >= document.Globs().size())
            return static_cast<std::size_t>(-1);
        const auto &instance = document.Globs()[instanceRecord];
        if (!instance.instanced || instance.sourceGlobIndex < 0)
            return static_cast<std::size_t>(-1);
        for (const auto &globset : document.Globsets())
        {
            const auto found = std::find(globset.globIndices.begin(), globset.globIndices.end(), instanceRecord);
            if (found == globset.globIndices.end())
                continue;
            const auto instanceSlot = static_cast<std::size_t>(std::distance(globset.globIndices.begin(), found));
            const auto sourceSlot = static_cast<std::size_t>(instance.sourceGlobIndex);
            if (sourceSlot >= instanceSlot || sourceSlot >= globset.globIndices.size())
                return static_cast<std::size_t>(-1);
            return globset.globIndices[sourceSlot];
        }
        return static_cast<std::size_t>(-1);
    };
    for (std::size_t globIndex = 0; globIndex < document.Globs().size(); ++globIndex)
        if (document.Globs()[globIndex].instanced && priorRecords.contains(authoritativeSource(globIndex)))
        {
            blocked.message =
                "This model is a reusable source for linked GLOB instances. Replace or unlink those instances first so their retail source references cannot become dangling.";
            return blocked;
        }
    auto result = ImportStaticModel(path, document, ownerObjectIndex, options);
    if (!result.succeeded)
        return result;
    const std::size_t keep = result.globIndex;
    std::vector<std::size_t> removed;
    for (std::size_t index = 0; index < document.Globs().size(); ++index)
        if (index != keep && document.Globs()[index].ownerObjectIndex == ownerObjectIndex)
            removed.push_back(index);
    for (auto iterator = removed.rbegin(); iterator != removed.rend(); ++iterator)
    {
        const auto erased = *iterator;
        document.EditableGlobs().erase(document.EditableGlobs().begin() + erased);
        for (auto &globset : document.EditableGlobsets())
        {
            globset.globIndices.erase(std::remove(globset.globIndices.begin(), globset.globIndices.end(), erased),
                                      globset.globIndices.end());
            for (auto &index : globset.globIndices)
                if (index > erased)
                    --index;
        }
        if (result.globIndex > erased)
            --result.globIndex;
    }
    document.RebuildRenderPacketsFromGlobs();
    result.message += " Replaced the selected object's prior model while preserving its identity and authored data.";
    return result;
}

bool BuildModelCollision(BrxDocument &document, const int ownerObjectIndex, const bool cameraCollision,
                         std::string &message)
{
    if (ownerObjectIndex < 0 || static_cast<std::size_t>(ownerObjectIndex) >= document.Objects().size())
    {
        message = "Select a valid model object first.";
        return false;
    }
    auto &object = document.EditableObjects()[static_cast<std::size_t>(ownerObjectIndex)];
    if (!CidDerivesFrom(object.cid, 3))
    {
        message = "Generated gameplay collision requires an SO-derived object.";
        return false;
    }
    // Recompute from local source geometry, preserving LoadAloFromBrx's bound
    // conventions: fixed SO GLOB bounds are world-space, movable SO GLOB
    // bounds are local-space, and ALO render radii surround the world origin
    // of the object rather than the mesh center.
    if (object.offset == 0)
    {
        float largestRadius = 0.0f;
        for (auto &glob : document.EditableGlobs())
        {
            if (glob.ownerObjectIndex != ownerObjectIndex || glob.instanced || glob.sourceSubglobs.empty())
                continue;
            glm::vec3 localLower(std::numeric_limits<float>::max());
            glm::vec3 localUpper(std::numeric_limits<float>::lowest());
            for (const auto &subglob : glob.sourceSubglobs)
            {
                const glm::vec3 center(subglob.center[0], subglob.center[1], subglob.center[2]);
                localLower = glm::min(localLower, center - glm::vec3(subglob.radius));
                localUpper = glm::max(localUpper, center + glm::vec3(subglob.radius));
            }
            const glm::vec3 localCenter = (localLower + localUpper) * 0.5f;
            const glm::mat4 world = glm::make_mat4(object.worldTransform);
            const float scale = (std::max)({glm::length(glm::vec3(world[0])),
                glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))});
            const float worldRadius = glm::length(localUpper - localCenter) * scale;
            const glm::vec3 worldCenter = glm::vec3(world * glm::vec4(localCenter, 1.0f));
            const glm::vec3 cullingCenter = object.so.fixedPhysics ? worldCenter : localCenter;
            std::copy_n(&cullingCenter.x, 3, glob.center);
            glob.radius = object.so.fixedPhysics ? worldRadius : glm::length(localUpper - localCenter);
            glob.modified = true;
            largestRadius = (std::max)(largestRadius,
                glm::length(worldCenter - glm::vec3(world[3])) + worldRadius);
        }
        object.renderRadiusSelf = largestRadius;
        object.renderRadiusAll = (std::max)(object.renderRadiusAll, largestRadius);
    }
    struct Part
    {
        const BrxSubglobRecord *source = nullptr;
        glm::mat4 transform{1.0f};
    };
    std::vector<Part> parts;
    std::size_t positionTotal = 0, triangleTotal = 0;
    const glm::mat4 objectWorld = glm::make_mat4(object.worldTransform);
    const float objectDeterminant = glm::determinant(objectWorld);
    if (!std::isfinite(objectDeterminant) || std::abs(objectDeterminant) <= 1.0e-8f)
    {
        message =
            "The selected object's world transform is singular; its model cannot be converted to SO-local collision.";
        return false;
    }
    const glm::mat4 worldToObject = glm::inverse(objectWorld);
    for (const auto &glob : document.Globs())
    {
        if (glob.ownerObjectIndex != ownerObjectIndex || glob.instanced)
            continue;
        // SUBGLOB vertices are local to their GLOB placement. Convert each
        // direct GLOB through its authoritative source-to-world matrix into
        // the SO's local frame; ProjectCane then applies the SO transform for
        // non-fixed GEOM, while the fixed path below bakes it back to world.
        const glm::mat4 transform = worldToObject * glm::make_mat4(glob.sourceToWorld);
        for (const auto &source : glob.sourceSubglobs)
        {
            parts.push_back({&source, transform});
            positionTotal += source.vertices.size();
            triangleTotal += source.triangles.size();
        }
    }
    if (triangleTotal == 0)
    {
        message = "The selected object has no authored model triangles.";
        return false;
    }
    if (positionTotal > 32768 || triangleTotal > 21845)
    {
        message =
            "The model exceeds the retail SO position/surface/edge index ranges; use a simplified collision mesh.";
        return false;
    }
    BrxSoGeometryInfo geometry;
    geometry.positions.reserve(positionTotal);
    geometry.surfaces.reserve(triangleTotal);
    geometry.bspNodes.reserve(triangleTotal);
    glm::vec3 lower(std::numeric_limits<float>::max()), upper(std::numeric_limits<float>::lowest());
    std::size_t rejectedTriangles = 0;
    for (const auto &part : parts)
    {
        const auto base = geometry.positions.size();
        for (const auto &vertex : part.source->vertices)
        {
            const glm::vec3 transformed =
                glm::vec3(part.transform * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f));
            if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y) || !std::isfinite(transformed.z))
            {
                message = "The selected model contains non-finite vertex positions.";
                return false;
            }
            geometry.positions.push_back({transformed.x, transformed.y, transformed.z});
            lower = glm::min(lower, transformed);
            upper = glm::max(upper, transformed);
        }
        for (const auto &triangle : part.source->triangles)
        {
            if (triangle[0] >= part.source->vertices.size() || triangle[1] >= part.source->vertices.size() ||
                triangle[2] >= part.source->vertices.size() || triangle[0] == triangle[1] ||
                triangle[1] == triangle[2] || triangle[2] == triangle[0])
            {
                ++rejectedTriangles;
                continue;
            }
            BrxSoGeometryInfo::Surface surface;
            for (int lane = 0; lane < 3; ++lane)
                surface.positionIndices[lane] = static_cast<std::uint16_t>(base + triangle[lane]);
            const glm::vec3 a = glm::make_vec3(geometry.positions[surface.positionIndices[0]].data());
            const glm::vec3 b = glm::make_vec3(geometry.positions[surface.positionIndices[1]].data());
            const glm::vec3 c = glm::make_vec3(geometry.positions[surface.positionIndices[2]].data());
            const glm::vec3 cross = glm::cross(b - a, c - a);
            const float areaSquared = glm::dot(cross, cross);
            const float edgeScale =
                (std::max)({glm::dot(b - a, b - a), glm::dot(c - b, c - b), glm::dot(a - c, a - c), 1.0f});
            if (!std::isfinite(areaSquared) || areaSquared <= edgeScale * edgeScale * 1.0e-12f)
            {
                ++rejectedTriangles;
                continue;
            }
            const glm::vec3 center = (a + b + c) / 3.0f;
            std::copy(&center.x, &center.x + 3, surface.center.begin());
            surface.radius = (std::max)({glm::length(a - center), glm::length(b - center), glm::length(c - center)});
            geometry.surfaces.push_back(std::move(surface));
        }
    }
    if (geometry.surfaces.empty())
    {
        message = "Every model triangle is degenerate; safe gameplay collision cannot be generated.";
        return false;
    }
    const std::size_t sourceSurfaceTotal = geometry.surfaces.size();
    // A ProjectCane BSP is a solid-space classifier, not an acceleration tree
    // over arbitrary triangles.  A null negative child means solid/inside and
    // a null positive child means empty/outside.  The old index-balanced tree
    // consequently made each leaf triangle an infinite solid half-space.
    // Until a true polygon-splitting solid BSP builder is available, emit the
    // tight model bounds as six outward planes.  This is conservative, but it
    // is finite, deterministic, and obeys the retail classifier contract.
    geometry.positions.clear();
    geometry.surfaces.clear();
    geometry.bspNodes.clear();
    const std::array<glm::vec3, 8> boundsCorners{{{lower.x, lower.y, lower.z},
                                                  {upper.x, lower.y, lower.z},
                                                  {upper.x, upper.y, lower.z},
                                                  {lower.x, upper.y, lower.z},
                                                  {lower.x, lower.y, upper.z},
                                                  {upper.x, lower.y, upper.z},
                                                  {upper.x, upper.y, upper.z},
                                                  {lower.x, upper.y, upper.z}}};
    for (const auto &corner : boundsCorners)
        geometry.positions.push_back({corner.x, corner.y, corner.z});
    constexpr std::array<std::array<std::uint16_t, 3>, 6> boundsPlanes{{
        {{0, 2, 1}}, // -Z
        {{4, 5, 6}}, // +Z
        {{0, 1, 5}}, // -Y
        {{3, 7, 6}}, // +Y
        {{0, 4, 7}}, // -X
        {{1, 2, 6}}  // +X
    }};
    constexpr std::array<std::array<std::uint16_t, 4>, 6> boundsLoops{
        {{{0, 3, 2, 1}}, {{4, 5, 6, 7}}, {{0, 1, 5, 4}}, {{3, 7, 6, 2}}, {{0, 4, 7, 3}}, {{1, 2, 6, 5}}}};
    for (const auto &indices : boundsPlanes)
    {
        BrxSoGeometryInfo::Surface surface;
        surface.positionIndices = indices;
        glm::vec3 surfaceCenter(0.0f);
        for (const auto corner : boundsLoops[geometry.surfaces.size()])
            surfaceCenter += boundsCorners[corner];
        surfaceCenter *= 0.25f;
        std::copy(&surfaceCenter.x, &surfaceCenter.x + 3, surface.center.begin());
        surface.radius = 0.0f;
        for (const auto corner : boundsLoops[geometry.surfaces.size()])
            surface.radius = (std::max)(surface.radius, glm::length(boundsCorners[corner] - surfaceCenter));
        geometry.surfaces.push_back(std::move(surface));
    }
    // A retail SURF is a polygonal plane, not one triangle. Store all four
    // boundary edges for each box face and link every edge to the neighboring
    // face. This prevents only half of a triangulated side being collidable.
    struct EdgeUse
    {
        std::uint16_t surface = 0, edge = 0;
    };
    std::unordered_map<std::uint32_t, EdgeUse> openEdges;
    for (std::size_t surfaceIndex = 0; surfaceIndex < geometry.surfaces.size(); ++surfaceIndex)
    {
        auto &surface = geometry.surfaces[surfaceIndex];
        for (std::uint16_t edgeIndex = 0; edgeIndex < 4; ++edgeIndex)
        {
            const auto first = boundsLoops[surfaceIndex][edgeIndex];
            const auto second = boundsLoops[surfaceIndex][(edgeIndex + 1) % 4];
            const std::uint16_t low = (std::min)(first, second), high = (std::max)(first, second);
            const std::uint32_t key = (static_cast<std::uint32_t>(low) << 16) | high;
            const auto found = openEdges.find(key);
            if (found == openEdges.end())
            {
                const auto storedEdge = static_cast<std::uint16_t>(surface.edges.size());
                surface.edges.push_back({first, second, static_cast<std::uint16_t>(surfaceIndex)});
                openEdges.emplace(key, EdgeUse{static_cast<std::uint16_t>(surfaceIndex), storedEdge});
            }
            else
            {
                geometry.surfaces[found->second.surface].edges[found->second.edge].adjacentSurface =
                    static_cast<std::uint16_t>(surfaceIndex);
                openEdges.erase(found);
            }
        }
    }
    const std::size_t surfaceTotal = geometry.surfaces.size();
    geometry.positionCount = static_cast<std::uint16_t>(geometry.positions.size());
    geometry.surfaceCount = static_cast<std::uint16_t>(geometry.surfaces.size());
    geometry.edgeCount = 0;
    for (const auto &surface : geometry.surfaces)
        geometry.edgeCount += static_cast<std::uint16_t>(surface.edges.size());
    std::copy(&lower.x, &lower.x + 3, geometry.boundsMin);
    std::copy(&upper.x, &upper.x + 3, geometry.boundsMax);
    const glm::vec3 center = (lower + upper) * .5f;
    geometry.radius = glm::length(upper - center);
    // Intersect the six inward half-spaces.  Each first triangle below carries
    // one of the box's outward planes; positive exits immediately to empty,
    // while negative proceeds until the final inside leaf.
    for (std::size_t plane = 0; plane < geometry.surfaces.size(); ++plane)
        geometry.bspNodes.push_back({static_cast<std::uint16_t>(plane),
                                     plane + 1 < geometry.surfaces.size() ? static_cast<std::uint16_t>(plane + 1)
                                                                          : static_cast<std::uint16_t>(0xffff),
                                     static_cast<std::uint16_t>(0xffff)});
    geometry.fullBspNodeCount = static_cast<std::uint16_t>(geometry.bspNodes.size());
    // Imported/model-derived geometry starts in the owning ALO's local space.
    // LoadSoFromBrx skips UpdateGeomWorld when fFixedPhys is set, so decoded
    // fixed SOs require both GEOM payloads to be stored in world space.
    if (object.offset != 0 && object.so.fixedPhysics)
    {
        const glm::mat4 localToWorld = glm::make_mat4(object.worldTransform);
        const float radiusScale =
            (std::max)({glm::length(glm::vec3(localToWorld[0])), glm::length(glm::vec3(localToWorld[1])),
                        glm::length(glm::vec3(localToWorld[2]))});
        lower = glm::vec3((std::numeric_limits<float>::max)());
        upper = glm::vec3((std::numeric_limits<float>::lowest)());
        for (auto &position : geometry.positions)
        {
            const glm::vec3 transformed =
                glm::vec3(localToWorld * glm::vec4(position[0], position[1], position[2], 1.0f));
            position = {transformed.x, transformed.y, transformed.z};
            lower = glm::min(lower, transformed);
            upper = glm::max(upper, transformed);
        }
        for (auto &surface : geometry.surfaces)
        {
            const glm::vec3 transformed =
                glm::vec3(localToWorld * glm::vec4(surface.center[0], surface.center[1], surface.center[2], 1.0f));
            surface.center = {transformed.x, transformed.y, transformed.z};
            surface.radius *= radiusScale;
        }
        std::copy(&lower.x, &lower.x + 3, geometry.boundsMin);
        std::copy(&upper.x, &upper.x + 3, geometry.boundsMax);
        geometry.radius *= radiusScale;
    }
    // ProjectCane's fFixedPhys controls the representation of collision
    // geometry; it does not make a root SO immovable. A root body still has
    // gravity integrated by PresetSoAccel unless its force/torque constraints
    // are both CNSTR_None (runtime CT_Locked). Author the complete static-body
    // contract only after collision generation succeeds. Upserting also
    // repairs physical models imported by older editor builds.
    if (!cameraCollision && object.offset == 0)
    {
        const auto setStaticOption = [&](const std::int16_t eopid, const std::int32_t value)
        {
            auto found = std::find_if(object.properties.begin(), object.properties.end(),
                                      [eopid](const BrxPropertyRecord &property) { return property.eopid == eopid; });
            if (found == object.properties.end())
            {
                const auto definition = EditorOptionDefinitionForCid(object.cid, eopid);
                BrxPropertyRecord property;
                property.eopid = eopid;
                property.kind = definition.kind;
                property.flags = definition.flags;
                property.argumentCount = definition.argumentCount;
                property.editable = true;
                property.modified = true;
                ResetPropertyValue(property);
                object.properties.push_back(std::move(property));
                found = std::prev(object.properties.end());
            }
            found->intValue = value;
            found->uintValue = static_cast<std::uint32_t>(value);
            found->boolValue = value != 0;
            found->modified = true;
        };
        setStaticOption(131, 1); // CNSTR_None -> CT_Locked translation/force.
        setStaticOption(132, 1); // CNSTR_None -> CT_Locked rotation/torque.
        setStaticOption(138, 1); // No gravity; explicit before constraints resolve.
    }
    object.so.present = true;
    object.so.modified = true;
    // Editor-created SO collision is authored in object-local coordinates.
    // Existing retail SOs retain fFixedPhys: that one flag controls both the
    // gameplay and camera GEOM representation, so a camera-only operation must
    // never silently reinterpret the untouched gameplay geometry (or vice versa).
    if (object.offset == 0)
        object.so.fixedPhysics = false;
    if (cameraCollision)
        object.so.cameraGeometry = geometry;
    else
    {
        object.so.collisionGeometry = geometry;
        object.so.bspNodeToCameraGroup.assign(geometry.bspNodes.size(), -1);
        // mpisurfihsgMic is a csurf+1 range table into ahsg. Once topology is
        // replaced, the old per-surface ranges cannot be inferred safely.
        object.so.hitSurfaceGroups.clear();
        object.so.surfaceToHitSurfaceGroup.clear();
    }
    {
        auto &triangles = document.EditableCollisionTriangles();
        const glm::mat4 collisionToWorld =
            object.so.fixedPhysics ? glm::mat4(1.0f) : glm::make_mat4(object.worldTransform);
        triangles.erase(
            std::remove_if(triangles.begin(), triangles.end(), [&](const BrxCollisionTriangle &t)
                           { return t.ownerObjectIndex == ownerObjectIndex && t.cameraGeometry == cameraCollision; }),
            triangles.end());
        for (const auto &surface : geometry.surfaces)
        {
            BrxCollisionTriangle triangle;
            triangle.ownerObjectIndex = ownerObjectIndex;
            triangle.cameraGeometry = cameraCollision;
            for (int lane = 0; lane < 3; ++lane)
            {
                const auto &source = geometry.positions[surface.positionIndices[lane]];
                const glm::vec3 world = glm::vec3(collisionToWorld * glm::vec4(source[0], source[1], source[2], 1.0f));
                triangle.positions[lane][0] = world.x;
                triangle.positions[lane][1] = world.y;
                triangle.positions[lane][2] = world.z;
            }
            triangles.push_back(triangle);
        }
    }
    message = "Generated " + std::to_string(surfaceTotal) + (cameraCollision ? " camera" : " gameplay") +
              " bounded collision surfaces from " + std::to_string(sourceSurfaceTotal) + " model triangles" +
              (!cameraCollision && object.offset == 0 ? " and locked the new editor SO as a static body" : "") +
              (rejectedTriangles ? " (rejected " + std::to_string(rejectedTriangles) + " degenerate triangles)" : "") +
              ".";
    return true;
}

namespace
{
struct QuantizedPosition
{
    std::int64_t x = 0, y = 0, z = 0;
    bool operator==(const QuantizedPosition &other) const noexcept
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct QuantizedPositionHash
{
    std::size_t operator()(const QuantizedPosition &value) const noexcept
    {
        std::size_t hash = std::hash<std::int64_t>{}(value.x);
        hash ^= std::hash<std::int64_t>{}(value.y) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        hash ^= std::hash<std::int64_t>{}(value.z) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        return hash;
    }
};

bool RebuildCollisionDerivedData(BrxSoGeometryInfo &geometry, std::string &error)
{
    if (geometry.positions.empty() || geometry.surfaces.empty())
    {
        error = "Collision geometry must contain positions and surfaces.";
        return false;
    }
    if (geometry.positions.size() > 32768u || geometry.surfaces.size() > 21845u)
    {
        error = "Collision exceeds the retail signed-index or edge-count limits.";
        return false;
    }

    // Canonicalize duplicate positions before recomputing derived data.
    // ReadGeom keeps signed 16-bit position indices, so removing redundant
    // entries improves large-hull capacity and gives every edge one identity.
    std::vector<std::array<float, 3>> compactPositions;
    std::vector<std::uint16_t> positionRemap(geometry.positions.size(), 0xffffu);
    std::unordered_map<QuantizedPosition, std::uint16_t, QuantizedPositionHash> canonical;
    constexpr double canonicalTolerance = 1.0e-6;
    for (std::size_t index = 0; index < geometry.positions.size(); ++index)
    {
        const auto &position = geometry.positions[index];
        if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2]))
        {
            error = "Collision contains a non-finite position.";
            return false;
        }
        const QuantizedPosition key{static_cast<std::int64_t>(std::llround(position[0] / canonicalTolerance)),
                                    static_cast<std::int64_t>(std::llround(position[1] / canonicalTolerance)),
                                    static_cast<std::int64_t>(std::llround(position[2] / canonicalTolerance))};
        const auto found = canonical.find(key);
        if (found != canonical.end())
            positionRemap[index] = found->second;
        else
        {
            const auto compactIndex = static_cast<std::uint16_t>(compactPositions.size());
            compactPositions.push_back(position);
            canonical.emplace(key, compactIndex);
            positionRemap[index] = compactIndex;
        }
    }
    for (auto &surface : geometry.surfaces)
    {
        for (auto &index : surface.positionIndices)
        {
            if (index >= positionRemap.size())
            {
                error = "Collision surface has an invalid position index.";
                return false;
            }
            index = positionRemap[index];
        }
        for (auto &edge : surface.edges)
        {
            if (edge.firstPosition >= positionRemap.size() || edge.secondPosition >= positionRemap.size())
            {
                error = "Collision edge has an invalid position index.";
                return false;
            }
            edge.firstPosition = positionRemap[edge.firstPosition];
            edge.secondPosition = positionRemap[edge.secondPosition];
            if (edge.firstPosition == edge.secondPosition)
            {
                error = "Collision welding collapsed an edge; regenerate or simplify the hull.";
                return false;
            }
        }
    }
    geometry.positions = std::move(compactPositions);
    std::vector<std::uint8_t> usedPositions(geometry.positions.size(), 0u);
    for (const auto &surface : geometry.surfaces)
    {
        for (const auto index : surface.positionIndices)
            usedPositions[index] = 1u;
        for (const auto &edge : surface.edges)
        {
            usedPositions[edge.firstPosition] = 1u;
            usedPositions[edge.secondPosition] = 1u;
        }
    }
    std::vector<std::uint16_t> usedRemap(geometry.positions.size(), 0xffffu);
    compactPositions.clear();
    compactPositions.reserve(geometry.positions.size());
    for (std::size_t index = 0; index < geometry.positions.size(); ++index)
        if (usedPositions[index] != 0u)
        {
            usedRemap[index] = static_cast<std::uint16_t>(compactPositions.size());
            compactPositions.push_back(geometry.positions[index]);
        }
    for (auto &surface : geometry.surfaces)
    {
        for (auto &index : surface.positionIndices)
            index = usedRemap[index];
        for (auto &edge : surface.edges)
        {
            edge.firstPosition = usedRemap[edge.firstPosition];
            edge.secondPosition = usedRemap[edge.secondPosition];
        }
    }
    geometry.positions = std::move(compactPositions);

    glm::vec3 lower((std::numeric_limits<float>::max)());
    glm::vec3 upper((std::numeric_limits<float>::lowest)());
    for (const auto &position : geometry.positions)
    {
        const glm::vec3 point(position[0], position[1], position[2]);
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
        {
            error = "Collision contains a non-finite position.";
            return false;
        }
        lower = glm::min(lower, point);
        upper = glm::max(upper, point);
    }

    for (std::size_t surfaceIndex = 0; surfaceIndex < geometry.surfaces.size(); ++surfaceIndex)
    {
        auto &surface = geometry.surfaces[surfaceIndex];
        for (const auto index : surface.positionIndices)
            if (index >= geometry.positions.size())
            {
                error = "Collision surface " + std::to_string(surfaceIndex) + " has an invalid position index.";
                return false;
            }
        const glm::vec3 a = glm::make_vec3(geometry.positions[surface.positionIndices[0]].data());
        const glm::vec3 b = glm::make_vec3(geometry.positions[surface.positionIndices[1]].data());
        const glm::vec3 c = glm::make_vec3(geometry.positions[surface.positionIndices[2]].data());
        const glm::vec3 cross = glm::cross(b - a, c - a);
        const float edgeScale =
            (std::max)({glm::dot(b - a, b - a), glm::dot(c - b, c - b), glm::dot(a - c, a - c), 1.0f});
        if (!std::isfinite(glm::dot(cross, cross)) || glm::dot(cross, cross) <= edgeScale * edgeScale * 1.0e-12f)
        {
            error = "Collision surface " + std::to_string(surfaceIndex) + " is degenerate.";
            return false;
        }
        std::unordered_set<std::uint16_t> boundaryPositions;
        if (surface.edges.size() > 127u)
        {
            error =
                "Collision surface " + std::to_string(surfaceIndex) + " exceeds ReadGeom's signed 8-bit edge limit.";
            return false;
        }
        for (const auto &edge : surface.edges)
        {
            if (edge.firstPosition >= geometry.positions.size() || edge.secondPosition >= geometry.positions.size() ||
                edge.adjacentSurface >= geometry.surfaces.size())
            {
                error = "Collision surface " + std::to_string(surfaceIndex) + " has an invalid edge record.";
                return false;
            }
            boundaryPositions.insert(edge.firstPosition);
            boundaryPositions.insert(edge.secondPosition);
        }
        if (boundaryPositions.empty())
            boundaryPositions.insert(surface.positionIndices.begin(), surface.positionIndices.end());
        glm::vec3 center(0.0f);
        for (const auto index : boundaryPositions)
            center += glm::make_vec3(geometry.positions[index].data());
        center /= static_cast<float>(boundaryPositions.size());
        std::copy(&center.x, &center.x + 3, surface.center.begin());
        surface.radius = 0.0f;
        for (const auto index : boundaryPositions)
            surface.radius =
                (std::max)(surface.radius, glm::length(glm::make_vec3(geometry.positions[index].data()) - center));
    }
    for (std::size_t nodeIndex = 0; nodeIndex < geometry.bspNodes.size(); ++nodeIndex)
    {
        const auto &node = geometry.bspNodes[nodeIndex];
        if (node.surfaceIndex >= geometry.surfaces.size())
        {
            error = "Collision BSP node " + std::to_string(nodeIndex) + " references an invalid surface.";
            return false;
        }
        for (const auto child : {node.negativeChild, node.positiveChild})
            if (child != 0xffffu && child >= geometry.bspNodes.size())
            {
                error = "Collision BSP node " + std::to_string(nodeIndex) + " references an invalid child.";
                return false;
            }
    }
    if (!geometry.bspNodes.empty())
    {
        std::vector<std::uint8_t> state(geometry.bspNodes.size(), 0u);
        std::vector<std::pair<std::uint16_t, bool>> work;
        work.emplace_back(0u, false);
        while (!work.empty())
        {
            const auto [nodeIndex, leaving] = work.back();
            work.pop_back();
            if (leaving)
            {
                state[nodeIndex] = 2u;
                continue;
            }
            if (state[nodeIndex] == 1u)
            {
                error = "Collision BSP contains a cycle reachable from its root.";
                return false;
            }
            if (state[nodeIndex] == 2u)
                continue;
            state[nodeIndex] = 1u;
            work.emplace_back(nodeIndex, true);
            const auto &node = geometry.bspNodes[nodeIndex];
            for (const auto child : {node.negativeChild, node.positiveChild})
                if (child != 0xffffu)
                    work.emplace_back(child, false);
        }
        const auto unreachable = static_cast<std::size_t>(std::count(state.begin(), state.end(), std::uint8_t{0u}));
        if (unreachable != 0u)
        {
            error = "Collision BSP contains " + std::to_string(unreachable) +
                    " unreachable node(s); rebuild its BSP before export.";
            return false;
        }
    }

    std::unordered_set<std::uint32_t> physicalEdges;
    std::size_t serializedEdgeCount = 0;
    for (const auto &surface : geometry.surfaces)
        for (const auto &edge : surface.edges)
        {
            const auto low = (std::min)(edge.firstPosition, edge.secondPosition);
            const auto high = (std::max)(edge.firstPosition, edge.secondPosition);
            const std::uint32_t key = (static_cast<std::uint32_t>(low) << 16u) | high;
            if (!physicalEdges.insert(key).second)
            {
                error =
                    "Collision serializes a physical edge more than once; regenerate the hull instead of repairing it.";
                return false;
            }
            ++serializedEdgeCount;
        }
    geometry.positionCount = static_cast<std::uint16_t>(geometry.positions.size());
    geometry.surfaceCount = static_cast<std::uint16_t>(geometry.surfaces.size());
    if (serializedEdgeCount > 65535u)
    {
        error = "Collision contains more edges than the retail U16 format permits.";
        return false;
    }
    geometry.edgeCount = static_cast<std::uint16_t>(serializedEdgeCount);
    if (geometry.bspNodes.size() > 65535u)
    {
        error = "Collision contains more BSP nodes than the retail U16 format permits.";
        return false;
    }
    geometry.fullBspNodeCount = static_cast<std::uint16_t>(
        (std::max)(static_cast<std::size_t>(geometry.fullBspNodeCount), geometry.bspNodes.size()));
    std::copy(&lower.x, &lower.x + 3, geometry.boundsMin);
    std::copy(&upper.x, &upper.x + 3, geometry.boundsMax);
    const glm::vec3 center = (lower + upper) * 0.5f;
    geometry.radius = glm::length(upper - center);
    return true;
}

void SyncCollisionPreview(BrxDocument &document, const int ownerObjectIndex, const BrxSoGeometryInfo &geometry,
                          const bool cameraGeometry)
{
    auto &triangles = document.EditableCollisionTriangles();
    const auto &owner = document.Objects()[static_cast<std::size_t>(ownerObjectIndex)];
    const glm::mat4 collisionToWorld = owner.so.fixedPhysics ? glm::mat4(1.0f) : glm::make_mat4(owner.worldTransform);
    triangles.erase(
        std::remove_if(
            triangles.begin(), triangles.end(), [ownerObjectIndex, cameraGeometry](const BrxCollisionTriangle &triangle)
            { return triangle.ownerObjectIndex == ownerObjectIndex && triangle.cameraGeometry == cameraGeometry; }),
        triangles.end());
    for (const auto &surface : geometry.surfaces)
    {
        BrxCollisionTriangle triangle;
        triangle.ownerObjectIndex = ownerObjectIndex;
        triangle.cameraGeometry = cameraGeometry;
        for (int lane = 0; lane < 3; ++lane)
        {
            const auto &source = geometry.positions[surface.positionIndices[lane]];
            const glm::vec3 world = glm::vec3(collisionToWorld * glm::vec4(source[0], source[1], source[2], 1.0f));
            triangle.positions[lane][0] = world.x;
            triangle.positions[lane][1] = world.y;
            triangle.positions[lane][2] = world.z;
        }
        triangles.push_back(triangle);
    }
}
}

bool ImportModelCollision(const std::filesystem::path &path, BrxDocument &document, const int ownerObjectIndex,
                          const ModelImportOptions &options, const bool cameraCollision, std::string &message)
{
    if (ownerObjectIndex < 0 || static_cast<std::size_t>(ownerObjectIndex) >= document.Objects().size())
    {
        message = "Select a valid SO first.";
        return false;
    }
    auto &object = document.EditableObjects()[static_cast<std::size_t>(ownerObjectIndex)];
    if (!CidDerivesFrom(object.cid, 3))
    {
        message = "Custom collision requires an SO-derived object.";
        return false;
    }
    if (!std::isfinite(options.scale) || options.scale <= 0.0f)
    {
        message = "Collision import scale must be finite and greater than zero.";
        return false;
    }

    ModelImportOptions collisionOptions = options;
    collisionOptions.importMaterials = false;
    collisionOptions.flipV = false;
    std::vector<MeshData> meshes;
    if (!ParseModelFile(path, collisionOptions, meshes, message))
        return false;

    glm::vec3 sourceLower((std::numeric_limits<float>::max)());
    glm::vec3 sourceUpper((std::numeric_limits<float>::lowest)());
    for (const auto &mesh : meshes)
        for (const auto &vertex : mesh.vertices)
        {
            const glm::vec3 point(vertex.position[0], vertex.position[1], vertex.position[2]);
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
            {
                message = "Collision mesh contains a non-finite vertex position.";
                return false;
            }
            sourceLower = glm::min(sourceLower, point);
            sourceUpper = glm::max(sourceUpper, point);
        }
    const float diagonal = glm::length(sourceUpper - sourceLower);
    if (!std::isfinite(diagonal) || diagonal <= 1.0e-5f)
    {
        message = "Collision mesh has no usable spatial extent.";
        return false;
    }
    const double weldTolerance = (std::max)(static_cast<double>(diagonal) * 1.0e-6, 1.0e-4);

    BrxSoGeometryInfo geometry;
    std::unordered_map<QuantizedPosition, std::uint16_t, QuantizedPositionHash> welded;
    std::vector<std::array<std::uint16_t, 3>> triangles;
    for (const auto &mesh : meshes)
    {
        std::vector<std::uint16_t> remap(mesh.vertices.size());
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
        {
            const auto &vertex = mesh.vertices[index];
            const glm::vec3 point(vertex.position[0], vertex.position[1], vertex.position[2]);
            const QuantizedPosition key{static_cast<std::int64_t>(std::llround(point.x / weldTolerance)),
                                        static_cast<std::int64_t>(std::llround(point.y / weldTolerance)),
                                        static_cast<std::int64_t>(std::llround(point.z / weldTolerance))};
            const auto found = welded.find(key);
            if (found != welded.end())
                remap[index] = found->second;
            else
            {
                if (geometry.positions.size() >= 32768u)
                {
                    message = "Collision mesh exceeds the retail 32,768-position limit after welding.";
                    return false;
                }
                const auto newIndex = static_cast<std::uint16_t>(geometry.positions.size());
                geometry.positions.push_back({point.x, point.y, point.z});
                welded.emplace(key, newIndex);
                remap[index] = newIndex;
            }
        }
        for (const auto &triangle : mesh.triangles)
        {
            if (triangle[0] >= remap.size() || triangle[1] >= remap.size() || triangle[2] >= remap.size())
            {
                message = "Collision mesh contains an invalid triangle index.";
                return false;
            }
            const std::array<std::uint16_t, 3> mapped{remap[triangle[0]], remap[triangle[1]], remap[triangle[2]]};
            if (mapped[0] == mapped[1] || mapped[1] == mapped[2] || mapped[2] == mapped[0])
                continue;
            triangles.push_back(mapped);
        }
    }
    if (geometry.positions.size() < 4u || triangles.size() < 4u)
    {
        message = "Collision mesh must contain a closed three-dimensional hull.";
        return false;
    }
    if (triangles.size() > 4096u || geometry.positions.size() > 4096u)
    {
        message = "Exact custom collision is limited to 4,096 vertices and triangles; import a simplified convex hull.";
        return false;
    }

    glm::vec3 solidCenter(0.0f);
    for (const auto &position : geometry.positions)
        solidCenter += glm::vec3(position[0], position[1], position[2]);
    solidCenter /= static_cast<float>(geometry.positions.size());
    std::unordered_map<std::uint32_t, std::uint32_t> edgeUses;
    const float planeTolerance = (std::max)(diagonal * 1.0e-5f, 1.0e-3f);
    for (auto indices : triangles)
    {
        auto point = [&](const std::uint16_t index)
        {
            const auto &p = geometry.positions[index];
            return glm::vec3(p[0], p[1], p[2]);
        };
        glm::vec3 a = point(indices[0]), b = point(indices[1]), c = point(indices[2]);
        glm::vec3 normal = glm::cross(b - a, c - a);
        if (!std::isfinite(glm::dot(normal, normal)) || glm::length(normal) <= planeTolerance * 1.0e-3f)
            continue;
        if (glm::dot(normal, ((a + b + c) / 3.0f) - solidCenter) < 0.0f)
        {
            std::swap(indices[1], indices[2]);
            b = point(indices[1]);
            c = point(indices[2]);
            normal = -normal;
        }
        normal = glm::normalize(normal);
        for (const auto &position : geometry.positions)
        {
            const glm::vec3 candidate(position[0], position[1], position[2]);
            if (glm::dot(normal, candidate - a) > planeTolerance)
            {
                message =
                    "Collision mesh is concave. Use a closed convex hull, or split it into multiple fixed SO collision hulls.";
                return false;
            }
        }
        BrxSoGeometryInfo::Surface surface;
        surface.positionIndices = indices;
        geometry.surfaces.push_back(std::move(surface));
        for (int edge = 0; edge < 3; ++edge)
        {
            const auto first = indices[edge], second = indices[(edge + 1) % 3];
            const auto low = (std::min)(first, second), high = (std::max)(first, second);
            ++edgeUses[(static_cast<std::uint32_t>(low) << 16u) | high];
        }
    }
    if (geometry.surfaces.size() < 4u)
    {
        message = "Collision mesh contains too few non-degenerate surfaces.";
        return false;
    }
    for (const auto &[edge, uses] : edgeUses)
        if (uses != 2u)
        {
            message =
                "Collision mesh is open or non-manifold; every welded edge must be shared by exactly two triangles.";
            return false;
        }

    // Retail GEOM surfaces are polygonal planes: three position indices define
    // the plane, while the variable edge list defines the complete boundary.
    // Keeping two coplanar source triangles as two BSP planes makes only the
    // first triangle's edge region authoritative during contact generation.
    // Merge them into one plane and retain only the outer polygon edges.
    struct PlaneGroup
    {
        glm::vec3 normal{0.0f};
        float distance = 0.0f;
        std::array<std::uint16_t, 3> representative{};
        std::unordered_map<std::uint32_t, std::uint32_t> edgeCounts;
    };
    std::vector<PlaneGroup> planeGroups;
    const float normalTolerance = 1.0e-4f;
    for (const auto &surface : geometry.surfaces)
    {
        const auto point = [&](const std::uint16_t index)
        {
            const auto &p = geometry.positions[index];
            return glm::vec3(p[0], p[1], p[2]);
        };
        const glm::vec3 a = point(surface.positionIndices[0]);
        const glm::vec3 b = point(surface.positionIndices[1]);
        const glm::vec3 c = point(surface.positionIndices[2]);
        const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        const float distance = glm::dot(normal, a);
        auto group = std::find_if(planeGroups.begin(), planeGroups.end(),
                                  [&](const PlaneGroup &candidate)
                                  {
                                      return glm::dot(candidate.normal, normal) >= 1.0f - normalTolerance &&
                                             std::abs(candidate.distance - distance) <= planeTolerance;
                                  });
        if (group == planeGroups.end())
        {
            PlaneGroup created;
            created.normal = normal;
            created.distance = distance;
            created.representative = surface.positionIndices;
            planeGroups.push_back(std::move(created));
            group = std::prev(planeGroups.end());
        }
        for (int edge = 0; edge < 3; ++edge)
        {
            const auto first = surface.positionIndices[edge];
            const auto second = surface.positionIndices[(edge + 1) % 3];
            const auto low = (std::min)(first, second), high = (std::max)(first, second);
            ++group->edgeCounts[(static_cast<std::uint32_t>(low) << 16u) | high];
        }
    }
    if (planeGroups.size() < 4u)
    {
        message = "Collision hull does not contain enough distinct planes to enclose a solid.";
        return false;
    }

    std::vector<std::vector<std::uint32_t>> boundaryEdges(planeGroups.size());
    std::unordered_map<std::uint32_t, std::vector<std::uint16_t>> edgePlaneGroups;
    geometry.surfaces.clear();
    geometry.surfaces.reserve(planeGroups.size());
    for (std::size_t groupIndex = 0; groupIndex < planeGroups.size(); ++groupIndex)
    {
        BrxSoGeometryInfo::Surface surface;
        surface.positionIndices = planeGroups[groupIndex].representative;
        geometry.surfaces.push_back(std::move(surface));
        for (const auto &[edge, count] : planeGroups[groupIndex].edgeCounts)
        {
            if (count == 1u)
            {
                boundaryEdges[groupIndex].push_back(edge);
                edgePlaneGroups[edge].push_back(static_cast<std::uint16_t>(groupIndex));
            }
            else if (count != 2u)
            {
                message = "A coplanar collision face has non-manifold internal triangulation.";
                return false;
            }
        }
        if (boundaryEdges[groupIndex].size() < 3u)
        {
            message = "A coplanar collision face has no valid outer boundary.";
            return false;
        }
        if (boundaryEdges[groupIndex].size() > 127u)
        {
            message =
                "A collision face exceeds ReadGeom's signed 8-bit 127-edge-per-surface limit; simplify the convex hull.";
            return false;
        }
    }
    for (const auto &[edge, groups] : edgePlaneGroups)
        if (groups.size() != 2u)
        {
            message = "Collision hull face boundaries do not form a closed convex solid.";
            return false;
        }

    geometry.bspNodes.reserve(geometry.surfaces.size());
    for (std::size_t surface = 0; surface < geometry.surfaces.size(); ++surface)
        geometry.bspNodes.push_back({static_cast<std::uint16_t>(surface),
                                     surface + 1u < geometry.surfaces.size() ? static_cast<std::uint16_t>(surface + 1u)
                                                                             : static_cast<std::uint16_t>(0xffffu),
                                     static_cast<std::uint16_t>(0xffffu)});
    if (!RebuildCollisionDerivedData(geometry, message))
        return false;
    std::size_t polygonEdgeCount = 0;
    for (std::size_t groupIndex = 0; groupIndex < planeGroups.size(); ++groupIndex)
    {
        auto &surface = geometry.surfaces[groupIndex];
        surface.edges.clear();
        glm::vec3 faceCenter(0.0f);
        std::unordered_set<std::uint16_t> facePositions;
        for (const auto edge : boundaryEdges[groupIndex])
        {
            const auto first = static_cast<std::uint16_t>(edge >> 16u);
            const auto second = static_cast<std::uint16_t>(edge & 0xffffu);
            const auto &groups = edgePlaneGroups[edge];
            // GEOM stores a physical edge once. ReadGeom links that record to
            // its owner through pedge and to the adjacent surface through
            // pedgeOther. Emitting it from both faces doubles the edge graph
            // and does not match retail SO topology.
            if (groups[0] == groupIndex)
                surface.edges.push_back({first, second, groups[1]});
            facePositions.insert(first);
            facePositions.insert(second);
        }
        for (const auto index : facePositions)
        {
            const auto &p = geometry.positions[index];
            faceCenter += glm::vec3(p[0], p[1], p[2]);
        }
        faceCenter /= static_cast<float>(facePositions.size());
        std::copy(&faceCenter.x, &faceCenter.x + 3, surface.center.begin());
        surface.radius = 0.0f;
        for (const auto index : facePositions)
        {
            const auto &p = geometry.positions[index];
            surface.radius = (std::max)(surface.radius, glm::length(glm::vec3(p[0], p[1], p[2]) - faceCenter));
        }
        polygonEdgeCount += surface.edges.size();
    }
    if (polygonEdgeCount > 65535u)
    {
        message = "Collision polygon boundaries exceed the retail U16 edge limit.";
        return false;
    }
    geometry.edgeCount = static_cast<std::uint16_t>(polygonEdgeCount);

    if (object.offset != 0 && object.so.fixedPhysics)
    {
        const glm::mat4 localToWorld = glm::make_mat4(object.worldTransform);
        for (auto &position : geometry.positions)
        {
            const glm::vec3 transformed =
                glm::vec3(localToWorld * glm::vec4(position[0], position[1], position[2], 1.0f));
            position = {transformed.x, transformed.y, transformed.z};
        }
        // Recompute all bounds from the transformed authoritative positions.
        // This also handles non-uniform object scale conservatively and keeps
        // the serialized SURF bounds consistent with the transformed planes.
        if (!RebuildCollisionDerivedData(geometry, message))
            return false;
    }

    if (!cameraCollision && object.offset == 0)
    {
        const auto setStaticOption = [&](const std::int16_t eopid)
        {
            auto found = std::find_if(object.properties.begin(), object.properties.end(),
                                      [eopid](const BrxPropertyRecord &property) { return property.eopid == eopid; });
            if (found == object.properties.end())
            {
                const auto definition = EditorOptionDefinitionForCid(object.cid, eopid);
                BrxPropertyRecord property;
                property.eopid = eopid;
                property.kind = definition.kind;
                property.flags = definition.flags;
                property.argumentCount = definition.argumentCount;
                property.editable = true;
                property.modified = true;
                ResetPropertyValue(property);
                object.properties.push_back(std::move(property));
                found = std::prev(object.properties.end());
            }
            found->intValue = 1;
            found->uintValue = 1;
            found->boolValue = true;
            found->modified = true;
        };
        setStaticOption(131);
        setStaticOption(132);
        setStaticOption(138);
    }
    object.so.present = true;
    object.so.modified = true;
    // fFixedPhys is shared by gameplay and camera GEOM. Preserve the retail
    // representation on decoded objects; new editor SOs author local geometry.
    if (object.offset == 0)
        object.so.fixedPhysics = false;
    if (cameraCollision)
        object.so.cameraGeometry = std::move(geometry);
    else
    {
        object.so.collisionGeometry = std::move(geometry);
        object.so.bspNodeToCameraGroup.assign(object.so.collisionGeometry.bspNodes.size(), -1);
        object.so.hitSurfaceGroups.clear();
        object.so.surfaceToHitSurfaceGroup.clear();
    }
    SyncCollisionPreview(document, ownerObjectIndex,
                         cameraCollision ? object.so.cameraGeometry : object.so.collisionGeometry, cameraCollision);
    message = "Imported exact " + std::string(cameraCollision ? "camera" : "gameplay") + " collision from " +
              path.filename().string() + " (" +
              std::to_string(cameraCollision ? object.so.cameraGeometry.surfaces.size()
                                             : object.so.collisionGeometry.surfaces.size()) +
              " surfaces). The visible model was not changed.";
    return true;
}

#if 0 // Rejected experiment: triangle-prism terrain is not a proper authored collision workflow.
bool ImportTerrainCollision(const std::filesystem::path& path, BrxDocument& document,
    const int ownerObjectIndex, const ModelImportOptions& options, std::string& message)
{
    if (ownerObjectIndex < 0 || static_cast<std::size_t>(ownerObjectIndex) >= document.Objects().size())
    { message = "Select a valid SO first."; return false; }
    auto& object = document.EditableObjects()[static_cast<std::size_t>(ownerObjectIndex)];
    if (!CidDerivesFrom(object.cid, 3))
    { message = "Terrain collision requires an SO-derived object."; return false; }
    if (!std::isfinite(options.scale) || options.scale <= 0.0f)
    { message = "Terrain collision import scale must be finite and greater than zero."; return false; }

    ModelImportOptions collisionOptions = options;
    collisionOptions.importMaterials = false; collisionOptions.flipV = false;
    std::vector<MeshData> meshes;
    if (!ParseModelFile(path, collisionOptions, meshes, message)) return false;
    std::size_t triangleCount = 0;
    glm::vec3 sourceLower((std::numeric_limits<float>::max)());
    glm::vec3 sourceUpper((std::numeric_limits<float>::lowest)());
    for (const auto& mesh : meshes)
    {
        triangleCount += mesh.triangles.size();
        for (const auto& vertex : mesh.vertices)
        {
            const glm::vec3 p(vertex.position[0], vertex.position[1], vertex.position[2]);
            sourceLower = glm::min(sourceLower, p); sourceUpper = glm::max(sourceUpper, p);
        }
    }
    if (triangleCount == 0u) { message = "Terrain file contains no triangles."; return false; }
    // Six positions, five polygonal surfaces, nine unique edges and five BSP
    // planes per prism. These checks are the actual retail field limits.
    if (triangleCount > 4369u || triangleCount * 6u > 32768u ||
        triangleCount * 9u > 65535u || triangleCount * 5u > 65535u)
    { message = "Terrain exceeds the retail SO position, surface, edge, or BSP limits; split it across multiple SOs."; return false; }
    const float diagonal = glm::length(sourceUpper - sourceLower);
    const float thickness = (std::max)(2.0f, diagonal * 1.0e-4f);

    BrxSoGeometryInfo geometry;
    geometry.positions.reserve(triangleCount * 6u);
    geometry.surfaces.reserve(triangleCount * 5u);
    geometry.bspNodes.reserve(triangleCount * 5u);
    glm::vec3 boundsMin((std::numeric_limits<float>::max)());
    glm::vec3 boundsMax((std::numeric_limits<float>::lowest)());
    std::size_t rejected = 0;
    for (const auto& mesh : meshes) for (const auto& triangle : mesh.triangles)
    {
        if (triangle[0] >= mesh.vertices.size() || triangle[1] >= mesh.vertices.size() ||
            triangle[2] >= mesh.vertices.size()) { ++rejected; continue; }
        const auto position = [&](const std::uint16_t index)
        { const auto& v = mesh.vertices[index]; return glm::vec3(v.position[0], v.position[1], v.position[2]); };
        const glm::vec3 a = position(triangle[0]), b = position(triangle[1]), c = position(triangle[2]);
        glm::vec3 normal = glm::cross(b - a, c - a);
        const float normalLength = glm::length(normal);
        if (!std::isfinite(normalLength) || normalLength <= 1.0e-5f) { ++rejected; continue; }
        normal /= normalLength;
        const glm::vec3 aBottom = a - normal * thickness;
        const glm::vec3 bBottom = b - normal * thickness;
        const glm::vec3 cBottom = c - normal * thickness;
        const std::uint16_t positionBase = static_cast<std::uint16_t>(geometry.positions.size());
        for (const auto& p : { a, b, c, aBottom, bBottom, cBottom })
        {
            geometry.positions.push_back({p.x,p.y,p.z});
            boundsMin = glm::min(boundsMin, p); boundsMax = glm::max(boundsMax, p);
        }
        const std::uint16_t surfaceBase = static_cast<std::uint16_t>(geometry.surfaces.size());
        const std::array<std::array<std::uint16_t,3>,5> planes{{
            {{0,1,2}}, {{3,5,4}}, {{0,3,4}}, {{1,4,5}}, {{2,5,3}} }};
        const std::array<std::array<std::uint16_t,4>,5> loops{{
            {{0,1,2,0}}, {{3,5,4,3}}, {{0,3,4,1}}, {{1,4,5,2}}, {{2,5,3,0}} }};
        for (std::size_t face = 0; face < planes.size(); ++face)
        {
            BrxSoGeometryInfo::Surface surface;
            for (int lane = 0; lane < 3; ++lane)
                surface.positionIndices[lane] = positionBase + planes[face][lane];
            const int vertexCount = face < 2u ? 3 : 4;
            glm::vec3 center(0.0f);
            for (int lane = 0; lane < vertexCount; ++lane)
            {
                const auto& p = geometry.positions[positionBase + loops[face][lane]];
                center += glm::vec3(p[0],p[1],p[2]);
            }
            center /= static_cast<float>(vertexCount);
            std::copy(&center.x, &center.x + 3, surface.center.begin());
            for (int lane = 0; lane < vertexCount; ++lane)
            {
                const auto& p = geometry.positions[positionBase + loops[face][lane]];
                surface.radius = (std::max)(surface.radius,
                    glm::length(glm::vec3(p[0],p[1],p[2]) - center));
            }
            geometry.surfaces.push_back(std::move(surface));
        }
        // Store each physical prism edge exactly once. The adjacent surface is
        // linked by ReadGeom through pedgeOther.
        auto addEdge = [&](const int owner, const int first, const int second, const int adjacent)
        {
            geometry.surfaces[surfaceBase + owner].edges.push_back({
                static_cast<std::uint16_t>(positionBase + first),
                static_cast<std::uint16_t>(positionBase + second),
                static_cast<std::uint16_t>(surfaceBase + adjacent) });
        };
        addEdge(0,0,1,2); addEdge(0,1,2,3); addEdge(0,2,0,4);
        addEdge(1,3,5,4); addEdge(1,5,4,3); addEdge(1,4,3,2);
        addEdge(2,0,3,4); addEdge(2,4,1,3); addEdge(3,5,2,4);

        const std::uint16_t nodeBase = static_cast<std::uint16_t>(geometry.bspNodes.size());
        const std::uint16_t nextSolid = static_cast<std::uint16_t>(nodeBase + 5u);
        for (std::uint16_t plane = 0; plane < 5u; ++plane)
            geometry.bspNodes.push_back({ static_cast<std::uint16_t>(surfaceBase + plane),
                plane < 4u ? static_cast<std::uint16_t>(nodeBase + plane + 1u) : static_cast<std::uint16_t>(0xffffu),
                nextSolid });
    }
    if (geometry.surfaces.empty())
    { message = "Every terrain triangle is degenerate."; return false; }
    // The final solid's outside branches terminate in positive/empty space.
    const std::size_t finalBase = geometry.bspNodes.size() - 5u;
    for (std::size_t node = finalBase; node < geometry.bspNodes.size(); ++node)
        geometry.bspNodes[node].positiveChild = 0xffffu;
    geometry.positionCount = static_cast<std::uint16_t>(geometry.positions.size());
    geometry.surfaceCount = static_cast<std::uint16_t>(geometry.surfaces.size());
    geometry.edgeCount = static_cast<std::uint16_t>(geometry.surfaces.size() / 5u * 9u);
    geometry.fullBspNodeCount = static_cast<std::uint16_t>(geometry.bspNodes.size());
    std::copy(&boundsMin.x, &boundsMin.x + 3, geometry.boundsMin);
    std::copy(&boundsMax.x, &boundsMax.x + 3, geometry.boundsMax);
    geometry.radius = glm::length(boundsMax - (boundsMin + boundsMax) * 0.5f);

    const auto setStaticOption = [&](const std::int16_t eopid)
    {
        auto found = std::find_if(object.properties.begin(), object.properties.end(),
            [eopid](const BrxPropertyRecord& property) { return property.eopid == eopid; });
        if (found == object.properties.end())
        {
            const auto definition = EditorOptionDefinitionForCid(object.cid,eopid);
            BrxPropertyRecord property; property.eopid=eopid; property.kind=definition.kind;
            property.flags=definition.flags; property.argumentCount=definition.argumentCount;
            property.editable=true; property.modified=true; ResetPropertyValue(property);
            object.properties.push_back(std::move(property)); found=std::prev(object.properties.end());
        }
        found->intValue=1; found->uintValue=1; found->boolValue=true; found->modified=true;
    };
    setStaticOption(131); setStaticOption(132); setStaticOption(138);
    object.so.present=true; object.so.fixedPhysics=false; object.so.modified=true;
    object.so.collisionGeometry=std::move(geometry);
    object.so.bspNodeToCameraGroup.assign(object.so.collisionGeometry.bspNodes.size(),-1);
    if(object.so.hitSurfaceGroups.empty())object.so.surfaceToHitSurfaceGroup.clear();
    else object.so.surfaceToHitSurfaceGroup.assign(object.so.collisionGeometry.surfaces.size()+1u,-1);
    SyncGameplayCollisionPreview(document,ownerObjectIndex,object.so.collisionGeometry);
    message="Imported terrain collision as "+std::to_string(object.so.collisionGeometry.surfaces.size()/5u)+
        " closed triangle prisms ("+std::to_string(object.so.collisionGeometry.bspNodes.size())+
        " BSP nodes, thickness "+std::to_string(thickness)+")"+
        (rejected?"; rejected "+std::to_string(rejected)+" degenerate triangles":"")+". Visible geometry was preserved.";
    return true;
}
#endif

bool RepairModelCollision(BrxDocument &document, const int ownerObjectIndex, const bool cameraCollision,
                          std::string &message)
{
    if (ownerObjectIndex < 0 || static_cast<std::size_t>(ownerObjectIndex) >= document.Objects().size())
    {
        message = "Select a valid SO first.";
        return false;
    }
    auto &object = document.EditableObjects()[static_cast<std::size_t>(ownerObjectIndex)];
    if (!object.so.present)
    {
        message = "The selected SO has no decoded collision payload.";
        return false;
    }
    auto &geometry = cameraCollision ? object.so.cameraGeometry : object.so.collisionGeometry;
    auto repaired = geometry;
    if (!RebuildCollisionDerivedData(repaired, message))
        return false;
    geometry = std::move(repaired);
    if (!cameraCollision)
    {
        object.so.bspNodeToCameraGroup.resize(geometry.bspNodes.size(), -1);
    }
    SyncCollisionPreview(document, ownerObjectIndex, geometry, cameraCollision);
    object.so.modified = true;
    message = std::string("Rebuilt ") + (cameraCollision ? "camera" : "gameplay") +
              " collision bounds, surface spheres, edges, adjacency, counts and preview data.";
    return true;
}

bool FlipModelCollisionNormals(BrxDocument &document, const int ownerObjectIndex, const bool cameraCollision,
                               std::string &message)
{
    if (ownerObjectIndex < 0 || static_cast<std::size_t>(ownerObjectIndex) >= document.Objects().size())
    {
        message = "Select a valid SO first.";
        return false;
    }
    auto &object = document.EditableObjects()[static_cast<std::size_t>(ownerObjectIndex)];
    if (!object.so.present)
    {
        message = "The selected SO has no decoded collision payload.";
        return false;
    }
    auto &geometry = cameraCollision ? object.so.cameraGeometry : object.so.collisionGeometry;
    if (geometry.surfaces.empty())
    {
        message = "The selected collision geometry has no surfaces.";
        return false;
    }
    auto flipped = geometry;
    for (auto &surface : flipped.surfaces)
    {
        std::swap(surface.positionIndices[1], surface.positionIndices[2]);
        for (auto &edge : surface.edges)
            std::swap(edge.firstPosition, edge.secondPosition);
    }
    // Reversing a node plane also reverses its half-spaces. Swap the children
    // so the same finite solid remains inside after normal correction.
    for (auto &node : flipped.bspNodes)
        std::swap(node.negativeChild, node.positiveChild);
    if (!RebuildCollisionDerivedData(flipped, message))
        return false;
    geometry = std::move(flipped);
    SyncCollisionPreview(document, ownerObjectIndex, geometry, cameraCollision);
    object.so.modified = true;
    message = std::string("Flipped ") + (cameraCollision ? "camera" : "gameplay") +
              " collision normals and preserved BSP inside/outside classification.";
    return true;
}

bool AlignImportedModelToOwner(BrxDocument &document, const int ownerObjectIndex, std::string &message)
{
    if (ownerObjectIndex < 0 || static_cast<std::size_t>(ownerObjectIndex) >= document.Objects().size())
    {
        message = "Select a valid imported model object first.";
        return false;
    }
    bool changed = false;
    const auto &owner = document.Objects()[static_cast<std::size_t>(ownerObjectIndex)];
    for (std::size_t index = 0; index < document.EditableGlobs().size(); ++index)
    {
        auto &glob = document.EditableGlobs()[index];
        if (glob.ownerObjectIndex != ownerObjectIndex || glob.instanced)
            continue;
        std::copy(std::begin(owner.worldTransform), std::end(owner.worldTransform), glob.sourceToWorld);
        glob.hasEditorPlacementTransform = false;
        const glm::mat4 identity(1.0f);
        std::copy(glm::value_ptr(identity), glm::value_ptr(identity) + 16, glob.editorPlacementTransform);
        glob.modified = true;
        changed |= document.CommitGlobEdit(index);
    }
    message = changed ? "Aligned the selected model with its object origin."
                      : "The selected object has no editable, directly-owned model geometry.";
    return changed;
}
