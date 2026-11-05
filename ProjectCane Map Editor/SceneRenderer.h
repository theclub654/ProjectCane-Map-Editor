#pragma once

#include "BrxDocument.h"
#include "VisibilityMap.h"
#include "EditorCamera.h"
#include "EmitterPreviewRuntime.h"
#include "EditorOptions.h"
#include "RenderShaders.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class SceneRenderer
{
    struct WrNoise
    {
        float amplitude = 0.0f, frequency = 0.0f, phase = 0.0f, random = 0.0f;
    };
    struct WrOperator
    {
        enum class Kind
        {
            Circle,
            Bend
        } kind = Kind::Circle;
        int matrixIndex = -1;
        glm::vec3 normal{1.0f, 0.0f, 0.0f};
        float wavelength = 0.0f, frequency = 0.0f, phase = 0.0f;
        glm::vec3 dpos[2]{};
        glm::vec2 duv[2]{};
        glm::vec3 bendNormal{0.0f, 1.0f, 0.0f};
        glm::vec2 bendRange{};
        float bendBias = 0.5f;
        std::vector<WrNoise> bendNoise;
        glm::vec3 swivelNormal{0.0f, 0.0f, 1.0f};
        glm::vec2 swivelRange{};
        float swivelBias = 0.5f;
        std::vector<WrNoise> swivelNoise;
    };
    struct WrController
    {
        std::int16_t oid = -1;
        std::int32_t objectIndex = -1;
        std::vector<WrOperator> operators;
    };
    struct CollisionVertex
    {
        float position[3]{};
        float color[4]{};
    };
    struct BlipInstance
    {
        glm::vec3 position{};
        glm::vec4 color{1.0f};
        glm::vec4 lifeRollScale{};
        glm::vec4 velocityMode{};
    };
    struct GlobInstance
    {
        glm::mat4 model{1.0f};
        glm::vec4 objectCenter{};
    };
    struct EmitterRenderDiagnostics
    {
        std::size_t simulatedParticles = 0;
        std::size_t submittedBlips = 0;
        std::size_t rejectedMissingTexture = 0;
        std::size_t modelParticles = 0;
    };
    struct PerformanceDiagnostics
    {
        double runtimeMotionMs = 0.0;
        double wrDeformationMs = 0.0;
        double dynamicShadowsMs = 0.0;
        double sceneGeometryMs = 0.0;
        double collisionMs = 0.0;
        double emittersMs = 0.0;
        double gpuRenderMs = 0.0;
        double totalRenderCpuMs = 0.0;
        std::size_t activeLightCount = 0;
        std::size_t totalLightCount = 0;
        std::size_t submittedLightReferences = 0;
        std::size_t lightListBuildCount = 0;
        std::size_t maximumBatchLightCount = 0;
        std::size_t projectedShadowCount = 0;
        std::size_t dynamicShadowCasterCount = 0;
        std::size_t shadowReceiverBatchCount = 0;
        std::size_t fastShadowSourceCount = 0;
        bool dynamicShadowPreviewEnabled = false;
    };

  public:
    bool Initialize()
    {
        const char *vertexSource = RenderShaders::kGlobVertex;
        const char *fragmentSource = RenderShaders::kGlobFragment;
        const char *celVertexSource = RenderShaders::kCelVertex;
        const char *celFragmentSource = RenderShaders::kCelFragment;
        const auto compile = [](GLenum type, const char *source)
        {
            const GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint okay = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
            if (okay == GL_FALSE)
            {
                glDeleteShader(shader);
                return GLuint{};
            }
            return shader;
        };
        const GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
        const GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
        if (vertex == 0 || fragment == 0)
            return false;
        program_ = glCreateProgram();
        glAttachShader(program_, vertex);
        glAttachShader(program_, fragment);
        glLinkProgram(program_);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        GLint okay = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &okay);
        if (okay == GL_FALSE)
            return false;

        const GLuint celVertex = compile(GL_VERTEX_SHADER, celVertexSource);
        const GLuint celFragment = compile(GL_FRAGMENT_SHADER, celFragmentSource);
        if (celVertex == 0 || celFragment == 0)
            return false;
        celProgram_ = glCreateProgram();
        glAttachShader(celProgram_, celVertex);
        glAttachShader(celProgram_, celFragment);
        glLinkProgram(celProgram_);
        glDeleteShader(celVertex);
        glDeleteShader(celFragment);
        glGetProgramiv(celProgram_, GL_LINK_STATUS, &okay);
        if (okay == GL_FALSE)
            return false;

        static constexpr const char *collisionVertexSource = R"(
#version 430 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProjection;
out vec4 vColor;
void main()
{
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
    gl_Position.z -= 0.0001 * gl_Position.w;
    vColor = aColor;
}
)";
        static constexpr const char *collisionFragmentSource = R"(
#version 430 core
in vec4 vColor;
layout(location = 0) out vec4 oColor;
void main() { oColor = vColor; }
)";
        const GLuint collisionVertex = compile(GL_VERTEX_SHADER, collisionVertexSource);
        const GLuint collisionFragment = compile(GL_FRAGMENT_SHADER, collisionFragmentSource);
        if (collisionVertex == 0 || collisionFragment == 0)
            return false;
        collisionProgram_ = glCreateProgram();
        glAttachShader(collisionProgram_, collisionVertex);
        glAttachShader(collisionProgram_, collisionFragment);
        glLinkProgram(collisionProgram_);
        glDeleteShader(collisionVertex);
        glDeleteShader(collisionFragment);
        glGetProgramiv(collisionProgram_, GL_LINK_STATUS, &okay);
        if (okay == GL_FALSE)
            return false;

        static constexpr const char *thumbnailVertexSource = R"(
#version 430 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
layout(location=3) in vec2 aUv;
uniform mat4 uViewProjection;
uniform mat4 uModel;
out vec3 vNormal;
out vec4 vColor;
out vec2 vUv;
void main()
{
    gl_Position=uViewProjection*uModel*vec4(aPosition,1.0);
    vNormal=normalize(transpose(inverse(mat3(uModel)))*aNormal);
    vColor=aColor;
    vUv=aUv;
})";
        static constexpr const char *thumbnailFragmentSource = R"(
#version 430 core
in vec3 vNormal;
in vec4 vColor;
in vec2 vUv;
uniform sampler2D uDiffuse;
uniform bool uHasDiffuse;
uniform vec4 uTint;
layout(location=0) out vec4 oColor;
void main()
{
    vec4 base=vColor*uTint*(uHasDiffuse?texture(uDiffuse,vUv):vec4(1.0));
    if(base.a<0.03)discard;
    float light=0.30+0.70*max(dot(normalize(vNormal),normalize(vec3(0.45,-0.55,0.70))),0.0);
    oColor=vec4(base.rgb*light,base.a);
})";
        const GLuint thumbnailVertex = compile(GL_VERTEX_SHADER, thumbnailVertexSource);
        const GLuint thumbnailFragment = compile(GL_FRAGMENT_SHADER, thumbnailFragmentSource);
        if (thumbnailVertex == 0 || thumbnailFragment == 0)
            return false;
        thumbnailProgram_ = glCreateProgram();
        glAttachShader(thumbnailProgram_, thumbnailVertex);
        glAttachShader(thumbnailProgram_, thumbnailFragment);
        glLinkProgram(thumbnailProgram_);
        glDeleteShader(thumbnailVertex);
        glDeleteShader(thumbnailFragment);
        glGetProgramiv(thumbnailProgram_, GL_LINK_STATUS, &okay);
        if (okay == GL_FALSE)
            return false;

        static constexpr const char *shadowVertexSource = R"(
#version 430 core
layout(location=0) in vec3 aPosition;
layout(location=2) in vec4 aColor;
uniform mat4 uWorldToUv;
uniform mat4 uModel;
out float vAlpha;
void main() { vec4 uv=uWorldToUv*uModel*vec4(aPosition,1.0); gl_Position=vec4(uv.xy*2.0-uv.ww,uv.z,uv.w); vAlpha=aColor.a; }
)";
        static constexpr const char *shadowFragmentSource = R"(
