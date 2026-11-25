// Human-created

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

// Get the SDK ID of this material
bool UVAMaterialComponent::GetMaterialID(int32& OutMaterialId)
{
	return VAResolveMaterialID(MaterialAsset, Material, AudioWorld, this, FString(), OutMaterialId);
}

bool UVAMaterialComponent::GetMaterialFor(const UActorComponent* component, int32& outMaterialId, bool& outUseFlatTransmission)
{
	// component must be provided
	check(component);

	if (component->GetOwner() == GetOwner())
	{
		// Multiple overrides can be provided for each StaticMeshComponent on this actor
		for (const FVAMaterialOverride& materialOverride : MaterialOverrides)
		{
			if (materialOverride.Component == component->GetFName())
			{
				// If a materialOverride is provided for this component, use it instead
				// TODO - remove overrides. Material refactor needed
				outUseFlatTransmission = materialOverride.bUseFlatTransmission;
				return VAResolveMaterialID(materialOverride.MaterialAsset, materialOverride.Material, AudioWorld, this, FString::Printf(TEXT(" (override for component '%s')"), *component->GetName()), outMaterialId);
			}
		}
	}

	// Use our own material
	outUseFlatTransmission = bUseFlatTransmission;
	return GetMaterialID(outMaterialId);
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
	JoinWorld();
}

void UVAMaterialComponent::JoinWorld()
{
	UWorld* world = GetWorld();
	BoundSubsystem = world ? world->GetSubsystem<UVAWorldSubsystem>() : nullptr;

	UVAWorldSubsystem* subsystem = BoundSubsystem.Get();

	if (subsystem)
		WorldUnregisteredHandle = subsystem->OnWorldUnregistered.AddUObject(this, &UVAMaterialComponent::OnWorldUnregistered);

	AudioWorld = AVAWorld::Find(this);

	if (AudioWorld)
	{
		AudioWorld->AddMaterialPrimitives(this);
		return;
	}

	// The VAWorld begins play after this component, as actor BeginPlay order isn't guaranteed
	if (subsystem)
		WorldRegisteredHandle = subsystem->OnWorldRegistered.AddUObject(this, &UVAMaterialComponent::OnWorldRegistered);
}

void UVAMaterialComponent::LeaveWorld()
{
	StopWaitingForWorld();

	// The subsystem is already gone if its map was
	if (UVAWorldSubsystem* subsystem = BoundSubsystem.Get())
		subsystem->OnWorldUnregistered.Remove(WorldUnregisteredHandle);

	WorldUnregisteredHandle.Reset();
	BoundSubsystem.Reset();

	if (AudioWorld)
		AudioWorld->RemoveMaterialPrimitives(this);

	AudioWorld = nullptr;
}

void UVAMaterialComponent::OnWorldUnregistered()
{
	// Still waiting for a VAWorld
	if (!AudioWorld)
		return;

	// The VAWorld destroyed this component's primitives as it ended play
	AudioWorld = nullptr;

	if (UVAWorldSubsystem* subsystem = BoundSubsystem.Get())
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

	if (UVAWorldSubsystem* subsystem = BoundSubsystem.Get())
		subsystem->OnWorldRegistered.Remove(WorldRegisteredHandle);

	WorldRegisteredHandle.Reset();
}

void UVAMaterialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	LeaveWorld();

	Super::EndPlay(EndPlayReason);
}

void UVAMaterialComponent::OnRegister()
{
	Super::OnRegister();

	UWorld* world = GetWorld();
	UVAWorldSubsystem* subsystem = world ? world->GetSubsystem<UVAWorldSubsystem>() : nullptr;

	// Seamless travel moves a kept actor into the new map without EndPlay or BeginPlay, and re-registers its components there
	if (HasBegunPlay() && subsystem != BoundSubsystem.Get())
	{
		LeaveWorld();
		JoinWorld();
	}
}

#if WITH_EDITOR
void UVAMaterialComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Edited during play, e.g. on the PIE actor, so rebuild this actor's geometry with the new material, propagate mode or LOD
	if (HasBegunPlay() && AudioWorld && GetOwner())
		AudioWorld->SyncPrimitive(GetOwner());
}
#endif
