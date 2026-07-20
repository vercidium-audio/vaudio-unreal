#include "VAudioMaterialAssetFactory.h"
#include "VAudioMaterial.h"

UVAudioMaterialAssetFactory::UVAudioMaterialAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UVAudioMaterialAsset::StaticClass();
}

UObject* UVAudioMaterialAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UVAudioMaterialAsset>(InParent, Class, Name, Flags);
}

FText UVAudioMaterialAssetFactory::GetDisplayName() const
{
	return SupportedClass->GetDisplayNameText();
}

UVAudioCustomMaterialAssetFactory::UVAudioCustomMaterialAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UVAudioCustomMaterialAsset::StaticClass();
}

UObject* UVAudioCustomMaterialAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UVAudioCustomMaterialAsset>(InParent, Class, Name, Flags);
}

FText UVAudioCustomMaterialAssetFactory::GetDisplayName() const
{
	return SupportedClass->GetDisplayNameText();
}
