#include "RenderShaders.h"

namespace RenderShaders
{
const char *const kBlipVertex = R"(
#version 430 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec3 aPosition;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec4 aLifeRollScale;
layout(location = 5) in vec4 aVelocityMode;
uniform mat4 uViewProjection;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uFlyingScale;
out vec2 vUv;
out vec4 vColor;
void main()
{
    float age = aLifeRollScale.x;
    float lifetime = max(aLifeRollScale.y, 0.0001);
    float roll = aLifeRollScale.z;
    float authoredScale = aLifeRollScale.w;
    float scale = authoredScale;
    int movementKind = int(aVelocityMode.w + 0.5);
    vec3 world;
    if (movementKind == 1)
    {
        vec3 line = aVelocityMode.xyz * uFlyingScale;
        vec2 projected = vec2(dot(line, uCameraRight), dot(line, uCameraUp));
        float projectedLength = length(projected);
        vec2 direction = projectedLength > 0.0001 ? projected / projectedLength : vec2(0.0, 1.0);
        float stripLength = max(projectedLength, abs(scale) * 2.0);
        vec3 along = uCameraRight * direction.x + uCameraUp * direction.y;
        vec3 across = uCameraRight * -direction.y + uCameraUp * direction.x;
        world = aPosition + along * ((aCorner.y * 0.5 + 0.5) * stripLength) +
            across * aCorner.x * scale;
    }
    else
    {
        vec2 rotated = movementKind == 2
            ? mat2(cos(roll), -sin(roll), sin(roll), cos(roll)) * aCorner
            : aCorner;
        world = aPosition + (uCameraRight * rotated.x + uCameraUp * rotated.y) * scale;
    }
    gl_Position = uViewProjection * vec4(world, 1.0);
    vUv = aUv;
    vColor = aColor;
}
)";

const char *const kBlipFragment = R"(
#version 430 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTexture;
layout(location = 0) out vec4 oColor;
void main()
{
    vec4 sampled = texture(uTexture, vUv) * vColor;
    if (sampled.a <= 0.0039215686) discard;
    oColor = sampled;
}
)";

