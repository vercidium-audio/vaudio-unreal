#include "VAWorld.h"
#include "VAEngineCompat.h"
#include "VAWorldSubsystem.h"
#include "VASubmixEffectDirectionalPan.h"
#include "VAEmitter.h"
#include "VASource.h"
#include "VASourceRelative.h"
#include "VASourceLeech.h"
#include "VAListener.h"
#include "VAMaterial.h"
#include "VAReverbConversion.h"
#include "VASubmixSend.h"
#include "VAConstants.h"
#include "VAEmitterHandle.h"
#include "VAEditorSprite.h"
#include "VALog.h"
#include "VaudioUnrealModule.h"

#include "EngineUtils.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"
#include "HAL/IConsoleManager.h"
#include "Components/BillboardComponent.h"
#include "Engine/StaticMeshActor.h"
#include "AudioMixerBlueprintLibrary.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

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

	// Allow components to be attached to this VAWorld
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Display the world bounds in the editor via a component
	WorldBounds = CreateDefaultSubobject<UVAWorldBoundsComponent>(TEXT("WorldBounds"));
	WorldBounds->SetupAttachment(Root);
	WorldBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WorldBounds->SetGenerateOverlapEvents(false);
	WorldBounds->SetHiddenInGame(false);

	// The box stays axis-aligned and unscaled whatever the actor's rotation and scale, so RefreshWorldBounds places it in world space
	WorldBounds->SetUsingAbsoluteLocation(true);
	WorldBounds->SetUsingAbsoluteRotation(true);
	WorldBounds->SetUsingAbsoluteScale(true);

#if WITH_EDITORONLY_DATA
	if (UBillboardComponent* sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite")))
	{
		sprite->SetupAttachment(Root);
		VASetEditorSprite(sprite, GetClass());
	}
#endif

	Root->TransformUpdated.AddUObject(this, &AVAWorld::OnRootMoved);
}

void AVAWorld::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	RefreshWorldBounds();
}

void AVAWorld::RefreshWorldBounds()
{
	// Convert min corner + size to location + extent
	WorldBounds->SetWorldLocation(GetBoundsPosition() + BoundsSize * 0.5f);
	WorldBounds->SetBoxExtent(BoundsSize * 0.5f);

	if (WorldBounds->ShapeColor != BoundsColor)
	{
		WorldBounds->ShapeColor = BoundsColor;
		WorldBounds->MarkRenderStateDirty();
	}
}

void AVAWorld::OnRootMoved(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	if (World)
		FollowBounds();

	RefreshWorldBounds();
}

FVector AVAWorld::GetBoundsTarget() const
{
	if (BoundsFollow != EVABoundsFollow::Listener)
		return GetActorLocation();

	return MainListener ? MainListener->GetActorLocation() - BoundsSize * 0.5 : BoundsPosition;
}

bool AVAWorld::IsWithinBounds(const FVector& Position) const
{
	const FVector min = GetBoundsPosition();
	const FVector max = min + BoundsSize;
	return Position.X >= min.X && Position.Y >= min.Y && Position.Z >= min.Z && Position.X <= max.X && Position.Y <= max.Y && Position.Z <= max.Z;
}

bool AVAWorld::IsOutsideBounds(const FVector& position, bool bClampPosition) const
{
	// A clamped emitter is raytraced from inside the bounds
	return !bClampPosition && !IsWithinBounds(position);
}

bool AVAWorld::IsSilencedByBounds(const FVector& position, bool bClampPosition) const
{
	// With the setting off the SDK hears an outside emitter clearly
	return bEmittersOutsideTheWorldAreMuffled && IsOutsideBounds(position, bClampPosition);
}

void AVAWorld::PlaceBounds()
{
	BoundsPosition = GetBoundsTarget();
	vaWorldSetPositionUnreal(World, BoundsPosition);
	RefreshWorldBounds();
}

void AVAWorld::FollowBounds()
{
	FVector target = GetBoundsTarget();

	if (target != BoundsPosition && FVector::DistSquared(target, BoundsPosition) >= FMath::Square(BoundsUpdateDistance))
		PlaceBounds();
}

FVector AVAWorld::ClampBoundsSize(const FVector& size) const
{
	if (BoundsFollow != EVABoundsFollow::Listener)
		return size;

	return size.ComponentMax(FVector(BoundsUpdateDistance * 2.0f));
}

void AVAWorld::ReclampBoundsSize()
{
	if (ClampBoundsSize(BoundsSize) != BoundsSize)
		SetBoundsSize(BoundsSize);
}

void AVAWorld::SetBoundsSize(const FVector& size)
{
	BoundsSize = ClampBoundsSize(size);
	RefreshWorldBounds();

	if (!World)
		return;

	vaWorldSetSizeUnreal(World, BoundsSize);

	// Stay centred on the listener, where a fixed box keeps its minimum corner
	if (BoundsFollow == EVABoundsFollow::Listener)
		PlaceBounds();
}

void AVAWorld::SetBoundsFollow(EVABoundsFollow follow)
{
	BoundsFollow = follow;
	ReclampBoundsSize();

	if (World)
		PlaceBounds();
}

void AVAWorld::SetBoundsUpdateDistance(float distance)
{
	BoundsUpdateDistance = FMath::Max(0.0f, distance);
	ReclampBoundsSize();
}

