#include "VAudioWorld.h"
#include "VAudioEmitterBase.h"
#include "VAudioSource.h"
#include "VAudioContinuous.h"
#include "VAudioListener.h"
#include "VAudioMaterial.h"
#include "VAudioReverbConversion.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "AudioMixerBlueprintLibrary.h"

extern "C" {
#include "vaudio.h"
}

#include "VAConstants.h"

#include "VARawLog.h"
#include "VADebugMessageKeys.h"

// List of worlds that Material assets use to reverse-lookup the world(s) they belong to
TArray<TWeakObjectPtr<AVAudioWorld>> AVAudioWorld::RunningWorlds;

AVAudioWorld::AVAudioWorld()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Purely a visual aid, not the root - it must never move independently of WorldPosition/
	// WorldSize, so it's not selectable/movable via its own gizmo (see RefreshWorldBounds, which
	// re-derives its transform every time either property changes).
	WorldBounds = CreateDefaultSubobject<UVAudioWorldBoundsComponent>(TEXT("WorldBounds"));
	WorldBounds->SetupAttachment(Root);
	WorldBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorldBounds->SetGenerateOverlapEvents(false);
	WorldBounds->SetHiddenInGame(false);

	// Not RefreshWorldBounds() here - the constructor only ever sees CDO defaults for
	// WorldPosition/WorldSize (a placed instance's saved values haven't been applied yet at this
	// point), so it would just build the box from the wrong numbers. OnConstruction() below runs
	// after those values are loaded and is what actually sizes/places WorldBounds.
}

void AVAudioWorld::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RefreshWorldBounds();
}

void AVAudioWorld::RefreshWorldBounds()
{
	// WorldPosition/WorldSize are an absolute world-space min-corner + size (see InitializeVAWorld's
	// vaWorldSetPosition/vaWorldSetSize calls), completely independent of this actor's own transform
	// - moving/placing the actor is just for organisational convenience and must not affect them.
	// WorldBounds is parented to the actor though, so its transform has to cancel out the actor's
	// current transform to land on the same absolute location regardless of where the actor sits.
	WorldBounds->SetWorldLocation(WorldPosition + WorldSize * 0.5f);
	WorldBounds->SetWorldRotation(FQuat::Identity);
	WorldBounds->SetBoxExtent(WorldSize * 0.5f);
}

