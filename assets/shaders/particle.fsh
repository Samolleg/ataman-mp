#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D qt_Texture0;
uniform vec4 u_tint;
uniform bool u_useTexture;
uniform bool u_radialMask;
uniform float u_softness;
varying highp vec2 v_texture;

void main(void)
{
  vec2 centered = fract(v_texture) * 2.0 - 1.0;
  float radial = smoothstep(1.0, max(0.05, u_softness), length(centered));
  vec4 source = u_useTexture ? texture2D(qt_Texture0, v_texture)
                             : vec4(1.0, 1.0, 1.0, radial);
  vec4 result = source * u_tint;
  if(u_radialMask)
    result.a *= radial;
  if(result.a < 0.035)
    discard;
  gl_FragColor = result;
}
