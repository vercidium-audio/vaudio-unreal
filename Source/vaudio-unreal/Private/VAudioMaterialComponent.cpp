#include "VAudioMaterialComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

UVAudioMaterialComponent::UVAudioMaterialComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

#if WITH_EDITOR
void UVAudioMaterialComponent::PostLoad()
{
	Super::PostLoad();
	EnsureMeshesAllowCPUAccess();
}

void UVAudioMaterialComponent::OnRegister()
{
	Super::OnRegister();
	EnsureMeshesAllowCPUAccess();
}

// Static meshes strip their CPU-side vertex/index buffers when cooked unless
// bAllowCPUAccess is set, which would silently break AVAudioWorld's triangle-mesh
// fallback in shipping builds. Flag every mesh this component's owner uses so the
// data survives cooking, matching the behaviour relied on in-editor.
void UVAudioMaterialComponent::EnsureMeshesAllowCPUAccess() const
{
	AActor* Owner = GetOwner();
	if (!Owner) return;

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
