#include "VAMaterialFactory.h"
#include "VAMaterial.h"

UVADefaultMaterialFactory::UVADefaultMaterialFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UVADefaultMaterial::StaticClass();
}

UObject* UVADefaultMaterialFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UVADefaultMaterial>(InParent, Class, Name, Flags);
}

FText UVADefaultMaterialFactory::GetDisplayName() const
{
	return SupportedClass->GetDisplayNameText();
}

UVACustomMaterialFactory::UVACustomMaterialFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UVACustomMaterial::StaticClass();
}

UObject* UVACustomMaterialFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UVACustomMaterial>(InParent, Class, Name, Flags);
}

FText UVACustomMaterialFactory::GetDisplayName() const
{
	return SupportedClass->GetDisplayNameText();
}
