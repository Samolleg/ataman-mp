uniform mat4 u_viewMmatrix;
uniform mat4 u_projMmatrix;
uniform vec3 u_center;
uniform vec3 u_cameraRight;
uniform vec3 u_cameraUp;
uniform float u_scale;
uniform float u_rotation;
uniform vec4 u_uvRect;
attribute vec2 a_position;
attribute vec2 a_texture;
varying highp vec2 v_texture;

void main(void)
{
  float c = cos(u_rotation);
  float s = sin(u_rotation);
  vec2 local = vec2(c * a_position.x - s * a_position.y,
                    s * a_position.x + c * a_position.y) * u_scale;
  vec3 world = u_center + u_cameraRight * local.x + u_cameraUp * local.y;
  gl_Position = u_projMmatrix * u_viewMmatrix * vec4(world, 1.0);
  v_texture = u_uvRect.xy + a_texture * u_uvRect.zw;
}
