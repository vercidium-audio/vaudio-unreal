#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FVaudioUnrealEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// GUnrealEd doesn't exist yet when the module loads at engine startup
	void RegisterVisualizers();
};
