#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VAMaterialComponent.h"
#include "VAMaterial.generated.h"

struct VAWorld;
class AVAWorld;

UCLASS(Abstract, BlueprintType)
class VAUDIOUNREAL_API UVAMaterialBase : public UDataAsset
{
	GENERATED_BODY()

public:
	// Percentage of low-frequency energy lost when a ray bounces
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float AbsorptionLF = 0.02f;

	// Percentage of high-frequency energy lost when a ray bounces
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float AbsorptionHF = 0.1f;

	// Scattering strength (0.0 = mirror, 1.0 = skews up to 90 degrees)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float Scattering = 0.1f;

	// How many meters a ray must travel through a primitive before it loses all low-frequency energy
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.01", Delta = "0.1"))
	float TransmissionLF = 10.0f;

	// How many meters a ray must travel through a primitive before it loses all high-frequency energy
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.01", Delta = "0.1"))
	float TransmissionHF = 5;

	// Percentage of low-frequency energy lost when a ray touches a Plane, Disk, Triangle, Line, non-watertight Mesh, non-enclosed Polygon or open Path primitive, instead of calculating how long the ray spent inside it
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float FlatTransmissionLF = 0.1f;

	// Percentage of high-frequency energy lost when a ray touches a Plane, Disk, Triangle, Line, non-watertight Mesh, non-enclosed Polygon or open Path primitive, instead of calculating how long the ray spent inside it
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material", meta = (ClampMin = "0.0", ClampMax = "1.0", Delta = "0.01"))
	float FlatTransmissionHF = 0.25f;

	// Returns the SDK material ID this asset applies to (built-in or custom, see subclasses).
	// Returns false (logs why) if the ID can't be resolved.
	virtual bool GetMaterialId(AVAWorld* Owner, int32& OutMaterialId) PURE_VIRTUAL(UVAMaterialBase::GetMaterialId, return false;);

	void ApplyToWorld(AVAWorld* Owner);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	// Finds the running world (if any) whose Materials array contains this asset, via
	// AVAWorld::RunningWorlds. Null if this asset isn't assigned to any running world.
	AVAWorld* FindOwningWorldActor();

	// Reads the current SDK defaults for MaterialId into our properties.
	void LoadDefaultsFromSDK(VAWorld* World, int32 MaterialId);
};

// Overrides one of the 23 built-in materials (e.g. "Concrete", "Metal") - pick from
// MaterialType's dropdown, then override individual properties as needed.
UCLASS(BlueprintType, DisplayName = "VADefaultMaterial")
class VAUDIOUNREAL_API UVADefaultMaterial : public UVAMaterialBase
{
	GENERATED_BODY()

public:
	// Which built-in material this asset overrides.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material")
	EVAMaterial MaterialType = EVAMaterial::Concrete;

	virtual bool GetMaterialId(AVAWorld* Owner, int32& OutMaterialId) override;

	// Reads defaults from the SDK for MaterialType and applies them to this asset's properties.
	// Call this from the editor to reset to built-in defaults.
	UFUNCTION(CallInEditor, Category = "Vercidium Audio")
	void ResetToDefaults();

#if WITH_EDITOR
	// Changing MaterialType resets the other properties to that material's SDK defaults, matching the Godot plugin's VADefaultMaterial.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

UCLASS(BlueprintType, DisplayName = "VACustomMaterial")
class VAUDIOUNREAL_API UVACustomMaterial : public UVAMaterialBase
{
	GENERATED_BODY()

public:
	virtual bool GetMaterialId(AVAWorld* Owner, int32& OutMaterialId) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	// Custom material ID (>= 1000), lazily assigned by GetMaterialId() the first time this asset is applied. 0 means "not yet assigned".
	UPROPERTY()
	int32 CustomMaterialId = 0;
};
