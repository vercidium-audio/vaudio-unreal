#include "VAudioWorld.h"
#include "VAudioSubmixEffectDirectionalPan.h"
#include "VAudioEmitterBase.h"
#include "VAudioSource.h"
#include "VAudioContinuous.h"
#include "VAudioListener.h"
#include "VAudioMaterial.h"
#include "VAudioReverbConversion.h"
#include "VAConstants.h"
#include "VARawLog.h"
#include "VADebugMessageKeys.h"

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
TArray<TWeakObjectPtr<AVAudioWorld>> AVAudioWorld::RunningWorlds;

AVAudioWorld::AVAudioWorld()
{
	PrimaryActorTick.bCanEverTick = true;

	// Allow components to be attached to this AudioWorld
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Display the world bounds in the editor via a component
	WorldBounds = CreateDefaultSubobject<UVAudioWorldBoundsComponent>(TEXT("WorldBounds"));
	WorldBounds->SetupAttachment(Root);
	WorldBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorldBounds->SetGenerateOverlapEvents(false);
	WorldBounds->SetHiddenInGame(false);
}

void AVAudioWorld::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RefreshWorldBounds();
}

void AVAudioWorld::RefreshWorldBounds()
{
	// Convert position + size to location + extent
	WorldBounds->SetWorldLocation(WorldPosition + WorldSize * 0.5f);
	WorldBounds->SetBoxExtent(WorldSize * 0.5f);

	// No rotation
	WorldBounds->SetWorldRotation(FQuat::Identity);
}

void AVAudioWorld::UpdateVAWorld()
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
	vaWorldSetWorldIsIndoors(World, bIsIndoors);
	vaWorldSetEpsilon(World, Epsilon);
	vaWorldSetEmittersOutsideTheWorldAreMuffled(World, bEmittersOutsideTheWorldAreMuffled);

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
bool UVAudioWorldBoundsComponent::CanEditChange(const FProperty* InProperty) const
{
	// Grey out the readonly Box Extents fields
	static const FName BoxExtentPropertyName(TEXT("BoxExtent"));

	if (InProperty && InProperty->GetFName() == BoxExtentPropertyName)
		return false;

	return Super::CanEditChange(InProperty);
}

void AVAudioWorld::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
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

void AVAudioWorld::BeginPlay()
{
	Super::BeginPlay();

	RunningWorlds.Add(this);

	InitializeVAWorld();
}

// This can also be called by other VA emitters, as they might initialise first (actor init order not guaranteed)
void AVAudioWorld::InitializeVAWorld()
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
		USubmixEffectDirectionalPanPreset* PanPreset = NewObject<USubmixEffectDirectionalPanPreset>(this);

		if (Sub)
		{
			// Pan effect must be added after the reverb preset so it operates on the wet reverb
			// output rather than dry input - see directional_reverb_plan.md's "Effect chain
			// insertion API" note.
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, Sub, Preset);
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, Sub, PanPreset);
		}
		else
			DisplayDebugWarning(VANullGroupedEAXMessage, TEXT("[VA] World '%s' has a null grouped EAX submix at index %d. Please assign a submix"), *GetActorNameOrLabel(), i);

		GroupedEAXPresets.Add(Preset);
		GroupedEAXPanPresets.Add(PanPreset);
	}

	InitialiseMaterials();
	ScanAndAddPrimitives();
}

void AVAudioWorld::ApplyGroupedEAXReverb()
{
	// Wait for raytracing to run at least once
	if (vaWorldGetInitialising(World))
		return;

	const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(World);
	int32 Count = vaWorldGetGroupedEAXCount(World);

	AVAudioListener* Listener = GetMainListener();
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

		// Direction == nullptr means "no entry for this emitter" (vaudio.h:384) - e.g. the listener
		// hasn't been raytraced against this zone yet, or lacks hasRelativeReverb. Leave pan holding
		// its last value rather than forcing it to 0 every such tick.
		if (ListenerVA)
		{
			VAVector* Direction = vaEAXReverbGetRelativeDirection(EAX, ListenerVA);

			if (Direction)
			{
				FVector directionUnreal(Direction->x, Direction->y, Direction->z);

				// Magnitude IS strength (OpenAL Soft EAX style) - do not normalize.
				float pan = FVector::DotProduct(directionUnreal, Listener->GetActorRightVector());
				pan = FMath::Clamp(pan, -1.0f, 1.0f);

				if (USubmixEffectDirectionalPanPreset* PanPreset = GroupedEAXPanPresets.IsValidIndex(i) ? GroupedEAXPanPresets[i] : nullptr)
					PanPreset->SetPan(pan);
			}
		}
	}
}

