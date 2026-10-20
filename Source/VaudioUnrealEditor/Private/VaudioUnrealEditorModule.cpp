#include "VaudioUnrealEditorModule.h"
#include "Editor/UnrealEdEngine.h"
#include "Misc/CoreDelegates.h"
#include "PropertyEditorModule.h"
#include "UnrealEdGlobals.h"
#include "VAAmbientSoundConversion.h"
#include "VAListener.h"
#include "VAListenerDetails.h"
#include "VARaytracedSource.h"
#include "VARaytracedSourceDetails.h"
#include "VAWorld.h"
#include "VAWorldBoundsVisualizer.h"
#include "VAWorldDetails.h"

void FVaudioUnrealEditorModule::StartupModule()
{
	FPropertyEditorModule& propertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	propertyEditor.RegisterCustomClassLayout(AVAListener::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FVAListenerDetails::MakeInstance));
	propertyEditor.RegisterCustomClassLayout(AVARaytracedSource::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FVARaytracedSourceDetails::MakeInstance));
	propertyEditor.RegisterCustomClassLayout(AVAWorld::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FVAWorldDetails::MakeInstance));

	VAAmbientSoundConversion::RegisterMenus();

	if (GUnrealEd)
		RegisterVisualizers();
	else
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FVaudioUnrealEditorModule::RegisterVisualizers);
}

void FVaudioUnrealEditorModule::RegisterVisualizers()
{
	if (GUnrealEd)
		GUnrealEd->RegisterComponentVisualizer(UVAWorldBoundsComponent::StaticClass()->GetFName(), MakeShareable(new FVAWorldBoundsVisualizer));
}

void FVaudioUnrealEditorModule::ShutdownModule()
{
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	if (GUnrealEd)
		GUnrealEd->UnregisterComponentVisualizer(UVAWorldBoundsComponent::StaticClass()->GetFName());

	VAAmbientSoundConversion::UnregisterMenus();

	if (FPropertyEditorModule* propertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		propertyEditor->UnregisterCustomClassLayout(AVAListener::StaticClass()->GetFName());
		propertyEditor->UnregisterCustomClassLayout(AVARaytracedSource::StaticClass()->GetFName());
		propertyEditor->UnregisterCustomClassLayout(AVAWorld::StaticClass()->GetFName());
	}
}

IMPLEMENT_MODULE(FVaudioUnrealEditorModule, VaudioUnrealEditor)
