// SPDX-License-Identifier: MIT
#include "HonkaiUpscaling.h"
#include "HAL/IConsoleManager.h"
#if WITH_HONKAI_DLSS
#include "DLSSLibrary.h"
#endif

#if WITH_HONKAI_DLSS
namespace
{
const TCHAR* const OwnedCVars[] = { TEXT("r.AntiAliasingMethod"), TEXT("r.ScreenPercentage"),
    TEXT("r.TemporalAA.Upscaler"), TEXT("r.TemporalAA.Upsampling"),
    TEXT("r.DynamicRes.OperationMode"), TEXT("r.NGX.DLSS.Preset"), TEXT("r.NGX.DLSS.Enable") };

void ApplyValues(const TMap<FString, FString>& Values, FName Tag)
{
    for (const auto& Value : Values)
        if (auto* CVar = IConsoleManager::Get().FindConsoleVariable(*Value.Key))
            CVar->Set(*Value.Value, ECVF_SetByTemp, Tag);
}
}
#endif

bool FHonkaiUpscaling::IsSupported(FString& Reason)
{
    Reason.Reset();
#if WITH_HONKAI_DLSS
    if (UDLSSLibrary::IsDLSSSupported()) return true;
    switch (UDLSSLibrary::QueryDLSSSupport())
    {
    case UDLSSSupport::NotSupportedIncompatibleHardware: Reason = TEXT("需要 NVIDIA RTX 显卡。"); break;
    case UDLSSSupport::NotSupportedDriverOutOfDate: Reason = TEXT("显卡驱动版本不满足要求。"); break;
    case UDLSSSupport::NotSupportedOperatingSystemOutOfDate: Reason = TEXT("Windows 版本不满足要求。"); break;
    case UDLSSSupport::NotSupportedIncompatibleAPICaptureToolActive: Reason = TEXT("当前图形调试工具与 DLSS 不兼容。"); break;
    default: Reason = TEXT("当前运行环境不支持 NVIDIA DLSS。"); break;
    }
#else
    Reason = TEXT("此构建未安装可选的 NVIDIA DLSS 插件。");
#endif
    return false;
}

void FHonkaiUpscaling::Restore()
{
    // Removing the temporary layer retains constructor values and later console overrides.
    for (const FString& Name : Overridden)
        if (auto* CVar = IConsoleManager::Get().FindConsoleVariable(*Name)) CVar->Unset(ECVF_SetByTemp, OverrideTag);
    Overridden.Reset();
    Mode = EHonkaiUpscalingMode::TSR;
}

bool FHonkaiUpscaling::SetMode(EHonkaiUpscalingMode Requested, FVector2D OutputSize, FString& Error)
{
    Error.Reset();
    if (Requested == EHonkaiUpscalingMode::TSR) { Restore(); return true; }
    if (Requested != EHonkaiUpscalingMode::DLAA && Requested != EHonkaiUpscalingMode::DLSSQuality)
    { Error = TEXT("未知的抗锯齿模式。"); return false; }
    if (!FMath::IsFinite(OutputSize.X) || !FMath::IsFinite(OutputSize.Y) || OutputSize.X < 32 || OutputSize.Y < 32)
    { Error = TEXT("窗口尚未准备好，请恢复窗口后重试。"); return false; }
    if (!IsSupported(Error)) return false;
#if WITH_HONKAI_DLSS
    // This viewer has no ray-traced denoiser integration; do not change an external RR session.
    if (UDLSSLibrary::IsDLSSRREnabled())
    { Error = TEXT("请先关闭外部启用的 DLSS 光线重构。"); return false; }
    const UDLSSMode RequestedMode = Requested == EHonkaiUpscalingMode::DLAA ? UDLSSMode::DLAA : UDLSSMode::Quality;
    bool bSupported = false, bFixed = false;
    float Optimal = 0, Minimum = 0, Maximum = 0, UnusedSharpness = 0;
    UDLSSLibrary::GetDLSSModeInformation(RequestedMode, OutputSize, bSupported, Optimal, bFixed, Minimum, Maximum, UnusedSharpness);
    if (!bSupported || !FMath::IsFinite(Optimal) || Optimal < 25 || Optimal > 100)
    { Error = TEXT("当前设备不支持所选 DLSS 模式。"); return false; }
    TMap<FString, FString> Before;
    for (const TCHAR* Name : OwnedCVars)
    {
        auto* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
        if (!CVar) { Error = TEXT("DLSS 插件尚未初始化。"); return false; }
        const uint32 Priority = CVar->GetFlags() & ECVF_SetByMask;
        if (Priority > ECVF_SetByTemp || (Priority == ECVF_SetByTemp && !Overridden.Contains(Name)))
        { Error = TEXT("抗锯齿设置正被外部临时设置或控制台接管。"); return false; }
        Before.Add(Name, CVar->GetString());
    }
    const bool bPreviouslyEnabled = UDLSSLibrary::IsDLSSEnabled();
    if (Overridden.IsEmpty() && bPreviouslyEnabled)
    { Error = TEXT("DLSS 正被外部设置接管。"); return false; }
    TMap<FString, FString> Proposed{
        {TEXT("r.AntiAliasingMethod"), TEXT("4")}, {TEXT("r.TemporalAA.Upscaler"), TEXT("1")},
        {TEXT("r.TemporalAA.Upsampling"), TEXT("1")}, {TEXT("r.DynamicRes.OperationMode"), TEXT("0")},
        {TEXT("r.NGX.DLSS.Preset"), TEXT("13")},
        {TEXT("r.NGX.DLSS.Enable"), TEXT("1")},
        {TEXT("r.ScreenPercentage"), FString::SanitizeFloat(Requested == EHonkaiUpscalingMode::DLAA ? 100.f : Optimal)}};
    ApplyValues(Proposed, OverrideTag);
    if (!UDLSSLibrary::IsDLSSEnabled())
    {
        if (!Overridden.IsEmpty()) ApplyValues(Before, OverrideTag);
        else for (const auto& Value : Before)
            IConsoleManager::Get().FindConsoleVariable(*Value.Key)->Unset(ECVF_SetByTemp, OverrideTag);
        Error = TEXT("未能启用 DLSS，已恢复原设置。"); return false;
    }
    for (const auto& Value : Before) Overridden.Add(Value.Key);
    Mode = Requested;
    UE_LOG(LogTemp, Display, TEXT("HONKAI_UPSCALING requested=%s expected_plugin=8.9.0 expected_NGX=310.9.16 preset=M(13) screen_percentage=%s; verify LogDLSSNGXRHI for active GPU feature"),
        Requested == EHonkaiUpscalingMode::DLAA ? TEXT("DLAA") : TEXT("Quality"), *Proposed[TEXT("r.ScreenPercentage")]);
    return true;
#else
    return false;
#endif
}
