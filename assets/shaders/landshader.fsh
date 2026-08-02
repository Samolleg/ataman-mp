#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D qt_Texture0;
varying highp vec2 qt_TexCoord0;
uniform highp vec4 u_lightPosition;
uniform highp float u_lightPower;
uniform highp vec4 u_color;
uniform highp vec3 u_watcherPos;
uniform highp vec4 u_lightColor;
uniform bool u_highlight;
uniform float transparency;
uniform bool u_solidColorEnabled;
uniform highp vec4 u_solidColor;
uniform bool u_environmentEnabled;
uniform int u_lightingMode;
uniform bool u_fogEnabled;
uniform float u_ambientStrength;
uniform float u_sunStrength;
uniform vec3 u_sunDirection;
uniform vec3 u_sunColor;
uniform vec3 u_ambientColor;
uniform vec3 u_fogColor;
uniform float u_fogStart;
uniform float u_fogEnd;
uniform vec3 u_mapFog;
uniform int u_pointLightCount;
uniform vec4 u_pointLightPositionRange[64];
uniform vec3 u_pointLightColor[64];
uniform bool u_shadowsEnabled;
uniform sampler2D u_staticShadowMap;
uniform sampler2D u_dynamicShadowMap;
uniform sampler2D u_farStaticShadowMap;
uniform sampler2D u_farDynamicShadowMap;
uniform sampler2D u_localShadowMap;
uniform int u_localShadowCount;
uniform int u_localShadowSkipIndex;
uniform mat4 u_localShadowMatrices[16];
uniform vec4 u_localShadowAtlasRects[16];
uniform vec2 u_localShadowTexelSize;
uniform mat4 u_staticLightSpaceMatrix;
uniform mat4 u_dynamicLightSpaceMatrix;
uniform mat4 u_farStaticLightSpaceMatrix;
uniform mat4 u_farDynamicLightSpaceMatrix;
uniform vec2 u_staticShadowTexelSize;
uniform vec2 u_dynamicShadowTexelSize;
uniform vec2 u_farStaticShadowTexelSize;
uniform vec2 u_farDynamicShadowTexelSize;
uniform float u_nearShadowSplit;
uniform bool u_shadowCascadeDebug;
uniform bool u_receiveStaticShadow;
uniform bool u_receiveDynamicShadow;
uniform bool u_terrainShadowEnabled;
uniform sampler2D u_terrainShadowMap;
uniform vec2 u_terrainShadowTextureSize;

varying highp vec4 v_position;
varying highp vec3 v_normal;
varying highp vec2 v_texture;
varying highp vec3 v_worldPosition;
varying highp float v_viewDepth;
varying highp vec3 v_materialEmission;

float terrainShadowVisibility()
{
  if (!u_terrainShadowEnabled)
    return 1.0;
  vec2 halfTexel = vec2(0.5) / u_terrainShadowTextureSize;
  vec2 uv = (v_worldPosition.xy + vec2(0.5)) / u_terrainShadowTextureSize;
  uv = clamp(uv, halfTexel, vec2(1.0) - halfTexel);
  return texture2D(u_terrainShadowMap, uv).r;
}

float unpackShadowDepth(vec4 encodedDepth)
{
  return dot(encodedDepth, vec4(1.0, 1.0 / 255.0,
                          1.0 / 65025.0, 1.0 / 16581375.0));
}

float borderFog(vec3 world)
{
  float border = u_mapFog.z;
  if (border <= 0.0)
    return 0.0;
  float distanceToEdge = min(min(world.x, world.y),
                             min(u_mapFog.x - world.x, u_mapFog.y - world.y));
  return clamp((border - distanceToEdge) / border, 0.0, 1.0);
}

float cascadeVisibility(vec3 staticProjected, vec3 dynamicProjected,
                        bool staticInside, bool dynamicInside,
                        sampler2D staticMap, sampler2D dynamicMap,
                        vec2 staticTexel, vec2 dynamicTexel,
                        float staticBias, float dynamicBias)
{
  float visibility = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
      vec2 offset = vec2(float(x), float(y));
      float staticDepth = staticInside
          ? unpackShadowDepth(texture2D(staticMap, staticProjected.xy + offset * staticTexel))
          : 1.0;
      float dynamicDepth = dynamicInside
          ? unpackShadowDepth(texture2D(dynamicMap, dynamicProjected.xy + offset * dynamicTexel))
          : 1.0;
      bool staticOccluded = staticInside && staticProjected.z - staticBias > staticDepth;
      bool dynamicOccluded = dynamicInside && dynamicProjected.z - dynamicBias > dynamicDepth;
      visibility += (staticOccluded || dynamicOccluded) ? 0.0 : 1.0;
    }
  visibility /= 9.0;
  return visibility;
}

