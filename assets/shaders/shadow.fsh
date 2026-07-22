uniform sampler2D qt_Texture0;
uniform bool u_shadowAlphaTest;
varying highp vec2 v_texture;

vec4 packDepth(const float depth)
{
  const vec4 shifts = vec4(16777216.0, 65536.0, 256.0, 1.0);
  const vec4 masks = vec4(0.0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0);
  vec4 packed = fract(min(depth, 0.99999994) * shifts);
  packed -= packed.xxyz * masks;
  return packed;
}

void main(void)
{
  if (u_shadowAlphaTest && texture2D(qt_Texture0, v_texture).a < 0.5)
    discard;
  gl_FragColor = packDepth(gl_FragCoord.z);
}
