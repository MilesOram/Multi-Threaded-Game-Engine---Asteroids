#version 120

// Input uniforms
uniform vec2 shipPosition;
uniform float glowRadius;
uniform vec2 windowSize;
uniform vec2 gridResolution;
uniform sampler2D occupancyTex;
uniform vec4 glowColor;

void main() 
{
    // Get the current pixel coordinate
    vec2 pixelPosition = gl_FragCoord.xy * vec2(1,-1) + vec2(0,windowSize.y);

    // Line tracing
    vec2 delta = pixelPosition - shipPosition;
    int steps = int(max(abs(delta.x), abs(delta.y)));
    vec2 increment = delta / float(steps);

    bool isVisible = true;
    vec2 tracePos = shipPosition;

    for (int i = 0; i <= steps; ++i) 
    {
        float occupancy = texture2D(occupancyTex, tracePos / gridResolution).r;

        if (occupancy > .1) 
        {
            isVisible = false;
            break;
        }

        tracePos += increment;
    }

    // Set the output visibility value
    float visibility = isVisible ? 1.0 : 0.0;

    float distance = length(pixelPosition - shipPosition);
    float attenuation = clamp(1.0 - distance / glowRadius, 0.0, 1.0);
    gl_FragColor = vec4(glowColor.rgb, attenuation *attenuation* visibility);
}

