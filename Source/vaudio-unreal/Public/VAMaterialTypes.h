// AI-created

#pragma once

#include "CoreMinimal.h"
#include "VAMaterialTypes.generated.h"

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

// A built-in material, or a material asset from the VAWorld's Materials array
USTRUCT(BlueprintType)
struct VAUDIOUNREAL_API FVAMaterialChoice
{
	GENERATED_BODY()

	// Optional - overrides Material below with a UVADefaultMaterial or UVACustomMaterial from the VAWorld's Materials array
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio")
	UVAMaterialBase* MaterialAsset = nullptr;

	// One of the built-in materials, used when MaterialAsset is unset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vercidium Audio", meta = (EditCondition = "MaterialAsset == nullptr", EditConditionHides))
	EVAMaterial Material = EVAMaterial::Concrete;

	FVAMaterialChoice() = default;
	FVAMaterialChoice(EVAMaterial material) : Material(material) {}

	// The SDK material ID in world. Returns false and warns (on reporter, with context appended) if MaterialAsset isn't in its Materials array
	bool GetMaterialID(const AVAWorld* world, const UObject* reporter, const FString& context, int32& outMaterialID) const;
};

// A material that asset names are matched against: its name split into words, and its SDK ID
struct FVAMaterialNameCandidate
{
	TArray<FString> Words;
	int32 MaterialID = 0;
};

// Splits a name into lowercase words on '_', '-', spaces, other punctuation, CamelCase and digits, e.g. "MI_ConcretePolished01" is mi, concrete, polished, 01
VAUDIOUNREAL_API TArray<FString> VASplitMaterialName(const FString& name);

// The candidate whose words appear in a row in name's words, preferring the longest. Returns false if none match
VAUDIOUNREAL_API bool VAMatchMaterialName(const FString& name, const TArray<FVAMaterialNameCandidate>& candidates, int32& outMaterialID);

// Every built-in material, matched by its enum name (e.g. ConcretePolished is concrete, polished)
VAUDIOUNREAL_API const TArray<FVAMaterialNameCandidate>& VABuiltInMaterialNames();
