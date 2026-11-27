// AI-created

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "VAMaterialTypes.h"
#include "VAMeshMaterialUserData.generated.h"

class AVAWorld;
class UStaticMesh;
class UVAMaterialBase;

// The material of one of the Static Mesh's material slots, instead of the VAMeshMaterial's Material
USTRUCT(BlueprintType)
struct VAUDIOUNREAL_API FVAMeshMaterialSlot
{
	GENERATED_BODY()

	// The Static Mesh's material slot, by its Material Slot Name
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (GetOptions = "GetMaterialSlotNames"))
	FName SlotName;

	// Leaves this slot's triangles out of raytracing, e.g. decals or foliage cards
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bIgnore = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (ShowOnlyInnerProperties, EditCondition = "!bIgnore"))
	FVAMaterialChoice Material;
};

// Add to a Static Mesh asset, so every actor using it affects raytracing. Meshes without one are only raytraced where a material slot's render material or physical material is matched by the VAWorld (RenderMaterialMap, PhysicalMaterialMap or bMatchMaterialNames)
UCLASS(BlueprintType, DisplayName = "VAMeshMaterial")
class VAUDIOUNREAL_API UVAMeshMaterialUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	// Used by simple collision, and by material slots whose render material and physical material aren't matched by the VAWorld
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (ShowOnlyInnerProperties))
	FVAMaterialChoice Material;

	// Materials for individual material slots of the mesh's render triangles, e.g. Glass for a building's window slot. These beat the VAWorld's matching of the slot's render material. Simple collision has no slots, so it always uses Material
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (TitleProperty = "SlotName"))
	TArray<FVAMeshMaterialSlot> SlotMaterials;

	// Whether permeation rays should lose a flat percentage of energy when touching this primitive, instead of calculating the distance spent inside it. Flat transmission uses the material of the triangle a ray touches
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bUseFlatTransmission = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (InlineEditConditionToggle))
	bool bOverridePermeationMaterial = false;

	// The material inside the mesh, which permeation rays lose energy to based on the distance they spend inside it. Uses Material when unset. Not used with flat transmission
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (EditCondition = "bOverridePermeationMaterial"))
	FVAMaterialChoice PermeationMaterial;

	// Whether simple collision or render triangles are added to the raytracing simulation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	EVAPropagateMode PropagateMode = EVAPropagateMode::All;

	// Which LOD of the mesh is raytraced
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (ClampMin = "0"))
	int32 MeshLOD = 0;

	// The SDK material ID in the world. Returns false and warns if it isn't in the world's Materials array
	bool GetMaterialID(const AVAWorld* world, const UObject* reporter, int32& outMaterialId) const;

	// The SDK material ID of the slot's SlotMaterials entry (INDEX_NONE if it's ignored), with outFound false if the slot has none. Returns false and warns if its material isn't in the world's Materials array
	bool FindSlotMaterialID(const AVAWorld* world, const UObject* reporter, FName slotName, bool& outFound, int32& outMaterialID) const;

	// The SDK material ID of PermeationMaterial, or 0 (Air, i.e. use the primitive's material) when it isn't overridden. Returns false and warns if it isn't in the world's Materials array
	bool GetPermeationMaterialID(const AVAWorld* world, const UObject* reporter, int32& outMaterialId) const;

	// The mesh's material slot names, for the SlotName dropdown
	UFUNCTION()
	TArray<FString> GetMaterialSlotNames() const;

	// Get the mesh's VAMeshMaterial, or null if it has none
	static UVAMeshMaterialUserData* Find(const UStaticMesh* mesh);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	// Refresh the mesh's components in running worlds when a VAMeshMaterial is added or removed
	static void OnObjectPropertyChanged(UObject* object, FPropertyChangedEvent& event);

private:
	static void RefreshRunningWorlds(const UStaticMesh* mesh);
#endif

private:
	// Appended to warnings, naming the mesh
	FString GetContext() const;
};
