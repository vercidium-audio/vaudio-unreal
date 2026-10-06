#include "VAWorld.h"
#include "VASubmixEffectDirectionalPan.h"
#include "VAEmitterBase.h"
#include "VASource.h"
#include "VAEmitter.h"
#include "VAListener.h"
#include "VAMaterial.h"
#include "VAReverbConversion.h"
#include "VAConstants.h"
#include "VALog.h"

#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "AudioMixerBlueprintLibrary.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

extern "C" {
#include "vaudio.h"
}


// List of worlds used by Material assets to reverse-lookup the world(s) they belong to
TArray<TWeakObjectPtr<AVAWorld>> AVAWorld::RunningWorlds;

TMap<VAEmitter*, AVAWorld*> AVAWorld::OrphanedEmitters;

void AVAWorld::OnReverbUpdatedTrampoline(VAWorld* world)
{
	if (AVAWorld* self = static_cast<AVAWorld*>(vaWorldGetUserData(world)))
	{
		self->OnReverbUpdated();
		self->RaytraceCount++;
	}
}

AVAWorld::AVAWorld()
{
	PrimaryActorTick.bCanEverTick = true;

	// Allow components to be attached to this AudioWorld
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Display the world bounds in the editor via a component
	WorldBounds = CreateDefaultSubobject<UVAWorldBoundsComponent>(TEXT("WorldBounds"));
	WorldBounds->SetupAttachment(Root);
	WorldBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorldBounds->SetGenerateOverlapEvents(false);
	WorldBounds->SetHiddenInGame(false);
}

void AVAWorld::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RefreshWorldBounds();
}

void AVAWorld::RefreshWorldBounds()
{
	// Convert position + size to location + extent
	WorldBounds->SetWorldLocation(WorldPosition + WorldSize * 0.5f);
	WorldBounds->SetBoxExtent(WorldSize * 0.5f);

	// No rotation
	WorldBounds->SetWorldRotation(FQuat::Identity);
}

void AVAWorld::UpdateVAWorld()
{
	// Rendering
	vaWorldSetRenderingEnabled(World, bRenderingEnabled);
	vaWorldSetCameraSpeed(World, CameraSpeed);

	// World bounds
	vaWorldSetPositionUnreal(World, WorldPosition);
	vaWorldSetSizeUnreal(World, WorldSize);

	// World config
	vaWorldSetInverseSpeedOfSound(World, 1.0f / FMath::Max(0.0001f, SpeedOfSound));
	vaWorldSetMetersPerUnit(World, FMath::Max(0.0001f, MetersPerUnit));
	vaWorldSetEpsilon(World, Epsilon);
	vaWorldSetEmittersOutsideTheWorldAreMuffled(World, bEmittersOutsideTheWorldAreMuffled);
	vaWorldSetOcclusionRaysLoseEnergyFromWorldBounds(World, bOcclusionRaysLoseEnergyFromWorldBounds);

	// Threading
	vaWorldSetWorkItemCount(World, FMath::Max(1, WorkItemCount));
	vaWorldSetMaximumConcurrencyLevel(World, FMath::Max(1, MaximumConcurrencyLevel));
	vaWorldSetPendingShutdown(World, bPendingShutdown);

	// Air absorption
	vaWorldSetReferenceFrequencyLF(World, ReferenceFrequencyLF);
	vaWorldSetReferenceFrequencyHF(World, ReferenceFrequencyHF);

	if (bAirAbsorptionEnabled)
	{
		vaWorldSetAirAbsorptionHumidity(World, Humidity);
		vaWorldSetAirAbsorptionTemperature(World, Temperature);
		vaWorldSetAirAbsorptionPressure(World, Pressure);
	}
	else
	{
		vaWorldSetAirAbsorption(World, nullptr);
	}
}

#if WITH_EDITOR
bool UVAWorldBoundsComponent::CanEditChange(const FProperty* InProperty) const
{
	// Grey out the readonly Box Extents fields
	static const FName BoxExtentPropertyName(TEXT("BoxExtent"));

	if (InProperty && InProperty->GetFName() == BoxExtentPropertyName)
		return false;

	return Super::CanEditChange(InProperty);
}

