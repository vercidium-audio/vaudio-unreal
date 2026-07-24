#pragma once

#include "Modules/ModuleManager.h"

class FVaudioUnrealModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void OnPostEngineInit();

	FDelegateHandle PostEngineInitHandle;
};