void AVAWorld::UpdateVAWorld()
{
	vaWorldSetName(World, TCHAR_TO_UTF8(*(WorldName.IsEmpty() ? VAActorName(this) : WorldName)));

	// Rendering
	vaWorldSetRenderingEnabled(World, bRenderingEnabled);
	vaWorldSetCameraSpeed(World, CameraSpeed);
	vaWorldSetVisualisationScale(World, VisualisationScale);

	// World bounds. BoundsUpdateDistance can be written directly from Blueprint, so clamp again here
	BoundsSize = ClampBoundsSize(BoundsSize);
	vaWorldSetSizeUnreal(World, BoundsSize);
	PlaceBounds();

	// World config
	vaWorldSetInverseSpeedOfSound(World, 1.0f / FMath::Max(0.0001f, SpeedOfSound));
	vaWorldSetMetersPerUnit(World, FMath::Max(0.0001f, MetersPerUnit));
	vaWorldSetEpsilon(World, Epsilon);
	vaWorldSetEmittersOutsideTheWorldAreMuffled(World, bEmittersOutsideTheWorldAreMuffled);
	vaWorldSetOcclusionRaysLoseEnergyFromWorldBounds(World, bOcclusionRaysLoseEnergyFromWorldBounds);

	// Threading
	vaWorldSetWorkItemCount(World, FMath::Max(1, WorkItemCount));
	int32 concurrencyLevel = MaximumConcurrencyLevel > 0 ? MaximumConcurrencyLevel : FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 1);
	vaWorldSetMaximumConcurrencyLevel(World, concurrencyLevel);
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

	FName memberName = VAMemberPropertyName(PropertyChangedEvent);

	if (memberName == GET_MEMBER_NAME_CHECKED(AVAWorld, BoundsSize) || memberName == GET_MEMBER_NAME_CHECKED(AVAWorld, BoundsFollow) || memberName == GET_MEMBER_NAME_CHECKED(AVAWorld, BoundsUpdateDistance))
		BoundsSize = ClampBoundsSize(BoundsSize);

	// Unlike the vaWorldSet* calls below, the box should reflect BoundsSize even before BeginPlay (or after EndPlay), since World is null until then
	RefreshWorldBounds();

	// Ignore edits before pressing Play
	if (!World)
		return;

	UpdateVAWorld();

	if (memberName == GET_MEMBER_NAME_CHECKED(AVAWorld, CollisionObjectTypes) || memberName == GET_MEMBER_NAME_CHECKED(AVAWorld, LandscapeLOD))
		RebuildPrimitives();
	else if (memberName == GET_MEMBER_NAME_CHECKED(AVAWorld, Materials) && SyncMaterials())
		RebuildPrimitives();
}
#endif

void AVAWorld::BeginPlay()
{
	Super::BeginPlay();

	UVAWorldSubsystem* subsystem = GetWorld()->GetSubsystem<UVAWorldSubsystem>();

	if (!subsystem)
		return;

	if (!FVaudioUnrealModule::IsSdkLoaded())
	{
		VA_ERROR_NAMED(TEXT("Is disabled, as the Vercidium Audio SDK failed to load. Check the log for the reason."));
		SetActorTickEnabled(false);
		return;
	}

	if (AVAWorld* existing = subsystem->GetVAWorld())
	{
		VA_ERROR_NAMED(TEXT("Is ignored, as '%s' is already the VAWorld in this level. A level can only have one VAWorld."), *VAActorName(existing));
		SetActorTickEnabled(false);
		return;
	}

	RunningWorlds.Add(this);

	InitializeVAWorld();

	// A level instance's actors are added once its level is, see AddLevelInstanceContents
	LevelAddedToWorldHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &AVAWorld::OnLevelAddedToWorld);

	// Actors that began play before this one join it now
	subsystem->RegisterWorld(this);
}

AVAWorld* AVAWorld::Find(const UObject* WorldContextObject)
{
	UWorld* world = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UVAWorldSubsystem* subsystem = world ? world->GetSubsystem<UVAWorldSubsystem>() : nullptr;
	return subsystem ? subsystem->GetVAWorld() : nullptr;
}

// Without a listener a raytraced one-shot never becomes ready, and one may never begin play, so it's dropped rather than left waiting. One silenced by the bounds would never be heard, so it isn't created either, unless it's a looping VASource, which waits for the bounds (an actor-free one-shot is removed when it's silenced)
static bool VACanStartOneShot(const AVAWorld* audioWorld, const TCHAR* function, USoundBase* sound, const FVector& position, bool bClampPosition, bool bWaitsForBounds)
{
	if (!audioWorld || !audioWorld->GetMainListener())
	{
		VA_WARN(TEXT("%s was dropped, as there's no VAListener that has begun play. One-shots don't wait for a listener"), function);
		return false;
	}

	if (!bWaitsForBounds && audioWorld->IsSilencedByBounds(position, bClampPosition))
	{
		VA_WARN(TEXT("%s of '%s' was dropped, as it's outside the VAWorld's bounds. Only a looping VASource waits for them"), function, *sound->GetName());
		return false;
	}

	return true;
}

static AVASource* VASpawnOneShot(const TCHAR* function, UWorld* world, USoundBase* sound, const FVector& location, const FVAOneShotSettings& settings, bool raytraceOnce)
{
	if (!VACanStartOneShot(AVAWorld::Find(world), function, sound, location, GetDefault<AVASource>()->bClampPosition, sound->IsLooping()))
		return nullptr;

	FActorSpawnParameters spawnParameters;
	spawnParameters.bDeferConstruction = true;
	spawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Never saved, e.g. when spawned while simulating in the editor
	spawnParameters.ObjectFlags |= RF_Transient;

	const FTransform transform(location);
	AVASource* source = world->SpawnActor<AVASource>(AVASource::StaticClass(), transform, spawnParameters);

	if (!source)
		return nullptr;

	source->SourceSound = sound;
	source->bAutoPlay = true;
	source->bRaytraceOnce = raytraceOnce;
	source->MarkSpawnedAsOneShot();

	source->VolumeMultiplier = settings.VolumeMultiplier;
	source->PitchMultiplier = settings.PitchMultiplier;
	source->AttenuationSettings = settings.AttenuationSettings;

	source->ReverbRayCount = settings.ReverbRayCount;
	source->ReverbBounceCount = settings.ReverbBounceCount;
	source->ReverbEnergyCap = settings.ReverbEnergyCap;
	source->MinimumReverbEnergy = settings.MinimumReverbEnergy;
	source->MaxVolume = settings.MaxVolume;
	source->MaxEchogramTime = settings.MaxEchogramTime;
	source->EchogramGranularity = settings.EchogramGranularity;
	source->bAffectsGroupedEAX = settings.bAffectsGroupedEAX;
	source->bKeepReverbTailAlive = settings.bKeepReverbTailAlive;
	source->bUseListenerReverb = settings.bUseListenerReverb;

	source->OcclusionEnergyCap = settings.OcclusionEnergyCap;
	source->PermeationEnergyCap = settings.PermeationEnergyCap;

	source->bClusteringEnabled = settings.bClusteringEnabled;
	source->ClusteringDistance = settings.ClusteringDistance;

	source->Type = settings.Type;

	source->FinishSpawning(transform);
	return source;
}

