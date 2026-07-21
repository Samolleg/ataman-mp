uniform mat4 u_viewMmatrix;
uniform mat4 u_projMmatrix;
attribute vec3 a_position;
attribute vec2 a_texture;
varying highp vec2 v_texture;

void main(void)
{
  vec4 position = u_projMmatrix * u_viewMmatrix * vec4(a_position, 1.0);
  gl_Position = position.xyww;
  v_texture = a_texture;
}
