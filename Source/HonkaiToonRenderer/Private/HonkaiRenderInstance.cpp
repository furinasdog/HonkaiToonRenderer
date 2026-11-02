#include "HonkaiRenderInstance.h"

#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/Texture2D.h"
#include "MaterialValueType.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
    constexpr int32 AtlasSize = 256;
    enum EParent { Toon, ToonTwoSided, Translucent, TranslucentTwoSided, Outline, FaceOutline, Cel, ParentCount };
    const TCHAR* ParentNames[] = { TEXT("M_Toon"), TEXT("M_ToonTwoSided"), TEXT("M_ToonTranslucent"),
        TEXT("M_ToonTranslucentTwoSided"), TEXT("M_Outline"), TEXT("M_FaceOutline"), TEXT("M_CelLighting") };

    // One list keeps the preset and its selective restoration in agreement.
#define HONKAI_VIEW_FIELDS(X) \
    X(AutoExposureMethod, AEM_Manual) \
    X(AutoExposureBias, 0.f) \
    X(AutoExposureApplyPhysicalCameraExposure, false) \
    X(ToneCurveAmount, 0.f) \
    X(ExpandGamut, 0.f) \
    X(BlueCorrection, 0.f) \
    X(LocalExposureHighlightContrastScale, 1.f) \
    X(LocalExposureShadowContrastScale, 1.f) \
    X(BloomIntensity, 1.f) \
    X(BloomThreshold, .85f) \
    X(VignetteIntensity, 0.f) \
    X(AmbientOcclusionIntensity, 0.f) \
    X(DynamicGlobalIlluminationMethod, EDynamicGlobalIlluminationMethod::None) \
    X(ReflectionMethod, EReflectionMethod::None)

    void ApplyViewPreset(FPostProcessSettings& Settings)
    {
#define SET_PRESET(Name, Value) Settings.bOverride_##Name = true; Settings.Name = Value;
        HONKAI_VIEW_FIELDS(SET_PRESET)
#undef SET_PRESET
    }

    void RestoreViewPreset(FPostProcessSettings& Settings, const FPostProcessSettings& Saved)
    {
#define RESTORE_PRESET(Name, Value) Settings.bOverride_##Name = Saved.bOverride_##Name; Settings.Name = Saved.Name;
        HONKAI_VIEW_FIELDS(RESTORE_PRESET)
#undef RESTORE_PRESET
    }
