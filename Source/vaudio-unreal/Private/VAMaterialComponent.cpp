#include "VAMaterialComponent.h"
#include "VAMaterial.h"
#include "VAWorld.h"
#include "VAWorldSubsystem.h"
#include "VAMaterialConversion.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

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

	if (!AudioWorld || !AudioWorld->HasMaterial(MaterialAsset))
	{
		VA_WARN_NAMED(TEXT("MaterialAsset '%s' is not in the VAWorld's Materials array. Add it to the Materials array of the level's VAWorld."), *MaterialAsset->GetName());
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

			if (!AudioWorld || !AudioWorld->HasMaterial(materialOverride.MaterialAsset))
			{
				VA_WARN_NAMED(TEXT("The MaterialAsset '%s' for component '%s' is not in the VAWorld's Materials array."), *materialOverride.MaterialAsset->GetName(), *component->GetName());
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

	AudioWorld = AVAWorld::Find(this);

	if (AudioWorld)
	{
		AudioWorld->AddMaterialPrimitives(this);
		return;
	}

	// The VAWorld begins play after this component, as actor BeginPlay order isn't guaranteed
	if (UVAWorldSubsystem* subsystem = GetWorld()->GetSubsystem<UVAWorldSubsystem>())
		WorldRegisteredHandle = subsystem->OnWorldRegistered.AddUObject(this, &UVAMaterialComponent::OnWorldRegistered);
}

void UVAMaterialComponent::OnWorldRegistered()
{
	StopWaitingForWorld();
	AudioWorld = AVAWorld::Find(this);

	if (AudioWorld)
		AudioWorld->AddMaterialPrimitives(this);
}

void UVAMaterialComponent::StopWaitingForWorld()
{
	if (!WorldRegisteredHandle.IsValid())
		return;

	if (UVAWorldSubsystem* subsystem = GetWorld()->GetSubsystem<UVAWorldSubsystem>())
		subsystem->OnWorldRegistered.Remove(WorldRegisteredHandle);

	WorldRegisteredHandle.Reset();
}

void UVAMaterialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopWaitingForWorld();

	if (AudioWorld)
		AudioWorld->RemoveMaterialPrimitives(this);

	AudioWorld = nullptr;

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