// Prefer whichever controller is actually steering this listener's point of view, so the debug
// camera's pitch/yaw matches what's heard regardless of controller type (player, AI, etc):
//  1. A controller possessing the listener's attach-parent pawn (how AVAudioListener is normally
//     set up - see UVAudioListenerComponent - e.g. a first/third-person character, or an AI pawn).
//  2. The first local player controller, for listeners not attached to a possessed pawn.
//  3. The listener actor's own rotation, if no controller is available at all.
static FRotator GetListenerControlRotation(AVAudioListener* Listener)
{
	if (APawn* Pawn = Cast<APawn>(Listener->GetAttachParentActor()))
		if (AController* Controller = Pawn->GetController())
			return Controller->GetControlRotation();

	if (APlayerController* PlayerController = Listener->GetWorld()->GetFirstPlayerController())
		return PlayerController->GetControlRotation();

	return Listener->GetActorRotation();
}

void AVAudioWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	RunningWorlds.RemoveSingleSwap(this);

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

void AVAudioWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (World)
	{
		AVAudioListener* mainListener = GetMainListener();

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

			// SetDryOutputEnabled() is only implemented on AVAudioSource - other AVAudioEmitterBase
			// subclasses don't have dry output to toggle here.
			for (AVAudioEmitterBase* Emitter : RegisteredEmitters)
				if (AVAudioSource* ConcreteEmitter = Cast<AVAudioSource>(Emitter))
					ConcreteEmitter->SetDryOutputEnabled(bDryEnabled);
		}

		if (GEngine)
		{
			// GEngine draws these on-screen messages in the reverse of the order they're added
			// each tick (last call ends up at the top), so this block is sequenced bottom-up:
			// whatever should appear highest on screen is called LAST.

			// Per-emitter position and world-bounds check
			for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
			{
				// This display logic (AVAudioListener vs. AVAudioContinuous/AVAudioSource) is only
				// meaningful for raytracing-target emitters - AVAudioRelativeSource/AVAudioAmbientSource
				// don't raytrace and get no status line here.
				AVAudioEmitterBase* baseEmitter = RegisteredEmitters[i];
				AVAudioListener* listener = Cast<AVAudioListener>(baseEmitter);
				AVAudioContinuous* continuousEmitter = listener ? nullptr : Cast<AVAudioContinuous>(baseEmitter);

				if (!listener && !continuousEmitter)
					continue;

				VAEmitter* vaEmitter = baseEmitter->GetVAEmitter();

				uint64 messageID = VAEmitterStatus + baseEmitter->GetEmitterIndex();

				// Registered emitters can still have a null VAEmitter* if TryInitializeEmitter()
				// hasn't completed yet (e.g. this world's own BeginPlay ran after theirs) - skip
				// until it catches up on a later Tick.
				if (!vaEmitter)
				{
					GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange,
						FString::Printf(TEXT("[VA] Emitter %d '%s': initialising"), i, *baseEmitter->GetActorNameOrLabel()));

					continue;
				}

				bool bInBounds = vaEmitterGetWithinWorldBounds(vaEmitter);
				VAVector P = vaEmitterGetPosition(vaEmitter);

				const wchar_t* boundsStatus = bInBounds ? TEXT("[in bounds]") : TEXT("[out of bounds]");


				if (listener)
				{
					FColor color = bInBounds ? FColor::Green : FColor::Orange;

					GEngine->AddOnScreenDebugMessage(messageID, 0.0f, color,
						FString::Printf(TEXT("[VA] Listener Emitter %d '%s': (%.1f, %.1f, %.1f) %s"), i, *listener->GetActorNameOrLabel(), P.x, P.y, P.z, boundsStatus));
				}
				else
				{
					AVAudioSource* source = Cast<AVAudioSource>(continuousEmitter);

					// If it's a source (not continuous), ensure its audio component is configured correctly
					if (source)
					{
						if (!source->SourceSound)
						{
							uint64 errorMessageID = VAEmitterMessageBase + i * VAEmitterMessageStride + VAEmitterSourceStatus;
							GEngine->AddOnScreenDebugMessage(errorMessageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source Emitter %d '%s' has no sound file assigned"), i, *continuousEmitter->GetActorNameOrLabel()));
						}
						else if (!source->SourceSound->AttenuationSettings)
						{
							uint64 errorMessageID = VAEmitterMessageBase + i * VAEmitterMessageStride + VAEmitterAttenuationStatus;
							GEngine->AddOnScreenDebugMessage(errorMessageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source Emitter %d '%s' has no Sound Attenuation - it will not fall off with distance"), i, *continuousEmitter->GetActorNameOrLabel()));
						}
						else if (!source->SourceAudioComponent) // SourceAudioComponent is set when it actually plays
						{
							uint64 errorMessageID = VAEmitterMessageBase + i * VAEmitterMessageStride + VAEmitterSourceStatus;
							GEngine->AddOnScreenDebugMessage(errorMessageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source Emitter %d '%s' has not played its sound yet"), i, *continuousEmitter->GetActorNameOrLabel()));
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

						GEngine->AddOnScreenDebugMessage(messageID, 0.0f, color,
							FString::Printf(TEXT("[VA] %s Emitter %d '%s': (%.1f, %.1f, %.1f), %s [groupedEAXIndex=%d] [submix=%s]"), *typeString, i, *continuousEmitter->GetActorNameOrLabel(), P.x, P.y, P.z, boundsStatus, groupedEAXIndex, *submixStatus));
					}
					else
					{
						FColor color = bInBounds ? FColor::Green : FColor::Orange;

						GEngine->AddOnScreenDebugMessage(messageID, 0.0f, color,
							FString::Printf(TEXT("[VA] %s Emitter %d '%s': (%.1f, %.1f, %.1f), %s [No EAX]"), *typeString, i, *continuousEmitter->GetActorNameOrLabel(), P.x, P.y, P.z, boundsStatus));
					}
				}
			}

			// Per-target LPF applied by the main listener (mirrors the filter AVAudioListener::TickTypeSpecific()
			// applies to each target's source - recomputed here purely for display).
			if (AVAudioListener* MessageListener = GetMainListener())
			{
				VAEmitter* ListenerVA = MessageListener->GetVAEmitter();

				if (ListenerVA)
				{
					for (int32 i = 0; i < MessageListener->TargetEmitters.Num(); ++i)
					{
						AVAudioEmitterBase* Target = MessageListener->TargetEmitters[i];

						uint64 messageID = VAEmitterMessageBase + MessageListener->GetEmitterIndex() * VAEmitterMessageStride + VAEmitterTargetStatus + i;

						if (!Target)
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has a null target"), *MessageListener->GetActorNameOrLabel()));
							continue;
						}

						if (Target == MessageListener)
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has itself in its own Target Emitters list"), *MessageListener->GetActorNameOrLabel()));
							continue;
						}

						if (!Target->GetVAEmitter())
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' target '%s' has no emitter. Ensure the target emitter is assigned to the same World"), *MessageListener->GetActorNameOrLabel(), *Target->GetActorNameOrLabel()));
							continue;
						}

						if (!vaEmitterHasRaytracedTarget(ListenerVA, Target->GetVAEmitter()))
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has not raytraced the '%s' emitter yet"), *MessageListener->GetActorNameOrLabel(), *Target->GetActorNameOrLabel()));
							continue;
						}

						VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(ListenerVA, Target->GetVAEmitter());

						if (!lowPassFilter)
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has raytraced the '%s' emitter, but has an invalid low pass filter"), *MessageListener->GetActorNameOrLabel(), *Target->GetActorNameOrLabel()));
							continue;
						}

						GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Green, FString::Printf(TEXT("[VA] '%s' filter: gainLF=%.2f  gainHF=%.2f"), *Target->GetActorNameOrLabel(), lowPassFilter->gainLF, lowPassFilter->gainHF));
					}
				}
			}

			// Per-grouped-EAX-zone reverb data (mirrors the settings ApplyGroupedEAXReverb() sends
			// to each preset - recomputed here purely for display).
			if (vaWorldGetInitialising(World) == false)
			{
				const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(World);
				int32 GroupedEAXCount = vaWorldGetGroupedEAXCount(World);

				if (GroupedEAX)
				{
					for (int32 i = 0; i < GroupedEAXCount; ++i)
					{
						const VAEAXReverb* EAX = GroupedEAX[i];

						uint64 messageID = VAGroupedEAXMessageBase + i;
						if (!EAX)
						{
							GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange,
								FString::Printf(TEXT("[VA] GroupedEAX[%d]: invalid"), i));

							continue;
						}

						USubmixEffectDirectionalPanPreset* PanPreset = GroupedEAXPanPresets.IsValidIndex(i) ? GroupedEAXPanPresets[i] : nullptr;
						float pan = PanPreset ? PanPreset->GetSettings().Pan : 0.0f;

						GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Green,
							FString::Printf(TEXT("[VA] GroupedEAX[%d]: decayTime=%.2f gainLF=%.2f gainHF=%.2f pan=%.2f"), i, EAX->decayTime, EAX->gainLF, EAX->gainHF, pan));
					}
				}
			}

			if (GroupedEAXSubmixes.Num() == 0)
			{
				GEngine->AddOnScreenDebugMessage(VANoGroupedEAXMessage, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] World '%s' has no Grouped EAX Submixes. Ensure at least one is added"), *GetActorNameOrLabel()));
			}


			if (AVAudioListener* CurrentMainListener = GetMainListener())
			{
				FVector ListenerPos = CurrentMainListener->GetActorLocation();

				int targetCount = CurrentMainListener->TargetEmitters.Num();
				FColor color = targetCount == 0 ? FColor::Orange : FColor::Green;

				const wchar_t* plural = targetCount == 1 ? TEXT("target") : TEXT("targets");

				GEngine->AddOnScreenDebugMessage(VAListenerStatusMessage, 0.0f, color, FString::Printf(TEXT("[VA] Listener '%s' has %d %s"), *CurrentMainListener->GetActorNameOrLabel(), targetCount, plural));

				VAEmitter* emitter = CurrentMainListener->GetVAEmitter();

				if (emitter && (vaEmitterGetAmbientOcclusionEnabled(emitter) || vaEmitterGetAmbientPermeationEnabled(emitter)))
				{
					// Wait for raytracing to complete at least once
					if (VALowPassFilter* ambientFilter = vaEmitterGetAmbientFilter(emitter))
					{
						GEngine->AddOnScreenDebugMessage(VAAmbientFilterMessage, 0.0f, FColor::Green, FString::Printf(TEXT("[VA] Ambient LPF: gainLF=%.2f  gainHF=%.2f"), ambientFilter->gainLF, ambientFilter->gainHF));
					}
				}
			}
			else
				GEngine->AddOnScreenDebugMessage(VAListenerStatusMessage, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] There is no main listener. Ensure an AVAudioListener actor is placed and assigned to a World")));
			
			GEngine->AddOnScreenDebugMessage(VAPrimitiveStatusMessage, 0.0f, FColor::Cyan, FString::Printf(TEXT("[VA] Primitives: prisms=%d spheres=%d capsules=%d meshes=%d"), PrismPrimitives.Num(), SpherePrimitives.Num(), CapsulePrimitives.Num(), MeshPrimitives.Num()));
			GEngine->AddOnScreenDebugMessage(VARaytracingTimeMessage, 0.0f, FColor::Cyan, FString::Printf(TEXT("[VA] Emitters: %d, Raytracing: %.2f ms"), vaWorldGetEmitterCount(World), vaWorldGetRaytracingTime(World)));

			// Called last (renders at the top) since these actors are silently missing from
			// raytracing entirely - the most likely warning to be missed otherwise. See
			// ScanAndAddPrimitives()/TryAddPrimitive(), which log the specific reason per actor.
			if (ActorsWithInvalidMaterials.Num() > 0)
			{
				GEngine->AddOnScreenDebugMessage(VAInvalidMaterialsMessage, 0.0f, FColor::Orange,
					FString::Printf(TEXT("[VA] %d actor(s) were not added to the world: %s. See Output Log for details."),
						ActorsWithInvalidMaterials.Num(), *FString::Join(ActorsWithInvalidMaterials, TEXT(", "))));
			}
		}
	}
}

