// Human-reviewed

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "VAMaterialTypes.h"
#include "VAMeshMaterialUserData.generated.h"

class AVAWorld;
class UStaticMesh;
class UVAMaterialBase;

// Add to a Static Mesh asset, so every actor using it affects raytracing. Meshes without one are ignored
UCLASS(BlueprintType, DisplayName = "VAMeshMaterial")
class VAUDIOUNREAL_API UVAMeshMaterialUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (ShowOnlyInnerProperties))
	FVAMaterialChoice Material;

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
	bool GetMaterialID(const AVAWorld* world, const UObject* reporter, int32& outMaterialId) const;

	// Get the mesh's VAMeshMaterial, or null if it has none
	static UVAMeshMaterialUserData* Find(const UStaticMesh* mesh);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// Refresh the mesh's components in running worlds when a VAMeshMaterial is added or removed
	static void OnObjectPropertyChanged(UObject* object, FPropertyChangedEvent& event);

private:
	static void RefreshRunningWorlds(const UStaticMesh* mesh);
#endif
};
