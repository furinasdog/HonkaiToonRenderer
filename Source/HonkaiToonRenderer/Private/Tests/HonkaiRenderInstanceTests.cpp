#include "HonkaiRenderInstance.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

    UTexture2D* MakeFaceMap()
    {
        auto* Texture = UTexture2D::CreateTransient(1, 1, PF_B8G8R8A8);
        Texture->SRGB = false;
        Texture->AddressX = Texture->AddressY = TA_Clamp;
        return Texture;
    }

    UTexture2D* MakeMiplessTexture()
    {
        auto* Texture = NewObject<UTexture2D>();
        auto* Data = new FTexturePlatformData();
        Data->SizeX = Data->SizeY = 1; Data->PixelFormat = PF_B8G8R8A8;
        Texture->SetPlatformData(Data);
        Texture->SRGB = false; Texture->AddressX = Texture->AddressY = TA_Clamp;
        return Texture;
    }

    struct FRendererFixture
    {
        TStrongObjectPtr<UHonkaiRenderInstance> Renderer { NewObject<UHonkaiRenderInstance>() };
        TStrongObjectPtr<USkeletalMesh> Asset { NewObject<USkeletalMesh>() };
        TStrongObjectPtr<USkeletalMeshComponent> Surface { NewObject<USkeletalMeshComponent>() };
        TStrongObjectPtr<USkeletalMeshComponent> Outline { NewObject<USkeletalMeshComponent>() };
        TStrongObjectPtr<UCameraComponent> Camera { NewObject<UCameraComponent>() };
        TArray<FHonkaiSurfaceInput> Inputs;

        FRendererFixture(int32 Slots = 1)
        {
            for (int32 I = 0; I < Slots; ++I) Asset->GetMaterials().Add(FSkeletalMaterial(nullptr));
            Surface->SetSkeletalMesh(Asset.Get()); Outline->SetSkeletalMesh(Asset.Get());
            Inputs.SetNum(Slots);
            for (auto& Input : Inputs)
            {
                Input.Tint = FLinearColor(.8f, .7f, .6f, .4f);
                Input.OutlineColor = FLinearColor(.1f, .1f, .1f);
                Input.OutlineWidth = .16f; Input.OutlineOpacity = .3f;
            }
        }

        FHonkaiRenderTargets Targets() const { return { Surface.Get(), Outline.Get(), Camera.Get() }; }
        UMaterialInstanceDynamic* Material(int32 Slot = 0) const { return Cast<UMaterialInstanceDynamic>(Surface->GetMaterial(Slot)); }
        ~FRendererFixture() { Renderer->Detach(); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiRenderLifecycleTest, "HonkaiToonRenderer.Lifecycle", Flags)
bool FHonkaiRenderLifecycleTest::RunTest(const FString&)
{
    FRendererFixture Fixture;
    auto* OriginalMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
    if (!TestNotNull(TEXT("Engine default material"), OriginalMaterial)) return false;
    Fixture.Surface->SetMaterial(0, OriginalMaterial);
    Fixture.Surface->SetOverlayMaterial(OriginalMaterial, true, 0);
    Fixture.Outline->SetOverlayMaterial(OriginalMaterial, true, 0);
    Fixture.Surface->SetCustomDepthStencilValue(17);
    Fixture.Surface->SetRenderCustomDepth(false);
    Fixture.Outline->SetVisibility(false);
    Fixture.Camera->PostProcessSettings.bOverride_BloomIntensity = true;
    Fixture.Camera->PostProcessSettings.BloomIntensity = .35f;
    auto* OtherBlendable = UMaterialInstanceDynamic::Create(
        LoadObject<UMaterialInterface>(nullptr, TEXT("/HonkaiToonRenderer/Materials/M_CelLighting.M_CelLighting")), Fixture.Camera.Get());
    if (!TestNotNull(TEXT("Cooked cel parent exists"), OtherBlendable)) return false;
    Fixture.Camera->PostProcessSettings.AddBlendable(OtherBlendable, .2f);
    FHonkaiFaceMaps Maps; Maps.Add(0, MakeFaceMap());
    const TWeakObjectPtr<UTexture2D> WeakMap = Maps[0].Get();
    FHonkaiRenderOptions Options; Options.bApplyViewPreset = true;
    FString Error;
    if (!TestTrue(TEXT("Public bind succeeds"), Fixture.Renderer->Bind(Fixture.Targets(), Fixture.Inputs, Maps, Options, Error)))
    { AddError(Error); return false; }
    auto* Material = Fixture.Material();
    if (!TestNotNull(TEXT("Surface is a dynamic material"), Material)) return false;
    TestEqual(TEXT("Postprocess retains other blendable"), Fixture.Camera->PostProcessSettings.WeightedBlendables.Array.Num(), 2);
    TestTrue(TEXT("Custom depth enabled"), bool(Fixture.Surface->bRenderCustomDepth));
    TestEqual(TEXT("Renderer stencil"), Fixture.Surface->CustomDepthStencilValue, 1);
    TestTrue(TEXT("Outline is visible"), Fixture.Outline->GetVisibleFlag());
    FLinearColor Tint;
    Material->GetVectorParameterValue(TEXT("Tint"), Tint);
    TestTrue(TEXT("Authored tint and opacity retained"), Tint.Equals(Fixture.Inputs[0].Tint));
    float Width = 0;
    Fixture.Outline->GetMaterial(0)->GetScalarParameterValue(TEXT("OutlineWidth"), Width);
    TestTrue(TEXT("Face outline width retains current ratio"), FMath::IsNearlyEqual(Width, .12f));
    TestTrue(TEXT("Preview tint accepts the same public interface"), Fixture.Renderer->SetTint(0, FLinearColor(.2f, .9f, .5f, .4f)));
    TestTrue(TEXT("Original tint restores"), Fixture.Renderer->SetTint(0, Fixture.Inputs[0].Tint));
    Fixture.Renderer->SetLighting(FVector(1, 0, 0), FVector(0, 1, 0));
    Material->GetVectorParameterValue(TEXT("LightDirection"), Tint);
    TestTrue(TEXT("Face world-space light reaches the material"), Tint.Equals(FLinearColor(0, 1, 0, 1)));

    Maps.Reset();
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Instance owns active face map through GC"), WeakMap.IsValid());
    UTexture* ActiveMap = nullptr;
    Material->GetTextureParameterValue(TEXT("FaceMap"), ActiveMap);
    TestTrue(TEXT("GC retains map binding"), ActiveMap == WeakMap.Get());
    TestTrue(TEXT("Empty map set disables face shading"), Fixture.Renderer->SetFaceMaps({}, Error));
    float Enabled = 1;
    Material->GetScalarParameterValue(TEXT("FaceEnabled"), Enabled);
    TestEqual(TEXT("Face disabled without component replacement"), Enabled, 0.f);
    Fixture.Outline->GetMaterial(0)->GetScalarParameterValue(TEXT("OutlineWidth"), Width);
    TestTrue(TEXT("Bind-time face outline is retained when threshold shading is disabled"), FMath::IsNearlyEqual(Width, .12f));
    Maps.Add(0, WeakMap.Get());
    TestTrue(TEXT("Same cached face map reapplies"), Fixture.Renderer->SetFaceMaps(Maps, Error));

    Fixture.Camera->PostProcessSettings.bOverride_ColorSaturation = true;
    Fixture.Camera->PostProcessSettings.ColorSaturation = FVector4(1.1, 1.2, 1.3, 1.0);
    auto* ChangedOverlay = LoadObject<UMaterialInterface>(nullptr, TEXT("/HonkaiToonRenderer/Materials/M_Outline.M_Outline"));
    Fixture.Surface->SetOverlayMaterial(ChangedOverlay, true, 0);
    Fixture.Outline->SetOverlayMaterial(ChangedOverlay, true, 0);
    Fixture.Renderer->Detach();
    TestTrue(TEXT("Original material override restored"), Fixture.Surface->GetMaterial(0) == OriginalMaterial);
    TestFalse(TEXT("Original custom depth flag restored"), bool(Fixture.Surface->bRenderCustomDepth));
    TestEqual(TEXT("Original stencil restored"), Fixture.Surface->CustomDepthStencilValue, 17);
    TestFalse(TEXT("Original outline visibility restored"), Fixture.Outline->GetVisibleFlag());
    TestTrue(TEXT("Detach preserves current host surface slot overlay"), Fixture.Surface->GetOverlayMaterial(true, 0) == ChangedOverlay);
    TestTrue(TEXT("Detach preserves current host outline slot overlay"), Fixture.Outline->GetOverlayMaterial(true, 0) == ChangedOverlay);
    TestTrue(TEXT("Original view preset value restored"), FMath::IsNearlyEqual(Fixture.Camera->PostProcessSettings.BloomIntensity, .35f));
    TestEqual(TEXT("Only own blendable removed"), Fixture.Camera->PostProcessSettings.WeightedBlendables.Array.Num(), 1);
    TestTrue(TEXT("Other blendable survives"), Fixture.Camera->PostProcessSettings.WeightedBlendables.Array[0].Object == OtherBlendable);
    TestTrue(TEXT("Unowned postprocess change survives"), Fixture.Camera->PostProcessSettings.ColorSaturation.Equals(FVector4(1.1, 1.2, 1.3, 1.0)));
    Fixture.Renderer->Detach();
    TestEqual(TEXT("Detach is idempotent"), Fixture.Camera->PostProcessSettings.WeightedBlendables.Array.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiRenderAtomicTest, "HonkaiToonRenderer.AtomicReplacement", Flags)
bool FHonkaiRenderAtomicTest::RunTest(const FString&)
{
    FRendererFixture Fixture;
    FString Error;
    FHonkaiFaceMaps Maps; Maps.Add(0, MakeFaceMap());
    if (!TestTrue(TEXT("Initial binding succeeds"), Fixture.Renderer->Bind(Fixture.Targets(), Fixture.Inputs, Maps, {}, Error)))
    { AddError(Error); return false; }
    auto* Material = Fixture.Material();
    auto* Ink = Fixture.Outline->GetMaterial(0);
    auto* Map = Maps[0].Get();
    const int32 Blendables = Fixture.Camera->PostProcessSettings.WeightedBlendables.Array.Num();
    for (int32 Case = 0; Case < 4; ++Case)
    {
        FHonkaiFaceMaps Invalid;
        if (Case == 0) Invalid.Add(9, Map);
        if (Case == 1) Invalid.Add(0, nullptr);
        if (Case == 2) { auto* SRGBMap = MakeFaceMap(); SRGBMap->SRGB = true; Invalid.Add(0, SRGBMap); }
        if (Case == 3)
        {
            auto* NoMips = MakeMiplessTexture();
            TestEqual(TEXT("Mipless counterexample still has positive reported size"), NoMips->GetSizeX(), 1);
            Invalid.Add(0, NoMips);
        }
        TestFalse(TEXT("Invalid face-map request is rejected"), Fixture.Renderer->SetFaceMaps(Invalid, Error));
        TestFalse(TEXT("Validation explains the error"), Error.IsEmpty());
        TestTrue(TEXT("Invalid map retains surface and outline"), Fixture.Material() == Material && Fixture.Outline->GetMaterial(0) == Ink);
        UTexture* Active = nullptr; Material->GetTextureParameterValue(TEXT("FaceMap"), Active);
        TestTrue(TEXT("Invalid map retains active face texture"), Active == Map);
    }
    TArray<FHonkaiSurfaceInput> Empty;
    TestFalse(TEXT("Invalid rebind is rejected"), Fixture.Renderer->Bind(Fixture.Targets(), Empty, {}, {}, Error));
    TestTrue(TEXT("Failed bind retains old live subject"), Fixture.Renderer->IsBound() && Fixture.Material() == Material);
    TestEqual(TEXT("Failed bind retains own postprocess"), Fixture.Camera->PostProcessSettings.WeightedBlendables.Array.Num(), Blendables);
    TestFalse(TEXT("Invalid preview slot is rejected"), Fixture.Renderer->SetTint(5, FLinearColor::White));
    for (int32 Case = 0; Case < 4; ++Case)
    {
        auto Invalid = Fixture.Inputs;
        if (Case == 0) Invalid[0].BaseTexture = MakeMiplessTexture();
        if (Case == 1) Invalid[0].SphereTexture = NewObject<UTextureCube>();
        if (Case == 2) Invalid[0].ToonRamp = MakeMiplessTexture();
        if (Case == 3) Invalid[0].OutlineRegion = static_cast<EHonkaiOutlineRegion>(255);
        TestFalse(TEXT("Malformed texture or outline input is rejected before rebind"), Fixture.Renderer->Bind(Fixture.Targets(), Invalid, Maps, {}, Error));
        TestTrue(TEXT("Failed texture/enum replacement retains bound material"), Fixture.Material() == Material);
        TestEqual(TEXT("Failed texture/enum replacement retains camera blendables"), Fixture.Camera->PostProcessSettings.WeightedBlendables.Array.Num(), Blendables);
    }
    TestTrue(TEXT("Rebind same subject succeeds"), Fixture.Renderer->Bind(Fixture.Targets(), Fixture.Inputs, Maps, {}, Error));
    TestEqual(TEXT("Rebind does not duplicate postprocess"), Fixture.Camera->PostProcessSettings.WeightedBlendables.Array.Num(), Blendables);
    Fixture.Renderer->Detach();
    TestEqual(TEXT("Original empty override array is restored"), Fixture.Surface->GetNumOverrideMaterials(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiRenderTextureAndOutlineTest, "HonkaiToonRenderer.TextureAndOutlineReplacement", Flags)
bool FHonkaiRenderTextureAndOutlineTest::RunTest(const FString&)
{
    FRendererFixture Fixture(2);
    auto* Base = MakeFaceMap(); auto* Sphere = MakeFaceMap(); auto* Ramp = MakeFaceMap();
    Fixture.Inputs[0].BaseTexture = Base; Fixture.Inputs[0].SphereTexture = Sphere; Fixture.Inputs[0].ToonRamp = Ramp;
    FHonkaiFaceMaps Initial; Initial.Add(0, MakeFaceMap()); Initial.Add(1, MakeFaceMap());
    FString Error;
    if (!TestTrue(TEXT("Valid 2D inputs without existing resources bind"), Fixture.Renderer->Bind(Fixture.Targets(), Fixture.Inputs, Initial, {}, Error)))
    { AddError(Error); return false; }
    if (FApp::CanEverRender())
    {
        for (UTexture* Texture : { Base, Sphere, Ramp, Initial[0].Get(), Initial[1].Get() })
            TestNotNull(TEXT("Every referenced texture has a render resource on a rendering target"), Texture->GetResource());
    }
    auto* OriginalA = Fixture.Outline->GetMaterial(0);
    auto* OriginalB = Fixture.Outline->GetMaterial(1);
    FHonkaiFaceMaps Replacement; Replacement.Add(0, MakeFaceMap());
    TestTrue(TEXT("Inactive profile resources can be prepared"), Fixture.Renderer->PrepareFaceMaps(Replacement, Error));
    UTexture* ActiveBeforeCommit = nullptr;
    Fixture.Material(0)->GetTextureParameterValue(TEXT("FaceMap"), ActiveBeforeCommit);
    TestTrue(TEXT("Preparing a profile leaves current surface/outline maps unchanged"), ActiveBeforeCommit == Initial[0].Get()
        && Fixture.Outline->GetMaterial(0) == OriginalA && Fixture.Outline->GetMaterial(1) == OriginalB);
    TestTrue(TEXT("One-slot enhancement replaces its own map"), Fixture.Renderer->SetFaceMaps(Replacement, Error));
    TestTrue(TEXT("Uncovered outline retains exact bind-time original instance"), Fixture.Outline->GetMaterial(1) == OriginalB);
    TestTrue(TEXT("Covered outline uses new face map"), Fixture.Outline->GetMaterial(0) != OriginalA);
    auto* ReplacementOutline = Fixture.Outline->GetMaterial(0);
    float Enabled = 1.f;
    Fixture.Material(1)->GetScalarParameterValue(TEXT("FaceEnabled"), Enabled);
    TestEqual(TEXT("Uncovered surface disables threshold shading"), Enabled, 0.f);
    TestTrue(TEXT("Original profile can be selected again"), Fixture.Renderer->SetFaceMaps(Initial, Error));
    TestTrue(TEXT("Original outline instance is reused"), Fixture.Outline->GetMaterial(0) == OriginalA);
    TestTrue(TEXT("Enhanced profile can be selected again"), Fixture.Renderer->SetFaceMaps(Replacement, Error));
    TestTrue(TEXT("Enhanced outline instance is reused after A/B"), Fixture.Outline->GetMaterial(0) == ReplacementOutline);
    TestTrue(TEXT("Empty map set is accepted"), Fixture.Renderer->SetFaceMaps({}, Error));
    TestTrue(TEXT("Empty map set restores both original outline instances"), Fixture.Outline->GetMaterial(0) == OriginalA && Fixture.Outline->GetMaterial(1) == OriginalB);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiRenderSurfaceTest, "HonkaiToonRenderer.SurfaceVariants", Flags)
bool FHonkaiRenderSurfaceTest::RunTest(const FString&)
{
    FRendererFixture Fixture(4);
    for (int32 I = 0; I < 4; ++I)
    { Fixture.Inputs[I].bTranslucent = I >= 2; Fixture.Inputs[I].bTwoSided = bool(I & 1); }
    auto* Ramp = UTexture2D::CreateTransient(1, 2, PF_B8G8R8A8);
    auto& Bulk = Ramp->GetPlatformData()->Mips[0].BulkData;
    auto* Pixels = static_cast<FColor*>(Bulk.Lock(LOCK_READ_WRITE));
    Pixels[0] = FColor::White; Pixels[1] = FColor(180, 160, 200); Bulk.Unlock();
    Fixture.Inputs[0].ToonRamp = Ramp; Fixture.Inputs[1].ToonRamp = Ramp;
    FString Error;
    if (!TestTrue(TEXT("All four surface variants bind"), Fixture.Renderer->Bind(Fixture.Targets(), Fixture.Inputs, {}, {}, Error)))
    { AddError(Error); return false; }
    for (int32 I = 0; I < 4; ++I)
    {
        auto* Material = Fixture.Material(I);
        if (!TestNotNull(TEXT("Dynamic surface material exists"), Material)) return false;
        TestEqual(TEXT("Authored double-sided flag retained"), Material->IsTwoSided(), bool(I & 1));
        TestEqual(TEXT("Authored transparency retained"), IsTranslucentBlendMode(Material->GetBlendMode()), I >= 2);
        float Row = 0; Material->GetScalarParameterValue(TEXT("RampRow"), Row);
        TestTrue(TEXT("Same source ramp shares one AO row; missing ramp keeps fallback"), FMath::IsNearlyEqual(Row, I < 2 ? 1.f / 255.f : 0.f));
    }
    Fixture.Renderer->Detach();
    FHonkaiRenderOptions BaseOnly; BaseOnly.bBaseColorOnly = true;
    TestTrue(TEXT("Base-color diagnostic binds"), Fixture.Renderer->Bind(Fixture.Targets(), Fixture.Inputs, {}, BaseOnly, Error));
    TestFalse(TEXT("Base diagnostic hides outline"), Fixture.Outline->GetVisibleFlag());
    TestEqual(TEXT("Base diagnostic adds no toon postprocess"), Fixture.Camera->PostProcessSettings.WeightedBlendables.Array.Num(), 0);
    return true;
}
#endif