#if WITH_EDITOR
bool UVAudioWorldBoundsComponent::CanEditChange(const FProperty* InProperty) const
{
	// BoxExtent is re-derived from the owning AVAudioWorld's WorldSize every time it changes (see
	// RefreshWorldBounds) - greyed out rather than hidden, since a native component's own details
	// sub-tree isn't reachable via the owning actor's UCLASS(HideCategories=...). Transform is left
	// editable even though RefreshWorldBounds also overwrites it, purely so it doesn't look broken.
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

	// Null before BeginPlay (or after EndPlay) - editing properties on a placed actor in the
	// editor (not PIE) hits this every time.
	if (!World)
		return;

	vaWorldSetPosition(World, vaVectorCreate(
		(float)WorldPosition.X,
		(float)WorldPosition.Y,
		(float)WorldPosition.Z));
	vaWorldSetSize(World, vaVectorCreate(
		(float)WorldSize.X,
		(float)WorldSize.Y,
		(float)WorldSize.Z));
	vaWorldSetInverseSpeedOfSound(World, 1.0f / FMath::Max(0.0001f, SpeedOfSound));
	vaWorldSetMetersPerUnit(World, FMath::Max(0.0001f, MetersPerUnit));
	vaWorldSetWorldIsIndoors(World, bIsIndoors);
	vaWorldSetEpsilon(World, Epsilon);
	vaWorldSetEmittersOutsideTheWorldAreMuffled(World, bEmittersOutsideTheWorldAreMuffled);
	vaWorldSetWorkItemCount(World, FMath::Max(1, WorkItemCount));
	vaWorldSetMaximumConcurrencyLevel(World, FMath::Max(1, MaximumConcurrencyLevel));
	vaWorldSetPendingShutdown(World, bPendingShutdown);
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
#endif

void AVAudioWorld::BeginPlay()
{
	Super::BeginPlay();

	RunningWorlds.Add(this);

	InitializeVAWorld();
}

void AVAudioWorld::InitializeVAWorld()
{
	// Already initialised, all is good
	if (World)
		return;

	World = vaWorldCreate();
	vaWorldSetLogMemoryAllocationWarnings(World, true);
	vaWorldSetCoordinateSystem(World, VACoordinateSystemUnreal);
	vaWorldSetLogCallback(World, &VASdkLogCallback);
	vaWorldSetPosition(World, vaVectorCreate(
		(float)WorldPosition.X,
		(float)WorldPosition.Y,
		(float)WorldPosition.Z));
	vaWorldSetSize(World, vaVectorCreate(
		(float)WorldSize.X,
		(float)WorldSize.Y,
		(float)WorldSize.Z));
	vaWorldSetInverseSpeedOfSound(World, 1.0f / FMath::Max(0.0001f, SpeedOfSound));
	vaWorldSetMetersPerUnit(World, FMath::Max(0.0001f, MetersPerUnit));
	vaWorldSetWorldIsIndoors(World, bIsIndoors);
	vaWorldSetEpsilon(World, Epsilon);
	vaWorldSetEmittersOutsideTheWorldAreMuffled(World, bEmittersOutsideTheWorldAreMuffled);
	vaWorldSetWorkItemCount(World, FMath::Max(1, WorkItemCount));
	vaWorldSetMaximumConcurrencyLevel(World, FMath::Max(1, MaximumConcurrencyLevel));
	vaWorldSetPendingShutdown(World, bPendingShutdown);
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

	int32 GroupedEAXCount = GroupedEAXSubmixes.Num();
	vaWorldSetMaximumGroupedEAXCount(World, GroupedEAXCount);

	for (int32 i = 0; i < GroupedEAXCount; i++)
	{
		USoundSubmix* Sub = GroupedEAXSubmixes[i];
		USubmixEffectReverbPreset* Preset = NewObject<USubmixEffectReverbPreset>(this);

		if (Sub)
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, Sub, Preset);
		else
			DisplayDebugWarning(VANullGroupedEAXMessage, TEXT("[VA] World '%s' has a null grouped EAX submix at index %d. Please assign a submix"), *GetActorNameOrLabel(), i);

		GroupedEAXPresets.Add(Preset);
	}

	ApplyMaterials();
	ScanAndAddPrimitives();
}

void AVAudioWorld::ApplyGroupedEAXReverb()
{
	// Wait for raytracing to run at least once
	if (vaWorldGetInitialising(World))
		return;

	const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(World);
	int32 Count = vaWorldGetGroupedEAXCount(World);

	for (int32 i = 0; i < Count; ++i)
	{
		USubmixEffectReverbPreset* Preset = GetGroupedEAXPreset(i);

		// ALready logged above
		if (!Preset)
			continue;

		const VAEAXReverb* EAX = GroupedEAX[i];

		FSubmixEffectReverbSettings settings = VAEAXReverbToSubmixSettings(EAX);
		Preset->SetSettings(settings);
	}
}

void AVAudioWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	RunningWorlds.RemoveSingleSwap(this);

	GroupedEAXPresets.Empty();

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
							GEngine->AddOnScreenDebugMessage(errorMessageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source Emitter %d '%s' cannot play its sound as it is not a target of the listener emitter"), i, *continuousEmitter->GetActorNameOrLabel()));
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

						GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Green,
							FString::Printf(TEXT("[VA] GroupedEAX[%d]: decayTime=%.2f gainLF=%.2f gainHF=%.2f"), i, EAX->decayTime, EAX->gainLF, EAX->gainHF));
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

		// HACK - when the listener initialises before the world, it'll initialise its targets (e.g. VAudioSource), which calls this GetMainListener() from its own TryInitializeEmitter, which
		//  then calls the listener's TryInitializeEmitter again below, but luckily it exits early rather than stack-overflows, because the listener's Emitter is already set.
		//  However, this allows actors to be defined in any order / hierarchy
		bool pass = Listener->TryInitializeEmitter();
		check(pass);

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

void AVAudioWorld::ApplyMaterials()
{
	int32 AppliedCount = 0;

	for (UVAudioMaterialAssetBase* Mat : Materials)
	{
		if (Mat)
		{
			Mat->ApplyToWorld(this);
			++AppliedCount;
		}
	}
}