float localShadowVisibility(vec3 world)
{
  float visibility = 1.0;
  for (int caster = 0; caster < 16; ++caster)
  {
    if (caster >= u_localShadowCount)
      break;
    if (caster == u_localShadowSkipIndex)
      continue;
    vec4 clip = u_localShadowMatrices[caster] * vec4(world, 1.0);
    vec3 projected = clip.xyz / clip.w * 0.5 + 0.5;
    if (projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0 ||
        projected.z < 0.0 || projected.z > 1.0)
      continue;
    vec4 rect = u_localShadowAtlasRects[caster];
    vec2 atlas = rect.xy + projected.xy * rect.zw;
    atlas = clamp(atlas, rect.xy + u_localShadowTexelSize,
                         rect.xy + rect.zw - u_localShadowTexelSize);
    float depth = unpackShadowDepth(texture2D(u_localShadowMap, atlas));
    visibility = min(visibility,
        projected.z - 0.000002 > depth ? 0.0 : 1.0);
  }
  return visibility;
}

void main(void)
{
  vec3 norm = normalize(v_normal);
  vec4 texColor = texture2D(qt_Texture0, v_texture);
  if (texColor.a <= 0.1)
    discard;
  vec3 light = vec3(1.0);
  vec3 cascadeDebugColor = vec3(0.0);
  if (u_environmentEnabled)
  {
    if (u_lightingMode == 1)
    {
      float simpleDiffuse = max(dot(norm, normalize(vec3(0.35, 0.45, 0.82))), 0.0);
      light = vec3(0.62 + 0.52 * simpleDiffuse);
    }
    else
    {
    float sunDiffuse = max(dot(norm, -normalize(u_sunDirection)), 0.0);
    float shadow = 1.0;
    if (u_shadowsEnabled)
    {
      vec4 staticPosition = u_staticLightSpaceMatrix * vec4(v_worldPosition, 1.0);
      vec4 dynamicPosition = u_dynamicLightSpaceMatrix * vec4(v_worldPosition, 1.0);
      vec4 farStaticPosition = u_farStaticLightSpaceMatrix * vec4(v_worldPosition, 1.0);
      vec4 farDynamicPosition = u_farDynamicLightSpaceMatrix * vec4(v_worldPosition, 1.0);
      vec3 staticProjected = staticPosition.xyz / staticPosition.w * 0.5 + 0.5;
      vec3 dynamicProjected = dynamicPosition.xyz / dynamicPosition.w * 0.5 + 0.5;
      vec3 farStaticProjected = farStaticPosition.xyz / farStaticPosition.w * 0.5 + 0.5;
      vec3 farDynamicProjected = farDynamicPosition.xyz / farDynamicPosition.w * 0.5 + 0.5;
      bool staticInside = u_receiveStaticShadow && staticProjected.x >= 0.0 && staticProjected.x <= 1.0 &&
          staticProjected.y >= 0.0 && staticProjected.y <= 1.0 && staticProjected.z <= 1.0;
      bool dynamicInside = u_receiveDynamicShadow && dynamicProjected.x >= 0.0 && dynamicProjected.x <= 1.0 &&
          dynamicProjected.y >= 0.0 && dynamicProjected.y <= 1.0 && dynamicProjected.z <= 1.0;
      bool farStaticInside = u_receiveStaticShadow && farStaticProjected.x >= 0.0 && farStaticProjected.x <= 1.0 &&
          farStaticProjected.y >= 0.0 && farStaticProjected.y <= 1.0 && farStaticProjected.z <= 1.0;
      bool farDynamicInside = u_receiveDynamicShadow && farDynamicProjected.x >= 0.0 && farDynamicProjected.x <= 1.0 &&
          farDynamicProjected.y >= 0.0 && farDynamicProjected.y <= 1.0 && farDynamicProjected.z <= 1.0;
      bool nearValid = staticInside || dynamicInside;
      bool farValid = farStaticInside || farDynamicInside;
      if (nearValid || farValid)
      {
        float staticBias = max(0.000012 * (1.0 - sunDiffuse), 0.000002);
        float dynamicBias = 0.000001;
        float blendStart = u_nearShadowSplit * 0.78;
        float cascadeBlend = smoothstep(u_nearShadowSplit * 0.78,
                                        u_nearShadowSplit, v_viewDepth);
        cascadeDebugColor = nearValid && farValid
            ? mix(vec3(1.0, 0.12, 0.05), vec3(0.05, 0.25, 1.0), cascadeBlend)
            : (nearValid ? vec3(1.0, 0.12, 0.05) : vec3(0.05, 0.25, 1.0));
        if (nearValid && (!farValid || v_viewDepth <= blendStart))
        {
          shadow = cascadeVisibility(
              staticProjected, dynamicProjected, staticInside, dynamicInside,
              u_staticShadowMap, u_dynamicShadowMap,
              u_staticShadowTexelSize, u_dynamicShadowTexelSize,
              staticBias, dynamicBias);
        }
        else if (farValid && (!nearValid || v_viewDepth >= u_nearShadowSplit))
        {
          shadow = cascadeVisibility(
              farStaticProjected, farDynamicProjected, farStaticInside, farDynamicInside,
              u_farStaticShadowMap, u_farDynamicShadowMap,
              u_farStaticShadowTexelSize, u_farDynamicShadowTexelSize,
              staticBias, dynamicBias);
        }
        else
        {
          float nearVisibility = cascadeVisibility(
              staticProjected, dynamicProjected, staticInside, dynamicInside,
              u_staticShadowMap, u_dynamicShadowMap,
              u_staticShadowTexelSize, u_dynamicShadowTexelSize,
              staticBias, dynamicBias);
          float farVisibility = cascadeVisibility(
              farStaticProjected, farDynamicProjected, farStaticInside, farDynamicInside,
              u_farStaticShadowMap, u_farDynamicShadowMap,
              u_farStaticShadowTexelSize, u_farDynamicShadowTexelSize,
              staticBias, dynamicBias);
          shadow = mix(nearVisibility, farVisibility, cascadeBlend);
        }
      }
      shadow = min(shadow, localShadowVisibility(v_worldPosition));
    }
    // Stock shadows are an 80/255-alpha projected silhouette over the
    // saturated environment result. Point lights are compared afterwards.
    vec3 directLight = min(u_sunColor * sunDiffuse + v_materialEmission,
                           vec3(1.0));
    vec3 environmentLight = max(u_ambientColor, directLight);
    // Terrain and object blockers are one strongest-blocker silhouette. Their
    // overlap must not apply the stock projected-shadow opacity twice.
    float combinedShadowVisibility = min(shadow,
        clamp(terrainShadowVisibility(), 0.0, 1.0));
    float projectedShadowVisibility = mix(1.0, combinedShadowVisibility,
                                          80.0 / 255.0);
    light = environmentLight * projectedShadowVisibility;
    for (int i = 0; i < 64; ++i)
    {
      if (i >= u_pointLightCount)
        break;
      vec3 offset = u_pointLightPositionRange[i].xyz - v_worldPosition;
      float range = max(u_pointLightPositionRange[i].w, 0.001);
      float distanceSquared = dot(offset, offset);
      float attenuation = max(0.0,
          1.0 - distanceSquared / (range * range));
      vec3 lightDirection = offset * inversesqrt(max(distanceSquared, 0.000001));
      float normalFactor = 1.0 + min(dot(norm, lightDirection), 0.0);
      vec3 pointCandidate = u_pointLightColor[i] * attenuation * normalFactor;
      light = max(light, pointCandidate);
    }
    }
  }
  vec4 resColor = vec4(clamp(light, 0.0, 1.0), 1.0) * texColor;
  if (u_environmentEnabled && u_fogEnabled)
  {
    float fog = max(clamp((v_viewDepth - u_fogStart) /
                          max(1.0, u_fogEnd - u_fogStart), 0.0, 1.0),
                    borderFog(v_worldPosition));
    resColor.rgb = mix(resColor.rgb, u_fogColor, fog);
  }

  resColor.a -= transparency;

  if (u_shadowCascadeDebug)
    resColor.rgb = cascadeDebugColor;


  gl_FragColor = u_solidColorEnabled ? u_solidColor : resColor;

}
