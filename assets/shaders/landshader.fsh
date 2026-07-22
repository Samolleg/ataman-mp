uniform sampler2D qt_Texture0;
varying highp vec2 qt_TexCoord0;
uniform highp vec4 u_lightPosition;
uniform highp float u_lightPower;
uniform highp vec4 u_color;
uniform highp vec3 u_watcherPos;
uniform highp vec4 u_lightColor;
uniform bool u_highlight;
uniform float transparency;
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
uniform vec4 u_pointLightPositionRange[8];
uniform vec3 u_pointLightColor[8];
uniform bool u_shadowsEnabled;
uniform sampler2D u_staticShadowMap;
uniform sampler2D u_dynamicShadowMap;
uniform sampler2D u_farStaticShadowMap;
uniform sampler2D u_farDynamicShadowMap;
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
uniform bool u_animateLiquid;
uniform float u_liquidTime;

varying highp vec4 v_position;
varying highp vec3 v_normal;
varying highp vec2 v_texture;
varying highp vec3 v_worldPosition;
varying highp float v_viewDepth;

float unpackShadowDepth(vec4 packed)
{
  return dot(packed, vec4(1.0, 1.0 / 255.0,
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
  bool centerOccluded = false;
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
      if (x == 0 && y == 0)
        centerOccluded = staticOccluded || dynamicOccluded;
      visibility += (staticOccluded || dynamicOccluded) ? 0.58 : 1.0;
    }
  visibility /= 9.0;
  return centerOccluded ? min(visibility, 0.58) : visibility;
}

void main(void)
{
  vec3 norm = normalize(v_normal);
  vec2 sampledUv = v_texture;
  if (u_animateLiquid)
  {
    // EI liquid overlays are authored in cells of the terrain atlas. Keep
    // motion smaller than the four-pixel DXT border trimmed by CTile so a
    // water/lava cell can never drift into its neighbour during a long run.
    float phase = u_liquidTime * 1.15;
    float wave = sin(v_worldPosition.x * 0.085 + phase)
               + cos(v_worldPosition.y * 0.072 - phase * 0.78);
    sampledUv += vec2(sin(phase) * 0.00042 + wave * 0.00012,
                      cos(phase * 0.83) * 0.00042 + wave * 0.00010);
  }
  vec4 texColor = texture2D(qt_Texture0, sampledUv);
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
        // Terrain never writes either shadow map, so receiver bias can remain
        // small without terrain self-acne. Larger values detach contact points.
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
    }
    float skyExposure = 0.88 + 0.12 * max(norm.z, 0.0);
    vec3 neutralAmbient = mix(vec3(1.0), u_ambientColor, 0.28);
    float ambientLevel = 0.32 + 0.68 * u_ambientStrength;
    light = (neutralAmbient * (ambientLevel * skyExposure)
          + u_sunColor * (u_sunStrength * sunDiffuse)) * shadow;
    for (int i = 0; i < 8; ++i)
    {
      if (i >= u_pointLightCount)
        break;
      vec3 offset = u_pointLightPositionRange[i].xyz - v_worldPosition;
      float range = max(u_pointLightPositionRange[i].w, 0.001);
      float distanceToLight = length(offset);
      float attenuation = max(0.0, 1.0 - distanceToLight / range);
      attenuation *= attenuation;
      light += u_pointLightColor[i] * attenuation
             * max(dot(norm, normalize(offset)), 0.0);
    }
    }
  }
  vec4 resColor = vec4(light, 1.0) * texColor;
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


  gl_FragColor = resColor;

}