void AVAWorld::WarnIfLooping(const TCHAR* function, USoundBase* sound, const TCHAR* alternative)
{
	if (!sound->IsLooping())
		return;

	// Weak, so a new sound at a destroyed one's address isn't mistaken for it
	bool alreadyWarned = false;
	LoopingWarnedSounds.Add(TWeakObjectPtr<USoundBase>(sound), &alreadyWarned);

	if (!alreadyWarned)
		VA_WARN(TEXT("%s was given '%s', which loops, so it never finishes and only AVAWorld::StopOneShots() stops it. Use %s for looping sounds"), function, *sound->GetName(), alternative);
}

void AVAWorld::PlaySoundAtLocation(const UObject* WorldContextObject, USoundBase* Sound, FVector Location, const FVAOneShotSettings& Settings)
{
	AVAWorld* audioWorld = Find(WorldContextObject);

	if (!Sound || !audioWorld)
	{
		VA_WARN(TEXT("PlaySoundAtLocation needs a sound, and a VAWorld that has begun play in the world context object's level"));
		return;
	}

	audioWorld->WarnIfLooping(TEXT("PlaySoundAtLocation"), Sound, TEXT("SpawnSoundAtLocation"));
	audioWorld->StartLocationOneShot(Sound, Location, Settings);
}

AVASource* AVAWorld::SpawnSoundAtLocation(const UObject* WorldContextObject, USoundBase* Sound, FVector Location, const FVAOneShotSettings& Settings)
{
	UWorld* world = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;

	if (!Sound || !world)
	{
		VA_WARN(TEXT("SpawnSoundAtLocation needs a sound and a world context object that's in a world"));
		return nullptr;
	}

	return VASpawnOneShot(TEXT("SpawnSoundAtLocation"), world, Sound, Location, Settings, Settings.bRaytraceOnce);
}

AVASource* AVAWorld::SpawnSoundAttached(USoundBase* Sound, USceneComponent* AttachComponent, FName AttachPointName, FVector Location, EAttachLocation::Type LocationType, bool bStopWhenAttachedToDestroyed, const FVAOneShotSettings& Settings)
{
	UWorld* world = AttachComponent ? AttachComponent->GetWorld() : nullptr;

	if (!Sound || !world)
	{
		VA_WARN(TEXT("SpawnSoundAttached needs a sound and a component to attach it to"));
		return nullptr;
	}

	const FTransform socket = AttachComponent->GetSocketTransform(AttachPointName);
	FVector worldLocation;

	switch (LocationType)
	{
		case EAttachLocation::KeepWorldPosition:
			worldLocation = Location;
			break;

		case EAttachLocation::KeepRelativeOffset:
			worldLocation = socket.TransformPosition(Location);
			break;

		default:
			worldLocation = socket.GetLocation();
			break;
	}

	// An attached sound moves, so it keeps raytracing
	AVASource* source = VASpawnOneShot(TEXT("SpawnSoundAttached"), world, Sound, worldLocation, Settings, false);

	if (!source)
		return nullptr;

	source->AttachToComponent(AttachComponent, FAttachmentTransformRules::KeepWorldTransform, AttachPointName);

	if (bStopWhenAttachedToDestroyed)
		source->DestroyWithComponent(AttachComponent);

	return source;
}

void AVAWorld::PlaySoundRelative(const UObject* WorldContextObject, USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier)
{
	AVAWorld* audioWorld = Find(WorldContextObject);

	if (!Sound || !audioWorld)
	{
		VA_WARN(TEXT("PlaySoundRelative needs a sound, and a VAWorld that has begun play in the world context object's level"));
		return;
	}

	audioWorld->WarnIfLooping(TEXT("PlaySoundRelative"), Sound, TEXT("a VASourceRelative"));
	audioWorld->StartRelativeOneShot(Sound, VolumeMultiplier, PitchMultiplier);
}

void AVAWorld::PlaySoundLeech(USoundBase* Sound, AVAEmitter* Emitter, float VolumeMultiplier, float PitchMultiplier, USoundAttenuation* AttenuationSettings)
{
	if (!Sound || !IsValid(Emitter))
	{
		VA_WARN(TEXT("PlaySoundLeech needs a sound and an emitter to leech"));
		return;
	}

	if (Emitter->IsA<AVAListener>())
	{
		VA_WARN(TEXT("PlaySoundLeech can't leech the listener '%s', which has no muffling result. Use PlaySoundRelative instead"), *VAActorName(Emitter));
		return;
	}

	// The emitter may not have joined its VAWorld yet
	AVAWorld* audioWorld = Emitter->GetAudioWorld() ? Emitter->GetAudioWorld() : Find(Emitter);

	if (!audioWorld)
	{
		VA_WARN(TEXT("PlaySoundLeech needs a VAWorld that has begun play in the emitter '%s''s level"), *VAActorName(Emitter));
		return;
	}

	audioWorld->WarnIfLooping(TEXT("PlaySoundLeech"), Sound, TEXT("a VASourceLeech"));
	audioWorld->StartLeechOneShot(Sound, Emitter, VolumeMultiplier, PitchMultiplier, AttenuationSettings);
}

