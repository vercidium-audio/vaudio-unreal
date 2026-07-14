#include "VAudioMaterial.h"
#include "VAudioWorld.h"
#include "Components/SceneComponent.h"

extern "C" {
#include "vaudio.h"
}

#include "VaRawLog.h"

// Maps actor label strings to EVAudioMaterial enum values.
static bool LabelToMaterialEnum(const FString& Label, EVAudioMaterial& Out)
{
	static const TMap<FString, EVAudioMaterial> Map = {
		{ TEXT("Brick"),              EVAudioMaterial::Brick            },
		{ TEXT("Cloth"),              EVAudioMaterial::Cloth            },
		{ TEXT("Concrete"),           EVAudioMaterial::Concrete         },
		{ TEXT("ConcretePolished"),   EVAudioMaterial::ConcretePolished },
		{ TEXT("Concrete Polished"),  EVAudioMaterial::ConcretePolished },
		{ TEXT("Dirt"),               EVAudioMaterial::Dirt             },
		{ TEXT("Glass"),              EVAudioMaterial::Glass            },
		{ TEXT("Grass"),              EVAudioMaterial::Grass            },
		{ TEXT("Gravel"),             EVAudioMaterial::Gravel           },
		{ TEXT("Gyprock"),            EVAudioMaterial::Gyprock          },
		{ TEXT("Ice"),                EVAudioMaterial::Ice              },
		{ TEXT("Leaf"),               EVAudioMaterial::Leaf             },
		{ TEXT("Marble"),             EVAudioMaterial::Marble           },
		{ TEXT("Metal"),              EVAudioMaterial::Metal            },
		{ TEXT("Mud"),                EVAudioMaterial::Mud              },
		{ TEXT("Rock"),               EVAudioMaterial::Rock             },
		{ TEXT("Sand"),               EVAudioMaterial::Sand             },
		{ TEXT("Snow"),               EVAudioMaterial::Snow             },
		{ TEXT("Tile"),               EVAudioMaterial::Tile             },
		{ TEXT("Tree"),               EVAudioMaterial::Tree             },
		{ TEXT("Water"),              EVAudioMaterial::Water            },
		{ TEXT("WoodIndoor"),         EVAudioMaterial::WoodIndoor       },
		{ TEXT("Wood Indoor"),        EVAudioMaterial::WoodIndoor       },
		{ TEXT("WoodOutdoor"),        EVAudioMaterial::WoodOutdoor      },
		{ TEXT("Wood Outdoor"),       EVAudioMaterial::WoodOutdoor      },
	};

	const EVAudioMaterial* Found = Map.Find(Label);
	if (Found)
	{
		Out = *Found;
		return true;
	}
	return false;
}

// TODO - this is defined in VAudioMaterial.cpp and VAudioWorld.cpp. Move it to one material helper file
static VAMaterialType EVAudioMaterialToVA(EVAudioMaterial M)
{
	switch (M)
	{
	case EVAudioMaterial::Brick:            return VAMaterialBrick;
	case EVAudioMaterial::Cloth:            return VAMaterialCloth;
	case EVAudioMaterial::Concrete:         return VAMaterialConcrete;
	case EVAudioMaterial::ConcretePolished: return VAMaterialConcretePolished;
	case EVAudioMaterial::Dirt:             return VAMaterialDirt;
	case EVAudioMaterial::Glass:            return VAMaterialGlass;
	case EVAudioMaterial::Grass:            return VAMaterialGrass;
	case EVAudioMaterial::Gravel:           return VAMaterialGravel;
	case EVAudioMaterial::Gyprock:          return VAMaterialGyprock;
	case EVAudioMaterial::Ice:              return VAMaterialIce;
	case EVAudioMaterial::Leaf:             return VAMaterialLeaf;
	case EVAudioMaterial::Marble:           return VAMaterialMarble;
	case EVAudioMaterial::Metal:            return VAMaterialMetal;
	case EVAudioMaterial::Mud:              return VAMaterialMud;
	case EVAudioMaterial::Rock:             return VAMaterialRock;
	case EVAudioMaterial::Sand:             return VAMaterialSand;
	case EVAudioMaterial::Snow:             return VAMaterialSnow;
	case EVAudioMaterial::Tile:             return VAMaterialTile;
	case EVAudioMaterial::Tree:             return VAMaterialTree;
	case EVAudioMaterial::Water:            return VAMaterialWater;
	case EVAudioMaterial::WoodIndoor:       return VAMaterialWoodIndoor;
	case EVAudioMaterial::WoodOutdoor:      return VAMaterialWoodOutdoor;
	default:                                return VAMaterialConcrete;
	}
}

