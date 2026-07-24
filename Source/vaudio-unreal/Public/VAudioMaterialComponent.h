#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VAudioMaterialComponent.generated.h"

class AVAudioWorld;
class UVAudioMaterialAssetBase;

UENUM(BlueprintType)
enum class EVAudioMaterial : uint8
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

// Add this component to any actor whose static mesh(es) should participate in
// acoustic raytracing. AVAudioWorld scans for this component on BeginPlay and
// submits the actor's collision/mesh geometry to the VA raytracing world.
UCLASS(ClassGroup = ("Vercidium Audio"), meta = (BlueprintSpawnableComponent), DisplayName = "VA Audio Material")
class VAUDIOUNREAL_API UVAudioMaterialComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVAudioMaterialComponent();

	// Which VA Audio World this actor (and its attached children) should be added to.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	AVAudioWorld* AudioWorld = nullptr;

	// Optional - if set, overrides Material below with a UVAudioDefaultMaterialAsset or UVAudioCustomMaterialAsset from AudioWorld's Materials array.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	UVAudioMaterialAssetBase* MaterialAsset = nullptr;

	// Used when MaterialAsset above is unset (the common case) - one of the 23 built-in materials.
	// Hidden while MaterialAsset is set, since it would otherwise be ignored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (EditCondition = "MaterialAsset == nullptr", EditConditionHides))
	EVAudioMaterial Material = EVAudioMaterial::Concrete;

	// Whether sound rays can permeate (pass through) this surface. Disable for solid opaque surfaces like glass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bSupports3DPermeation = true;

	// Resolves this component's effective SDK material ID: MaterialAsset if set, otherwise the
	// built-in Material enum. Returns false (logs why) if resolution fails - e.g. MaterialAsset
	// isn't in AudioWorld's Materials array.
	bool GetMaterialId(int32& OutMaterialId);

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void OnRegister() override;

private:
	// Ensures sibling static meshes keep a CPU-readable copy of their vertex/index buffers
	// in cooked builds, since AVAudioWorld's triangle-mesh fallback reads them at runtime.
	void EnsureMeshesAllowCPUAccess() const;
#endif
};
