// SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"

enum class EHonkaiUpscalingMode : uint8 { TSR, DLAA, DLSSQuality };

// Game thread only; one instance owns application-wide CVars, not individual models.
// The owner must call Restore from EndPlay before the viewport is destroyed.
class HONKAITOONRENDERER_API FHonkaiUpscaling
{
public:
    FHonkaiUpscaling() = default;
    FHonkaiUpscaling(const FHonkaiUpscaling&) = delete;
    FHonkaiUpscaling& operator=(const FHonkaiUpscaling&) = delete;
    bool SetMode(EHonkaiUpscalingMode Requested, FVector2D OutputSize, FString& Error);
    void Restore();
    static bool IsSupported(FString& Reason);
    EHonkaiUpscalingMode GetMode() const { return Mode; }
    bool IsActive() const { return Mode != EHonkaiUpscalingMode::TSR; }
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FHonkaiUpscalingRestoreTest;
#endif
    TSet<FString> Overridden;
    const FName OverrideTag = TEXT("HonkaiToon.Upscaling");
    EHonkaiUpscalingMode Mode = EHonkaiUpscalingMode::TSR;
};
