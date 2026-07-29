#include "VaudioUnrealModule.h"
#include "VARawLog.h"

#include "Engine/Engine.h"
#include "Misc/CoreDelegates.h"

void FVaudioUnrealModule::StartupModule()
{
	VARawLog(TEXT("[VA] Startup"));

	// GEngine is not guaranteed to be valid yet at module startup, so defer until the engine has finished initialising
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FVaudioUnrealModule::OnPostEngineInit);
}

void FVaudioUnrealModule::OnPostEngineInit()
{
	VARawLog(TEXT("[VA] PostEngineInit - clearing on screen debug messages"));

	check(GEngine);
	GEngine->ClearOnScreenDebugMessages();
}

void FVaudioUnrealModule::ShutdownModule()
{
	VARawLog(TEXT("[VA] Shutdown"));

	FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);

	if (GEngine)
	{
		VARawLog(TEXT("[VA] Shutdown - clearing on screen debug messages"));
		GEngine->ClearOnScreenDebugMessages();
	}
}

IMPLEMENT_MODULE(FVaudioUnrealModule, VaudioUnreal)