AVAudioMaterial::AVAudioMaterial()
{
	PrimaryActorTick.bCanEverTick = false;

	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool AVAudioMaterial::ResolveMaterialType(EVAudioMaterial& OutMaterial) const
{
	return LabelToMaterialEnum(MaterialName, OutMaterial);
}

VAWorld* AVAudioMaterial::GetOwningVAWorld() const
{
	AActor* Parent = GetAttachParentActor();

	// Null if this material actor isn't attached to anything - materials only take effect
	// when attached (directly or via a chain) to a VAudioWorld actor.
	if (!Parent)
	{
		VALog(L"not attached to a parent actor - attach this to a VAudioWorld (or an actor attached to one) for the material to take effect.");
		return nullptr;
	}

	AVAudioWorld* AudioWorld = Cast<AVAudioWorld>(Parent);

	// Null if the attach parent isn't a VAudioWorld (e.g. attached to an unrelated actor).
	if (!AudioWorld)
	{
		VALog(L"attach parent '%s' is not a VAudioWorld.", *Parent->GetName());
		return nullptr;
	}

	// May still be null if the VAudioWorld's own BeginPlay hasn't run yet (actor BeginPlay
	// order isn't guaranteed) - callers already handle a null return for that case.
	return AudioWorld->GetVAWorld();
}

void AVAudioMaterial::LoadDefaultsFromSDK(VAWorld* World, int32 MaterialId)
{
	AbsorptionLF        = vaWorldGetMaterialAbsorptionLF(World, MaterialId);
	AbsorptionHF        = vaWorldGetMaterialAbsorptionHF(World, MaterialId);
	Scattering          = vaWorldGetMaterialScattering(World, MaterialId);
	TransmissionLF      = vaWorldGetMaterialTransmissionLF(World, MaterialId);
	TransmissionHF      = vaWorldGetMaterialTransmissionHF(World, MaterialId);
	PlaneTransmissionLF = vaWorldGetMaterialPlaneTransmissionLF(World, MaterialId);
	PlaneTransmissionHF = vaWorldGetMaterialPlaneTransmissionHF(World, MaterialId);
}

void AVAudioMaterial::ApplyToWorld(VAWorld* World) const
{
	EVAudioMaterial MatEnum;

	if (!ResolveMaterialType(MatEnum))
	{
		VALog(L"MaterialName '%s' doesn't match any built-in material name - no changes applied.", *MaterialName);
		return;
	}

	int32 MaterialId = (int32)EVAudioMaterialToVA(MatEnum);
	vaWorldSetMaterialAbsorptionLF(World,        MaterialId, AbsorptionLF);
	vaWorldSetMaterialAbsorptionHF(World,        MaterialId, AbsorptionHF);
	vaWorldSetMaterialScattering(World,          MaterialId, Scattering);
	vaWorldSetMaterialTransmissionLF(World,      MaterialId, TransmissionLF);
	vaWorldSetMaterialTransmissionHF(World,      MaterialId, TransmissionHF);
	vaWorldSetMaterialPlaneTransmissionLF(World, MaterialId, PlaneTransmissionLF);
	vaWorldSetMaterialPlaneTransmissionHF(World, MaterialId, PlaneTransmissionHF);

	VALog(L"applied custom material (id=%d) TransmissionLF=%.3f TransmissionHF=%.3f", MaterialId, TransmissionLF, TransmissionHF);
}

void AVAudioMaterial::ResetToDefaults()
{
	VAWorld* World = GetOwningVAWorld();

	if (!World)
	{
		// TODO - can we raise an official warning somewhere? Show it in the editor?
		VALog(L"not attached to a VAudioWorld or world hasn't started - can't read defaults.");
		return;
	}

	EVAudioMaterial MatEnum;
	if (!ResolveMaterialType(MatEnum))
	{
		// TODO - can we raise an official warning somewhere? Show it in the editor?
		VALog(L"MaterialName '%s' doesn't match any built-in material name.", *MaterialName);
		return;
	}

	int32 MaterialId = (int32)EVAudioMaterialToVA(MatEnum);
	LoadDefaultsFromSDK(World, MaterialId);

#if WITH_EDITOR
	Modify();
#endif
}

#if WITH_EDITOR
void AVAudioMaterial::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	VAWorld* World = GetOwningVAWorld();

	// Null if not attached to a VAudioWorld (see GetOwningVAWorld(), which already logs the
	// reason) or that world's own BeginPlay hasn't run yet - nothing to apply the material to.
	if (!World)
		return;

	ApplyToWorld(World);
}
#endif
