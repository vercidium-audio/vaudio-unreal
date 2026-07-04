using System.IO;
using UnrealBuildTool;

public class VercidiumAudio : ModuleRules
{
	public VercidiumAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AudioMixer", "Synthesis" });

		string SDKPath = Path.Combine(ModuleDirectory, "../../../../ThirdParty/vaudio");

		PublicIncludePaths.Add(Path.Combine(SDKPath, "include"));
		PublicAdditionalLibraries.Add(Path.Combine(SDKPath, "lib", "Win64", "vaudionative.lib"));

		string DllSource = Path.Combine(SDKPath, "lib", "Win64", "vaudionative.dll");
		string DllDest   = Path.Combine("$(BinaryOutputDir)", "vaudionative.dll");
		RuntimeDependencies.Add(DllDest, DllSource);
		PublicDelayLoadDLLs.Add("vaudionative.dll");
	}
}
