uniform mat4 u_viewMmatrix;
uniform mat4 u_projMmatrix;
attribute vec3 a_worldPosition;
attribute vec2 a_texture;
attribute vec2 a_localTexture;
attribute vec4 a_tint;
varying highp vec2 v_texture;
varying highp vec2 v_localTexture;
varying lowp vec4 v_tint;

void main(void)
{
  gl_Position = u_projMmatrix * u_viewMmatrix * vec4(a_worldPosition, 1.0);
  v_texture = a_texture;
  v_localTexture = a_localTexture;
  v_tint = a_tint;
}
