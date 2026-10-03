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

#if WITH_EDITOR
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
