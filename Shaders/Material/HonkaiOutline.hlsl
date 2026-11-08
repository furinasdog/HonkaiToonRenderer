float3 viewNormal = mul(Normal, (float3x3)ResolvedView.TranslatedWorldToView);
float3 viewPosition = mul(Position - Camera, (float3x3)ResolvedView.TranslatedWorldToView);
// Preserve authored world width at 3 m; perspective distance compensation is bounded.
float perspective = abs(ResolvedView.ViewToClip[3][3]) < 0.5 ? 1.0 : 0.0;
float distanceScale = lerp(1.0, sqrt(clamp(abs(viewPosition.z) / 300.0, 0.25, 16.0)), perspective);
float2 direction = viewNormal.xy * rsqrt(dot(viewNormal.xy, viewNormal.xy) + 0.0001);
return mul(float3(direction * Width * distanceScale, 0), (float3x3)ResolvedView.ViewToTranslatedWorld);