USoundSubmix* AVAudioWorld::GetGroupedEAXSubmix(int32 Index) const
{
	return GroupedEAXSubmixes.IsValidIndex(Index) ? GroupedEAXSubmixes[Index] : nullptr;
}

USubmixEffectReverbPreset* AVAudioWorld::GetGroupedEAXPreset(int32 Index) const
{
	return GroupedEAXPresets.IsValidIndex(Index) ? GroupedEAXPresets[Index] : nullptr;
}

void AVAudioWorld::RegisterEmitter(AVAudioEmitterBase* Emitter)
{
	RegisteredEmitters.AddUnique(Emitter);
	Emitter->SetEmitterIndex(RegisteredEmitters.Find(Emitter));

	if (AVAudioListener* ConcreteListener = Cast<AVAudioListener>(Emitter))
	{
		if (MainListener.IsValid() && MainListener.Get() != ConcreteListener)
		{
			DisplayDebugWarning(VADuplicateListenerMessage, TEXT("[VA] World '%s' has multiple listeners: '%s' and '%s'. Only one listener should exist per world."),
				*GetActorNameOrLabel(), *MainListener->GetActorNameOrLabel(), *Emitter->GetActorNameOrLabel());
		}
		else
		{
			MainListener = ConcreteListener;
		}
	}
}

