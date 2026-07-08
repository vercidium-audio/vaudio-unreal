#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VAudioMaterialComponent.h"
#include "VAudioMaterial.generated.h"

struct VAWorld;

// Place this actor as a child of AVAudioWorld in the outliner.
// Name it to match a built-in material (e.g. "Concrete", "Metal") to inherit
// that material's defaults, then override individual properties as needed.
// Changes to properties during PIE are applied immediately.
UCLASS(DisplayName = "VA Audio Material")
class VAUDIOUNREAL_API AVAudioMaterial : public AActor
{
	GENERATED_BODY()

public:
	AVAudioMaterial();

	// Percentage of low-frequency energy lost when a ray bounces (0.0 to 1.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AbsorptionLF = 0.02f;

	// Percentage of high-frequency energy lost when a ray bounces (0.0 to 1.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AbsorptionHF = 0.1f;

	// Scattering strength (0.0 = mirror, 1.0 = skews up to 90 degrees)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Scattering = 0.1f;

	// How many meters a ray must travel through a primitive before it loses all low-frequency energy (0.01 to max)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.01"))
	float TransmissionLF = 10.0f;

	// How many meters a ray must travel through a primitive before it loses all high-frequency energy (0.01 to max)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.01"))
	float TransmissionHF = 5;

	// Low-frequency energy lost when a permeation ray passes through a flat primitive (0.0 to 1.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlaneTransmissionLF = 0.1f;

	// High-frequency energy lost when a permeation ray passes through a flat primitive (0.0 to 1.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PlaneTransmissionHF = 0.25f;

	// Resolves the actor label to an EVAudioMaterial. Returns false if the name doesn't match any built-in.
	bool ResolveMaterialType(EVAudioMaterial& OutMaterial) const;

	// Reads defaults from the SDK for the resolved material type and applies them to this actor's properties.
	// Call this from the editor to reset to built-in defaults.
	UFUNCTION(CallInEditor, Category = "Vercidium Audio")
	void ResetToDefaults();

	// Pushes this actor's current property values into the VA world.
	void ApplyToWorld(VAWorld* World) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// Reads the current SDK defaults for MaterialType into our properties.
	void LoadDefaultsFromSDK(VAWorld* World, int32 MaterialId);

	VAWorld* GetOwningVAWorld() const;
};
