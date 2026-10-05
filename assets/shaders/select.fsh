#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform highp vec4 u_color;
uniform bool u_instanced;
uniform sampler2D qt_Texture0;
varying highp vec2 v_texture;
varying highp vec4 v_instanceColor;

void main(void)
{
    // Match the visible pass, including foliage cutouts and alpha textures.
    if (texture2D(qt_Texture0, v_texture).a <= 0.1)
        discard;
    gl_FragColor = u_instanced ? v_instanceColor : u_color;
}
