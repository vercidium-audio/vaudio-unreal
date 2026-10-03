#include "VAMaterial.h"
#include "VAWorld.h"
#include "VAMaterialConversion.h"

#include "VALog.h"

void UVAMaterialBase::LoadDefaultsFromSDK(VAWorld* World, int32 MaterialId)
{
	AbsorptionLF        = vaWorldGetMaterialAbsorptionLF(World, MaterialId);
	AbsorptionHF        = vaWorldGetMaterialAbsorptionHF(World, MaterialId);
	Scattering          = vaWorldGetMaterialScattering(World, MaterialId);
	TransmissionLF      = vaWorldGetMaterialTransmissionLF(World, MaterialId);
	TransmissionHF      = vaWorldGetMaterialTransmissionHF(World, MaterialId);
	FlatTransmissionLF = vaWorldGetMaterialFlatTransmissionLF(World, MaterialId);
	FlatTransmissionHF = vaWorldGetMaterialFlatTransmissionHF(World, MaterialId);
}

void UVAMaterialBase::ApplyToWorld(AVAWorld* Owner)
{
	VAWorld* World = Owner ? Owner->GetVAWorld() : nullptr;

	// Null if Owner's own BeginPlay hasn't run yet - nothing to apply the material to.
	if (!World)
		return;

	int32 MaterialId;
	if (!GetMaterialId(Owner, MaterialId))
		return;

	// Custom materials (MaterialId >= 1000) don't exist in the SDK until we create them -
	// built-in materials (UVADefaultMaterial) already exist, so skip this for those.
	if (IsA<UVACustomMaterial>() && !vaWorldHasMaterial(World, MaterialId))
	{
		VAResult result = vaWorldCreateMaterial(World, MaterialId);

		if (result != VA_SUCCESS)
		{
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to create custom material %d."), MaterialId);
			return;
		}
	}

	vaWorldSetMaterialAbsorptionLF(World,        MaterialId, AbsorptionLF);
	vaWorldSetMaterialAbsorptionHF(World,        MaterialId, AbsorptionHF);
	vaWorldSetMaterialScattering(World,          MaterialId, Scattering);
	vaWorldSetMaterialTransmissionLF(World,      MaterialId, TransmissionLF);
	vaWorldSetMaterialTransmissionHF(World,      MaterialId, TransmissionHF);
	vaWorldSetMaterialFlatTransmissionLF(World, MaterialId, FlatTransmissionLF);
	vaWorldSetMaterialFlatTransmissionHF(World, MaterialId, FlatTransmissionHF);
}

AVAWorld* UVAMaterialBase::FindOwningWorldActor()
{
	// We must loop over each world, as this material could live on the World directly, or on a Component attached to geometry
	for (const TWeakObjectPtr<AVAWorld>& WeakWorld : AVAWorld::RunningWorlds)
	{
		AVAWorld* AudioWorld = WeakWorld.Get();

		if (AudioWorld && AudioWorld->Materials.Contains(this))
			return AudioWorld;
	}

	return nullptr;
}

#if WITH_EDITOR
void UVAMaterialBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	AVAWorld* Owner = FindOwningWorldActor();

	// Null if this asset isn't assigned to any world
	if (!Owner)
		return;

	ApplyToWorld(Owner);
}
#endif

bool UVADefaultMaterial::GetMaterialId(AVAWorld* Owner, int32& OutMaterialId)
{
	OutMaterialId = (int32)EVAMaterialToVA(MaterialType);
	return true;
}

void UVADefaultMaterial::ResetToDefaults()
{
	int32 MaterialId = (int32)EVAMaterialToVA(MaterialType);

	AVAWorld* Owner = FindOwningWorldActor();
	VAWorld* World = Owner ? Owner->GetVAWorld() : nullptr;

	if (World)
	{
		// A world is already running (e.g. PIE) - read its live defaults for this material.
		LoadDefaultsFromSDK(World, MaterialId);
	}
	else
	{
		VAWorld* ScratchWorld = vaWorldCreate();
		LoadDefaultsFromSDK(ScratchWorld, MaterialId);
		vaWorldDestroy(ScratchWorld);
	}

#if WITH_EDITOR
	Modify();
#endif
}

#if WITH_EDITOR
void UVADefaultMaterial::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UVADefaultMaterial, MaterialType))
		ResetToDefaults();

	// Applies to the world (base class implementation)
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

static constexpr int32 FirstCustomMaterialId = 1000;

bool UVACustomMaterial::GetMaterialId(AVAWorld* Owner, int32& OutMaterialId)
{
	if (CustomMaterialId == 0)
	{
		// Claim the lowest ID that hasn't been claimed yet
		int32 NextId = FirstCustomMaterialId;

		for (UVAMaterialBase* Other : Owner->Materials)
		{
			UVACustomMaterial* OtherCustom = Cast<UVACustomMaterial>(Other);

			if (OtherCustom && OtherCustom != this && OtherCustom->CustomMaterialId >= NextId)
				NextId = OtherCustom->CustomMaterialId + 1;
		}

		CustomMaterialId = NextId;

#if WITH_EDITOR
		Modify();
#endif
	}

	OutMaterialId = CustomMaterialId;
	return true;
}

#if WITH_EDITOR
void UVACustomMaterial::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Applies to the world (base class implementation)
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