// Mirrors FAudioDevice::CreateComponent, which doesn't create short sounds that start out of range of every listener
static bool VAIsAudible(FAudioDevice* audioDevice, UWorld* world, USoundBase* sound, const FVector& location, USoundAttenuation* attenuation)
{
	static IConsoleVariable* maxLengthCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("au.SoundDistanceOptimizationLength"));
	const float maxLength = maxLengthCVar ? maxLengthCVar->GetFloat() : 1.0f;

	if (sound->GetDuration() > FMath::Max(0.0f, maxLength))
		return true;

	const FSoundAttenuationSettings* attenuationSettings = attenuation ? &attenuation->Attenuation : sound->GetAttenuationSettingsToApply();
	float maxDistance = 0.0f;
	float focusFactor = 0.0f;
	audioDevice->GetMaxDistanceAndFocusFactor(sound, world, location, attenuationSettings, maxDistance, focusFactor);
	return audioDevice->SoundIsAudible(sound, world, location, attenuationSettings, maxDistance, focusFactor);
}

UAudioComponent* AVAWorld::AcquireOneShotComponent(USoundBase* sound, USoundAttenuation* attenuation, const FVector* location, USceneComponent* attachParent)
{
	UWorld* world = GetWorld();
	FAudioDevice* audioDevice = world->GetAudioDeviceRaw();

	// Raytracing doesn't need the sound, so having no audio device (e.g. -nosound) isn't worth a warning
	if (!audioDevice || !world->bAllowAudioPlayback)
		return nullptr;

	if (location && !VAIsAudible(audioDevice, world, sound, *location, attenuation))
		return nullptr;

	UAudioComponent* component = nullptr;

	// Destroyed by the user or with its owner while pooled
	while (!component && AudioComponentPool.Num() > 0)
	{
		UAudioComponent* pooled = AudioComponentPool.Pop();

		if (IsValid(pooled))
			component = pooled;
	}

	if (!component)
	{
		// Owned by this world rather than the leeched emitter, so it can outlive the emitter and be reused
		FAudioDevice::FCreateComponentParams params(world, this);
		component = FAudioDevice::CreateComponent(sound, params);

		if (!component)
			return nullptr;
	}

	// Everything a previous one-shot could have changed. FVASourcePlayback::Play sets the volume, pitch, low pass filter and bAutoDestroy, and the submix sends and dry output live on the active sound that Play() creates
	component->SetSound(sound);
	component->bStopWhenOwnerDestroyed = false;
	component->bAllowSpatialization = location != nullptr;
	component->bIsUISound = location == nullptr;
	component->bOverrideAttenuation = false;
	component->AttenuationSettings = attenuation;
	component->SubtitlePriority = sound->GetSubtitlePriority();

	// Keeps playing where it was if the emitter is destroyed
	if (attachParent)
		component->AttachToComponent(attachParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	else if (location)
		component->SetWorldLocationAndRotation(*location, FRotator::ZeroRotator);

	return component;
}

void AVAWorld::ReleaseOneShotComponent(UAudioComponent* component)
{
	if (!IsValid(component))
		return;

	if (AudioComponentPool.Num() >= MaxPooledAudioComponents)
	{
		component->DestroyComponent();
		return;
	}

	component->Stop();
	component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	// Lets the sound be garbage collected while it waits
	component->SetSound(nullptr);
	AudioComponentPool.Add(component);
}

void AVAWorld::StartRelativeOneShot(USoundBase* sound, float volumeMultiplier, float pitchMultiplier)
{
	UAudioComponent* component = AcquireOneShotComponent(sound, nullptr, nullptr);

	if (!component)
		return;

	FVAOneShot& oneShot = AddOneShot(volumeMultiplier, pitchMultiplier);
	oneShot.bRelative = true;

	ApplyOneShotResults(oneShot);
	oneShot.Playback.Play(component);
}

void AVAWorld::StartLeechOneShot(USoundBase* sound, AVAEmitter* emitter, float volumeMultiplier, float pitchMultiplier, USoundAttenuation* attenuation)
{
	const FVector location = emitter->GetActorLocation();

	if (!VACanStartOneShot(this, TEXT("PlaySoundLeech"), sound, location, emitter->bClampPosition, false))
		return;

	UAudioComponent* component = AcquireOneShotComponent(sound, attenuation, &location, emitter->GetRootComponent());

	if (!component)
		return;

	FVAOneShot& oneShot = AddOneShot(volumeMultiplier, pitchMultiplier);
	oneShot.Emitter = emitter;

	WaitToPlayOneShot(oneShot, component);
}

void AVAWorld::StartLocationOneShot(USoundBase* sound, const FVector& location, const FVAOneShotSettings& settings)
{
	// Raytraced with AVASource's defaults, so its bClampPosition too
	if (!VACanStartOneShot(this, TEXT("PlaySoundAtLocation"), sound, location, GetDefault<AVASource>()->bClampPosition, false))
		return;

	const FString name = FString::Printf(TEXT("PlaySoundAtLocation '%s'"), *sound->GetName());

	VAEmitter* handle = VACreateEmitterHandle();
	vaEmitterSetName(handle, TCHAR_TO_UTF8(*name));
	vaEmitterSetPositionUnreal(handle, location);

	// AVASource's defaults for what the settings don't cover, and a seed of its own rather than the CDO's
	VAPushNonOneShotProperties(handle, *GetDefault<AVASource>());
	VAPushOneShotProperties(handle, settings);
	vaEmitterSetScatteringSeed(handle, VARandomScatteringSeed());

	if (!AddEmitterToWorld(handle, name))
	{
		VADestroyEmitterHandle(handle);
		return;
	}

	MainListener->AddTarget(handle, name);

	// Raytracing doesn't need the sound, so with no audio device (e.g. -nosound) it's raytraced but not heard
	UAudioComponent* component = AcquireOneShotComponent(sound, settings.AttenuationSettings, &location);

	FVAOneShot& oneShot = AddOneShot(settings.VolumeMultiplier, settings.PitchMultiplier);
	oneShot.Handle = handle;
	oneShot.Location = location;
	oneShot.Name = name;
	oneShot.bAffectsGroupedEAX = settings.bAffectsGroupedEAX;
	oneShot.bUseListenerReverb = settings.bUseListenerReverb;
	oneShot.bRaytraceOnce = settings.bRaytraceOnce;

	WaitToPlayOneShot(oneShot, component);
}

FVAOneShot& AVAWorld::AddOneShot(float volumeMultiplier, float pitchMultiplier)
{
	FVAOneShot& oneShot = OneShots.AddDefaulted_GetRef();
	oneShot.Playback.bAutoDestroy = false;
	oneShot.Playback.SetVolumeMultiplier(volumeMultiplier);
	oneShot.Playback.SetPitchMultiplier(pitchMultiplier);
	oneShot.Playback.SetDryOutputEnabled(!bReverbOnly);
	return oneShot;
}

void AVAWorld::WaitToPlayOneShot(FVAOneShot& oneShot, UAudioComponent* component)
{
	oneShot.bPending = true;
	oneShot.PendingComponent = component;

	// Waits for the muffling and reverb, so the sound never starts unmuffled or without reverb
	if (IsOneShotReady(oneShot))
	{
		ApplyOneShotResults(oneShot);
		PlayPendingOneShot(oneShot);
	}
}

bool AVAWorld::IsOneShotReady(const FVAOneShot& oneShot) const
{
	if (oneShot.Handle)
		return VAIsHandleReadyToPlay(oneShot.Handle, this);

	AVAEmitter* emitter = oneShot.Emitter.Get();
	return emitter && emitter->IsReadyToPlay();
}

bool AVAWorld::IsOneShotSilencedByBounds(const FVAOneShot& oneShot) const
{
	if (oneShot.Handle)
		return IsSilencedByBounds(oneShot.Location, GetDefault<AVASource>()->bClampPosition);

	// A leech whose emitter was destroyed finishes where it was
	const AVAEmitter* emitter = oneShot.Emitter.Get();
	return emitter && emitter->IsSilencedByBounds();
}

void AVAWorld::ApplyOneShotResults(FVAOneShot& oneShot)
{
	if (oneShot.bRelative)
	{
		AVASourceRelative::ApplyListenerReverbSend(oneShot.Playback, this, this, OneShotWarnedListener);
	}
	else if (oneShot.Handle)
	{
		const int32 groupedEAXIndex = oneShot.bAffectsGroupedEAX ? vaEmitterGetGroupedEAXIndex(oneShot.Handle) : -1;
		VAApplyHandleResults(oneShot.Playback, oneShot.Handle, this, groupedEAXIndex, oneShot.bUseListenerReverb, this);
	}
	// A leech keeps its emitter's last results once the emitter is destroyed
	else if (AVAEmitter* emitter = oneShot.Emitter.Get())
	{
		emitter->ApplyResultsToPlayback(oneShot.Playback);
	}
}

// Its results stay once suspended, and its reverb tail keeps feeding grouped EAX. One playing outside the bounds has no EAX yet, so it waits to be raytraced inside first
static void VASuspendIfRaytracedOnce(const FVAOneShot& oneShot, const AVAWorld* world)
{
	if (oneShot.Handle && oneShot.bRaytraceOnce && !vaEmitterGetSuspended(oneShot.Handle) && VAHasHandleResults(oneShot.Handle, world))
		vaEmitterSetSuspended(oneShot.Handle, true);
}

void AVAWorld::PlayPendingOneShot(FVAOneShot& oneShot)
{
	oneShot.bPending = false;
	VASuspendIfRaytracedOnce(oneShot, this);

	if (UAudioComponent* component = oneShot.PendingComponent)
	{
		oneShot.PendingComponent = nullptr;
		oneShot.Playback.Play(component);
	}
}

void AVAWorld::RemoveOneShot(int32 index)
{
	FVAOneShot& oneShot = OneShots[index];
	const TArray<UAudioComponent*> components = oneShot.Playback.GetComponents();
	oneShot.Playback.Stop();

	for (UAudioComponent* component : components)
		ReleaseOneShotComponent(component);

	ReleaseOneShotComponent(oneShot.PendingComponent);

	if (oneShot.Handle)
		ReleaseEmitterHandle(oneShot.Handle, oneShot.Name);

	OneShots.RemoveAtSwap(index);
}

void AVAWorld::UpdateOneShots()
{
	for (int32 i = OneShots.Num() - 1; i >= 0; i--)
	{
		FVAOneShot& oneShot = OneShots[i];

		// It left the bounds or they moved away from it, so it's cut off, or never plays if it's pending
		if (IsOneShotSilencedByBounds(oneShot))
		{
			RemoveOneShot(i);
			continue;
		}

		ApplyOneShotResults(oneShot);

		if (oneShot.bPending)
		{
			// Destroyed by the user to cancel it, or with its owner
			if (oneShot.PendingComponent && !IsValid(oneShot.PendingComponent))
			{
				RemoveOneShot(i);
				continue;
			}

			if (!IsOneShotReady(oneShot))
			{
				// The last listener left, or the leeched emitter was destroyed, before it was ready. Like a new one-shot, it doesn't wait for a listener
				if (!MainListener || (!oneShot.Handle && !oneShot.Emitter.IsValid()))
					RemoveOneShot(i);

				continue;
			}

			PlayPendingOneShot(oneShot);
		}
		else
		{
			VASuspendIfRaytracedOnce(oneShot, this);
		}

		// With no audio device nothing plays, so it's released as soon as it's ready
		if (!oneShot.Playback.IsPlaying())
			RemoveOneShot(i);
	}
}

void AVAWorld::StopOneShots()
{
	for (int32 i = OneShots.Num() - 1; i >= 0; i--)
		RemoveOneShot(i);
}

void AVAWorld::InitializeVAWorld()
{
	// Already initialised, all is good
	if (World)
		return;

	World = vaWorldCreate();
	BoundsPosition = GetActorLocation();
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
			VAAddSubmixEffect(this, Sub, Preset);
			VAAddSubmixEffect(this, Sub, PanPreset);
		}
		else
			VA_WARN_NAMED(TEXT("Has a null grouped EAX submix at index %d. Please assign a submix"), i);

		GroupedEAXPresets.Add(Preset);
		GroupedEAXPanPresets.Add(PanPreset);
	}

	SyncMaterials();
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
				VAVector panVector = {};
				vaWorldCalculateListenerRelativePan(World, *Direction, FMath::DegreesToRadians(ListenerRotation.Pitch), FMath::DegreesToRadians(ListenerRotation.Yaw), &panVector);
				float pan = FMath::Clamp((float)panVector.x, -1.0f, 1.0f);

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
	FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedToWorldHandle);

	StopOneShots();

	for (UAudioComponent* component : AudioComponentPool)
		if (IsValid(component))
			component->DestroyComponent();

	AudioComponentPool.Empty();
	LoopingWarnedSounds.Empty();

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
		LevelInstanceSources.Empty();
		CustomMaterialIds.Empty();
		AppliedMaterials.Empty();

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

	// Last, so the emitters still playing free their handles against a destroyed vaWorld and wait for the next VAWorld
	if (UVAWorldSubsystem* subsystem = GetWorld()->GetSubsystem<UVAWorldSubsystem>())
		subsystem->UnregisterWorld(this);
}

