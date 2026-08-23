uniform mat4 u_viewMmatrix;
uniform mat4 u_projMmatrix;
uniform mat4 u_modelMmatrix;
uniform bool u_overlayMotionEnabled;
uniform float u_overlayTimeSeconds;
uniform vec3 u_overlayWindDirection;
uniform float u_overlayWindStrength;
uniform vec2 u_overlayAtlasGrid;
attribute vec4 a_position;
attribute vec3 a_normal;
attribute vec2 a_texture;
attribute vec4 a_overlayAnimation;
attribute vec4 a_overlayTileAnimation;
attribute vec2 a_overlayGrid;
attribute vec3 a_materialEmission;
attribute float a_materialOpacity;
varying highp vec4 v_position;
varying highp vec3 v_normal;
varying highp vec2 v_texture;
varying highp vec3 v_worldPosition;
varying highp float v_viewDepth;
varying highp vec3 v_materialEmission;
varying highp float v_materialOpacity;

const float PI = 3.14159265358979323846;

vec2 overlayTileOrigin(float slot)
{
  float tileSide = max(1.0, u_overlayAtlasGrid.y);
  float slotsPerPage = tileSide * tileSide;
  float page = floor(slot / slotsPerPage);
  float localSlot = slot - page * slotsPerPage;
  float column = mod(localSlot, tileSide);
  float row = floor(localSlot / tileSide);
  return vec2((page + column / tileSide) / max(1.0, u_overlayAtlasGrid.x),
              (tileSide - 1.0 - row) / tileSide);
}

float tableSine(float angle)
{
  float tablePosition = angle * (512.0 / (2.0 * PI));
  float tableIndex = sign(tablePosition) * floor(abs(tablePosition) + 0.5);
  return sin(mod(tableIndex, 512.0) * (2.0 * PI / 512.0));
}

void main(void)
{
  vec4 baseWorld = u_modelMmatrix * a_position;
  vec4 worldPosition = baseWorld;
  float strength = clamp(u_overlayWindStrength, 0.0, 1.0);
  if (u_overlayMotionEnabled && a_overlayAnimation.w > 0.5
      && a_overlayAnimation.x != 0.0 && strength > 0.0)
  {
    vec3 direction = length(u_overlayWindDirection) > 0.0
        ? normalize(u_overlayWindDirection) : vec3(0.0);
    float waveLength = (PI / 7.0) * (asin(1.0 - strength) + PI / 1.999);
    vec2 waveDirection = direction.xy * waveLength;
    float logicalTime = max(0.0, u_overlayTimeSeconds) / 0.055;
    float windPhase = 2.0 * PI + floor(logicalTime) * strength * (-PI / 21.0);
    float amplitude = strength * 0.3 * a_overlayAnimation.x;
    float waveAngle = dot(baseWorld.xy, waveDirection)
        + windPhase * a_overlayAnimation.x + a_overlayAnimation.z;
    float warpPhase = logicalTime * 0.05;
    worldPosition.x += tableSine(a_overlayGrid.x * PI * 0.25 + warpPhase)
        * amplitude * 3.0;
    worldPosition.y += tableSine(a_overlayGrid.y * PI * 0.25 + warpPhase)
        * amplitude * 3.0;
    worldPosition.z += tableSine(waveAngle) * amplitude * 0.25;
  }

  vec4 viewPosition = u_viewMmatrix * worldPosition;
  gl_Position = u_projMmatrix * viewPosition;
  v_position = viewPosition;
  v_worldPosition = worldPosition.xyz;
  v_viewDepth = max(0.0, -viewPosition.z);
  v_normal = normalize(vec3(u_modelMmatrix * vec4(a_normal, 0.0)));
  v_texture = a_texture;
  if(u_overlayMotionEnabled && a_overlayTileAnimation.z > 1.0)
  {
    float logicalFrame = floor(max(0.0, u_overlayTimeSeconds) / 0.055);
    float frameSlot = a_overlayTileAnimation.x
        + mod(a_overlayTileAnimation.w + logicalFrame,
              a_overlayTileAnimation.z);
    v_texture += overlayTileOrigin(frameSlot)
        - overlayTileOrigin(a_overlayTileAnimation.y);
  }
  v_materialEmission = a_materialEmission;
  v_materialOpacity = a_materialOpacity;
}