#undef HONKAI_VIEW_FIELDS

    bool CopyToonRamp(UTexture2D* Texture, TArray<FColor>& Pixels, int32 Row)
    {
        auto* Data = Texture ? Texture->GetPlatformData() : nullptr;
        if (!Data || Data->PixelFormat != PF_B8G8R8A8 || Data->Mips.IsEmpty()
            || Row < 0 || Row >= AtlasSize || Pixels.Num() != AtlasSize * AtlasSize) return false;
        auto& Mip = Data->Mips[0];
        if (Mip.SizeX <= 0 || Mip.SizeY <= 0 || Mip.BulkData.GetBulkDataSize() < int64(Mip.SizeX) * Mip.SizeY * int64(sizeof(FColor))) return false;
        const FColor* Source = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
        if (!Source) { Mip.BulkData.Unlock(); return false; }
        // Authored vertical ramps run from light at the top to shadow at the bottom.
        for (int32 X = 0; X < AtlasSize; ++X)
        {
            const int32 Y = FMath::RoundToInt((255 - X) * (Mip.SizeY - 1) / 255.f);
            Pixels[Row * AtlasSize + X] = Source[Y * Mip.SizeX + Mip.SizeX / 2];
        }
        Mip.BulkData.Unlock();
        return true;
    }

    UTexture2D* BuildToonAtlas(const TArray<FHonkaiSurfaceInput>& Inputs, TMap<UTexture*, uint8>& Rows)
    {
        TArray<FColor> Pixels;
        Pixels.SetNumUninitialized(AtlasSize * AtlasSize);
        for (int32 Y = 0; Y < AtlasSize; ++Y)
            for (int32 X = 0; X < AtlasSize; ++X)
                Pixels[Y * AtlasSize + X] = FMath::Lerp(FLinearColor(.64f, .52f, .61f), FLinearColor::White, X / 255.f).ToFColorSRGB();
        for (const auto& Input : Inputs)
        {
            UTexture2D* Texture = Input.ToonRamp;
            if (!Texture || Rows.Contains(Texture) || Rows.Num() >= 255) continue;
            const uint8 Row = uint8(Rows.Num() + 1);
            if (CopyToonRamp(Texture, Pixels, Row)) Rows.Add(Texture, Row);
        }
        auto* Atlas = UTexture2D::CreateTransient(AtlasSize, AtlasSize, PF_B8G8R8A8);
        if (!Atlas) return nullptr;
        Atlas->SRGB = true;
        Atlas->Filter = TF_Bilinear;
        Atlas->AddressX = Atlas->AddressY = TA_Clamp;
        auto& Bulk = Atlas->GetPlatformData()->Mips[0].BulkData;
        void* Destination = Bulk.Lock(LOCK_READ_WRITE);
        if (!Destination) { Bulk.Unlock(); return nullptr; }
        FMemory::Memcpy(Destination, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
        Bulk.Unlock();
        return Atlas;
    }

    void RestoreMaterials(UMeshComponent* Mesh, const TArray<TObjectPtr<UMaterialInterface>>& Overrides)
    {
        // EmptyOverrideMaterials also clears per-slot overlays; those belong to the host.
        TArray<TStrongObjectPtr<UMaterialInterface>> Overlays;
        for (const auto& Material : Mesh->MaterialSlotsOverlayMaterial) Overlays.Emplace(Material.Get());
        Mesh->EmptyOverrideMaterials();
        for (int32 I = 0; I < Overrides.Num(); ++I) Mesh->SetMaterial(I, Overrides[I]);
        for (int32 I = 0; I < Overlays.Num(); ++I) Mesh->SetOverlayMaterial(Overlays[I].Get(), true, I);
    }

    bool ValidateTexture(UTexture* Texture, FString& Error)
    {
        if (!Texture) return true;
        if (!IsValid(Texture) || Texture->GetMaterialType() != MCT_Texture2D)
        { Error = TEXT("Renderer inputs require live, non-virtual 2D textures."); return false; }
        // Resident streaming textures need no CPU mip payload inspection or re-upload.
        if (Texture->GetResource()) return true;
        if (const auto* Texture2D = Cast<UTexture2D>(Texture))
        {
            const auto* Data = Texture2D->GetPlatformData();
            if (!Data || Data->SizeX <= 0 || Data->SizeY <= 0 || Data->Mips.IsEmpty()
                || Data->Mips[0].SizeX <= 0 || Data->Mips[0].SizeY <= 0
                || Data->PixelFormat == PF_Unknown || Data->PixelFormat >= PF_MAX)
            { Error = TEXT("A texture without a GPU resource must contain valid platform mip metadata."); return false; }
        }
        if (Texture->GetSurfaceWidth() <= 0 || Texture->GetSurfaceHeight() <= 0)
        { Error = TEXT("A texture without a GPU resource must have positive dimensions."); return false; }
        return true;
    }

    bool PrepareTexture(UTexture* Texture, FString& Error)
    {
        if (!Texture || Texture->GetResource()) return true;
        Texture->UpdateResource();
        if (FApp::CanEverRender() && !Texture->GetResource())
        { Error = TEXT("Unable to create a render resource for texture: ") + Texture->GetName(); return false; }
        return true;
    }

    bool UsesFaceMap(UMaterialInstanceDynamic* Material, UTexture2D* Map)
    {
        UTexture* BoundMap = nullptr;
        return Map && Material && Material->GetTextureParameterValue(TEXT("FaceMap"), BoundMap) && BoundMap == Map;
    }

    bool IsFiniteColor(FLinearColor Color)
    {
        return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
    }
}

bool UHonkaiRenderInstance::ValidateFaceMaps(const FHonkaiFaceMaps& Maps, FString& Error) const
{
    for (const auto& Entry : Maps)
    {
        const UTexture2D* Texture = Entry.Value;
        if (!Inputs.IsValidIndex(Entry.Key) || !IsValid(Texture) || Texture->SRGB
            || Texture->AddressX != TA_Clamp || Texture->AddressY != TA_Clamp
            || Texture->GetSizeX() <= 0 || Texture->GetSizeY() <= 0)
        {
            Error = TEXT("Face maps require a valid surface slot and a linear, clamped 2D texture.");
            return false;
        }
        if (!ValidateTexture(Entry.Value, Error)) return false;
    }
    return true;
}

