#version 120

// Input uniforms
uniform vec2 shipPosition;
uniform float glowRadius;
uniform vec2 windowSize;
uniform vec2 gridResolution;
uniform sampler2D occupancyTex;
uniform float radialPulseStart;
uniform float radialPulseWidth;

uniform vec4 glowColor;

uniform vec2 centreForLastPulse;

void main() 
{
    vec2 shipPos = shipPosition * vec2(1,-1) + vec2(0,windowSize.y);
    // Get the current pixel coordinate
    vec2 pixelPosition = gl_FragCoord.xy;

    // Line tracing
    vec2 delta = pixelPosition - shipPos;
    float distance = length(delta);
    float pulseDist = length(pixelPosition - (centreForLastPulse* vec2(1,-1) + vec2(0,windowSize.y)));
    bool isVisible = true;
    bool isVisiblePulse = true;
    if(distance < glowRadius)
    {
        int steps = int(max(abs(delta.x), abs(delta.y)));
        vec2 increment = delta / float(steps);

        vec2 tracePos = shipPos;
        int reentry = 15;
        float firstColorHit = -1.0;
        float secondColorHit = -1.0;
        bool inside = false;

        for (int i = 0; i <= steps+2; ++i) 
        {
            float occupancy = texture2D(occupancyTex, tracePos / gridResolution).r;

            if (!inside)
            {
                if (occupancy > 0)
                {
                    if (firstColorHit == -1.0) 
                    {
                        firstColorHit = occupancy;
                    }
                    else if (secondColorHit == -1.0)
                    {
                        if (firstColorHit != occupancy)
                        {
                            secondColorHit = occupancy;
                        }
                    }
                    else
                    {
                        if (occupancy != firstColorHit && occupancy != secondColorHit)
                        {
                            isVisible = false;
                            break;
                        }
                    }
                    inside = true;
                }
                else
                {
                    if (firstColorHit != -1.0 || secondColorHit != -1.0)
                    {
                        --reentry;
                        if (reentry == 0)
                        {
                            isVisible = false;
                            break;
                        }
                    }
                }
            }
            else
            {
                if (occupancy == 0)
                {
                    inside = false;
                }
                else if (occupancy != firstColorHit)
                {
                    if(secondColorHit == -1.0)
                    {
                        secondColorHit = occupancy;
                    }
                    else if(occupancy != secondColorHit)
                    {
                        isVisible = false;
                        break;
                    }
                }
            }


            tracePos += increment;
        }
        if(reentry != 15 && !inside) isVisible = false;

    }

    if(pulseDist > radialPulseStart && pulseDist < radialPulseStart + radialPulseWidth)
    {
        vec2 delta2 = pixelPosition - (centreForLastPulse* vec2(1,-1) + vec2(0,windowSize.y));
        int steps = int(max(abs(delta2.x), abs(delta2.y))) / 4;
        vec2 increment = delta2 / float(steps);
        vec2 tracePos = (centreForLastPulse* vec2(1,-1) + vec2(0,windowSize.y));
        int reentry = 2;
        float firstColorHit = -1.0;
        bool inside = false;
        tracePos += increment * 20;
        for (int i = 20; i <= steps+2; ++i) 
        {
            float occupancy = texture2D(occupancyTex, tracePos / gridResolution).r;
    
            if(length(tracePos - shipPos) < radialPulseStart)
            {
                if(occupancy > 0)
                {
                    isVisiblePulse = false;
                    break;
                }
            }
            else
            {
                if(!inside)
                {
                    if(occupancy > 0)
                    {
                        if(firstColorHit == -1.0) 
                        {
                            firstColorHit = occupancy; 
                        }
                        else
                        {
                            if(firstColorHit != occupancy) 
                            {
                                isVisiblePulse = false;
                                break;
                            }
                        }
                        inside = true;
                    }
                    else
                    {
                        if(firstColorHit != -1.0)
                        {
                            --reentry;
                            if(reentry == 0)
                            {
                                isVisiblePulse = false;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    if(occupancy == 0)
                    {
                        inside = false;
                    }
                    else if(firstColorHit != occupancy)
                    {
                        isVisiblePulse = false;
                        break;
                    }
                }
            }
    
            tracePos += increment;
        }
    }


    float attenuation = clamp((isVisible ? 1.0 : 0.0) * (1.0 - distance / glowRadius), 0.0, 1.0);
    
    float pulseBrightness = clamp((isVisiblePulse ? 1.0 : 0.0) * (radialPulseWidth/2 - abs(pulseDist - radialPulseStart - radialPulseWidth/2))/radialPulseWidth, 0.0, 1.0);

    gl_FragColor = vec4(glowColor.rgb, clamp(pulseBrightness + attenuation*attenuation, 0.0, 1.0));
}
