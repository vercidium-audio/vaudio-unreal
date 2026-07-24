#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VAudioMaterialComponent.h"
#include "VAudioMaterial.generated.h"

struct VAWorld;
class AVAudioWorld;

// Shared base class for VA Audio Material assets, assigned to an AVAudioWorld's Materials array as an asset, not a level actor.
// 
// Changes to properties in the editor are applied immediately to any running world that references this asset.
// 
// Two kinds:
// - UVAudioDefaultMaterialAsset: overrides one of the 23 built-in materials
// - UVAudioCustomMaterialAsset: defines a brand new material
UCLASS(Abstract, BlueprintType)
class VAUDIOUNREAL_API UVAudioMaterialAssetBase : public UDataAsset
{
	GENERATED_BODY()

public:
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

	// Returns the SDK material ID this asset applies to (built-in or custom, see subclasses).
	// Returns false (logs why) if the ID can't be resolved.
	virtual bool GetMaterialId(AVAudioWorld* Owner, int32& OutMaterialId) PURE_VIRTUAL(UVAudioMaterialAssetBase::GetMaterialId, return false;);

	// Pushes this asset's current property values into Owner's VA world. Owner must be the
	// AVAudioWorld whose Materials array contains this asset (needed to resolve/assign the
	// material ID - see GetMaterialId()).
	void ApplyToWorld(AVAudioWorld* Owner);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	// Finds the running world (if any) whose Materials array contains this asset, via
	// AVAudioWorld::RunningWorlds. Null if this asset isn't assigned to any running world.
	AVAudioWorld* FindOwningWorldActor();

	// Reads the current SDK defaults for MaterialId into our properties.
	void LoadDefaultsFromSDK(VAWorld* World, int32 MaterialId);
};

// Overrides one of the 23 built-in materials (e.g. "Concrete", "Metal") - pick from
// MaterialType's dropdown, then override individual properties as needed.
UCLASS(BlueprintType, DisplayName = "VA Default Material")
class VAUDIOUNREAL_API UVAudioDefaultMaterialAsset : public UVAudioMaterialAssetBase
{
	GENERATED_BODY()

public:
	// Which built-in material this asset overrides.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material")
	EVAudioMaterial MaterialType = EVAudioMaterial::Concrete;

	virtual bool GetMaterialId(AVAudioWorld* Owner, int32& OutMaterialId) override;

	// Reads defaults from the SDK for MaterialType and applies them to this asset's properties.
	// Call this from the editor to reset to built-in defaults.
	UFUNCTION(CallInEditor, Category = "Vercidium Audio")
	void ResetToDefaults();
};

// Defines a brand new custom material (not one of the 23 built-ins), with an SDK-assigned ID
// (>= 1000, auto-assigned - unique among the other custom materials in the same AVAudioWorld's
// Materials array). Assign this asset to a UVAudioMaterialComponent's MaterialAsset field to use it on geometry.
UCLASS(BlueprintType, DisplayName = "VA Custom Material")
class VAUDIOUNREAL_API UVAudioCustomMaterialAsset : public UVAudioMaterialAssetBase
{
	GENERATED_BODY()

public:
	virtual bool GetMaterialId(AVAudioWorld* Owner, int32& OutMaterialId) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// Custom material ID (>= 1000), lazily assigned by GetMaterialId() the first time this asset is applied. 0 means "not yet assigned".
	UPROPERTY()
	int32 CustomMaterialId = 0;
};