void UHonkaiRenderInstance::ConfigureCamera(UCameraComponent* InCamera)
{
    if (IsInGameThread() && IsValid(InCamera)) ApplyViewPreset(InCamera->PostProcessSettings);
}

UMaterialInstanceDynamic* UHonkaiRenderInstance::CreateOutline(int32 Slot, UTexture2D* FaceMap, UObject* ResourceOwner)
{
    const auto& Input = Inputs[Slot];
    auto* Material = UMaterialInstanceDynamic::Create(Parents[FaceMap ? EParent::FaceOutline : EParent::Outline], ResourceOwner);
    if (!Material) return nullptr;
    Material->SetVectorParameterValue(TEXT("SurfaceTint"), Input.Tint);
    Material->SetVectorParameterValue(TEXT("Tint"), Input.OutlineColor);
    Material->SetScalarParameterValue(TEXT("OutlineRegion"), float(Input.OutlineRegion));
    Material->SetScalarParameterValue(TEXT("Opacity"), Input.bTranslucent ? 0.f : Input.OutlineOpacity);
    Material->SetScalarParameterValue(TEXT("OutlineWidth"), Input.OutlineWidth * (FaceMap ? .75f : 1.f));
    if (Input.BaseTexture) Material->SetTextureParameterValue(TEXT("BaseTexture"), Input.BaseTexture);
    if (FaceMap) Material->SetTextureParameterValue(TEXT("FaceMap"), FaceMap);
    return Material;
}

