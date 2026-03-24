using UnrealBuildTool;

public class AvatarVR : ModuleRules
{
    public AvatarVR(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "HTTP",
            "HTTPServer",
            "Json",
            "JsonUtilities",
            "ACECore",
            "ACERuntime",
            "MRUtilityKit",
            "OculusXRAnchors",
            "OculusXRScene"
        });
    }
}