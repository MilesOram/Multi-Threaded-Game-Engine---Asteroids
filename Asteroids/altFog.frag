#version 120

uniform sampler2D texture;
uniform vec2 center;
uniform vec2 windowSize;
uniform float maxDist;

void main() 
{
    float dist = length(gl_FragCoord.xy * vec2(1,-1) + vec2(0,windowSize.y) - center);
    float alpha = 1 - dist/maxDist;
    vec4 texColor = texture2D(texture, gl_TexCoord[0].st);

    gl_FragColor = vec4(texColor.rgb, texColor.a * alpha);
}