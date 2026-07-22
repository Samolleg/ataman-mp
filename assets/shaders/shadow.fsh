#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D qt_Texture0;
uniform bool u_shadowAlphaTest;
varying highp vec2 v_texture;

vec4 packDepth(const float depth)
{
  // The target is RGBA8, so the fractional base-256 encoder is not safe:
  // UNORM rounding can carry a channel to 255 and move reconstructed depth by
  // almost 1/256.  That is over a world unit in the current shadow volume and
  // visibly detaches contact shadows.  Base 255 survives the RGBA8 round trip.
  const vec4 shifts = vec4(1.0, 255.0, 65025.0, 16581375.0);
  const vec4 masks = vec4(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
  vec4 packed = fract(min(depth, 0.99999994) * shifts);
  packed -= packed.yzww * masks;
  return packed;
}

void main(void)
{
  if (u_shadowAlphaTest && texture2D(qt_Texture0, v_texture).a < 0.5)
    discard;
  gl_FragColor = packDepth(gl_FragCoord.z);
}
