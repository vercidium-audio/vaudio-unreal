using System.IO;
using UnrealBuildTool;

public class VaudioUnreal : ModuleRules
{
	public VaudioUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AudioMixer", "Synthesis", "MeshDescription", "StaticMeshDescription" });

		if (Target.bBuildEditor)
		{
			// Editor-only: UVAudioVisualisationComponent's "Generate Fade Nodes" button programmatically
			// builds the DiamondMaterial fade graph (see VAudioVisualisationComponent.cpp, #if WITH_EDITOR).
			// Never referenced outside WITH_EDITOR blocks, so this never ships in packaged builds.
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "MaterialEditor" });
		}

		string SDKPath = Path.Combine(ModuleDirectory, "../../../../ThirdParty/vaudio");

		PublicIncludePaths.Add(Path.Combine(SDKPath, "include"));
		PublicAdditionalLibraries.Add(Path.Combine(SDKPath, "lib", "Win64", "vaudionative.lib"));

		string DllSource = Path.Combine(SDKPath, "lib", "Win64", "vaudionative.dll");
		string DllDest   = Path.Combine("$(BinaryOutputDir)", "vaudionative.dll");
		RuntimeDependencies.Add(DllDest, DllSource);
		PublicDelayLoadDLLs.Add("vaudionative.dll");
	}
}
