uniform sampler2D qt_Texture0;
uniform vec3 u_tint;
uniform vec3 u_fogColor;
uniform bool u_fogEnabled;
varying highp vec2 v_texture;
varying highp float v_skyHeight;

void main(void)
{
  vec4 color = texture2D(qt_Texture0, v_texture);
  vec3 result = color.rgb * u_tint;
  if (u_fogEnabled)
  {
    float skyVisibility = smoothstep(0.0, 13.91404, v_skyHeight);
    result = mix(u_fogColor, result, skyVisibility);
  }
  gl_FragColor = vec4(result, 1.0);
}
