#include "VaudioUnrealModule.h"
#include "VALog.h"

#include "Engine/Engine.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Paths.h"

#include "vaudio.h"

static bool GSdkLoaded = false;

void FVaudioUnrealModule::StartupModule()
{
	VA_LOG(TEXT("Startup"));

	LoadSdk();

	// GEngine is not guaranteed to be valid yet at module startup, so defer until the engine has finished initialising
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FVaudioUnrealModule::OnPostEngineInit);
}

void FVaudioUnrealModule::LoadSdk()
{
	TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin(TEXT("vaudio-unreal"));

	if (!plugin.IsValid())
	{
		VA_ERROR(TEXT("Failed to find the vaudio-unreal plugin, so the Vercidium Audio SDK can't be loaded."));
		return;
	}

	FString libraryPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(plugin->GetBaseDir(), TEXT("Binaries"), TEXT("ThirdParty"), TEXT("vaudio"), VA_LIBRARY_PLATFORM, VA_LIBRARY_NAME));

	if (!FPaths::FileExists(libraryPath))
	{
		VA_ERROR(TEXT("The Vercidium Audio SDK library is missing from '%s'. Rebuild the project so it's staged from the plugin's Source/ThirdParty/vaudio folder."), *libraryPath);
		return;
	}

	SdkHandle = FPlatformProcess::GetDllHandle(*libraryPath);

	if (!SdkHandle)
	{
		VA_ERROR(TEXT("Failed to load the Vercidium Audio SDK from '%s'."), *libraryPath);
		return;
	}

	int major = 0;
	int minor = 0;
	int patch = 0;
	vaGetVersion(&major, &minor, &patch);

	// The header the plugin was compiled against must match the library, otherwise struct layouts and functions may differ
	if (major != VA_VERSION_MAJOR || minor != VA_VERSION_MINOR)
	{
		VA_ERROR(TEXT("The Vercidium Audio SDK library is v%d.%d.%d, but the plugin was built against vaudio.h v%d.%d.%d. Copy the matching library into the plugin's Source/ThirdParty/vaudio folder."), major, minor, patch, VA_VERSION_MAJOR, VA_VERSION_MINOR, VA_VERSION_PATCH);
		return;
	}

	GSdkLoaded = true;
	VA_LOG(TEXT("Loaded Vercidium Audio SDK v%d.%d.%d from '%s'"), major, minor, patch, *libraryPath);
}

bool FVaudioUnrealModule::IsSdkLoaded()
{
	return GSdkLoaded;
}

void FVaudioUnrealModule::OnPostEngineInit()
{
	VA_LOG(TEXT("PostEngineInit - clearing on screen debug messages"));

	check(GEngine);
	GEngine->ClearOnScreenDebugMessages();
}

void FVaudioUnrealModule::ShutdownModule()
{
	VA_LOG(TEXT("Shutdown"));

	FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);

	if (GEngine)
	{
		VA_LOG(TEXT("Shutdown - clearing on screen debug messages"));
		GEngine->ClearOnScreenDebugMessages();
	}

	GSdkLoaded = false;

	if (SdkHandle)
	{
		FPlatformProcess::FreeDllHandle(SdkHandle);
		SdkHandle = nullptr;
	}
}

IMPLEMENT_MODULE(FVaudioUnrealModule, VaudioUnreal)
