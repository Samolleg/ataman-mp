uniform sampler2D qt_Texture0;
varying highp vec2 qt_TexCoord0;
uniform bool u_highlight;
uniform vec4 u_highlightColor;
//varying highp vec4 v_position;
//varying highp vec3 v_normal;
varying highp vec2 v_texture;
uniform vec4 customColor;
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
uniform mat4 u_staticLightSpaceMatrix;
uniform mat4 u_dynamicLightSpaceMatrix;
uniform vec2 u_staticShadowTexelSize;
uniform vec2 u_dynamicShadowTexelSize;
uniform bool u_receiveDynamicShadow;
uniform bool u_receiveStaticShadow;
varying highp vec3 v_normal;
varying highp vec3 v_worldPosition;
varying highp float v_viewDepth;

float borderFog(vec3 world)
{
  float border = u_mapFog.z;
  if (border <= 0.0)
    return 0.0;
  float left = min(world.x - border - 1.0, 0.0);
  float bottom = min(world.y - border - 1.0, 0.0);
  float right = min(u_mapFog.x - border - world.x - 1.0, 0.0);
  float top = min(u_mapFog.y - border - world.y - 1.0, 0.0);
  return clamp(sqrt(left * left + bottom * bottom + right * right + top * top)
               / border, 0.0, 1.0);
}

void main(void)
{    
    vec4 resColor = customColor.w == 0.0 ? texture2D(qt_Texture0, v_texture) : customColor;

    if (u_environmentEnabled && customColor.w == 0.0)
    {
      vec3 normal = normalize(v_normal);
      if (u_lightingMode == 1)
      {
        float simpleDiffuse = max(dot(normal, normalize(vec3(0.35, 0.45, 0.82))), 0.0);
        resColor.rgb *= vec3(0.62 + 0.52 * simpleDiffuse);
      }
      else
      {
      float sunDiffuse = max(dot(normal, -normalize(u_sunDirection)), 0.0);
      float shadow = 1.0;
      if (u_shadowsEnabled && sunDiffuse > 0.0)
      {
        vec4 staticPosition = u_staticLightSpaceMatrix * vec4(v_worldPosition, 1.0);
        vec4 dynamicPosition = u_dynamicLightSpaceMatrix * vec4(v_worldPosition, 1.0);
        vec3 staticProjected = staticPosition.xyz / staticPosition.w * 0.5 + 0.5;
        vec3 dynamicProjected = dynamicPosition.xyz / dynamicPosition.w * 0.5 + 0.5;
        bool staticInside = u_receiveStaticShadow && staticProjected.x >= 0.0 && staticProjected.x <= 1.0 &&
            staticProjected.y >= 0.0 && staticProjected.y <= 1.0 && staticProjected.z <= 1.0;
        bool dynamicInside = u_receiveDynamicShadow && dynamicProjected.x >= 0.0 && dynamicProjected.x <= 1.0 &&
            dynamicProjected.y >= 0.0 && dynamicProjected.y <= 1.0 && dynamicProjected.z <= 1.0;
        if (staticInside || dynamicInside)
        {
          float bias = max(0.0035 * (1.0 - sunDiffuse), 0.0012);
          float visibility = 0.0;
          for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
            {
              vec2 offset = vec2(float(x), float(y));
              float staticDepth = staticInside
                  ? texture2D(u_staticShadowMap,
                      staticProjected.xy + offset * u_staticShadowTexelSize).r
                  : 1.0;
              float dynamicDepth = dynamicInside
                  ? texture2D(u_dynamicShadowMap,
                      dynamicProjected.xy + offset * u_dynamicShadowTexelSize).r
                  : 1.0;
              bool staticOccluded = staticInside && staticProjected.z - bias > staticDepth;
              bool dynamicOccluded = dynamicInside && dynamicProjected.z - bias > dynamicDepth;
              visibility += (staticOccluded || dynamicOccluded) ? 0.58 : 1.0;
            }
          shadow = visibility / 9.0;
        }
      }
      float skyExposure = 0.88 + 0.12 * max(normal.z, 0.0);
      vec3 neutralAmbient = mix(vec3(1.0), u_ambientColor, 0.28);
      float ambientLevel = 0.32 + 0.68 * u_ambientStrength;
      vec3 light = neutralAmbient * (ambientLevel * skyExposure)
                 + u_sunColor * (u_sunStrength * sunDiffuse * shadow);
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
               * max(dot(normal, normalize(offset)), 0.0);
      }
      resColor.rgb *= light;
      if (u_fogEnabled)
      {
        float fog = max(clamp((v_viewDepth - u_fogStart) /
                              max(1.0, u_fogEnd - u_fogStart), 0.0, 1.0),
                        borderFog(v_worldPosition));
        resColor.rgb = mix(resColor.rgb, u_fogColor, fog);
      }
      }
    }

    // Highlithing
    if (u_highlight == true)
    {
      float highlightIntensity = 0.6f;
      vec4 highlightColor = u_highlightColor;
      highlightColor *= highlightIntensity;
      resColor += highlightColor;
    }

    gl_FragColor = resColor;
}