void AVAWorld::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Unlike the vaWorldSet* calls below, the box should reflect WorldPosition/WorldSize even
	// before BeginPlay (or after EndPlay), since World is null until then.
	RefreshWorldBounds();

	// Ignore edits before pressing Play
	if (!World)
		return;

	UpdateVAWorld();

	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(AVAWorld, CollisionObjectTypes))
		RebuildPrimitives();
}
#endif

void AVAWorld::BeginPlay()
{
	Super::BeginPlay();

	RunningWorlds.Add(this);

	InitializeVAWorld();
}

// This can also be called by other VA emitters, as they might initialise first (actor init order not guaranteed)
void AVAWorld::InitializeVAWorld()
{
	// Already initialised, all is good
	if (World)
		return;

	World = vaWorldCreate();
	vaWorldSetUserData(World, this);
	vaWorldSetOnReverbUpdatedCallback(World, &OnReverbUpdatedTrampoline);

	// Logging
	vaWorldSetLogCallback(World, &VASdkWorldLogCallback);
	vaWorldSetLogMemoryAllocationWarnings(World, true);

	// Coordinate system
	vaWorldSetCoordinateSystem(World, VACoordinateSystemUnreal);

	// Size / air absorption / etc
	UpdateVAWorld();

	// Create presets for each submix
	int32 GroupedEAXCount = GroupedEAXSubmixes.Num();
	vaWorldSetMaximumGroupedEAXCount(World, GroupedEAXCount);

	for (int32 i = 0; i < GroupedEAXCount; i++)
	{
		USoundSubmix* Sub = GroupedEAXSubmixes[i];
		USubmixEffectReverbPreset* Preset = NewObject<USubmixEffectReverbPreset>(this);
		UVASubmixEffectDirectionalPanPreset* PanPreset = NewObject<UVASubmixEffectDirectionalPanPreset>(this);

		if (Sub)
		{
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, Sub, Preset);
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, Sub, PanPreset);
		}
		else
			VA_WARN_NAMED(TEXT("Has a null grouped EAX submix at index %d. Please assign a submix"), i);

		GroupedEAXPresets.Add(Preset);
		GroupedEAXPanPresets.Add(PanPreset);
	}

	InitialiseMaterials();
}

// Pan follows what the player hears, which is the player controller's audio listener rather than the listener actor
static FRotator GetAudioListenerRotation(AVAListener* Listener)
{
	APlayerController* PlayerController = nullptr;

	if (APawn* Pawn = Cast<APawn>(Listener->GetAttachParentActor()))
		PlayerController = Cast<APlayerController>(Pawn->GetController());

	if (!PlayerController)
		PlayerController = Listener->GetWorld()->GetFirstPlayerController();

	if (PlayerController)
	{
		FVector Location, Front, Right;
		PlayerController->GetAudioListenerPosition(Location, Front, Right);
		return Front.Rotation();
	}

	return Listener->GetActorRotation();
}

// Runs on the game thread during vaWorldUpdate, once per completed raytracing pass
void AVAWorld::OnReverbUpdated()
{
	AVAListener* Listener = GetMainListener();
	VAEmitter* ListenerVA = Listener ? Listener->GetVAEmitter() : nullptr;

	if (ListenerVA)
		Listener->ApplyListenerReverb();

	const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(World);
	int32 Count = vaWorldGetGroupedEAXCount(World);

	FRotator ListenerRotation = ListenerVA ? GetAudioListenerRotation(Listener) : FRotator::ZeroRotator;

	for (int32 i = 0; i < Count; ++i)
	{
		USubmixEffectReverbPreset* Preset = GetGroupedEAXPreset(i);

		// ALready logged above
		if (!Preset)
			continue;

		const VAEAXReverb* EAX = GroupedEAX[i];

		FSubmixEffectReverbSettings settings = VAEAXReverbToSubmixSettings(EAX);
		Preset->SetSettings(settings);

		if (ListenerVA)
		{
			VAVector* Direction = vaEAXReverbGetRelativeDirection(EAX, ListenerVA);

			if (Direction)
			{
				// Listener space is X+ right. Magnitude is strength (OpenAL Soft EAX style), so it isn't normalised
				VAVector panVector = vaWorldCalculateListenerRelativePan(World, *Direction, FMath::DegreesToRadians(ListenerRotation.Pitch), FMath::DegreesToRadians(ListenerRotation.Yaw));
				float pan = FMath::Clamp(panVector.x, -1.0f, 1.0f);

				if (UVASubmixEffectDirectionalPanPreset* PanPreset = GroupedEAXPanPresets.IsValidIndex(i) ? GroupedEAXPanPresets[i] : nullptr)
					PanPreset->SetPan(pan);
			}
		}
	}
}

