#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "VAudioMaterialAssetFactory.generated.h"

UCLASS()
class UVAudioDefaultMaterialAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVAudioDefaultMaterialAssetFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

	// Without this, the Content Browser's "Create Asset" menu falls back to the generic
	// "Data Asset" label instead of SupportedClass's own UCLASS(DisplayName = ...).
	virtual FText GetDisplayName() const override;
};

// Same as UVAudioDefaultMaterialAssetFactory, but for UVAudioCustomMaterialAsset (a brand new material with an SDK-assigned ID).
UCLASS()
class UVAudioCustomMaterialAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVAudioCustomMaterialAssetFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
};
