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

UCLASS(ClassGroup = ("Vercidium Audio"), meta = (BlueprintSpawnableComponent), DisplayName = "VAMaterialComponent")
class VAUDIOUNREAL_API UVAMaterialComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVAMaterialComponent();

	// Which VAWorld this actor (and its attached children) should be added to.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	AVAWorld* AudioWorld = nullptr;

	// Optional - if set, overrides Material below with a UVADefaultMaterial or UVACustomMaterial from AudioWorld's Materials array.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	UVAMaterialBase* MaterialAsset = nullptr;

	// Used when MaterialAsset above is unset (the common case) - one of the 23 built-in materials.
	// Hidden while MaterialAsset is set, since it would otherwise be ignored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (EditCondition = "MaterialAsset == nullptr", EditConditionHides))
	EVAMaterial Material = EVAMaterial::Concrete;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bUseFlatTransmission = false;

	bool GetMaterialId(int32& OutMaterialId);

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual void OnRegister() override;

private:
	// Ensures sibling static meshes keep a CPU-readable copy of their vertex/index buffers
	// in cooked builds, since AVAWorld's triangle-mesh fallback reads them at runtime.
	void EnsureMeshesAllowCPUAccess() const;
#endif
};
