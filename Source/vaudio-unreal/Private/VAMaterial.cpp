#include "VAMaterial.h"
#include "VAWorld.h"
#include "VAMaterialConversion.h"
#include "VAConstants.h"

#include "VALog.h"
#include "VaudioUnrealModule.h"

namespace
{
	struct FVAMaterialDefaults
	{
		float AbsorptionLF;
		float AbsorptionHF;
		float Scattering;
		float TransmissionLF;
		float TransmissionHF;
		float FlatTransmissionLF;
		float FlatTransmissionHF;
		VAColor Color;
	};

	// Read once from a scratch world, since a running world's values may already be overridden by a VADefaultMaterial
	const FVAMaterialDefaults& GetMaterialDefaults(int32 MaterialId)
	{
		static FVAMaterialDefaults Defaults[VAMaterialTypeCount];
		static bool bCached = false;

		if (!bCached && FVaudioUnrealModule::IsSdkLoaded())
		{
			VAWorld* World = vaWorldCreate();

			for (int32 i = 0; i < VAMaterialTypeCount; i++)
			{
				Defaults[i].AbsorptionLF       = vaWorldGetMaterialAbsorptionLF(World, i);
				Defaults[i].AbsorptionHF       = vaWorldGetMaterialAbsorptionHF(World, i);
				Defaults[i].Scattering         = vaWorldGetMaterialScattering(World, i);
				Defaults[i].TransmissionLF     = vaWorldGetMaterialTransmissionLF(World, i);
				Defaults[i].TransmissionHF     = vaWorldGetMaterialTransmissionHF(World, i);
				Defaults[i].FlatTransmissionLF = vaWorldGetMaterialFlatTransmissionLF(World, i);
				Defaults[i].FlatTransmissionHF = vaWorldGetMaterialFlatTransmissionHF(World, i);
				Defaults[i].Color              = vaWorldGetMaterialColor(World, i);
			}

			VAResult result = vaWorldDestroy(World);

			if (result != VA_SUCCESS)
				VA_ERROR_RESULT(result, TEXT("Failed to destroy the scratch world used to read the default materials."));

			bCached = true;
		}

		return Defaults[FMath::Clamp(MaterialId, 0, VAMaterialTypeCount - 1)];
	}

	void LogIfSetterFailed(VAResult result, const TCHAR* PropertyName, const FString& MaterialName)
	{
		if (result == VA_SUCCESS || result == VA_UNCHANGED)
			return;

		VA_ERROR_RESULT(result, TEXT("Failed to set %s on material '%s'."), PropertyName, *MaterialName);
	}

	void SetWorldMaterial(VAWorld* World, int32 MaterialId, const FString& MaterialName, float AbsorptionLF, float AbsorptionHF, float Scattering, float TransmissionLF, float TransmissionHF, float FlatTransmissionLF, float FlatTransmissionHF, VAColor Color)
	{
		LogIfSetterFailed(vaWorldSetMaterialAbsorptionLF(World, MaterialId, AbsorptionLF), TEXT("AbsorptionLF"), MaterialName);
		LogIfSetterFailed(vaWorldSetMaterialAbsorptionHF(World, MaterialId, AbsorptionHF), TEXT("AbsorptionHF"), MaterialName);
		LogIfSetterFailed(vaWorldSetMaterialScattering(World, MaterialId, Scattering), TEXT("Scattering"), MaterialName);
		LogIfSetterFailed(vaWorldSetMaterialTransmissionLF(World, MaterialId, TransmissionLF), TEXT("TransmissionLF"), MaterialName);
		LogIfSetterFailed(vaWorldSetMaterialTransmissionHF(World, MaterialId, TransmissionHF), TEXT("TransmissionHF"), MaterialName);
		LogIfSetterFailed(vaWorldSetMaterialFlatTransmissionLF(World, MaterialId, FlatTransmissionLF), TEXT("FlatTransmissionLF"), MaterialName);
		LogIfSetterFailed(vaWorldSetMaterialFlatTransmissionHF(World, MaterialId, FlatTransmissionHF), TEXT("FlatTransmissionHF"), MaterialName);
		LogIfSetterFailed(vaWorldSetMaterialColor(World, MaterialId, Color), TEXT("Color"), MaterialName);
	}
}

void UVAMaterialBase::LoadDefaultsFromSDK(int32 MaterialId)
{
	const FVAMaterialDefaults& Defaults = GetMaterialDefaults(MaterialId);

	AbsorptionLF       = Defaults.AbsorptionLF;
	AbsorptionHF       = Defaults.AbsorptionHF;
	Scattering         = Defaults.Scattering;
	TransmissionLF     = Defaults.TransmissionLF;
	TransmissionHF     = Defaults.TransmissionHF;
	FlatTransmissionLF = Defaults.FlatTransmissionLF;
	FlatTransmissionHF = Defaults.FlatTransmissionHF;
	Color              = VAColorToFColor(Defaults.Color);
}

void UVAMaterialBase::ApplyToWorld(AVAWorld* Owner) const
{
	VAWorld* World = Owner ? Owner->GetVAWorld() : nullptr;

	// Null if Owner's own BeginPlay hasn't run yet - nothing to apply the material to.
	if (!World)
		return;

	int32 MaterialId;
	if (!GetMaterialId(Owner, MaterialId))
		return;

	SetWorldMaterial(World, MaterialId, GetMaterialName(), AbsorptionLF, AbsorptionHF, Scattering, TransmissionLF, TransmissionHF, FlatTransmissionLF, FlatTransmissionHF, FColorToVA(Color));
}

#if WITH_EDITOR
void UVAMaterialBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Several worlds can share this asset, e.g. PIE with multiple clients
	for (const TWeakObjectPtr<AVAWorld>& WeakWorld : AVAWorld::RunningWorlds)
	{
		AVAWorld* Owner = WeakWorld.Get();

		if (Owner && Owner->Materials.Contains(this))
			ApplyToWorld(Owner);
	}
}
#endif

bool UVADefaultMaterial::GetMaterialId(const AVAWorld* Owner, int32& OutMaterialId) const
{
	OutMaterialId = (int32)EVAMaterialToVA(MaterialType);
	return true;
}

void UVADefaultMaterial::ResetToDefaults()
{
	LoadDefaultsFromSDK((int32)EVAMaterialToVA(MaterialType));

#if WITH_EDITOR
	Modify();
#endif
}

void UVADefaultMaterial::RestoreWorldDefaults(AVAWorld* Owner) const
{
	VAWorld* World = Owner ? Owner->GetVAWorld() : nullptr;

	if (!World)
		return;

	int32 MaterialId = (int32)EVAMaterialToVA(MaterialType);
	const FVAMaterialDefaults& Defaults = GetMaterialDefaults(MaterialId);

	SetWorldMaterial(World, MaterialId, GetMaterialName(), Defaults.AbsorptionLF, Defaults.AbsorptionHF, Defaults.Scattering, Defaults.TransmissionLF, Defaults.TransmissionHF, Defaults.FlatTransmissionLF, Defaults.FlatTransmissionHF, Defaults.Color);
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

bool UVACustomMaterial::GetMaterialId(const AVAWorld* Owner, int32& OutMaterialId) const
{
	OutMaterialId = Owner ? Owner->GetCustomMaterialId(this) : 0;

	if (OutMaterialId == 0)
	{
		VA_ERROR(TEXT("Custom material '%s' has no ID in this VAWorld. Add it to the VAWorld's Materials array."), *GetMaterialName());
		return false;
	}

	return true;
}
