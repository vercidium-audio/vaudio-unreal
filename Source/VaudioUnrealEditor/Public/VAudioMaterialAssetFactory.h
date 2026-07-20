#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "VAudioMaterialAssetFactory.generated.h"

// Lets UVAudioMaterialAsset (overrides a built-in material) be created via the Content
// Browser's "Create Asset" (right-click) menu, and gives array/object property pickers a
// "create new asset" option for it.
UCLASS()
class UVAudioMaterialAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVAudioMaterialAssetFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

	// Without this, the Content Browser's "Create Asset" menu falls back to the generic
	// "Data Asset" label instead of SupportedClass's own UCLASS(DisplayName = ...).
	virtual FText GetDisplayName() const override;
};

// Same as UVAudioMaterialAssetFactory, but for UVAudioCustomMaterialAsset (a brand new
// material with an SDK-assigned ID).
UCLASS()
class UVAudioCustomMaterialAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVAudioCustomMaterialAssetFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
};
