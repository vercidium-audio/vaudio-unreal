#include "VaudioUnrealModule.h"
#include "VAEngineCompat.h"
#include "VALog.h"

#include "Engine/Engine.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#include "vaudio.h"

#if !PLATFORM_WINDOWS
#include <dlfcn.h>
#endif

static bool GSdkLoaded = false;

void FVaudioUnrealModule::StartupModule()
{
	VA_LOG(TEXT("Startup"));

	LoadSdk();

	// GEngine is not guaranteed to be valid yet at module startup, so defer until the engine has finished initialising
	PostEngineInitHandle = VAOnPostEngineInit().AddRaw(this, &FVaudioUnrealModule::OnPostEngineInit);
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

#if PLATFORM_WINDOWS
	// Only a build stages the library into Binaries, so a Blueprint-only project using the prebuilt plugin loads it from where the user copied it
	if (!FPaths::FileExists(libraryPath))
	{
		libraryPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(plugin->GetBaseDir(), TEXT("Source"), TEXT("ThirdParty"), TEXT("vaudio"), TEXT("lib"), VA_LIBRARY_PLATFORM, VA_LIBRARY_NAME));
	}

	if (!FPaths::FileExists(libraryPath))
	{
		VA_ERROR(TEXT("The Vercidium Audio SDK library is missing from '%s'. Copy the SDK into the plugin's Source/ThirdParty/vaudio folder (see the README)."), *libraryPath);
		return;
	}

	SdkHandle = FPlatformProcess::GetDllHandle(*libraryPath);

	if (!SdkHandle)
	{
		VA_ERROR(TEXT("Failed to load the Vercidium Audio SDK from '%s'."), *libraryPath);
		return;
	}
#else
	// The module links the library directly, so the dynamic linker has already loaded it via rpath. Loading the staged copy by path as well would map a second, unused instance
	Dl_info info;

	if (dladdr(reinterpret_cast<void*>(&vaGetVersion), &info) && info.dli_fname)
	{
		libraryPath = FPaths::ConvertRelativePathToFull(UTF8_TO_TCHAR(info.dli_fname));
	}
#endif

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

	// The single precision SDK's library has the same name, and every position, size and transform has a different layout in it
	int floatSize = vaGetFloatSize();

	if (floatSize != VA_FLOAT_SIZE)
	{
		VA_ERROR(TEXT("The Vercidium Audio SDK library at '%s' is the single precision build, but the plugin needs the double precision one (3d/native-double). Copy its header and libraries into the plugin's Source/ThirdParty/vaudio folder."), *libraryPath);
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

	VAOnPostEngineInit().Remove(PostEngineInitHandle);

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
