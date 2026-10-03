#include "VAMaterialComponent.h"
#include "VAMaterial.h"
#include "VAWorld.h"
#include "VAMaterialConversion.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "VALog.h"

UVAMaterialComponent::UVAMaterialComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UVAMaterialComponent::GetMaterialId(int32& OutMaterialId)
{
	if (!MaterialAsset)
	{
		OutMaterialId = (int32)EVAMaterialToVA(Material);
		return true;
	}

	if (!AudioWorld || !AudioWorld->Materials.Contains(MaterialAsset))
	{
		VA_WARN_NAMED(TEXT("MaterialAsset '%s' is not in AudioWorld's Materials array - assign AudioWorld first and add the asset to its Materials array."), *MaterialAsset->GetName());
		return false;
	}

	return MaterialAsset->GetMaterialId(AudioWorld, OutMaterialId);
}

bool UVAMaterialComponent::GetMaterialFor(const UActorComponent* component, int32& outMaterialId, bool& outUseFlatTransmission)
{
	// Overrides name this actor's own components, not those of child actors inheriting this material
	if (component && component->GetOwner() == GetOwner())
	{
		for (const FVAMaterialOverride& materialOverride : MaterialOverrides)
		{
			if (materialOverride.Component != component->GetFName())
				continue;

			outUseFlatTransmission = materialOverride.bUseFlatTransmission;

			if (!materialOverride.MaterialAsset)
			{
				outMaterialId = (int32)EVAMaterialToVA(materialOverride.Material);
				return true;
			}

			if (!AudioWorld || !AudioWorld->Materials.Contains(materialOverride.MaterialAsset))
			{
				VA_WARN_NAMED(TEXT("The MaterialAsset '%s' for component '%s' is not in AudioWorld's Materials array."), *materialOverride.MaterialAsset->GetName(), *component->GetName());
				return false;
			}

			return materialOverride.MaterialAsset->GetMaterialId(AudioWorld, outMaterialId);
		}
	}

	outUseFlatTransmission = bUseFlatTransmission;
	return GetMaterialId(outMaterialId);
}

TArray<FString> UVAMaterialComponent::GetComponentNames() const
{
	TArray<FString> names;

	if (AActor* owner = GetOwner())
	{
		for (UActorComponent* component : owner->GetComponents())
			if (Cast<USceneComponent>(component))
				names.Add(component->GetName());
	}

	return names;
}

void UVAMaterialComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AudioWorld)
		AudioWorld->AddMaterialPrimitives(this);
}

void UVAMaterialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AudioWorld)
		AudioWorld->RemoveMaterialPrimitives(this);

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void UVAMaterialComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Edited during play, e.g. on the PIE actor, so rebuild this actor's geometry with the new material, propagate mode or LOD
	if (HasBegunPlay() && AudioWorld && GetOwner())
		AudioWorld->SyncPrimitive(GetOwner());
}

void UVAMaterialComponent::PostLoad()
{
	Super::PostLoad();
	EnsureMeshesAllowCPUAccess();
}

void UVAMaterialComponent::OnRegister()
{
	Super::OnRegister();
	EnsureMeshesAllowCPUAccess();

	// Owner is null in CDO/archetype contexts (see EnsureMeshesAllowCPUAccess above) - nothing to warn about yet.
	AActor* Owner = GetOwner();
	if (Owner && !AudioWorld)
		VA_LOG_NAMED(TEXT("Has no AudioWorld assigned - its geometry will not be added to raytracing until one is set."));
}


void UVAMaterialComponent::EnsureMeshesAllowCPUAccess() const
{
	AActor* Owner = GetOwner();

	if (!Owner)
		return;

	TArray<UStaticMeshComponent*> MeshComps;
	Owner->GetComponents<UStaticMeshComponent>(MeshComps);

	for (UStaticMeshComponent* MeshComp : MeshComps)
	{
		UStaticMesh* Mesh = MeshComp ? MeshComp->GetStaticMesh() : nullptr;
		if (Mesh && !Mesh->bAllowCPUAccess)
		{
			Mesh->Modify();
			Mesh->bAllowCPUAccess = true;
		}
	}
}
#endif
