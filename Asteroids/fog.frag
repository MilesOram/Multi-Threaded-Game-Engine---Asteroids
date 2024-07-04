#version 120

uniform sampler2D texture;
uniform sampler2D alphaMap;
uniform vec2 windowSize;

void main() 
{
    vec4 texColor = texture2D(texture, gl_TexCoord[0].st);

    gl_FragColor = texColor;
    gl_FragColor *= texture2D(alphaMap, gl_FragCoord.xy / windowSize);
}

