#version 120
#extension GL_EXT_gpu_shader4 : enable
uniform sampler2D texture;
uniform vec2 texSize;

void main()
{
    vec2 texCoord = gl_TexCoord[0].st;
    float alpha = 1.0;
    vec4 color = vec4(0.0);
    vec2 texOffset = 0.5 / texSize;

    float kernel[81];
    float kernelSum = 0.0;

    for (int i = 0; i < 81; i++) 
    {
        int x = (i / 9) - 4;
        int y = int(floor(float(i % 9) - 4.0));
        float frac = (4-abs(x))/4.0 * (4-abs(y))/4.0;
        kernel[i] = frac;
        kernelSum += frac;
    }

    for (int i = -4; i <= 4; i++) 
    {
        for (int j = -4; j <= 4; j++) 
        {
            vec2 offset = vec2(float(i), float(j)) * texOffset;
            int kernelIndex = (i + 4) * 9 + (j + 4);
            color += texture2D(texture, texCoord + offset) * kernel[kernelIndex];
        }
    }

    color /= kernelSum;

    gl_FragColor = color;
}