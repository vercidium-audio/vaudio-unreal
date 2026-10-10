#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VAMaterialComponent.generated.h"

class AVAWorld;
class UVAMaterialBase;

UENUM(BlueprintType)
enum class EVAMaterial : uint8
{
	Brick            UMETA(DisplayName = "Brick"),
	Cloth            UMETA(DisplayName = "Cloth"),
	Concrete         UMETA(DisplayName = "Concrete"),
	ConcretePolished UMETA(DisplayName = "Concrete (Polished)"),
	Dirt             UMETA(DisplayName = "Dirt"),
	Glass            UMETA(DisplayName = "Glass"),
	Grass            UMETA(DisplayName = "Grass"),
	Gravel           UMETA(DisplayName = "Gravel"),
	Gyprock          UMETA(DisplayName = "Gyprock"),
	Ice              UMETA(DisplayName = "Ice"),
	Leaf             UMETA(DisplayName = "Leaf"),
	Marble           UMETA(DisplayName = "Marble"),
	Metal            UMETA(DisplayName = "Metal"),
	Mud              UMETA(DisplayName = "Mud"),
	Rock             UMETA(DisplayName = "Rock"),
	Sand             UMETA(DisplayName = "Sand"),
	Snow             UMETA(DisplayName = "Snow"),
	Tile             UMETA(DisplayName = "Tile"),
	Tree             UMETA(DisplayName = "Tree"),
	Water            UMETA(DisplayName = "Water"),
	WoodIndoor       UMETA(DisplayName = "Wood (Indoor)"),
	WoodOutdoor      UMETA(DisplayName = "Wood (Outdoor)"),
};

// Which of an actor's components become raytracing geometry, matching the Godot vercidium_audio_propagate metadata
UENUM(BlueprintType)
enum class EVAPropagateMode : uint8
{
	// Shape components, plus each static mesh's simple collision (or its render triangles if it has none)
	All,

	// Shape components and static mesh simple collision only
	Colliders,

	// Static mesh render triangles only
	Visuals,
};

// Gives one of the actor's components a different material, e.g. a glass window mesh on a brick building
USTRUCT(BlueprintType)
struct FVAMaterialOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (GetOptions = "GetComponentNames"))
	FName Component;

	// Optional - if set, overrides Material below with a UVADefaultMaterial or UVACustomMaterial from the VAWorld's Materials array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	UVAMaterialBase* MaterialAsset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (EditCondition = "MaterialAsset == nullptr", EditConditionHides))
	EVAMaterial Material = EVAMaterial::Concrete;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bUseFlatTransmission = false;
};

UCLASS(ClassGroup = ("Vercidium Audio"), meta = (BlueprintSpawnableComponent), DisplayName = "VAMaterialComponent")
class VAUDIOUNREAL_API UVAMaterialComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVAMaterialComponent();

	// Optional - if set, overrides Material below with a UVADefaultMaterial or UVACustomMaterial from the VAWorld's Materials array.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	UVAMaterialBase* MaterialAsset = nullptr;

	// Used when MaterialAsset above is unset (the common case) - one of the 23 built-in materials.
	// Hidden while MaterialAsset is set, since it would otherwise be ignored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (EditCondition = "MaterialAsset == nullptr", EditConditionHides))
	EVAMaterial Material = EVAMaterial::Concrete;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bUseFlatTransmission = false;

	// Which components of this actor, and of attached child actors that inherit this material, become raytracing geometry
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	EVAPropagateMode PropagateMode = EVAPropagateMode::All;

	// Which LOD of each static mesh is raytraced. Lower detail LODs are cheaper to raytrace, but should stay closed (watertight) so transmission is calculated correctly. Clamped to the mesh's lowest-detail LOD
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (ClampMin = "0"))
	int32 MeshLOD = 0;

	// Materials for individual components of this actor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	TArray<FVAMaterialOverride> MaterialOverrides;

	bool GetMaterialId(int32& OutMaterialId);

	// The material for one component, taking MaterialOverrides into account. Returns false (and logs why) if the material can't be resolved
	bool GetMaterialFor(const UActorComponent* component, int32& outMaterialId, bool& outUseFlatTransmission);

	// The level's VAWorld, found automatically. Null until this component has joined it
	UFUNCTION(BlueprintPure, Category = "Vercidium Audio")
	AVAWorld* GetAudioWorld() const { return AudioWorld; }

	UFUNCTION()
	TArray<FString> GetComponentNames() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(Transient)
	TObjectPtr<AVAWorld> AudioWorld = nullptr;

	// Set while this component waits for the level's VAWorld to begin play
	FDelegateHandle WorldRegisteredHandle;

	void OnWorldRegistered();
	void StopWaitingForWorld();

	// Bound from BeginPlay to EndPlay. When the VAWorld ends play first, this component waits for the next VAWorld
	FDelegateHandle WorldUnregisteredHandle;

	void OnWorldUnregistered();

public:

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void OnRegister() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

private:
	// Ensures sibling static meshes keep a CPU-readable copy of their vertex/index buffers
	// in cooked builds, since AVAWorld's triangle-mesh fallback reads them at runtime.
	void EnsureMeshesAllowCPUAccess() const;
#endif
};