void AVAudioWorld::UnregisterEmitter(AVAudioEmitterBase* Emitter)
{
	RegisteredEmitters.Remove(Emitter);
	Emitter->SetEmitterIndex(-1);

	// Removing shifts every later emitter's position in RegisteredEmitters - keep EmitterIndex
	// (used to build on-screen debug message keys, see VADebugMessageKeys.h) in sync so indices
	// stay dense and no two registered emitters ever share a key.
	for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
		RegisteredEmitters[i]->SetEmitterIndex(i);

	if (MainListener.Get() == Emitter)
		MainListener = nullptr;

	// If the world was removed first, no need to invoke vaWorldRemoveEmitter
	if (World)
		vaWorldRemoveEmitter(World, Emitter->GetVAEmitter());
}

AVAudioListener* AVAudioWorld::GetMainListener()
{
	if (MainListener.IsValid())
		return MainListener.Get();

	// Not registered yet - this happens when the AVAudioListener is a child actor of something
	// whose own BeginPlay (and therefore the listener's) hasn't run yet (actor BeginPlay order
	// isn't guaranteed - see TryInitializeEmitter()). Find it in the level and force it to
	// initialise now, same as AVAudioListener::InitializeTypeSpecific() does for its targets.
	UWorld* UEWorld = GetWorld();
	if (!UEWorld)
		return nullptr;

	for (TActorIterator<AVAudioListener> ActorIt(UEWorld); ActorIt; ++ActorIt)
	{
		AVAudioListener* Listener = *ActorIt;

		if (Listener->AudioWorld != this)
			continue;

		// The listener will initialise its targets, which will fail if the listener isn't set, so MainListener needs to be set here
		MainListener = Listener;
		break;
	}

	return MainListener.Get();
}

void AVAudioWorld::ExportWorld()
{
	if (!World)
	{
		VALog(L"Cannot export world (press Play first).");
		return;
	}

	FString Path = FPaths::ProjectDir() + TEXT("vaudio_export.va");
	vaWorldExport(World, TCHAR_TO_UTF8(*Path));
}

void AVAudioWorld::InitialiseMaterials()
{
	for (UVAudioMaterialAssetBase* Mat : Materials)
	{
		// Ignore null materials
		if (Mat)
		{
			Mat->ApplyToWorld(this);
		}
	}
}