static FRotator GetListenerControlRotation(AVAListener* Listener)
{
	if (APawn* Pawn = Cast<APawn>(Listener->GetAttachParentActor()))
		if (AController* Controller = Pawn->GetController())
			return Controller->GetControlRotation();

	if (APlayerController* PlayerController = Listener->GetWorld()->GetFirstPlayerController())
		return PlayerController->GetControlRotation();

	return Listener->GetActorRotation();
}

void AVAWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	RunningWorlds.RemoveSingleSwap(this);

	for (int32 i = 0; i < GroupedEAXPresets.Num(); i++)
	{
		USoundSubmix* Sub = GroupedEAXSubmixes.IsValidIndex(i) ? GroupedEAXSubmixes[i] : nullptr;

		if (!Sub)
			continue;

		if (USubmixEffectReverbPreset* Preset = GroupedEAXPresets[i])
			UAudioMixerBlueprintLibrary::RemoveSubmixEffect(this, Sub, Preset);

		if (UVASubmixEffectDirectionalPanPreset* PanPreset = GroupedEAXPanPresets[i])
			UAudioMixerBlueprintLibrary::RemoveSubmixEffect(this, Sub, PanPreset);
	}

	GroupedEAXPresets.Empty();
	GroupedEAXPanPresets.Empty();

	if (World)
	{
		// Blocks until the raytracing threads finish, after which no primitive is in use
		vaWorldWait(World);
		DestroyPrimitives();
		MaterialSources.Empty();

		// Invokes OnRemoved for the emitters whose removal was waiting on raytracing results
		VAResult result = vaWorldDestroy(World);

		if (result != VA_SUCCESS)
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to destroy the world."));

		World = nullptr;
		DestroyRemovedEmitters();

		// vaWorldDestroy unlinked the emitters still waiting on a reverb tail, so they can be freed now
		for (auto it = OrphanedEmitters.CreateIterator(); it; ++it)
		{
			if (it.Value() != this)
				continue;

			vaEmitterDestroy(it.Key());
			it.RemoveCurrent();
		}
	}

	// Emitters that end play after this see GetVAWorld() == nullptr and destroy their own handles
	RegisteredEmitters.Empty();
	Listeners.Empty();
	MainListener = nullptr;
	PendingEventEmitters.Empty();
}

void AVAWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (World)
	{
		AVAListener* mainListener = GetMainListener();

		// Sync the camera with the main listener. Debug window can still free fly with F1
		if (mainListener)
		{
			vaWorldSetCameraPosition(World, vaEmitterGetPosition(mainListener->GetVAEmitter()));

			FRotator rotation = GetListenerControlRotation(mainListener);
			vaWorldSetCameraPitch(World, FMath::DegreesToRadians(rotation.Pitch));
			vaWorldSetCameraYaw(World, FMath::DegreesToRadians(rotation.Yaw));
		}

		vaWorldUpdate(World);

		// OnRemoved has been invoked for these, so the raytracing threads no longer read them
		DestroyRemovedEmitters();

		// Broadcast the Blueprint events the SDK callbacks queued during vaWorldUpdate. A handler may destroy emitters, hence the weak pointers
		TArray<TWeakObjectPtr<AVAEmitterBase>> eventEmitters = MoveTemp(PendingEventEmitters);

		for (const TWeakObjectPtr<AVAEmitterBase>& emitter : eventEmitters)
			if (AVAEmitterBase* alive = emitter.Get())
				alive->FlushPendingEvents();

		if (bReverbOnly != bWasReverbOnly)
		{
			bWasReverbOnly = bReverbOnly;
			bool bDryEnabled = !bReverbOnly;

			// SetDryOutputEnabled() is only implemented on AVASource - other AVAEmitterBase
			// subclasses don't have dry output to toggle here.
			for (AVAEmitterBase* Emitter : RegisteredEmitters)
				if (AVASource* ConcreteEmitter = Cast<AVASource>(Emitter))
					ConcreteEmitter->SetDryOutputEnabled(bDryEnabled);
		}

		if (GEngine)
		{
			// Per-emitter position and world-bounds check
			// Listeners that aren't current have no handle
			TArray<AVAEmitterBase*> statusEmitters;

			if (MainListener)
				statusEmitters.Add(MainListener);

			statusEmitters.Append(RegisteredEmitters);

			for (int32 i = 0; i < statusEmitters.Num(); ++i)
			{
				AVAEmitterBase* baseEmitter = statusEmitters[i];
				AVAListener* listener = Cast<AVAListener>(baseEmitter);
				AVAEmitter* continuousEmitter = listener ? nullptr : Cast<AVAEmitter>(baseEmitter);

				if (!listener && !continuousEmitter)
					continue;

				VAEmitter* vaEmitter = baseEmitter->GetVAEmitter();

				uint64 messageID = VAMessageKey(baseEmitter, EVAMessageSlot::Status);

				if (!vaEmitter)
				{
					VAShowMessage(messageID, 0.0f, FColor::Orange,
						FString::Printf(TEXT("[VA] Emitter %d '%s': initialising"), i, *baseEmitter->GetActorNameOrLabel()));

					continue;
				}

				bool bInBounds = vaEmitterGetWithinWorldBounds(vaEmitter);
				VAVector P = vaEmitterGetPosition(vaEmitter);

				const wchar_t* boundsStatus = bInBounds ? TEXT("[in bounds]") : TEXT("[out of bounds]");


				if (listener)
				{
					FColor color = bInBounds ? FColor::Green : FColor::Orange;

					VAShowMessage(messageID, 0.0f, color,
						FString::Printf(TEXT("[VA] Listener Emitter %d '%s': (%.1f, %.1f, %.1f) %s"), i, *listener->GetActorNameOrLabel(), P.x, P.y, P.z, boundsStatus));
				}
				else
				{
					AVASource* source = Cast<AVASource>(continuousEmitter);

					// If it's a source (not continuous), ensure its audio component is configured correctly
					if (source)
					{
						if (!source->SourceSound)
						{
							uint64 errorMessageID = VAMessageKey(continuousEmitter, EVAMessageSlot::SourceStatus);
							VAShowMessage(errorMessageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source Emitter %d '%s' has no sound file assigned"), i, *continuousEmitter->GetActorNameOrLabel()));
						}
						else if (!source->SourceSound->AttenuationSettings)
						{
							uint64 errorMessageID = VAMessageKey(continuousEmitter, EVAMessageSlot::AttenuationStatus);
							VAShowMessage(errorMessageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source Emitter %d '%s' has no Sound Attenuation - it will not fall off with distance"), i, *continuousEmitter->GetActorNameOrLabel()));
						}
						else if (!source->SourceAudioComponent) // SourceAudioComponent is set when it actually plays
						{
							uint64 errorMessageID = VAMessageKey(continuousEmitter, EVAMessageSlot::SourceStatus);
							VAShowMessage(errorMessageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source Emitter %d '%s' has not played its sound yet"), i, *continuousEmitter->GetActorNameOrLabel()));
						}
					}

					UAudioComponent* sourceAudioComponent = source ? source->SourceAudioComponent : nullptr;
					FString typeString = source ? TEXT("Source") : TEXT("Continuous");

					if (continuousEmitter->bAffectsGroupedEAX)
					{
						int32 groupedEAXIndex = vaEmitterGetGroupedEAXIndex(vaEmitter);
						USoundSubmix* Submix = GetGroupedEAXSubmix(groupedEAXIndex);

						FColor color = bInBounds && Submix != NULL ? FColor::Green : FColor::Orange;

						FString submixStatus = Submix ? Submix->GetName() : TEXT("null");

						VAShowMessage(messageID, 0.0f, color,
							FString::Printf(TEXT("[VA] %s Emitter %d '%s': (%.1f, %.1f, %.1f), %s [groupedEAXIndex=%d] [submix=%s]"), *typeString, i, *continuousEmitter->GetActorNameOrLabel(), P.x, P.y, P.z, boundsStatus, groupedEAXIndex, *submixStatus));
					}
					else
					{
						FColor color = bInBounds ? FColor::Green : FColor::Orange;

						VAShowMessage(messageID, 0.0f, color,
							FString::Printf(TEXT("[VA] %s Emitter %d '%s': (%.1f, %.1f, %.1f), %s [No EAX]"), *typeString, i, *continuousEmitter->GetActorNameOrLabel(), P.x, P.y, P.z, boundsStatus));
					}
				}
			}

			// Per-target LPF from the main listener, the same filter each source applies to itself
			if (AVAListener* MessageListener = GetMainListener())
			{
				VAEmitter* ListenerVA = MessageListener->GetVAEmitter();

				if (ListenerVA)
				{
					for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
					{
						AVAEmitterBase* Target = RegisteredEmitters[i];

						uint64 messageID = VAMessageKey(MessageListener, EVAMessageSlot::TargetStatus, i);

						if (!Target->GetVAEmitter())
						{
							VAShowMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' target '%s' has no emitter. Ensure the target emitter is assigned to the same World"), *MessageListener->GetActorNameOrLabel(), *Target->GetActorNameOrLabel()));
							continue;
						}

						if (!vaEmitterHasRaytracedTarget(ListenerVA, Target->GetVAEmitter()))
						{
							VAShowMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has not raytraced the '%s' emitter yet"), *MessageListener->GetActorNameOrLabel(), *Target->GetActorNameOrLabel()));
							continue;
						}

						VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(ListenerVA, Target->GetVAEmitter());

						if (!lowPassFilter)
						{
							VAShowMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has raytraced the '%s' emitter, but has an invalid low pass filter"), *MessageListener->GetActorNameOrLabel(), *Target->GetActorNameOrLabel()));
							continue;
						}

						VAShowMessage(messageID, 0.0f, FColor::Green, FString::Printf(TEXT("[VA] '%s' filter: gainLF=%.2f  gainHF=%.2f"), *Target->GetActorNameOrLabel(), lowPassFilter->gainLF, lowPassFilter->gainHF));
					}
				}
			}

			// Per-grouped-EAX-zone reverb data (mirrors the settings OnReverbUpdated() sends to each preset - recomputed here purely for display).
			const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(World);
			int32 GroupedEAXCount = vaWorldGetGroupedEAXCount(World);

			if (GroupedEAX)
			{
				for (int32 i = 0; i < GroupedEAXCount; ++i)
				{
					const VAEAXReverb* EAX = GroupedEAX[i];

					uint64 messageID = VAMessageKey(this, EVAMessageSlot::GroupedEAX, i);
					if (!EAX)
					{
						VAShowMessage(messageID, 0.0f, FColor::Orange,
							FString::Printf(TEXT("[VA] GroupedEAX[%d]: invalid"), i));

						continue;
					}

					UVASubmixEffectDirectionalPanPreset* PanPreset = GroupedEAXPanPresets.IsValidIndex(i) ? GroupedEAXPanPresets[i] : nullptr;
					float pan = PanPreset ? PanPreset->GetSettings().Pan : 0.0f;

					VAShowMessage(messageID, 0.0f, FColor::Green,
						FString::Printf(TEXT("[VA] GroupedEAX[%d]: decayTime=%.2f gainLF=%.2f gainHF=%.2f pan=%.2f"), i, EAX->decayTime, EAX->gainLF, EAX->gainHF, pan));
				}
			}

			if (GroupedEAXSubmixes.Num() == 0)
			{
				VAShowMessage(VAMessageKey(this, EVAMessageSlot::NoGroupedEAX), 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] World '%s' has no Grouped EAX Submixes. Ensure at least one is added"), *GetActorNameOrLabel()));
			}


			if (AVAListener* CurrentMainListener = GetMainListener())
			{
				FVector ListenerPos = CurrentMainListener->GetActorLocation();

				int targetCount = RegisteredEmitters.Num();
				FColor color = targetCount == 0 ? FColor::Orange : FColor::Green;

				const wchar_t* plural = targetCount == 1 ? TEXT("target") : TEXT("targets");

				VAShowMessage(VAMessageKey(this, EVAMessageSlot::ListenerStatus), 0.0f, color, FString::Printf(TEXT("[VA] Listener '%s' has %d %s"), *CurrentMainListener->GetActorNameOrLabel(), targetCount, plural));

				VAEmitter* emitter = CurrentMainListener->GetVAEmitter();

				if (emitter && (vaEmitterGetAmbientOcclusionEnabled(emitter) || vaEmitterGetAmbientPermeationEnabled(emitter)))
				{
					// Wait for raytracing to complete at least once
					if (VALowPassFilter* ambientFilter = vaEmitterGetAmbientFilter(emitter))
					{
						VAShowMessage(VAMessageKey(this, EVAMessageSlot::AmbientFilter), 0.0f, FColor::Green, FString::Printf(TEXT("[VA] Ambient LPF: gainLF=%.2f  gainHF=%.2f"), ambientFilter->gainLF, ambientFilter->gainHF));
					}
				}
			}
			else
				VAShowMessage(VAMessageKey(this, EVAMessageSlot::ListenerStatus), 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] There is no main listener. Ensure a VAListener actor is placed and assigned to this world")));
			
			VAShowMessage(VAMessageKey(this, EVAMessageSlot::PrimitiveStatus), 0.0f, FColor::Cyan, FString::Printf(TEXT("[VA] Primitives: %d"), PrimitiveBindings.Num()));
			VAShowMessage(VAMessageKey(this, EVAMessageSlot::RaytracingTime), 0.0f, FColor::Cyan, FString::Printf(TEXT("[VA] Emitters: %d, Raytracing: %.2f ms"), vaWorldGetEmitterCount(World), vaWorldGetRaytracingTime(World)));

			if (ActorsWithInvalidMaterials.Num() > 0)
			{
				VAShowMessage(VAMessageKey(this, EVAMessageSlot::InvalidMaterials), 0.0f, FColor::Orange,
					FString::Printf(TEXT("[VA] %d actor(s) were not added to the world: %s. See Output Log for details."),
						ActorsWithInvalidMaterials.Num(), *FString::Join(ActorsWithInvalidMaterials, TEXT(", "))));
			}
		}
	}
}

USoundSubmix* AVAWorld::GetGroupedEAXSubmix(int32 Index) const
{
	return GroupedEAXSubmixes.IsValidIndex(Index) ? GroupedEAXSubmixes[Index] : nullptr;
}

USubmixEffectReverbPreset* AVAWorld::GetGroupedEAXPreset(int32 Index) const
{
	return GroupedEAXPresets.IsValidIndex(Index) ? GroupedEAXPresets[Index] : nullptr;
}

float AVAWorld::GetGroupedEAXPan(int32 Index) const
{
	UVASubmixEffectDirectionalPanPreset* PanPreset = GroupedEAXPanPresets.IsValidIndex(Index) ? GroupedEAXPanPresets[Index] : nullptr;
	return PanPreset ? PanPreset->GetSettings().Pan : 0.0f;
}

bool AVAWorld::AddEmitterToWorld(AVAEmitterBase* emitter)
{
	VAResult result = vaWorldAddEmitter(World, emitter->GetVAEmitter());

	switch (result)
	{
		case VA_SUCCESS:
			return true;

		case VA_ALREADY_EXISTS:
			VA_ERROR_NAMED(TEXT("Failed to register emitter '%s' as it is already added to this world."), *emitter->GetActorNameOrLabel());
			return false;

		case VA_WORLD_CONFLICT:
			VA_ERROR_NAMED(TEXT("Failed to register emitter '%s' as it is already added to a different world."), *emitter->GetActorNameOrLabel());
			return false;

		default:
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to register emitter '%s'."), *emitter->GetActorNameOrLabel());
			return false;
	}
}

bool AVAWorld::RegisterEmitter(AVAEmitterBase* emitter)
{
	if (!AddEmitterToWorld(emitter))
		return false;

	RegisteredEmitters.Add(emitter);

	// If no listener has begun play yet, SetCurrentListener wires this emitter up when one does
	if (MainListener)
		MainListener->AddTarget(emitter);

	return true;
}

void AVAWorld::UnregisterEmitter(AVAEmitterBase* emitter)
{
	RegisteredEmitters.Remove(emitter);
}

bool AVAWorld::RegisterListener(AVAListener* listener)
{
	if (!MainListener)
	{
		if (!SetCurrentListener(listener))
			return false;

		Listeners.Add(listener);
		return true;
	}

	Listeners.Add(listener);

	// A listener that begins play with bCurrent enabled takes over, e.g. a spawned player pawn's listener
	if (listener->IsCurrent())
	{
		VA_WARN_NAMED(TEXT("VAListener '%s' has bCurrent enabled, so it replaced '%s' as the current listener. Disable bCurrent on listeners that shouldn't take over when they begin play, and call MakeCurrent() on the one that should be used."), *listener->GetActorNameOrLabel(), *MainListener->GetActorNameOrLabel());
		SetCurrentListener(listener);
	}

	return true;
}

void AVAWorld::UnregisterListener(AVAListener* listener)
{
	Listeners.Remove(listener);

	if (MainListener != listener)
		return;

	if (Listeners.Num() > 0)
	{
		SetCurrentListener(Listeners[0]);
		return;
	}

	// Last listener in this world, so it keeps the shared handle and releases it in EndPlay
	MainListener = nullptr;
}

bool AVAWorld::SetCurrentListener(AVAListener* listener)
{
	if (MainListener == listener)
		return true;

	AVAListener* previous = MainListener;
	VAEmitter* sharedHandle = previous ? previous->GetVAEmitter() : nullptr;

	if (previous)
		previous->Deactivate();

	MainListener = listener;

	if (!listener->Activate(sharedHandle))
	{
		MainListener = nullptr;
		return false;
	}

	// Set up the emitters that began play before the first listener
	if (!sharedHandle)
		WirePendingTargets();

	return true;
}

void AVAWorld::ReleaseCurrentListener(AVAListener* listener)
{
	if (MainListener != listener)
	{
		listener->Deactivate();
		return;
	}

	for (AVAListener* other : Listeners)
	{
		if (other != listener)
		{
			SetCurrentListener(other);
			return;
		}
	}

	VA_WARN_NAMED(TEXT("VAListener '%s' is the only listener in this world, so it stays current."), *listener->GetActorNameOrLabel());
	listener->bCurrent = true;
}

void AVAWorld::WirePendingTargets()
{
	if (!MainListener)
		return;

	for (AVAEmitterBase* emitter : RegisteredEmitters)
	{
		if (emitter->GetVAEmitter())
			MainListener->AddTarget(emitter);
	}
}

void AVAWorld::OnOrphanedEmitterRemoved(VAEmitter* handle)
{
	AVAWorld* world = nullptr;

	if (OrphanedEmitters.RemoveAndCopyValue(handle, world))
		world->DeferEmitterDestroy(handle);
}

void AVAWorld::QueueEmitterEvents(AVAEmitterBase* emitter)
{
	PendingEventEmitters.AddUnique(emitter);
}

void AVAWorld::DestroyRemovedEmitters()
{
	for (VAEmitter* handle : PendingEmitterDestroys)
	{
		VAResult result = vaEmitterDestroy(handle);

		if (result != VA_SUCCESS)
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to destroy a removed emitter."));
	}

	PendingEmitterDestroys.Empty();
}

int32 AVAWorld::GetOrphanedEmitterCount() const
{
	int32 count = 0;

	for (const TPair<VAEmitter*, AVAWorld*>& pair : OrphanedEmitters)
		if (pair.Value == this)
			count++;

	return count;
}

void AVAWorld::ExportWorld()
{
	if (!World)
	{
		VA_WARN_NAMED(TEXT("Cannot export world (press Play first)"));
		return;
	}

	FString Path = FPaths::ProjectDir() + TEXT("vaudio_export.va");
	vaWorldExport(World, TCHAR_TO_UTF8(*Path));
}

void AVAWorld::InitialiseMaterials()
{
	for (UVAMaterialBase* Mat : Materials)
	{
		// Ignore null materials
		if (Mat)
		{
			Mat->ApplyToWorld(this);
		}
	}
}
