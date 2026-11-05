#include "CharacterExport.h"

#include "BrxDocument.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
using json = nlohmann::json;

void Align4(std::vector<std::uint8_t> &bytes)
{
    while ((bytes.size() & 3u) != 0)
        bytes.push_back(0);
}

template <class T> void Append(std::vector<std::uint8_t> &bytes, const T &value)
{
    const auto *begin = reinterpret_cast<const std::uint8_t *>(&value);
    bytes.insert(bytes.end(), begin, begin + sizeof(T));
}

std::int32_t RuntimeParent(const BrxObjectRecord &object)
{
    return object.proxyRuntimeParentIndex >= 0 ? object.proxyRuntimeParentIndex : object.parentIndex;
}

bool IsDescendant(const std::vector<BrxObjectRecord> &objects, std::int32_t object, const std::int32_t root)
{
    for (std::size_t guard = 0;
         object >= 0 && static_cast<std::size_t>(object) < objects.size() && guard++ <= objects.size();
         object = RuntimeParent(objects[static_cast<std::size_t>(object)]))
        if (object == root)
            return true;
    return false;
}

bool IsInCharacterInstance(const std::vector<BrxObjectRecord> &objects, const std::int32_t objectIndex,
                           const std::int32_t characterRoot)
{
    if (objectIndex < 0 || characterRoot < 0 || static_cast<std::size_t>(objectIndex) >= objects.size() ||
        static_cast<std::size_t>(characterRoot) >= objects.size())
        return false;
    if (IsDescendant(objects, objectIndex, characterRoot))
        return true;
    const auto &root = objects[static_cast<std::size_t>(characterRoot)];
    const auto &object = objects[static_cast<std::size_t>(objectIndex)];
    if (root.proxyRecordIndex < 0 || object.proxyRecordIndex != root.proxyRecordIndex)
        return false;
    return root.proxyRootIndex < 0 || object.proxyRootIndex == root.proxyRootIndex;
}

std::string Name(const BrxObjectRecord &object)
{
    if (!object.editorName.empty())
        return object.editorName;
    return std::string(BrxDocument::ClassName(object.cid)) + "_" + std::to_string(object.oid);
}

