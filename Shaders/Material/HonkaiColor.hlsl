// Face and body use the same paint and ramp treatment; only their shadow masks differ.
struct HonkaiColor
{
    float3 Shade(float3 base, float3 shadowColor, float3 rampColor, float band, float rim)
    {
        float sourceSaturation = 1 - min(base.r, min(base.g, base.b)) / max(max(base.r, max(base.g, base.b)), 0.001);
        // Keep a distinct ink-like shade while retaining the hue of the authored ramp.
        float3 originalShadow = shadowColor;
        float shadowLuma = dot(shadowColor, float3(0.2126, 0.7152, 0.0722));
        shadowColor = saturate(lerp(shadowLuma.xxx, shadowColor, 1.2)) * 0.88;
        float3 lighting = max(0, rampColor + (shadowColor - originalShadow) * (1 - band));
        float warmPaint = 1 - smoothstep(-0.005, 0.015, base.b - base.r);
        float coolRamp = smoothstep(0.02, 0.08, shadowColor.b - shadowColor.r);
        // Near-neutral paint must not amplify tiny channel differences into colored shadow stripes.
        float shadowWarmPaint = (1 - smoothstep(-0.12, 0.12, base.b - base.r))
            * smoothstep(0.06, 0.16, sourceSaturation);
        lighting *= lerp(float3(1, 1, 1), float3(1.08, 0.68, 0.68), shadowWarmPaint * coolRamp * (1 - band));
        float3 glow = base * smoothstep(0.82, 0.94, min(base.r, min(base.g, base.b))) * band * 0.3;
        float pale = smoothstep(0.78, 0.85, max(base.r, max(base.g, base.b))) * (1 - smoothstep(0.08, 0.2, sourceSaturation));
        base = lerp(base, float3(1.0, 0.90, 0.865), pale * band * 0.8);
        float saturation = 1 - min(base.r, min(base.g, base.b)) / max(max(base.r, max(base.g, base.b)), 0.001);
        float warmMidtone = warmPaint * smoothstep(0.18, 0.4, saturation) * (1 - smoothstep(0.65, 0.85, saturation));
        base *= lerp(float3(1, 1, 1), float3(1.08, 1.0, 0.88), warmMidtone);
        base *= lerp(float3(1, 1, 1), float3(0.99, 0.99, 1.04), (1 - warmPaint) * band);
        float vivid = smoothstep(0.75, 0.95, saturation);
        base = lerp(base, dot(base, float3(0.2126, 0.7152, 0.0722)).xxx, vivid * 0.06);
        return base * (lighting + rim * 0.12) + glow;
    }

    float3 Highlight(float3 base, float3 specular, float power, float3 normal, float3 light, float3 view, float lit)
    {
        float3 halfVector = light + view;
        halfVector *= rsqrt(max(dot(halfVector, halfVector), 0.0001));
        float lobe = pow(saturate(dot(normal, halfVector)), clamp(power * 0.5, 1.0, 512.0));
        float broad = smoothstep(0.35, 0.7, lobe);
        float narrow = smoothstep(0.55, 0.85, lobe * lobe * lobe * lobe);
        float3 tint = base / max(max(base.r, max(base.g, base.b)), 0.001);
        float peak = max(specular.r, max(specular.g, specular.b));
        float3 color = specular * lerp(1.0.xxx, tint, 0.35) * broad * 0.22
            + lerp(specular, peak.xxx, 0.5) * narrow * 0.4;
        return color * saturate(lit) * smoothstep(0.0, 0.15, dot(normal, light));
    }
};
