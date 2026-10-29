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

	// Colour of primitives with this material in the debug window (dev SDK only)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material")
	FColor Color = FColor::White;

	// The SDK material ID this asset applies to in Owner. Custom materials only have one once Owner has applied them. Returns false (and logs why) otherwise
	virtual bool GetMaterialId(const AVAWorld* Owner, int32& OutMaterialId) const PURE_VIRTUAL(UVAMaterialBase::GetMaterialId, return false;);

	// The name used in log messages
	virtual FString GetMaterialName() const { return GetName(); }

	// Pushes every property to Owner's SDK world
	void ApplyToWorld(AVAWorld* Owner) const;

#if WITH_EDITOR
	// Applies the edit to every running VAWorld whose Materials array contains this asset
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	// Reads the SDK's built-in values for MaterialId into our properties
	void LoadDefaultsFromSDK(int32 MaterialId);
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

	virtual bool GetMaterialId(const AVAWorld* Owner, int32& OutMaterialId) const override;

	// Reads defaults from the SDK for MaterialType and applies them to this asset's properties.
	// Call this from the editor to reset to built-in defaults.
	UFUNCTION(CallInEditor, Category = "Vercidium Audio")
	void ResetToDefaults();

	// Puts MaterialType's SDK built-in values back in Owner's SDK world, when this asset is removed from its Materials array
	void RestoreWorldDefaults(AVAWorld* Owner) const;

#if WITH_EDITOR
	// Changing MaterialType resets the other properties to that material's SDK defaults, matching the Godot plugin's VADefaultMaterial.
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

// A new material. Each VAWorld that lists it assigns it its own SDK ID (1000 and up) when it begins play, so one asset can be shared by several levels
UCLASS(BlueprintType, DisplayName = "VACustomMaterial")
class VAUDIOUNREAL_API UVACustomMaterial : public UVAMaterialBase
{
	GENERATED_BODY()

public:
	// Shown in log messages. Empty uses the asset name
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio|Material")
	FString MaterialName;

	virtual bool GetMaterialId(const AVAWorld* Owner, int32& OutMaterialId) const override;
	virtual FString GetMaterialName() const override { return MaterialName.IsEmpty() ? GetName() : MaterialName; }
};
