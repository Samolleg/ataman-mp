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
uniform bool u_fogEnabled;
uniform float u_ambientStrength;
uniform float u_sunStrength;
uniform vec3 u_sunDirection;
uniform vec3 u_sunColor;
uniform vec3 u_fogColor;
uniform float u_fogStart;
uniform float u_fogEnd;
uniform int u_pointLightCount;
uniform vec4 u_pointLightPositionRange[4];
uniform vec3 u_pointLightColor[4];
uniform bool u_shadowsEnabled;
uniform sampler2D u_staticShadowMap;
uniform sampler2D u_dynamicShadowMap;
uniform mat4 u_lightSpaceMatrix;

varying highp vec4 v_position;
varying highp vec3 v_normal;
varying highp vec2 v_texture;
varying highp vec3 v_worldPosition;
varying highp float v_viewDepth;

void main(void)
{
  vec3 norm = normalize(v_normal);
  vec4 texColor = texture2D(qt_Texture0, v_texture);
  vec3 light = vec3(1.0);
  if (u_environmentEnabled)
  {
    float sunDiffuse = max(dot(norm, -normalize(u_sunDirection)), 0.0);
    float shadow = 1.0;
    if (u_shadowsEnabled && sunDiffuse > 0.0)
    {
      vec4 lightPosition = u_lightSpaceMatrix * vec4(v_worldPosition, 1.0);
      vec3 projected = lightPosition.xyz / lightPosition.w * 0.5 + 0.5;
      if (projected.x >= 0.0 && projected.x <= 1.0 &&
          projected.y >= 0.0 && projected.y <= 1.0 && projected.z <= 1.0)
      {
        float storedDepth = min(texture2D(u_staticShadowMap, projected.xy).r,
                                texture2D(u_dynamicShadowMap, projected.xy).r);
        float bias = max(0.0015 * (1.0 - sunDiffuse), 0.00035);
        shadow = projected.z - bias > storedDepth ? 0.42 : 1.0;
      }
    }
    light = vec3(u_ambientStrength)
          + u_sunColor * (u_sunStrength * sunDiffuse * shadow);
    for (int i = 0; i < 4; ++i)
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
  vec4 resColor = vec4(light, 1.0) * texColor;
  if (u_environmentEnabled && u_fogEnabled)
  {
    float fog = clamp((v_viewDepth - u_fogStart) /
                      max(1.0, u_fogEnd - u_fogStart), 0.0, 1.0);
    resColor.rgb = mix(resColor.rgb, u_fogColor, fog);
  }

  resColor.a -= transparency;


  gl_FragColor = resColor;

}
