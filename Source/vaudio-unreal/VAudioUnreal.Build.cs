using System.IO;
using UnrealBuildTool;

public class VaudioUnreal : ModuleRules
{
	public VaudioUnreal(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AudioMixer", "MeshDescription", "StaticMeshDescription" });
		PrivateDependencyModuleNames.Add("Projects");

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "MaterialEditor" });
		}

		// The SDK is vendored inside the plugin, and its shared library is staged into Binaries/ThirdParty/vaudio/<platform>, where FVaudioUnrealModule::StartupModule loads it from
		string sdkPath = Path.Combine(ModuleDirectory, "..", "ThirdParty", "vaudio");
		PublicIncludePaths.Add(Path.Combine(sdkPath, "include"));

		string platform;
		string library;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			platform = "Win64";
			library = "vaudionative.dll";
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			platform = "Linux";
			library = "libvaudionative.so";
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			platform = "Mac";
			library = "libvaudionative.dylib";
		}
		else
		{
			throw new BuildException("vaudio-unreal doesn't support " + Target.Platform + ", only Win64, Linux and Mac");
		}

		string libPath = Path.Combine(sdkPath, "lib", platform);
		string libraryPath = Path.Combine(libPath, library);

		if (!File.Exists(libraryPath))
		{
			throw new BuildException("Vercidium Audio SDK not found at " + libraryPath + ". Download the SDK from vercidium.com and copy it into " + sdkPath + " (see the README)");
		}

		string binariesPath = Path.Combine("$(PluginDir)", "Binaries", "ThirdParty", "vaudio", platform);
		PublicDefinitions.Add("VA_LIBRARY_NAME=TEXT(\"" + library + "\")");
		PublicDefinitions.Add("VA_LIBRARY_PLATFORM=TEXT(\"" + platform + "\")");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(libPath, "vaudionative.lib"));
			PublicDelayLoadDLLs.Add(library);
		}
		else
		{
			PublicAdditionalLibraries.Add(libraryPath);
		}

		RuntimeDependencies.Add(Path.Combine(binariesPath, library), libraryPath);

		// The dev SDK ships the debug window, which it looks for beside the shared library. The production SDK doesn't have it
		string[] debugWindowFiles = Target.Platform == UnrealTargetPlatform.Win64 ? new string[] { "vaudio-debug-window.exe", "glfw3.dll" }
			: Target.Platform == UnrealTargetPlatform.Linux ? new string[] { "vaudio-debug-window", "libglfw.so.3" }
			: new string[] { "vaudio-debug-window", "libglfw.3.dylib" };

		foreach (string file in debugWindowFiles)
		{
			string source = Path.Combine(libPath, file);

			if (File.Exists(source))
			{
				RuntimeDependencies.Add(Path.Combine(binariesPath, file), source);
			}
		}
	}
}