const char *const kGlobVertex = R"(
#version 430 core
#define MAX_LIGHTS 256
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
layout(location=3) in vec2 aUv;
layout(location=4) in float aBoneWeightPreview;
uniform mat4 uViewProjection;
uniform mat4 uModel;
uniform int uInstanceMode;
struct GlobInstance {
    mat4 model;
    vec4 objectCenter;
};
layout(std430, binding = 9) readonly buffer GlobInstanceBuffer {
    GlobInstance globInstances[];
};
uniform int uThreeWay;
uniform int uLightCount;
uniform int uActiveLightCount;
uniform int uActiveLightIndices[MAX_LIGHTS];
uniform int uDynamicObject;
uniform vec3 uObjectCenter;
uniform float uObjectRadius;
struct LightBlock {
    ivec4 meta;
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 ru;
    vec4 du;
    // x/y are the reciprocal point-light constant/inv-distance terms.
    // z is the maximum light distance; w is unused.
    vec4 falloffCone;
    mat4 frustum;
    vec4 falloffScale;
    vec4 falloffBias;
};
layout(std430, binding = 2) readonly buffer LightBuffer {
    LightBlock lights[];
};
uniform float uWorldShadow;
uniform float uWorldMidtone;
uniform float uSelfIllumination;
out vec3 vWorldPosition;
out vec4 vColor;
out vec2 vUv;
out float vAmbient;
out vec3 vMidtone;
out vec3 vHighlight;
out float vBoneWeightPreview;
flat out vec4 vObjectSphere;
vec3 safeNormalize(vec3 value) {
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-12 ? value * inversesqrt(lengthSquared) : vec3(0.0);
}
void main() {
    mat4 model = uInstanceMode != 0 ? globInstances[gl_InstanceID].model : uModel;
    vec4 instanceCenter = uInstanceMode != 0
        ? globInstances[gl_InstanceID].objectCenter
        : vec4(uObjectCenter, uObjectRadius);
    vec3 objectCenter = instanceCenter.xyz;
    float objectRadius = instanceCenter.w;
    vec4 worldPosition = model * vec4(aPosition, 1.0);
    // Retail glob.vert uses mat3(op.model) directly.  Keep editor placement,
    // RTCK and animation preview lighting on the same convention.
    vec3 worldNormal = mat3(model) * aNormal;
    gl_Position = uViewProjection * worldPosition;
    vWorldPosition = worldPosition.xyz;
    vColor = aColor;
    vUv = aUv;
    vBoneWeightPreview = aBoneWeightPreview;
    vObjectSphere = vec4(objectCenter, objectRadius);
    vAmbient = 0.0;
    vMidtone = aColor.rgb;
    vHighlight = vec3(0.0);
    if (uThreeWay != 0) {
        vec3 normal = safeNormalize(worldNormal);
        float objectShadow = uWorldShadow;
        float objectMidtone = uWorldMidtone + uSelfIllumination * 0.000031;
        vec3 highlightLight = vec3(0.0);
        for (int activeIndex = 0;
            activeIndex < min(uActiveLightCount, MAX_LIGHTS); ++activeIndex) {
            int i = uActiveLightIndices[activeIndex];
            if (i < 0 || i >= min(uLightCount, MAX_LIGHTS)) continue;
            // ProjectCane LIGHTBLK metadata: x kind, y excludes dynamic
            // objects, z active, w dynamic-light membership.
            if (lights[i].meta.z == 0) continue;
            // LIGHTK_Nil and corrupt/unknown values take ProjectCane's
            // switch-default path and contribute nothing.
            if (lights[i].meta.x < 0 || lights[i].meta.x > 3) continue;
            if (uDynamicObject != 0 && lights[i].meta.y != 0) continue;
            // ProjectCane builds a per-GLOB light list with sphere tests
            // before vertex lighting.  The editor keeps one shared GPU light
            // table, so perform the equivalent cheap object-sphere rejection
            // before any per-vertex reciprocal/frustum work.
            if (lights[i].meta.x != 0 && lights[i].falloffCone.z > 0.0) {
                vec3 centerToLight = lights[i].position.xyz - objectCenter;
                float reach = lights[i].falloffCone.z + objectRadius;
                if (dot(centerToLight, centerToLight) > reach * reach) continue;
            }
            vec3 L;
            float attenuation = 1.0;
            if (lights[i].meta.x == 0) {
                L = safeNormalize(lights[i].direction.xyz);
            } else {
                // GLOB::fDynamic uses one light vector/falloff value at the
                // GLOB center; static GLOBs evaluate each transformed vertex.
                vec3 lightingPosition = uDynamicObject != 0
                    ? objectCenter : worldPosition.xyz;
                vec3 toLight = lights[i].position.xyz - lightingPosition;
                float distanceSquared = dot(toLight, toLight);
                if (distanceSquared <= 1e-12) continue;
                float inverseDistance = inversesqrt(distanceSquared);
                L = toLight * inverseDistance;
                if (lights[i].meta.x == 1) {
                    // FitRecipFunction/pointlight.constant + invDst / d.
                    attenuation = clamp(lights[i].falloffCone.x +
                        lights[i].falloffCone.y * inverseDistance, 0.0, 1.0);
                } else {
                    // ProjectCane AddFrustrumLight evaluates the light's clip
                    // volume and four independent linear falloff channels.
                    vec4 clipL = lights[i].frustum * vec4(lightingPosition, 1.0);
                    if (clipL.w <= 0.0 || abs(clipL.x) > clipL.w ||
                        abs(clipL.y) > clipL.w || abs(clipL.z) > clipL.w)
                        continue;
                    float invW = 1.0 / clipL.w;
                    float u = abs(clipL.x * invW);
                    float v = abs(clipL.y * invW);
                    float radial = u * u + v * v;
                    attenuation =
                        clamp(lights[i].falloffScale.x * u + lights[i].falloffBias.x, 0.0, 1.0) *
                        clamp(lights[i].falloffScale.y * v + lights[i].falloffBias.y, 0.0, 1.0) *
                        clamp(lights[i].falloffScale.z * radial + lights[i].falloffBias.z, 0.0, 1.0) *
                        clamp(lights[i].falloffScale.w * clipL.w + lights[i].falloffBias.w, 0.0, 1.0);
                }
            }
            float curve = dot(normal, L);
            curve += curve * curve * curve;
            objectShadow += max(curve * lights[i].ru.x + lights[i].du.x, 0.0) * attenuation;
            objectMidtone += max(curve * lights[i].ru.y + lights[i].du.y, 0.0) * attenuation;
            float highlight = max(curve * lights[i].ru.z + lights[i].du.z, 0.0) * attenuation;
            highlightLight += lights[i].color.rgb * highlight;
        }
        float dominantLight = max(max(highlightLight.r, highlightLight.g), highlightLight.b);
        float shadowModifier = 1.0 - dominantLight;
        float clampedMidtone = clamp(objectMidtone, 0.0, shadowModifier);
        float shadowContribution = max(min(shadowModifier - clampedMidtone, objectShadow), 0.0);
        float baseIntensity = dot(aColor.rgb, vec3(0.3333333));
        vAmbient = shadowContribution * baseIntensity;
        vMidtone = clampedMidtone * aColor.rgb;
        vHighlight = min(highlightLight, vec3(1.0)) * baseIntensity;
    }
})";

