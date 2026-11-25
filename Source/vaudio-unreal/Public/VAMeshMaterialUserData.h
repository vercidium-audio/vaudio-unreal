// Human-created

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "VAMaterialComponent.h"
#include "VAMeshMaterialUserData.generated.h"

class AVAWorld;
class UStaticMesh;
class UVAMaterialBase;

// Add to a Static Mesh asset, so every actor using it will affect raytracing without needing its own VAMaterialComponent.
// A VAMaterialComponent on the actor overrides this user data
UCLASS(BlueprintType, DisplayName = "VAMeshMaterial")
class VAUDIOUNREAL_API UVAMeshMaterialUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	// Optional - overrides Material below with a UVADefaultMaterial or UVACustomMaterial asset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	UVAMaterialBase* MaterialAsset = nullptr;

	// Used when MaterialAsset above is unset (the common case).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	EVAMaterial Material = EVAMaterial::Concrete;

	// Whether permeation rays should lose a flat percentage of energy when touching this primitive, instead of calculating the distance spent inside it
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bUseFlatTransmission = false;

	// Whether simple collision or render triangles are added to the raytracing simulation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	EVAPropagateMode PropagateMode = EVAPropagateMode::All;

	// Which LOD of the mesh is raytraced
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (ClampMin = "0"))
	int32 MeshLOD = 0;

	// The SDK material ID in the world. Returns false and warns if it isn't in the world's Materials array
	bool GetMaterialId(const AVAWorld* world, const UObject* reporter, int32& outMaterialId) const;

	// Get the mesh's VAMeshMaterial, or null if it has none
	static UVAMeshMaterialUserData* Find(const UStaticMesh* mesh);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
