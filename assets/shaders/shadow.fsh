uniform sampler2D qt_Texture0;
uniform bool u_shadowAlphaTest;
varying highp vec2 v_texture;

void main(void)
{
  if (u_shadowAlphaTest && texture2D(qt_Texture0, v_texture).a < 0.5)
    discard;
  gl_FragColor = vec4(gl_FragCoord.z, gl_FragCoord.z, gl_FragCoord.z, 1.0);
}
