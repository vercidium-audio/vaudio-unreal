using UnrealBuildTool;

public class VaudioUnrealEditor : ModuleRules
{
	public VaudioUnrealEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "VaudioUnreal" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AssetRegistry", "PropertyEditor", "Slate", "SlateCore", "ToolMenus", "Projects" });

#if !UE_5_0_OR_LATER
		CppStandard = CppStandardVersion.Cpp17;
#endif
	}
}
