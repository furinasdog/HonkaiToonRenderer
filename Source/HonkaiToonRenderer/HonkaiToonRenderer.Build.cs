using UnrealBuildTool;

public class HonkaiToonRenderer : ModuleRules
{
    public HonkaiToonRenderer(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        bool HasOfficialDlss = Target.Platform == UnrealTargetPlatform.Win64 && Target.ProjectFile != null
            && System.IO.File.Exists(System.IO.Path.Combine(Target.ProjectFile.Directory.FullName, "Plugins", "NVIDIA", "DLSS", "DLSS.uplugin"))
            && System.IO.File.Exists(System.IO.Path.Combine(Target.ProjectFile.Directory.FullName, "Plugins", "NVIDIA", "StreamlineNGXCommon", "StreamlineNGXCommon.uplugin"));
        PrivateDefinitions.Add("WITH_HONKAI_DLSS=" + (HasOfficialDlss ? "1" : "0"));
        if (HasOfficialDlss) PrivateDependencyModuleNames.Add("DLSSBlueprint");
    }
}
