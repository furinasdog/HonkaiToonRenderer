// R/G hold the light-angle thresholds for opposite sides, in linear space.
// shortcut: the static PMX bind pose uses mesh-local axes; animated heads need bone axes.
float2 horizontal = normalize(Light.xy + float2(0, 0.00001));
float angle = acos(clamp(horizontal.y, -1.0, 1.0)) / 3.14159265;
// The threshold texture uses normalized light angles; do not expand oblique angles.
float threshold = horizontal.x >= 0 ? Map.r : Map.g;
float width = max(fwidth(threshold), 0.0001);
float lit = smoothstep(angle - width, angle + width, threshold);
float row = (round(RampRow * 255.0) + 0.5) / 256.0;
float3 shadowColor = Texture2DSampleLevel(ToonAtlas, ToonAtlasSampler, float2(0.5 / 256.0, row), 0).rgb;
float3 rampColor = Texture2DSampleLevel(ToonAtlas, ToonAtlasSampler, float2((lit * 255.0 + 0.5) / 256.0, row), 0).rgb;
// The separate nose region uses a hard boundary; cheek/lip normals stay unused.
float noseWidth = max(fwidth(Map.a) * 0.5, 0.0001);
float nose = (1 - smoothstep(0.5 - noseWidth, 0.5 + noseWidth, Map.a)) * lit;
// The authored small nose region remains visible from the front; lighting controls its strength.
nose *= saturate(0.65 + abs(horizontal.x));
HonkaiColor color;
return color.Shade(Base, shadowColor, rampColor, lit, 0) * lerp(float3(1, 1, 1), float3(0.94, 0.72, 0.65), nose)
    + color.Highlight(Base, SpecularColor, Shininess, normalize(WorldNormal), normalize(WorldLight), normalize(ViewDirection), lit);
