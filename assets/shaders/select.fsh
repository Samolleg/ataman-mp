uniform highp vec4 u_color;
uniform bool u_instanced;
varying highp vec4 v_instanceColor;

void main(void)
{
    gl_FragColor = u_instanced ? v_instanceColor : u_color;
}
