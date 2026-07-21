uniform sampler2D qt_Texture0;
uniform vec3 u_tint;
varying highp vec2 v_texture;

void main(void)
{
  vec4 color = texture2D(qt_Texture0, v_texture);
  gl_FragColor = vec4(color.rgb * u_tint, 1.0);
}
