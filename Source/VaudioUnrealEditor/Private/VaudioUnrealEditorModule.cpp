#include "VaudioUnrealEditorModule.h"
#include "PropertyEditorModule.h"
#include "VAListener.h"
#include "VAListenerDetails.h"

void FVaudioUnrealEditorModule::StartupModule()
{
	FPropertyEditorModule& propertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	propertyEditor.RegisterCustomClassLayout(AVAListener::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FVAListenerDetails::MakeInstance));
}

void FVaudioUnrealEditorModule::ShutdownModule()
{
	if (FPropertyEditorModule* propertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
		propertyEditor->UnregisterCustomClassLayout(AVAListener::StaticClass()->GetFName());
}

IMPLEMENT_MODULE(FVaudioUnrealEditorModule, VaudioUnrealEditor)
