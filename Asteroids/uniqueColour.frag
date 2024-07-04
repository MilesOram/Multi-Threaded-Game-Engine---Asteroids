#version 120

uniform sampler2D texture;
uniform float uniqueColor;

void main() 
{
    vec4 texColor = texture2D(texture, gl_TexCoord[0].st);
    if(texColor.a > 0.01) 
    {
        texColor = gl_Color;
    }
    gl_FragColor = texColor;
}