#version 430 core
layout(location=0) out vec4 oColor;
in float vAlpha;
void main() { oColor=vec4(1.0,1.0,1.0,vAlpha); }
)";
        const GLuint shadowVertex = compile(GL_VERTEX_SHADER, shadowVertexSource);
        const GLuint shadowFragment = compile(GL_FRAGMENT_SHADER, shadowFragmentSource);
        if (shadowVertex == 0 || shadowFragment == 0)
            return false;
        shadowProgram_ = glCreateProgram();
        glAttachShader(shadowProgram_, shadowVertex);
        glAttachShader(shadowProgram_, shadowFragment);
        glLinkProgram(shadowProgram_);
        glDeleteShader(shadowVertex);
        glDeleteShader(shadowFragment);
        glGetProgramiv(shadowProgram_, GL_LINK_STATUS, &okay);
        if (okay == GL_FALSE)
            return false;

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vertexBuffer_);
        glGenBuffers(1, &indexBuffer_);
        glGenBuffers(1, &lightBuffer_);
        glGenBuffers(1, &shadowBuffer_);
        glGenBuffers(1, &celEdgeBuffer_);
        glGenBuffers(1, &globInstanceBuffer_);
        // Active SSBO blocks need backing storage even when their logical
        // count is zero or uInstanceMode selects an ordinary draw.
        UploadShaderStorageRecords(lightBuffer_, std::vector<GpuLightBlock>{}, GL_STATIC_DRAW);
        UploadShaderStorageRecords(shadowBuffer_, std::vector<GpuProjectedShadow>{}, GL_STATIC_DRAW);
        UploadShaderStorageRecords(globInstanceBuffer_, std::vector<GlobInstance>{}, GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lightBuffer_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, shadowBuffer_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, globInstanceBuffer_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BrxRenderVertex),
                              reinterpret_cast<void *>(offsetof(BrxRenderVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(BrxRenderVertex),
                              reinterpret_cast<void *>(offsetof(BrxRenderVertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BrxRenderVertex),
                              reinterpret_cast<void *>(offsetof(BrxRenderVertex, color)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(BrxRenderVertex),
                              reinterpret_cast<void *>(offsetof(BrxRenderVertex, uv)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(BrxRenderVertex),
                              reinterpret_cast<void *>(offsetof(BrxRenderVertex, boneWeightPreview)));
        glBindVertexArray(0);

        glGenVertexArrays(1, &collisionVao_);
        glGenBuffers(1, &collisionVertexBuffer_);
        glBindVertexArray(collisionVao_);
        glBindBuffer(GL_ARRAY_BUFFER, collisionVertexBuffer_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CollisionVertex),
                              reinterpret_cast<void *>(offsetof(CollisionVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(CollisionVertex),
                              reinterpret_cast<void *>(offsetof(CollisionVertex, color)));
        glBindVertexArray(0);

        const GLuint blipVertex = compile(GL_VERTEX_SHADER, RenderShaders::kBlipVertex);
        const GLuint blipFragment = compile(GL_FRAGMENT_SHADER, RenderShaders::kBlipFragment);
        if (blipVertex == 0 || blipFragment == 0)
            return false;
        blipProgram_ = glCreateProgram();
        glAttachShader(blipProgram_, blipVertex);
        glAttachShader(blipProgram_, blipFragment);
        glLinkProgram(blipProgram_);
        glDeleteShader(blipVertex);
        glDeleteShader(blipFragment);
        glGetProgramiv(blipProgram_, GL_LINK_STATUS, &okay);
        if (okay == GL_FALSE)
            return false;
        const float blipQuad[]{-1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
                               1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 1.0f,  0.0f, 1.0f};
        glGenVertexArrays(1, &blipVao_);
        glGenBuffers(1, &blipQuadBuffer_);
        glGenBuffers(1, &blipInstanceBuffer_);
        glBindVertexArray(blipVao_);
        glBindBuffer(GL_ARRAY_BUFFER, blipQuadBuffer_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(blipQuad), blipQuad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(2 * sizeof(float)));
        glBindBuffer(GL_ARRAY_BUFFER, blipInstanceBuffer_);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(BlipInstance),
                              reinterpret_cast<void *>(offsetof(BlipInstance, position)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(BlipInstance),
                              reinterpret_cast<void *>(offsetof(BlipInstance, color)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(BlipInstance),
                              reinterpret_cast<void *>(offsetof(BlipInstance, lifeRollScale)));
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(BlipInstance),
                              reinterpret_cast<void *>(offsetof(BlipInstance, velocityMode)));
        for (GLuint attribute = 2; attribute <= 5; ++attribute)
            glVertexAttribDivisor(attribute, 1);
        glBindVertexArray(0);
        glGenTextures(1, &whiteBlipTexture_);
        glBindTexture(GL_TEXTURE_2D, whiteBlipTexture_);
        const std::uint32_t white = 0xffffffffu;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glGenFramebuffers(1, &framebuffer_);
        glGenFramebuffers(1, &shadowFramebuffer_);
        glGenFramebuffers(1, &thumbnailFramebuffer_);
        glGenTextures(1, &colorTexture_);
        glGenRenderbuffers(1, &depthBuffer_);
        glGenRenderbuffers(1, &thumbnailDepthBuffer_);
        glGenQueries(static_cast<GLsizei>(gpuTimerQueries_.size()), gpuTimerQueries_.data());
        return true;
    }

    void Shutdown()
    {
        glDeleteQueries(static_cast<GLsizei>(gpuTimerQueries_.size()), gpuTimerQueries_.data());
        ReleaseSceneTextures();
        ClearModelThumbnailCache();
        glDeleteRenderbuffers(1, &thumbnailDepthBuffer_);
        glDeleteFramebuffers(1, &thumbnailFramebuffer_);
        glDeleteRenderbuffers(1, &depthBuffer_);
        glDeleteTextures(1, &colorTexture_);
        glDeleteFramebuffers(1, &framebuffer_);
        glDeleteFramebuffers(1, &shadowFramebuffer_);
        glDeleteBuffers(1, &indexBuffer_);
        glDeleteBuffers(1, &vertexBuffer_);
        glDeleteBuffers(1, &lightBuffer_);
        glDeleteBuffers(1, &shadowBuffer_);
        glDeleteBuffers(1, &celEdgeBuffer_);
        glDeleteBuffers(1, &globInstanceBuffer_);
        glDeleteBuffers(1, &collisionVertexBuffer_);
        glDeleteBuffers(1, &blipInstanceBuffer_);
        glDeleteBuffers(1, &blipQuadBuffer_);
        glDeleteVertexArrays(1, &blipVao_);
        glDeleteTextures(1, &whiteBlipTexture_);
        glDeleteVertexArrays(1, &collisionVao_);
        glDeleteVertexArrays(1, &vao_);
        glDeleteProgram(program_);
        glDeleteProgram(celProgram_);
        glDeleteProgram(collisionProgram_);
        glDeleteProgram(blipProgram_);
        glDeleteProgram(shadowProgram_);
        glDeleteProgram(thumbnailProgram_);
        *this = {};
    }

    void Upload(const BrxDocument &document)
    {
        // Loading or rebuilding scene data must never carry a running particle
        // simulation into the new document. Preview is always user initiated.
        emitterPreview_.Stop();
        emitterPreview_.InvalidateSnapshot();
        ReleaseSceneTextures();
        ClearModelThumbnailCache();
        indexCount_ = static_cast<GLsizei>(document.RenderIndices().size());
        batches_ = document.RenderBatches();
        vertices_ = document.RenderVertices();
        baseVertices_ = vertices_;
        indices_ = document.RenderIndices();
        std::stable_sort(batches_.begin(), batches_.end(), [](const BrxRenderBatch &left, const BrxRenderBatch &right)
                         { return left.renderPhase < right.renderPhase; });
        RebuildSurfaceBatchIndex();
        shaders_ = document.Shaders();
        RebuildEyesAnimationIndex();
        objects_ = document.Objects();
        RebuildConveyorPrimaryScrollers(document);
        BuildAloxResolvedTransforms();
        runtimeMotionStartTime_ = static_cast<float>(glfwGetTime());
        runtimeMotionDeltas_.assign(objects_.size(), glm::mat4(1.0f));
        asegPreviewDeltas_.assign(objects_.size(), glm::mat4(1.0f));
        asegPreviewActive_ = false;
        asegPreviewGeometryActive_ = false;
        asegPreviewVertices_.clear();
        asegPreviewCelEdges_.clear();
        runtimeLocalVelocities_.assign(objects_.size(), glm::vec3(0.0f));
        runtimeLocalAngularVelocities_.assign(objects_.size(), glm::vec3(0.0f));
        hasAuthoredRuntimeMotion_ = false;
        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex)
            for (const auto &property : objects_[objectIndex].properties)
            {
                if (property.eopid == 21)
                {
                    runtimeLocalVelocities_[objectIndex] = glm::make_vec3(property.floatValue);
                    hasAuthoredRuntimeMotion_ |= glm::dot(runtimeLocalVelocities_[objectIndex],
                                                          runtimeLocalVelocities_[objectIndex]) > 0.0000001f;
                }
                else if (property.eopid == 22)
                {
                    runtimeLocalAngularVelocities_[objectIndex] = glm::make_vec3(property.floatValue);
                    hasAuthoredRuntimeMotion_ |= glm::dot(runtimeLocalAngularVelocities_[objectIndex],
                                                          runtimeLocalAngularVelocities_[objectIndex]) > 0.0000001f;
                }
            }
        celBatches_ = document.CelBatches();
        celEdges_ = document.CelEdges();
        baseCelEdges_ = celEdges_;
        asegPreviewVertices_.clear();
        asegPreviewCelEdges_.clear();
        asegPreviewGeometryActive_ = false;
        visibilityNodes_ = document.VisibilityNodes();
        visibilityOneHopMasks_ = document.VisibilityOneHopMasks();
        globWarps_ = document.GlobWarps();
        boneWeightPreviewRenderGroup_ = kNoSelection;
        boneWeightPreviewFirstIndex_ = kNoSelection;
        BuildWrControllers();
        logicalOwnerByObject_.resize(objects_.size());
        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex)
        {
            std::int32_t logicalOwner = static_cast<std::int32_t>(objectIndex);
            std::int32_t candidate = logicalOwner;
            std::size_t guard = 0;
            while (candidate >= 0 && static_cast<std::size_t>(candidate) < objects_.size() &&
                   guard++ <= objects_.size())
            {
                const auto &candidateObject = objects_[static_cast<std::size_t>(candidate)];
                // A concrete SO-derived object is the gameplay identity.  Its
                // child ALOs own draw data but are only visual parts of it.
                if (candidateObject.cid != 3 && CidDerivesFrom(candidateObject.cid, 3))
                {
                    logicalOwner = candidate;
                    break;
                }
                candidate = candidateObject.parentIndex;
            }
            logicalOwnerByObject_[objectIndex] = logicalOwner;
        }
        // Apply authored cel state only after logical visual-to-gameplay owners
        // are known. SO geometry is commonly carried by child ALO records.
        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex)
        {
            for (const auto &property : objects_[objectIndex].properties)
            {
                if (property.eopid != 75 && property.eopid != 95 && property.eopid != 96)
                    continue;
                UpdateAloCelColor(static_cast<std::int32_t>(objectIndex), property.floatValue, property.eopid == 96);
            }
        }
        laserBeams_ = document.LaserBeams();
        laserTemplateObjects_.clear();
        laserTemplateObjectSet_.clear();
        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex)
        {
            if (objects_[objectIndex].oid != 650 && objects_[objectIndex].oid != 651)
                continue;
            // SnipAloObjects detaches these models from their LASEN during
            // ProjectCane's load.  Keep detached/global copies too: LASENs
            // without a private sense/damage model use the stock line model.
            laserTemplateObjects_.push_back(static_cast<std::int32_t>(objectIndex));
            laserTemplateObjectSet_.insert(static_cast<std::int32_t>(objectIndex));
        }
        worldMidtone_ = document.WorldHeader().worldMidtone;
        worldShadow_ = document.WorldHeader().worldShadow;
        std::copy(std::begin(document.WorldHeader().skyColor), std::end(document.WorldHeader().skyColor), skyColor_);
        fogNear_ = document.WorldHeader().fogNear;
        fogFar_ = document.WorldHeader().fogFar;
        fogMaximum_ = document.WorldHeader().fogMaximum;
        std::copy(std::begin(document.WorldHeader().fogColor), std::end(document.WorldHeader().fogColor), fogColor_);
        projectionFov_ = std::clamp(document.WorldHeader().cameraFov, 0.01f, 3.13f);
        projectionNear_ = std::max(document.WorldHeader().cameraNearClip, 0.001f);
        projectionFar_ = std::max(document.WorldHeader().cameraFarClip, projectionNear_ + 0.001f);
        cameraMrdRatio_ = std::clamp(document.WorldHeader().cameraMrdRatio, 0.75f, 3.0f);
        // CM's class-specific camera values are ordinary runtime setters, not
        // LoadOptionsFromBrx SetUser data. Authored defaults come from SW; the
        // CM inspector can temporarily simulate runtime changes after load.
        lights_.clear();
        lights_.reserve(std::min<std::size_t>(document.Lights().size(), 256));
        const auto fitLinear = [](float x0, float y0, float x1, float y1, float &scale, float &bias)
        {
            if (std::abs(x0 - x1) < 0.0001f)
            {
                scale = 0.0f;
                bias = y0;
            }
            else
            {
                scale = (y1 - y0) / (x1 - x0);
                bias = y0 - scale * x0;
            }
        };
        const auto angleCoefficients =
            [&](float angle0, float strength0, float angle1, float strength1, float &scale, float &bias)
        {
            const auto curve = [](float degrees)
            {
                const float cosine = std::cos(degrees * 0.008726647f);
                return cosine + cosine * cosine * cosine;
            };
            fitLinear(curve(angle0), strength0 / 255.0f, curve(angle1), strength1 / 255.0f, scale, bias);
        };
        const auto hsvColor = [](const float *hsv)
        {
            const float saturation = std::clamp(hsv[1] / 255.0f, 0.0f, 1.0f);
            float hue = std::fmod(std::max(hsv[0], 0.0f), 360.0f) / 60.0f;
            const int sector = static_cast<int>(std::floor(hue));
            const float fraction = hue - static_cast<float>(sector);
            const float p = 1.0f - saturation;
            const float q = 1.0f - saturation * fraction;
            const float t = 1.0f - saturation * (1.0f - fraction);
            switch (sector)
            {
            case 0:
                return glm::vec3(1.0f, t, p);
            case 1:
                return glm::vec3(q, 1.0f, p);
            case 2:
                return glm::vec3(p, 1.0f, t);
            case 3:
                return glm::vec3(p, q, 1.0f);
            case 4:
                return glm::vec3(t, p, 1.0f);
            default:
                return glm::vec3(1.0f, p, q);
            }
        };
        // Preserve SW::dlLight/serialized order exactly. ProjectCane stops at
        // its 256-slot active table; it does not reprioritize directional lights.
        std::vector<const BrxLightRecord *> selectedLights;
        selectedLights.reserve(256);
        lightSourceIndices_.clear();
        lightOwnerIndices_.clear();
        for (std::size_t sourceIndex = 0; sourceIndex < document.Lights().size(); ++sourceIndex)
        {
            const auto &source = document.Lights()[sourceIndex];
            if (selectedLights.size() >= 256)
                break;
            selectedLights.push_back(&source);
            lightSourceIndices_.push_back(sourceIndex);
            lightOwnerIndices_.push_back(source.ownerObjectIndex);
        }
        if (lightIsolationOwner_ >= 0 && std::find(lightOwnerIndices_.begin(), lightOwnerIndices_.end(),
                                                   lightIsolationOwner_) == lightOwnerIndices_.end())
            lightIsolationOwner_ = -1;

        for (const BrxLightRecord *sourceRecord : selectedLights)
        {
            const auto &source = *sourceRecord;
            GpuLight light;
            light.kind = source.kind;
            light.dynamic = source.dynamic;
            light.excludeDynamicObjects = source.excludeDynamicObjects;
            light.visibilityPosition = glm::vec3(source.position[0], source.position[1], source.position[2]);
            light.visibilityDirection = glm::vec3(source.direction[0], source.direction[1], source.direction[2]);
            light.visibilityDistance = source.falloff[1];
            light.visibilityConeAngle = source.coneAngle;
            light.position = glm::vec3(source.position[0], source.position[1], source.position[2]);
            light.direction = glm::normalize(glm::vec3(source.direction[0], source.direction[1], source.direction[2]));
            const glm::mat4 lightDelta = AloxDeltaForObject(source.ownerObjectIndex);
            light.position = glm::vec3(lightDelta * glm::vec4(light.position, 1.0f));
            glm::mat3 lightRotation(lightDelta);
            for (int axis = 0; axis < 3; ++axis)
            {
                const float length = glm::length(lightRotation[axis]);
                if (length > 0.000001f)
                    lightRotation[axis] /= length;
            }
            light.direction = glm::normalize(lightRotation * light.direction);
            light.color = source.highlightHsv[2] > 0.0001f ? hsvColor(source.highlightHsv) : glm::vec3(0.0f);
            angleCoefficients(360.0f - source.shadowAngle, 0.0f, 360.0f, source.shadowStrength, light.ru.x, light.du.x);
            angleCoefficients(source.midtoneAngle, 0.0f, 0.0f, source.midtoneStrength, light.ru.y, light.du.y);
            angleCoefficients(source.highlightAngle, 0.0f, 0.0f, source.highlightHsv[2], light.ru.z, light.du.z);
            // ProjectCane's positional lights use FitRecipFunction(gMin, 1,
            // gMax, 0): attenuation = constant + invDistance * (1 / d).
            const float nearDistance = source.falloff[0];
            const float farDistance = source.falloff[1];
            light.maximumDistance = (std::max)(farDistance, 0.0f);
            if (std::abs(nearDistance - farDistance) >= 0.0001f && std::abs(nearDistance) >= 0.0001f &&
                std::abs(farDistance) >= 0.0001f)
            {
                light.falloff.y = (nearDistance * farDistance * (0.0f - 1.0f)) / (nearDistance - farDistance);
                light.falloff.x = 1.0f - light.falloff.y / nearDistance;
            }
            else
            {
                light.falloff = glm::vec2(1.0f, 0.0f);
            }
            const float outer = std::cos(glm::radians(source.coneAngle * 0.5f));
            const float inner = std::cos(glm::radians(source.hotSpotAngle * 0.5f));
            light.cone = glm::vec2(outer, std::max(inner, outer + 0.0001f));
            if (source.kind >= 2)
            {
                glm::vec3 forward = light.direction;
                glm::vec3 up = lightRotation * glm::vec3(source.up[0], source.up[1], source.up[2]);
                if (glm::dot(up, up) < 0.000001f || std::abs(glm::dot(glm::normalize(up), forward)) > 0.999f)
                    up = std::abs(forward.z) < 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                const float nearPlane = 50.0f;
                const float farPlane = std::max(source.falloff[1], nearPlane + 0.001f);
                const float fov = glm::radians(std::max(source.coneAngle, 0.01f));
                light.frustum = glm::perspectiveRH_NO(fov, 1.0f, nearPlane, farPlane) *
                                glm::lookAtRH(light.position, light.position + forward, up);
                const auto linearFalloff = [](float minimum, float maximum, float &scale, float &bias)
                {
                    if (std::abs(minimum - maximum) < 0.0001f)
                    {
                        scale = 0.0f;
                        bias = 1.0f;
                    }
                    else
                    {
                        scale = -1.0f / (maximum - minimum);
                        bias = 1.0f - scale * minimum;
                    }
                };
                float distanceScale = 0.0f;
                float distanceBias = 1.0f;
                linearFalloff(source.falloff[0], source.falloff[1], distanceScale, distanceBias);
                const float outerTan = std::max(std::tan(fov * 0.5f), 0.0001f);
                const float innerTan =
                    std::tan(glm::radians(std::clamp(source.hotSpotAngle, 0.0f, source.coneAngle) * 0.5f));
                const float innerOuterRatio = std::clamp(innerTan / outerTan, 0.0f, 1.0f);
                if (source.kind == 2)
                {
                    linearFalloff(innerOuterRatio, 1.0f, light.frustumScale.x, light.frustumBias.x);
                    light.frustumScale.y = light.frustumScale.x;
                    light.frustumBias.y = light.frustumBias.x;
                    light.frustumScale.z = 0.0f;
                    light.frustumBias.z = 1.0f;
                    light.frustumScale.w = distanceScale;
                    light.frustumBias.w = distanceBias;
                }
                else
                {
                    linearFalloff(std::sqrt(innerOuterRatio), 1.0f, light.frustumScale.z, light.frustumBias.z);
                    light.frustumScale.w = distanceScale;
                    light.frustumBias.w = distanceBias;
                }
            }
            lights_.push_back(light);
        }
        if (lights_.empty())
        {
            GpuLight light;
            light.kind = 0;
            light.direction = glm::normalize(glm::vec3(-0.2f, 0.3f, -1.0f));
            light.color = glm::vec3(1.0f, 0.0f, 0.0f);
            angleCoefficients(240.0f, 0.0f, 0.0f, 255.0f, light.ru.y, light.du.y);
            lights_.push_back(light);
        }
        std::vector<GpuLightBlock> lightBlocks;
        lightBlocks.reserve(lights_.size());
        for (std::size_t lightIndex = 0; lightIndex < lights_.size(); ++lightIndex)
        {
            const auto &light = lights_[lightIndex];
            const bool isolated = lightIsolationOwner_ < 0 || (lightIndex < lightOwnerIndices_.size() &&
                                                               lightOwnerIndices_[lightIndex] == lightIsolationOwner_);
            GpuLightBlock block{};
            block.meta =
                glm::ivec4(light.kind, light.excludeDynamicObjects ? 1 : 0, isolated ? 1 : 0, light.dynamic ? 1 : 0);
            block.position = glm::vec4(light.position, 1.0f);
            block.direction = glm::vec4(light.direction, 0.0f);
            block.color = glm::vec4(light.color, 1.0f);
            block.ru = glm::vec4(light.ru, 0.0f);
            block.du = glm::vec4(light.du, 0.0f);
            block.falloffCone = glm::vec4(light.falloff, light.maximumDistance, 0.0f);
            block.frustum = light.frustum;
            block.falloffScale = light.frustumScale;
            block.falloffBias = light.frustumBias;
            lightBlocks.push_back(block);
        }
        UploadShaderStorageRecords(lightBuffer_, lightBlocks, GL_STATIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lightBuffer_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, celEdgeBuffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(celEdges_.size() * sizeof(BrxCelEdge)),
                     celEdges_.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(document.RenderVertices().size() * sizeof(BrxRenderVertex)),
                     document.RenderVertices().data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(document.RenderIndices().size() * sizeof(std::uint32_t)),
                     document.RenderIndices().data(), GL_STATIC_DRAW);
        glBindVertexArray(0);

        textures_.resize(document.Bitmaps().size());
        glGenTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
        for (std::size_t imageIndex = 0; imageIndex < document.Bitmaps().size(); ++imageIndex)
        {
            const auto &image = document.Bitmaps()[imageIndex];
            glBindTexture(GL_TEXTURE_2D, textures_[imageIndex]);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            (image.textureFlags & 1u) != 0 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            (image.textureFlags & 2u) != 0 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                            (image.mipCount > 1 || (image.textureFlags & 4u) != 0) ? GL_LINEAR_MIPMAP_LINEAR
                                                                                   : GL_LINEAR);
            std::size_t requiredBytes = 0;
            std::uint32_t checkedWidth = image.width;
            std::uint32_t checkedHeight = image.height;
            bool validPayload = image.width != 0 && image.height != 0 && image.width <= 32768u &&
                                image.height <= 32768u && image.mipCount != 0 && image.mipCount <= 32 &&
                                image.textureFormat <= 2;
            for (std::uint32_t level = 0; validPayload && level < image.mipCount; ++level)
            {
                const std::size_t levelBytes =
                    image.textureFormat == 0 ? static_cast<std::size_t>(checkedWidth) * checkedHeight * 4
                                             : static_cast<std::size_t>((checkedWidth + 3) / 4) *
                                                   ((checkedHeight + 3) / 4) * (image.textureFormat == 1 ? 8 : 16);
                if (requiredBytes > image.pixels.size() || levelBytes > image.pixels.size() - requiredBytes)
                    validPayload = false;
                else
                    requiredBytes += levelBytes;
                checkedWidth = std::max(1u, checkedWidth >> 1);
                checkedHeight = std::max(1u, checkedHeight >> 1);
            }
            if (!validPayload || requiredBytes != image.pixels.size())
            {
                constexpr std::uint32_t invalidTexture = 0xffff00ffu;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &invalidTexture);
                continue;
            }
            std::size_t offset = 0;
            std::uint32_t width = image.width;
            std::uint32_t height = image.height;
            for (std::uint32_t level = 0; level < image.mipCount; ++level)
            {
                const std::size_t levelBytes = image.textureFormat == 0
                                                   ? static_cast<std::size_t>(width) * height * 4
                                                   : static_cast<std::size_t>((width + 3) / 4) * ((height + 3) / 4) *
                                                         (image.textureFormat == 1 ? 8 : 16);
                if (image.textureFormat == 0)
                    glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA8, width, height, 0, GL_RGBA,
                                 GL_UNSIGNED_BYTE, image.pixels.data() + offset);
                else
                    glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level),
                                           image.textureFormat == 1 ? 0x83F0 : 0x83F3, width, height, 0,
                                           static_cast<GLsizei>(levelBytes), image.pixels.data() + offset);
                offset += levelBytes;
                width = std::max(1u, width >> 1);
                height = std::max(1u, height >> 1);
            }
            if ((image.textureFlags & 4u) != 0 && image.mipCount == 1)
                glGenerateMipmap(GL_TEXTURE_2D);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        BuildFastShadowPreviewCache();
        BuildProjectedShadows();
    }

    void UpdateLight(std::size_t sourceIndex, const BrxLightRecord &source)
    {
        const auto found = std::find(lightSourceIndices_.begin(), lightSourceIndices_.end(), sourceIndex);
        if (found == lightSourceIndices_.end())
            return;
        const std::size_t gpuIndex = static_cast<std::size_t>(found - lightSourceIndices_.begin());
        if (gpuIndex >= lights_.size())
            return;
        const auto fitLinear = [](float x0, float y0, float x1, float y1, float &scale, float &bias)
        {
            if (std::abs(x0 - x1) < 0.0001f)
            {
                scale = 0.0f;
                bias = y0;
            }
            else
            {
                scale = (y1 - y0) / (x1 - x0);
                bias = y0 - scale * x0;
            }
        };
        const auto angleCoefficients =
            [&](float angle0, float strength0, float angle1, float strength1, float &scale, float &bias)
        {
            const auto curve = [](float degrees)
            {
                const float cosine = std::cos(degrees * 0.008726647f);
                return cosine + cosine * cosine * cosine;
            };
            fitLinear(curve(angle0), strength0 / 255.0f, curve(angle1), strength1 / 255.0f, scale, bias);
        };
        const auto hsvColor = [](const float *hsv)
        {
            const float saturation = std::clamp(hsv[1] / 255.0f, 0.0f, 1.0f);
            const float hue = std::fmod(std::max(hsv[0], 0.0f), 360.0f) / 60.0f;
            const int sector = static_cast<int>(std::floor(hue));
            const float fraction = hue - static_cast<float>(sector);
            const float p = 1.0f - saturation, q = 1.0f - saturation * fraction;
            const float t = 1.0f - saturation * (1.0f - fraction);
            switch (sector)
            {
            case 0:
                return glm::vec3(1.0f, t, p);
            case 1:
                return glm::vec3(q, 1.0f, p);
            case 2:
                return glm::vec3(p, 1.0f, t);
            case 3:
                return glm::vec3(p, q, 1.0f);
            case 4:
                return glm::vec3(t, p, 1.0f);
            default:
                return glm::vec3(1.0f, p, q);
            }
        };
        GpuLight light;
        light.kind = source.kind;
        light.dynamic = source.dynamic;
        light.excludeDynamicObjects = source.excludeDynamicObjects;
        light.visibilityPosition = glm::vec3(source.position[0], source.position[1], source.position[2]);
        light.visibilityDirection = glm::vec3(source.direction[0], source.direction[1], source.direction[2]);
        light.visibilityDistance = source.falloff[1];
        light.visibilityConeAngle = source.coneAngle;
        light.position = glm::vec3(source.position[0], source.position[1], source.position[2]);
        light.direction = glm::vec3(source.direction[0], source.direction[1], source.direction[2]);
        if (glm::dot(light.direction, light.direction) > 0.000001f)
            light.direction = glm::normalize(light.direction);
        const glm::mat4 lightDelta = AloxDeltaForObject(source.ownerObjectIndex);
        light.position = glm::vec3(lightDelta * glm::vec4(light.position, 1.0f));
        glm::mat3 lightRotation(lightDelta);
        for (int axis = 0; axis < 3; ++axis)
        {
            const float length = glm::length(lightRotation[axis]);
            if (length > 0.000001f)
                lightRotation[axis] /= length;
        }
        light.direction = glm::normalize(lightRotation * light.direction);
        light.color = source.highlightHsv[2] > 0.0001f ? hsvColor(source.highlightHsv) : glm::vec3(0.0f);
        angleCoefficients(360.0f - source.shadowAngle, 0.0f, 360.0f, source.shadowStrength, light.ru.x, light.du.x);
        angleCoefficients(source.midtoneAngle, 0.0f, 0.0f, source.midtoneStrength, light.ru.y, light.du.y);
        angleCoefficients(source.highlightAngle, 0.0f, 0.0f, source.highlightHsv[2], light.ru.z, light.du.z);
        const float nearDistance = source.falloff[0], farDistance = source.falloff[1];
        light.maximumDistance = (std::max)(farDistance, 0.0f);
        if (std::abs(nearDistance - farDistance) >= 0.0001f && std::abs(nearDistance) >= 0.0001f &&
            std::abs(farDistance) >= 0.0001f)
        {
            light.falloff.y = nearDistance * farDistance * -1.0f / (nearDistance - farDistance);
            light.falloff.x = 1.0f - light.falloff.y / nearDistance;
        }
        else
            light.falloff = glm::vec2(1.0f, 0.0f);
        const float fov = glm::radians(std::max(source.coneAngle, 0.01f));
        if (source.kind >= 2)
        {
            glm::vec3 up = lightRotation * glm::vec3(source.up[0], source.up[1], source.up[2]);
            if (glm::dot(up, up) < 0.000001f || std::abs(glm::dot(glm::normalize(up), light.direction)) > 0.999f)
                up = std::abs(light.direction.z) < 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            const float nearPlane = 50.0f;
            const float farPlane = std::max(source.falloff[1], nearPlane + 0.001f);
            light.frustum = glm::perspectiveRH_NO(fov, 1.0f, nearPlane, farPlane) *
                            glm::lookAtRH(light.position, light.position + light.direction, up);
            const auto linearFalloff = [](float minimum, float maximum, float &scale, float &bias)
            {
                if (std::abs(minimum - maximum) < 0.0001f)
                {
                    scale = 0.0f;
                    bias = 1.0f;
                }
                else
                {
                    scale = -1.0f / (maximum - minimum);
                    bias = 1.0f - scale * minimum;
                }
            };
            const float outerTan = std::max(std::tan(fov * 0.5f), 0.0001f);
            const float innerTan =
                std::tan(glm::radians(std::clamp(source.hotSpotAngle, 0.0f, source.coneAngle) * 0.5f));
            const float ratio = std::clamp(innerTan / outerTan, 0.0f, 1.0f);
            float distanceScale = 0.0f, distanceBias = 1.0f;
            linearFalloff(source.falloff[0], source.falloff[1], distanceScale, distanceBias);
            if (source.kind == 2)
            {
                linearFalloff(ratio, 1.0f, light.frustumScale.x, light.frustumBias.x);
                light.frustumScale.y = light.frustumScale.x;
                light.frustumBias.y = light.frustumBias.x;
                light.frustumScale.z = 0.0f;
                light.frustumBias.z = 1.0f;
            }
            else
                linearFalloff(std::sqrt(ratio), 1.0f, light.frustumScale.z, light.frustumBias.z);
            light.frustumScale.w = distanceScale;
            light.frustumBias.w = distanceBias;
        }
        lights_[gpuIndex] = light;
        std::vector<GpuLightBlock> blocks;
        blocks.reserve(lights_.size());
        for (std::size_t lightIndex = 0; lightIndex < lights_.size(); ++lightIndex)
        {
            const auto &gpuLight = lights_[lightIndex];
            const bool visible =
                lightIndex >= lightOwnerIndices_.size() || !IsGameplayPreviewHidden(lightOwnerIndices_[lightIndex]);
            const bool isolated = lightIsolationOwner_ < 0 || (lightIndex < lightOwnerIndices_.size() &&
                                                               lightOwnerIndices_[lightIndex] == lightIsolationOwner_);
            GpuLightBlock block{};
            block.meta = glm::ivec4(gpuLight.kind, gpuLight.excludeDynamicObjects ? 1 : 0, visible && isolated ? 1 : 0,
                                    gpuLight.dynamic ? 1 : 0);
            block.position = glm::vec4(gpuLight.position, 1.0f);
            block.direction = glm::vec4(gpuLight.direction, 0.0f);
            block.color = glm::vec4(gpuLight.color, 1.0f);
            block.ru = glm::vec4(gpuLight.ru, 0.0f);
            block.du = glm::vec4(gpuLight.du, 0.0f);
            block.falloffCone = glm::vec4(gpuLight.falloff, gpuLight.maximumDistance, 0.0f);
            block.frustum = gpuLight.frustum;
            block.falloffScale = gpuLight.frustumScale;
            block.falloffBias = gpuLight.frustumBias;
            blocks.push_back(block);
        }
        UploadShaderStorageRecords(lightBuffer_, blocks, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lightBuffer_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void UpdateFog(float nearDistance, float farDistance, float maximum)
    {
        fogNear_ = nearDistance;
        fogFar_ = farDistance;
        fogMaximum_ = maximum;
    }

    void UpdateFogColor(const float *color)
    {
        std::copy_n(color, 4, fogColor_);
    }

    void UpdateWorldAppearance(float midtone, float shadow, const float *skyColor)
    {
        worldMidtone_ = midtone;
        worldShadow_ = shadow;
        std::copy_n(skyColor, 4, skyColor_);
    }

    void UpdateProjection(float fieldOfViewRadians, float nearClip, float farClip)
    {
        projectionFov_ = std::clamp(fieldOfViewRadians, 0.01f, 3.13f);
        projectionNear_ = std::max(nearClip, 0.001f);
        projectionFar_ = std::max(farClip, projectionNear_ + 0.001f);
    }

    void UpdateCameraMrdRatio(float ratio)
    {
        // ProjectCane exposes 0.75x..3.0x draw-distance multipliers.  Letting
        // the editor feed arbitrary values (the old UI allowed 100x) expands
        // every finite surface/cel MRD until it appears to render forever.
        cameraMrdRatio_ = std::clamp(ratio, 0.75f, 3.0f);
    }

    void UpdateVisibility(const std::vector<BrxVisibilityNode> &nodes, const std::vector<std::uint32_t> &oneHopMasks)
    {
        visibilityNodes_ = nodes;
        visibilityOneHopMasks_ = oneHopMasks;
    }

    // Editor-only overlays never alter the serialized masks or camera classification.
    static glm::vec4 VisibilityZoneColor(std::uint32_t zone)
    {
        const float hue = std::fmod(static_cast<float>(zone) * 0.61803398875f, 1.0f);
        const glm::vec3 p =
            glm::abs(glm::fract(glm::vec3(hue) + glm::vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f);
        return glm::vec4(glm::mix(glm::vec3(1.0f), glm::clamp(p - 1.0f, 0.0f, 1.0f), 0.65f), 0.65f);
    }
    void SetVisibilityZonePreview(bool colorize, std::uint32_t isolationMask)
    {
        visibilityZoneColors_ = colorize;
        visibilityIsolationMask_ = isolationMask;
    }

    void UpdateObjectZoneMask(const std::int32_t objectIndex, const std::uint32_t zoneMask)
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= objects_.size())
            return;
        objects_[static_cast<std::size_t>(objectIndex)].zoneMask = zoneMask;
        // MS always uses GLOBI masks; ordinary ALOs use object masks only for VISS 2.
        if (objects_[static_cast<std::size_t>(objectIndex)].cid == 4 ||
            objects_[static_cast<std::size_t>(objectIndex)].visibilityMode != 2u)
            return;
        for (auto &batch : batches_)
            if (batch.ownerObjectIndex == objectIndex)
                batch.zoneMask = zoneMask;
        for (auto &cel : celBatches_)
            if (cel.ownerObjectIndex == objectIndex)
                cel.zoneMask = zoneMask;
    }

    void UpdateAloRenderSettings(const std::int32_t objectIndex, const BrxObjectRecord &source)
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= objects_.size())
            return;
        objects_[static_cast<std::size_t>(objectIndex)] = source;
        // Do not copy the whole-ALO cel MRD into each GLOB packet.  ProjectCane
        // keeps ALO::sCelBorderMRD and GLOB::sCelBorderMRD independent: MRDS_All
        // consumes the former in RenderAloAll, while MRDS_Glob consumes the
        // latter in RenderAloGlobset.  Overwriting the packet value here turned
        // finite authored GLOB cutoffs into FLT_MAX after any inspector edit
        // (most visibly on Paris SO OID 1895).
    }

    void ReloadDerivedRenderPackets(const BrxDocument &document)
    {
        ClearModelThumbnailCache();
        std::vector<float> retainedBoneWeights;
        if (boneWeightPreviewRenderGroup_ != kNoSelection && vertices_.size() == document.RenderVertices().size())
        {
            retainedBoneWeights.reserve(vertices_.size());
            for (const auto &vertex : vertices_)
                retainedBoneWeights.push_back(vertex.boneWeightPreview);
        }
        batches_ = document.RenderBatches();
        celBatches_ = document.CelBatches();
        RebuildSurfaceBatchIndex();
        // Derived render packets and their owning ALO state are one snapshot.
        // Reloading only the packets leaves MRDS/object MRD/cel MRD and parent
        // inheritance stale after inspector, proxy, or instance edits.
        objects_ = document.Objects();
        RebuildConveyorPrimaryScrollers(document);
        BuildAloxResolvedTransforms();
        shaders_ = document.Shaders();
        RebuildEyesAnimationIndex();
        globWarps_ = document.GlobWarps();
        vertices_ = document.RenderVertices();
        baseVertices_ = vertices_;
        if (retainedBoneWeights.size() == vertices_.size())
            for (std::size_t vertexIndex = 0; vertexIndex < vertices_.size(); ++vertexIndex)
            {
                vertices_[vertexIndex].boneWeightPreview = retainedBoneWeights[vertexIndex];
                baseVertices_[vertexIndex].boneWeightPreview = retainedBoneWeights[vertexIndex];
            }
        celEdges_ = document.CelEdges();
        baseCelEdges_ = celEdges_;
        asegPreviewVertices_.clear();
        asegPreviewCelEdges_.clear();
        asegPreviewGeometryActive_ = false;
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(BrxRenderVertex)),
                     vertices_.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, celEdgeBuffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(celEdges_.size() * sizeof(BrxCelEdge)),
                     celEdges_.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        BuildWrControllers();
    }

    bool SetBoneWeightPreview(const BrxDocument &document, const std::size_t globIndex, const std::size_t subglobIndex,
                              const int boneIndex, const bool enabled)
    {
        for (std::size_t vertexIndex = 0; vertexIndex < baseVertices_.size(); ++vertexIndex)
        {
            baseVertices_[vertexIndex].boneWeightPreview = 0.0f;
            vertices_[vertexIndex].boneWeightPreview = 0.0f;
        }
        boneWeightPreviewRenderGroup_ = kNoSelection;
        boneWeightPreviewFirstIndex_ = kNoSelection;
        if (enabled)
        {
            if (globIndex >= document.Globs().size())
                return false;
            const auto &glob = document.Globs()[globIndex];
            if (subglobIndex >= glob.sourceSubglobs.size() || subglobIndex >= glob.derivedBatches.size())
                return false;
            const auto &source = glob.sourceSubglobs[subglobIndex];
            const auto &batch = glob.derivedBatches[subglobIndex];
            for (std::size_t triangleIndex = 0; triangleIndex < source.triangles.size(); ++triangleIndex)
            {
                const std::size_t indexOffset = static_cast<std::size_t>(batch.firstIndex) + triangleIndex * 3;
                if (indexOffset + 2 >= indices_.size())
                    return false;
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const std::uint16_t sourceVertexIndex = source.triangles[triangleIndex][corner];
                    const std::uint32_t renderVertexIndex = indices_[indexOffset + corner];
                    if (sourceVertexIndex >= source.vertices.size() || renderVertexIndex >= vertices_.size())
                        return false;
                    float weight = 0.0f;
                    const auto &sourceVertex = source.vertices[sourceVertexIndex];
                    for (std::size_t influence = 0; influence < 4; ++influence)
                        if (sourceVertex.boneIndices[influence] == boneIndex)
                            weight += sourceVertex.boneWeights[influence];
                    weight = std::clamp(weight, 0.0f, 1.0f);
                    baseVertices_[renderVertexIndex].boneWeightPreview = weight;
                    vertices_[renderVertexIndex].boneWeightPreview = weight;
                }
            }
            boneWeightPreviewRenderGroup_ = glob.renderGroup;
            boneWeightPreviewFirstIndex_ = batch.firstIndex;
        }
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(BrxRenderVertex)),
                     vertices_.data(), GL_DYNAMIC_DRAW);
        return true;
    }

    void UpdateAloCelColor(const std::int32_t objectIndex, const float *color, const bool recursive)
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= objects_.size())
            return;
        const auto isAffected = [&](std::int32_t candidate)
        {
            // Geometry can be owned by a visual child ALO while viewport
            // selection resolves to its concrete SO-derived gameplay owner.
            // Treat both identities as the same RenderAloAll submission.
            if (candidate == objectIndex || ResolveLogicalOwner(candidate) == objectIndex)
                return true;
            if (!recursive)
                return false;
            std::size_t guard = 0;
            while (candidate >= 0 && static_cast<std::size_t>(candidate) < objects_.size() &&
                   guard++ <= objects_.size())
            {
                candidate = objects_[static_cast<std::size_t>(candidate)].parentIndex;
                if (candidate == objectIndex || ResolveLogicalOwner(candidate) == objectIndex)
                    return true;
            }
            return false;
        };
        for (auto &cel : celBatches_)
        {
            if (!isAffected(cel.ownerObjectIndex))
                continue;
            for (int component = 0; component < 4; ++component)
                cel.color[component] = std::clamp(color[component], 0.0f, 1.0f);
        }
    }

    [[nodiscard]] glm::mat4 ProjectionMatrix(const float aspect) const
    {
        return glm::perspective(projectionFov_, std::max(aspect, 0.001f), projectionNear_, projectionFar_);
    }

    [[nodiscard]] float ProjectionFov() const
    {
        return projectionFov_;
    }
    [[nodiscard]] float ProjectionNear() const
    {
        return projectionNear_;
    }
    [[nodiscard]] float ProjectionFar() const
    {
        return projectionFar_;
    }

    [[nodiscard]] glm::mat4 ResolvedObjectTransform(const std::int32_t objectIndex) const
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= aloxResolvedWorld_.size())
            return glm::mat4(1.0f);
        glm::mat4 result = aloxResolvedWorld_[static_cast<std::size_t>(objectIndex)];
        if (IsInPreviewHierarchy(objectIndex))
            result = previewObjectDelta_ * result;
        return result;
    }

    [[nodiscard]] glm::mat4 ResolvedAuthoredObjectTransform(const std::int32_t objectIndex) const
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= aloxResolvedWorld_.size())
            return glm::mat4(1.0f);
        return aloxResolvedWorld_[static_cast<std::size_t>(objectIndex)];
    }

    // World transform exactly as it is presented by the viewport. ASEG and
    // runtime motion are submission-time deltas, so callers that simulate a
    // camera must opt into them instead of reading the authored placement.
    [[nodiscard]] glm::mat4 PreviewedObjectTransform(const std::int32_t objectIndex) const
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= aloxResolvedWorld_.size())
            return glm::mat4(1.0f);
        glm::mat4 result = aloxResolvedWorld_[static_cast<std::size_t>(objectIndex)];
        if (IsInPreviewHierarchy(objectIndex))
            result = previewObjectDelta_ * result;
        else if (asegPreviewActive_ && static_cast<std::size_t>(objectIndex) < asegPreviewDeltas_.size())
            result = asegPreviewDeltas_[static_cast<std::size_t>(objectIndex)] * result;
        else if (static_cast<std::size_t>(objectIndex) < runtimeMotionDeltas_.size())
            result = runtimeMotionDeltas_[static_cast<std::size_t>(objectIndex)] * result;
        return result;
    }

    [[nodiscard]] glm::mat4 ResolvedObjectDelta(const std::int32_t objectIndex) const
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= aloxResolvedDeltas_.size())
            return glm::mat4(1.0f);
        glm::mat4 result = aloxResolvedDeltas_[static_cast<std::size_t>(objectIndex)];
        if (IsInPreviewHierarchy(objectIndex))
            result = previewObjectDelta_ * result;
        else if (asegPreviewActive_ && static_cast<std::size_t>(objectIndex) < asegPreviewDeltas_.size())
            result = asegPreviewDeltas_[static_cast<std::size_t>(objectIndex)] * result;
        else if (static_cast<std::size_t>(objectIndex) < runtimeMotionDeltas_.size())
            result = runtimeMotionDeltas_[static_cast<std::size_t>(objectIndex)] * result;
        return result;
    }

    void SetObjectHidden(const std::int32_t objectIndex, const bool hidden)
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= objects_.size())
            return;
        objects_[static_cast<std::size_t>(objectIndex)].hidden = hidden;
    }

    [[nodiscard]] std::int32_t LightIsolationOwner() const
    {
        return lightIsolationOwner_;
    }

    void SetLightIsolationOwner(const std::int32_t objectIndex)
    {
        const std::int32_t resolvedObjectIndex =
            objectIndex >= 0 && std::find(lightOwnerIndices_.begin(), lightOwnerIndices_.end(), objectIndex) ==
                                    lightOwnerIndices_.end()
                ? -1
                : objectIndex;
        if (lightIsolationOwner_ == resolvedObjectIndex)
            return;
        lightIsolationOwner_ = resolvedObjectIndex;
        std::vector<GpuLightBlock> blocks;
        blocks.reserve(lights_.size());
        for (std::size_t index = 0; index < lights_.size(); ++index)
        {
            const auto &light = lights_[index];
            const bool visible =
                index >= lightOwnerIndices_.size() || !IsGameplayPreviewHidden(lightOwnerIndices_[index]);
            const bool isolated = lightIsolationOwner_ < 0 || (index < lightOwnerIndices_.size() &&
                                                               lightOwnerIndices_[index] == lightIsolationOwner_);
            GpuLightBlock block{};
            block.meta = glm::ivec4(light.kind, light.excludeDynamicObjects ? 1 : 0, visible && isolated ? 1 : 0,
                                    light.dynamic ? 1 : 0);
            block.position = glm::vec4(light.position, 1.0f);
            block.direction = glm::vec4(light.direction, 0.0f);
            block.color = glm::vec4(light.color, 1.0f);
            block.ru = glm::vec4(light.ru, 0.0f);
            block.du = glm::vec4(light.du, 0.0f);
            block.falloffCone = glm::vec4(light.falloff, light.maximumDistance, 0.0f);
            block.frustum = light.frustum;
            block.falloffScale = light.frustumScale;
            block.falloffBias = light.frustumBias;
            blocks.push_back(block);
        }
        UploadShaderStorageRecords(lightBuffer_, blocks, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lightBuffer_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void SetGameplayPreviewHiddenObjects(const std::unordered_set<std::int32_t> &hidden)
    {
        static const std::unordered_set<std::int32_t> none;
        SetGameplayPreviewHiddenObjects(hidden, none);
    }

    void SetGameplayPreviewHiddenObjects(const std::unordered_set<std::int32_t> &hidden,
                                         const std::unordered_set<std::int32_t> &ownHidden)
    {
        if (hidden.size() == gameplayPreviewHiddenObjects_.size() &&
            std::all_of(hidden.begin(), hidden.end(), [&](const std::int32_t objectIndex)
                        { return gameplayPreviewHiddenObjects_.contains(objectIndex); }) &&
            ownHidden.size() == gameplayPreviewOwnHiddenObjects_.size() &&
            std::all_of(ownHidden.begin(), ownHidden.end(), [&](const std::int32_t objectIndex)
                        { return gameplayPreviewOwnHiddenObjects_.contains(objectIndex); }))
            return;
        gameplayPreviewHiddenObjects_ = hidden;
        gameplayPreviewOwnHiddenObjects_ = ownHidden;
        std::vector<GpuLightBlock> blocks;
        blocks.reserve(lights_.size());
        for (std::size_t index = 0; index < lights_.size(); ++index)
        {
            const auto &light = lights_[index];
            const bool visible =
                index >= lightOwnerIndices_.size() || !IsGameplayPreviewHidden(lightOwnerIndices_[index]);
            const bool isolated = lightIsolationOwner_ < 0 || (index < lightOwnerIndices_.size() &&
                                                               lightOwnerIndices_[index] == lightIsolationOwner_);
            GpuLightBlock block{};
            block.meta = glm::ivec4(light.kind, light.excludeDynamicObjects ? 1 : 0, visible && isolated ? 1 : 0,
                                    light.dynamic ? 1 : 0);
            block.position = glm::vec4(light.position, 1.0f);
            block.direction = glm::vec4(light.direction, 0.0f);
            block.color = glm::vec4(light.color, 1.0f);
            block.ru = glm::vec4(light.ru, 0.0f);
            block.du = glm::vec4(light.du, 0.0f);
            block.falloffCone = glm::vec4(light.falloff, light.maximumDistance, 0.0f);
            block.frustum = light.frustum;
            block.falloffScale = light.frustumScale;
            block.falloffBias = light.frustumBias;
            blocks.push_back(block);
        }
        UploadShaderStorageRecords(lightBuffer_, blocks, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lightBuffer_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void Clear()
    {
        emitterPreview_.Stop();
        indexCount_ = 0;
        batches_.clear();
        surfaceByOwnerGroup_.clear();
        celBatches_.clear();
        celEdges_.clear();
        vertices_.clear();
        indices_.clear();
        shaders_.clear();
        eyesAnimationBySecondaryShaderOid_.clear();
        lights_.clear();
        lightSourceIndices_.clear();
        lightOwnerIndices_.clear();
        lightIsolationOwner_ = -1;
        gameplayPreviewHiddenObjects_.clear();
        gameplayPreviewOwnHiddenObjects_.clear();
        visibilityNodes_.clear();
        visibilityOneHopMasks_.clear();
        objects_.clear();
        aloxResolvedWorld_.clear();
        aloxResolvedDeltas_.clear();
        runtimeMotionDeltas_.clear();
        asegPreviewDeltas_.clear();
        asegPreviewActive_ = false;
        runtimeLocalVelocities_.clear();
        runtimeLocalAngularVelocities_.clear();
        logicalOwnerByObject_.clear();
        visibleBatchCount_ = 0;
        culledBatchCount_ = 0;
        cameraZoneMask_ = 0x0fffffffu;
        selectedRenderGroup_ = kNoSelection;
        selectedSubglobFirstIndex_ = kNoSelection;
        selectionScope_ = SelectionScope::None;
        selectedObjectIndices_.clear();
        selectedRenderGroups_.clear();
        selectedSubglobs_.clear();
        hiddenObjectSelections_.clear();
        hiddenGlobSelections_.clear();
        hiddenSubglobSelections_.clear();
        lockedObjectSelections_.clear();
        lockedGlobSelections_.clear();
        lockedSubglobSelections_.clear();
        isolationScope_ = SelectionScope::None;
        isolationUsesSelection_ = false;
        ClearSelection();
        ReleaseSceneTextures();
    }

    void ClearSelection()
    {
        selectedRenderGroup_ = kNoSelection;
        selectedSubglobFirstIndex_ = kNoSelection;
        selectionScope_ = SelectionScope::None;
        selectedObjectIndex_ = -1;
        selectedVisualObjectIndex_ = -1;
        selectedObjectIndices_.clear();
        selectedRenderGroups_.clear();
        selectedSubglobs_.clear();
        pickCycle_.clear();
        pickCycleIndex_ = 0;
    }
    void SelectRenderGroup(std::uint32_t group)
    {
        pickCycle_.clear();
        pickCycleIndex_ = 0;
        selectedRenderGroup_ = group;
        selectedSubglobFirstIndex_ = kNoSelection;
        selectionScope_ = SelectionScope::Glob;
        const BrxRenderBatch *batch = SelectedBatch();
        selectedVisualObjectIndex_ = batch ? batch->ownerObjectIndex : -1;
        selectedObjectIndex_ = selectedVisualObjectIndex_;
        selectedObjectIndices_.clear();
        selectedRenderGroups_.clear();
        selectedSubglobs_.clear();
        selectedRenderGroups_.insert(group);
    }
    void SelectObject(std::int32_t objectIndex)
    {
        pickCycle_.clear();
        pickCycleIndex_ = 0;
        selectedObjectIndex_ = objectIndex;
        selectedSubglobFirstIndex_ = kNoSelection;
        selectionScope_ = SelectionScope::Object;
        selectedVisualObjectIndex_ = objectIndex;
        selectedRenderGroup_ = kNoSelection;
        const auto found = std::find_if(batches_.begin(), batches_.end(), [&](const BrxRenderBatch &batch)
                                        { return batch.ownerObjectIndex == objectIndex; });
        if (found != batches_.end())
            selectedRenderGroup_ = found->renderGroup;
        if (found != batches_.end())
            selectedVisualObjectIndex_ = found->ownerObjectIndex;
        selectedObjectIndices_.clear();
        selectedRenderGroups_.clear();
        selectedSubglobs_.clear();
        if (objectIndex >= 0)
            selectedObjectIndices_.insert(objectIndex);
    }
    void SelectSubglob(const std::uint32_t renderGroup, const std::uint32_t firstIndex)
    {
        pickCycle_.clear();
        pickCycleIndex_ = 0;
        selectedRenderGroup_ = renderGroup;
        selectedSubglobFirstIndex_ = firstIndex;
        selectionScope_ = SelectionScope::Subglob;
        const auto found = std::find_if(batches_.begin(), batches_.end(), [&](const BrxRenderBatch &batch)
                                        { return batch.renderGroup == renderGroup && batch.firstIndex == firstIndex; });
        selectedVisualObjectIndex_ = found == batches_.end() ? -1 : found->ownerObjectIndex;
        selectedObjectIndex_ = selectedVisualObjectIndex_;
        selectedObjectIndices_.clear();
        selectedRenderGroups_.clear();
        selectedSubglobs_.clear();
        selectedSubglobs_.insert((static_cast<std::uint64_t>(renderGroup) << 32) | firstIndex);
    }
    void SelectRenderGroupPrimary(const std::uint32_t renderGroup)
    {
        selectedRenderGroup_ = renderGroup;
        selectedSubglobFirstIndex_ = kNoSelection;
        selectionScope_ = SelectionScope::Glob;
        const BrxRenderBatch *batch = SelectedBatch();
        selectedVisualObjectIndex_ = batch ? batch->ownerObjectIndex : -1;
        selectedObjectIndex_ = selectedVisualObjectIndex_;
    }
    void SelectSubglobPrimary(const std::uint32_t renderGroup, const std::uint32_t firstIndex)
    {
        selectedRenderGroup_ = renderGroup;
        selectedSubglobFirstIndex_ = firstIndex;
        selectionScope_ = SelectionScope::Subglob;
        const auto found = std::find_if(batches_.begin(), batches_.end(), [&](const BrxRenderBatch &batch)
                                        { return batch.renderGroup == renderGroup && batch.firstIndex == firstIndex; });
        selectedVisualObjectIndex_ = found == batches_.end() ? -1 : found->ownerObjectIndex;
        selectedObjectIndex_ = selectedVisualObjectIndex_;
    }
    void ToggleObjectSelection(const std::int32_t objectIndex)
    {
        if (selectionScope_ != SelectionScope::Object)
        {
            SelectObject(objectIndex);
            return;
        }
        if (!selectedObjectIndices_.erase(objectIndex))
            selectedObjectIndices_.insert(objectIndex);
        if (selectedObjectIndices_.empty())
        {
            ClearSelection();
            return;
        }
        selectedObjectIndex_ = *selectedObjectIndices_.begin();
        selectedVisualObjectIndex_ = selectedObjectIndex_;
    }
    void SelectObjects(const std::vector<std::int32_t> &objectIndices, const std::int32_t primaryObjectIndex)
    {
        pickCycle_.clear();
        pickCycleIndex_ = 0;
        selectedObjectIndices_.clear();
        selectedRenderGroups_.clear();
        selectedSubglobs_.clear();
        for (const std::int32_t objectIndex : objectIndices)
            if (objectIndex >= 0)
                selectedObjectIndices_.insert(objectIndex);
        if (primaryObjectIndex >= 0)
            selectedObjectIndices_.insert(primaryObjectIndex);
        if (selectedObjectIndices_.empty())
        {
            ClearSelection();
            return;
        }
        selectionScope_ = SelectionScope::Object;
        selectedObjectIndex_ =
            selectedObjectIndices_.contains(primaryObjectIndex) ? primaryObjectIndex : *selectedObjectIndices_.begin();
        selectedVisualObjectIndex_ = selectedObjectIndex_;
        selectedRenderGroup_ = kNoSelection;
        selectedSubglobFirstIndex_ = kNoSelection;
    }
    void SelectObjectRange(const std::int32_t first, const std::int32_t last)
    {
        selectedObjectIndices_.clear();
        selectedRenderGroups_.clear();
        selectedSubglobs_.clear();
        const std::int32_t begin = (std::min)(first, last);
        const std::int32_t end = (std::max)(first, last);
        for (std::int32_t index = begin; index <= end; ++index)
            selectedObjectIndices_.insert(index);
        selectionScope_ = SelectionScope::Object;
        selectedObjectIndex_ = last;
        selectedVisualObjectIndex_ = last;
        selectedRenderGroup_ = kNoSelection;
        selectedSubglobFirstIndex_ = kNoSelection;
    }
    void ToggleRenderGroupSelection(const std::uint32_t renderGroup)
    {
        if (selectionScope_ != SelectionScope::Glob)
        {
            SelectRenderGroup(renderGroup);
            return;
        }
        if (!selectedRenderGroups_.erase(renderGroup))
        {
            selectedRenderGroups_.insert(renderGroup);
            SelectRenderGroupPrimary(renderGroup);
            return;
        }
        if (selectedRenderGroups_.empty())
        {
            ClearSelection();
            return;
        }
        SelectRenderGroupPrimary(*selectedRenderGroups_.begin());
    }
    void ToggleSubglobSelection(const std::uint32_t renderGroup, const std::uint32_t firstIndex)
    {
        if (selectionScope_ != SelectionScope::Subglob)
        {
            SelectSubglob(renderGroup, firstIndex);
            return;
        }
        const std::uint64_t key = (static_cast<std::uint64_t>(renderGroup) << 32) | firstIndex;
        if (!selectedSubglobs_.erase(key))
            selectedSubglobs_.insert(key);
        if (selectedSubglobs_.empty())
        {
            ClearSelection();
            return;
        }
        const std::uint64_t primary = *selectedSubglobs_.begin();
        SelectSubglobPrimary(static_cast<std::uint32_t>(primary >> 32), static_cast<std::uint32_t>(primary));
    }
    [[nodiscard]] bool IsObjectSelected(const std::int32_t objectIndex) const
    {
        return selectionScope_ == SelectionScope::Object && selectedObjectIndices_.contains(objectIndex);
    }
    [[nodiscard]] std::size_t SelectionCount() const
    {
        if (selectionScope_ == SelectionScope::Object)
            return selectedObjectIndices_.size();
        if (selectionScope_ == SelectionScope::Glob)
            return selectedRenderGroups_.size();
        if (selectionScope_ == SelectionScope::Subglob)
            return selectedSubglobs_.size();
        return 0;
    }
    [[nodiscard]] const std::unordered_set<std::int32_t> &SelectedObjectIndices() const
    {
        return selectedObjectIndices_;
    }
    [[nodiscard]] bool HasSelection() const
    {
        return selectedObjectIndex_ >= 0 || selectedRenderGroup_ != kNoSelection;
    }
    [[nodiscard]] std::int32_t SelectedObjectIndex() const
    {
        return selectedObjectIndex_;
    }
    [[nodiscard]] std::int32_t SelectedVisualObjectIndex() const
    {
        return selectedVisualObjectIndex_;
    }
    [[nodiscard]] std::int32_t ResolveLogicalOwner(const std::int32_t visualObjectIndex) const
    {
        if (visualObjectIndex < 0 || static_cast<std::size_t>(visualObjectIndex) >= logicalOwnerByObject_.size())
            return visualObjectIndex;
        return logicalOwnerByObject_[static_cast<std::size_t>(visualObjectIndex)];
    }
    [[nodiscard]] std::size_t PickCyclePosition() const
    {
        return pickCycle_.empty() ? 0 : pickCycleIndex_ + 1;
    }
    [[nodiscard]] std::size_t PickCycleCount() const
    {
        return pickCycle_.size();
    }
    [[nodiscard]] bool LastPickWasEmitterParticle() const
    {
        return lastPickWasEmitterParticle_;
    }
    void SetEditorReferenceLinks(std::vector<std::array<glm::vec3, 2>> links)
    {
        editorReferenceLinks_ = std::move(links);
    }
    [[nodiscard]] std::uint32_t SelectedRenderGroup() const
    {
        return selectedRenderGroup_;
    }
    [[nodiscard]] bool HasGlobSelection() const
    {
        return selectionScope_ == SelectionScope::Glob || selectionScope_ == SelectionScope::Subglob;
    }
    [[nodiscard]] bool HasSubglobSelection() const
    {
        return selectionScope_ == SelectionScope::Subglob;
    }
    [[nodiscard]] std::uint32_t SelectedSubglobFirstIndex() const
    {
        return selectedSubglobFirstIndex_;
    }
    [[nodiscard]] GLuint TextureHandle(const std::int32_t bitmapIndex) const
    {
        return bitmapIndex >= 0 && static_cast<std::size_t>(bitmapIndex) < textures_.size()
                   ? textures_[static_cast<std::size_t>(bitmapIndex)]
                   : 0;
    }
    void BeginAssetThumbnailFrame()
    {
        ++thumbnailFrame_;
        thumbnailBudgetRemaining_ = 2;
    }
    void ClearModelThumbnailCache()
    {
        for (const auto &entry : modelThumbnails_)
            if (entry.second.texture != 0)
                glDeleteTextures(1, &entry.second.texture);
        modelThumbnails_.clear();
    }
    [[nodiscard]] GLuint ModelThumbnail(const BrxDocument &document, const std::size_t globsetIndex,
                                        const std::uint64_t stableId, const int requestedSize = 160)
    {
        auto cached = modelThumbnails_.find(stableId);
        if (cached != modelThumbnails_.end())
        {
            cached->second.lastUsedFrame = thumbnailFrame_;
            return cached->second.texture;
        }
        if (thumbnailBudgetRemaining_ <= 0 || thumbnailProgram_ == 0 || globsetIndex >= document.Globsets().size())
            return 0;
        struct DrawItem
        {
            const BrxRenderBatch *batch = nullptr;
            glm::mat4 model{1.0f};
        };
        std::vector<DrawItem> items;
        glm::vec3 lower((std::numeric_limits<float>::max)());
        glm::vec3 upper((std::numeric_limits<float>::lowest)());
        for (const auto globIndex : document.Globsets()[globsetIndex].globIndices)
        {
            if (globIndex >= document.Globs().size())
                continue;
            for (const auto &batch : document.Globs()[globIndex].derivedBatches)
            {
                if (batch.indexCount == 0 || batch.firstIndex >= indices_.size())
                    continue;
                const glm::mat4 model = SubmissionTransformForObject(batch.ownerObjectIndex);
                const glm::vec3 center = glm::vec3(
                    model * glm::vec4(batch.boundsCenter[0], batch.boundsCenter[1], batch.boundsCenter[2], 1.0f));
                const float scale = (std::max)({glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                                                glm::length(glm::vec3(model[2]))});
                const glm::vec3 extent((std::max)(batch.boundsRadius * scale, 0.001f));
                lower = glm::min(lower, center - extent);
                upper = glm::max(upper, center + extent);
                items.push_back({&batch, model});
            }
        }
        if (items.empty())
            return 0;
        const glm::vec3 center = (lower + upper) * 0.5f;
        const float radius = (std::max)(glm::length(upper - center), 0.01f);
        const int size = std::clamp(requestedSize, 64, 256);
        if (modelThumbnails_.size() >= 256u)
        {
            const auto oldest =
                std::min_element(modelThumbnails_.begin(), modelThumbnails_.end(), [](const auto &a, const auto &b)
                                 { return a.second.lastUsedFrame < b.second.lastUsedFrame; });
            if (oldest != modelThumbnails_.end())
            {
                glDeleteTextures(1, &oldest->second.texture);
                modelThumbnails_.erase(oldest);
            }
        }
        ModelThumbnailRecord thumbnail;
        thumbnail.lastUsedFrame = thumbnailFrame_;
        glGenTextures(1, &thumbnail.texture);
        glBindTexture(GL_TEXTURE_2D, thumbnail.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindRenderbuffer(GL_RENDERBUFFER, thumbnailDepthBuffer_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, size, size);
        GLint oldFramebuffer = 0, oldProgram = 0, oldVao = 0, oldViewport[4]{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldFramebuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
        glGetIntegerv(GL_VIEWPORT, oldViewport);
        const GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST), hadCull = glIsEnabled(GL_CULL_FACE),
                        hadBlend = glIsEnabled(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, thumbnailFramebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, thumbnail.texture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, thumbnailDepthBuffer_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, oldFramebuffer);
            glDeleteTextures(1, &thumbnail.texture);
            return 0;
        }
        glViewport(0, 0, size, size);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.035f, 0.045f, 0.060f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const glm::vec3 direction = glm::normalize(glm::vec3(1.15f, -1.35f, 0.85f));
        const glm::mat4 view = glm::lookAt(center + direction * radius * 2.75f, center, glm::vec3(0, 0, 1));
        const glm::mat4 projection =
            glm::perspective(glm::radians(38.0f), 1.0f, (std::max)(radius * 0.01f, 0.01f), radius * 8.0f);
        const glm::mat4 viewProjection = projection * view;
        glUseProgram(thumbnailProgram_);
        glBindVertexArray(vao_);
        glUniformMatrix4fv(glGetUniformLocation(thumbnailProgram_, "uViewProjection"), 1, GL_FALSE,
                           glm::value_ptr(viewProjection));
        glUniform1i(glGetUniformLocation(thumbnailProgram_, "uDiffuse"), 0);
        const GLint modelLocation = glGetUniformLocation(thumbnailProgram_, "uModel");
        const GLint tintLocation = glGetUniformLocation(thumbnailProgram_, "uTint");
        const GLint hasDiffuseLocation = glGetUniformLocation(thumbnailProgram_, "uHasDiffuse");
        for (const auto &item : items)
        {
            const auto &batch = *item.batch;
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(item.model));
            glm::vec4 tint(1.0f);
            GLuint texture = 0;
            if (batch.shaderIndex < shaders_.size())
            {
                const auto &shader = shaders_[batch.shaderIndex];
                tint = glm::make_vec4(shader.rgba);
                const auto bitmap =
                    std::find_if(shader.diffuseBitmapIndices.begin(), shader.diffuseBitmapIndices.end(),
                                 [&](const std::int32_t index)
                                 { return index >= 0 && static_cast<std::size_t>(index) < textures_.size(); });
                if (bitmap != shader.diffuseBitmapIndices.end())
                    texture = textures_[static_cast<std::size_t>(*bitmap)];
            }
            glUniform4fv(tintLocation, 1, glm::value_ptr(tint));
            glUniform1i(hasDiffuseLocation, texture != 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glDrawElements(
                GL_TRIANGLES, static_cast<GLsizei>(batch.indexCount), GL_UNSIGNED_INT,
                reinterpret_cast<const void *>(static_cast<std::uintptr_t>(batch.firstIndex) * sizeof(std::uint32_t)));
        }
        glBindVertexArray(oldVao);
        glUseProgram(oldProgram);
        glBindFramebuffer(GL_FRAMEBUFFER, oldFramebuffer);
        glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
        if (!hadDepth)
            glDisable(GL_DEPTH_TEST);
        if (!hadCull)
            glDisable(GL_CULL_FACE);
        if (!hadBlend)
            glDisable(GL_BLEND);
        --thumbnailBudgetRemaining_;
        const GLuint result = thumbnail.texture;
        modelThumbnails_.emplace(stableId, thumbnail);
        return result;
    }
    EmitterPreviewRuntime &EmitterPreview()
    {
        return emitterPreview_;
    }
    [[nodiscard]] const EmitterRenderDiagnostics &EmitterDiagnostics() const
    {
        return emitterRenderDiagnostics_;
    }
    [[nodiscard]] const PerformanceDiagnostics &Performance() const
    {
        return performanceDiagnostics_;
    }
    void ResolveGpuPerformance()
    {
        // Never ask for a result until the driver says it is ready. Reading a
        // current-frame GL_TIME_ELAPSED result serializes the CPU and GPU and
        // was the source of the old 70-90 ms diagnostic stall. A small query
        // ring gives us an honest delayed measurement without blocking.
        for (std::size_t queryIndex = 0; queryIndex < gpuTimerQueries_.size(); ++queryIndex)
        {
            if (!gpuTimerPending_[queryIndex])
                continue;
            GLint available = GL_FALSE;
            glGetQueryObjectiv(gpuTimerQueries_[queryIndex], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_FALSE)
                continue;
            GLuint64 elapsedNanoseconds = 0;
            glGetQueryObjectui64v(gpuTimerQueries_[queryIndex], GL_QUERY_RESULT, &elapsedNanoseconds);
            performanceDiagnostics_.gpuRenderMs = static_cast<double>(elapsedNanoseconds) / 1000000.0;
            gpuTimerPending_[queryIndex] = false;
        }
    }
    void UpdateEmitterPreview(const BrxDocument &document, const std::int32_t objectIndex, const float deltaSeconds,
                              const glm::mat4 &cameraWorld)
    {
        std::vector<std::int32_t> emitters;
        auto scope = emitterPreview_.Scope();
        // "Play selected" is a temporary audition mode. Once selection moves
        // away from an EMITTER, return to the frozen visible-world preview;
        // otherwise the stale Selected scope silently removes every ambient
        // fire, smoke, drip, and cloned particle system from the viewport.
        if (scope == EmitterPreviewScope::Selected &&
            (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= document.Objects().size() ||
             !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(objectIndex)].cid, 80)))
        {
            // Selected playback is an audition owned by one authored object.
            // Leaving that object must stop the transport before returning to
            // the visible-world snapshot; otherwise the still-true global
            // playing flag advances every emitter on the following frame.
            emitterPreview_.Stop();
            emitterPreview_.SetSelectedEmitter(-1);
            emitterPreview_.SetScope(EmitterPreviewScope::Visible);
            scope = EmitterPreviewScope::Visible;
        }
        else if (scope == EmitterPreviewScope::Selected && emitterPreview_.SelectedEmitter() >= 0 &&
                 emitterPreview_.SelectedEmitter() != objectIndex)
        {
            // Changing selection also ends the previous audition. Do not
            // silently start the newly selected emitter with the old one's
            // transport state.
            emitterPreview_.Stop();
            emitterPreview_.SetSelectedEmitter(-1);
            emitterPreview_.SetScope(EmitterPreviewScope::Visible);
            scope = EmitterPreviewScope::Visible;
        }
        // A stopped preview is a frozen authoring snapshot. Once that snapshot
        // is current, do not rescan every object or rebuild three full transform
        // arrays merely to redisplay the same particles.
        if (!emitterPreview_.NeedsUpdate())
            return;
        if (scope == EmitterPreviewScope::Selected)
        {
            if (objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < document.Objects().size() &&
                CidDerivesFrom(document.Objects()[static_cast<std::size_t>(objectIndex)].cid, 80))
            {
                // ProjectCane shares one particle buffer between grouped
                // emitters, but DrawBlipg submits that buffer at every member
                // transform.  A selected member must therefore preview at its
                // own authored placement rather than jump to whichever member
                // supplied the synthetic group's particle template.
                emitters.push_back(objectIndex);
            }
        }
        else
        {
            emitters.reserve(document.Objects().size());
            for (std::size_t index = 0; index < document.Objects().size(); ++index)
            {
                const auto &object = document.Objects()[index];
                if (!CidDerivesFrom(object.cid, 80))
                    continue;
                // An inline PROXY hierarchy is still the original live placed
                // instance in ProjectCane.  Source-reference PROXY records add
                // clones; they do not make the source hierarchy dormant.
                // Skipping !proxyClone objects suppresses legitimate children
                // such as EMITTER OIDs 1616/1617 on the original sign.
                if (scope == EmitterPreviewScope::Visible &&
                    hiddenObjectSelections_.contains(static_cast<std::int32_t>(index)))
                    continue;
                // A serialized hidden ALO is not a hidden particle system.
                // RenderEmitterSelf is empty and ProjectCane's UpdateEmitter /
                // DrawBlipg path remains active for particle-only emitters.
                // Only an explicit editor Hide action suppresses the preview.
                // ProjectCane's synthetic group owns one BLIP buffer, but
                // DrawBlipg loops over dlGroup and renders that same buffer at
                // every member (including every PROXY instance).  Collapsing
                // the authored records to one representative made all other
                // torches, pipes, signs, and cloned effects disappear.  The
                // editor keeps a safe per-member snapshot so every runtime
                // submission remains visible and independently selectable.
                emitters.push_back(static_cast<std::int32_t>(index));
            }
        }
        std::vector<glm::mat4> resolvedWorld;
        std::vector<glm::vec3> linearVelocity;
        std::vector<glm::vec3> angularVelocity;
        resolvedWorld.reserve(document.Objects().size());
        linearVelocity.reserve(document.Objects().size());
        angularVelocity.reserve(document.Objects().size());
        for (std::size_t index = 0; index < document.Objects().size(); ++index)
        {
            resolvedWorld.push_back(ResolvedObjectTransform(static_cast<std::int32_t>(index)));
            const glm::mat3 basis(resolvedWorld.back());
            linearVelocity.push_back(index < runtimeLocalVelocities_.size() ? basis * runtimeLocalVelocities_[index]
                                                                            : glm::vec3(0.0f));
            angularVelocity.push_back(index < runtimeLocalAngularVelocities_.size()
                                          ? basis * runtimeLocalAngularVelocities_[index]
                                          : glm::vec3(0.0f));
        }
        emitterPreview_.SetSpatialContext(std::move(resolvedWorld), std::move(linearVelocity),
                                          std::move(angularVelocity), cameraWorld);
        emitterPreview_.SetSelectedEmitter(
            scope == EmitterPreviewScope::Selected && !emitters.empty() ? emitters.front() : -1);
        emitterPreview_.Update(document, emitters, deltaSeconds);
    }
    [[nodiscard]] bool IsObjectEditorHidden(const std::int32_t objectIndex) const
    {
        return hiddenObjectSelections_.contains(objectIndex);
    }
    [[nodiscard]] bool IsGlobEditorHidden(const std::uint32_t renderGroup) const
    {
        return hiddenGlobSelections_.contains(renderGroup);
    }
    [[nodiscard]] bool IsSubglobEditorHidden(const std::uint32_t renderGroup, const std::uint32_t firstIndex) const
    {
        return hiddenSubglobSelections_.contains((static_cast<std::uint64_t>(renderGroup) << 32) | firstIndex);
    }
    void SetObjectEditorVisible(const std::int32_t objectIndex, const bool visible)
    {
        if (visible)
            hiddenObjectSelections_.erase(objectIndex);
        else
            hiddenObjectSelections_.insert(objectIndex);
    }
    void SetGlobEditorVisible(const std::uint32_t renderGroup, const bool visible)
    {
        if (visible)
            hiddenGlobSelections_.erase(renderGroup);
        else
            hiddenGlobSelections_.insert(renderGroup);
    }
    void SetSubglobEditorVisible(const std::uint32_t renderGroup, const std::uint32_t firstIndex, const bool visible)
    {
        const std::uint64_t key = (static_cast<std::uint64_t>(renderGroup) << 32) | firstIndex;
        if (visible)
            hiddenSubglobSelections_.erase(key);
        else
            hiddenSubglobSelections_.insert(key);
    }
    [[nodiscard]] bool IsObjectLocked(const std::int32_t objectIndex) const
    {
        return lockedObjectSelections_.contains(objectIndex);
    }
    [[nodiscard]] bool IsGlobLocked(const std::uint32_t renderGroup) const
    {
        return lockedGlobSelections_.contains(renderGroup);
    }
    [[nodiscard]] bool IsSubglobLocked(const std::uint32_t renderGroup, const std::uint32_t firstIndex) const
    {
        return lockedSubglobSelections_.contains((static_cast<std::uint64_t>(renderGroup) << 32) | firstIndex);
    }
    void SetObjectLocked(const std::int32_t objectIndex, const bool locked)
    {
        if (locked)
            lockedObjectSelections_.insert(objectIndex);
        else
            lockedObjectSelections_.erase(objectIndex);
    }
    void SetGlobLocked(const std::uint32_t renderGroup, const bool locked)
    {
        if (locked)
            lockedGlobSelections_.insert(renderGroup);
        else
            lockedGlobSelections_.erase(renderGroup);
    }
    void SetSubglobLocked(const std::uint32_t renderGroup, const std::uint32_t firstIndex, const bool locked)
    {
        const std::uint64_t key = (static_cast<std::uint64_t>(renderGroup) << 32) | firstIndex;
        if (locked)
            lockedSubglobSelections_.insert(key);
        else
            lockedSubglobSelections_.erase(key);
    }
    [[nodiscard]] bool CurrentSelectionLocked() const
    {
        if (selectionScope_ == SelectionScope::Object)
            return IsObjectLocked(selectedObjectIndex_);
        if (selectionScope_ == SelectionScope::Glob)
            return IsObjectLocked(selectedObjectIndex_) || IsGlobLocked(selectedRenderGroup_);
        if (selectionScope_ == SelectionScope::Subglob)
            return IsObjectLocked(selectedObjectIndex_) || IsGlobLocked(selectedRenderGroup_) ||
                   IsSubglobLocked(selectedRenderGroup_, selectedSubglobFirstIndex_);
        return false;
    }
    bool FrameSelection(EditorCamera &camera) const
    {
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        bool found = false;
        for (const auto &batch : batches_)
        {
            const bool included = IsBatchSelected(batch);
            if (!included)
                continue;
            const glm::mat4 placement = SubmissionTransformForObject(batch.ownerObjectIndex);
            const glm::vec3 center = glm::vec3(
                placement * glm::vec4(batch.boundsCenter[0], batch.boundsCenter[1], batch.boundsCenter[2], 1.0f));
            const float transformedRadius = batch.boundsRadius * (std::max)({glm::length(glm::vec3(placement[0])),
                                                                             glm::length(glm::vec3(placement[1])),
                                                                             glm::length(glm::vec3(placement[2]))});
            const glm::vec3 radius(transformedRadius);
            minimum = glm::min(minimum, center - radius);
            maximum = glm::max(maximum, center + radius);
            found = true;
        }
        if (found)
            camera.FrameBounds(minimum, maximum);
        return found;
    }
    bool FrameObject(EditorCamera &camera, const std::int32_t objectIndex) const
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= objects_.size())
            return false;
        const auto isDescendant = [&](std::int32_t candidate)
        {
            for (std::size_t guard = 0;
                 candidate >= 0 && static_cast<std::size_t>(candidate) < objects_.size() && guard++ <= objects_.size();
                 candidate = objects_[static_cast<std::size_t>(candidate)].parentIndex)
                if (candidate == objectIndex)
                    return true;
            return false;
        };
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        bool found = false;
        for (const auto &batch : batches_)
        {
            if (!isDescendant(batch.ownerObjectIndex))
                continue;
            const glm::mat4 placement = SubmissionTransformForObject(batch.ownerObjectIndex);
            const glm::vec3 center = glm::vec3(
                placement * glm::vec4(batch.boundsCenter[0], batch.boundsCenter[1], batch.boundsCenter[2], 1.0f));
            const float transformedRadius = batch.boundsRadius * (std::max)({glm::length(glm::vec3(placement[0])),
                                                                             glm::length(glm::vec3(placement[1])),
                                                                             glm::length(glm::vec3(placement[2]))});
            const glm::vec3 radius(transformedRadius);
            minimum = glm::min(minimum, center - radius);
            maximum = glm::max(maximum, center + radius);
            found = true;
        }
        if (found)
            camera.FrameBounds(minimum, maximum);
        return found;
    }
    [[nodiscard]] bool IsSubglobSelected(const std::uint32_t renderGroup, const std::uint32_t firstIndex) const
    {
        return selectionScope_ == SelectionScope::Subglob &&
               selectedSubglobs_.contains((static_cast<std::uint64_t>(renderGroup) << 32) | firstIndex);
    }
    [[nodiscard]] bool IsRenderGroupSelected(const std::uint32_t renderGroup) const
    {
        return selectionScope_ == SelectionScope::Glob && selectedRenderGroups_.contains(renderGroup);
    }
    [[nodiscard]] bool IsBatchSelected(const BrxRenderBatch &batch) const
    {
        if (selectionScope_ == SelectionScope::Object)
            return selectedObjectIndices_.contains(batch.ownerObjectIndex);
        if (selectionScope_ == SelectionScope::Glob)
            return selectedRenderGroups_.contains(batch.renderGroup);
        if (selectionScope_ == SelectionScope::Subglob)
            return selectedSubglobs_.contains((static_cast<std::uint64_t>(batch.renderGroup) << 32) | batch.firstIndex);
        return false;
    }
    [[nodiscard]] const BrxRenderBatch *SelectedBatch() const
    {
        const auto found = std::find_if(batches_.begin(), batches_.end(),
                                        [&](const BrxRenderBatch &batch)
                                        {
                                            return batch.renderGroup == selectedRenderGroup_ &&
                                                   (selectionScope_ != SelectionScope::Subglob ||
                                                    batch.firstIndex == selectedSubglobFirstIndex_);
                                        });
        return found == batches_.end() ? nullptr : &*found;
    }

    void HideSelection()
    {
        if (selectionScope_ == SelectionScope::Object)
            hiddenObjectSelections_.insert(selectedObjectIndices_.begin(), selectedObjectIndices_.end());
        else if (selectionScope_ == SelectionScope::Glob)
            hiddenGlobSelections_.insert(selectedRenderGroups_.begin(), selectedRenderGroups_.end());
        else if (selectionScope_ == SelectionScope::Subglob)
            hiddenSubglobSelections_.insert(selectedSubglobs_.begin(), selectedSubglobs_.end());
    }
    void SetSelectionLocked(const bool locked)
    {
        if (selectionScope_ == SelectionScope::Object)
            for (const std::int32_t objectIndex : selectedObjectIndices_)
                SetObjectLocked(objectIndex, locked);
        else if (selectionScope_ == SelectionScope::Glob)
            for (const std::uint32_t renderGroup : selectedRenderGroups_)
                SetGlobLocked(renderGroup, locked);
        else if (selectionScope_ == SelectionScope::Subglob)
            for (const std::uint64_t key : selectedSubglobs_)
                SetSubglobLocked(static_cast<std::uint32_t>(key >> 32), static_cast<std::uint32_t>(key), locked);
    }
    void IsolateSelection()
    {
        isolationScope_ = selectionScope_;
        isolationUsesSelection_ = SelectionCount() > 1;
        isolationObjectIndex_ = selectedObjectIndex_;
        isolationRenderGroup_ = selectedRenderGroup_;
        isolationSubglobFirstIndex_ = selectedSubglobFirstIndex_;
    }
    void ShowAllSelections()
    {
        hiddenObjectSelections_.clear();
        hiddenGlobSelections_.clear();
        hiddenSubglobSelections_.clear();
        isolationScope_ = SelectionScope::None;
        isolationUsesSelection_ = false;
    }
    [[nodiscard]] bool BatchEditorVisible(const BrxRenderBatch &batch) const
    {
        if (IsGameplayPreviewHidden(batch.ownerObjectIndex) ||
            hiddenObjectSelections_.contains(batch.ownerObjectIndex) ||
            hiddenGlobSelections_.contains(batch.renderGroup) ||
            hiddenSubglobSelections_.contains((static_cast<std::uint64_t>(batch.renderGroup) << 32) | batch.firstIndex))
            return false;
        if (isolationUsesSelection_)
            return IsBatchSelected(batch);
        switch (isolationScope_)
        {
        case SelectionScope::Object:
            return batch.ownerObjectIndex == isolationObjectIndex_;
        case SelectionScope::Glob:
            return batch.renderGroup == isolationRenderGroup_;
        case SelectionScope::Subglob:
            return batch.renderGroup == isolationRenderGroup_ && batch.firstIndex == isolationSubglobFirstIndex_;
        default:
            return true;
        }
    }
    [[nodiscard]] bool CelEditorVisible(const BrxCelBatch &cel) const
    {
        if (IsGameplayPreviewHidden(cel.ownerObjectIndex) || hiddenObjectSelections_.contains(cel.ownerObjectIndex) ||
            hiddenGlobSelections_.contains(cel.renderGroup))
            return false;
        const bool hasHiddenSubglob =
            std::any_of(hiddenSubglobSelections_.begin(), hiddenSubglobSelections_.end(), [&](const std::uint64_t key)
                        { return static_cast<std::uint32_t>(key >> 32) == cel.renderGroup; });
        if (hasHiddenSubglob)
            return false;
        if (isolationUsesSelection_)
        {
            if (isolationScope_ == SelectionScope::Object)
                return selectedObjectIndices_.contains(cel.ownerObjectIndex);
            if (isolationScope_ == SelectionScope::Glob)
                return selectedRenderGroups_.contains(cel.renderGroup);
            return false;
        }
        switch (isolationScope_)
        {
        case SelectionScope::Object:
            return cel.ownerObjectIndex == isolationObjectIndex_;
        case SelectionScope::Glob:
            return cel.renderGroup == isolationRenderGroup_;
        // Cel packets describe a whole GLOB, so drawing one while a single
        // SUBGLOB is isolated would incorrectly outline neighboring geometry.
        case SelectionScope::Subglob:
            return false;
        default:
            return true;
        }
    }
    [[nodiscard]] bool IsGameplayPreviewHidden(std::int32_t objectIndex) const
    {
        if (gameplayPreviewOwnHiddenObjects_.contains(objectIndex))
            return true;
        for (std::size_t guard = 0;
             objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < objects_.size() && guard++ <= objects_.size();
             objectIndex = RuntimeParentIndex(objectIndex))
            if (gameplayPreviewHiddenObjects_.contains(objectIndex))
                return true;
        return false;
    }

    void ApplyObjectTransform(std::int32_t objectIndex, const glm::mat4 &delta)
    {
        if (objectIndex < 0)
            return;
        std::vector<bool> transformed(vertices_.size(), false);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(delta)));
        for (auto &batch : batches_)
        {
            if (batch.ownerObjectIndex != objectIndex)
                continue;
            const std::size_t end =
                std::min(indices_.size(), static_cast<std::size_t>(batch.firstIndex) + batch.indexCount);
            for (std::size_t index = batch.firstIndex; index < end; ++index)
            {
                const std::uint32_t vertexIndex = indices_[index];
                if (vertexIndex >= vertices_.size() || transformed[vertexIndex])
                    continue;
                transformed[vertexIndex] = true;
                auto &vertex = vertices_[vertexIndex];
                auto &baseVertex = baseVertices_[vertexIndex];
                const glm::vec4 position =
                    delta * glm::vec4(baseVertex.position[0], baseVertex.position[1], baseVertex.position[2], 1.0f);
                glm::vec3 normal =
                    normalMatrix * glm::vec3(baseVertex.normal[0], baseVertex.normal[1], baseVertex.normal[2]);
                if (glm::dot(normal, normal) > 0.000001f)
                    normal = glm::normalize(normal);
                vertex.position[0] = position.x;
                vertex.position[1] = position.y;
                vertex.position[2] = position.z;
                vertex.normal[0] = normal.x;
                vertex.normal[1] = normal.y;
                vertex.normal[2] = normal.z;
                baseVertex = vertex;
            }
            const glm::vec4 center =
                delta * glm::vec4(batch.boundsCenter[0], batch.boundsCenter[1], batch.boundsCenter[2], 1.0f);
            batch.boundsCenter[0] = center.x;
            batch.boundsCenter[1] = center.y;
            batch.boundsCenter[2] = center.z;
            const glm::vec4 sortCenter =
                delta * glm::vec4(batch.sortCenter[0], batch.sortCenter[1], batch.sortCenter[2], 1.0f);
            batch.sortCenter[0] = sortCenter.x;
            batch.sortCenter[1] = sortCenter.y;
            batch.sortCenter[2] = sortCenter.z;
            const float scale = std::max(glm::length(glm::vec3(delta[0])),
                                         std::max(glm::length(glm::vec3(delta[1])), glm::length(glm::vec3(delta[2]))));
            batch.boundsRadius *= scale;
        }
        for (auto &batch : celBatches_)
        {
            if (batch.ownerObjectIndex != objectIndex)
                continue;
            for (std::uint32_t edgeIndex = 0; edgeIndex < batch.edgeCount; ++edgeIndex)
            {
                auto &edge = celEdges_[batch.firstEdge + edgeIndex];
                auto &baseEdge = baseCelEdges_[batch.firstEdge + edgeIndex];
                for (std::size_t pointIndex = 0; pointIndex < 4; ++pointIndex)
                {
                    auto &point = edge.points[pointIndex];
                    auto &basePoint = baseEdge.points[pointIndex];
                    const glm::vec4 transformed = delta * glm::vec4(basePoint[0], basePoint[1], basePoint[2], 1.0f);
                    point[0] = transformed.x;
                    point[1] = transformed.y;
                    point[2] = transformed.z;
                    for (int component = 0; component < 4; ++component)
                        basePoint[component] = point[component];
                }
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices_.size() * sizeof(BrxRenderVertex)),
                     vertices_.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, celEdgeBuffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(celEdges_.size() * sizeof(BrxCelEdge)),
                     celEdges_.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Runtime previews must never rewrite authored BRX data or the baked
    // editor vertex cache.  This delta is applied only while submitting the
    // selected object's render packets.
    void SetObjectPreviewTransform(const std::int32_t objectIndex, const glm::mat4 &delta)
    {
        previewObjectIndex_ = objectIndex;
        previewObjectDelta_ = delta;
    }

    void SetObjectPreviewAlpha(const float alpha)
    {
        previewObjectAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
    }

    void SetSkyCameraFollowPreview(const bool enabled)
    {
        skyCameraFollowPreview_ = enabled;
    }

    void SetDynamicShadowPreviewEnabled(const bool enabled)
    {
        dynamicShadowPreviewEnabled_ = enabled;
    }

    [[nodiscard]] bool DynamicShadowPreviewEnabled() const
    {
        return dynamicShadowPreviewEnabled_;
    }

    // Shadow options are authored on objects, while the renderer deliberately
    // keeps a private snapshot of those objects.  Rebuild only the two shadow
    // caches when Shadow Studio changes an option; a full scene upload would
    // unnecessarily recreate every texture and geometry buffer.
    void RefreshShadowPreviews(const BrxDocument &document)
    {
        objects_ = document.Objects();
        BuildAloxResolvedTransforms();
        if (shadowTextureArray_ != 0)
        {
            glDeleteTextures(1, &shadowTextureArray_);
            shadowTextureArray_ = 0;
        }
        BuildFastShadowPreviewCache();
        BuildProjectedShadows();
    }

    [[nodiscard]] bool SkyCameraFollowPreview() const
    {
        return skyCameraFollowPreview_;
    }

    void SetConveyorPreview(const std::int32_t objectIndex, const float uVelocity, const float vVelocity)
    {
        if (conveyorPreviewObjectIndex_ != objectIndex)
            conveyorPreviewStartTime_ = static_cast<float>(glfwGetTime());
        conveyorPreviewObjectIndex_ = objectIndex;
        conveyorPreviewUVelocity_ = uVelocity;
        conveyorPreviewVVelocity_ = vVelocity;
    }

    void ClearConveyorPreview()
    {
        conveyorPreviewObjectIndex_ = -1;
        conveyorPreviewUVelocity_ = 0.0f;
        conveyorPreviewVVelocity_ = 0.0f;
    }

    void SetAdditionalObjectPreview(const std::int32_t objectIndex, const glm::mat4 &delta, const float alpha)
    {
        additionalPreviewDeltas_[objectIndex] = delta;
        additionalPreviewAlphas_[objectIndex] = std::clamp(alpha, 0.0f, 1.0f);
    }

    // Object-specific inspectors usually hold the gameplay object index while
    // its render packets may belong to a child ALO. Store by the same logical
    // owner key used during submission. Editor vertices are baked in world
    // space, so callers provide the corresponding rigid world-space delta.
    void SetIsolatedObjectPreview(const std::int32_t objectIndex, const glm::mat4 &delta, const float alpha)
    {
        const std::int32_t logicalOwner = ResolveLogicalOwner(objectIndex);
        if (logicalOwner < 0)
            return;
        isolatedPreviewDeltas_[logicalOwner] = delta;
        isolatedPreviewAlphas_[logicalOwner] = std::clamp(alpha, 0.0f, 1.0f);
    }

    void ClearIsolatedObjectPreview(const std::int32_t objectIndex)
    {
        const std::int32_t logicalOwner = ResolveLogicalOwner(objectIndex);
        if (logicalOwner < 0)
            return;
        isolatedPreviewDeltas_.erase(logicalOwner);
        isolatedPreviewAlphas_.erase(logicalOwner);
    }

    void ClearAdditionalObjectPreviews()
    {
        additionalPreviewDeltas_.clear();
        additionalPreviewAlphas_.clear();
    }

    void ClearObjectPreviewTransform()
    {
        previewObjectIndex_ = -1;
        previewObjectDelta_ = glm::mat4(1.0f);
        previewObjectAlpha_ = 1.0f;
    }

    void SetAutomaticAloMotion(const bool enabled)
    {
        if (automaticAloMotion_ == enabled)
            return;
        automaticAloMotion_ = enabled;
        runtimeMotionStartTime_ = static_cast<float>(glfwGetTime());
        runtimeMotionDeltas_.assign(objects_.size(), glm::mat4(1.0f));
    }

    void SetAsegPreviewTransforms(const std::vector<glm::mat4> &deltas)
    {
        const bool valid = deltas.size() == objects_.size();
        asegPreviewDeltas_ = deltas;
        if (!valid)
            asegPreviewDeltas_.assign(objects_.size(), glm::mat4(1.0f));
        asegPreviewActive_ = valid;
    }

    void SetAsegPreviewGeometry(const std::vector<BrxRenderVertex> &vertices, const std::vector<BrxCelEdge> &celEdges)
    {
        const bool validVertices = vertices.size() == baseVertices_.size();
        const bool validCelEdges = celEdges.size() == baseCelEdges_.size();
        asegPreviewVertices_ = validVertices ? vertices : baseVertices_;
        asegPreviewCelEdges_ = validCelEdges ? celEdges : baseCelEdges_;
        vertices_ = asegPreviewVertices_;
        celEdges_ = asegPreviewCelEdges_;
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices_.size() * sizeof(BrxRenderVertex)),
                        vertices_.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, celEdgeBuffer_);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(celEdges_.size() * sizeof(BrxCelEdge)),
                        celEdges_.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        asegPreviewGeometryActive_ = validVertices;
        dynamicShadowMapsDirty_ = true;
    }

    void ClearAsegPreviewGeometry()
    {
        if (!asegPreviewGeometryActive_)
            return;
        vertices_ = baseVertices_;
        celEdges_ = baseCelEdges_;
        asegPreviewVertices_.clear();
        asegPreviewCelEdges_.clear();
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertices_.size() * sizeof(BrxRenderVertex)),
                        vertices_.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, celEdgeBuffer_);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(celEdges_.size() * sizeof(BrxCelEdge)),
                        celEdges_.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        asegPreviewGeometryActive_ = false;
        dynamicShadowMapsDirty_ = true;
    }

    void ClearAsegPreviewTransforms()
    {
        asegPreviewDeltas_.assign(objects_.size(), glm::mat4(1.0f));
        asegPreviewActive_ = false;
        ClearAsegPreviewGeometry();
    }

    [[nodiscard]] bool IsInPreviewHierarchy(std::int32_t objectIndex) const
    {
        if (previewObjectIndex_ < 0 || objectIndex < 0)
            return false;

        // Render packets are commonly owned by a child ALO even though
        // picking/inspection resolves that visual record to its concrete
        // SO-derived gameplay object (BUTTON, STEPGUARD, and so on).  Use the
        // same identity here. Do not walk the raw serialized descendant tree:
        // gameplay objects can sit above unrelated level content, and applying
        // a preview delta to that entire tree scales or moves the level.
        const std::int32_t previewLogicalOwner = ResolveLogicalOwner(previewObjectIndex_);
        const std::int32_t objectLogicalOwner = ResolveLogicalOwner(objectIndex);
        return objectIndex == previewObjectIndex_ || objectLogicalOwner == previewObjectIndex_ ||
               objectLogicalOwner == previewLogicalOwner;
    }

    [[nodiscard]] bool IsInObjectHierarchy(std::int32_t objectIndex, const std::int32_t hierarchyRoot) const
    {
        if (objectIndex < 0 || hierarchyRoot < 0 || static_cast<std::size_t>(objectIndex) >= objects_.size() ||
            static_cast<std::size_t>(hierarchyRoot) >= objects_.size())
            return false;

        const std::int32_t rootLogicalOwner = ResolveLogicalOwner(hierarchyRoot);
        std::size_t guard = 0;
        while (objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < objects_.size() &&
               guard++ <= objects_.size())
        {
            const std::int32_t logicalOwner = ResolveLogicalOwner(objectIndex);
            if (objectIndex == hierarchyRoot || logicalOwner == hierarchyRoot || logicalOwner == rootLogicalOwner)
                return true;
            objectIndex = objects_[static_cast<std::size_t>(objectIndex)].parentIndex;
        }
        return false;
    }

    [[nodiscard]] std::int32_t RuntimeParentIndex(const std::int32_t objectIndex) const
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= objects_.size())
            return -1;
        const auto &object = objects_[static_cast<std::size_t>(objectIndex)];
        const bool serializedUnderProxyWrapper = object.parentIndex >= 0 &&
                                                 static_cast<std::size_t>(object.parentIndex) < objects_.size() &&
                                                 objects_[static_cast<std::size_t>(object.parentIndex)].cid == 83 &&
                                                 object.proxyRecordIndex >= 0 && object.proxyRootIndex >= 0;
        return serializedUnderProxyWrapper ? object.proxyRuntimeParentIndex : object.parentIndex;
    }

    [[nodiscard]] glm::mat4 AloxDeltaForObject(const std::int32_t objectIndex) const
    {
        if (objectIndex < 0 || static_cast<std::size_t>(objectIndex) >= aloxResolvedDeltas_.size())
            return glm::mat4(1.0f);
        return aloxResolvedDeltas_[static_cast<std::size_t>(objectIndex)];
    }

    [[nodiscard]] glm::mat4 SubmissionTransformForObject(const std::int32_t objectIndex) const
    {
        if (objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < frameSubmissionTransformValid_.size() &&
            frameSubmissionTransformValid_[static_cast<std::size_t>(objectIndex)] != 0)
            return frameSubmissionTransforms_[static_cast<std::size_t>(objectIndex)];
        glm::mat4 result = AloxDeltaForObject(objectIndex);
        if (skyCameraFollowPreview_)
        {
            std::int32_t ancestor = objectIndex;
            std::size_t guard = 0;
            while (ancestor >= 0 && static_cast<std::size_t>(ancestor) < objects_.size() && guard++ <= objects_.size())
            {
                const auto &candidate = objects_[static_cast<std::size_t>(ancestor)];
                if (candidate.cid == 84)
                {
                    glm::vec3 authoredPosition(candidate.worldTransform[12], candidate.worldTransform[13],
                                               candidate.worldTransform[14]);
                    if (static_cast<std::size_t>(ancestor) < aloxResolvedWorld_.size())
                        authoredPosition = glm::vec3(aloxResolvedWorld_[static_cast<std::size_t>(ancestor)][3]);
                    bool followZ = false;
                    const auto property =
                        std::find_if(candidate.properties.begin(), candidate.properties.end(),
                                     [](const BrxPropertyRecord &value) { return value.eopid == 608; });
                    if (property != candidate.properties.end())
                        followZ = property->boolValue;
                    glm::vec3 target = skyPreviewCameraPosition_;
                    if (!followZ)
                        target.z = authoredPosition.z;
                    result = glm::translate(glm::mat4(1.0f), target - authoredPosition) * result;
                    break;
                }
                const std::int32_t parent = RuntimeParentIndex(ancestor);
                if (parent == ancestor)
                    break;
                ancestor = parent;
            }
        }
        const std::int32_t logicalOwner = ResolveLogicalOwner(objectIndex);
        const auto isolated = isolatedPreviewDeltas_.find(logicalOwner);
        const auto additional = additionalPreviewDeltas_.find(logicalOwner);
        if (isolated != isolatedPreviewDeltas_.end())
            result = isolated->second * result;
        else if (additional != additionalPreviewDeltas_.end())
            result = additional->second * result;
        else if (IsInPreviewHierarchy(objectIndex))
            result = previewObjectDelta_ * result;
        else if (asegPreviewActive_ && objectIndex >= 0 &&
                 static_cast<std::size_t>(objectIndex) < asegPreviewDeltas_.size())
            result = asegPreviewDeltas_[static_cast<std::size_t>(objectIndex)] * result;
        else if (objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < runtimeMotionDeltas_.size())
            result = runtimeMotionDeltas_[static_cast<std::size_t>(objectIndex)] * result;
        if (objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < frameSubmissionTransformValid_.size())
        {
            frameSubmissionTransforms_[static_cast<std::size_t>(objectIndex)] = result;
            frameSubmissionTransformValid_[static_cast<std::size_t>(objectIndex)] = 1;
        }
        return result;
    }

    bool Pick(const EditorCamera &camera, float x, float y, int width, int height, const bool additive = false)
    {
        if (((vertices_.empty() || indices_.empty()) && emitterPreview_.Particles().empty() && laserBeams_.empty()) ||
            width <= 0 || height <= 0)
        {
            ClearSelection();
            return false;
        }
        const float ndcX = 2.0f * x / static_cast<float>(width) - 1.0f;
        const float ndcY = 1.0f - 2.0f * y / static_cast<float>(height);
        const glm::mat4 projection = ProjectionMatrix(static_cast<float>(width) / static_cast<float>(height));
        const glm::mat4 viewProjection = projection * camera.ViewMatrix();
        const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
        glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;
        const glm::vec3 origin = glm::vec3(nearPoint);
        const glm::vec3 direction = glm::normalize(glm::vec3(farPoint - nearPoint));
        struct PickHit
        {
            float distance;
            std::int32_t logicalObjectIndex;
            std::int32_t visualObjectIndex;
            std::uint32_t renderGroup;
            bool emitterParticle;
        };
        std::vector<PickHit> hits;
        // Preview particles are transient, but selecting one should resolve to
        // its authored EMITTER. Use a camera-ray sphere around the rendered
        // billboard so particles remain clickable without inventing BRX objects.
        for (const auto &particle : emitterPreview_.Particles())
        {
            if (particle.ownerObjectIndex < 0 || IsObjectLocked(particle.ownerObjectIndex))
                continue;
            const glm::vec3 offset = particle.position - origin;
            const float alongRay = glm::dot(offset, direction);
            if (alongRay <= 0.0f)
                continue;
            const glm::vec3 closest = origin + direction * alongRay;
            const float radius = (std::max)(8.0f, std::abs(particle.scale));
            const float radiusSquared = radius * radius;
            const float centerDistanceSquared = glm::dot(particle.position - closest, particle.position - closest);
            if (centerDistanceSquared > radiusSquared)
                continue;
            const float entryDistance = alongRay - std::sqrt((std::max)(0.0f, radiusSquared - centerDistanceSquared));
            hits.push_back({(std::max)(entryDistance, 0.0001f), particle.ownerObjectIndex, particle.ownerObjectIndex,
                            kNoSelection, true});
        }
        // RenderLasenSelf creates camera-facing beam instances procedurally, so
        // they have no persistent triangle batch for the normal picker. Match
        // the visible polyline in screen space and select its owning LASEN.
        constexpr float laserPickRadiusPixels = 8.0f;
        for (const auto &beam : laserBeams_)
        {
            if (beam.ownerObjectIndex < 0 || static_cast<std::size_t>(beam.ownerObjectIndex) >= objects_.size() ||
                beam.points.size() < 2 || IsObjectLocked(beam.ownerObjectIndex) ||
                objects_[static_cast<std::size_t>(beam.ownerObjectIndex)].hidden ||
                hiddenObjectSelections_.contains(beam.ownerObjectIndex))
                continue;
            glm::mat4 lasenWorld(1.0f);
            std::memcpy(glm::value_ptr(lasenWorld),
                        objects_[static_cast<std::size_t>(beam.ownerObjectIndex)].worldTransform, sizeof(float) * 16);
            for (std::size_t point = 1; point < beam.points.size(); ++point)
            {
                const auto &local0 = beam.points[point - 1];
                const auto &local1 = beam.points[point];
                const glm::vec3 world0 = glm::vec3(lasenWorld * glm::vec4(local0[0], local0[1], local0[2], 1.0f));
                const glm::vec3 world1 = glm::vec3(lasenWorld * glm::vec4(local1[0], local1[1], local1[2], 1.0f));
                const glm::vec4 clip0 = viewProjection * glm::vec4(world0, 1.0f);
                const glm::vec4 clip1 = viewProjection * glm::vec4(world1, 1.0f);
                if (clip0.w <= 0.0f || clip1.w <= 0.0f)
                    continue;
                const glm::vec2 screen0((clip0.x / clip0.w * 0.5f + 0.5f) * width,
                                        (0.5f - clip0.y / clip0.w * 0.5f) * height);
                const glm::vec2 screen1((clip1.x / clip1.w * 0.5f + 0.5f) * width,
                                        (0.5f - clip1.y / clip1.w * 0.5f) * height);
                const glm::vec2 screenSegment = screen1 - screen0;
                const float screenLengthSquared = glm::dot(screenSegment, screenSegment);
                if (screenLengthSquared <= 0.0001f)
                    continue;
                const float segmentT =
                    glm::clamp(glm::dot(glm::vec2(x, y) - screen0, screenSegment) / screenLengthSquared, 0.0f, 1.0f);
                const glm::vec2 screenClosest = screen0 + screenSegment * segmentT;
                const glm::vec2 screenDelta = glm::vec2(x, y) - screenClosest;
                if (glm::dot(screenDelta, screenDelta) > laserPickRadiusPixels * laserPickRadiusPixels)
                    continue;
                const glm::vec3 worldClosest = glm::mix(world0, world1, segmentT);
                const float distance = glm::dot(worldClosest - origin, direction);
                if (distance > 0.0f)
                    hits.push_back({distance, beam.ownerObjectIndex, beam.ownerObjectIndex, kNoSelection, false});
            }
        }
        for (const auto &batch : batches_)
        {
            if (!BatchEditorVisible(batch))
                continue;
            if (IsObjectLocked(batch.ownerObjectIndex) || IsGlobLocked(batch.renderGroup) ||
                IsSubglobLocked(batch.renderGroup, batch.firstIndex))
                continue;
            const std::size_t end = static_cast<std::size_t>(batch.firstIndex) + batch.indexCount;
            if (end > indices_.size())
                continue;
            glm::mat4 editorInstanceTransform(1.0f);
            if (batch.hasEditorInstanceTransform)
                std::memcpy(glm::value_ptr(editorInstanceTransform), batch.editorInstanceTransform,
                            sizeof(batch.editorInstanceTransform));
            editorInstanceTransform = SubmissionTransformForObject(batch.ownerObjectIndex) * editorInstanceTransform;
            const auto pickPosition = [&](const std::uint32_t vertexIndex)
            {
                const auto &vertex = vertices_[vertexIndex];
                const glm::vec3 position(vertex.position[0], vertex.position[1], vertex.position[2]);
                return glm::vec3(editorInstanceTransform * glm::vec4(position, 1.0f));
            };
            for (std::size_t index = batch.firstIndex; index + 2 < end; index += 3)
            {
                const std::uint32_t ia = indices_[index];
                const std::uint32_t ib = indices_[index + 1];
                const std::uint32_t ic = indices_[index + 2];
                if (ia >= vertices_.size() || ib >= vertices_.size() || ic >= vertices_.size())
                    continue;
                const glm::vec3 a = pickPosition(ia);
                const glm::vec3 b = pickPosition(ib);
                const glm::vec3 c = pickPosition(ic);
                const glm::vec3 edge1 = b - a;
                const glm::vec3 edge2 = c - a;
                const glm::vec3 p = glm::cross(direction, edge2);
                const float determinant = glm::dot(edge1, p);
                if (std::abs(determinant) < 0.000001f)
                    continue;
                const float inverseDeterminant = 1.0f / determinant;
                const glm::vec3 t = origin - a;
                const float u = glm::dot(t, p) * inverseDeterminant;
                if (u < 0.0f || u > 1.0f)
                    continue;
                const glm::vec3 q = glm::cross(t, edge1);
                const float v = glm::dot(direction, q) * inverseDeterminant;
                if (v < 0.0f || u + v > 1.0f)
                    continue;
                const float distance = glm::dot(edge2, q) * inverseDeterminant;
                if (distance > 0.0f)
                    hits.push_back(
                        {distance, batch.ownerObjectIndex, batch.ownerObjectIndex, batch.renderGroup, false});
            }
        }
        std::stable_sort(hits.begin(), hits.end(),
                         [](const PickHit &left, const PickHit &right) { return left.distance < right.distance; });
        std::vector<PickTarget> targets;
        for (const auto &hit : hits)
        {
            const bool duplicate = std::any_of(targets.begin(), targets.end(),
                                               [&](const PickTarget &target)
                                               {
                                                   // Every GLOB record is a distinct placement, even when several
                                                   // records share one ALO/SO owner and the same source geometry.
                                                   return hit.renderGroup == kNoSelection
                                                              ? target.renderGroup == kNoSelection &&
                                                                    target.objectIndex == hit.logicalObjectIndex &&
                                                                    target.emitterParticle == hit.emitterParticle
                                                              : target.renderGroup == hit.renderGroup;
                                               });
            if (!duplicate)
                targets.push_back({hit.visualObjectIndex, hit.visualObjectIndex, hit.renderGroup, hit.emitterParticle});
        }
        if (targets.empty())
        {
            if (!additive)
                ClearSelection();
            lastPickX_ = x;
            lastPickY_ = y;
            return false;
        }
        const float dx = x - lastPickX_;
        const float dy = y - lastPickY_;
        const bool sameSpot = dx * dx + dy * dy <= 36.0f;
        const bool sameStack = sameSpot && targets.size() == pickCycle_.size() &&
                               std::equal(targets.begin(), targets.end(), pickCycle_.begin(),
                                          [](const PickTarget &left, const PickTarget &right)
                                          {
                                              return left.objectIndex == right.objectIndex &&
                                                     left.renderGroup == right.renderGroup &&
                                                     left.emitterParticle == right.emitterParticle;
                                          });
        if (sameStack)
            pickCycleIndex_ = (pickCycleIndex_ + 1) % targets.size();
        else
        {
            pickCycle_ = targets;
            pickCycleIndex_ = 0;
        }
        lastPickX_ = x;
        lastPickY_ = y;
        const PickTarget &selected = pickCycle_[pickCycleIndex_];
        lastPickWasEmitterParticle_ = selected.emitterParticle;
        if (additive)
        {
            if (selected.renderGroup == kNoSelection)
                ToggleObjectSelection(selected.objectIndex);
            else
                ToggleRenderGroupSelection(selected.renderGroup);
        }
        else
        {
            if (selected.renderGroup == kNoSelection)
            {
                selectedObjectIndex_ = selected.objectIndex;
                selectedVisualObjectIndex_ = selected.visualObjectIndex;
                selectedRenderGroup_ = kNoSelection;
                selectedSubglobFirstIndex_ = kNoSelection;
                selectionScope_ = SelectionScope::Object;
                selectedObjectIndices_.clear();
                selectedRenderGroups_.clear();
                selectedSubglobs_.clear();
                selectedObjectIndices_.insert(selected.objectIndex);
                return true;
            }
            selectedObjectIndex_ = selected.objectIndex;
            selectedVisualObjectIndex_ = selected.visualObjectIndex;
            selectedRenderGroup_ = selected.renderGroup;
            selectedSubglobFirstIndex_ = kNoSelection;
            selectionScope_ = SelectionScope::Glob;
            selectedObjectIndices_.clear();
            selectedRenderGroups_.clear();
            selectedSubglobs_.clear();
            selectedRenderGroups_.insert(selected.renderGroup);
        }
        return true;
    }

    GLuint Render(const EditorCamera &camera, int width, int height, int presentHeight, bool frustumCulling,
                  bool bspCulling, bool renderCelBorders, bool showSelectionHighlight, const float *selectionColor,
                  int fogType, const std::vector<BrxCollisionTriangle> &collisionTriangles, bool showCollision,
                  bool showGameplayCollision, bool showCameraCollision, int collisionColorMode,
                  const float *gameplayCollisionColor, const float *cameraCollisionColor,
                  std::int32_t collisionOwnerFilter)
    {
        const double renderCpuStart = glfwGetTime();
        const double previousGpuRenderMs = performanceDiagnostics_.gpuRenderMs;
        performanceDiagnostics_ = {};
        performanceDiagnostics_.gpuRenderMs = previousGpuRenderMs;
        performanceDiagnostics_.projectedShadowCount = projectedShadows_.size();
        performanceDiagnostics_.dynamicShadowCasterCount = dynamicShadowCasters_.size();
        performanceDiagnostics_.shadowReceiverBatchCount =
            static_cast<std::size_t>(std::count_if(batches_.begin(), batches_.end(), [](const BrxRenderBatch &batch)
                                                   { return batch.shadowReceiverMask != 0; }));
        performanceDiagnostics_.fastShadowSourceCount = fastShadowSources_.size();
        performanceDiagnostics_.dynamicShadowPreviewEnabled = dynamicShadowPreviewEnabled_;
        const std::size_t gpuTimerIndex = gpuTimerWriteIndex_;
        const bool measureGpu = gpuTimerQueries_[gpuTimerIndex] != 0 && !gpuTimerPending_[gpuTimerIndex];
        if (measureGpu)
            glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries_[gpuTimerIndex]);
        ++renderFrame_;
        skyPreviewCameraPosition_ = camera.Position();
        emitterRenderDiagnostics_ = {};
        emitterRenderDiagnostics_.simulatedParticles = emitterPreview_.Particles().size();
        width = std::max(width, 1);
        height = std::max(height, 1);
        EnsureTarget(width, height);
        const float runtimeTime = static_cast<float>(glfwGetTime());
        double performanceMark = glfwGetTime();
        UpdateRuntimeAloMotion(runtimeTime);
        performanceDiagnostics_.runtimeMotionMs = (glfwGetTime() - performanceMark) * 1000.0;
        performanceMark = glfwGetTime();
        UpdateWrDeformation(runtimeTime);
        performanceDiagnostics_.wrDeformationMs = (glfwGetTime() - performanceMark) * 1000.0;
        // Submission transforms are requested repeatedly by culling, sorting,
        // lighting, cel borders and draw calls. Resolve each object at most once
        // per frame instead of retraversing its hierarchy for every SUBGLOB.
        frameSubmissionTransforms_.resize(objects_.size());
        frameSubmissionTransformValid_.assign(objects_.size(), 0);
        RefreshDynamicVisibilityMasks();
        // The editor uses an ownership-optimized ImGui backend which does not
        // synchronously read back and restore GL state. Establish the complete
        // scene baseline explicitly before any shadow or world submission.
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glActiveTexture(GL_TEXTURE0);
        performanceMark = glfwGetTime();
        if (dynamicShadowPreviewEnabled_)
            RenderDynamicShadows();
        performanceDiagnostics_.dynamicShadowsMs = (glfwGetTime() - performanceMark) * 1000.0;
        const double sceneGeometryStart = glfwGetTime();
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glClearStencil(0);
        glClearColor(skyColor_[0], skyColor_[1], skyColor_[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        visibleBatchCount_ = 0;
        culledBatchCount_ = 0;
        if (visibilityNodes_.empty())
            cameraZoneMask_ = 0x0fffffffu;
        else
        {
            const std::uint32_t resolvedZoneMask = ResolveVisibilityMask(camera.Position());
            if (resolvedZoneMask != 0)
                cameraZoneMask_ = resolvedZoneMask;
        }
        if (indexCount_ != 0)
        {
            const glm::mat4 projection = ProjectionMatrix(static_cast<float>(width) / static_cast<float>(height));
            const glm::mat4 viewProjection = projection * camera.ViewMatrix();
            const auto matrixRow = [&](int row)
            {
                return glm::vec4(viewProjection[0][row], viewProjection[1][row], viewProjection[2][row],
                                 viewProjection[3][row]);
            };
            const glm::vec4 row0 = matrixRow(0);
            const glm::vec4 row1 = matrixRow(1);
            const glm::vec4 row2 = matrixRow(2);
            const glm::vec4 row3 = matrixRow(3);
            std::array<glm::vec4, 6> frustumPlanes{row3 + row0, row3 - row0, row3 + row1,
                                                   row3 - row1, row3 + row2, row3 - row2};
            for (auto &plane : frustumPlanes)
            {
                const float length = glm::length(glm::vec3(plane));
                if (length > 0.000001f)
                    plane /= length;
            }
            const auto sphereInFrustum = [&](const BrxRenderBatch &batch)
            {
                const glm::mat4 placement = SubmissionTransformForObject(batch.ownerObjectIndex);
                const bool mergedStatic = batch.ownerObjectIndex >= 0 &&
                                          static_cast<std::size_t>(batch.ownerObjectIndex) < objects_.size() &&
                                          objects_[static_cast<std::size_t>(batch.ownerObjectIndex)].cid == 4;
                // RenderMsGlobset performs one frustum test with the GLOB
                // center/radius before DrawGlob submits any SUBGLOB. Using
                // each material's tighter bound can incorrectly remove only
                // part of an MS GLOB near a frustum edge.
                const float *centerSource = mergedStatic ? batch.sortCenter : batch.boundsCenter;
                const glm::vec3 center =
                    glm::vec3(placement * glm::vec4(centerSource[0], centerSource[1], centerSource[2], 1.0f));
                const float sourceRadius = mergedStatic ? batch.mrdRadius : batch.boundsRadius;
                const float radius = sourceRadius * (std::max)({glm::length(glm::vec3(placement[0])),
                                                                glm::length(glm::vec3(placement[1])),
                                                                glm::length(glm::vec3(placement[2]))});
                for (const auto &plane : frustumPlanes)
                {
                    if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
                        return false;
                }
                return true;
            };
            // ProjectCane RecalcCm/FInsideCmMrd: authored MRDs are scaled by
            // max(1, camera MRD ratio / vertical FOV), then fade over the
            // final 400 authored units (scaled by the same adjustment).
            const float cameraMrdAdjust =
                (std::max)(1.0f, std::clamp(cameraMrdRatio_, 0.75f, 3.0f) / (std::max)(projectionFov_, 0.0001f));
            const auto insideMrdAlpha = [&](const glm::vec3 &center, const float radius, const float authoredMrd)
            {
                if (authoredMrd >= 1.0e9f)
                    return 1.0f;
                const glm::vec3 delta = center - camera.Position();
                const float distanceSquared = glm::dot(delta, delta);
                const float outer = authoredMrd * cameraMrdAdjust + radius;
                const float outerSquared = outer * outer;
                if (distanceSquared > outerSquared)
                    return 0.0f;
                const float inner = outer - cameraMrdAdjust * 400.0f;
                const float innerSquared = inner * inner;
                if (distanceSquared <= innerSquared)
                    return 1.0f;
                const float denominator = outerSquared - innerSquared;
                return denominator > 0.0f ? std::clamp((outerSquared - distanceSquared) / denominator, 0.0f, 1.0f)
                                          : 0.0f;
            };
            std::unordered_map<std::int32_t, float> inheritedMrdAlphaCache;
            inheritedMrdAlphaCache.reserve(objects_.size());
            const auto inheritedObjectMrdAlpha = [&](std::int32_t objectIndex, const bool celBorder)
            {
                // Ordinary batch MRD requests dominate this path and all
                // SUBGLOBs owned by one object share the inherited result.
                // Cache that hierarchy walk once per owner per frame.  Cel
                // border distance has separate semantics and is handled by
                // its dedicated path below.
                if (!celBorder)
                    if (const auto cached = inheritedMrdAlphaCache.find(objectIndex);
                        cached != inheritedMrdAlphaCache.end())
                        return cached->second;
                const std::int32_t originalObjectIndex = objectIndex;
                float alpha = 1.0f;
                std::size_t guard = 0;
                while (objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < objects_.size() &&
                       guard++ <= objects_.size())
                {
                    const auto &object = objects_[static_cast<std::size_t>(objectIndex)];
                    if (object.mrdMode == 2)
                    {
                        glm::vec3 center =
                            objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < aloxResolvedWorld_.size()
                                ? glm::vec3(aloxResolvedWorld_[static_cast<std::size_t>(objectIndex)][3])
                                : glm::vec3(object.worldTransform[12], object.worldTransform[13],
                                            object.worldTransform[14]);
                        if (IsInPreviewHierarchy(objectIndex))
                            center = glm::vec3(previewObjectDelta_ * glm::vec4(center, 1.0f));
                        const glm::mat4 placement = SubmissionTransformForObject(objectIndex);
                        const float radiusScale =
                            (std::max)({glm::length(glm::vec3(placement[0])), glm::length(glm::vec3(placement[1])),
                                        glm::length(glm::vec3(placement[2]))});
                        const float surfaceAlpha =
                            insideMrdAlpha(center, object.renderRadiusAll * radiusScale, object.maximumRenderDistance);
                        alpha *= surfaceAlpha;
                        if (celBorder && object.celBorderMaximumDistance < object.maximumRenderDistance)
                        {
                            alpha *= insideMrdAlpha(center, object.renderRadiusAll * radiusScale,
                                                    object.celBorderMaximumDistance);
                        }
                        if (alpha <= 0.0f)
                        {
                            if (!celBorder)
                                inheritedMrdAlphaCache.emplace(originalObjectIndex, 0.0f);
                            return 0.0f;
                        }
                    }
                    objectIndex = RuntimeParentIndex(objectIndex);
                }
                if (!celBorder)
                    inheritedMrdAlphaCache.emplace(originalObjectIndex, alpha);
                return alpha;
            };
            const auto inheritedCelBorderMrdAlpha = [&](std::int32_t objectIndex)
            {
                float alpha = 1.0f;
                std::size_t guard = 0;
                while (objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < objects_.size() &&
                       guard++ <= objects_.size())
                {
                    const auto &object = objects_[static_cast<std::size_t>(objectIndex)];
                    if (object.mrdMode == 2 && object.celBorderMaximumDistance < object.maximumRenderDistance)
                    {
                        glm::vec3 center =
                            objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < aloxResolvedWorld_.size()
                                ? glm::vec3(aloxResolvedWorld_[static_cast<std::size_t>(objectIndex)][3])
                                : glm::vec3(object.worldTransform[12], object.worldTransform[13],
                                            object.worldTransform[14]);
                        if (IsInPreviewHierarchy(objectIndex))
                            center = glm::vec3(previewObjectDelta_ * glm::vec4(center, 1.0f));
                        const glm::mat4 placement = SubmissionTransformForObject(objectIndex);
                        const float radiusScale =
                            (std::max)({glm::length(glm::vec3(placement[0])), glm::length(glm::vec3(placement[1])),
                                        glm::length(glm::vec3(placement[2]))});
                        alpha *= insideMrdAlpha(center, object.renderRadiusAll * radiusScale,
                                                object.celBorderMaximumDistance);
                        if (alpha <= 0.0f)
                            return 0.0f;
                    }
                    objectIndex = RuntimeParentIndex(objectIndex);
                }
                return alpha;
            };
            const auto mrdAlphaForBatch = [&](const BrxRenderBatch &batch)
            {
                const std::int32_t owner = batch.ownerObjectIndex;
                float alpha = inheritedObjectMrdAlpha(owner, false);
                if (alpha <= 0.0f)
                    return 0.0f;
                if (owner >= 0 && static_cast<std::size_t>(owner) < objects_.size())
                {
                    const auto &object = objects_[static_cast<std::size_t>(owner)];
                    // The live game may normalize an ALO's effective MRDS as
                    // it is attached to its hierarchy.  The editor has only
                    // the serialized snapshot, so a finite authored GLOB MRD
                    // must remain authoritative even when that snapshot says
                    // MRDS_None.  Otherwise ordinary static objects and their
                    // outlines remain visible forever.  MRDS_All still adds
                    // the inherited whole-object test above.
                    if (object.cid == 4 || object.mrdMode == 1 || batch.maximumRenderDistance < 1.0e9f)
                    {
                        const glm::vec3 center =
                            glm::vec3(SubmissionTransformForObject(owner) *
                                      glm::vec4(batch.sortCenter[0], batch.sortCenter[1], batch.sortCenter[2], 1.0f));
                        const glm::mat4 placement = SubmissionTransformForObject(owner);
                        const float radiusScale =
                            (std::max)({glm::length(glm::vec3(placement[0])), glm::length(glm::vec3(placement[1])),
                                        glm::length(glm::vec3(placement[2]))});
                        alpha *= insideMrdAlpha(center, batch.mrdRadius * radiusScale, batch.maximumRenderDistance);
                    }
                }
                return alpha;
            };
            glUseProgram(program_);
            glUniformMatrix4fv(glGetUniformLocation(program_, "uViewProjection"), 1, GL_FALSE,
                               glm::value_ptr(viewProjection));
            const GLint modelLocation = glGetUniformLocation(program_, "uModel");
            const glm::mat4 identityModel(1.0f);
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(identityModel));
            const glm::mat4 cameraMatrix = glm::inverse(camera.ViewMatrix());
            const auto rotateVectorsMatrix = [](glm::vec3 from, glm::vec3 to)
            {
                constexpr float epsilon = 0.0001f;
                from = glm::length(from) < epsilon ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::normalize(from);
                to = glm::length(to) < epsilon ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::normalize(to);
                glm::vec3 axis = glm::cross(from, to);
                if (glm::length(axis) < epsilon)
                {
                    axis = glm::cross(from, glm::vec3(1.0f, 0.0f, 0.0f));
                    if (glm::length(axis) < epsilon)
                        axis = glm::cross(from, glm::vec3(0.0f, 1.0f, 0.0f));
                }
                axis = glm::normalize(axis);
                const float angle = std::acos(std::clamp(glm::dot(from, to), -1.0f, 1.0f));
                return glm::mat3(glm::rotate(glm::mat4(1.0f), angle, axis));
            };
            const auto rtckModelForBatch = [&](const BrxRenderBatch &batch)
            {
                glm::mat4 editorInstanceModel(1.0f);
                if (batch.hasEditorInstanceTransform)
                    std::memcpy(glm::value_ptr(editorInstanceModel), batch.editorInstanceTransform,
                                sizeof(batch.editorInstanceTransform));
                editorInstanceModel = SubmissionTransformForObject(batch.ownerObjectIndex) * editorInstanceModel;
                if (batch.rotationTracking == 0)
                    return editorInstanceModel;

                glm::mat4 baseModel(1.0f);
                std::memcpy(glm::value_ptr(baseModel), batch.rtckBaseMatrix, sizeof(batch.rtckBaseMatrix));
                const glm::vec3 center(batch.sortCenter[0], batch.sortCenter[1], batch.sortCenter[2]);

                // Exact AdjustAloRtckMat submission path used by both
                // RenderAloGlobset and RenderMsGlobset. The recovered game
                // function treats every non-None RTCK value through this same
                // two-stage camera alignment.
                // CM stores the game camera basis as X=forward, Y=left,
                // Z=up. GLM's inverse view stores right, up, backward. Thus
                // AdjustAloRtckMat's -pcm->mat[0] is GL backward (column 2),
                // while pcm->mat[2] is GL up (column 1).
                const glm::vec3 toCamera = glm::vec3(cameraMatrix[2]);
                const glm::mat3 firstRotation = rotateVectorsMatrix(glm::vec3(baseModel[0]), toCamera);
                glm::mat4 centered = baseModel;
                centered[3].x -= center.x;
                centered[3].y -= center.y;
                centered[3].z -= center.z;
                glm::mat4 first(1.0f);
                first[0] = glm::vec4(firstRotation[0], 0.0f);
                first[1] = glm::vec4(firstRotation[1], 0.0f);
                first[2] = glm::vec4(firstRotation[2], 0.0f);
                const glm::mat4 aligned = first * centered;
                const glm::vec3 alignedZ(aligned[2]);
                const glm::vec3 projectedZ = alignedZ - toCamera * glm::dot(alignedZ, toCamera);
                const glm::mat3 secondRotation = rotateVectorsMatrix(projectedZ, glm::vec3(cameraMatrix[1]));
                glm::mat4 second(1.0f);
                second[0] = glm::vec4(secondRotation[0], 0.0f);
                second[1] = glm::vec4(secondRotation[1], 0.0f);
                second[2] = glm::vec4(secondRotation[2], 0.0f);
                second[3] = glm::vec4(center, 1.0f);
                const glm::mat4 adjusted = second * aligned;

                // Editor vertices already contain baseModel, unlike the game
                // vertex stream, so submit only the camera-tracking delta.
                return editorInstanceModel * adjusted * glm::inverse(baseModel);
            };
            glUniform1i(glGetUniformLocation(program_, "uAmbient"), 0);
            glUniform1i(glGetUniformLocation(program_, "uDiffuse"), 1);
            glUniform1i(glGetUniformLocation(program_, "uSaturate"), 2);
            glUniform1i(glGetUniformLocation(program_, "uShadowTextureArray"), 3);
            glUniform1i(glGetUniformLocation(program_, "uShadowCount"), static_cast<GLint>(projectedShadows_.size()));
            glUniform1i(glGetUniformLocation(program_, "uDynamicShadowPreviewEnabled"),
                        dynamicShadowPreviewEnabled_ ? 1 : 0);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTextureArray_);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, shadowBuffer_);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, globInstanceBuffer_);
            glUniform1f(glGetUniformLocation(program_, "uWorldShadow"), worldShadow_);
            glUniform1f(glGetUniformLocation(program_, "uWorldMidtone"), worldMidtone_);
            glUniform3fv(glGetUniformLocation(program_, "uCameraPosition"), 1, glm::value_ptr(camera.Position()));
            glUniform4fv(glGetUniformLocation(program_, "uFogColor"), 1, fogColor_);
            glUniform3f(glGetUniformLocation(program_, "uFogSettings"), fogNear_, fogFar_, fogMaximum_);
            glUniform1i(glGetUniformLocation(program_, "uFogType"), fogType);
            glUniform1i(glGetUniformLocation(program_, "uLightCount"), static_cast<GLint>(lights_.size()));
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lightBuffer_);
            glBindVertexArray(vao_);
            const GLint alphaPassLocation = glGetUniformLocation(program_, "uAlphaPass");
            const GLint alphaCutoffLocation = glGetUniformLocation(program_, "uAlphaCutoff");
            const GLint projectedPassLocation = glGetUniformLocation(program_, "uProjectedVolumeFinalPass");
            const GLint projectedColorLocation = glGetUniformLocation(program_, "uProjectedVolumeColor");
            const GLint objectFogLocation = glGetUniformLocation(program_, "uObjectFog");
            const GLint objectAlphaLocation = glGetUniformLocation(program_, "uObjectAlpha");
            const GLint selectionOverlayLocation = glGetUniformLocation(program_, "uSelectionOverlay");
            const GLint selectionColorLocation = glGetUniformLocation(program_, "uSelectionColor");
            const GLint boneWeightPreviewLocation = glGetUniformLocation(program_, "uBoneWeightPreview");
            const GLint dynamicObjectLocation = glGetUniformLocation(program_, "uDynamicObject");
            const GLint objectCenterLocation = glGetUniformLocation(program_, "uObjectCenter");
            const GLint objectRadiusLocation = glGetUniformLocation(program_, "uObjectRadius");
            const GLint activeLightCountLocation = glGetUniformLocation(program_, "uActiveLightCount");
            const GLint activeLightIndicesLocation = glGetUniformLocation(program_, "uActiveLightIndices[0]");
            std::array<GLint, 256> activeLightIndices{};
            GLsizei activeLightCount = 0;
            for (std::size_t lightIndex = 0; lightIndex < lights_.size() && activeLightCount < 256; ++lightIndex)
            {
                bool zoneAccepted = true;
                if (bspCulling && lightIndex < lightOwnerIndices_.size())
                {
                    const std::int32_t owner = lightOwnerIndices_[lightIndex];
                    if (owner >= 0 && static_cast<std::size_t>(owner) < objects_.size())
                    {
                        const std::uint32_t lightZone = objects_[owner].zoneMask;
                        // light.cpp::PrepareSwLights has no zero-mask bypass.
                        zoneAccepted = (lightZone & cameraZoneMask_) == cameraZoneMask_;
                    }
                }
                if (zoneAccepted)
                    activeLightIndices[activeLightCount++] = static_cast<GLint>(lightIndex);
            }
            glUniform1i(activeLightCountLocation, activeLightCount);
            performanceDiagnostics_.activeLightCount = static_cast<std::size_t>(activeLightCount);
            performanceDiagnostics_.totalLightCount = lights_.size();
            if (activeLightCount != 0)
                glUniform1iv(activeLightIndicesLocation, activeLightCount, activeLightIndices.data());
            const GLint hasAmbientLocation = glGetUniformLocation(program_, "uHasAmbient");
            const GLint hasDiffuseLocation = glGetUniformLocation(program_, "uHasDiffuse");
            const GLint hasSaturateLocation = glGetUniformLocation(program_, "uHasSaturate");
            const GLint threeWayLocation = glGetUniformLocation(program_, "uThreeWay");
            const GLint uvOffsetLocation = glGetUniformLocation(program_, "uUvOffset");
            const GLint selfIlluminationLocation = glGetUniformLocation(program_, "uSelfIllumination");
            const GLint shadowReceiverLocation = glGetUniformLocation(program_, "uShadowReceiverMask");
            const GLint instanceModeLocation = glGetUniformLocation(program_, "uInstanceMode");
            glUniform1i(instanceModeLocation, 0);
            glUniform1i(projectedPassLocation, 0);
            glUniform1i(selectionOverlayLocation, 0);
            glUniform4fv(selectionColorLocation, 1, selectionColor);
            glUniform4f(projectedColorLocation, 1.0f, 1.0f, 1.0f, 1.0f);
            std::unordered_map<const BrxRenderBatch *, float> batchAlphaCache;
            batchAlphaCache.reserve(batches_.size() * 2);
            const auto objectAlphaForBatch = [&](const BrxRenderBatch &batch)
            {
                if (const auto cached = batchAlphaCache.find(&batch); cached != batchAlphaCache.end())
                    return cached->second;
                // Retail DYSH geometry is submitted only to RP_DynamicTexture;
                // it is not ordinary visible scenery. In helper-inspection mode
                // (preview disabled) expose the model so it can still be edited.
                if (dynamicShadowPreviewEnabled_ && batch.ownerObjectIndex >= 0 &&
                    static_cast<std::size_t>(batch.ownerObjectIndex) < objects_.size() &&
                    objects_[static_cast<std::size_t>(batch.ownerObjectIndex)].cid == 97)
                {
                    batchAlphaCache.emplace(&batch, 0.0f);
                    return 0.0f;
                }
                float mrdAlpha = mrdAlphaForBatch(batch);
                const auto isolatedAlpha = isolatedPreviewAlphas_.find(ResolveLogicalOwner(batch.ownerObjectIndex));
                const auto additionalAlpha = additionalPreviewAlphas_.find(ResolveLogicalOwner(batch.ownerObjectIndex));
                if (isolatedAlpha != isolatedPreviewAlphas_.end())
                    mrdAlpha *= isolatedAlpha->second;
                else if (additionalAlpha != additionalPreviewAlphas_.end())
                    mrdAlpha *= additionalAlpha->second;
                else if (IsInPreviewHierarchy(batch.ownerObjectIndex))
                    mrdAlpha *= previewObjectAlpha_;
                if (!batch.hasGleam)
                {
                    batchAlphaCache.emplace(&batch, mrdAlpha);
                    return mrdAlpha;
                }
                const glm::vec3 cameraRight = glm::vec3(cameraMatrix[0]);
                const glm::vec3 gleamNormal(batch.gleamNormal[0], batch.gleamNormal[1], batch.gleamNormal[2]);
                const float intensity = std::abs(glm::dot(gleamNormal, cameraRight));
                const float *curve = batch.gleamCurve;
                const float result =
                    mrdAlpha *
                    std::clamp(curve[0] + intensity * (curve[1] + intensity * (curve[2] + intensity * curve[3])), 0.0f,
                               1.0f);
                batchAlphaCache.emplace(&batch, result);
                return result;
            };
            std::array<GLuint, 3> boundMaterialTextures{(std::numeric_limits<GLuint>::max)(),
                                                        (std::numeric_limits<GLuint>::max)(),
                                                        (std::numeric_limits<GLuint>::max)()};
            const auto bindMaterialTexture = [&](const std::size_t unit, const GLuint texture)
            {
                if (boundMaterialTextures[unit] == texture)
                    return;
                glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
                glBindTexture(GL_TEXTURE_2D, texture);
                boundMaterialTextures[unit] = texture;
            };
            const auto drawBatch = [&](const BrxRenderBatch &batch, int alphaPass, float alphaCutoff,
                                       bool forceWhiteDiffuse = false, bool preserveProjectedColor = false,
                                       const glm::mat4 *modelOverride = nullptr, float alphaMultiplier = 1.0f,
                                       const std::vector<GlobInstance> *instances = nullptr,
                                       bool bypassSourceMrd = false)
            {
                const bool boneWeightPreview = batch.renderGroup == boneWeightPreviewRenderGroup_ &&
                                               batch.firstIndex == boneWeightPreviewFirstIndex_;
                const bool selectedHighlight = showSelectionHighlight && IsBatchSelected(batch) && !boneWeightPreview;
                const bool zoneOverlay = visibilityZoneColors_ && batch.zoneMask != 0u;
                glm::vec4 previewColor = glm::make_vec4(selectionColor);
                if (zoneOverlay && !selectedHighlight)
                {
                    std::uint32_t zone = 0;
                    while (zone < 31u && (batch.zoneMask & (1u << zone)) == 0u)
                        ++zone;
                    previewColor = VisibilityZoneColor(zone);
                }
                glUniform4fv(selectionColorLocation, 1, glm::value_ptr(previewColor));
                glUniform1i(selectionOverlayLocation, (selectedHighlight || zoneOverlay) ? 1 : 0);
                const glm::mat4 model = modelOverride != nullptr ? *modelOverride : rtckModelForBatch(batch);
                glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
                glUniform1i(dynamicObjectLocation, batch.dynamicLighting ? 1 : 0);
                glUniform1i(shadowReceiverLocation, batch.shadowReceiverMask);
                const glm::vec3 lightingCenter = glm::vec3(
                    model * glm::vec4(batch.boundsCenter[0], batch.boundsCenter[1], batch.boundsCenter[2], 1.0f));
                glUniform3fv(objectCenterLocation, 1, glm::value_ptr(lightingCenter));
                const float lightingRadius =
                    batch.boundsRadius * (std::max)({glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                                                     glm::length(glm::vec3(model[2])), 0.0001f});
                glUniform1f(objectRadiusLocation, lightingRadius);
                // ProjectCane builds a compact light list once per GLOB before
                // vertex submission. The old editor passed the entire zone
                // list and repeated the same sphere rejection for every
                // vertex, across every material pass. Keep the broad list for
                // multi-instance draws (each instance has a different sphere),
                // but submit only intersecting lights for ordinary GLOBs.
                std::array<GLint, 256> batchLightIndices{};
                GLsizei batchLightCount = 0;
                if (instances != nullptr && instances->size() > 1)
                {
                    batchLightCount = activeLightCount;
                    std::copy_n(activeLightIndices.begin(), static_cast<std::size_t>(activeLightCount),
                                batchLightIndices.begin());
                }
                else
                {
                    for (GLsizei activeIndex = 0; activeIndex < activeLightCount; ++activeIndex)
                    {
                        const GLint lightIndex = activeLightIndices[static_cast<std::size_t>(activeIndex)];
                        if (lightIndex < 0 || static_cast<std::size_t>(lightIndex) >= lights_.size())
                            continue;
                        const GpuLight &light = lights_[static_cast<std::size_t>(lightIndex)];
                        bool intersects = light.kind == 0 || light.maximumDistance <= 0.0f;
                        if (!intersects)
                        {
                            const glm::vec3 centerToLight = light.position - lightingCenter;
                            const float reach = light.maximumDistance + lightingRadius;
                            intersects = glm::dot(centerToLight, centerToLight) <= reach * reach;
                        }
                        if (intersects)
                            batchLightIndices[static_cast<std::size_t>(batchLightCount++)] = lightIndex;
                    }
                }
                glUniform1i(activeLightCountLocation, batchLightCount);
                if (batchLightCount != 0)
                    glUniform1iv(activeLightIndicesLocation, batchLightCount, batchLightIndices.data());
                performanceDiagnostics_.submittedLightReferences += static_cast<std::size_t>(batchLightCount);
                ++performanceDiagnostics_.lightListBuildCount;
                performanceDiagnostics_.maximumBatchLightCount = (std::max)(
                    performanceDiagnostics_.maximumBatchLightCount, static_cast<std::size_t>(batchLightCount));
                if (!preserveProjectedColor)
                    glUniform4f(projectedColorLocation, 1.0f, 1.0f, 1.0f, 1.0f);
                // Cloned stock geometry (notably RenderFastShadow's OID 29)
                // is submitted at another object's position. Its source ALO
                // MRD is not part of that draw in ProjectCane; evaluating the
                // stock object's original location can incorrectly fade every
                // cloned shadow to zero.
                const float objectAlpha =
                    bypassSourceMrd ? alphaMultiplier : objectAlphaForBatch(batch) * alphaMultiplier;
                glUniform1f(objectFogLocation, batch.fogScale);
                glUniform1f(objectAlphaLocation, objectAlpha);
                GLuint ambientTexture = 0;
                GLuint diffuseTexture = 0;
                GLuint saturateTexture = 0;
                bool threeWay = false;
                glm::vec2 animationUv(0.0f);
                std::size_t animationFrame = 0;
                if (batch.shaderIndex < shaders_.size())
                {
                    const auto &shader = shaders_[batch.shaderIndex];
                    // ProjectCane resolves the GLOB SAA independently for every
                    // SUBGLOB through PsaiFromSaaShd.  A GLOB animation only
                    // overrides the material animation when its target OID
                    // matches this SUBGLOB's SHD; otherwise SHD::psaa remains
                    // the fallback.  Treating hasShaderAnimation as a blanket
                    // GLOB-wide override suppressed scrolling on unrelated
                    // SUBGLOB materials.
                    const auto animationTargetsShader = [&](const BrxShaderAnimationRecord &candidate)
                    {
                        return candidate.shaderOid == shader.oid || (candidate.kind == BrxShaderAnimationKind::Eyes &&
                                                                     candidate.otherShaderOid == shader.oid);
                    };
                    const bool globAnimationMatches = batch.hasShaderAnimation &&
                                                      batch.shaderAnimation.kind != BrxShaderAnimationKind::None &&
                                                      animationTargetsShader(batch.shaderAnimation);
                    const BrxShaderAnimationRecord *materialAnimation = nullptr;
                    // PostEyesLoad walks global SAAs in load order and assigns
                    // the EYES animation to the secondary SHD.  A later EYES
                    // owner wins, even when that SHD serialized its own SAA.
                    const auto eyesAnimation = eyesAnimationBySecondaryShaderOid_.find(shader.oid);
                    if (eyesAnimation != eyesAnimationBySecondaryShaderOid_.end())
                        materialAnimation = &eyesAnimation->second;
                    if (materialAnimation == nullptr && shader.animation.kind != BrxShaderAnimationKind::None &&
                        animationTargetsShader(shader.animation))
                        materialAnimation = &shader.animation;
                    const BrxShaderAnimationRecord *animation =
                        globAnimationMatches ? &batch.shaderAnimation : materialAnimation;
                    if (animation != nullptr)
                    {
                        const float t = static_cast<float>(glfwGetTime());
                        const auto positiveMod = [](float value, float modulus)
                        { return modulus > 0.0f ? std::fmod(std::fmod(value, modulus) + modulus, modulus) : value; };
                        const auto averageRange = [](float minimum, float maximum, float floor)
                        { return (std::max)(floor, (minimum + maximum) * 0.5f); };
                        const std::size_t frameCount =
                            (std::max)({shader.ambientBitmapIndices.size(), shader.diffuseBitmapIndices.size(),
                                        shader.saturateBitmapIndices.size()});
                        switch (animation->kind)
                        {
                        case BrxShaderAnimationKind::Loop:
                            if (frameCount > 0)
                            {
                                const float duration = averageRange(animation->values[0], animation->values[1], 0.001f);
                                const float pause = averageRange(animation->values[2], animation->values[3], 0.0f);
                                const float phase = positiveMod(t, duration + pause);
                                const std::int64_t initial = animation->initialFrame;
                                const std::size_t initialFrame =
                                    static_cast<std::size_t>((initial % static_cast<std::int64_t>(frameCount) +
                                                              static_cast<std::int64_t>(frameCount)) %
                                                             static_cast<std::int64_t>(frameCount));
                                const float activePhase = phase - pause;
                                const std::size_t advance =
                                    activePhase > 0.0f ? static_cast<std::size_t>(activePhase / duration * frameCount)
                                                       : 0;
                                animationFrame = (initialFrame + advance) % frameCount;
                            }
                            break;
                        case BrxShaderAnimationKind::PingPong:
                            if (frameCount > 1)
                            {
                                const float duration = averageRange(animation->values[0], animation->values[1], 0.001f);
                                const float pause = averageRange(animation->values[2], animation->values[3], 0.0f);
                                const float phase = positiveMod(t, duration + pause);
                                const std::size_t span = frameCount * 2;
                                const float activePhase = phase - pause;
                                const std::size_t step =
                                    activePhase > 0.0f ? static_cast<std::size_t>(activePhase / duration * span) % span
                                                       : 0;
                                const std::size_t pingPongFrame = step < frameCount ? step : span - step - 1;
                                const std::int64_t initial = animation->initialFrame;
                                const std::size_t initialFrame =
                                    static_cast<std::size_t>((initial % static_cast<std::int64_t>(frameCount) +
                                                              static_cast<std::int64_t>(frameCount)) %
                                                             static_cast<std::int64_t>(frameCount));
                                animationFrame = (initialFrame + pingPongFrame) % frameCount;
                            }
                            break;
                        case BrxShaderAnimationKind::Shuffle:
                            if (frameCount > 0)
                            {
                                const float pause =
                                    (std::max)(0.0f, (animation->values[0] + animation->values[1]) * 0.5f);
                                const std::uint64_t step =
                                    pause > 0.0f ? static_cast<std::uint64_t>(t / pause) : renderFrame_;
                                // Runtime chooses a random non-zero offset, so
                                // two adjacent choices may never be identical.
                                // Use a deterministic no-repeat sequence for
                                // an editor preview without mutating game data.
                                animationFrame = frameCount > 1 ? step % frameCount : 0;
                            }
                            break;
                        case BrxShaderAnimationKind::Eyes:
                        {
                            // PostEyesLoad drives both materials from the
                            // larger frame count, then SetSaiIframe clamps the
                            // requested frame independently for each SHD.
                            std::size_t eyesFrameCount = frameCount;
                            for (const auto &candidate : shaders_)
                            {
                                if (candidate.oid != animation->shaderOid && candidate.oid != animation->otherShaderOid)
                                    continue;
                                eyesFrameCount =
                                    (std::max)(eyesFrameCount, (std::max)({candidate.ambientBitmapIndices.size(),
                                                                           candidate.diffuseBitmapIndices.size(),
                                                                           candidate.saturateBitmapIndices.size()}));
                            }
                            if (eyesFrameCount > 1)
                            {
                                const float blink = (std::max)(0.001f, animation->values[0]);
                                const float open =
                                    (std::max)(0.0f, (animation->values[1] + animation->values[2]) * 0.5f);
                                const float cycle = open + blink;
                                const float phase = positiveMod(t, cycle);
                                if (phase > open)
                                {
                                    const float u = (phase - open) / blink;
                                    const float triangle = u < 0.5f ? u * 2.0f : (1.0f - u) * 2.0f;
                                    animationFrame = static_cast<std::size_t>(triangle * (eyesFrameCount - 1));
                                }
                            }
                            break;
                        }
                        case BrxShaderAnimationKind::Scroller:
                        {
                            float uVelocity = animation->values[0];
                            float vVelocity = animation->values[1];
                            float scrollerTime = t;
                            const auto conveyorScroller =
                                conveyorPrimaryScrollerRenderGroup_.find(conveyorPreviewObjectIndex_);
                            if (conveyorPreviewObjectIndex_ >= 0 &&
                                conveyorScroller != conveyorPrimaryScrollerRenderGroup_.end() &&
                                conveyorScroller->second == batch.renderGroup)
                            {
                                uVelocity = conveyorPreviewUVelocity_;
                                vVelocity = conveyorPreviewVVelocity_;
                                scrollerTime = (std::max)(0.0f, t - conveyorPreviewStartTime_);
                            }
                            const auto runtimeMod = [](float value, float modulus)
                            { return modulus != 0.0f ? std::fmod(value, modulus) : value; };
                            animationUv.x = runtimeMod(scrollerTime * uVelocity, animation->values[2]);
                            animationUv.y = runtimeMod(scrollerTime * vVelocity, animation->values[3]);
                            break;
                        }
                        case BrxShaderAnimationKind::Circler:
                            animationUv.x =
                                std::sin(t * animation->values[0]) * animation->values[1] + animation->values[2];
                            animationUv.y =
                                std::cos(t * animation->values[0]) * animation->values[1] + animation->values[3];
                            break;
                        default:
                            break; // Hologram and Looker require per-object render context.
                        }
                    }
                    const auto textureFor = [&](const std::vector<std::int32_t> &indices)
                    {
                        if (indices.empty())
                            return GLuint{};
                        // SetSaiIframe clamps rather than wraps when a shared
                        // EYES animation requests a frame beyond one linked
                        // shader's shorter frame list.
                        const auto textureIndex = indices[(std::min)(animationFrame, indices.size() - 1u)];
                        if (textureIndex < 0 || static_cast<std::size_t>(textureIndex) >= textures_.size())
                            return GLuint{};
                        return textures_[static_cast<std::size_t>(textureIndex)];
                    };
                    ambientTexture = textureFor(shader.ambientBitmapIndices);
                    diffuseTexture = textureFor(shader.diffuseBitmapIndices);
                    saturateTexture = textureFor(shader.saturateBitmapIndices);
                    if (diffuseTexture == 0)
                        diffuseTexture = ambientTexture;
                    threeWay = shader.kind == 0;
                }
                if (forceWhiteDiffuse)
                {
                    ambientTexture = 0;
                    diffuseTexture = 0;
                    saturateTexture = 0;
                    threeWay = false;
                }
                bindMaterialTexture(0, ambientTexture);
                bindMaterialTexture(1, diffuseTexture);
                bindMaterialTexture(2, saturateTexture);
                glUniform1i(hasAmbientLocation, ambientTexture != 0);
                glUniform1i(hasDiffuseLocation, diffuseTexture != 0);
                glUniform1i(hasSaturateLocation, saturateTexture != 0);
                glUniform1i(threeWayLocation, threeWay ? 1 : 0);
                glUniform2fv(uvOffsetLocation, 1, glm::value_ptr(animationUv));
                glUniform1f(selfIlluminationLocation, batch.selfIllumination);
                glUniform1i(alphaPassLocation, alphaPass);
                glUniform1f(alphaCutoffLocation, alphaCutoff);
                glUniform1i(boneWeightPreviewLocation, batch.renderGroup == boneWeightPreviewRenderGroup_ &&
                                                               batch.firstIndex == boneWeightPreviewFirstIndex_
                                                           ? 1
                                                           : 0);
                if (instances != nullptr && instances->size() > 1)
                {
                    glBindBuffer(GL_SHADER_STORAGE_BUFFER, globInstanceBuffer_);
                    glBufferData(GL_SHADER_STORAGE_BUFFER,
                                 static_cast<GLsizeiptr>(instances->size() * sizeof(GlobInstance)), instances->data(),
                                 GL_STREAM_DRAW);
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, globInstanceBuffer_);
                    glUniform1i(instanceModeLocation, 1);
                    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(batch.indexCount), GL_UNSIGNED_INT,
                                            reinterpret_cast<const void *>(
                                                static_cast<std::uintptr_t>(batch.firstIndex) * sizeof(std::uint32_t)),
                                            static_cast<GLsizei>(instances->size()));
                    glUniform1i(instanceModeLocation, 0);
                }
                else
                {
                    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(batch.indexCount), GL_UNSIGNED_INT,
                                   reinterpret_cast<const void *>(static_cast<std::uintptr_t>(batch.firstIndex) *
                                                                  sizeof(std::uint32_t)));
                }
            };
            const auto ownedRenderGroupKey = [](const BrxRenderBatch &item)
            {
                return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(item.ownerObjectIndex)) << 32) |
                       static_cast<std::uint64_t>(item.renderGroup);
            };
            const GLint celViewProjectionLocation = glGetUniformLocation(celProgram_, "uViewProjection");
            const GLint celModelLocation = glGetUniformLocation(celProgram_, "uModel");
            const GLint celColorLocation = glGetUniformLocation(celProgram_, "uCelColor");
            const GLint celFirstEdgeLocation = glGetUniformLocation(celProgram_, "uFirstEdge");
            const auto drawCelPhase = [&](std::uint8_t celPhase)
            {
                if (!renderCelBorders || celBatches_.empty())
                    return;
                glUseProgram(celProgram_);
                glUniformMatrix4fv(celViewProjectionLocation, 1, GL_FALSE, glm::value_ptr(viewProjection));
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, celEdgeBuffer_);
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                // Match ProjectCane's authored cel-border raster width. The
                // scene is rendered into the viewport-sized target and then
                // presented through ImGui, so preserve the same render-to-
                // presentation-height compensation used by ProjectCane.
                const float celBorderRasterScale =
                    static_cast<float>(height) / static_cast<float>((std::max)(presentHeight, 1));
                glLineWidth(4.125f * celBorderRasterScale);
                if (celPhase == 5)
                {
                    glEnable(GL_STENCIL_TEST);
                    glStencilMask(0x40);
                    glStencilFunc(GL_ALWAYS, 0x40, 0x40);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                }
                for (const auto &cel : celBatches_)
                {
                    // Only phase 14 can receive an alpha-promoted outline from
                    // another authored cel phase. Reject unrelated authored
                    // phases before hierarchy, transform and frustum work in
                    // the opaque cel passes.
                    if (celPhase != 14 && cel.renderPhase != celPhase)
                        continue;
                    if (!CelEditorVisible(cel))
                        continue;
                    if (bspCulling && !ObjectHierarchyPassesVisibility(cel.ownerObjectIndex))
                        continue;
                    if (visibilityIsolationMask_ != 0u && (cel.zoneMask & visibilityIsolationMask_) == 0u)
                        continue;
                    if (cel.ownerObjectIndex >= 0 && static_cast<std::size_t>(cel.ownerObjectIndex) < objects_.size() &&
                        objects_[static_cast<std::size_t>(cel.ownerObjectIndex)].hidden)
                        continue;
                    if (bspCulling && cel.zoneMask != 0 && (cel.zoneMask & cameraZoneMask_) != cameraZoneMask_)
                        continue;
                    const glm::mat4 celPlacement = SubmissionTransformForObject(cel.ownerObjectIndex);
                    const glm::vec3 center =
                        glm::vec3(celPlacement * glm::vec4(cel.center[0], cel.center[1], cel.center[2], 1.0f));
                    const float celRadius = cel.radius * (std::max)({glm::length(glm::vec3(celPlacement[0])),
                                                                     glm::length(glm::vec3(celPlacement[1])),
                                                                     glm::length(glm::vec3(celPlacement[2]))});
                    if (frustumCulling)
                    {
                        bool outside = false;
                        for (const auto &plane : frustumPlanes)
                        {
                            if (glm::dot(glm::vec3(plane), center) + plane.w < -celRadius)
                            {
                                outside = true;
                                break;
                            }
                        }
                        if (outside)
                            continue;
                    }
                    const std::uint64_t surfaceKey =
                        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cel.ownerObjectIndex)) << 32) |
                        static_cast<std::uint64_t>(cel.renderGroup);
                    const auto surfaceEntry = surfaceByOwnerGroup_.find(surfaceKey);
                    const BrxRenderBatch *surface =
                        surfaceEntry != surfaceByOwnerGroup_.end() ? surfaceEntry->second : nullptr;
                    glm::mat4 celModel = surface != nullptr ? rtckModelForBatch(*surface) : identityModel;
                    if (surface == nullptr)
                        celModel = SubmissionTransformForObject(cel.ownerObjectIndex);
                    glUniformMatrix4fv(celModelLocation, 1, GL_FALSE, glm::value_ptr(celModel));
                    const float surfaceAlpha = surface != nullptr
                                                   ? objectAlphaForBatch(*surface)
                                                   : inheritedObjectMrdAlpha(cel.ownerObjectIndex, false);
                    float alpha = surfaceAlpha;
                    if (surface == nullptr)
                    {
                        const auto isolatedAlpha =
                            isolatedPreviewAlphas_.find(ResolveLogicalOwner(cel.ownerObjectIndex));
                        const auto additionalAlpha =
                            additionalPreviewAlphas_.find(ResolveLogicalOwner(cel.ownerObjectIndex));
                        if (isolatedAlpha != isolatedPreviewAlphas_.end())
                            alpha *= isolatedAlpha->second;
                        else if (additionalAlpha != additionalPreviewAlphas_.end())
                            alpha *= additionalAlpha->second;
                        else if (IsInPreviewHierarchy(cel.ownerObjectIndex))
                            alpha *= previewObjectAlpha_;
                    }
                    alpha *= inheritedCelBorderMrdAlpha(cel.ownerObjectIndex);
                    if (cel.ownerObjectIndex >= 0 && static_cast<std::size_t>(cel.ownerObjectIndex) < objects_.size())
                    {
                        // Honor finite packet MRDs directly. The runtime can
                        // change the owner's effective MRDS after loading, but
                        // these authored limits remain the correct editor
                        // preview for ordinary and cel-only GLOB packets.
                        if (surface == nullptr && cel.surfaceMaximumDistance < 1.0e9f)
                            alpha *= insideMrdAlpha(center, celRadius, cel.surfaceMaximumDistance);
                        if (cel.maximumDistance < 1.0e9f)
                            alpha *= insideMrdAlpha(center, celRadius, cel.maximumDistance);
                    }
                    if (alpha <= 0.0f)
                        continue;
                    // RenderAloGlobset selects the cel queue from the surface
                    // RPL after surface-alpha promotion. A shorter cel MRD can
                    // fade the outline without moving an opaque-after-volume
                    // border into the translucent-cel queue.
                    std::uint8_t effectiveCelPhase = cel.renderPhase;
                    if (surfaceAlpha < 0.999999f && surface != nullptr)
                    {
                        switch (surface->renderPhase)
                        {
                        case 3: // RP_Opaque
                        case 4: // RP_Cutout
                        case 7: // RP_OpaqueAfterProjVolume
                        case 8: // RP_CutoutAfterProjVolume
                            effectiveCelPhase = 14;
                            break;
                        default:
                            break;
                        }
                    }
                    if (effectiveCelPhase != celPhase)
                        continue;
                    glUniform4f(celColorLocation, cel.color[0], cel.color[1], cel.color[2],
                                cel.color[3] * std::clamp(alpha, 0.0f, 1.0f));
                    glUniform1ui(celFirstEdgeLocation, cel.firstEdge);
                    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(cel.edgeCount * 2));
                }
                if (celPhase == 5)
                {
                    glStencilMask(0xFF);
                    glDisable(GL_STENCIL_TEST);
                }
                glDisable(GL_BLEND);
                glDepthFunc(GL_LESS);
                glUseProgram(program_);
                glBindVertexArray(vao_);
            };

            // Execute the BRX render priorities in the same order and with the
            // same fixed-function state used by ProjectCane::RenderRpl.
            // Upload keeps this list stable-sorted by render priority, so one
            // traversal preserves RenderRpl order without rescanning every
            // batch nineteen times per frame.
            // RenderAloGlobset promotes alpha-faded opaque/cutout packets to
            // RP_Translucent before SubmitRpl. Keep frame-local copies so the
            // authored BRX phase remains intact while queue selection matches
            // ProjectCane for MRD, fader, gleam, and GLOBI alpha contributors.
            // Most packets retain their authored phase. Keep pointers to those
            // immutable packets and allocate a frame-local copy only for the
            // comparatively small set promoted into the translucent queue.
            // Copying every packet here made large worlds pay for thousands of
            // full BrxRenderBatch copies every frame without changing output.
            std::deque<BrxRenderBatch> promotedFrameBatches;
            std::vector<const BrxRenderBatch *> renderOrder;
            renderOrder.reserve(batches_.size());
            const auto batchVisible = [&](const BrxRenderBatch &candidate)
            {
                const bool outsideFrustum = frustumCulling && !sphereInFrustum(candidate);
                const bool outsideBsp =
                    bspCulling && candidate.zoneMask != 0 && (candidate.zoneMask & cameraZoneMask_) != cameraZoneMask_;
                return !outsideFrustum && !outsideBsp;
            };
            const auto groupedPhase = [](const int phase)
            {
                // Opaque phases 3/7 can be promoted to grouped translucent
                // phase 13 after MRD alpha is evaluated. Preserve every
                // member whenever one member of that source GLOB is visible.
                return phase == 1 || phase == 3 || phase == 4 || phase == 6 || phase == 7 || phase == 8 || phase == 13;
            };
            std::vector<std::uint8_t> eligibleBatches(batches_.size(), 0);
            std::unordered_set<std::uint64_t> visibleRenderGroups;
            visibleRenderGroups.reserve(batches_.size());
            for (std::size_t batchIndex = 0; batchIndex < batches_.size(); ++batchIndex)
            {
                const BrxRenderBatch &batch = batches_[batchIndex];
                if ((bspCulling && !ObjectHierarchyPassesVisibility(batch.ownerObjectIndex)) ||
                    !BatchEditorVisible(batch) ||
                    (batch.ownerObjectIndex >= 0 &&
                     static_cast<std::size_t>(batch.ownerObjectIndex) < objects_.size() &&
                     objects_[static_cast<std::size_t>(batch.ownerObjectIndex)].hidden) ||
                    laserTemplateObjectSet_.contains(batch.ownerObjectIndex))
                    continue;
                eligibleBatches[batchIndex] = 1;
                if (groupedPhase(batch.renderPhase) && batchVisible(batch))
                    visibleRenderGroups.insert(ownedRenderGroupKey(batch));
            }
            for (std::size_t batchIndex = 0; batchIndex < batches_.size(); ++batchIndex)
            {
                if (eligibleBatches[batchIndex] == 0)
                    continue;
                const BrxRenderBatch &batch = batches_[batchIndex];
                if (visibilityIsolationMask_ != 0u && (batch.zoneMask & visibilityIsolationMask_) == 0u)
                    continue;
                const bool spatiallyVisible = groupedPhase(batch.renderPhase)
                                                  ? visibleRenderGroups.contains(ownedRenderGroupKey(batch))
                                                  : batchVisible(batch);
                if (!spatiallyVisible)
                {
                    ++culledBatchCount_;
                    continue;
                }
                const float alpha = objectAlphaForBatch(batch);
                if (alpha > 0.0f)
                {
                    const BrxRenderBatch *submitted = &batch;
                    if (alpha < 0.999999f)
                    {
                        switch (batch.renderPhase)
                        {
                        case 3: // RP_Opaque
                        case 4: // RP_Cutout
                        case 7: // RP_OpaqueAfterProjVolume
                        case 8: // RP_CutoutAfterProjVolume
                            promotedFrameBatches.push_back(batch);
                            promotedFrameBatches.back().renderPhase = 13; // RP_Translucent
                            promotedFrameBatches.back().translucentSort = true;
                            submitted = &promotedFrameBatches.back();
                            break;
                        case 5: // RP_CelBorder
                        case 9: // RP_CelBorderAfterProjVolume
                            promotedFrameBatches.push_back(batch);
                            promotedFrameBatches.back().renderPhase = 14; // RP_TranslucentCelBorder
                            promotedFrameBatches.back().translucentSort = true;
                            submitted = &promotedFrameBatches.back();
                            break;
                        default:
                            break;
                        }
                    }
                    renderOrder.push_back(submitted);
                }
                else
                    ++culledBatchCount_;
            }
            std::stable_sort(
                renderOrder.begin(), renderOrder.end(),
                [&](const BrxRenderBatch *left, const BrxRenderBatch *right)
                {
                    if (left->renderPhase != right->renderPhase)
                        return left->renderPhase < right->renderPhase;
                    // SubmitRpl separates unsorted/additive cutouts from
                    // sorted alpha cutouts. Background has the same split.
                    const bool sortablePhase = left->renderPhase == 1 || left->renderPhase == 4 ||
                                               left->renderPhase == 8 || left->renderPhase == 13;
                    if (!sortablePhase)
                        return false;
                    if (left->renderPhase != 13 && left->translucentSort != right->translucentSort)
                        return !left->translucentSort; // retail renders this queue first
                    if (left->renderPhase != 13 && !left->translucentSort)
                        return false;
                    const auto sortZ = [&](const BrxRenderBatch *item)
                    {
                        if (item->hasExplicitSortZ)
                            return item->explicitSortZ;
                        const glm::vec3 sortCenter =
                            glm::vec3(SubmissionTransformForObject(item->ownerObjectIndex) *
                                      glm::vec4(item->sortCenter[0], item->sortCenter[1], item->sortCenter[2], 1.0f));
                        const glm::vec3 delta = sortCenter - camera.Position();
                        return glm::dot(delta, delta);
                    };
                    return sortZ(left) > sortZ(right);
                });
            // Parsed GLOB groups are normally remapped globally, but cloned and
            // instanced packets can intentionally retain a source group. Keep
            // the owner in the queue key so one instance cannot suppress or
            // inherit another instance's queue/alpha state.
            // Index grouped packets once. The old path scanned the complete
            // render queue for every first group member and then linearly
            // searched submitted-group vectors. Large retail worlds therefore
            // performed quadratic CPU work every frame.
            std::unordered_map<std::uint64_t, std::vector<const BrxRenderBatch *>> volumeGroups;
            std::unordered_map<std::uint64_t, std::unordered_map<int, std::vector<const BrxRenderBatch *>>>
                sortedGroups;
            volumeGroups.reserve(renderOrder.size());
            sortedGroups.reserve(renderOrder.size());
            for (const BrxRenderBatch *candidate : renderOrder)
            {
                const std::uint64_t key = ownedRenderGroupKey(*candidate);
                if (candidate->renderPhase == 6)
                    volumeGroups[key].push_back(candidate);
                else if (candidate->renderPhase == 1 || candidate->renderPhase == 4 || candidate->renderPhase == 8 ||
                         candidate->renderPhase == 13)
                    sortedGroups[key][candidate->renderPhase].push_back(candidate);
            }
            std::unordered_set<std::uint64_t> submittedVolumeGroups;
            std::unordered_set<std::uint64_t> submittedSortedGroups;
            submittedVolumeGroups.reserve(volumeGroups.size());
            submittedSortedGroups.reserve(sortedGroups.size());
            // Retail worlds heavily reuse static scenery.  The BRX keeps each
            // placement as a distinct GLOB instance, but issuing every
            // SUBGLOB placement as a separate OpenGL draw makes dense levels
            // CPU/driver bound (A Grave Undertaking exceeded 12,000 draws in
            // one frame).  Batch only state-identical opaque packets; sorted,
            // alpha-tested, animated, selected, gleam and camera-tracked
            // packets retain their exact per-object submission path.  The
            // non-translucent background queue is eligible as well: RenderDoc
            // measurements show that this is the dominant repeated-draw path
            // in dense worlds.  It is handled inside RP_Background below so
            // every SUBGLOB in an owning GLOB remains represented.
            const auto opaqueInstanceKey = [](const BrxRenderBatch &item)
            {
                std::uint64_t key = item.firstIndex;
                const auto mix = [&](const std::uint64_t value)
                { key ^= value + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2); };
                mix(item.indexCount);
                mix(item.shaderIndex);
                mix(item.renderPhase);
                mix(item.shadowReceiverMask);
                mix(item.dynamicLighting ? 1u : 0u);
                std::uint32_t bits = 0;
                std::memcpy(&bits, &item.selfIllumination, sizeof(bits));
                mix(bits);
                std::memcpy(&bits, &item.fogScale, sizeof(bits));
                mix(bits);
                return key;
            };
            const auto opaqueInstanceCompatible = [](const BrxRenderBatch &left, const BrxRenderBatch &right)
            {
                return left.firstIndex == right.firstIndex && left.indexCount == right.indexCount &&
                       left.shaderIndex == right.shaderIndex && left.renderPhase == right.renderPhase &&
                       left.shadowReceiverMask == right.shadowReceiverMask &&
                       left.dynamicLighting == right.dynamicLighting &&
                       left.selfIllumination == right.selfIllumination && left.fogScale == right.fogScale;
            };
            std::unordered_map<std::uint64_t, std::vector<const BrxRenderBatch *>> opaqueInstanceGroups;
            opaqueInstanceGroups.reserve(renderOrder.size());
            for (const BrxRenderBatch *candidate : renderOrder)
            {
                const bool opaquePhase = candidate->renderPhase == 1 || candidate->renderPhase == 3 ||
                                         candidate->renderPhase == 7 || candidate->renderPhase == 18;
                if (!opaquePhase || candidate->rotationTracking != 0 ||
                    (candidate->renderPhase == 1 && candidate->translucentSort) || candidate->hasShaderAnimation ||
                    candidate->hasGleam || objectAlphaForBatch(*candidate) < 0.999999f ||
                    (showSelectionHighlight && IsBatchSelected(*candidate)) ||
                    (candidate->renderGroup == boneWeightPreviewRenderGroup_ &&
                     candidate->firstIndex == boneWeightPreviewFirstIndex_))
                    continue;
                auto &group = opaqueInstanceGroups[opaqueInstanceKey(*candidate)];
                if (!group.empty() && !opaqueInstanceCompatible(*group.front(), *candidate))
                    continue; // hash collision: preserve the ordinary draw path
                group.push_back(candidate);
            }
            std::unordered_map<const BrxRenderBatch *, std::uint64_t> opaqueInstanceMembership;
            opaqueInstanceMembership.reserve(renderOrder.size());
            for (const auto &[key, group] : opaqueInstanceGroups)
                if (group.size() > 1)
                    for (const BrxRenderBatch *member : group)
                        opaqueInstanceMembership.emplace(member, key);
            bool drewCelBorder = false;
            bool drewCelAfterProjection = false;
            bool drewTranslucentCel = false;
            for (const BrxRenderBatch *batchPointer : renderOrder)
            {
                std::vector<const BrxRenderBatch *> volumeMembers;
                std::vector<const BrxRenderBatch *> groupedMembers;
                const BrxRenderBatch &sourceBatch = *batchPointer;
                if (sourceBatch.renderPhase == 6)
                {
                    const std::uint64_t sourceGroupKey = ownedRenderGroupKey(sourceBatch);
                    if (submittedVolumeGroups.contains(sourceGroupKey))
                        continue;
                    submittedVolumeGroups.insert(sourceGroupKey);
                    bool volumeGroupVisible = false;
                    const auto indexedGroup = volumeGroups.find(sourceGroupKey);
                    if (indexedGroup != volumeGroups.end())
                        volumeMembers = indexedGroup->second;
                    for (const BrxRenderBatch *candidate : volumeMembers)
                    {
                        const bool memberOutsideFrustum = frustumCulling && !sphereInFrustum(*candidate);
                        const bool memberOutsideBsp = bspCulling && candidate->zoneMask != 0 &&
                                                      (candidate->zoneMask & cameraZoneMask_) != cameraZoneMask_;
                        if (!memberOutsideFrustum && !memberOutsideBsp)
                            volumeGroupVisible = true;
                    }
                    if (!volumeGroupVisible)
                    {
                        culledBatchCount_ += volumeMembers.size();
                        continue;
                    }
                    // A projected volume must remain a closed mesh. Once any
                    // member is visible, submit every subglob in the group to
                    // all stencil passes rather than frustum-culling pieces.
                    visibleBatchCount_ += volumeMembers.size();
                    batchPointer = volumeMembers.front();
                }
                else if (sourceBatch.renderPhase == 1 || sourceBatch.renderPhase == 4 || sourceBatch.renderPhase == 8 ||
                         sourceBatch.renderPhase == 13)
                {
                    // SubmitRpl queues and sorts one RPL per GLOB. DrawGlob
                    // then visits every SUBGLOB without allowing another
                    // GLOB to be interleaved between its materials.
                    const std::uint64_t sourceGroupKey = ownedRenderGroupKey(sourceBatch);
                    if (submittedSortedGroups.contains(sourceGroupKey))
                        continue;
                    submittedSortedGroups.insert(sourceGroupKey);
                    bool groupVisible = false;
                    const auto indexedGroup = sortedGroups.find(sourceGroupKey);
                    if (indexedGroup != sortedGroups.end())
                    {
                        const auto indexedPhase = indexedGroup->second.find(sourceBatch.renderPhase);
                        if (indexedPhase != indexedGroup->second.end())
                            groupedMembers = indexedPhase->second;
                    }
                    for (const BrxRenderBatch *candidate : groupedMembers)
                    {
                        const bool memberOutsideFrustum = frustumCulling && !sphereInFrustum(*candidate);
                        const bool memberOutsideBsp = bspCulling && candidate->zoneMask != 0 &&
                                                      (candidate->zoneMask & cameraZoneMask_) != cameraZoneMask_;
                        if (!memberOutsideFrustum && !memberOutsideBsp)
                            groupVisible = true;
                    }
                    if (!groupVisible)
                    {
                        culledBatchCount_ += groupedMembers.size();
                        continue;
                    }
                    visibleBatchCount_ += groupedMembers.size();
                    batchPointer = groupedMembers.front();
                }
                const BrxRenderBatch &batch = *batchPointer;
                const int phase = batch.renderPhase;
                if (!drewCelBorder && phase > 5)
                {
                    drawCelPhase(5);
                    drewCelBorder = true;
                }
                if (!drewCelAfterProjection && phase > 9)
                {
                    drawCelPhase(9);
                    drewCelAfterProjection = true;
                }
                if (!drewTranslucentCel && phase > 14)
                {
                    drawCelPhase(14);
                    drewTranslucentCel = true;
                }
                if (phase != 6 && groupedMembers.empty())
                {
                    const bool outsideFrustum = frustumCulling && !sphereInFrustum(batch);
                    const bool outsideBsp =
                        bspCulling && batch.zoneMask != 0 && (batch.zoneMask & cameraZoneMask_) != cameraZoneMask_;
                    if (outsideFrustum || outsideBsp)
                    {
                        ++culledBatchCount_;
                        continue;
                    }
                    ++visibleBatchCount_;
                }
                const auto standardBlend = [&]()
                { glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO); };
                const auto additiveBlend = [&]() { glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO); };
                if (const auto membership = opaqueInstanceMembership.find(&batch);
                    phase != 1 && membership != opaqueInstanceMembership.end())
                {
                    const auto group = opaqueInstanceGroups.find(membership->second);
                    if (group != opaqueInstanceGroups.end() && group->second.front() != &batch)
                        continue;
                    if (group != opaqueInstanceGroups.end())
                    {
                        std::vector<GlobInstance> instances;
                        instances.reserve(group->second.size());
                        for (const BrxRenderBatch *member : group->second)
                        {
                            GlobInstance instance;
                            instance.model = rtckModelForBatch(*member);
                            instance.objectCenter =
                                instance.model * glm::vec4(member->boundsCenter[0], member->boundsCenter[1],
                                                           member->boundsCenter[2], 1.0f);
                            instance.objectCenter.w =
                                member->boundsRadius * (std::max)({glm::length(glm::vec3(instance.model[0])),
                                                                   glm::length(glm::vec3(instance.model[1])),
                                                                   glm::length(glm::vec3(instance.model[2])), 0.0001f});
                            instances.push_back(instance);
                        }
                        glDisable(GL_BLEND);
                        glDepthFunc(GL_LESS);
                        glDepthMask(GL_TRUE);
                        drawBatch(batch, 0, 0.0f, false, false, nullptr, 1.0f, &instances);
                        continue;
                    }
                }
                switch (phase)
                {
                case 0: // RP_DynamicTexture: off-screen producer in game.
                    break;
                case 1: // RP_Background
                    // RenderRpl keeps opaque and alpha background RPLs in
                    // separate queues.  fTransluscentSort is populated by
                    // FillGlobset when any SHD in the owning GLOB uses
                    // grfshd 2; do not force opaque/additive sky geometry
                    // through source-alpha blending.
                    if (batch.translucentSort)
                    {
                        glEnable(GL_BLEND);
                        standardBlend();
                    }
                    else
                    {
                        glDisable(GL_BLEND);
                    }
                    // Editor map geometry must still respect the scene
                    // depth buffer.  GL_ALWAYS made backdrop/map batches
                    // paint over objects that were actually in front.
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_FALSE);
                    for (const BrxRenderBatch *member : groupedMembers)
                    {
                        const auto membership = opaqueInstanceMembership.find(member);
                        if (!batch.translucentSort && membership != opaqueInstanceMembership.end())
                        {
                            const auto group = opaqueInstanceGroups.find(membership->second);
                            if (group == opaqueInstanceGroups.end() || group->second.front() != member)
                                continue;

                            std::vector<GlobInstance> instances;
                            instances.reserve(group->second.size());
                            for (const BrxRenderBatch *instanceBatch : group->second)
                            {
                                GlobInstance instance;
                                instance.model = rtckModelForBatch(*instanceBatch);
                                instance.objectCenter =
                                    instance.model * glm::vec4(instanceBatch->boundsCenter[0],
                                                               instanceBatch->boundsCenter[1],
                                                               instanceBatch->boundsCenter[2], 1.0f);
                                instance.objectCenter.w =
                                    instanceBatch->boundsRadius *
                                    (std::max)({glm::length(glm::vec3(instance.model[0])),
                                                glm::length(glm::vec3(instance.model[1])),
                                                glm::length(glm::vec3(instance.model[2])), 0.0001f});
                                instances.push_back(instance);
                            }
                            drawBatch(*member, 0, 0.0f, false, false, nullptr, 1.0f, &instances);
                            continue;
                        }
                        drawBatch(*member, 0, 0.0f);
                    }
                    break;
                case 2: // RP_BlotContext (SetGlobDraw selects DrawTranslucent)
                    glEnable(GL_BLEND);
                    standardBlend();
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_TRUE);
                    drawBatch(batch, 1, 0.9f);
                    glDepthMask(GL_FALSE);
                    drawBatch(batch, 2, 0.9f);
                    glDepthMask(GL_TRUE);
                    break;
                case 3:  // RP_Opaque
                case 7:  // RP_OpaqueAfterProjVolume
                case 18: // RP_Max
                    glDisable(GL_BLEND);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_TRUE);
                    drawBatch(batch, 0, 0.0f);
                    break;
                case 4: // RP_Cutout
                case 8: // RP_CutoutAfterProjVolume
                    glEnable(GL_BLEND);
                    // SubmitRpl chooses this queue from GLOB::fTransluscentSort;
                    // DrawGlob still chooses per-SUBGLOB blending for RP 13.
                    batch.translucentSort ? standardBlend() : additiveBlend();
                    glDepthFunc(GL_LESS);
                    for (const BrxRenderBatch *member : groupedMembers)
                    {
                        glDepthMask(GL_TRUE);
                        drawBatch(*member, 1, 0.9f);
                        glDepthMask(GL_FALSE);
                        drawBatch(*member, 2, 0.9f);
                    }
                    glDepthMask(GL_TRUE);
                    break;
                case 5:  // RP_CelBorder
                case 9:  // RP_CelBorderAfterProjVolume
                case 14: // RP_TranslucentCelBorder
                    // Rendered by the dedicated edge pass once EDGE payloads
                    // have been retained; never draw these as filled triangles.
                    break;
                case 6: // RP_ProjVolume
                {
                    // RenderRpl clears stencil once at frame start and then
                    // lets DrawProjVolume's front/back passes maintain bit
                    // 7.  Bit 6 belongs to the earlier cel-border pass.
                    const std::uint8_t volumeFlags = batch.projectedFlags;
                    const auto packedVolumeChannel = [](float channel)
                    {
                        const int source = std::clamp(static_cast<int>(channel * 255.0f + 0.5f), 0, 255);
                        return static_cast<float>((source + 1) >> 1);
                    };
                    const auto setProjectedVolumeColor = [&](const BrxRenderBatch &member)
                    {
                        const BrxShaderRecord *memberShader =
                            member.shaderIndex < shaders_.size() ? &shaders_[member.shaderIndex] : nullptr;
                        if (memberShader == nullptr)
                        {
                            glUniform4f(projectedColorLocation, 1.0f, 1.0f, 1.0f, 1.0f);
                            return;
                        }
                        const float packedAlpha = packedVolumeChannel(memberShader->volumeRgba[3]);
                        const float objectAlpha = objectAlphaForBatch(member);
                        const float fadedAlpha = std::floor(packedAlpha * objectAlpha);
                        const float compensatedAlpha = objectAlpha > 0.0f ? (fadedAlpha / 128.0f) / objectAlpha : 0.0f;
                        glUniform4f(projectedColorLocation, packedVolumeChannel(memberShader->volumeRgba[0]) / 255.0f,
                                    packedVolumeChannel(memberShader->volumeRgba[1]) / 255.0f,
                                    packedVolumeChannel(memberShader->volumeRgba[2]) / 255.0f, compensatedAlpha);
                    };
                    // DrawGlob invokes DrawProjVolume once per SUBGLOB.  Each
                    // subglob completes its own stencil construction and
                    // projected-color pass before the next one is visited.
                    for (const BrxRenderBatch *member : volumeMembers)
                    {
                        setProjectedVolumeColor(*member);
                        const auto drawVolumeMember = [&]()
                        {
                            // DrawGlob binds white when grfshd's texture bit
                            // is clear. This decision is per SHD/SUBGLOB;
                            // g_grfshd (volumeFlags) only controls the
                            // GLOB-level stencil sequence.
                            const BrxShaderRecord *memberShader =
                                member->shaderIndex < shaders_.size() ? &shaders_[member->shaderIndex] : nullptr;
                            const bool forceWhite = memberShader == nullptr || (memberShader->flags & 2u) == 0;
                            drawBatch(*member, 0, 0.0f, forceWhite, true);
                        };

                        glEnable(GL_BLEND);
                        glEnable(GL_STENCIL_TEST);
                        glDepthMask(GL_FALSE);
                        glDepthFunc(GL_LESS);
                        glStencilMask(0x80);
                        glStencilFunc(GL_ALWAYS, 0x80, 0x80);
                        glUniform1i(projectedPassLocation, 0);

                        if (volumeFlags == 0 || volumeFlags == 2)
                        {
                            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                            glFrontFace(GL_CW);
                            // Exact DrawProjVolume front-face packet:
                            // grfshd 0 clears on stencil/depth failure,
                            // grfshd 2 preserves those pixels.
                            glStencilOp(volumeFlags == 2 ? GL_KEEP : GL_ZERO, GL_REPLACE,
                                        volumeFlags == 2 ? GL_KEEP : GL_ZERO);
                            if (volumeFlags == 2)
                                additiveBlend();
                            else
                                glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
                            drawVolumeMember();

                            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                            glFrontFace(GL_CCW);
                            glStencilOp(GL_KEEP, volumeFlags == 2 ? GL_KEEP : GL_ZERO, GL_KEEP);
                            glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
                            drawVolumeMember();
                        }
                        else
                        {
                            glUniform1i(projectedPassLocation, 2);
                            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                            glFrontFace(GL_CW);
                            glStencilOp(GL_ZERO, GL_REPLACE, GL_ZERO);
                            if (volumeFlags == 3)
                                additiveBlend();
                            else
                                glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
                            drawVolumeMember();

                            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                            glFrontFace(GL_CCW);
                            glStencilOp(GL_KEEP, GL_ZERO, GL_KEEP);
                            if ((volumeFlags & 2) != 0)
                                additiveBlend();
                            else
                                glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
                            drawVolumeMember();
                        }

                        // ProjectCane's DrawProjVolume uses GL_ALWAYS for
                        // the final color packet. The two preceding
                        // front/back depth passes have already reduced the
                        // closed volume to stencil bit 7; testing the cone
                        // mesh against scene depth again makes the final
                        // packet disappear behind the very world surfaces
                        // it is supposed to illuminate (most visibly the
                        // SMARTGUARD FLASH child).
                        glDepthFunc(GL_ALWAYS);
                        glStencilFunc(GL_EQUAL, 0x80, 0xC0);
                        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                        glFrontFace(GL_CW);
                        // ProjectCane uses ordinary alpha for 0/2 and
                        // additive alpha for 1/3 final packets.
                        if (volumeFlags == 0 || volumeFlags == 2)
                            standardBlend();
                        else
                            additiveBlend();
                        glUniform1i(projectedPassLocation, 1);
                        drawVolumeMember();
                        glUniform1i(projectedPassLocation, 0);
                    }

                    glStencilMask(0xFF);
                    glDisable(GL_STENCIL_TEST);
                    glDepthMask(GL_TRUE);
                    glDepthFunc(GL_LESS);
                    glFrontFace(GL_CCW);
                    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glDisable(GL_STENCIL_TEST);
                    break;
                }
                case 10: // RP_MurkClear
                    glEnable(GL_BLEND);
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_FALSE);
                    glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ONE, GL_ZERO);
                    glFrontFace(GL_CW);
                    drawBatch(batch, 0, 0.0f);
                    glFrontFace(GL_CCW);
                    standardBlend();
                    drawBatch(batch, 0, 0.0f);
                    break;
                case 11: // RP_MurkOpaque
                    glDisable(GL_BLEND);
                    glDepthFunc(GL_LESS);
                    glDepthMask(GL_TRUE);
                    drawBatch(batch, 1, 0.1f);
                    break;
                case 12: // RP_MurkFill
                    glEnable(GL_BLEND);
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_FALSE);
                    glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA, GL_ONE, GL_ZERO);
                    drawBatch(batch, 0, 0.0f);
                    standardBlend();
                    drawBatch(batch, 0, 0.0f);
                    break;
                case 13: // RP_Translucent
                    glEnable(GL_BLEND);
                    glDepthFunc(GL_LESS);
                    for (const BrxRenderBatch *member : groupedMembers)
                    {
                        const BrxShaderRecord *memberShader =
                            member->shaderIndex < shaders_.size() ? &shaders_[member->shaderIndex] : nullptr;
                        const bool memberAlphaBlend =
                            memberShader != nullptr && (memberShader->flags == 2 || memberShader->flags == 6);
                        memberAlphaBlend ? standardBlend() : additiveBlend();
                        glDepthMask(GL_TRUE);
                        drawBatch(*member, 1, 0.9f);
                        glDepthMask(GL_FALSE);
                        drawBatch(*member, 2, 0.9f);
                    }
                    glDepthMask(GL_TRUE);
                    break;
                case 15: // RP_Blip uses a separate procedural stream.
                    break;
                case 16: // RP_Foreground
                case 17: // RP_WorldMap
                    if (phase == 17)
                    {
                        glEnable(GL_BLEND);
                        standardBlend();
                    }
                    else
                        glDisable(GL_BLEND);
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_FALSE);
                    drawBatch(batch, 0, 0.0f);
                    break;
                default:
                    break;
                }
            }
            if (!drewCelBorder)
                drawCelPhase(5);
            if (!drewCelAfterProjection)
                drawCelPhase(9);
            if (!drewTranslucentCel)
                drawCelPhase(14);

            // RenderFastShadow uses psw->aploStock[17]. Stock BRX objects are
            // OIDs 0x0c..0x2a, therefore slot 17 is OID 29. Submit that exact
            // model with ProjectCane's translation-only, radius/depth scale.
            if (fastShadowModel_ >= 0 && !fastShadowBatchIndices_.empty())
            {
                const glm::mat4 sourceWorld = ResolvedObjectTransform(fastShadowModel_);
                for (const auto &shadow : fastShadowSources_)
                {
                    // SubmissionTransformForObject is a delta over vertices
                    // that are already authored in world space. Its column 3
                    // is therefore commonly zero. RenderFastShadow uses the
                    // owner's actual current world position.
                    const glm::vec3 position(PreviewedObjectTransform(shadow.ownerObjectIndex)[3]);
                    const glm::mat4 desired =
                        glm::translate(glm::mat4(1), position) *
                        glm::scale(glm::mat4(1),
                                   glm::vec3(shadow.radius * 0.01f, shadow.radius * 0.01f, shadow.depth * 0.01f));
                    const glm::mat4 delta = desired * glm::inverse(sourceWorld);
                    for (const std::size_t batchIndex : fastShadowBatchIndices_)
                    {
                        if (batchIndex >= batches_.size())
                            continue;
                        const auto &batch = batches_[batchIndex];
                        const glm::mat4 model = delta * rtckModelForBatch(batch);
                        drawBatch(batch, 0, 0.0f, false, false, &model, 1.0f, nullptr, true);
                    }
                }
            }

            // LASEN is not a static model. RenderLasenSelf converts every child
            // SHAPE segment into a camera-facing instance of OID 650 (sense) or
            // OID 651 (damage) via RenderAloLine. The editor previews the normal
            // sense state, matching an enabled, non-triggered retail laser.
            glUseProgram(program_);
            glBindVertexArray(vao_);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_FALSE);
            for (const auto &beam : laserBeams_)
            {
                if (beam.ownerObjectIndex < 0 || static_cast<std::size_t>(beam.ownerObjectIndex) >= objects_.size())
                    continue;
                glm::mat4 lasenWorld(1.0f);
                std::memcpy(glm::value_ptr(lasenWorld),
                            objects_[static_cast<std::size_t>(beam.ownerObjectIndex)].worldTransform,
                            sizeof(float) * 16);
                std::int32_t templateObject = -1;
                for (const std::int32_t candidate : laserTemplateObjects_)
                {
                    if (objects_[static_cast<std::size_t>(candidate)].oid != 650)
                        continue;
                    std::int32_t ancestor = objects_[static_cast<std::size_t>(candidate)].parentIndex;
                    while (ancestor >= 0 && objects_[static_cast<std::size_t>(ancestor)].cid != 49)
                        ancestor = objects_[static_cast<std::size_t>(ancestor)].parentIndex;
                    if (ancestor == beam.ownerObjectIndex)
                    {
                        templateObject = candidate;
                        break;
                    }
                }
                // RenderLasenSelf falls back to psw->aploStock[0x0b] when
                // SnipAloObjects did not find a private OID 650 model.  Proxy
                // instances in the elevator shaft use that path.
                if (templateObject < 0)
                {
                    for (const std::int32_t candidate : laserTemplateObjects_)
                    {
                        if (objects_[static_cast<std::size_t>(candidate)].oid == 650)
                        {
                            templateObject = candidate;
                            break;
                        }
                    }
                }
                if (templateObject < 0)
                    continue;

                glm::mat4 templateWorld(1.0f);
                std::memcpy(glm::value_ptr(templateWorld),
                            objects_[static_cast<std::size_t>(templateObject)].worldTransform, sizeof(float) * 16);
                const glm::mat4 inverseTemplate = glm::inverse(templateWorld);
                for (std::size_t point = 1; point < beam.points.size(); ++point)
                {
                    const auto &local0 = beam.points[point - 1];
                    const auto &local1 = beam.points[point];
                    const glm::vec3 p0 = glm::vec3(lasenWorld * glm::vec4(local0[0], local0[1], local0[2], 1.0f));
                    const glm::vec3 p1 = glm::vec3(lasenWorld * glm::vec4(local1[0], local1[1], local1[2], 1.0f));
                    const glm::vec3 direction = p1 - p0;
                    glm::vec3 axis1 = glm::cross(p0 - camera.Position(), direction);
                    const float axis1Length = glm::length(axis1);
                    if (axis1Length <= 0.01f)
                        continue;
                    axis1 /= axis1Length;
                    glm::vec3 axis0 = glm::cross(axis1, direction);
                    const float axis0Length = glm::length(axis0);
                    if (axis0Length < 0.0001f)
                        continue;
                    axis0 /= axis0Length;

                    glm::mat4 lineModel(1.0f);
                    lineModel[0] = glm::vec4(axis0, 0.0f);
                    lineModel[1] = glm::vec4(axis1, 0.0f); // retail rWidth = 1.0
                    lineModel[2] = glm::vec4(direction * 0.01f, 0.0f);
                    lineModel[3] = glm::vec4(p0, 1.0f);
                    const glm::mat4 model = lineModel * inverseTemplate;
                    for (const auto &batch : batches_)
                    {
                        if (batch.ownerObjectIndex != templateObject)
                            continue;
                        const BrxShaderRecord *shader =
                            batch.shaderIndex < shaders_.size() ? &shaders_[batch.shaderIndex] : nullptr;
                        const bool alphaBlend = shader != nullptr && (shader->flags == 2 || shader->flags == 6);
                        if (alphaBlend)
                            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
                        else
                            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
                        if (batch.renderPhase == 4 || batch.renderPhase == 13)
                        {
                            glDepthMask(GL_TRUE);
                            drawBatch(batch, 1, 0.9f, false, false, &model);
                            glDepthMask(GL_FALSE);
                            drawBatch(batch, 2, 0.9f, false, false, &model);
                        }
                        else
                        {
                            glDepthMask(GL_FALSE);
                            drawBatch(batch, 0, 0.0f, false, false, &model);
                        }
                    }
                }
            }
            // RIP particles render an authored ALO instead of a billboard. The
            // source vertices are already in world space, so move them back
            // through the source transform and submit a scaled particle copy.
            for (const auto &particle : emitterPreview_.Particles())
            {
                if (!particle.modelParticle || particle.renderObjectIndex < 0 || particle.color.a <= 0.0001f ||
                    std::abs(particle.scale) <= 0.0001f)
                    continue;
                const glm::mat4 sourceWorld = ResolvedObjectTransform(particle.renderObjectIndex);
                glm::mat3 particleBasis = particle.orientation;
                const float speedSquared = glm::dot(particle.velocity, particle.velocity);
                if ((particle.ripType == 19 || particle.ripType == 23) && speedSquared > 0.0001f)
                {
                    const glm::vec3 forward = glm::normalize(particle.velocity);
                    if (particle.ripType == 19)
                    {
                        glm::vec3 axisY = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), forward);
                        if (glm::dot(axisY, axisY) < 0.0001f)
                            axisY = glm::vec3(0.0f, 1.0f, 0.0f);
                        else
                            axisY = glm::normalize(axisY);
                        particleBasis = glm::mat3(forward, axisY, glm::normalize(glm::cross(forward, axisY)));
                    }
                    else
                    {
                        glm::vec3 axisZ = glm::cross(forward, glm::normalize(glm::vec3(cameraMatrix[0])));
                        if (glm::dot(axisZ, axisZ) < 0.0001f)
                            axisZ = glm::vec3(0.0f, 0.0f, 1.0f);
                        else
                            axisZ = glm::normalize(axisZ);
                        particleBasis = glm::mat3(forward, glm::normalize(glm::cross(axisZ, forward)), axisZ);
                    }
                }
                const glm::vec3 renderPosition = particle.position + glm::vec3(0.0f, 0.0f, particle.renderHeightOffset);
                const glm::mat4 particleWorld = glm::translate(glm::mat4(1.0f), renderPosition) *
                                                glm::mat4(particleBasis) *
                                                glm::scale(glm::mat4(1.0f), glm::vec3(particle.scale));
                const glm::mat4 cloneDelta = particleWorld * glm::inverse(sourceWorld);
                for (const auto &batch : batches_)
                {
                    if (!IsInObjectHierarchy(batch.ownerObjectIndex, particle.renderObjectIndex))
                        continue;
                    const glm::mat4 model = cloneDelta * rtckModelForBatch(batch);
                    drawBatch(batch, 0, 0.0f, false, false, &model, std::clamp(particle.color.a, 0.0f, 1.0f));
                }
            }
            // Editor-safe representation of RIPT_Trail / RIPT_SmokeTrail.
            // Retail draws a stock ALO stretched between the tracked RIP and
            // the trail head. The editor uses a translucent segment because
            // the BRX does not serialize psw->aploStock[] bindings.
            std::vector<CollisionVertex> ripLineVertices;
            ripLineVertices.reserve(emitterPreview_.Particles().size() * 2 + editorReferenceLinks_.size() * 2);
            for (const auto &particle : emitterPreview_.Particles())
            {
                if (!particle.modelParticle)
                    continue;
                glm::vec3 lineStart{}, lineEnd{};
                bool drawLine = false;
                if (particle.trailType == 10 || particle.trailType == 13)
                {
                    lineStart = particle.previousPosition;
                    lineEnd = particle.position;
                    drawLine = true;
                }
                else if (particle.ripType == 9 || particle.ripType == 17)
                {
                    const float tailDuration = (std::min)(particle.age, particle.ripType == 9 ? 0.05f : 0.15f);
                    lineStart = particle.position - particle.velocity * tailDuration;
                    lineEnd = particle.position;
                    drawLine = true;
                }
                else if (particle.ripType == 23 && particle.renderObjectIndex < 0 &&
                         glm::dot(particle.velocity, particle.velocity) > 0.0001f)
                {
                    lineStart = particle.position;
                    lineEnd = particle.position + glm::normalize(particle.velocity) * 100.0f;
                    drawLine = true;
                }
                else if (particle.ripType == 8 && glm::dot(particle.position - particle.previousPosition,
                                                           particle.position - particle.previousPosition) > 100.0f)
                {
                    lineStart = particle.previousPosition;
                    lineEnd = particle.position;
                    drawLine = true;
                }
                if (!drawLine)
                    continue;
                const glm::vec3 delta = lineEnd - lineStart;
                if (glm::dot(delta, delta) <= 0.0001f)
                    continue;
                CollisionVertex first{}, second{};
                first.position[0] = lineStart.x;
                first.position[1] = lineStart.y;
                first.position[2] = lineStart.z;
                second.position[0] = lineEnd.x;
                second.position[1] = lineEnd.y;
                second.position[2] = lineEnd.z;
                const float alpha = std::clamp(particle.color.a, 0.0f, 1.0f);
                const float trailColor[4] = {particle.trailType == 13 ? 0.72f : 1.0f,
                                             particle.trailType == 13 ? 0.78f : 0.72f,
                                             particle.trailType == 13 ? 0.82f : 0.25f, alpha};
                std::copy_n(trailColor, 4, first.color);
                std::copy_n(trailColor, 4, second.color);
                ripLineVertices.push_back(first);
                ripLineVertices.push_back(second);
            }
            for (const auto &link : editorReferenceLinks_)
            {
                CollisionVertex first{}, second{};
                first.position[0] = link[0].x;
                first.position[1] = link[0].y;
                first.position[2] = link[0].z;
                second.position[0] = link[1].x;
                second.position[1] = link[1].y;
                second.position[2] = link[1].z;
                const float linkColor[4]{0.20f, 0.85f, 1.0f, 0.90f};
                std::copy_n(linkColor, 4, first.color);
                std::copy_n(linkColor, 4, second.color);
                ripLineVertices.push_back(first);
                ripLineVertices.push_back(second);
            }
            if (!ripLineVertices.empty())
            {
                glBindVertexArray(collisionVao_);
                glBindBuffer(GL_ARRAY_BUFFER, collisionVertexBuffer_);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(ripLineVertices.size() * sizeof(CollisionVertex)),
                             ripLineVertices.data(), GL_STREAM_DRAW);
                glUseProgram(collisionProgram_);
                glUniformMatrix4fv(glGetUniformLocation(collisionProgram_, "uViewProjection"), 1, GL_FALSE,
                                   glm::value_ptr(viewProjection));
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                glLineWidth(2.0f);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(ripLineVertices.size()));
                glLineWidth(1.0f);
                glDepthMask(GL_TRUE);
                glBindVertexArray(vao_);
                glUseProgram(program_);
            }
            glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(identityModel));
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glUniform1i(selectionOverlayLocation, 0);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glFrontFace(GL_CCW);
            glDisable(GL_BLEND);
            glActiveTexture(GL_TEXTURE0);
            glBindVertexArray(0);
            glUseProgram(0);
        }

        performanceDiagnostics_.sceneGeometryMs = (glfwGetTime() - sceneGeometryStart) * 1000.0;
        const double collisionStart = glfwGetTime();
        if (showCollision && !collisionTriangles.empty())
        {
            static constexpr float groupPalette[][4]{{1.0f, 0.353f, 0.353f, 0.863f}, {0.353f, 0.863f, 1.0f, 0.863f},
                                                     {0.471f, 1.0f, 0.471f, 0.863f}, {1.0f, 0.824f, 0.314f, 0.863f},
                                                     {0.824f, 0.431f, 1.0f, 0.863f}, {1.0f, 0.510f, 0.824f, 0.863f},
                                                     {0.392f, 1.0f, 0.863f, 0.863f}, {1.0f, 0.627f, 0.314f, 0.863f}};
            std::vector<CollisionVertex> normalVertices;
            std::vector<CollisionVertex> selectedVertices;
            normalVertices.reserve(collisionTriangles.size() * 6);
            selectedVertices.reserve(collisionTriangles.size());
            const auto appendEdge =
                [](std::vector<CollisionVertex> &vertices, const float *start, const float *end, const float *color)
            {
                CollisionVertex a{}, b{};
                std::copy_n(start, 3, a.position);
                std::copy_n(end, 3, b.position);
                std::copy_n(color, 4, a.color);
                std::copy_n(color, 4, b.color);
                vertices.push_back(a);
                vertices.push_back(b);
            };
            for (const auto &triangle : collisionTriangles)
            {
                if (triangle.cameraGeometry ? !showCameraCollision : !showGameplayCollision)
                    continue;
                const std::int32_t logicalOwner = ResolveLogicalOwner(triangle.ownerObjectIndex);
                if (collisionOwnerFilter != (std::numeric_limits<std::int32_t>::min)() &&
                    (collisionOwnerFilter < 0 || logicalOwner != collisionOwnerFilter))
                    continue;
                const bool selected = logicalOwner == selectedObjectIndex_ || IsObjectSelected(logicalOwner);
                float color[4]{};
                std::copy_n(triangle.cameraGeometry ? cameraCollisionColor : gameplayCollisionColor, 4, color);
                if (!triangle.cameraGeometry && logicalOwner >= 0 &&
                    static_cast<std::size_t>(logicalOwner) < objects_.size())
                {
                    const auto &collisionOwner = objects_[static_cast<std::size_t>(logicalOwner)];
                    int groupIndex = -1;
                    if (collisionColorMode == 1 && collisionOwner.so.present)
                    {
                        for (std::size_t nodeIndex = 0; nodeIndex < collisionOwner.so.collisionGeometry.bspNodes.size();
                             ++nodeIndex)
                        {
                            if (collisionOwner.so.collisionGeometry.bspNodes[nodeIndex].surfaceIndex !=
                                triangle.surfaceIndex)
                                continue;
                            if (nodeIndex < collisionOwner.so.bspNodeToCameraGroup.size())
                                groupIndex = collisionOwner.so.bspNodeToCameraGroup[nodeIndex];
                            break;
                        }
                    }
                    else if (collisionColorMode == 2 && collisionOwner.so.present &&
                             triangle.surfaceIndex + 1u < collisionOwner.so.surfaceToHitSurfaceGroup.size())
                    {
                        const int begin = collisionOwner.so.surfaceToHitSurfaceGroup[triangle.surfaceIndex];
                        const int end = collisionOwner.so.surfaceToHitSurfaceGroup[triangle.surfaceIndex + 1u];
                        groupIndex = end > begin ? begin : -1;
                    }
                    if (groupIndex >= 0)
                        std::copy_n(groupPalette[static_cast<std::size_t>(groupIndex) % std::size(groupPalette)], 4,
                                    color);
                }
                if (selected && collisionColorMode == 0)
                {
                    color[0] = 1.0f;
                    color[1] = 0.824f;
                    color[2] = 0.157f;
                    color[3] = 0.922f;
                }
                auto &destination = selected ? selectedVertices : normalVertices;
                const glm::mat4 placement = SubmissionTransformForObject(triangle.ownerObjectIndex);
                float transformed[3][3]{};
                for (int pointIndex = 0; pointIndex < 3; ++pointIndex)
                {
                    const glm::vec3 point = glm::vec3(placement * glm::vec4(triangle.positions[pointIndex][0],
                                                                            triangle.positions[pointIndex][1],
                                                                            triangle.positions[pointIndex][2], 1.0f));
                    transformed[pointIndex][0] = point.x;
                    transformed[pointIndex][1] = point.y;
                    transformed[pointIndex][2] = point.z;
                }
                appendEdge(destination, transformed[0], transformed[1], color);
                appendEdge(destination, transformed[1], transformed[2], color);
                appendEdge(destination, transformed[2], transformed[0], color);
            }
            normalVertices.insert(normalVertices.end(), selectedVertices.begin(), selectedVertices.end());
            glBindVertexArray(collisionVao_);
            glBindBuffer(GL_ARRAY_BUFFER, collisionVertexBuffer_);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(normalVertices.size() * sizeof(CollisionVertex)),
                         normalVertices.data(), GL_STREAM_DRAW);
            glUseProgram(collisionProgram_);
            const glm::mat4 collisionViewProjection =
                ProjectionMatrix(static_cast<float>(width) / static_cast<float>(height)) * camera.ViewMatrix();
            glUniformMatrix4fv(glGetUniformLocation(collisionProgram_, "uViewProjection"), 1, GL_FALSE,
                               glm::value_ptr(collisionViewProjection));
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            const GLsizei normalCount = static_cast<GLsizei>(normalVertices.size() - selectedVertices.size());
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, 0, normalCount);
            if (!selectedVertices.empty())
            {
                glLineWidth(2.0f);
                glDrawArrays(GL_LINES, normalCount, static_cast<GLsizei>(selectedVertices.size()));
            }
            glLineWidth(1.0f);
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glBindVertexArray(vao_);
            glUseProgram(program_);
        }

        performanceDiagnostics_.collisionMs = (glfwGetTime() - collisionStart) * 1000.0;
        const double emitterStart = glfwGetTime();
        if (!emitterPreview_.Particles().empty())
        {
            std::unordered_map<std::uint32_t, std::vector<BlipInstance>> batches;
            for (const auto &particle : emitterPreview_.Particles())
            {
                if (particle.modelParticle)
                {
                    ++emitterRenderDiagnostics_.modelParticles;
                    continue;
                }
                const std::uint32_t key = static_cast<std::uint16_t>(particle.shaderOid) |
                                          (particle.additive ? 0x10000u : 0u) |
                                          (static_cast<std::uint32_t>(particle.textureFrame) << 17);
                batches[key].push_back(
                    {particle.position,
                     particle.color,
                     {particle.age, particle.lifetime, particle.roll, particle.scale},
                     {particle.velocity.x * particle.flyingScale, particle.velocity.y * particle.flyingScale,
                      particle.velocity.z * particle.flyingScale, static_cast<float>(particle.movementKind)}});
            }
            glBindVertexArray(blipVao_);
            glUseProgram(blipProgram_);
            const glm::mat4 viewProjection =
                ProjectionMatrix(static_cast<float>(width) / static_cast<float>(height)) * camera.ViewMatrix();
            const glm::mat4 cameraMatrix = glm::inverse(camera.ViewMatrix());
            glUniformMatrix4fv(glGetUniformLocation(blipProgram_, "uViewProjection"), 1, GL_FALSE,
                               glm::value_ptr(viewProjection));
            glUniform3fv(glGetUniformLocation(blipProgram_, "uCameraRight"), 1,
                         glm::value_ptr(glm::vec3(cameraMatrix[0])));
            glUniform3fv(glGetUniformLocation(blipProgram_, "uCameraUp"), 1,
                         glm::value_ptr(glm::vec3(cameraMatrix[1])));
            glUniform1f(glGetUniformLocation(blipProgram_, "uFlyingScale"), 1.0f / 60.0f);
            glUniform1i(glGetUniformLocation(blipProgram_, "uTexture"), 0);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            for (const auto &[key, instances] : batches)
            {
                const std::int16_t shaderOid = static_cast<std::int16_t>(key & 0xffffu);
                const std::uint32_t textureFrame = key >> 17;
                GLuint texture = 0;
                auto shader = std::find_if(shaders_.begin(), shaders_.end(),
                                           [&](const BrxShaderRecord &value) { return value.oid == shaderOid; });
                if (shader == shaders_.end())
                    shader = std::find_if(shaders_.begin(), shaders_.end(),
                                          [](const BrxShaderRecord &value) { return value.oid == 0; });
                if (shader != shaders_.end())
                {
                    const auto frameTexture = [&](const std::vector<std::int32_t> &indices)
                    {
                        if (indices.empty())
                            return GLuint{};
                        const std::int32_t bitmap = indices[textureFrame % indices.size()];
                        return bitmap >= 0 && static_cast<std::size_t>(bitmap) < textures_.size()
                                   ? textures_[static_cast<std::size_t>(bitmap)]
                                   : GLuint{};
                    };
                    // DrawBlip uses CTK_Ambient only for SHDK_ThreeWay.  For
                    // every other shader kind it uses the first diffuse map.
                    // Merely checking whether the ambient vector is nonempty
                    // is wrong because PCBRX still stores a -1 placeholder.
                    texture = shader->kind == 0 ? frameTexture(shader->ambientBitmapIndices)
                                                : frameTexture(shader->diffuseBitmapIndices);
                }
                // DrawBlip returns without submitting a group when its first
                // TEX has no usable bitmap.  A white fallback changes missing
                // data into giant opaque ribbons and hides the real problem.
                if (texture == 0)
                {
                    emitterRenderDiagnostics_.rejectedMissingTexture += instances.size();
                    continue;
                }
                emitterRenderDiagnostics_.submittedBlips += instances.size();
                glBindBuffer(GL_ARRAY_BUFFER, blipInstanceBuffer_);
                glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instances.size() * sizeof(BlipInstance)),
                             instances.data(), GL_STREAM_DRAW);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texture);
                glBlendFunc(GL_SRC_ALPHA, (key & 0x10000u) != 0u ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
                glDrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, static_cast<GLsizei>(instances.size()));
            }
            glDisable(GL_BLEND);
            glEnable(GL_CULL_FACE);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glBindVertexArray(vao_);
            glUseProgram(program_);
        }

        // ProjectCane presents g_sceneFbo as an opaque image.  The editor
        // instead gives this RGBA attachment to ImGui, whose image shader
        // composites the attachment alpha a second time.  RenderRpl's blend
        // state deliberately stores source alpha (ONE, ZERO) in the scene
        // target, so without this presentation step translucent windows,
        // atmosphere cards, and projected-volume pixels reveal the dark ImGui
        // panel even though their framebuffer RGB is already composited.
        // Preserve that RGB and make only the completed image alpha opaque.
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        performanceDiagnostics_.emittersMs = (glfwGetTime() - emitterStart) * 1000.0;
        glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (measureGpu)
        {
            glEndQuery(GL_TIME_ELAPSED);
            gpuTimerPending_[gpuTimerIndex] = true;
            gpuTimerWriteIndex_ = (gpuTimerIndex + 1) % gpuTimerQueries_.size();
        }
        performanceDiagnostics_.totalRenderCpuMs = (glfwGetTime() - renderCpuStart) * 1000.0;
        return colorTexture_;
    }

    [[nodiscard]] std::size_t VisibleBatchCount() const
    {
        return visibleBatchCount_;
    }
    [[nodiscard]] std::size_t CulledBatchCount() const
    {
        return culledBatchCount_;
    }
    [[nodiscard]] std::size_t WarpBindingCount() const
    {
        return globWarps_.size();
    }
    [[nodiscard]] std::size_t ResolvedWarpBindingCount() const
    {
        return static_cast<std::size_t>(std::count_if(globWarps_.begin(), globWarps_.end(),
                                                      [&](const BrxGlobWarpRecord &binding)
                                                      { return FindWrController(binding) != nullptr; }));
    }
    [[nodiscard]] std::size_t WarpOperatorCount() const
    {
        return std::accumulate(wrControllers_.begin(), wrControllers_.end(), std::size_t{0},
                               [](std::size_t count, const WrController &controller)
                               { return count + controller.operators.size(); });
    }
    [[nodiscard]] std::size_t WarpedVertexCount() const
    {
        return warpedVertexCount_;
    }
    [[nodiscard]] float MaximumWarpDisplacement() const
    {
        return maximumWarpDisplacement_;
    }
    [[nodiscard]] std::uint32_t CameraZoneMask() const
    {
        return cameraZoneMask_;
    }
    void ResetVisibilityCamera()
    {
        cameraZoneMask_ = VisibilityMap::FallbackMask;
    }
    [[nodiscard]] std::size_t ZonePassingBatchCount() const
    {
        return static_cast<std::size_t>(
            std::count_if(batches_.begin(), batches_.end(),
                          [&](const auto &batch)
                          {
                              return ObjectHierarchyPassesVisibility(batch.ownerObjectIndex) &&
                                     (batch.zoneMask == 0u || (batch.zoneMask & cameraZoneMask_) == cameraZoneMask_);
                          }));
    }

  private:
    struct GpuLight
    {
        int kind = 0;
        bool dynamic = false;
        bool excludeDynamicObjects = false;
        glm::vec3 visibilityPosition{};
        glm::vec3 visibilityDirection{0.0f, 0.0f, 1.0f};
        float visibilityDistance = 2000.0f;
        float visibilityConeAngle = 60.0f;
        glm::vec3 position{};
        glm::vec3 direction{0.0f, 0.0f, 1.0f};
        glm::vec3 color{};
        glm::vec3 ru{};
        glm::vec3 du{};
        glm::vec2 falloff{200.0f, 2000.0f};
        float maximumDistance = 2000.0f;
        glm::vec2 cone{};
        glm::mat4 frustum{1.0f};
        glm::vec4 frustumScale{};
        glm::vec4 frustumBias{1.0f};
    };

    struct alignas(16) GpuLightBlock
    {
        glm::ivec4 meta{};
        glm::vec4 position{};
        glm::vec4 direction{};
        glm::vec4 color{};
        glm::vec4 ru{};
        glm::vec4 du{};
        glm::vec4 falloffCone{};
        glm::mat4 frustum{1.0f};
        glm::vec4 falloffScale{};
        glm::vec4 falloffBias{1.0f};
    };

    // Exact std430 projection payload consumed by the GLOB shadow path.
    struct alignas(16) GpuProjectedShadow
    {
        glm::mat4 worldToUv{1.0f};
        glm::vec4 rgba{1.0f};
        glm::vec4 depth{}; // wMin, wMax, wFadeMin, dynamic
        glm::ivec4 meta{}; // texture layer, SHDK, clamp S, clamp T
        glm::vec4 sphere{};
    };

    template <class Record>
    static void UploadShaderStorageRecords(const GLuint buffer, const std::vector<Record> &records, const GLenum usage)
    {
        // A trailing unsized shader array still requires one element's backing
        // storage. Keep the logical light/shadow/instance counts unchanged.
        const Record emptyRecord{};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>((std::max)(records.size(), std::size_t{1}) * sizeof(Record)),
                     records.empty() ? static_cast<const void *>(&emptyRecord)
                                     : static_cast<const void *>(records.data()),
                     usage);
    }

    void CopyShadowTextureLayer(GLuint source, int layer)
    {
        GLint width = 0, height = 0;
        glBindTexture(GL_TEXTURE_2D, source);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        if (width <= 0 || height <= 0)
            return;

        // Retail texture uploads are available to the shadow copier as
        // renderable RGBA images. The editor retains DXT-compressed source
        // textures, which cannot portably be attached to a read framebuffer;
        // that made the copy silently fail and left static shadow layers
        // transparent. Read the source through GL's decompressor and resize
        // it once on the CPU during scene upload.
        std::vector<std::uint8_t> sourcePixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, sourcePixels.data());
        std::vector<std::uint8_t> layerPixels(static_cast<std::size_t>(shadowResolution_) * shadowResolution_ * 4u);
        for (int y = 0; y < shadowResolution_; ++y)
        {
            const int sourceY =
                (std::min)(height - 1, static_cast<int>((static_cast<std::int64_t>(y) * height) / shadowResolution_));
            for (int x = 0; x < shadowResolution_; ++x)
            {
                const int sourceX =
                    (std::min)(width - 1, static_cast<int>((static_cast<std::int64_t>(x) * width) / shadowResolution_));
                const std::size_t sourceOffset = (static_cast<std::size_t>(sourceY) * width + sourceX) * 4u;
                const std::size_t destinationOffset = (static_cast<std::size_t>(y) * shadowResolution_ + x) * 4u;
                std::copy_n(sourcePixels.data() + sourceOffset, 4, layerPixels.data() + destinationOffset);
            }
        }
        glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTextureArray_);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, shadowResolution_, shadowResolution_, 1, GL_RGBA,
                        GL_UNSIGNED_BYTE, layerPixels.data());
    }

    void BuildProjectedShadows()
    {
        projectedShadows_.clear();
        dynamicShadowCasters_.clear();
        dynamicShadowLastModels_.clear();
        dynamicShadowMapsDirty_ = true;
        struct Source
        {
            int owner = -1;
            short shader = 0x2c, dysh = -1;
            float nr = -1, fr = -1, nc = 100, fc = 400;
            glm::vec3 up{0, 1, 0};
            bool live = false;
        };
        std::vector<Source> sources;
        for (std::size_t i = 0; i < objects_.size(); ++i)
        {
            Source s;
            s.owner = static_cast<int>(i);
            for (const auto &p : objects_[i].properties)
            {
                if (p.eopid == 43)
                {
                    s.live = p.boolValue;
                    continue;
                }
                if (p.eopid < 44 || p.eopid > 51)
                    continue;
                s.live = true;
                switch (p.eopid)
                {
                case 44:
                    s.shader = p.intValue == -1 ? static_cast<short>(0x2c) : static_cast<short>(p.intValue);
                    break;
                case 45:
                    s.nr = p.floatValue[0];
                    break;
                case 46:
                    s.fr = p.floatValue[0];
                    break;
                case 47:
                    s.nc = p.floatValue[0];
                    break;
                case 48:
                    s.fc = p.floatValue[0];
                    break;
                case 49:
                {
                    float t = std::tan(p.floatValue[0] * 0.008726647f);
                    s.nr = t * s.nc;
                    s.fr = t * s.fc;
                    break;
                }
                case 50:
                    s.up = glm::make_vec3(p.floatValue);
                    break;
                case 51:
                    s.dysh = static_cast<short>(p.intValue);
                    break;
                default:
                    break;
                }
            }
            if (s.live)
                sources.push_back(s);
        }
        GLint maxLayers = 0;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
        const int count = std::min<int>(64, std::min<int>(maxLayers, static_cast<int>(sources.size())));
        if (count <= 0)
            return;
        glGenTextures(1, &shadowTextureArray_);
        glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTextureArray_);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, shadowResolution_, shadowResolution_, count);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        for (int layer = 0; layer < count; ++layer)
        {
            auto s = sources[layer];
            // PostShadowLoad supplies these exact defaults. They are authored
            // projection dimensions, not bounds inferred from the caster mesh.
            if (s.nr < 0 && s.fr < 0)
            {
                s.nr = 100.0f;
                s.fr = 400.0f;
            }
            else if (s.nr < 0)
                s.nr = s.fr;
            else if (s.fr < 0)
                s.fr = s.nr;
            auto sh = std::find_if(shaders_.begin(), shaders_.end(),
                                   [&](const BrxShaderRecord &v) { return v.oid == s.shader; });
            if (sh == shaders_.end())
                continue;
            const glm::mat4 owner = ResolvedObjectTransform(s.owner);
            const glm::vec3 pos(owner[3]);
            // UpdateAloXfWorldHierarchy only follows the owner's -Z/Y basis for
            // SHDK 3. SHDK 2 retains InitShadow's world -Z and the authored up.
            glm::vec3 normal = sh->kind == 3 ? -glm::vec3(owner[2]) : glm::vec3(0, 0, -1);
            glm::vec3 up = sh->kind == 3 ? glm::vec3(owner[1]) : s.up;
            normal = glm::length(normal) > 0.0001f ? glm::normalize(normal) : glm::vec3(0, 0, -1);
            up = glm::length(up) > 0.0001f ? glm::normalize(up) : glm::vec3(0, 1, 0);
            glm::vec3 side = glm::cross(up, normal);
            if (glm::length(side) < 0.0001f)
                side = glm::cross(std::abs(normal.z) < 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0), normal);
            side = glm::normalize(side);
            glm::mat3 look(normal, side, glm::cross(normal, side));
            glm::vec3 eye = pos;
            glm::mat4 proj(1);
            GpuProjectedShadow out{};
            if (std::abs(s.nr - s.fr) >= 0.0001f)
            {
                float eo = (s.fr * s.nc - s.nr * s.fc) / (s.fr - s.nr), zn = s.nc - eo, zf = s.fc - eo;
                eye += normal * eo;
                proj = glm::mat4(0);
                float scale = 0.4f / (s.nr / zn), zs = (zn + zf) / (zn - zf);
                proj[0][0] = scale;
                proj[1][1] = scale;
                proj[2][0] = proj[2][1] = 0.5f;
                proj[2][2] = zs;
                proj[2][3] = 1;
                proj[3][2] = zn * (1 - zs);
                out.depth = glm::vec4(std::min(zn, zf), std::max(zn, zf), std::min(zn, zf) - 50, 0);
            }
            else
            {
                proj[0][0] = proj[1][1] = 0.5f / std::max(s.nr, 0.0001f);
                proj[3][0] = proj[3][1] = 0.5f;
                out.depth =
                    glm::vec4(-(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(), s.nc - 50, 0);
            }
            glm::mat4 camera(1);
            camera[0] = glm::vec4(-look[1], 0);
            camera[1] = glm::vec4(-look[2], 0);
            camera[2] = glm::vec4(look[0], 0);
            camera[3] = glm::vec4(eye, 1);
            out.worldToUv = proj * glm::inverse(camera);
            float effectDistance = 0, effectRadius = 0;
            if (std::abs(s.nr - s.fr) >= 0.0001f)
            {
                const float dc = s.fc - s.nc, base = (s.nc * s.fr - s.fc * s.nr) / (s.nr - s.fr);
                const float h = std::sqrt(dc * dc + s.fr * s.fr), z1 = ((s.nc + base) * h) / dc;
                const float focus = (z1 * (z1 + 0.5f * h)) / (s.nc + base), z2 = (s.fc + base) - focus;
                effectRadius = std::sqrt(z2 * z2 + s.fr * s.fr);
                effectDistance = focus - base;
            }
            else
            {
                effectRadius = std::abs(s.fc - s.nc) * 0.5f;
                effectDistance = s.nc + effectRadius;
            }
            out.sphere = glm::vec4(pos + normal * effectDistance, effectRadius);
            out.rgba = glm::make_vec4(sh->rgba);
            const int textureFlags = sh->textureStages.empty() ? 0 : sh->textureStages[0].flags;
            out.meta = glm::ivec4(layer, sh->kind, (textureFlags & 1) != 0, (textureFlags & 2) != 0);
            out.depth.w = 0.0f;
            projectedShadows_.push_back(out);
            if (s.dysh != -1)
            {
                // EOPID 51 names the DYSH silhouette helper. ProjectCane's
                // RenderDyshSelf submits that helper's GLOBSET into the dynamic
                // texture; ordinary GLOBs receive it through GRFGLOB bits 0/1.
                std::int32_t helper = -1;
                int bestDistance = (std::numeric_limits<int>::max)(), bestSplice = 1;
                for (std::size_t oi = 0; oi < objects_.size(); ++oi)
                {
                    if (objects_[oi].oid != s.dysh)
                        continue;
                    auto chainDistance = [&](std::int32_t from, std::int32_t target)
                    {
                        int distance = 0;
                        for (std::size_t guard = 0; from >= 0 && static_cast<std::size_t>(from) < objects_.size() &&
                                                    guard++ <= objects_.size();
                             ++distance)
                        {
                            if (from == target)
                                return distance;
                            from = objects_[static_cast<std::size_t>(from)].parentIndex;
                        }
                        return (std::numeric_limits<int>::max)() / 4;
                    };
                    const int distance = std::min(chainDistance(s.owner, static_cast<std::int32_t>(oi)),
                                                  chainDistance(static_cast<std::int32_t>(oi), s.owner));
                    const int splice =
                        objects_[oi].spliceIndex == objects_[static_cast<std::size_t>(s.owner)].spliceIndex ? 0 : 1;
                    if (distance < bestDistance || (distance == bestDistance && splice < bestSplice))
                    {
                        bestDistance = distance;
                        bestSplice = splice;
                        helper = static_cast<std::int32_t>(oi);
                    }
                }
                if (helper >= 0 && objects_[static_cast<std::size_t>(helper)].cid == 97)
                {
                    dynamicShadowCasters_.emplace_back(layer, helper);
                    projectedShadows_.back().depth.w = 1.0f;
                }
            }
            if (projectedShadows_.back().depth.w == 0.0f && !sh->diffuseBitmapIndices.empty())
            {
                int bmp = sh->diffuseBitmapIndices[0];
                if (bmp >= 0 && static_cast<std::size_t>(bmp) < textures_.size())
                    CopyShadowTextureLayer(textures_[bmp], layer);
            }
        }
        // Caster hierarchy membership is structural. Resolve it once when the
        // scene is uploaded instead of performing caster * batch * hierarchy
        // walks in every shadow frame. Transforms remain dynamic; only the
        // immutable list of batches belonging to each caster is cached.
        dynamicShadowBatchIndices_.assign(dynamicShadowCasters_.size(), {});
        dynamicShadowLastModels_.assign(dynamicShadowCasters_.size(), glm::mat4(0.0f));
        const auto descendantOfCaster = [&](std::int32_t child, std::int32_t root)
        {
            for (std::size_t guard = 0;
                 child >= 0 && static_cast<std::size_t>(child) < objects_.size() && guard++ <= objects_.size();)
            {
                if (child == root)
                    return true;
                child = objects_[static_cast<std::size_t>(child)].parentIndex;
            }
            return false;
        };
        for (std::size_t casterIndex = 0; casterIndex < dynamicShadowCasters_.size(); ++casterIndex)
        {
            const std::int32_t caster = dynamicShadowCasters_[casterIndex].second;
            auto &ownedBatches = dynamicShadowBatchIndices_[casterIndex];
            for (std::size_t batchIndex = 0; batchIndex < batches_.size(); ++batchIndex)
                if (descendantOfCaster(batches_[batchIndex].ownerObjectIndex, caster))
                    ownedBatches.push_back(batchIndex);
        }
        dynamicShadowAffectedByWr_ = false;
        for (const auto &binding : globWarps_)
        {
            const std::size_t vertexBegin = binding.firstVertex;
            const std::size_t vertexEnd = std::min(vertices_.size(), vertexBegin + binding.vertexCount);
            if (vertexBegin >= vertexEnd)
                continue;
            for (const auto &ownedBatches : dynamicShadowBatchIndices_)
            {
                for (const std::size_t batchIndex : ownedBatches)
                {
                    if (batchIndex >= batches_.size())
                        continue;
                    const auto &batch = batches_[batchIndex];
                    const std::size_t indexEnd =
                        std::min(indices_.size(), static_cast<std::size_t>(batch.firstIndex) + batch.indexCount);
                    for (std::size_t index = batch.firstIndex; index < indexEnd; ++index)
                    {
                        const std::uint32_t vertex = indices_[index];
                        if (vertex >= vertexBegin && vertex < vertexEnd)
                        {
                            dynamicShadowAffectedByWr_ = true;
                            break;
                        }
                    }
                    if (dynamicShadowAffectedByWr_)
                        break;
                }
                if (dynamicShadowAffectedByWr_)
                    break;
            }
            if (dynamicShadowAffectedByWr_)
                break;
        }
        UploadShaderStorageRecords(shadowBuffer_, projectedShadows_, GL_STATIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, shadowBuffer_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void BuildFastShadowPreviewCache()
    {
        fastShadowModel_ = -1;
        fastShadowBatchIndices_.clear();
        fastShadowSources_.clear();
        for (std::size_t index = 0; index < objects_.size(); ++index)
        {
            if (objects_[index].oid == 29 && CidDerivesFrom(objects_[index].cid, 2))
            {
                fastShadowModel_ = static_cast<std::int32_t>(index);
                break;
            }
        }
        if (fastShadowModel_ < 0)
            return;
        const auto descendantOfModel = [&](std::int32_t objectIndex)
        {
            for (std::size_t guard = 0; objectIndex >= 0 && static_cast<std::size_t>(objectIndex) < objects_.size() &&
                                        guard++ <= objects_.size();)
            {
                if (objectIndex == fastShadowModel_)
                    return true;
                objectIndex = objects_[static_cast<std::size_t>(objectIndex)].parentIndex;
            }
            return false;
        };
        for (std::size_t batchIndex = 0; batchIndex < batches_.size(); ++batchIndex)
            if (descendantOfModel(batches_[batchIndex].ownerObjectIndex))
                fastShadowBatchIndices_.push_back(batchIndex);
        for (std::size_t ownerIndex = 0; ownerIndex < objects_.size(); ++ownerIndex)
        {
            float radius = 0.0f;
            float depth = 0.0f;
            for (const auto &property : objects_[ownerIndex].properties)
            {
                if (property.eopid == 41)
                    radius = property.floatValue[0];
                else if (property.eopid == 42)
                    depth = property.floatValue[0];
            }
            if (radius > 0.0f)
                fastShadowSources_.push_back({static_cast<std::int32_t>(ownerIndex), radius, depth});
        }
    }

    void RenderDynamicShadows()
    {
        if (shadowTextureArray_ == 0 || shadowProgram_ == 0 || dynamicShadowCasters_.empty())
            return;
        // DYSH layers are persistent texture-array images. Retail regenerates
        // them when a helper is submitted, but the editor's mostly-static
        // scene does not need to rasterize every unchanged silhouette on every
        // viewport frame. Detect animated/moved helpers and otherwise reuse
        // the existing layer exactly.
        for (std::size_t casterIndex = 0; casterIndex < dynamicShadowCasters_.size(); ++casterIndex)
        {
            const auto helper = dynamicShadowCasters_[casterIndex].second;
            const glm::mat4 model = SubmissionTransformForObject(helper);
            if (casterIndex >= dynamicShadowLastModels_.size())
            {
                dynamicShadowMapsDirty_ = true;
                break;
            }
            bool changed = false;
            for (int column = 0; column < 4 && !changed; ++column)
                for (int row = 0; row < 4; ++row)
                    if (std::abs(model[column][row] - dynamicShadowLastModels_[casterIndex][column][row]) > 0.00001f)
                    {
                        changed = true;
                        break;
                    }
            dynamicShadowMapsDirty_ |= changed;
        }
        if (!dynamicShadowMapsDirty_)
            return;
        GLint oldFramebuffer = 0, oldDepthFunc = GL_LESS, oldViewport[4]{};
        GLboolean oldDepthMask = GL_TRUE;
        const GLboolean oldDepthTest = glIsEnabled(GL_DEPTH_TEST), oldBlend = glIsEnabled(GL_BLEND),
                        oldCull = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldFramebuffer);
        glGetIntegerv(GL_DEPTH_FUNC, &oldDepthFunc);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &oldDepthMask);
        glGetIntegerv(GL_VIEWPORT, oldViewport);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer_);
        glViewport(0, 0, shadowResolution_, shadowResolution_);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glUseProgram(shadowProgram_);
        glBindVertexArray(vao_);
        const GLint projectionLocation = glGetUniformLocation(shadowProgram_, "uWorldToUv");
        const GLint modelLocation = glGetUniformLocation(shadowProgram_, "uModel");
        for (std::size_t casterIndex = 0; casterIndex < dynamicShadowCasters_.size(); ++casterIndex)
        {
            const auto [layer, caster] = dynamicShadowCasters_[casterIndex];
            if (casterIndex < dynamicShadowLastModels_.size())
                dynamicShadowLastModels_[casterIndex] = SubmissionTransformForObject(caster);
            const auto shadow = std::find_if(projectedShadows_.begin(), projectedShadows_.end(),
                                             [&](const GpuProjectedShadow &value) { return value.meta.x == layer; });
            if (shadow == projectedShadows_.end())
                continue;
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, shadowTextureArray_, 0, layer);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, glm::value_ptr(shadow->worldToUv));
            if (casterIndex >= dynamicShadowBatchIndices_.size())
                continue;
            for (const std::size_t batchIndex : dynamicShadowBatchIndices_[casterIndex])
            {
                if (batchIndex >= batches_.size())
                    continue;
                const auto &batch = batches_[batchIndex];
                const glm::mat4 model = SubmissionTransformForObject(batch.ownerObjectIndex);
                glUniformMatrix4fv(modelLocation, 1, GL_FALSE, glm::value_ptr(model));
                glDrawElements(
                    GL_TRIANGLES, static_cast<GLsizei>(batch.indexCount), GL_UNSIGNED_INT,
                    reinterpret_cast<void *>(static_cast<std::uintptr_t>(batch.firstIndex) * sizeof(std::uint32_t)));
            }
        }
        glBindVertexArray(0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, oldFramebuffer);
        glDepthFunc(oldDepthFunc);
        glDepthMask(oldDepthMask);
        if (oldDepthTest)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        if (oldBlend)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
        if (oldCull)
            glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);
        glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
        dynamicShadowMapsDirty_ = false;
    }

    void ReleaseSceneTextures()
    {
        if (!textures_.empty())
            glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
        textures_.clear();
        projectedShadows_.clear();
        dynamicShadowCasters_.clear();
        dynamicShadowBatchIndices_.clear();
        dynamicShadowLastModels_.clear();
        dynamicShadowMapsDirty_ = true;
        dynamicShadowAffectedByWr_ = false;
        if (shadowTextureArray_ != 0)
        {
            glDeleteTextures(1, &shadowTextureArray_);
            shadowTextureArray_ = 0;
        }
    }

    void EnsureTarget(int width, int height)
    {
        if (width_ == width && height_ == height)
            return;
        width_ = width;
        height_ = height;
        glBindTexture(GL_TEXTURE_2D, colorTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool ObjectHierarchyPassesVisibility(std::int32_t index) const
    {
        // RenderAloAll rejects before RenderAloSelf and recursive children.
        // This also applies to MS's whole-ALO VISS 2 gate, independently of
        // the per-GLOBI mask tested by RenderMsGlobset.
        for (std::size_t guard = 0;
             index >= 0 && static_cast<std::size_t>(index) < objects_.size() && guard < objects_.size();
             ++guard, index = RuntimeParentIndex(index))
        {
            const auto &object = objects_[static_cast<std::size_t>(index)];
            // RenderRatAll dispatches its selected child directly, rather
            // than invoking RenderAloAll for the RAT wrapper (CID 65).
            if (object.hasAloPayload && object.cid != 65 && object.visibilityMode == 2u && object.zoneMask != 0u &&
                (object.zoneMask & cameraZoneMask_) != cameraZoneMask_)
                return false;
        }
        return true;
    }

    void RefreshDynamicVisibilityMasks()
    {
        // UpdateAloXf reclassifies ZONS 2 at posWorld with sRadiusRenderAll.
        // Keep runtime preview results separate from the authored document.
        for (std::size_t index = 0; index < objects_.size(); ++index)
        {
            auto &object = objects_[index];
            if (!object.hasAloPayload || object.zoneMode != 2u)
                continue;
            const auto transform =
                SubmissionTransformForObject(static_cast<std::int32_t>(index)) * glm::make_mat4(object.worldTransform);
            const auto result = VisibilityMap::SphereOneHop(visibilityNodes_, visibilityOneHopMasks_,
                                                            glm::vec3(transform[3]), object.renderRadiusAll);
            if (result.valid)
                object.zoneMask = result.mask;
        }
        // LIGHT overrides the generic ALO classification in
        // light.cpp::UpdateLightBeamGrfzon when fDynamic is set.
        for (std::size_t index = 0; index < lights_.size() && index < lightOwnerIndices_.size(); ++index)
        {
            const auto &light = lights_[index];
            const auto owner = lightOwnerIndices_[index];
            if (!light.dynamic || owner < 0 || static_cast<std::size_t>(owner) >= objects_.size())
                continue;
            if (light.kind == 0)
            {
                objects_[owner].zoneMask = VisibilityMap::FallbackMask;
                continue;
            }
            if (light.kind < 1 || light.kind > 3)
                continue;
            const auto transform = SubmissionTransformForObject(owner);
            auto center = glm::vec3(transform * glm::vec4(light.visibilityPosition, 1.0f));
            float radius = light.visibilityDistance;
            if (light.kind == 2 || light.kind == 3)
            {
                auto forward = glm::mat3(transform) * light.visibilityDirection;
                const float lengthSquared = glm::dot(forward, forward);
                if (lengthSquared > 0.0f)
                    forward /= std::sqrt(lengthSquared);
                const float halfLength = light.visibilityDistance * 0.5f;
                const float baseRadius =
                    std::tan(glm::radians(light.visibilityConeAngle * 0.5f)) * light.visibilityDistance;
                center += forward * halfLength;
                radius = std::sqrt(halfLength * halfLength + baseRadius * baseRadius);
            }
            const auto result = VisibilityMap::SphereOneHop(visibilityNodes_, visibilityOneHopMasks_, center, radius);
            if (result.valid)
                objects_[owner].zoneMask = result.mask;
        }
        const auto refreshPacket = [&](auto &packet)
        {
            const auto owner = packet.ownerObjectIndex;
            if (owner < 0 || static_cast<std::size_t>(owner) >= objects_.size())
                return;
            const auto &object = objects_[static_cast<std::size_t>(owner)];
            // Dynamic LIGHT can replace grfzon even with fixed ALO ZONS.
            // Every VISS 2 packet consumes the current object value.
            if (object.cid != 4 && object.visibilityMode == 2u)
                packet.zoneMask = object.zoneMask;
        };
        for (auto &batch : batches_)
            refreshPacket(batch);
        for (auto &cel : celBatches_)
            refreshPacket(cel);
    }

    [[nodiscard]] std::uint32_t ResolveVisibilityMask(const glm::vec3 &point) const
    {
        const auto result = VisibilityMap::PointNoHop(visibilityNodes_, point);
        // Preserve the last camera mask on malformed editor data as well as
        // empty leaves, rather than introducing an all-32-bit camera zone.
        return result.valid ? result.mask : 0u;
    }

    void BuildAloxResolvedTransforms()
    {
        aloxResolvedWorld_.assign(objects_.size(), glm::mat4(1.0f));
        aloxResolvedDeltas_.assign(objects_.size(), glm::mat4(1.0f));
        std::vector<std::uint8_t> state(objects_.size(), 0);

        const auto hierarchyDistance = [&](std::int32_t from, std::int32_t to)
        {
            std::unordered_map<std::int32_t, int> ancestors;
            int depth = 0;
            for (std::size_t guard = 0;
                 from >= 0 && static_cast<std::size_t>(from) < objects_.size() && guard++ <= objects_.size();)
            {
                ancestors.emplace(from, depth++);
                from = RuntimeParentIndex(from);
            }
            depth = 0;
            for (std::size_t guard = 0;
                 to >= 0 && static_cast<std::size_t>(to) < objects_.size() && guard++ <= objects_.size();)
            {
                if (const auto found = ancestors.find(to); found != ancestors.end())
                    return found->second + depth;
                ++depth;
                to = RuntimeParentIndex(to);
            }
            return (std::numeric_limits<int>::max)() / 4;
        };
        const auto findTransformSource = [&](const std::size_t ownerIndex, const std::int16_t oid,
                                             const std::int16_t requiredBaseCid) -> std::int32_t
        {
            if (oid == -1)
                return -1;
            std::int32_t best = -1;
            int bestHierarchyDistance = (std::numeric_limits<int>::max)();
            int bestSplicePenalty = (std::numeric_limits<int>::max)();
            std::size_t bestRecordDistance = (std::numeric_limits<std::size_t>::max)();
            const auto &owner = objects_[ownerIndex];
            for (std::size_t candidateIndex = 0; candidateIndex < objects_.size(); ++candidateIndex)
            {
                const auto &candidate = objects_[candidateIndex];
                if (candidateIndex == ownerIndex)
                    continue;
                if (candidate.oid != oid ||
                    (candidate.cid != requiredBaseCid && !CidDerivesFrom(candidate.cid, requiredBaseCid)))
                    continue;
                // PloFindSwNearest is hierarchy/context based.  Use hierarchy
                // distance first, splice context second, and serialized order
                // only as a deterministic final tie breaker.
                const int candidateHierarchyDistance =
                    hierarchyDistance(static_cast<std::int32_t>(ownerIndex), static_cast<std::int32_t>(candidateIndex));
                const int splicePenalty = candidate.spliceIndex == owner.spliceIndex ? 0 : 1;
                const std::size_t recordDistance =
                    candidateIndex > ownerIndex ? candidateIndex - ownerIndex : ownerIndex - candidateIndex;
                if (candidateHierarchyDistance < bestHierarchyDistance ||
                    (candidateHierarchyDistance == bestHierarchyDistance && splicePenalty < bestSplicePenalty) ||
                    (candidateHierarchyDistance == bestHierarchyDistance && splicePenalty == bestSplicePenalty &&
                     recordDistance < bestRecordDistance))
                {
                    bestHierarchyDistance = candidateHierarchyDistance;
                    bestSplicePenalty = splicePenalty;
                    bestRecordDistance = recordDistance;
                    best = static_cast<std::int32_t>(candidateIndex);
                }
            }
            return best;
        };

        std::function<bool(std::size_t)> resolve = [&](const std::size_t objectIndex)
        {
            if (state[objectIndex] == 2)
                return true;
            if (state[objectIndex] == 1)
            {
                // Propagate failure through the complete cycle/descendant path
                // so no recursive frame can overwrite this authored fallback.
                std::memcpy(glm::value_ptr(aloxResolvedWorld_[objectIndex]), objects_[objectIndex].worldTransform,
                            sizeof(objects_[objectIndex].worldTransform));
                return false;
            }
            state[objectIndex] = 1;
            const auto &object = objects_[objectIndex];
            glm::mat4 local(1.0f);
            glm::mat4 authoredWorld(1.0f);
            std::memcpy(glm::value_ptr(local), object.localTransform, sizeof(object.localTransform));
            std::memcpy(glm::value_ptr(authoredWorld), object.worldTransform, sizeof(object.worldTransform));

            glm::mat4 normalParent(1.0f);
            if (object.parentIndex >= 0 && static_cast<std::size_t>(object.parentIndex) < objects_.size())
            {
                if (!resolve(static_cast<std::size_t>(object.parentIndex)))
                {
                    aloxResolvedWorld_[objectIndex] = authoredWorld;
                    aloxResolvedDeltas_[objectIndex] = glm::mat4(1.0f);
                    state[objectIndex] = 2;
                    return false;
                }
                normalParent = aloxResolvedWorld_[static_cast<std::size_t>(object.parentIndex)];
            }
            // UpdateAloXfWorldHierarchy uses the authored local transform.
            // Pre/post matrices belong to AdjustAloRotation and IK/animation,
            // not ordinary hierarchy placement.
            const glm::mat3 localRotationScale(local);
            glm::mat4 resolved = normalParent * local;

            if (object.hasAloPayload && (object.alox.flags & 0x0cu) != 0)
            {
                glm::mat4 alternateParent(1.0f);
                std::int32_t sourceIndex = -1;
                if (object.alox.editorFosterOid != -1)
                {
                    const auto cached = object.alox.editorFosterObjectIndex;
                    if (cached >= 0 && static_cast<std::size_t>(cached) < objects_.size() &&
                        objects_[static_cast<std::size_t>(cached)].oid == object.alox.editorFosterOid &&
                        objects_[static_cast<std::size_t>(cached)].spliceIndex == object.alox.editorFosterSpliceIndex &&
                        objects_[static_cast<std::size_t>(cached)].offset == object.alox.editorFosterObjectOffset)
                        sourceIndex = cached;
                    else
                    {
                        const auto stable =
                            std::find_if(objects_.begin(), objects_.end(),
                                         [&](const BrxObjectRecord &candidate)
                                         {
                                             return candidate.offset == object.alox.editorFosterObjectOffset &&
                                                    candidate.oid == object.alox.editorFosterOid &&
                                                    candidate.spliceIndex == object.alox.editorFosterSpliceIndex;
                                         });
                        if (stable != objects_.end())
                            sourceIndex = static_cast<std::int32_t>(std::distance(objects_.begin(), stable));
                    }
                }
                if (sourceIndex < 0)
                    sourceIndex = findTransformSource(objectIndex, object.alox.rotationSourceOid, 3);
                if (sourceIndex >= 0 && static_cast<std::size_t>(sourceIndex) != objectIndex)
                {
                    if (!resolve(static_cast<std::size_t>(sourceIndex)))
                    {
                        aloxResolvedWorld_[objectIndex] = authoredWorld;
                        aloxResolvedDeltas_[objectIndex] = glm::mat4(1.0f);
                        state[objectIndex] = 2;
                        return false;
                    }
                    alternateParent = aloxResolvedWorld_[static_cast<std::size_t>(sourceIndex)];
                }
                const glm::mat4 &positionParent = (object.alox.flags & 0x04u) != 0 ? alternateParent : normalParent;
                const glm::mat4 &rotationParent = (object.alox.flags & 0x08u) != 0 ? alternateParent : normalParent;
                const glm::vec4 position = positionParent * glm::vec4(glm::vec3(local[3]), 1.0f);
                const glm::mat3 rotationScale = glm::mat3(rotationParent) * localRotationScale;
                resolved = glm::mat4(1.0f);
                resolved[0] = glm::vec4(rotationScale[0], 0.0f);
                resolved[1] = glm::vec4(rotationScale[1], 0.0f);
                resolved[2] = glm::vec4(rotationScale[2], 0.0f);
                resolved[3] = position;
            }
            // SSC is a render-time correction sourced from parent->pactScale.
            // The BRX editor has no live ACT scale controller, so applying it
            // to static authored scale would be incorrect.  Pose/animation
            // preview owns this correction when an ACT scale is available.
            aloxResolvedWorld_[objectIndex] = resolved;
            const float determinant = glm::determinant(authoredWorld);
            aloxResolvedDeltas_[objectIndex] =
                std::abs(determinant) > 0.000001f ? resolved * glm::inverse(authoredWorld) : glm::mat4(1.0f);
            state[objectIndex] = 2;
            return true;
        };

        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex)
            resolve(objectIndex);

        const auto isDescendantOf = [&](std::int32_t candidate, const std::int32_t ancestor)
        {
            for (std::size_t guard = 0;
                 candidate >= 0 && static_cast<std::size_t>(candidate) < objects_.size() && guard++ <= objects_.size();)
            {
                if (candidate == ancestor)
                    return true;
                candidate = objects_[static_cast<std::size_t>(candidate)].parentIndex;
            }
            return false;
        };
        const auto applyHierarchyDelta = [&](const std::int32_t root, const glm::mat4 &delta)
        {
            for (std::size_t candidate = 0; candidate < objects_.size(); ++candidate)
                if (isDescendantOf(static_cast<std::int32_t>(candidate), root))
                    aloxResolvedWorld_[candidate] = delta * aloxResolvedWorld_[candidate];
        };

        // UpdateAloConstraints is invoked by an ALOX 0x10 driver and solves
        // the referenced 0x20 IK host. Do not solve every 0x20 record merely
        // because it exists in the file.
        std::vector<std::size_t> ikHosts;
        for (std::size_t driverIndex = 0; driverIndex < objects_.size(); ++driverIndex)
        {
            const auto &driver = objects_[driverIndex];
            if (!driver.hasAloPayload || (driver.alox.flags & 0x10u) == 0)
                continue;
            const std::int32_t hostIndex = findTransformSource(driverIndex, driver.alox.ikTargetOid, 2);
            if (hostIndex < 0 || (objects_[static_cast<std::size_t>(hostIndex)].alox.flags & 0x20u) == 0)
                continue;
            const auto candidate = static_cast<std::size_t>(hostIndex);
            if (std::find(ikHosts.begin(), ikHosts.end(), candidate) == ikHosts.end())
                ikHosts.push_back(candidate);
        }
        // Runtime velocity terms are deliberately excluded from the static
        // authored-pose preview, matching grfik=0 loaded by ProjectCane.
        for (const std::size_t hostIndex : ikHosts)
        {
            const auto &host = objects_[hostIndex];
            const std::int32_t elbowIndex = findTransformSource(hostIndex, host.alox.elbowOid, 2);
            if (elbowIndex < 0)
                continue;
            const std::int32_t shoulderIndex = objects_[static_cast<std::size_t>(elbowIndex)].parentIndex;
            if (shoulderIndex < 0 || static_cast<std::size_t>(shoulderIndex) >= objects_.size())
                continue;

            const auto resetIkJointBasis = [&](const std::int32_t jointIndex, const bool elbowJoint)
            {
                const auto &joint = objects_[static_cast<std::size_t>(jointIndex)];
                if (!joint.hasAloPayload)
                    return;
                glm::mat4 local = glm::make_mat4(joint.localTransform);
                glm::mat3 basis(1.0f);
                if (!elbowJoint || (joint.alox.flags & 0x03u) != 0)
                    basis = glm::make_mat3(joint.alox.postRotation) * glm::make_mat3(joint.alox.preRotation);
                local[0] = glm::vec4(basis[0], 0.0f);
                local[1] = glm::vec4(basis[1], 0.0f);
                local[2] = glm::vec4(basis[2], 0.0f);
                glm::mat4 parentWorld(1.0f);
                if (joint.parentIndex >= 0 && static_cast<std::size_t>(joint.parentIndex) < aloxResolvedWorld_.size())
                    parentWorld = aloxResolvedWorld_[static_cast<std::size_t>(joint.parentIndex)];
                const glm::mat4 baseWorld = parentWorld * local;
                const glm::mat4 currentWorld = aloxResolvedWorld_[static_cast<std::size_t>(jointIndex)];
                if (std::abs(glm::determinant(currentWorld)) > 0.000001f)
                    applyHierarchyDelta(jointIndex, baseWorld * glm::inverse(currentWorld));
            };
            // SolveAloIK restores these exact local bases before solving.
            resetIkJointBasis(shoulderIndex, false);
            resetIkJointBasis(elbowIndex, true);

            const glm::vec3 shoulder(aloxResolvedWorld_[static_cast<std::size_t>(shoulderIndex)][3]);
            const glm::vec3 elbow(aloxResolvedWorld_[static_cast<std::size_t>(elbowIndex)][3]);
            const glm::vec3 wrist = glm::vec3(
                aloxResolvedWorld_[static_cast<std::size_t>(elbowIndex)] *
                glm::vec4(host.alox.wristPosition[0], host.alox.wristPosition[1], host.alox.wristPosition[2], 1.0f));
            const glm::vec3 target =
                glm::vec3(aloxResolvedWorld_[hostIndex] *
                          glm::vec4(host.alox.ikPosition[0], host.alox.ikPosition[1], host.alox.ikPosition[2], 1.0f));
            const float upperLength = glm::length(elbow - shoulder);
            const float lowerLength = glm::length(wrist - elbow);
            glm::vec3 targetVector = target - shoulder;
            float targetLength = glm::length(targetVector);
            if (upperLength < 0.0001f || lowerLength < 0.0001f || targetLength < 0.0001f)
                continue;
            targetVector /= targetLength;
            const float minimumReach = std::abs(upperLength - lowerLength) + 0.0001f;
            const float maximumReach = upperLength + lowerLength - 0.0001f;
            if (minimumReach > maximumReach)
                continue;
            targetLength = std::clamp(targetLength, minimumReach, maximumReach);
            const float along = (upperLength * upperLength - lowerLength * lowerLength + targetLength * targetLength) /
                                (2.0f * targetLength);
            const float height = std::sqrt((std::max)(0.0f, upperLength * upperLength - along * along));
            glm::vec3 bend = (elbow - shoulder) - targetVector * glm::dot(elbow - shoulder, targetVector);
            if (glm::length(bend) < 0.0001f)
                bend =
                    glm::cross(targetVector, std::abs(targetVector.z) < 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0));
            bend = glm::normalize(glm::mat3(glm::rotate(glm::mat4(1.0f), host.alox.twistRadians, targetVector)) *
                                  glm::normalize(bend));
            const glm::vec3 solvedElbow = shoulder + targetVector * along + bend * height;

            const glm::quat shoulderRotation =
                glm::rotation(glm::normalize(elbow - shoulder), glm::normalize(solvedElbow - shoulder));
            const glm::mat4 shoulderDelta = glm::translate(glm::mat4(1.0f), shoulder) *
                                            glm::mat4_cast(shoulderRotation) *
                                            glm::translate(glm::mat4(1.0f), -shoulder);
            applyHierarchyDelta(shoulderIndex, shoulderDelta);
            const glm::vec3 movedElbow(aloxResolvedWorld_[static_cast<std::size_t>(elbowIndex)][3]);
            const glm::vec3 movedWrist = glm::vec3(shoulderDelta * glm::vec4(wrist, 1.0f));
            if (glm::length(movedWrist - movedElbow) > 0.0001f && glm::length(target - movedElbow) > 0.0001f)
            {
                const glm::quat elbowRotation =
                    glm::rotation(glm::normalize(movedWrist - movedElbow), glm::normalize(target - movedElbow));
                const glm::mat4 elbowDelta = glm::translate(glm::mat4(1.0f), movedElbow) *
                                             glm::mat4_cast(elbowRotation) *
                                             glm::translate(glm::mat4(1.0f), -movedElbow);
                applyHierarchyDelta(elbowIndex, elbowDelta);
            }
        }

        // IK can alter several already-resolved hierarchy members, so derive
        // every submission delta only after all static constraints are solved.
        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex)
        {
            glm::mat4 authoredWorld(1.0f);
            std::memcpy(glm::value_ptr(authoredWorld), objects_[objectIndex].worldTransform,
                        sizeof(objects_[objectIndex].worldTransform));
            aloxResolvedDeltas_[objectIndex] = std::abs(glm::determinant(authoredWorld)) > 0.000001f
                                                   ? aloxResolvedWorld_[objectIndex] * glm::inverse(authoredWorld)
                                                   : glm::mat4(1.0f);
        }
    }

    void BuildWrControllers()
    {
        wrControllers_.clear();
        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex)
        {
            const auto &object = objects_[objectIndex];
            if (object.cid != 141)
                continue;
            WrController controller;
            controller.oid = object.oid;
            controller.objectIndex = static_cast<std::int32_t>(objectIndex);
            WrOperator *current = nullptr;
            int matrixCount = 0;
            int halfCircleMatrix = -1;
            for (const auto &property : object.properties)
            {
                if (property.eopid == 1003)
                {
                    if (controller.operators.size() < 8)
                    {
                        controller.operators.emplace_back();
                        current = &controller.operators.back();
                        if (halfCircleMatrix < 0)
                        {
                            current->matrixIndex = matrixCount++;
                            halfCircleMatrix = current->matrixIndex;
                        }
                        else
                        {
                            current->matrixIndex = halfCircleMatrix;
                            halfCircleMatrix = -1;
                        }
                    }
                    continue;
                }
                if (property.eopid == 1004)
                {
                    if (controller.operators.size() < 8)
                    {
                        controller.operators.emplace_back();
                        current = &controller.operators.back();
                        current->kind = WrOperator::Kind::Bend;
                        current->matrixIndex = matrixCount++;
                    }
                    continue;
                }
                if (property.eopid == 1022)
                {
                    current = property.intValue >= 0 &&
                                      static_cast<std::size_t>(property.intValue) < controller.operators.size()
                                  ? &controller.operators[static_cast<std::size_t>(property.intValue)]
                                  : nullptr;
                    continue;
                }
                if (current == nullptr)
                    continue;
                const glm::vec3 v3(property.floatValue[0], property.floatValue[1], property.floatValue[2]);
                const glm::vec2 v2(property.floatValue[0], property.floatValue[1]);
                switch (property.eopid)
                {
                case 1005:
                    current->normal = v3;
                    break;
                case 1006:
                    current->wavelength = property.floatValue[0];
                    break;
                case 1007:
                    current->frequency = property.floatValue[0];
                    break;
                case 1008:
                    current->phase = property.floatValue[0];
                    break;
                case 1009:
                    current->dpos[0] = v3;
                    break;
                case 1010:
                    current->dpos[1] = v3;
                    break;
                case 1011:
                    current->duv[0] = v2;
                    break;
                case 1012:
                    current->duv[1] = v2;
                    break;
                case 1013:
                    current->bendNormal = v3;
                    break;
                case 1014:
                    current->bendRange = v2;
                    break;
                case 1015:
                    current->bendBias = property.floatValue[0];
                    break;
                case 1017:
                    current->swivelNormal = v3;
                    break;
                case 1018:
                    current->swivelRange = v2;
                    break;
                case 1019:
                    current->swivelBias = property.floatValue[0];
                    break;
                case 1016:
                case 1020:
                    if (property.arguments.size() == 4)
                    {
                        WrNoise noise;
                        noise.amplitude = property.arguments[0].floatValue[0];
                        noise.frequency = property.arguments[1].floatValue[0];
                        noise.phase = property.arguments[2].floatValue[0];
                        noise.random = property.arguments[3].floatValue[0];
                        auto &list = property.eopid == 1016 ? current->bendNoise : current->swivelNoise;
                        if (list.size() < 4)
                            list.push_back(noise);
                    }
                    break;
                default:
                    break;
                }
            }
            wrControllers_.push_back(std::move(controller));
        }
        // GetWrBounds expands each attached GLOB by the combined circle-warp
        // displacement so animated geometry is not rejected at its rest pose.
        for (const auto &binding : globWarps_)
        {
            if (binding.matrixCount == 0)
                continue;
            const WrController *controller = FindWrController(binding);
            if (controller == nullptr)
                continue;
            glm::vec3 expansion(0.0f);
            for (const auto &op : controller->operators)
            {
                if (op.kind != WrOperator::Kind::Circle || op.matrixIndex < 0)
                    continue;
                expansion += glm::sqrt(op.dpos[0] * op.dpos[0] + op.dpos[1] * op.dpos[1]);
            }
            const float radiusExpansion = glm::length(expansion);
            for (auto &batch : batches_)
                if (batch.renderGroup == binding.renderGroup)
                    batch.boundsRadius += radiusExpansion;
            for (auto &batch : celBatches_)
                if (batch.renderGroup == binding.renderGroup)
                    batch.radius += radiusExpansion;
        }
    }

    const WrController *FindWrController(const BrxGlobWarpRecord &binding) const
    {
        const WrController *best = nullptr;
        int bestScore = std::numeric_limits<int>::min();
        for (const auto &candidate : wrControllers_)
        {
            if (candidate.oid != binding.controllerOid)
                continue;
            int score = 0;
            if (binding.ownerObjectIndex >= 0 && candidate.objectIndex >= 0 &&
                static_cast<std::size_t>(binding.ownerObjectIndex) < objects_.size() &&
                static_cast<std::size_t>(candidate.objectIndex) < objects_.size())
            {
                const auto ownerParent = RuntimeParentIndex(binding.ownerObjectIndex);
                const auto controllerParent = RuntimeParentIndex(candidate.objectIndex);
                if (ownerParent == controllerParent)
                    score += 100000;
                int owner = binding.ownerObjectIndex;
                for (int ownerDepth = 0; owner >= 0 && static_cast<std::size_t>(owner) < objects_.size(); ++ownerDepth)
                {
                    int controller = candidate.objectIndex;
                    for (int controllerDepth = 0;
                         controller >= 0 && static_cast<std::size_t>(controller) < objects_.size(); ++controllerDepth)
                    {
                        if (owner == controller)
                            score = std::max(score, 10000 - ownerDepth - controllerDepth);
                        controller = RuntimeParentIndex(controller);
                    }
                    owner = RuntimeParentIndex(owner);
                }
            }
            if (best == nullptr || score > bestScore)
            {
                best = &candidate;
                bestScore = score;
            }
        }
        return best;
    }

    static float WrHash01(std::uint32_t n)
    {
        std::uint32_t x = (n << 13) ^ n;
        return static_cast<float>((x * (x * x * 0x3D73u + 0xC0AE5u) + 0x5208DD0Du) & 0x7fffffffu) * 4.656613e-10f;
    }

    static float WrNoiseValue(const WrNoise &noise, float time)
    {
        const float x = 2.0f * noise.frequency * time + noise.phase;
        const auto n = static_cast<std::uint32_t>(x);
        const float n0 = WrHash01(n), n1 = WrHash01(n + 1);
        const float a = (n & 1u) == 0u ? noise.random * (n0 - 1.0f) + 1.0f : n0 * noise.random;
        const float b = (n & 1u) == 0u ? n1 * noise.random : noise.random * (n1 - 1.0f) + 1.0f;
        const float f = x - std::floor(x);
        return a + f * f * (3.0f - 2.0f * f) * (b - a);
    }

    static float WrBiasedRange(const std::vector<WrNoise> &noises, const glm::vec2 &range, float bias, float time)
    {
        float weighted = 0.0f, amplitude = 0.0f;
        for (const auto &noise : noises)
        {
            weighted += noise.amplitude * WrNoiseValue(noise, time);
            amplitude += noise.amplitude;
        }
        float u = amplitude != 0.0f ? weighted / amplitude : 0.0f;
        if (bias > 0.000001f && bias < 0.999999f)
            u = u / ((1.0f / bias - 2.0f) * (1.0f - u) + 1.0f);
        return range.x + u * (range.y - range.x);
    }

    // Mirrors the driven-transform branch of ProjectCane's
    // ProjectAloTransform.  EOPIDs 21/22 are authored in object-local space;
    // the engine rotates them into the parent frame before integrating them.
    // Keep this entirely in render-time deltas so previewing motion never
    // changes the serialized transform or dirties the document.
    void UpdateRuntimeAloMotion(const float absoluteTime)
    {
        if (!automaticAloMotion_ || !hasAuthoredRuntimeMotion_ || objects_.empty() ||
            aloxResolvedWorld_.size() != objects_.size() || runtimeLocalVelocities_.size() != objects_.size() ||
            runtimeLocalAngularVelocities_.size() != objects_.size())
            return;
        runtimeMotionDeltas_.assign(objects_.size(), glm::mat4(1.0f));

        const float elapsed = (std::max)(absoluteTime - runtimeMotionStartTime_, 0.0f);
        std::vector<glm::mat4> projectedWorld(objects_.size(), glm::mat4(1.0f));
        std::vector<std::uint8_t> state(objects_.size(), 0);
        const auto normalizedBasis = [](const glm::mat4 &transform)
        {
            glm::mat3 basis(transform);
            for (int column = 0; column < 3; ++column)
            {
                const float length = glm::length(basis[column]);
                if (length > 0.000001f)
                    basis[column] /= length;
            }
            return basis;
        };
        std::vector<std::uint8_t> affected(objects_.size(), 0);
        std::function<bool(std::size_t)> project = [&](const std::size_t objectIndex)
        {
            if (state[objectIndex] == 2)
                return affected[objectIndex] != 0;
            if (state[objectIndex] == 1)
            {
                projectedWorld[objectIndex] = aloxResolvedWorld_[objectIndex];
                state[objectIndex] = 2;
                return false;
            }
            state[objectIndex] = 1;
            const auto &object = objects_[objectIndex];
            const bool validParent =
                object.parentIndex >= 0 && static_cast<std::size_t>(object.parentIndex) < objects_.size();
            glm::mat4 parentAuthored(1.0f), parentProjected(1.0f);
            bool parentAffected = false;
            if (validParent)
            {
                const std::size_t parentIndex = static_cast<std::size_t>(object.parentIndex);
                parentAffected = project(parentIndex);
                parentAuthored = aloxResolvedWorld_[parentIndex];
                parentProjected = projectedWorld[parentIndex];
            }

            glm::mat4 local = validParent ? glm::inverse(parentAuthored) * aloxResolvedWorld_[objectIndex]
                                          : aloxResolvedWorld_[objectIndex];
            if (CidDerivesFrom(object.cid, 2))
            {
                const glm::mat3 rotation = normalizedBasis(local);
                const glm::vec3 localVelocity = runtimeLocalVelocities_[objectIndex];
                const glm::vec3 localAngularVelocity = runtimeLocalAngularVelocities_[objectIndex];
                const bool ownMotion = glm::dot(localVelocity, localVelocity) > 0.0000001f ||
                                       glm::dot(localAngularVelocity, localAngularVelocity) > 0.0000001f;
                affected[objectIndex] = parentAffected || ownMotion;
                glm::vec3 position(local[3]);
                position += rotation * localVelocity * elapsed;
                const glm::vec3 angularVelocity = rotation * localAngularVelocity;
                const float angularSpeed = glm::length(angularVelocity);
                if (angularSpeed > 0.000001f)
                {
                    const glm::vec4 savedPosition = local[3];
                    local[3] = glm::vec4(0, 0, 0, 1);
                    local = glm::rotate(glm::mat4(1.0f), std::fmod(angularSpeed * elapsed, 6.283185307179586f),
                                        angularVelocity / angularSpeed) *
                            local;
                    local[3] = savedPosition;
                }
                local[3] = glm::vec4(position, 1.0f);
            }
            else
                affected[objectIndex] = parentAffected;
            projectedWorld[objectIndex] = parentProjected * local;
            if (affected[objectIndex] != 0)
                runtimeMotionDeltas_[objectIndex] =
                    projectedWorld[objectIndex] * glm::inverse(aloxResolvedWorld_[objectIndex]);
            state[objectIndex] = 2;
            return affected[objectIndex] != 0;
        };
        for (std::size_t objectIndex = 0; objectIndex < objects_.size(); ++objectIndex)
            project(objectIndex);
    }

    void UpdateWrDeformation(float time)
    {
        warpedVertexCount_ = 0;
        maximumWarpDisplacement_ = 0.0f;
        // Static levels already have their base geometry in the GPU buffers.
        // Avoid copying every vertex and cel edge every frame when no WR or
        // geometry ASEG can alter them.
        if (globWarps_.empty() && !asegPreviewGeometryActive_)
            return;
        const bool useAsegVertices = asegPreviewGeometryActive_ && asegPreviewVertices_.size() == baseVertices_.size();
        const bool useAsegCelEdges = asegPreviewGeometryActive_ && asegPreviewCelEdges_.size() == baseCelEdges_.size();
        const auto &deformationVertices = useAsegVertices ? asegPreviewVertices_ : baseVertices_;
        const auto &deformationCelEdges = useAsegCelEdges ? asegPreviewCelEdges_ : baseCelEdges_;
        if (deformationVertices.size() != vertices_.size())
            return;
        if (globWarps_.empty())
            return;
        // WR records already describe their affected contiguous vertex/edge
        // spans. Reset and upload only merged affected spans instead of copying and
        // transferring the entire level mesh every frame. Large worlds such
        // as A Grave Undertaking have only a handful of WR spans inside a very
        // large static vertex buffer.
        std::vector<std::pair<std::size_t, std::size_t>> dirtyVertexRanges;
        std::vector<std::pair<std::size_t, std::size_t>> dirtyEdgeRanges;
        for (const auto &binding : globWarps_)
        {
            if (binding.matrixCount == 0 || FindWrController(binding) == nullptr)
                continue;
            const std::size_t vertexBegin = std::min<std::size_t>(binding.firstVertex, vertices_.size());
            const std::size_t vertexEnd =
                std::min(vertices_.size(), static_cast<std::size_t>(binding.firstVertex) + binding.vertexCount);
            if (vertexBegin < vertexEnd)
                dirtyVertexRanges.emplace_back(vertexBegin, vertexEnd);
            if (binding.deformPosition)
            {
                const std::size_t edgeBegin = std::min<std::size_t>(binding.firstCelEdge, celEdges_.size());
                const std::size_t edgeEnd =
                    std::min(celEdges_.size(), static_cast<std::size_t>(binding.firstCelEdge) + binding.celEdgeCount);
                if (edgeBegin < edgeEnd)
                    dirtyEdgeRanges.emplace_back(edgeBegin, edgeEnd);
            }
        }
        const auto mergeRanges = [](auto &ranges)
        {
            std::sort(ranges.begin(), ranges.end());
            std::size_t output = 0;
            for (const auto &range : ranges)
            {
                if (output != 0 && range.first <= ranges[output - 1].second)
                    ranges[output - 1].second = std::max(ranges[output - 1].second, range.second);
                else
                    ranges[output++] = range;
            }
            ranges.resize(output);
        };
        mergeRanges(dirtyVertexRanges);
        mergeRanges(dirtyEdgeRanges);
        for (const auto [begin, end] : dirtyVertexRanges)
            std::copy(deformationVertices.begin() + begin, deformationVertices.begin() + end,
                      vertices_.begin() + begin);
        if (deformationCelEdges.size() == celEdges_.size())
            for (const auto [begin, end] : dirtyEdgeRanges)
                std::copy(deformationCelEdges.begin() + begin, deformationCelEdges.begin() + end,
                          celEdges_.begin() + begin);
        constexpr float twoPi = 6.283185307179586f;
        for (const auto &binding : globWarps_)
        {
            if (binding.matrixCount == 0)
                continue;
            const WrController *controller = FindWrController(binding);
            if (controller == nullptr)
                continue;
            const glm::mat4 objectToWorld = glm::make_mat4(binding.objectToWorld);
            const glm::mat4 worldToObject = glm::inverse(objectToWorld);
            const glm::mat4 envelope = glm::make_mat4(binding.envelopeMatrix);
            const float denominator = binding.innerDistance - binding.outerDistance;
            const float slope =
                std::abs(denominator) > 1.0e-20f ? (binding.innerWeight - binding.outerWeight) / denominator : 0.0f;
            const float intercept = binding.innerWeight - slope * binding.innerDistance;
            const float weightMin = std::min(binding.innerWeight, binding.outerWeight);
            const float weightMax = std::max(binding.innerWeight, binding.outerWeight);
            const auto calculateDeltas =
                [&](const glm::vec3 &worldPosition, glm::vec3 &positionDelta, glm::vec2 &uvDelta)
            {
                const glm::vec4 localPosition = worldToObject * glm::vec4(worldPosition, 1.0f);
                float envelopeWeight = 1.0f;
                if (binding.envelopeKind >= 0)
                {
                    const glm::vec3 p = glm::vec3(envelope * localPosition);
                    float distance = 0.0f;
                    switch (binding.envelopeKind)
                    {
                    // ProjectCane WEK ordering from glob.h:
                    // XYZ, XY, XZ, YZ, X, Y, Z.
                    case 0:
                        distance = glm::length(p);
                        break;
                    case 1:
                        distance = glm::length(glm::vec2(p.x, p.y));
                        break;
                    case 2:
                        distance = glm::length(glm::vec2(p.x, p.z));
                        break;
                    case 3:
                        distance = glm::length(glm::vec2(p.y, p.z));
                        break;
                    case 4:
                        distance = p.x;
                        break;
                    case 5:
                        distance = p.y;
                        break;
                    case 6:
                        distance = p.z;
                        break;
                    default:
                        break;
                    }
                    envelopeWeight = std::clamp(intercept + distance * slope, weightMin, weightMax);
                }
                for (const auto &op : controller->operators)
                {
                    if (op.matrixIndex < 0)
                        continue;
                    if (op.kind == WrOperator::Kind::Circle)
                    {
                        const float reciprocalWavelength = op.wavelength != 0.0f ? 1.0f / op.wavelength : 0.0f;
                        const float angle = (glm::dot(worldPosition, op.normal) * reciprocalWavelength +
                                             time * op.frequency + op.phase) *
                                            twoPi;
                        const float c = std::cos(angle) * envelopeWeight;
                        const float s = std::sin(angle) * envelopeWeight;
                        positionDelta += op.dpos[0] * c + op.dpos[1] * s;
                        uvDelta += op.duv[0] * c + op.duv[1] * s;
                    }
                    else
                    {
                        const float bend = WrBiasedRange(op.bendNoise, op.bendRange, op.bendBias, time);
                        const float swivel = WrBiasedRange(op.swivelNoise, op.swivelRange, op.swivelBias, time);
                        glm::vec3 swivelAxis = glm::dot(op.swivelNormal, op.swivelNormal) > 1.0e-20f
                                                   ? glm::normalize(op.swivelNormal)
                                                   : glm::vec3(0, 0, 1);
                        glm::vec3 bendAxis =
                            glm::mat3(glm::rotate(glm::mat4(1.0f), swivel, swivelAxis)) * op.bendNormal;
                        bendAxis =
                            glm::dot(bendAxis, bendAxis) > 1.0e-20f ? glm::normalize(bendAxis) : glm::vec3(0, 0, 1);
                        glm::vec3 pivot(0.0f);
                        if (binding.envelopeKind >= 4 && binding.envelopeKind <= 6)
                        {
                            glm::vec3 axis(0.0f);
                            axis[binding.envelopeKind - 4] = 1.0f;
                            pivot = glm::vec3(
                                glm::inverse(envelope) *
                                glm::vec4(axis * (0.5f * (binding.innerDistance + binding.outerDistance)), 1.0f));
                        }
                        const glm::vec3 worldPivot = glm::vec3(objectToWorld * glm::vec4(pivot, 1.0f));
                        const glm::mat3 rotation = glm::mat3(glm::rotate(glm::mat4(1.0f), bend, bendAxis));
                        positionDelta += (rotation - glm::mat3(1.0f)) * ((worldPosition - worldPivot) * envelopeWeight);
                    }
                }
            };
            const std::size_t end =
                std::min(vertices_.size(), static_cast<std::size_t>(binding.firstVertex) + binding.vertexCount);
            for (std::size_t vertexIndex = binding.firstVertex; vertexIndex < end; ++vertexIndex)
            {
                const auto &base = deformationVertices[vertexIndex];
                const glm::vec3 worldPosition(base.position[0], base.position[1], base.position[2]);
                glm::vec3 positionDelta(0.0f);
                glm::vec2 uvDelta(0.0f);
                calculateDeltas(worldPosition, positionDelta, uvDelta);
                if (binding.deformPosition)
                {
                    auto &vertex = vertices_[vertexIndex];
                    vertex.position[0] += positionDelta.x;
                    vertex.position[1] += positionDelta.y;
                    vertex.position[2] += positionDelta.z;
                    ++warpedVertexCount_;
                    maximumWarpDisplacement_ = std::max(maximumWarpDisplacement_, glm::length(positionDelta));
                }
                if (binding.deformUv)
                {
                    vertices_[vertexIndex].uv[0] += uvDelta.x;
                    vertices_[vertexIndex].uv[1] += uvDelta.y;
                }
            }
            if (binding.deformPosition)
            {
                const std::size_t edgeEnd =
                    std::min(celEdges_.size(), static_cast<std::size_t>(binding.firstCelEdge) + binding.celEdgeCount);
                for (std::size_t edgeIndex = binding.firstCelEdge; edgeIndex < edgeEnd; ++edgeIndex)
                    for (auto &point : celEdges_[edgeIndex].points)
                    {
                        glm::vec3 delta(0.0f);
                        glm::vec2 ignoredUv(0.0f);
                        calculateDeltas(glm::vec3(point[0], point[1], point[2]), delta, ignoredUv);
                        point[0] += delta.x;
                        point[1] += delta.y;
                        point[2] += delta.z;
                    }
            }
        }
        if (!dirtyVertexRanges.empty())
        {
            glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
            for (const auto [begin, end] : dirtyVertexRanges)
                glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(begin * sizeof(BrxRenderVertex)),
                                static_cast<GLsizeiptr>((end - begin) * sizeof(BrxRenderVertex)),
                                vertices_.data() + begin);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        if (!dirtyEdgeRanges.empty())
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, celEdgeBuffer_);
            for (const auto [begin, end] : dirtyEdgeRanges)
                glBufferSubData(GL_SHADER_STORAGE_BUFFER, static_cast<GLintptr>(begin * sizeof(BrxCelEdge)),
                                static_cast<GLsizeiptr>((end - begin) * sizeof(BrxCelEdge)), celEdges_.data() + begin);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }
        // Only force silhouette regeneration when WR actually touches a
        // dynamic helper. The conservative overlap test is prepared once from
        // immutable index ranges during scene upload.
        if (dynamicShadowAffectedByWr_ && !dirtyVertexRanges.empty())
            dynamicShadowMapsDirty_ = true;
    }

    void RebuildSurfaceBatchIndex()
    {
        surfaceByOwnerGroup_.clear();
        surfaceByOwnerGroup_.reserve(batches_.size());
        for (const auto &batch : batches_)
        {
            const std::uint64_t key =
                (static_cast<std::uint64_t>(static_cast<std::uint32_t>(batch.ownerObjectIndex)) << 32) |
                static_cast<std::uint64_t>(batch.renderGroup);
            surfaceByOwnerGroup_.try_emplace(key, &batch);
        }
    }

    void RebuildEyesAnimationIndex()
    {
        eyesAnimationBySecondaryShaderOid_.clear();
        eyesAnimationBySecondaryShaderOid_.reserve(shaders_.size());
        // PostEyesLoad's later owner wins. Assignment in load order preserves
        // that rule while making frame-time material lookup constant-time.
        for (const auto &shader : shaders_)
            if (shader.animation.kind == BrxShaderAnimationKind::Eyes)
                eyesAnimationBySecondaryShaderOid_[shader.animation.otherShaderOid] = shader.animation;
    }

    void RebuildConveyorPrimaryScrollers(const BrxDocument &document)
    {
        conveyorPrimaryScrollerRenderGroup_.clear();
        for (const auto &globset : document.Globsets())
        {
            if (globset.ownerObjectIndex < 0 ||
                static_cast<std::size_t>(globset.ownerObjectIndex) >= document.Objects().size() ||
                !CidDerivesFrom(document.Objects()[static_cast<std::size_t>(globset.ownerObjectIndex)].cid, 57) ||
                conveyorPrimaryScrollerRenderGroup_.contains(globset.ownerObjectIndex))
                continue;
            for (const auto globIndex : globset.globIndices)
            {
                if (globIndex >= document.Globs().size())
                    continue;
                const auto &glob = document.Globs()[globIndex];
                if (!glob.hasShaderAnimation || glob.shaderAnimation.kind != BrxShaderAnimationKind::Scroller)
                    continue;
                conveyorPrimaryScrollerRenderGroup_.emplace(globset.ownerObjectIndex, glob.renderGroup);
                break;
            }
        }
    }

    struct ModelThumbnailRecord
    {
        GLuint texture = 0;
        std::uint64_t lastUsedFrame = 0;
    };
    GLuint program_ = 0;
    GLuint celProgram_ = 0;
    GLuint collisionProgram_ = 0;
    GLuint blipProgram_ = 0;
    GLuint shadowProgram_ = 0;
    GLuint thumbnailProgram_ = 0;
    static constexpr std::uint32_t kNoSelection = std::numeric_limits<std::uint32_t>::max();
    GLuint vao_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint indexBuffer_ = 0;
    GLuint lightBuffer_ = 0;
    GLuint shadowBuffer_ = 0;
    GLuint celEdgeBuffer_ = 0;
    GLuint globInstanceBuffer_ = 0;
    GLuint collisionVao_ = 0;
    GLuint collisionVertexBuffer_ = 0;
    GLuint blipVao_ = 0;
    GLuint blipQuadBuffer_ = 0;
    GLuint blipInstanceBuffer_ = 0;
    GLuint whiteBlipTexture_ = 0;
    GLuint framebuffer_ = 0;
    GLuint shadowFramebuffer_ = 0;
    GLuint thumbnailFramebuffer_ = 0;
    GLuint thumbnailDepthBuffer_ = 0;
    std::unordered_map<std::uint64_t, ModelThumbnailRecord> modelThumbnails_;
    std::uint64_t thumbnailFrame_ = 0;
    std::uint64_t renderFrame_ = 0;
    int thumbnailBudgetRemaining_ = 0;
    GLuint shadowTextureArray_ = 0;
    static constexpr int shadowResolution_ = 256;
    GLuint colorTexture_ = 0;
    GLuint depthBuffer_ = 0;
    GLsizei indexCount_ = 0;
    std::vector<GLuint> textures_;
    std::vector<BrxRenderBatch> batches_;
    std::vector<BrxCelBatch> celBatches_;
    std::vector<BrxCelEdge> celEdges_;
    std::vector<BrxCelEdge> baseCelEdges_;
    std::vector<BrxCelEdge> asegPreviewCelEdges_;
    std::vector<BrxRenderVertex> vertices_;
    std::vector<BrxRenderVertex> baseVertices_;
    std::vector<BrxRenderVertex> asegPreviewVertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<BrxShaderRecord> shaders_;
    std::vector<GpuLight> lights_;
    std::vector<GpuProjectedShadow> projectedShadows_;
    std::vector<std::pair<int, std::int32_t>> dynamicShadowCasters_;
    std::vector<std::vector<std::size_t>> dynamicShadowBatchIndices_;
    std::vector<glm::mat4> dynamicShadowLastModels_;
    bool dynamicShadowMapsDirty_ = true;
    bool dynamicShadowAffectedByWr_ = false;
    struct FastShadowSource
    {
        std::int32_t ownerObjectIndex = -1;
        float radius = 0.0f;
        float depth = 0.0f;
    };
    std::int32_t fastShadowModel_ = -1;
    std::vector<std::size_t> fastShadowBatchIndices_;
    std::vector<FastShadowSource> fastShadowSources_;
    std::vector<std::size_t> lightSourceIndices_;
    std::vector<std::int32_t> lightOwnerIndices_;
    std::int32_t lightIsolationOwner_ = -1;
    std::unordered_set<std::int32_t> gameplayPreviewHiddenObjects_;
    std::unordered_set<std::int32_t> gameplayPreviewOwnHiddenObjects_;
    std::vector<BrxVisibilityNode> visibilityNodes_;
    std::vector<std::uint32_t> visibilityOneHopMasks_;
    bool visibilityZoneColors_ = false;
    std::uint32_t visibilityIsolationMask_ = 0u;
    std::vector<BrxObjectRecord> objects_;
    std::vector<glm::mat4> aloxResolvedWorld_;
    std::vector<glm::mat4> aloxResolvedDeltas_;
    std::vector<glm::mat4> runtimeMotionDeltas_;
    mutable std::vector<glm::mat4> frameSubmissionTransforms_;
    mutable std::vector<std::uint8_t> frameSubmissionTransformValid_;
    std::vector<glm::mat4> asegPreviewDeltas_;
    bool asegPreviewActive_ = false;
    bool asegPreviewGeometryActive_ = false;
    bool dynamicShadowPreviewEnabled_ = true;
    std::vector<glm::vec3> runtimeLocalVelocities_;
    std::vector<glm::vec3> runtimeLocalAngularVelocities_;
    std::vector<BrxGlobWarpRecord> globWarps_;
    std::vector<WrController> wrControllers_;
    std::vector<std::int32_t> logicalOwnerByObject_;
    std::vector<BrxLaserBeamRecord> laserBeams_;
    EmitterPreviewRuntime emitterPreview_;
    EmitterRenderDiagnostics emitterRenderDiagnostics_;
    PerformanceDiagnostics performanceDiagnostics_;
    std::array<GLuint, 4> gpuTimerQueries_{};
    std::array<bool, 4> gpuTimerPending_{};
    std::size_t gpuTimerWriteIndex_ = 0;
    std::vector<std::int32_t> laserTemplateObjects_;
    std::unordered_set<std::int32_t> laserTemplateObjectSet_;
    std::unordered_map<std::uint64_t, const BrxRenderBatch *> surfaceByOwnerGroup_;
    std::unordered_map<std::int32_t, BrxShaderAnimationRecord> eyesAnimationBySecondaryShaderOid_;
    std::vector<std::array<glm::vec3, 2>> editorReferenceLinks_;
    std::size_t visibleBatchCount_ = 0;
    std::size_t culledBatchCount_ = 0;
    std::size_t warpedVertexCount_ = 0;
    float maximumWarpDisplacement_ = 0.0f;
    std::uint32_t cameraZoneMask_ = 0x0fffffffu;
    enum class SelectionScope : std::uint8_t
    {
        None,
        Object,
        Glob,
        Subglob
    };
    std::uint32_t selectedRenderGroup_ = kNoSelection;
    std::uint32_t selectedSubglobFirstIndex_ = kNoSelection;
    std::uint32_t boneWeightPreviewRenderGroup_ = kNoSelection;
    std::uint32_t boneWeightPreviewFirstIndex_ = kNoSelection;
    SelectionScope selectionScope_ = SelectionScope::None;
    std::unordered_set<std::int32_t> selectedObjectIndices_;
    std::unordered_set<std::uint32_t> selectedRenderGroups_;
    std::unordered_set<std::uint64_t> selectedSubglobs_;
    std::unordered_set<std::int32_t> hiddenObjectSelections_;
    std::unordered_set<std::uint32_t> hiddenGlobSelections_;
    std::unordered_set<std::uint64_t> hiddenSubglobSelections_;
    std::unordered_set<std::int32_t> lockedObjectSelections_;
    std::unordered_set<std::uint32_t> lockedGlobSelections_;
    std::unordered_set<std::uint64_t> lockedSubglobSelections_;
    SelectionScope isolationScope_ = SelectionScope::None;
    bool isolationUsesSelection_ = false;
    std::int32_t isolationObjectIndex_ = -1;
    std::uint32_t isolationRenderGroup_ = kNoSelection;
    std::uint32_t isolationSubglobFirstIndex_ = kNoSelection;
    std::int32_t selectedObjectIndex_ = -1;
    std::int32_t selectedVisualObjectIndex_ = -1;
    std::int32_t previewObjectIndex_ = -1;
    glm::mat4 previewObjectDelta_{1.0f};
    float previewObjectAlpha_ = 1.0f;
    bool skyCameraFollowPreview_ = true;
    glm::vec3 skyPreviewCameraPosition_{0.0f};
    std::unordered_map<std::int32_t, glm::mat4> additionalPreviewDeltas_;
    std::unordered_map<std::int32_t, float> additionalPreviewAlphas_;
    std::unordered_map<std::int32_t, glm::mat4> isolatedPreviewDeltas_;
    std::unordered_map<std::int32_t, float> isolatedPreviewAlphas_;
    std::int32_t conveyorPreviewObjectIndex_ = -1;
    std::unordered_map<std::int32_t, std::uint32_t> conveyorPrimaryScrollerRenderGroup_;
    float conveyorPreviewUVelocity_ = 0.0f;
    float conveyorPreviewVVelocity_ = 0.0f;
    float conveyorPreviewStartTime_ = 0.0f;
    bool automaticAloMotion_ = false;
    bool hasAuthoredRuntimeMotion_ = false;
    float runtimeMotionStartTime_ = 0.0f;
    struct PickTarget
    {
        std::int32_t objectIndex = -1;
        std::int32_t visualObjectIndex = -1;
        std::uint32_t renderGroup = kNoSelection;
        bool emitterParticle = false;
    };
    std::vector<PickTarget> pickCycle_;
    std::size_t pickCycleIndex_ = 0;
    float lastPickX_ = -10000.0f;
    float lastPickY_ = -10000.0f;
    bool lastPickWasEmitterParticle_ = false;
    float worldMidtone_ = 0.0f;
    float worldShadow_ = 50.0f / 255.0f;
    float skyColor_[4]{0.0627451f, 0.0627451f, 0.1568627f, 1.0f};
    float fogNear_ = 1000.0f;
    float fogFar_ = 10000.0f;
    float fogMaximum_ = 1.0f;
    float fogColor_[4]{};
    float projectionFov_ = 1.0471975512f;
    float projectionNear_ = 0.1f;
    float projectionFar_ = 1000000.0f;
    float cameraMrdRatio_ = 1.0f;
    int width_ = 0;
    int height_ = 0;
};