bool UHonkaiRenderInstance::Prepare(const FHonkaiRenderTargets& Targets, const TArray<FHonkaiSurfaceInput>& Surfaces,
    const FHonkaiFaceMaps& Maps, const FHonkaiRenderOptions& InOptions, UObject* ResourceOwner, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !IsValid(Targets.Surface) || !IsValid(Targets.Camera) || Surfaces.IsEmpty()
        || Targets.Surface->GetNumMaterials() != Surfaces.Num()
        || (Targets.Outline && (!IsValid(Targets.Outline) || Targets.Outline == Targets.Surface || Targets.Outline->GetNumMaterials() != Surfaces.Num())))
    {
        Error = TEXT("Bind requires a camera and a surface with matching material inputs; an optional outline must have the same slot count.");
        return false;
    }
    for (const auto& Input : Surfaces)
    {
        if (!IsFiniteColor(Input.Tint) || !IsFiniteColor(Input.OutlineColor) || !FMath::IsFinite(Input.ToonStrength)
            || !FMath::IsFinite(Input.SphereMode) || !FMath::IsFinite(Input.OutlineWidth) || Input.OutlineWidth < 0.f
            || !FMath::IsFinite(Input.OutlineOpacity) || Input.OutlineOpacity < 0.f || Input.OutlineOpacity > 1.f
            || Input.Tint.A < 0.f || Input.Tint.A > 1.f
            || uint8(Input.OutlineRegion) > uint8(EHonkaiOutlineRegion::Hair))
        { Error = TEXT("Surface values must be finite, opacity must lie in [0,1], and outline region must be Default, Skin or Hair."); return false; }
        for (UTexture* Texture : { Input.BaseTexture.Get(), static_cast<UTexture*>(Input.ToonRamp.Get()), Input.SphereTexture.Get() })
            if (!ValidateTexture(Texture, Error)) return false;
    }
    Inputs = Surfaces;
    Options = InOptions;
    if (!ValidateFaceMaps(Maps, Error)) return false;
    for (const TCHAR* Name : ParentNames)
    {
        const FString Path = FString::Printf(TEXT("/HonkaiToonRenderer/Materials/%s.%s"), Name, Name);
        auto* Parent = LoadObject<UMaterialInterface>(nullptr, *Path);
        if (!Parent) { Error = TEXT("Missing cooked renderer asset: ") + Path; return false; }
        Parents.Add(Parent);
    }
    TMap<UTexture*, uint8> RampRows;
    ToonAtlas = BuildToonAtlas(Inputs, RampRows);
    if (!ToonAtlas) { Error = TEXT("Unable to allocate the authored toon-ramp atlas."); return false; }
    // Copy CPU ramps before upload; every resource must exist before DMI uniform caching can sample it.
    if (!PrepareTexture(ToonAtlas, Error)) return false;
    for (const auto& Input : Inputs)
        for (UTexture* Texture : { Input.BaseTexture.Get(), static_cast<UTexture*>(Input.ToonRamp.Get()), Input.SphereTexture.Get() })
            if (!PrepareTexture(Texture, Error)) return false;
    if (!PrepareFaceMaps(Maps, Error)) return false;
    if (!Options.bBaseColorOnly)
    {
        CelPass = UMaterialInstanceDynamic::Create(Parents[EParent::Cel], ResourceOwner);
        if (!CelPass) { Error = TEXT("Unable to create the toon postprocess material."); return false; }
        CelPass->SetTextureParameterValue(TEXT("ToonAtlas"), ToonAtlas);
        CelPass->SetVectorParameterValue(TEXT("LightDirection"), FLinearColor(FVector::ForwardVector));
    }
    for (int32 I = 0; I < Inputs.Num(); ++I)
    {
        const auto& Input = Inputs[I];
        const int32 Parent = Input.bTranslucent ? (Input.bTwoSided ? EParent::TranslucentTwoSided : EParent::Translucent)
            : (Input.bTwoSided ? EParent::ToonTwoSided : EParent::Toon);
        auto* Material = UMaterialInstanceDynamic::Create(Parents[Parent], ResourceOwner);
        if (!Material) { Error = TEXT("Unable to create a surface material."); return false; }
        Materials.Add(Material);
        Material->SetScalarParameterValue(TEXT("ToonStrength"), Options.bBaseColorOnly ? 0.f : Input.ToonStrength);
        Material->SetScalarParameterValue(TEXT("SphereStrength"), Options.bBaseColorOnly ? 0.f : 1.f);
        Material->SetVectorParameterValue(TEXT("Tint"), Input.Tint);
        Material->SetScalarParameterValue(TEXT("Opacity"), Input.Tint.A);
        Material->SetVectorParameterValue(TEXT("LightDirection"), FLinearColor(FVector::ForwardVector));
        if (Input.BaseTexture) Material->SetTextureParameterValue(TEXT("BaseTexture"), Input.BaseTexture);
        if (const uint8* Row = RampRows.Find(Input.ToonRamp))
        {
            Material->SetScalarParameterValue(TEXT("RampRow"), *Row / 255.f);
            Material->SetTextureParameterValue(TEXT("ToonRamp"), Input.ToonRamp);
            Material->SetScalarParameterValue(TEXT("ToonRampEnabled"), 1.f);
        }
        if (Input.SphereTexture)
        {
            Material->SetTextureParameterValue(TEXT("SphereTexture"), Input.SphereTexture);
            Material->SetScalarParameterValue(TEXT("SphereMode"), Input.SphereMode);
        }
        auto* Ink = CreateOutline(I, nullptr, ResourceOwner);
        if (!Ink) { Error = TEXT("Unable to create an outline material."); return false; }
        BaseOutlines.Add(Ink);
    }
    FaceOutlines.SetNum(Inputs.Num()); OutlineFaceMaps.SetNum(Inputs.Num());
    for (const auto& Entry : Maps)
    {
        auto* Ink = CreateOutline(Entry.Key, Entry.Value, ResourceOwner);
        if (!Ink) { Error = TEXT("Unable to create a face outline material."); return false; }
        FaceOutlines[Entry.Key] = Ink; OutlineFaceMaps[Entry.Key] = Entry.Value;
        BaseOutlines[Entry.Key] = Ink;
    }
    ActiveFaceMaps = Maps;
    return true;
}

