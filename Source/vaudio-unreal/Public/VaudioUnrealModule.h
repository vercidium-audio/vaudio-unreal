// HUman-created

#pragma once

#include "Modules/ModuleManager.h"

class FVaudioUnrealModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// False if the SDK's shared library failed to load at startup, in which case no va* function may be called
	static bool IsSdkLoaded();

	// Retry loading the SDK if it failed at startup, so a library copied in while the editor is running works without a restart
	static bool EnsureSdkLoaded();

private:
	static void LoadSdk();
	void OnPostEngineInit();

	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle ObjectPropertyChangedHandle;
};
