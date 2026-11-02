#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Scene.h"
#include "UObject/Object.h"
#include "HonkaiRenderInstance.generated.h"

class UCameraComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class USkinnedMeshComponent;
class UTexture;
class UTexture2D;

enum class EHonkaiOutlineRegion : uint8 { Default, Skin, Hair };

/** Authored surface data. No importer, file path or character-name assumptions. */
USTRUCT()
struct HONKAITOONRENDERER_API FHonkaiSurfaceInput
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<UTexture> BaseTexture;
    UPROPERTY() TObjectPtr<UTexture2D> ToonRamp;
    UPROPERTY() TObjectPtr<UTexture> SphereTexture;
    FLinearColor Tint = FLinearColor::White;
    FLinearColor OutlineColor = FLinearColor::Black;
    float ToonStrength = 1.f;
    float SphereMode = 0.f;
    float OutlineWidth = 0.f; // UE units; the face variant uses 75% of this width.
    float OutlineOpacity = 0.f;
    EHonkaiOutlineRegion OutlineRegion = EHonkaiOutlineRegion::Default;
    bool bTranslucent = false;
    bool bTwoSided = false;
};

struct FHonkaiRenderTargets
{
    USkeletalMeshComponent* Surface = nullptr;
    USkinnedMeshComponent* Outline = nullptr; // Caller supplies a mesh sharing the surface pose.
    UCameraComponent* Camera = nullptr;
};

struct FHonkaiRenderOptions
{
    bool bBaseColorOnly = false;
    bool bApplyViewPreset = false; // Explicit opt-in: manual exposure, neutral tone mapping and bloom.
};

using FHonkaiFaceMaps = TMap<int32, TObjectPtr<UTexture2D>>;

/** One active subject and camera. Keep the instance in a UPROPERTY until Detach. */
UCLASS()
class HONKAITOONRENDERER_API UHonkaiRenderInstance : public UObject
{
    GENERATED_BODY()
public:
    /** Apply the view preset before a subject is loaded, when the host owns the whole camera. */
    static void ConfigureCamera(UCameraComponent* Camera);
    /** All methods run on the game thread. A rejected bind preserves the current subject. */
    bool Bind(const FHonkaiRenderTargets& Targets, const TArray<FHonkaiSurfaceInput>& Surfaces,
        const FHonkaiFaceMaps& FaceMaps, const FHonkaiRenderOptions& Options, FString& Error);
    /** Validate configured slot indices and prepare textures without changing active maps. */
    bool PrepareFaceMaps(const FHonkaiFaceMaps& Maps, FString& Error);
    /** Empty maps disable face shading; invalid slot/texture data changes nothing. */
    bool SetFaceMaps(const FHonkaiFaceMaps& Maps, FString& Error);
    /** Both vectors are world directions; the second may compensate current head motion. */
    void SetLighting(FVector WorldDirection, FVector FaceWorldDirection);
    bool SetTint(int32 Slot, FLinearColor Tint);
    void Detach();
    bool IsBound() const { return Surface.IsValid(); }
    int32 NumSurfaces() const { return Materials.Num(); }
    virtual void BeginDestroy() override;

private:
    bool Prepare(const FHonkaiRenderTargets& Targets, const TArray<FHonkaiSurfaceInput>& Surfaces,
        const FHonkaiFaceMaps& Maps, const FHonkaiRenderOptions& InOptions, UObject* ResourceOwner, FString& Error);
    bool ValidateFaceMaps(const FHonkaiFaceMaps& Maps, FString& Error) const;
    UMaterialInstanceDynamic* CreateOutline(int32 Slot, UTexture2D* FaceMap, UObject* ResourceOwner);
    void ApplyFaceMaps();

    TWeakObjectPtr<USkeletalMeshComponent> Surface;
    TWeakObjectPtr<USkinnedMeshComponent> Outline;
    TWeakObjectPtr<UCameraComponent> Camera;
    FHonkaiRenderOptions Options;
    FPostProcessSettings OriginalPostProcess;
    bool bOriginalCustomDepth = false, bOriginalOutlineVisible = true;
    int32 OriginalStencil = 0;
    ERendererStencilMask OriginalStencilWriteMask = ERendererStencilMask::ERSM_Default;
    UPROPERTY() TArray<FHonkaiSurfaceInput> Inputs;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Parents;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> SavedSurfaceMaterials;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> SavedOutlineMaterials;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> BaseOutlines;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> FaceOutlines;
    UPROPERTY() TArray<TObjectPtr<UTexture2D>> OutlineFaceMaps;
    UPROPERTY() TMap<int32, TObjectPtr<UTexture2D>> ActiveFaceMaps;
    UPROPERTY() TObjectPtr<UTexture2D> ToonAtlas;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CelPass;
};
