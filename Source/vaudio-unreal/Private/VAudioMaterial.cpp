#include "VAudioMaterial.h"
#include "VAudioWorld.h"
#include "VAudioMaterialConversion.h"

#include "VARawLog.h"

void UVAudioMaterialAssetBase::LoadDefaultsFromSDK(VAWorld* World, int32 MaterialId)
{
	AbsorptionLF        = vaWorldGetMaterialAbsorptionLF(World, MaterialId);
	AbsorptionHF        = vaWorldGetMaterialAbsorptionHF(World, MaterialId);
	Scattering          = vaWorldGetMaterialScattering(World, MaterialId);
	TransmissionLF      = vaWorldGetMaterialTransmissionLF(World, MaterialId);
	TransmissionHF      = vaWorldGetMaterialTransmissionHF(World, MaterialId);
	FlatTransmissionLF = vaWorldGetMaterialFlatTransmissionLF(World, MaterialId);
	FlatTransmissionHF = vaWorldGetMaterialFlatTransmissionHF(World, MaterialId);
}

void UVAudioMaterialAssetBase::ApplyToWorld(AVAudioWorld* Owner)
{
	VAWorld* World = Owner ? Owner->GetVAWorld() : nullptr;

	// Null if Owner's own BeginPlay hasn't run yet - nothing to apply the material to.
	if (!World)
		return;

	int32 MaterialId;
	if (!GetMaterialId(Owner, MaterialId))
		return;

	// Custom materials (MaterialId >= 1000) don't exist in the SDK until we create them -
	// built-in materials (UVAudioDefaultMaterialAsset) already exist, so skip this for those.
	if (IsA<UVAudioCustomMaterialAsset>() && !vaWorldHasMaterial(World, MaterialId))
	{
		VAResult result = vaWorldCreateMaterial(World, MaterialId);
		check(result == VA_SUCCESS);
	}

	vaWorldSetMaterialAbsorptionLF(World,        MaterialId, AbsorptionLF);
	vaWorldSetMaterialAbsorptionHF(World,        MaterialId, AbsorptionHF);
	vaWorldSetMaterialScattering(World,          MaterialId, Scattering);
	vaWorldSetMaterialTransmissionLF(World,      MaterialId, TransmissionLF);
	vaWorldSetMaterialTransmissionHF(World,      MaterialId, TransmissionHF);
	vaWorldSetMaterialFlatTransmissionLF(World, MaterialId, FlatTransmissionLF);
	vaWorldSetMaterialFlatTransmissionHF(World, MaterialId, FlatTransmissionHF);
}

AVAudioWorld* UVAudioMaterialAssetBase::FindOwningWorldActor()
{
	// We must loop over each world, as this material could live on the World directly, or on a Component attached to geometry
	for (const TWeakObjectPtr<AVAudioWorld>& WeakWorld : AVAudioWorld::RunningWorlds)
	{
		AVAudioWorld* AudioWorld = WeakWorld.Get();

		if (AudioWorld && AudioWorld->Materials.Contains(this))
			return AudioWorld;
	}

	return nullptr;
}

#if WITH_EDITOR
void UVAudioMaterialAssetBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	AVAudioWorld* Owner = FindOwningWorldActor();

	// Null if this asset isn't assigned to any world
	if (!Owner)
		return;

	ApplyToWorld(Owner);
}
#endif

bool UVAudioDefaultMaterialAsset::GetMaterialId(AVAudioWorld* Owner, int32& OutMaterialId)
{
	OutMaterialId = (int32)EVAudioMaterialToVA(MaterialType);
	return true;
}

void UVAudioDefaultMaterialAsset::ResetToDefaults()
{
	int32 MaterialId = (int32)EVAudioMaterialToVA(MaterialType);

	AVAudioWorld* Owner = FindOwningWorldActor();
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
void UVAudioDefaultMaterialAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UVAudioDefaultMaterialAsset, MaterialType))
		ResetToDefaults();

	// Applies to the world (base class implementation)
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

static constexpr int32 FirstCustomMaterialId = 1000;

bool UVAudioCustomMaterialAsset::GetMaterialId(AVAudioWorld* Owner, int32& OutMaterialId)
{
	if (CustomMaterialId == 0)
	{
		// Claim the lowest ID that hasn't been claimed yet
		int32 NextId = FirstCustomMaterialId;

		for (UVAudioMaterialAssetBase* Other : Owner->Materials)
		{
			UVAudioCustomMaterialAsset* OtherCustom = Cast<UVAudioCustomMaterialAsset>(Other);

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
void UVAudioCustomMaterialAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Applies to the world (base class implementation)
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