// Unreal FOVs are horizontal, the debug window's is vertical
static float VerticalFOVRadians(float horizontalDegrees, FIntPoint viewportSize)
{
	if (viewportSize.X <= 0 || viewportSize.Y <= 0)
		return 0.0f;

	float aspect = (float)viewportSize.X / (float)viewportSize.Y;
	return 2.0f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(horizontalDegrees) * 0.5f) / aspect);
}

// The debug window can still free fly with F1
void AVAWorld::SyncDebugCamera()
{
	if (!bRenderingEnabled)
		return;

	FVector position;
	FRotator rotation;
	float fieldOfView = 0.0f;

#if WITH_EDITOR
	// Simulate, or PIE after ejecting with F8, is driven from the level editor viewport rather than the player
	FLevelEditorViewportClient* viewportClient = GCurrentLevelEditingViewportClient;

	if (bSyncViewport && GEditor && GEditor->bIsSimulatingInEditor && viewportClient && viewportClient->IsPerspective())
	{
		position = viewportClient->GetViewLocation();
		rotation = viewportClient->GetViewRotation();

		if (viewportClient->Viewport)
			fieldOfView = VerticalFOVRadians(viewportClient->ViewFOV, viewportClient->Viewport->GetSizeXY());
	}
	else
#endif
	{
		APlayerController* playerController = GetWorld()->GetFirstPlayerController();
		APlayerCameraManager* cameraManager = playerController ? playerController->PlayerCameraManager : nullptr;

		// Follow the camera even when the listener doesn't auto-follow it. The camera location is (0, 0, 0) until its first UpdateCamera()
		if (cameraManager && cameraManager->GetCameraCacheTime() > 0.0f)
		{
			position = cameraManager->GetCameraLocation();
			rotation = cameraManager->GetCameraRotation();
		}
		else
		{
			AVAListener* mainListener = GetMainListener();
			VAEmitter* listenerVA = mainListener ? mainListener->GetVAEmitter() : nullptr;

			if (!listenerVA)
				return;

			VAVector listenerPosition = vaEmitterGetPosition(listenerVA);
			position = VAVectorToFVector(listenerPosition);
			rotation = GetListenerControlRotation(mainListener);
		}

		UGameViewportClient* gameViewport = GetWorld()->GetGameViewport();

		if (cameraManager && gameViewport && gameViewport->Viewport)
			fieldOfView = VerticalFOVRadians(cameraManager->GetFOVAngle(), gameViewport->Viewport->GetSizeXY());
	}

	vaWorldSetCameraPosition(World, FVectorToVAVector(position));
	vaWorldSetCameraPitch(World, FMath::DegreesToRadians(rotation.Pitch));
	vaWorldSetCameraYaw(World, FMath::DegreesToRadians(rotation.Yaw));

	if (fieldOfView > 0.0f && fieldOfView < PI)
		vaWorldSetFieldOfView(World, fieldOfView);
}

void AVAWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (World)
	{
		if (BoundsFollow == EVABoundsFollow::Listener)
			FollowBounds();

		SyncDebugCamera();

		vaWorldUpdate(World);

		// OnRemoved has been invoked for these, so the raytracing threads no longer read them
		DestroyRemovedEmitters();

		// Broadcast the Blueprint events the SDK callbacks queued during vaWorldUpdate. A handler may destroy emitters, hence the weak pointers
		TArray<TWeakObjectPtr<AVAEmitter>> eventEmitters = MoveTemp(PendingEventEmitters);

		for (const TWeakObjectPtr<AVAEmitter>& emitter : eventEmitters)
			if (AVAEmitter* alive = emitter.Get())
				alive->FlushPendingEvents();

		UpdateOneShots();

		if (bReverbOnly != bWasReverbOnly)
		{
			bWasReverbOnly = bReverbOnly;
			bool bDryEnabled = !bReverbOnly;

			// Only sources have dry output to toggle
			for (AVAEmitter* Emitter : RegisteredEmitters)
				if (AVARaytracedSource* ConcreteEmitter = Cast<AVARaytracedSource>(Emitter))
					ConcreteEmitter->SetDryOutputEnabled(bDryEnabled);

			for (FVAOneShot& oneShot : OneShots)
				oneShot.Playback.SetDryOutputEnabled(bDryEnabled);
		}

		if (bShowDebugMessages && GEngine)
			ShowDebugMessages();
	}
}