const char *const kGlobFragment = R"(
#version 430 core
in vec3 vWorldPosition;
in vec4 vColor;
in vec2 vUv;
in float vAmbient;
in vec3 vMidtone;
in vec3 vHighlight;
in float vBoneWeightPreview;
flat in vec4 vObjectSphere;
uniform sampler2D uAmbient;
uniform sampler2D uDiffuse;
uniform sampler2D uSaturate;
uniform int uHasAmbient;
uniform int uHasDiffuse;
uniform int uHasSaturate;
uniform int uThreeWay;
uniform vec3 uCameraPosition;
uniform vec4 uFogColor;
uniform vec3 uFogSettings;
uniform int uFogType;
uniform float uObjectFog;
uniform float uObjectAlpha;
uniform int uAlphaPass;
uniform float uAlphaCutoff;
uniform int uProjectedVolumeFinalPass;
uniform vec4 uProjectedVolumeColor;
uniform int uSelectionOverlay;
uniform vec4 uSelectionColor;
uniform vec2 uUvOffset;
uniform int uBoneWeightPreview;
uniform int uShadowCount;
uniform int uShadowReceiverMask;
uniform int uDynamicShadowPreviewEnabled;
uniform sampler2DArray uShadowTextureArray;
struct ProjectedShadow {
    mat4 worldToUv;
    vec4 rgba;
    vec4 depth; // wMin, wMax, wFadeMin, dynamic
    ivec4 meta; // layer, shader kind, clampS, clampT
    vec4 sphere;
};
layout(std430, binding=4) readonly buffer ShadowBuffer {
    ProjectedShadow projectedShadows[];
};
out vec4 outColor;
float sampleProjectedShadow(ProjectedShadow shadow, vec3 worldPosition) {
    vec4 projected = shadow.worldToUv * vec4(worldPosition, 1.0);
    if (any(isnan(projected)) || any(isinf(projected))) return 0.0;
    float depth = projected.w;
    float oldDepth = depth;
    vec2 uvq = projected.xy;
    if (shadow.depth.y < depth) {
        float delta = shadow.depth.y - depth;
        uvq += vec2(delta * 0.5);
        depth += delta;
    } else if (depth < shadow.depth.x) {
        float delta = shadow.depth.x - depth;
        uvq += vec2(delta * 0.5);
        depth += delta;
        if (oldDepth < shadow.depth.z) return 0.0;
    }
    if (abs(depth) < 0.00001 || isnan(depth) || isinf(depth)) return 0.0;
    vec2 uv = uvq / depth;
    if (shadow.depth.w != 0.0 && (any(lessThan(uv, vec2(0.0))) ||
        any(greaterThan(uv, vec2(1.0))))) return 0.0;
    vec2 halfTexel = 0.5 / vec2(textureSize(uShadowTextureArray, 0).xy);
    if (shadow.depth.w != 0.0 || shadow.meta.z != 0)
        uv.x = clamp(uv.x, halfTexel.x, 1.0-halfTexel.x);
    if (shadow.depth.w != 0.0 || shadow.meta.w != 0)
        uv.y = clamp(uv.y, halfTexel.y, 1.0-halfTexel.y);
    return texture(uShadowTextureArray, vec3(uv, float(shadow.meta.x))).a;
}
void main() {
    if (uBoneWeightPreview != 0) {
        float w = clamp(vBoneWeightPreview, 0.0, 1.0);
        vec3 heat = w < 0.25 ? mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), w * 4.0) :
            (w < 0.5 ? mix(vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), (w - 0.25) * 4.0) :
            (w < 0.75 ? mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (w - 0.5) * 4.0) :
            mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (w - 0.75) * 4.0)));
        outColor = vec4(heat, 1.0);
        return;
    }
    vec2 animatedUv = vUv + uUvOffset;
    vec4 diffuse = uHasDiffuse != 0 ? texture(uDiffuse, animatedUv) : vec4(1.0);
    // ProjectCane's AlphaTest uses only texture/vertex alpha. Object fade
    // (including GLEAM) is applied afterward to the framebuffer alpha.
    float materialAlpha = diffuse.a * vColor.a;
    if (uAlphaPass == 1 && materialAlpha < uAlphaCutoff) discard;
    float sourceAlpha = materialAlpha * uObjectAlpha;
    vec3 color;
    float outputAlpha = sourceAlpha;
    if (uProjectedVolumeFinalPass == 1) {
        color = uProjectedVolumeColor.rgb;
        outputAlpha = clamp(uProjectedVolumeColor.a * uObjectAlpha, 0.0, 1.0);
    } else if (uProjectedVolumeFinalPass == 2) {
        color = vColor.rgb * diffuse.rgb;
        outputAlpha = clamp(vColor.a * diffuse.a * uObjectAlpha, 0.0, 1.0);
    } else if (uThreeWay != 0) {
        vec3 ambient = uHasAmbient != 0 ? texture(uAmbient, animatedUv).rgb : diffuse.rgb;
        vec3 saturate = uHasSaturate != 0 ? texture(uSaturate, animatedUv).rgb : diffuse.rgb;
        color = ambient * vAmbient + diffuse.rgb * vMidtone + saturate * vHighlight;
    } else {
        color = vColor.rgb * diffuse.rgb * uProjectedVolumeColor.rgb;
        outputAlpha *= uProjectedVolumeColor.a;
    }
    if (uProjectedVolumeFinalPass == 0 && uShadowReceiverMask != 0) {
        int appliedShadows=0;
        for (int shadowIndex=0; shadowIndex<min(uShadowCount,64); ++shadowIndex) {
            ProjectedShadow shadow=projectedShadows[shadowIndex];
            // The DYSH preview switch controls only frame-generated dynamic
            // silhouettes.  Retail static projected shadows remain active;
            // suppressing uShadowCount used to incorrectly disable both.
            if (shadow.depth.w != 0.0 && uDynamicShadowPreviewEnabled == 0) continue;
            int receiverBit=shadow.meta.y==2 ? 1 : (shadow.meta.y==3 ? 2 : 0);
            if ((uShadowReceiverMask & receiverBit)==0) continue;
            // ProjectCane's FShadowIntersectsSphere accepts a receiver by its
            // GLOB/SUBGLOB bounding sphere. Testing each fragment instead
            // rejects large receiving models whose center/radius intersects
            // the projector even though a particular fragment lies outside
            // the projector's effect sphere.
            vec3 delta=vObjectSphere.xyz-shadow.sphere.xyz;
            float combined=shadow.sphere.w+vObjectSphere.w;
            if (dot(delta,delta)>combined*combined) continue;
            float amount=sampleProjectedShadow(shadow,vWorldPosition)*clamp(shadow.rgba.a,0.0,1.0);
            color *= shadow.depth.w!=0.0 ? vec3(1.0-amount) : vec3(1.0+amount);
            if (++appliedShadows >= 4) break;
        }
    }
    float distanceToCamera = length(uCameraPosition - vWorldPosition);
    float fog = 0.0;
    if (uFogType == 1) {
        float reciprocalDepth = 1.0 / max(distanceToCamera, 0.0001);
        float reciprocalNear = 1.0 / max(abs(uFogSettings.x), 0.000001);
        float reciprocalFar = 1.0 / max(abs(uFogSettings.y), 0.000001);
        fog = clamp((reciprocalNear - reciprocalDepth) /
            max(reciprocalNear - reciprocalFar, 0.000001), 0.0, 1.0);
    } else if (uFogType == 2) {
        fog = clamp((distanceToCamera - uFogSettings.x) /
            max(uFogSettings.y - uFogSettings.x, 0.000001), 0.0, 1.0);
    }
    float fogMultiplier = uObjectFog > 0.001
        ? uFogSettings.z * uObjectFog : uFogSettings.z;
    fog = uFogType == 0 ? 0.0 : clamp(fog * fogMultiplier * uFogColor.a, 0.0, 1.0);
    color = mix(color, uFogColor.rgb, fog);
    if (uSelectionOverlay != 0) {
        color = mix(color, uSelectionColor.rgb, uSelectionColor.a);
        // Selection is an editor-only RGB tint.  Do not make the selected
        // surface opaque: doing so masks RenderAloAll/RenderAloGlobset MRD
        // fading while an object is selected and causes an abrupt pop at the
        // final cull distance.
    }
    outColor = vec4(color, outputAlpha);
})";

