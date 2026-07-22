uniform sampler2D qt_Texture0;
uniform vec3 u_tint;
uniform vec3 u_fogColor;
varying highp vec2 v_texture;
varying highp float v_fogBlend;

void main(void)
{
  vec4 color = texture2D(qt_Texture0, v_texture);
  gl_FragColor = vec4(mix(color.rgb * u_tint, u_fogColor,
                          clamp(v_fogBlend, 0.0, 1.0)), 1.0);
}