void AVAWorld::ShowDebugMessages()
{
	// Only configuration problems are shown. Initialising, waiting to be raytraced and not playing yet are normal states, e.g. every one-shot passes through them
	for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
	{
		AVASource* source = Cast<AVASource>(RegisteredEmitters[i]);

		if (!source)
			continue;

		if (!source->SourceSound)
			VAShowMessage(VAMessageKey(source, EVAMessageSlot::SourceStatus), 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source Emitter %d '%s' has no sound file assigned"), i, *VAActorName(source)));
		else if (!source->SourceSound->AttenuationSettings && !source->AttenuationSettings)
			VAShowMessage(VAMessageKey(source, EVAMessageSlot::AttenuationStatus), 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source Emitter %d '%s' has no Sound Attenuation - it will not fall off with distance"), i, *VAActorName(source)));
	}

	// Per-target LPF from the main listener, the same filter each source applies to itself
	if (AVAListener* MessageListener = GetMainListener())
	{
		VAEmitter* ListenerVA = MessageListener->GetVAEmitter();

		if (ListenerVA)
		{
			for (int32 i = 0; i < RegisteredEmitters.Num(); ++i)
			{
				AVAEmitter* Target = RegisteredEmitters[i];

				uint64 messageID = VAMessageKey(MessageListener, EVAMessageSlot::TargetStatus, i);

				if (!Target->GetVAEmitter())
				{
					VAShowMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' target '%s' has no emitter. Ensure the target emitter is assigned to the same World"), *VAActorName(MessageListener), *VAActorName(Target)));
					continue;
				}

				// Not raytraced yet is a normal state, and it has no filter to check until it is
				if (vaEmitterHasRaytracedTarget(ListenerVA, Target->GetVAEmitter()) != VA_TRUE)
					continue;

				VALowPassFilter* lowPassFilter = nullptr;

				if (vaEmitterGetTargetFilter(ListenerVA, Target->GetVAEmitter(), &lowPassFilter) != VA_SUCCESS)
				{
					VAShowMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Listener '%s' has raytraced the '%s' emitter, but has an invalid low pass filter"), *VAActorName(MessageListener), *VAActorName(Target)));
					continue;
				}
			}
		}
	}

	// Per-grouped-EAX reverb data (mirrors the settings OnReverbUpdated() sends to each preset - recomputed here purely for display).
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
		}
	}

	if (GroupedEAXSubmixes.Num() == 0)
	{
		VAShowMessage(VAMessageKey(this, EVAMessageSlot::NoGroupedEAX), 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] World '%s' has no Grouped EAX Submixes. Ensure at least one is added"), *VAActorName(this)));
	}

	if (!GetMainListener())
		VAShowMessage(VAMessageKey(this, EVAMessageSlot::ListenerStatus), 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] There is no main listener. Ensure a VAListener actor is placed and assigned to this world")));
	
	if (ActorsWithInvalidMaterials.Num() > 0)
	{
		VAShowMessage(VAMessageKey(this, EVAMessageSlot::InvalidMaterials), 0.0f, FColor::Orange,
			FString::Printf(TEXT("[VA] %d actor(s) were not added to the world: %s. See Output Log for details."),
				ActorsWithInvalidMaterials.Num(), *FString::Join(ActorsWithInvalidMaterials, TEXT(", "))));
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

bool AVAWorld::AddEmitterToWorld(AVAEmitter* emitter)
{
	return AddEmitterToWorld(emitter->GetVAEmitter(), VAActorName(emitter));
}

bool AVAWorld::AddEmitterToWorld(VAEmitter* handle, const FString& name)
{
	VAResult result = vaWorldAddEmitter(World, handle);

	switch (result)
	{
		case VA_SUCCESS:
			return true;

		case VA_ALREADY_EXISTS:
			VA_ERROR_NAMED(TEXT("Failed to register emitter '%s' as it is already added to this world."), *name);
			return false;

		case VA_WORLD_CONFLICT:
			VA_ERROR_NAMED(TEXT("Failed to register emitter '%s' as it is already added to a different world."), *name);
			return false;

		default:
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to register emitter '%s'."), *name);
			return false;
	}
}

void AVAWorld::ReleaseEmitterHandle(VAEmitter* handle, const FString& name, bool bAlreadyRemoved)
{
	// The world ended first, and vaWorldDestroy unlinked this emitter from it, so it can be freed now
	if (!World)
	{
		VAResult result = VADestroyEmitterHandle(handle);

		if (result != VA_SUCCESS)
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to destroy emitter '%s' after its world ended."), *name);

		return;
	}

	// OnRemoved fires once the in-flight raytracing results are handled, or once its reverb tail finishes (VA_PENDING_REMOVAL). It's orphaned first, as OnRemoved fires during vaWorldRemoveEmitter when no results are pending
	vaEmitterSetUserData(handle, nullptr);
	AddOrphanedEmitter(handle);

	// vaWorldRemoveEmitter returns VA_NOT_FOUND once the tail has finished but the in-flight results haven't been handled, and OnRemoved is still to come
	if (bAlreadyRemoved)
		return;

	VAResult result = vaWorldRemoveEmitter(World, handle);

	switch (result)
	{
		case VA_SUCCESS:
		case VA_PENDING_REMOVAL:
			break;

		// Never added to the world
		case VA_NOT_FOUND:
			OrphanedEmitters.Remove(handle);
			VADestroyEmitterHandle(handle);
			break;

		default:
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to remove emitter '%s' from its world."), *name);
			break;
	}
}

bool AVAWorld::RegisterEmitter(AVAEmitter* emitter)
{
	if (!AddEmitterToWorld(emitter))
		return false;

	RegisteredEmitters.Add(emitter);

	// If no listener has begun play yet, SetCurrentListener wires this emitter up when one does
	if (MainListener)
		MainListener->AddTarget(emitter);

	return true;
}

void AVAWorld::UnregisterEmitter(AVAEmitter* emitter)
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
		VA_WARN_NAMED(TEXT("VAListener '%s' has bCurrent enabled, so it replaced '%s' as the current listener. Disable bCurrent on listeners that shouldn't take over when they begin play, and call MakeCurrent() on the one that should be used."), *VAActorName(listener), *VAActorName(MainListener));
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

	// A new current listener gets centred bounds whatever BoundsUpdateDistance is
	if (World && BoundsFollow == EVABoundsFollow::Listener)
		PlaceBounds();

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

	VA_WARN_NAMED(TEXT("VAListener '%s' is the only listener in this world, so it stays current."), *VAActorName(listener));
	listener->bCurrent = true;
}

void AVAWorld::WirePendingTargets()
{
	if (!MainListener)
		return;

	for (AVAEmitter* emitter : RegisteredEmitters)
	{
		if (emitter->GetVAEmitter())
			MainListener->AddTarget(emitter);
	}

	for (const FVAOneShot& oneShot : OneShots)
	{
		if (oneShot.Handle)
			MainListener->AddTarget(oneShot.Handle, oneShot.Name);
	}
}

void AVAWorld::OnOrphanedEmitterRemoved(VAEmitter* handle)
{
	AVAWorld* world = nullptr;

	if (OrphanedEmitters.RemoveAndCopyValue(handle, world))
		world->DeferEmitterDestroy(handle);
}

void AVAWorld::QueueEmitterEvents(AVAEmitter* emitter)
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

int32 AVAWorld::GetTotalOrphanedEmitterCount()
{
	return OrphanedEmitters.Num();
}

void AVAWorld::ExportWorld()
{
	if (!World)
	{
		VA_WARN_NAMED(TEXT("Cannot export world (press Play first)"));
		return;
	}

	ExportToFile(TEXT("vaudio_export.va"));
}

bool AVAWorld::ExportToFile(const FString& Path)
{
	if (!World)
	{
		VA_WARN_NAMED(TEXT("Cannot export world (press Play first)"));
		return false;
	}

	FString fullPath = FPaths::ConvertRelativePathToFull(FPaths::IsRelative(Path) ? FPaths::ProjectDir() / Path : Path);
	VAResult result = vaWorldExport(World, TCHAR_TO_UTF8(*fullPath));

	if (result != VA_SUCCESS)
	{
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to export the world to '%s'."), *fullPath);
		return false;
	}

	VA_LOG_NAMED(TEXT("Exported the world to '%s'."), *fullPath);
	return true;
}

VABlueprintReal AVAWorld::GetMainThreadTime() const
{
	return (VABlueprintReal)vaWorldGetMainThreadTime(World);
}

VABlueprintReal AVAWorld::GetPreparationTime() const
{
	return (VABlueprintReal)vaWorldGetPreparationTime(World);
}

VABlueprintReal AVAWorld::GetRaytracingTime() const
{
	return (VABlueprintReal)vaWorldGetRaytracingTime(World);
}

VABlueprintReal AVAWorld::GetAnalysisTime() const
{
	return (VABlueprintReal)vaWorldGetAnalysisTime(World);
}

int32 AVAWorld::GetGroupedEAXCount() const
{
	return World ? vaWorldGetGroupedEAXCount(World) : 0;
}

static const VAEAXReverb* GetGroupedEAX(VAWorld* world, int32 index)
{
	if (!world || index < 0 || index >= vaWorldGetGroupedEAXCount(world))
		return nullptr;

	const VAEAXReverb** groupedEAX = vaWorldGetGroupedEAX(world);
	return groupedEAX ? groupedEAX[index] : nullptr;
}

float AVAWorld::GetGroupedEAXGainLF(int32 Index) const
{
	const VAEAXReverb* eax = GetGroupedEAX(World, Index);
	return eax ? eax->gainLF : 0.0f;
}

float AVAWorld::GetGroupedEAXGainHF(int32 Index) const
{
	const VAEAXReverb* eax = GetGroupedEAX(World, Index);
	return eax ? eax->gainHF : 0.0f;
}

float AVAWorld::GetGroupedEAXDecayTime(int32 Index) const
{
	const VAEAXReverb* eax = GetGroupedEAX(World, Index);
	return eax ? eax->decayTime : 0.0f;
}

static constexpr int32 FirstCustomMaterialId = 1000;

bool AVAWorld::SyncMaterials()
{
	if (!World)
		return false;

	bool changed = false;

	for (UVAMaterialBase* material : AppliedMaterials)
	{
		if (!material || Materials.Contains(material))
			continue;

		changed = true;

		if (const UVADefaultMaterial* defaultMaterial = Cast<UVADefaultMaterial>(material))
			defaultMaterial->RestoreWorldDefaults(this);
		else
			VA_ERROR_NAMED(TEXT("Custom material '%s' was removed from Materials during play. Custom materials can't be removed at runtime - primitives using it keep its last values until the level is reloaded"), *material->GetMaterialName());
	}

	TArray<UVAMaterialBase*> applied;

	for (UVAMaterialBase* material : Materials)
	{
		if (!material || applied.Contains(material))
			continue;

		applied.Add(material);

		if (!AppliedMaterials.Contains(material))
			changed = true;

		UVACustomMaterial* customMaterial = Cast<UVACustomMaterial>(material);

		if (customMaterial && !CustomMaterialIds.Contains(customMaterial))
		{
			// One past the highest ID this world has assigned, so a removed material's ID is never reused by another
			int32 materialId = FirstCustomMaterialId;

			for (const auto& pair : CustomMaterialIds)
				materialId = FMath::Max(materialId, pair.Value + 1);

			VAResult result = vaWorldCreateMaterial(World, materialId);

			if (result != VA_SUCCESS)
			{
				VA_ERROR_NAMED_RESULT(result, TEXT("Failed to create custom material '%s'."), *customMaterial->GetMaterialName());
				continue;
			}

			CustomMaterialIds.Add(customMaterial, materialId);
		}

		// Re-applies every default material too, so a removed duplicate doesn't leave the SDK defaults in place
		material->ApplyToWorld(this);
	}

	AppliedMaterials = MoveTemp(applied);
	return changed;
}

void AVAWorld::SetMaterials(const TArray<UVAMaterialBase*>& newMaterials)
{
	Materials = newMaterials;

	if (SyncMaterials())
		RebuildPrimitives();
}

int32 AVAWorld::GetCustomMaterialId(const UVACustomMaterial* material) const
{
	const int32* materialId = CustomMaterialIds.Find(material);
	return materialId ? *materialId : 0;
}

bool AVAWorld::HasMaterial(const UVAMaterialBase* material) const
{
	if (Materials.Contains(material))
		return true;

	const UVACustomMaterial* customMaterial = Cast<UVACustomMaterial>(material);
	return customMaterial && GetCustomMaterialId(customMaterial) != 0;
}
