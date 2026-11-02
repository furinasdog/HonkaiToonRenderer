// SPDX-License-Identifier: MIT
#include "HonkaiUpscaling.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiUpscalingRejectionTest, "HonkaiToon.Upscaling.Rejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FHonkaiUpscalingRejectionTest::RunTest(const FString&)
{
    auto* AA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"));
    auto* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
    const int32 OldAA = AA->GetInt(); const float OldScale = Scale->GetFloat();
    FHonkaiUpscaling Settings; FString Error;
    TestFalse(TEXT("Invalid mode is rejected"), Settings.SetMode(static_cast<EHonkaiUpscalingMode>(255), {1280,800}, Error));
    TestFalse(TEXT("Minimized viewport is rejected"), Settings.SetMode(EHonkaiUpscalingMode::DLAA, {8,8}, Error));
    TestFalse(TEXT("Nonfinite viewport is rejected"), Settings.SetMode(EHonkaiUpscalingMode::DLAA, {std::numeric_limits<double>::quiet_NaN(),800}, Error));
    const bool bSupported = FHonkaiUpscaling::IsSupported(Error);
    const bool bNullRHI = FParse::Param(FCommandLine::Get(), TEXT("nullrhi"));
    UE_LOG(LogTemp, Display, TEXT("HONKAI_UPSCALING_SUPPORT compiled_sdk=%d nullrhi=%d supported=%d reason=%s"),
        WITH_HONKAI_DLSS, bNullRHI, bSupported, *Error);
    if (bNullRHI) TestFalse(TEXT("NullRHI cannot report a working DLSS renderer"), bSupported);
    if (!bSupported)
        TestFalse(TEXT("Unsupported hardware or SDK is rejected"), Settings.SetMode(EHonkaiUpscalingMode::DLSSQuality, {1280,800}, Error));
    TestEqual(TEXT("Rejected requests preserve AA"), AA->GetInt(), OldAA);
    TestEqual(TEXT("Rejected requests preserve resolution"), Scale->GetFloat(), OldScale);
    TestTrue(TEXT("Off remains available"), Settings.SetMode(EHonkaiUpscalingMode::TSR, {0,0}, Error));
    TestFalse(TEXT("Rejected requests never activate mode"), Settings.IsActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHonkaiUpscalingRestoreTest, "HonkaiToon.Upscaling.Restore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FHonkaiUpscalingRestoreTest::RunTest(const FString&)
{
    const FString Name = TEXT("HonkaiToon.Test.Restore.") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto& Console = IConsoleManager::Get();
    auto* Value = Console.RegisterConsoleVariable(*Name, TEXT("137.5"), TEXT("Private restore fixture"), ECVF_Default);
    const uint32 OriginalFlags = Value->GetFlags();
    FHonkaiUpscaling Settings;
    Settings.Overridden.Add(Name);
    Settings.Mode = EHonkaiUpscalingMode::DLSSQuality;
    Value->Set(TEXT("50"), ECVF_SetByTemp, Settings.OverrideTag);
    Settings.Restore();
    TestEqual(TEXT("Restore puts back exact saved value"), Value->GetString(), FString(TEXT("137.5")));
    TestEqual(TEXT("Restore retains CVar priority and flags"), Value->GetFlags(), OriginalFlags);
    TestFalse(TEXT("Restore clears active mode"), Settings.IsActive());
    TestTrue(TEXT("Restore clears saved state"), Settings.Overridden.IsEmpty());
    Value->Set(TEXT("75"), ECVF_SetByGameSetting);
    Settings.Restore();
    TestEqual(TEXT("Second restore does not overwrite later changes"), Value->GetString(), FString(TEXT("75")));
    const FName OtherOwner(TEXT("HonkaiToon.Test.OtherOwner"));
    Value->Set(TEXT("90"), ECVF_SetByTemp, OtherOwner);
    Settings.Overridden.Add(Name);
    Value->Set(TEXT("50"), ECVF_SetByTemp, Settings.OverrideTag);
    Settings.Restore();
    TestEqual(TEXT("Restore preserves another tagged temporary owner"), Value->GetString(), FString(TEXT("90")));
    Value->Unset(ECVF_SetByTemp, OtherOwner);
    TestEqual(TEXT("Underlying game setting remains intact"), Value->GetString(), FString(TEXT("75")));
    Settings.Overridden.Add(Name);
    Value->Set(TEXT("50"), ECVF_SetByTemp, Settings.OverrideTag);
    Value->Set(TEXT("44"), ECVF_SetByConsole);
    Settings.Restore();
    TestEqual(TEXT("Later console override is retained"), Value->GetString(), FString(TEXT("44")));
    Value->Unset(ECVF_SetByConsole);
    TestEqual(TEXT("Temporary cleanup did not replace original game setting"), Value->GetString(), FString(TEXT("75")));
    Console.UnregisterConsoleObject(*Name, false);
    return true;
}
#endif
