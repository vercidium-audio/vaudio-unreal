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

	// Logging
	vaWorldSetLogCallback(World, &VASdkLogCallback);
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
	ScanAndAddPrimitives();
}

void AVAWorld::ApplyGroupedEAXReverb()
{
	const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(World);
	int32 Count = vaWorldGetGroupedEAXCount(World);

	AVAListener* Listener = GetMainListener();
	VAEmitter* ListenerVA = Listener ? Listener->GetVAEmitter() : nullptr;

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
				FVector directionUnreal(Direction->x, Direction->y, Direction->z);

				// Magnitude IS strength (OpenAL Soft EAX style) - do not normalize.
				float pan = FVector::DotProduct(directionUnreal, Listener->GetActorRightVector());
				pan = FMath::Clamp(pan, -1.0f, 1.0f);

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
		vaWorldWait(World);
		DestroyPrimitives();
		vaWorldDestroy(World);
		World = nullptr;
	}
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
		ApplyGroupedEAXReverb();

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
			for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
			{
				AVAEmitterBase* baseEmitter = RegisteredEmitters[i];
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

			// Per-target LPF applied by the main listener (mirrors the filter AVAListener::TickTypeSpecific()
			// applies to each target's source - recomputed here purely for display).
			if (AVAListener* MessageListener = GetMainListener())
			{
				VAEmitter* ListenerVA = MessageListener->GetVAEmitter();

				if (ListenerVA)
				{
					for (int32 i = 0; i < MessageListener->TargetEmitters.Num(); ++i)
					{
						AVAEmitterBase* Target = MessageListener->TargetEmitters[i];

						uint64 messageID = VAMessageKey(MessageListener, EVAMessageSlot::TargetStatus, i);

						if (!Target)
						{
							VAShowMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has a null target"), *MessageListener->GetActorNameOrLabel()));
							continue;
						}

						if (Target == MessageListener)
						{
							VAShowMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has itself in its own Target Emitters list"), *MessageListener->GetActorNameOrLabel()));
							continue;
						}

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

			// Per-grouped-EAX-zone reverb data (mirrors the settings ApplyGroupedEAXReverb() sends
			// to each preset - recomputed here purely for display).
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

				int targetCount = CurrentMainListener->TargetEmitters.Num();
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
				VAShowMessage(VAMessageKey(this, EVAMessageSlot::ListenerStatus), 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] There is no main listener. Ensure an AVAListener actor is placed and assigned to a World")));
			
			VAShowMessage(VAMessageKey(this, EVAMessageSlot::PrimitiveStatus), 0.0f, FColor::Cyan, FString::Printf(TEXT("[VA] Primitives: prisms=%d spheres=%d capsules=%d meshes=%d"), PrismPrimitives.Num(), SpherePrimitives.Num(), CapsulePrimitives.Num(), MeshPrimitives.Num()));
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

void AVAWorld::RegisterEmitter(AVAEmitterBase* Emitter)
{
	RegisteredEmitters.AddUnique(Emitter);
	Emitter->SetEmitterIndex(RegisteredEmitters.Find(Emitter));

	if (AVAListener* ConcreteListener = Cast<AVAListener>(Emitter))
	{
		if (MainListener.IsValid() && MainListener.Get() != ConcreteListener)
		{
			VA_WARN_NAMED(TEXT("Has multiple listeners: '%s' and '%s'. Only one listener should exist per world"), *MainListener->GetActorNameOrLabel(), *Emitter->GetActorNameOrLabel());
		}
		else
		{
			MainListener = ConcreteListener;
		}
	}
}

void AVAWorld::UnregisterEmitter(AVAEmitterBase* Emitter)
{
	RegisteredEmitters.Remove(Emitter);
	Emitter->SetEmitterIndex(-1);

	for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
		RegisteredEmitters[i]->SetEmitterIndex(i);

	if (MainListener.Get() == Emitter)
		MainListener = nullptr;

	// If the world was removed first, no need to invoke vaWorldRemoveEmitter
	if (World)
		vaWorldRemoveEmitter(World, Emitter->GetVAEmitter());
}

AVAListener* AVAWorld::GetMainListener()
{
	if (MainListener.IsValid())
		return MainListener.Get();

	UWorld* UEWorld = GetWorld();
	if (!UEWorld)
		return nullptr;

	for (TActorIterator<AVAListener> ActorIt(UEWorld); ActorIt; ++ActorIt)
	{
		AVAListener* Listener = *ActorIt;

		if (Listener->AudioWorld != this)
			continue;

		// The listener will initialise its targets, which will fail if the listener isn't set, so MainListener needs to be set here
		MainListener = Listener;
		break;
	}

	return MainListener.Get();
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
