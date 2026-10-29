#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "VAMaterialFactory.generated.h"

UCLASS()
class UVADefaultMaterialFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVADefaultMaterialFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

	// Without this, the Content Browser's "Create Asset" menu falls back to the generic
	// "Data Asset" label instead of SupportedClass's own UCLASS(DisplayName = ...).
	virtual FText GetDisplayName() const override;
};

// Same as UVADefaultMaterialFactory, but for UVACustomMaterial (a brand new material with an SDK-assigned ID).
UCLASS()
class UVACustomMaterialFactory : public UFactory
{
	GENERATED_BODY()

public:
	UVACustomMaterialFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual FText GetDisplayName() const override;
};