bool UHonkaiRenderInstance::Bind(const FHonkaiRenderTargets& Targets, const TArray<FHonkaiSurfaceInput>& Surfaces,
    const FHonkaiFaceMaps& Maps, const FHonkaiRenderOptions& InOptions, FString& Error)
{
    if (!IsInGameThread()) { Error = TEXT("Bind must run on the game thread."); return false; }
    TStrongObjectPtr<UHonkaiRenderInstance> Prepared(NewObject<UHonkaiRenderInstance>());
    if (!Prepared->Prepare(Targets, Surfaces, Maps, InOptions, this, Error)) return false;
    Detach();
    Surface = Targets.Surface; Outline = Targets.Outline; Camera = Targets.Camera;
    Inputs = MoveTemp(Prepared->Inputs); Parents = MoveTemp(Prepared->Parents);
    Materials = MoveTemp(Prepared->Materials); BaseOutlines = MoveTemp(Prepared->BaseOutlines);
    FaceOutlines = MoveTemp(Prepared->FaceOutlines); OutlineFaceMaps = MoveTemp(Prepared->OutlineFaceMaps);
    ActiveFaceMaps = MoveTemp(Prepared->ActiveFaceMaps); ToonAtlas = Prepared->ToonAtlas; CelPass = Prepared->CelPass;
    Options = InOptions;
    SavedSurfaceMaterials = Surface->OverrideMaterials;
    bOriginalCustomDepth = Surface->bRenderCustomDepth; OriginalStencil = Surface->CustomDepthStencilValue;
    OriginalStencilWriteMask = Surface->CustomDepthStencilWriteMask;
    Surface->SetRenderCustomDepth(true); Surface->SetCustomDepthStencilValue(1); Surface->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
    for (int32 I = 0; I < Materials.Num(); ++I) Surface->SetMaterial(I, Materials[I]);
    if (Outline.IsValid())
    {
        SavedOutlineMaterials = Outline->OverrideMaterials;
        bOriginalOutlineVisible = Outline->GetVisibleFlag();
        Outline->SetVisibility(!Options.bBaseColorOnly);
    }
    OriginalPostProcess = Camera->PostProcessSettings;
    if (Options.bApplyViewPreset) ApplyViewPreset(Camera->PostProcessSettings);
    if (CelPass) Camera->PostProcessSettings.AddBlendable(CelPass.Get(), 1.f);
    ApplyFaceMaps();
    return true;
}

void UHonkaiRenderInstance::ApplyFaceMaps()
{
    for (int32 I = 0; I < Materials.Num(); ++I)
    {
        const auto* Map = ActiveFaceMaps.Find(I);
        Materials[I]->SetScalarParameterValue(TEXT("FaceEnabled"), Map && !Options.bBaseColorOnly ? 1.f : 0.f);
        if (Map) Materials[I]->SetTextureParameterValue(TEXT("FaceMap"), Map->Get());
        if (Outline.IsValid()) Outline->SetMaterial(I,
            Map && !UsesFaceMap(BaseOutlines[I], Map->Get()) ? FaceOutlines[I] : BaseOutlines[I]);
    }
}

bool UHonkaiRenderInstance::PrepareFaceMaps(const FHonkaiFaceMaps& Maps, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || Inputs.IsEmpty()) { Error = TEXT("Face-map preparation requires a configured surface layout on the game thread."); return false; }
    if (!ValidateFaceMaps(Maps, Error)) return false;
    for (const auto& Entry : Maps) if (!PrepareTexture(Entry.Value, Error)) return false;
    return true;
}

bool UHonkaiRenderInstance::SetFaceMaps(const FHonkaiFaceMaps& Maps, FString& Error)
{
    Error.Reset();
    if (!IsInGameThread() || !Surface.IsValid()) { Error = TEXT("No live subject is bound on the game thread."); return false; }
    if (!PrepareFaceMaps(Maps, Error)) return false;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> Prepared = FaceOutlines;
    auto PreparedMaps = OutlineFaceMaps;
    for (const auto& Entry : Maps)
    {
        if (UsesFaceMap(BaseOutlines[Entry.Key], Entry.Value)) continue;
        if (OutlineFaceMaps[Entry.Key] == Entry.Value && Prepared[Entry.Key]) continue;
        Prepared[Entry.Key] = CreateOutline(Entry.Key, Entry.Value, this);
        if (!Prepared[Entry.Key]) { Error = TEXT("Unable to create the requested face outline material."); return false; }
        PreparedMaps[Entry.Key] = Entry.Value;
    }
    FaceOutlines = MoveTemp(Prepared);
    OutlineFaceMaps = MoveTemp(PreparedMaps);
    ActiveFaceMaps = Maps;
    ApplyFaceMaps();
    return true;
}

