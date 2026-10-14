#include "VaudioUnrealEditorModule.h"
#include "PropertyEditorModule.h"
#include "VAListener.h"
#include "VAListenerDetails.h"
#include "VARaytracedSource.h"
#include "VARaytracedSourceDetails.h"

void FVaudioUnrealEditorModule::StartupModule()
{
	FPropertyEditorModule& propertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	propertyEditor.RegisterCustomClassLayout(AVAListener::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FVAListenerDetails::MakeInstance));
	propertyEditor.RegisterCustomClassLayout(AVARaytracedSource::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FVARaytracedSourceDetails::MakeInstance));
}

void FVaudioUnrealEditorModule::ShutdownModule()
{
	if (FPropertyEditorModule* propertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		propertyEditor->UnregisterCustomClassLayout(AVAListener::StaticClass()->GetFName());
		propertyEditor->UnregisterCustomClassLayout(AVARaytracedSource::StaticClass()->GetFName());
	}
}

IMPLEMENT_MODULE(FVaudioUnrealEditorModule, VaudioUnrealEditor)
