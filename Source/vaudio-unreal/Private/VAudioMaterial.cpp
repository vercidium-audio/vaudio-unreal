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
	PlaneTransmissionLF = vaWorldGetMaterialPlaneTransmissionLF(World, MaterialId);
	PlaneTransmissionHF = vaWorldGetMaterialPlaneTransmissionHF(World, MaterialId);
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

	vaWorldSetMaterialAbsorptionLF(World,        MaterialId, AbsorptionLF);
	vaWorldSetMaterialAbsorptionHF(World,        MaterialId, AbsorptionHF);
	vaWorldSetMaterialScattering(World,          MaterialId, Scattering);
	vaWorldSetMaterialTransmissionLF(World,      MaterialId, TransmissionLF);
	vaWorldSetMaterialTransmissionHF(World,      MaterialId, TransmissionHF);
	vaWorldSetMaterialPlaneTransmissionLF(World, MaterialId, PlaneTransmissionLF);
	vaWorldSetMaterialPlaneTransmissionHF(World, MaterialId, PlaneTransmissionHF);
}

AVAudioWorld* UVAudioMaterialAssetBase::FindOwningWorldActor()
{
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

	// Null if this asset isn't assigned to any currently-running world's Materials array -
	// nothing to apply the material to.
	if (!Owner)
		return;

	ApplyToWorld(Owner);
}
#endif

// ---------------------------------------------------------------------------
// UVAudioMaterialAsset - overrides one of the 23 built-in materials
// ---------------------------------------------------------------------------

bool UVAudioDefaultMaterialAsset::GetMaterialId(AVAudioWorld* Owner, int32& OutMaterialId)
{
	OutMaterialId = (int32)EVAudioMaterialToVA(MaterialType);
	return true;
}

void UVAudioDefaultMaterialAsset::ResetToDefaults()
{
	AVAudioWorld* Owner = FindOwningWorldActor();
	VAWorld* World = Owner ? Owner->GetVAWorld() : nullptr;

	if (!World)
	{
		// TODO - can we raise an official warning somewhere? Show it in the editor?
		VALogObj(L"not assigned to a running VAudioWorld's Materials array - can't read defaults.");
		return;
	}

	int32 MaterialId = (int32)EVAudioMaterialToVA(MaterialType);
	LoadDefaultsFromSDK(World, MaterialId);

#if WITH_EDITOR
	Modify();
#endif
}

// ---------------------------------------------------------------------------
// UVAudioCustomMaterialAsset - a brand new material with an SDK-assigned ID
// ---------------------------------------------------------------------------

// Smallest ID reserved for custom (non-built-in) materials - matches VAMaterialType's comment
// in vaudio.h ("First 1000 values are reserved").
// TODO - move this constant to vaudio.h
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
