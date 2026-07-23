#include "VAudioMaterialComponent.h"
#include "VAudioMaterial.h"
#include "VAudioWorld.h"
#include "VAudioMaterialConversion.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "VaRawLog.h"

UVAudioMaterialComponent::UVAudioMaterialComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UVAudioMaterialComponent::GetMaterialId(int32& OutMaterialId)
{
	if (!MaterialAsset)
	{
		OutMaterialId = (int32)EVAudioMaterialToVA(Material);
		return true;
	}

	if (!AudioWorld || !AudioWorld->Materials.Contains(MaterialAsset))
	{
		VALogObj(L"MaterialAsset '%s' is not in AudioWorld's Materials array - assign AudioWorld first and add the asset to its Materials array.", *MaterialAsset->GetName());
		return false;
	}

	return MaterialAsset->GetMaterialId(AudioWorld, OutMaterialId);
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

	// Owner is null in CDO/archetype contexts (see EnsureMeshesAllowCPUAccess above) - nothing to warn about yet.
	AActor* Owner = GetOwner();
	if (Owner && !AudioWorld)
		VALogObj(L"'%s' has no AudioWorld assigned - its geometry will not be added to raytracing until one is set.", *Owner->GetActorNameOrLabel());
}


// Static meshes strip their CPU-side vertex/index buffers when cooked unless
// bAllowCPUAccess is set, which would silently break AVAudioWorld's triangle-mesh
// fallback in shipping builds. Flag every mesh this component's owner uses so the
// data survives cooking, matching the behaviour relied on in-editor.
// TODO - don't require bAllowCPUAccess. Instead, the commandlet in C:\Users\verc\Documents\Unreal Projects\vaudiodemo\Plugins\vaudio-unreal\Source\VaudioUnrealEditor will already serialise the mesh data. Ensure this is working correctly
void UVAudioMaterialComponent::EnsureMeshesAllowCPUAccess() const
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
