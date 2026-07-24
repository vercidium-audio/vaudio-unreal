#include "VAudioMaterialAssetFactory.h"
#include "VAudioMaterial.h"

UVAudioDefaultMaterialAssetFactory::UVAudioDefaultMaterialAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UVAudioDefaultMaterialAsset::StaticClass();
}

UObject* UVAudioDefaultMaterialAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UVAudioDefaultMaterialAsset>(InParent, Class, Name, Flags);
}

FText UVAudioDefaultMaterialAssetFactory::GetDisplayName() const
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
