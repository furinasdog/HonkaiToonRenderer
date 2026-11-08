// Quantize illumination, not the painted texture; only visible character pixels participate.
float visible = step(abs(Stencil.r - 1.0), 0.1) * step(abs(Depth.r - CustomDepth.r), 0.1);
float3 base = Diffuse.rgb;
float baseLuma = dot(base, float3(0.2126, 0.7152, 0.0722));
if (visible < 0.5) return Scene.rgb;
float2 uv = GetDefaultSceneTextureUV(Parameters, 1);
float row = (round(MaterialAO.r * 255.0) + 0.5) / 256.0;
if (baseLuma < 0.0001)
{
    if (MaterialAO.r < 0.5 / 255.0) return Scene.rgb;
    // Face color is emissive. Only a nearer non-face surface may cast this short contact shadow.
    // shortcut: screen-space contact misses occluders outside the viewport; retain the face threshold there.
    float2 towardLight = mul(normalize(LightDirection), (float3x3)ResolvedView.TranslatedWorldToView).xy * float2(1, -1);
    float2 reach = towardLight * View.ViewSizeAndInvSize.zw * clamp(1200.0 / max(Depth.r, 1.0), 2.0, 12.0);
    float contact = 0;
    [unroll] for (int i = 1; i <= 3; ++i)
    {
        float2 sampleUV = uv + reach * (i / 3.0);
        float delta = Depth.r - SceneTextureLookup(sampleUV, 1, false).r;
        float3 occluder = SceneTextureLookup(sampleUV, 2, false).rgb;
        contact = max(contact, step(0.3, delta) * step(delta, 6.0) * step(0.001, dot(occluder, 1.0 / 3.0)));
    }
    float3 shadow = Texture2DSampleLevel(ToonAtlas, ToonAtlasSampler, float2(0.5 / 256.0, row), 0).rgb;
    return Scene.rgb * lerp(float3(1, 1, 1), shadow, contact * 0.65);
}
// shortcut: this diffuse estimate assumes the viewer's neutral light and nonmetallic material.
float3 linearScene = Scene.rgb * View.OneOverPreExposure;
float illumination = dot(linearScene, float3(0.2126, 0.7152, 0.0722)) / baseLuma;
float band = smoothstep(0.08, 0.95, illumination);
// Material AO is reserved for the ramp index in the viewer's direct-light-only pipeline.
float3 shadowColor = Texture2DSampleLevel(ToonAtlas, ToonAtlasSampler, float2(0.5 / 256.0, row), 0).rgb;
float3 rampColor = Texture2DSampleLevel(ToonAtlas, ToonAtlasSampler, float2((band * 255.0 + 0.5) / 256.0, row), 0).rgb;
// A narrow depth edge follows the silhouette instead of brightening painted texture boundaries.
float2 pixel = View.ViewSizeAndInvSize.zw * 1.5;
float adjacentDepth = max(max(SceneTextureLookup(uv + float2(pixel.x, 0), 1, false).r,
                             SceneTextureLookup(uv - float2(pixel.x, 0), 1, false).r),
                         max(SceneTextureLookup(uv + float2(0, pixel.y), 1, false).r,
                             SceneTextureLookup(uv - float2(0, pixel.y), 1, false).r));
float rim = step(max(0.5, Depth.r * 0.003), adjacentDepth - Depth.r);
rim *= saturate(dot(normalize(Normal.xyz), normalize(LightDirection)));
HonkaiColor color;
float4 specular = Texture2DSampleLevel(SpecularAtlas, SpecularAtlasSampler, float2(0.5, row), 0);
float3 highlight = color.Highlight(base, specular.rgb, specular.a, normalize(Normal.xyz), normalize(LightDirection), normalize(ViewDirection), band);
return (color.Shade(base, shadowColor, rampColor, band, rim) + highlight) * View.PreExposure;
