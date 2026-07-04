#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VAudioMaterialComponent.generated.h"

class AVAudioWorld;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	EVAudioMaterial Material = EVAudioMaterial::Concrete;

	// Whether sound rays can permeate (pass through) this surface. Disable for solid opaque surfaces like glass.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	bool bSupports3DPermeation = true;
};
