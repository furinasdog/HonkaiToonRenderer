// R/G hold the light-angle thresholds for opposite sides, in linear space.
// shortcut: the static PMX bind pose uses mesh-local axes; animated heads need bone axes.
float2 horizontal = normalize(Light.xy + float2(0, 0.00001));
float angle = acos(clamp(horizontal.y, -1.0, 1.0)) / 3.14159265;
// Expand oblique light angles for a readable cheek shadow while keeping frontal light symmetric.
angle = pow(angle, 0.5);
float threshold = horizontal.x >= 0 ? Map.r : Map.g;
float width = max(fwidth(threshold), 0.0001);
float lit = smoothstep(angle - width, angle + width, threshold);
float3 lighting = lerp(float3(0.78, 0.61, 0.64), float3(1, 1, 1), lit);
if (ToonRampEnabled > 0.5)
{
    float3 shade = Texture2DSampleLevel(ToonRamp, ToonRampSampler, float2(0.5, 0.99), 0).rgb * float3(1.0, 0.97, 0.90);
    float3 light = Texture2DSampleLevel(ToonRamp, ToonRampSampler, float2(0.5, 0.01), 0).rgb;
    lighting = lerp(shade, light, lit);
}
// Apply a warm highlight shoulder only to pale skin paint, retaining shadow and dark markings.
float pale = smoothstep(0.45, 0.8, min(Base.r, min(Base.g, Base.b)));
float3 highlight = (Base + (1 - Base) * float3(0.8, 0.7, 0.55)) * float3(1.0, 0.99, 0.965);
Base = lerp(Base, highlight, pale * lit);
// The separate nose region uses a hard boundary; cheek/lip normals stay unused.
float noseWidth = max(fwidth(Map.a) * 0.5, 0.0001);
float nose = (1 - smoothstep(0.5 - noseWidth, 0.5 + noseWidth, Map.a)) * lit;
// The authored small nose region remains visible from the front; lighting controls its strength.
nose *= saturate(0.65 + abs(horizontal.x));
return Base * lighting * lerp(float3(1, 1, 1), float3(0.94, 0.72, 0.65), nose);