const char *const kCelVertex = R"(
#version 430 core
layout(std430, binding=8) readonly buffer EdgeBuffer { vec4 edgePoints[]; };
uniform mat4 uViewProjection;
uniform mat4 uModel;
uniform uint uFirstEdge;
void cullVertex() { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }
float cross2d(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
void main() {
    uint edge = uFirstEdge + uint(gl_VertexID >> 1);
    uint point = edge * 4u;
    vec4 A = uViewProjection * uModel * vec4(edgePoints[point + 0u].xyz, 1.0);
    vec4 B = uViewProjection * uModel * vec4(edgePoints[point + 1u].xyz, 1.0);
    vec4 C = uViewProjection * uModel * vec4(edgePoints[point + 2u].xyz, 1.0);
    vec4 D = uViewProjection * uModel * vec4(edgePoints[point + 3u].xyz, 1.0);
    if (A.w <= 0.0 || B.w <= 0.0 || C.w <= 0.0 || D.w <= 0.0) {
        cullVertex(); return;
    }
    vec3 a = A.xyz / A.w;
    vec3 b = B.xyz / B.w;
    vec3 c = C.xyz / C.w;
    vec3 d = D.xyz / D.w;
    float side0 = cross2d(b.xy - a.xy, c.xy - b.xy);
    float side1 = cross2d(d.xy - b.xy, b.xy - a.xy);
    if (side0 * side1 > 0.0) { cullVertex(); return; }
    gl_Position = (gl_VertexID & 1) == 0 ? A : B;
})";

const char *const kCelFragment = R"(
#version 430 core
uniform vec4 uCelColor;
layout(location=0) out vec4 outColor;
void main() { outColor = uCelColor; }
)";
}
