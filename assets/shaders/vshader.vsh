uniform mat4 u_viewMmatrix;
uniform mat4 u_projMmatrix;
uniform mat4 u_modelMmatrix;
uniform mat4 u_partMmatrix;
uniform bool u_partAnimated;
uniform bool u_instanced;
uniform float u_textureUvScale;
uniform float u_shadowDepthBias;
attribute vec4 a_position;
attribute vec3 a_normal;
attribute vec2 a_texture;
attribute vec4 a_instanceMatrix0;
attribute vec4 a_instanceMatrix1;
attribute vec4 a_instanceMatrix2;
attribute vec4 a_instanceMatrix3;
attribute vec4 a_instanceColor;
varying highp vec4 v_position;
varying highp vec3 v_normal;
varying highp vec2 v_texture;
varying highp vec4 v_instanceColor;
varying highp vec3 v_worldPosition;
varying highp float v_viewDepth;

void main(void)
{
    //convert point via model-view matrix
    //mat4 mvpMatrix = u_modelMmatrix * u_projMmatrix;
  mat4 partMatrix = u_partAnimated ? u_partMmatrix : mat4(1.0);
  mat4 instanceMatrix = mat4(a_instanceMatrix0, a_instanceMatrix1,
                             a_instanceMatrix2, a_instanceMatrix3);
  mat4 modelMatrix = u_instanced ? instanceMatrix : u_modelMmatrix;
  mat4 worldMatrix = modelMatrix * partMatrix;
  mat4 mvMatrix = u_viewMmatrix * worldMatrix;
  gl_Position = u_projMmatrix * mvMatrix * a_position;
  // The shadow pass sets this only for terrain and resets it before drawing
  // object casters. Clip-space bias is deterministic across desktop drivers;
  // ordinary color/selection programs retain the linked default of zero.
  gl_Position.z += u_shadowDepthBias * gl_Position.w;

  v_position = mvMatrix * a_position;
  vec4 worldPosition = worldMatrix * a_position;
  v_worldPosition = worldPosition.xyz;
  v_viewDepth = max(0.0, -v_position.z);
  v_normal = normalize(vec3(worldMatrix * vec4(a_normal, 0.0)));
  v_texture = a_texture * (u_partAnimated ? u_textureUvScale : 1.0);
  v_instanceColor = a_instanceColor;
}
