#pragma once

#include "Modules/ModuleManager.h"

class FVaudioUnrealModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// False if the SDK's shared library failed to load at startup, in which case no va* function may be called
	static bool IsSdkLoaded();

private:
	void LoadSdk();
	void OnPostEngineInit();

	FDelegateHandle PostEngineInitHandle;
	void* SdkHandle = nullptr;
};
