using UnrealBuildTool;

public class VaudioUnrealEditor : ModuleRules
{
	public VaudioUnrealEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "VaudioUnreal" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AssetRegistry" });
	}
}
