using System.IO;
using UnrealBuildTool;

public class VaudioUnreal : ModuleRules
{
	public VaudioUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AudioMixer", "MeshDescription", "StaticMeshDescription" });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "MaterialEditor" });
		}

		// The host project's ThirdParty folder, so a project that loads the plugin via AdditionalPluginDirectories (e.g. the test devproject) uses its own SDK copy
		string SDKPath = Path.Combine(Target.ProjectFile.Directory.FullName, "ThirdParty", "vaudio");

		PublicIncludePaths.Add(Path.Combine(SDKPath, "include"));
		PublicAdditionalLibraries.Add(Path.Combine(SDKPath, "lib", "Win64", "vaudionative.lib"));

		string DllSource = Path.Combine(SDKPath, "lib", "Win64", "vaudionative.dll");
		string DllDest   = Path.Combine("$(BinaryOutputDir)", "vaudionative.dll");
		RuntimeDependencies.Add(DllDest, DllSource);
		PublicDelayLoadDLLs.Add("vaudionative.dll");
	}
}
