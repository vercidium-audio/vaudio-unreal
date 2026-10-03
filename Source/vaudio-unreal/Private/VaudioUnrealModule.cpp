#include "VaudioUnrealModule.h"
#include "VALog.h"

#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"

void FVaudioUnrealModule::StartupModule()
{
	VA_LOG(TEXT("Startup"));

	// GEngine is not guaranteed to be valid yet at module startup, so defer until the engine has finished initialising
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FVaudioUnrealModule::OnPostEngineInit);
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
}

IMPLEMENT_MODULE(FVaudioUnrealModule, VaudioUnreal)