void UHonkaiRenderInstance::SetLighting(FVector WorldDirection, FVector FaceWorldDirection)
{
    if (!IsInGameThread() || WorldDirection.ContainsNaN() || FaceWorldDirection.ContainsNaN()
        || WorldDirection.IsNearlyZero() || FaceWorldDirection.IsNearlyZero()) return;
    if (CelPass) CelPass->SetVectorParameterValue(TEXT("LightDirection"), FLinearColor(WorldDirection));
    for (const auto& Material : Materials) if (Material) Material->SetVectorParameterValue(TEXT("LightDirection"), FLinearColor(FaceWorldDirection));
}

bool UHonkaiRenderInstance::SetTint(int32 Slot, FLinearColor Tint)
{
    if (!IsInGameThread() || !Materials.IsValidIndex(Slot) || !IsFiniteColor(Tint) || Tint.A < 0.f || Tint.A > 1.f) return false;
    Materials[Slot]->SetVectorParameterValue(TEXT("Tint"), Tint);
    Materials[Slot]->SetScalarParameterValue(TEXT("Opacity"), Tint.A);
    return true;
}

void UHonkaiRenderInstance::Detach()
{
    if (Surface.IsValid())
    {
        RestoreMaterials(Surface.Get(), SavedSurfaceMaterials);
        Surface->SetRenderCustomDepth(bOriginalCustomDepth); Surface->SetCustomDepthStencilValue(OriginalStencil);
        Surface->SetCustomDepthStencilWriteMask(OriginalStencilWriteMask);
    }
    if (Outline.IsValid())
    {
        RestoreMaterials(Outline.Get(), SavedOutlineMaterials);
        Outline->SetVisibility(bOriginalOutlineVisible);
    }
    if (Camera.IsValid())
    {
        if (CelPass) Camera->PostProcessSettings.RemoveBlendable(CelPass.Get());
        if (Options.bApplyViewPreset) RestoreViewPreset(Camera->PostProcessSettings, OriginalPostProcess);
    }
    Surface.Reset(); Outline.Reset(); Camera.Reset();
    Inputs.Reset(); Parents.Reset(); SavedSurfaceMaterials.Reset(); SavedOutlineMaterials.Reset();
    Materials.Reset(); BaseOutlines.Reset(); FaceOutlines.Reset(); OutlineFaceMaps.Reset(); ActiveFaceMaps.Reset();
    ToonAtlas = nullptr; CelPass = nullptr;
}

void UHonkaiRenderInstance::BeginDestroy()
{
    Detach();
    Super::BeginDestroy();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiRampCopyTest, "HonkaiToonRenderer.RampOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FHonkaiRampCopyTest::RunTest(const FString&)
{
    auto* Texture = UTexture2D::CreateTransient(1, 2, PF_B8G8R8A8);
    auto& Bulk = Texture->GetPlatformData()->Mips[0].BulkData;
    auto* Source = static_cast<FColor*>(Bulk.Lock(LOCK_READ_WRITE));
    Source[0] = FColor::White; Source[1] = FColor(180, 160, 200); Bulk.Unlock();
    TArray<FColor> Pixels; Pixels.Init(FColor::Black, AtlasSize * AtlasSize);
    TestTrue(TEXT("Valid authored gradient copies"), CopyToonRamp(Texture, Pixels, 1));
    TestEqual(TEXT("Shadow starts the row"), Pixels[256], FColor(180, 160, 200));
    TestEqual(TEXT("Light ends the row"), Pixels[511], FColor::White);
    TestEqual(TEXT("Other rows unchanged"), Pixels[255], FColor::Black);
    TestFalse(TEXT("Missing source uses fallback"), CopyToonRamp(nullptr, Pixels, 1));
    TestFalse(TEXT("Out-of-bounds row rejected"), CopyToonRamp(Texture, Pixels, 256));
    return true;
}
#endif