std::int32_t ResolveBone(const std::vector<BrxObjectRecord> &objects, const std::int16_t oid, const std::int32_t owner,
                         const std::int32_t characterRoot)
{
    std::unordered_map<std::int32_t, int> ownerAncestors;
    std::int32_t current = owner;
    for (int depth = 1; current >= 0 && static_cast<std::size_t>(current) < objects.size(); ++depth)
    {
        if (!ownerAncestors.emplace(current, depth).second)
            break;
        current = RuntimeParent(objects[static_cast<std::size_t>(current)]);
    }
    std::int32_t best = -1;
    int bestDepth = (std::numeric_limits<int>::max)();
    for (std::size_t index = 0; index < objects.size(); ++index)
    {
        if (objects[index].oid != oid || !IsDescendant(objects, static_cast<std::int32_t>(index), characterRoot))
            continue;
        current = static_cast<std::int32_t>(index);
        for (std::size_t guard = 0;
             current >= 0 && static_cast<std::size_t>(current) < objects.size() && guard++ <= objects.size();
             current = RuntimeParent(objects[static_cast<std::size_t>(current)]))
        {
            const auto found = ownerAncestors.find(current);
            if (found == ownerAncestors.end())
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
}

bool ExportRiggedCharacterGlb(const BrxDocument &document, const std::int32_t characterRoot,
                              const std::filesystem::path &path, std::string &error)
{
    error.clear();
    try
    {
        const auto &objects = document.Objects();
        if (characterRoot < 0 || static_cast<std::size_t>(characterRoot) >= objects.size())
        {
            error = "Character root is invalid.";
            return false;
        }

        struct SetExport
        {
            const BrxGlobsetRecord *set = nullptr;
            std::vector<std::int32_t> bones;
        };
        std::vector<SetExport> sets;
        std::unordered_set<std::int32_t> requiredObjects{characterRoot};
        for (const auto &set : document.Globsets())
        {
            if (set.boneOids.empty() || !IsInCharacterInstance(objects, set.ownerObjectIndex, characterRoot))
                continue;
            SetExport output{&set};
            bool valid = true;
            for (const auto oid : set.boneOids)
            {
                const std::int32_t bone = ResolveBone(objects, oid, set.ownerObjectIndex, characterRoot);
                if (bone < 0)
                {
                    valid = false;
                    break;
                }
                output.bones.push_back(bone);
                for (std::int32_t ancestor = bone; ancestor >= 0 && static_cast<std::size_t>(ancestor) < objects.size();
                     ancestor = RuntimeParent(objects[static_cast<std::size_t>(ancestor)]))
                {
                    requiredObjects.insert(ancestor);
                    if (ancestor == characterRoot)
                        break;
                }
            }
            if (valid)
                sets.push_back(std::move(output));
        }
        if (sets.empty())
        {
            error = "The selected character has no complete skinned GLOBSET palette.";
            return false;
        }

        json gltf = {
            {"asset", {{"version", "2.0"}, {"generator", "ProjectCane Map Editor rigged character exporter"}}}};
        json views = json::array(), accessors = json::array(), meshes = json::array();
        json nodes = json::array(), skins = json::array(), materials = json::array();
        materials.push_back(
            {{"name", "ProjectCane Material"},
             {"pbrMetallicRoughness",
              {{"baseColorFactor", {1.0, 1.0, 1.0, 1.0}}, {"metallicFactor", 0.0}, {"roughnessFactor", 1.0}}},
             {"doubleSided", true}});
        std::vector<std::uint8_t> binary;
        const auto accessor = [&](const std::size_t offset, const std::size_t length, const int target,
                                  const int component, const std::size_t count, const char *type,
                                  json extra = json::object())
        {
            const int view = static_cast<int>(views.size());
            json viewJson = {{"buffer", 0}, {"byteOffset", offset}, {"byteLength", length}};
            if (target != 0)
                viewJson["target"] = target;
            views.push_back(std::move(viewJson));
            json value = {{"bufferView", view}, {"componentType", component}, {"count", count}, {"type", type}};
            value.update(extra);
            const int index = static_cast<int>(accessors.size());
            accessors.push_back(std::move(value));
            return index;
        };

        const glm::mat4 rootWorld = glm::make_mat4(objects[static_cast<std::size_t>(characterRoot)].worldTransform);
        const glm::mat4 worldToRoot = glm::inverse(rootWorld);
        std::unordered_map<std::int32_t, int> objectNodes;
        std::vector<std::int32_t> ordered(requiredObjects.begin(), requiredObjects.end());
        std::sort(ordered.begin(), ordered.end(),
                  [&](const auto a, const auto b)
                  {
                      auto depth = [&](std::int32_t value)
                      {
                          int result = 0;
                          while (value >= 0 && value != characterRoot &&
                                 static_cast<std::size_t>(value) < objects.size())
                          {
                              value = RuntimeParent(objects[static_cast<std::size_t>(value)]);
                              ++result;
                          }
                          return result;
                      };
                      return depth(a) < depth(b);
                  });
        for (const auto objectIndex : ordered)
        {
            const auto &object = objects[static_cast<std::size_t>(objectIndex)];
            glm::mat4 local(1.0f);
            if (objectIndex != characterRoot)
            {
                const std::int32_t parent = RuntimeParent(object);
                const glm::mat4 parentWorld =
                    parent >= 0 ? glm::make_mat4(objects[static_cast<std::size_t>(parent)].worldTransform) : rootWorld;
                local = glm::inverse(parentWorld) * glm::make_mat4(object.worldTransform);
            }
            json matrix = json::array();
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    matrix.push_back(local[column][row]);
            const int node = static_cast<int>(nodes.size());
            nodes.push_back({{"name", Name(object)}, {"matrix", std::move(matrix)}});
            objectNodes[objectIndex] = node;
        }
        for (const auto objectIndex : ordered)
        {
            if (objectIndex == characterRoot)
                continue;
            const std::int32_t parent = RuntimeParent(objects[static_cast<std::size_t>(objectIndex)]);
            const auto parentNode = objectNodes.find(parent);
            if (parentNode == objectNodes.end())
                continue;
            auto &parentJson = nodes[parentNode->second];
            if (!parentJson.contains("children"))
                parentJson["children"] = json::array();
            parentJson["children"].push_back(objectNodes[objectIndex]);
        }

        std::size_t exportedPrimitiveCount = 0;
        for (const auto &output : sets)
        {
            const auto &set = *output.set;
            Align4(binary);
            const std::size_t inverseBindOffset = binary.size();
            for (const auto bone : output.bones)
            {
                const glm::mat4 boneInRoot =
                    worldToRoot * glm::make_mat4(objects[static_cast<std::size_t>(bone)].worldTransform);
                const glm::mat4 inverseBind = glm::inverse(boneInRoot);
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                        Append(binary, inverseBind[column][row]);
            }
            const int inverseBindAccessor =
                accessor(inverseBindOffset, binary.size() - inverseBindOffset, 0, 5126, output.bones.size(), "MAT4");
            json joints = json::array();
            for (const auto bone : output.bones)
                joints.push_back(objectNodes[bone]);
            const int skinIndex = static_cast<int>(skins.size());
            skins.push_back({{"name", Name(objects[static_cast<std::size_t>(set.ownerObjectIndex)]) + " Skin"},
                             {"inverseBindMatrices", inverseBindAccessor},
                             {"joints", joints},
                             {"skeleton", objectNodes[characterRoot]}});

            json primitives = json::array();
            for (const auto globIndex : set.globIndices)
            {
                if (globIndex >= document.Globs().size())
                    continue;
                const auto &glob = document.Globs()[globIndex];
                const glm::mat4 sourceToRoot = worldToRoot * glm::make_mat4(glob.sourceToWorld);
                const glm::mat3 normalToRoot = glm::inverseTranspose(glm::mat3(sourceToRoot));
                for (const auto &subglob : glob.sourceSubglobs)
                {
                    if (subglob.vertices.empty() || subglob.triangles.empty())
                        continue;
                    glm::vec3 minimum((std::numeric_limits<float>::max)());
                    glm::vec3 maximum((std::numeric_limits<float>::lowest)());
                    Align4(binary);
                    const std::size_t positionOffset = binary.size();
                    for (const auto &vertex : subglob.vertices)
                    {
                        const glm::vec3 value(sourceToRoot * glm::vec4(glm::make_vec3(vertex.position), 1.0f));
                        minimum = glm::min(minimum, value);
                        maximum = glm::max(maximum, value);
                        Append(binary, value.x);
                        Append(binary, value.y);
                        Append(binary, value.z);
                    }
                    const int positions = accessor(
                        positionOffset, binary.size() - positionOffset, 34962, 5126, subglob.vertices.size(), "VEC3",
                        {{"min", {minimum.x, minimum.y, minimum.z}}, {"max", {maximum.x, maximum.y, maximum.z}}});
                    Align4(binary);
                    const std::size_t normalOffset = binary.size();
                    for (const auto &vertex : subglob.vertices)
                    {
                        glm::vec3 value = normalToRoot * glm::make_vec3(vertex.normal);
                        if (glm::dot(value, value) > 0.000001f)
                            value = glm::normalize(value);
                        Append(binary, value.x);
                        Append(binary, value.y);
                        Append(binary, value.z);
                    }
                    const int normals = accessor(normalOffset, binary.size() - normalOffset, 34962, 5126,
                                                 subglob.vertices.size(), "VEC3");
                    Align4(binary);
                    const std::size_t uvOffset = binary.size();
                    for (const auto &vertex : subglob.vertices)
                    {
                        Append(binary, vertex.uv[0]);
                        Append(binary, vertex.uv[1]);
                    }
                    const int uvs =
                        accessor(uvOffset, binary.size() - uvOffset, 34962, 5126, subglob.vertices.size(), "VEC2");
                    Align4(binary);
                    const std::size_t jointOffset = binary.size();
                    for (const auto &vertex : subglob.vertices)
                        for (std::size_t influence = 0; influence < std::size(vertex.boneIndices); ++influence)
                        {
                            const std::uint16_t joint = vertex.boneIndices[influence] < output.bones.size()
                                                            ? vertex.boneIndices[influence]
                                                            : 0u;
                            Append(binary, joint);
                        }
                    const int jointAccessor = accessor(jointOffset, binary.size() - jointOffset, 34962, 5123,
                                                       subglob.vertices.size(), "VEC4");
                    Align4(binary);
                    const std::size_t weightOffset = binary.size();
                    for (const auto &vertex : subglob.vertices)
                    {
                        float total = 0.0f;
                        for (std::size_t influence = 0; influence < std::size(vertex.boneWeights); ++influence)
                            if (vertex.boneIndices[influence] < output.bones.size())
                                total += (std::max)(vertex.boneWeights[influence], 0.0f);
                        for (std::size_t influence = 0; influence < std::size(vertex.boneWeights); ++influence)
                        {
                            const float validWeight = vertex.boneIndices[influence] < output.bones.size()
                                                          ? (std::max)(vertex.boneWeights[influence], 0.0f)
                                                          : 0.0f;
                            const float weight =
                                total > 0.000001f ? validWeight / total : (influence == 0 ? 1.0f : 0.0f);
                            Append(binary, weight);
                        }
                    }
                    const int weightAccessor = accessor(weightOffset, binary.size() - weightOffset, 34962, 5126,
                                                        subglob.vertices.size(), "VEC4");
                    Align4(binary);
                    const std::size_t indexOffset = binary.size();
                    for (const auto &triangle : subglob.triangles)
                        for (const auto index : triangle)
                            Append(binary, static_cast<std::uint32_t>(index));
                    const int indices = accessor(indexOffset, binary.size() - indexOffset, 34963, 5125,
                                                 subglob.triangles.size() * 3u, "SCALAR");
                    primitives.push_back({{"attributes",
                                           {{"POSITION", positions},
                                            {"NORMAL", normals},
                                            {"TEXCOORD_0", uvs},
                                            {"JOINTS_0", jointAccessor},
                                            {"WEIGHTS_0", weightAccessor}}},
                                          {"indices", indices},
                                          {"material", 0},
                                          {"mode", 4}});
                    ++exportedPrimitiveCount;
                }
            }
            if (primitives.empty())
                continue;
            const int meshIndex = static_cast<int>(meshes.size());
            meshes.push_back({{"name", Name(objects[static_cast<std::size_t>(set.ownerObjectIndex)])},
                              {"primitives", std::move(primitives)}});
            const int meshNode = static_cast<int>(nodes.size());
            nodes.push_back({{"name", Name(objects[static_cast<std::size_t>(set.ownerObjectIndex)]) + " Mesh"},
                             {"mesh", meshIndex},
                             {"skin", skinIndex}});
            auto &rootNode = nodes[objectNodes[characterRoot]];
            if (!rootNode.contains("children"))
                rootNode["children"] = json::array();
            rootNode["children"].push_back(meshNode);
        }
        if (exportedPrimitiveCount == 0)
        {
            error = "The selected character contains no exportable skinned triangles.";
            return false;
        }

        gltf["buffers"] = json::array({{{"byteLength", binary.size()}}});
        gltf["bufferViews"] = std::move(views);
        gltf["accessors"] = std::move(accessors);
        gltf["materials"] = std::move(materials);
        gltf["meshes"] = std::move(meshes);
        gltf["nodes"] = std::move(nodes);
        gltf["skins"] = std::move(skins);
        gltf["scenes"] = json::array({{{"nodes", json::array({objectNodes[characterRoot]})}}});
        gltf["scene"] = 0;
        std::string jsonText = gltf.dump();
        while ((jsonText.size() & 3u) != 0)
            jsonText.push_back(' ');
        Align4(binary);
        const std::uint64_t total = 12u + 8u + jsonText.size() + 8u + binary.size();
        if (total > 0xffffffffu)
        {
            error = "Character GLB exceeds 4 GiB.";
            return false;
        }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            error = "Could not open the character GLB output file.";
            return false;
        }
        const auto writeU32 = [&](const std::uint32_t value)
        { stream.write(reinterpret_cast<const char *>(&value), sizeof(value)); };
        writeU32(0x46546c67u);
        writeU32(2u);
        writeU32(static_cast<std::uint32_t>(total));
        writeU32(static_cast<std::uint32_t>(jsonText.size()));
        writeU32(0x4e4f534au);
        stream.write(jsonText.data(), static_cast<std::streamsize>(jsonText.size()));
        writeU32(static_cast<std::uint32_t>(binary.size()));
        writeU32(0x004e4942u);
        stream.write(reinterpret_cast<const char *>(binary.data()), static_cast<std::streamsize>(binary.size()));
        stream.close();
        if (!stream)
        {
            error = "Failed while writing the character GLB.";
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
