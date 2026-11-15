#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "VAMaterialComponent.h"
#include "VAMeshMaterialUserData.generated.h"

class AVAWorld;
class UStaticMesh;
class UVAMaterialBase;

// Added to a Static Mesh asset's Asset User Data, so every actor using the mesh becomes raytracing geometry without its own VAMaterialComponent. A VAMaterialComponent on the actor overrides it
UCLASS(BlueprintType, DisplayName = "VAMeshMaterial")
class VAUDIOUNREAL_API UVAMeshMaterialUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	// Optional - if set, overrides Material below with a UVADefaultMaterial or UVACustomMaterial from the VAWorld's Materials array.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	UVAMaterialBase* MaterialAsset = nullptr;

	// Used when MaterialAsset above is unset (the common case) - one of the 23 built-in materials. Ignored while MaterialAsset is set. No EditCondition here, as the Static Mesh editor can't evaluate one on an Asset User Data entry and hides the property
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	EVAMaterial Material = EVAMaterial::Concrete;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bUseFlatTransmission = false;

	// Whether the mesh's simple collision or render triangles become raytracing geometry
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	EVAPropagateMode PropagateMode = EVAPropagateMode::All;

	// Which LOD of the mesh is raytraced. Lower detail LODs are cheaper to raytrace, but should stay closed (watertight) so transmission is calculated correctly. Clamped to the mesh's lowest-detail LOD
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (ClampMin = "0"))
	int32 MeshLOD = 0;

	// The SDK material ID in world. Returns false (and warns as reporter) if MaterialAsset isn't in world's Materials array
	bool GetMaterialId(const AVAWorld* world, const UObject* reporter, int32& outMaterialId) const;

	// The mesh's VAMeshMaterial, or null if it has none
	static UVAMeshMaterialUserData* Find(const UStaticMesh* mesh);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
