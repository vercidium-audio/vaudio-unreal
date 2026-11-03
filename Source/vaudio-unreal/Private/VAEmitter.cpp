#include "VAEmitter.h"
#include "VAEngineCompat.h"
#include "VAWorld.h"
#include "VAWorldSubsystem.h"
#include "VAListener.h"
#include "VAVisualisation.h"
#include "Engine/World.h"

extern "C" {
#include "vaudio.h"
}

#include "VAConstants.h"
#include "VAEditorSprite.h"
#include "VALog.h"

// vaEmitterSetUserData() stashes the owning actor on the VAEmitter* itself, so these trampolines
// can resolve identity directly instead of needing a side registry.
static void VAOnRaytracingCompleteTrampoline(VAEmitter* emitter)
{
	if (AVAEmitter* owner = static_cast<AVAEmitter*>(vaEmitterGetUserData(emitter)))
		owner->QueueRaytracingComplete();
}

static void VAOnRaytracedByAnotherEmitterTrampoline(VAEmitter* source, VAEmitter* target)
{
	AVAEmitter* owner = static_cast<AVAEmitter*>(vaEmitterGetUserData(target));

	if (!owner)
		return;

	VALowPassFilter* filter = nullptr;

	if (vaEmitterGetTargetFilter(source, target, &filter) == VA_SUCCESS)
		owner->QueueRaytracedByListener(filter->gainLF, filter->gainHF);
}

static void VAOnRemovedTrampoline(VAEmitter* emitter)
{
	if (AVAEmitter* owner = static_cast<AVAEmitter*>(vaEmitterGetUserData(emitter)))
	{
		owner->OnEmitterRemoved(emitter);
		return;
	}

	// The actor already released this handle while its removal was pending
	AVAWorld::OnOrphanedEmitterRemoved(emitter);
}

static int32 VARandomScatteringSeed()
{
#if UE_VERSION_OLDER_THAN(5, 0, 0)
	// 4.27 has no Rand32, and its Rand() is only 15 bits on MSVC
	return (int32)(FGuid::NewGuid().A & 0x7fffffff);
#else
	return (int32)(FMath::Rand32() & 0x7fffffff);
#endif
}

AVAEmitter::AVAEmitter()
{
	PrimaryActorTick.bCanEverTick = true;

	UBillboardComponent* Root = CreateDefaultSubobject<UBillboardComponent>(TEXT("Root"));
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	VASetEditorSprite(Root, GetClass());
#endif

	ScatteringSeed = VARandomScatteringSeed();
}

void AVAEmitter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Actors created from a Blueprint or a duplicated template copy its seed, so give them their own. A seed the user changed no longer matches the template, so it's kept
	if (const AVAEmitter* archetype = Cast<AVAEmitter>(GetArchetype()))
	{
		if (ScatteringSeed == archetype->ScatteringSeed)
			ScatteringSeed = VARandomScatteringSeed();
	}
}

void AVAEmitter::BeginPlay()
{
	Super::BeginPlay();

	BindWorld();

	// A UVAVisualisation on this actor may have initialised it already, during Super::BeginPlay
	if (TryInitializeEmitter() || AudioWorld)
		return;

	// The VAWorld begins play after this actor, as actor BeginPlay order isn't guaranteed. Tick returns early until then
	WaitForWorld();
}

void AVAEmitter::BindWorld()
{
	UWorld* world = GetWorld();
	BoundSubsystem = world ? world->GetSubsystem<UVAWorldSubsystem>() : nullptr;

	if (UVAWorldSubsystem* subsystem = BoundSubsystem.Get())
		WorldUnregisteredHandle = subsystem->OnWorldUnregistered.AddUObject(this, &AVAEmitter::OnWorldUnregistered);
}

void AVAEmitter::UnbindWorld()
{
	StopWaitingForWorld();

	// The subsystem is already gone if its map was
	if (UVAWorldSubsystem* subsystem = BoundSubsystem.Get())
		subsystem->OnWorldUnregistered.Remove(WorldUnregisteredHandle);

	WorldUnregisteredHandle.Reset();
	BoundSubsystem.Reset();
}

void AVAEmitter::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);

	UWorld* world = GetWorld();
	UVAWorldSubsystem* subsystem = world ? world->GetSubsystem<UVAWorldSubsystem>() : nullptr;

	// Seamless travel renames a kept actor into the new map's level
	if (HasActorBegunPlay() && subsystem != BoundSubsystem.Get())
		OnWorldChanged();
}

void AVAEmitter::OnWorldChanged()
{
	// The old map's VAWorld normally ends play before kept actors are moved, so this emitter has left it already
	if (AudioWorld)
		OnWorldUnregistered();

	// An emitter that failed validation, or one in its final removal, isn't waiting and doesn't join the new map's VAWorld either
	const bool waiting = WorldRegisteredHandle.IsValid();

	UnbindWorld();
	BindWorld();

	if (!waiting)
		return;

	if (AVAWorld::Find(this))
		OnWorldRegistered();
	else
		WaitForWorld();
}

void AVAEmitter::WaitForWorld()
{
	if (WorldRegisteredHandle.IsValid())
		return;

	if (UVAWorldSubsystem* subsystem = BoundSubsystem.Get())
		WorldRegisteredHandle = subsystem->OnWorldRegistered.AddUObject(this, &AVAEmitter::OnWorldRegistered);
}

void AVAEmitter::OnWorldUnregistered()
{
	// Still waiting for a VAWorld
	if (!AudioWorld)
		return;

	// Misconfigured, or in its final removal. Neither joins the next VAWorld
	if (!registered)
	{
		ReleaseEmitter();
		AudioWorld = nullptr;
		return;
	}

	// The handle is about to be destroyed
	TeardownVisualisations();

	LeaveWorld();

	AudioWorld = nullptr;
	pendingRaytracingComplete = false;
	pendingRaytracedByListener = false;

	WaitForWorld();
}

void AVAEmitter::OnWorldRegistered()
{
	StopWaitingForWorld();

	if (!TryInitializeEmitter())
		return;

	// A visualisation that began play while this emitter was waiting
	InitializeVisualisations();
}

void AVAEmitter::InitializeVisualisations()
{
	TArray<UVAVisualisation*> visualisations;
	GetComponents(visualisations);

	for (UVAVisualisation* visualisation : visualisations)
		if (visualisation->HasBegunPlay())
			visualisation->InitializeVisualisation();
}

void AVAEmitter::TeardownVisualisations()
{
	TArray<UVAVisualisation*> visualisations;
	GetComponents(visualisations);

	for (UVAVisualisation* visualisation : visualisations)
		visualisation->TeardownVisualisation();
}

void AVAEmitter::StopWaitingForWorld()
{
	if (!WorldRegisteredHandle.IsValid())
		return;

	if (UVAWorldSubsystem* subsystem = BoundSubsystem.Get())
		subsystem->OnWorldRegistered.Remove(WorldRegisteredHandle);

	WorldRegisteredHandle.Reset();
}

bool AVAEmitter::TryInitializeEmitter()
{
	// Already failed once before, don't try again
	if (failedInitialisation)
		return false;

	// Already initialised, all is good
	if (registered)
		return true;

	if (!AudioWorld)
		AudioWorld = AVAWorld::Find(this);

	// The level's VAWorld hasn't begun play yet
	if (!AudioWorld)
		return false;

	foundWorld = true;

	if (!ValidateConfig() || !AttachToWorld())
	{
		// Failed validation or the SDK rejected it, so disable this actor
		SetActorTickEnabled(false);
		failedInitialisation = true;
		return false;
	}

	registered = true;
	return true;
}

bool AVAEmitter::AttachToWorld()
{
	CreateEmitter();

	// Properties are pushed before the emitter joins the world
	InitializeTypeSpecific();

	if (AudioWorld->RegisterEmitter(this))
		return true;

	DestroyUnaddedEmitter();
	return false;
}

void AVAEmitter::DetachFromWorld()
{
	AudioWorld->UnregisterEmitter(this);
}

void AVAEmitter::InitializeTypeSpecific()
{
	if (bAffectsGroupedEAX && (ReverbRayCount == 0 || ReverbBounceCount == 0))
	{
		VA_WARN_NAMED(TEXT("Has affectsGroupedEAX=true, but does not cast reverb rays"));
	}

	UpdateVAEmitter();
}

void AVAEmitter::DeinitializeTypeSpecific()
{
	CurrentGroupedEAXIndex = -1;
}

void AVAEmitter::CreateEmitter()
{
	VAEmitter* handle = vaEmitterCreate();

	vaEmitterSetLogCallback(handle, &VASdkEmitterLogCallback);
	vaEmitterSetLogErrorCallback(handle, &VASdkEmitterLogErrorCallback);
	vaEmitterSetOnRaytracingCompleteCallback(handle, &VAOnRaytracingCompleteTrampoline);
	vaEmitterSetOnRaytracedByAnotherEmitterCallback(handle, &VAOnRaytracedByAnotherEmitterTrampoline);
	vaEmitterSetOnRemovedCallback(handle, &VAOnRemovedTrampoline);

	AdoptEmitter(handle);
}

void AVAEmitter::AdoptEmitter(VAEmitter* handle)
{
	Emitter = handle;

	// Lets the callback trampolines resolve this actor from the VAEmitter* alone
	vaEmitterSetUserData(Emitter, this);
	vaEmitterSetName(Emitter, TCHAR_TO_UTF8(*VAActorName(this)));
	vaEmitterSetPositionUnreal(Emitter, GetActorLocation());
}

void AVAEmitter::DestroyUnaddedEmitter()
{
	vaEmitterSetUserData(Emitter, nullptr);
	vaEmitterDestroy(Emitter);
	Emitter = nullptr;
}

void AVAEmitter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	const bool waiting = WorldRegisteredHandle.IsValid();

	UnbindWorld();

	// Not when its VAWorld ended play first, e.g. as the level unloads
	if (waiting && !foundWorld)
		VA_WARN_NAMED(TEXT("Ended play without finding a VAWorld, so it never cast rays or played sound. Place a VAWorld in the level."));

	LeaveWorld();
}

void AVAEmitter::LeaveWorld()
{
	DeinitializeTypeSpecific();

	if (registered)
	{
		DetachFromWorld();
		registered = false;
	}

	ReleaseEmitter();
}

void AVAEmitter::ReleaseEmitter()
{
	if (!Emitter)
		return;

	VAWorld* vaWorld = AudioWorld ? AudioWorld->GetVAWorld() : nullptr;

	// The world ended first, and vaWorldDestroy unlinked this emitter from it, so it can be freed now
	if (!vaWorld)
	{
		vaEmitterSetUserData(Emitter, nullptr);
		VAResult result = vaEmitterDestroy(Emitter);

		if (result != VA_SUCCESS)
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to destroy the emitter after its world ended."));

		Emitter = nullptr;
		return;
	}

	// Already removed, and vaWorldRemoveEmitter returns VA_NOT_FOUND once the tail has finished but the in-flight results haven't been handled. OnRemoved is still to come, so the world takes over the handle
	if (finalRemoval)
	{
		vaEmitterSetUserData(Emitter, nullptr);
		AudioWorld->AddOrphanedEmitter(Emitter);
		Emitter = nullptr;
		return;
	}

	// If no raytracing results were pending, OnRemoved has already run, handing the handle to the world and clearing Emitter
	VAResult result = vaWorldRemoveEmitter(vaWorld, Emitter);

	if (!Emitter)
		return;

	switch (result)
	{
		// OnRemoved fires once the in-flight raytracing results are handled, or once its reverb tail finishes (VA_PENDING_REMOVAL). This actor may be gone by then, so the world takes over the handle
		case VA_SUCCESS:
		case VA_PENDING_REMOVAL:
			vaEmitterSetUserData(Emitter, nullptr);
			AudioWorld->AddOrphanedEmitter(Emitter);
			break;

		// Never added to the world
		case VA_NOT_FOUND:
			vaEmitterSetUserData(Emitter, nullptr);
			vaEmitterDestroy(Emitter);
			break;

		default:
			VA_ERROR_NAMED_RESULT(result, TEXT("Failed to remove the emitter from its world."));
			vaEmitterSetUserData(Emitter, nullptr);
			AudioWorld->AddOrphanedEmitter(Emitter);
			break;
	}

	Emitter = nullptr;
}

void AVAEmitter::BeginFinalRemoval()
{
	finalRemoval = true;

	// The SDK refuses changes to the handle from here on
	TeardownVisualisations();

	if (registered)
	{
		DetachFromWorld();
		registered = false;
	}

	VAWorld* vaWorld = AudioWorld ? AudioWorld->GetVAWorld() : nullptr;

	if (!Emitter || !vaWorld)
	{
		ReleaseEmitter();
		return;
	}

	// The user data stays set, so OnRemoved clears Emitter. It has already run if no raytracing results were pending
	VAResult result = vaWorldRemoveEmitter(vaWorld, Emitter);

	if (result != VA_SUCCESS && result != VA_PENDING_REMOVAL)
	{
		VA_ERROR_NAMED_RESULT(result, TEXT("Failed to remove the emitter from its world."));
		ReleaseEmitter();
	}
}

void AVAEmitter::OnEmitterRemoved(VAEmitter* handle)
{
	AudioWorld->DeferEmitterDestroy(handle);

	if (Emitter == handle)
		Emitter = nullptr;
}

void AVAEmitter::QueueRaytracingComplete()
{
	pendingRaytracingComplete = true;
	AudioWorld->QueueEmitterEvents(this);
}

void AVAEmitter::QueueRaytracedByListener(float gainLF, float gainHF)
{
	pendingRaytracedByListener = true;
	pendingGainLF = gainLF;
	pendingGainHF = gainHF;
	AudioWorld->QueueEmitterEvents(this);
}

void AVAEmitter::FlushPendingEvents()
{
	if (pendingRaytracingComplete)
	{
		pendingRaytracingComplete = false;
		OnRaytracingComplete.Broadcast();
	}

	// A handler above may have destroyed this actor
	if (pendingRaytracedByListener && IsValid(this))
	{
		pendingRaytracedByListener = false;
		OnRaytracedByListener.Broadcast(pendingGainLF, pendingGainHF);
	}
}

void AVAEmitter::Tick(float DeltaTime)
{
	// Initialisation failed after the tick function was registered, or the SDK refuses changes as this emitter is leaving
	if (!Emitter || finalRemoval)
		return;

	Super::Tick(DeltaTime);

	vaEmitterSetPositionUnreal(Emitter, GetActorLocation());

	TickTypeSpecific(DeltaTime);

	// Waits for IsReadyToPlay, as an emitter suspended before its first results has no reverb to keep
	if ((bRaytraceOnce || suspendRequested) && IsReadyToPlay())
		vaEmitterSetSuspended(Emitter, true);
}

void AVAEmitter::SetSuspended(bool bSuspended)
{
	// The SDK refuses changes to the handle once it's being removed
	if (finalRemoval)
		return;

	if (IsA<AVAListener>())
	{
		VA_WARN_NAMED(TEXT("SetSuspended has no effect on a listener, as its rays produce the muffling of every other emitter"));
		return;
	}

	suspendRequested = bSuspended;

	if (bSuspended)
		return;

	// Otherwise the next Tick would suspend it again
	bRaytraceOnce = false;

	if (Emitter)
		vaEmitterSetSuspended(Emitter, false);
}

bool AVAEmitter::IsSuspended() const
{
	return Emitter && vaEmitterGetSuspended(Emitter);
}

void AVAEmitter::TickTypeSpecific(float DeltaTime)
{
	if (bAffectsGroupedEAX)
		CurrentGroupedEAXIndex = vaEmitterGetGroupedEAXIndex(Emitter);
}

void AVAEmitter::UpdateVAEmitter()
{
	vaEmitterSetReverbRayCount(Emitter, ReverbRayCount);
	vaEmitterSetReverbBounceCount(Emitter, ReverbBounceCount);
	vaEmitterSetReverbEnergyCap(Emitter, ReverbEnergyCap);
	vaEmitterSetMinimumReverbEnergy(Emitter, MinimumReverbEnergy);
	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetMaxEchogramTime(Emitter, MaxEchogramTime);
	vaEmitterSetEchogramGranularity(Emitter, EchogramGranularity);
	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX);
	vaEmitterSetKeepReverbTailAlive(Emitter, bKeepReverbTailAlive);
	vaEmitterSetHasRelativeReverb(Emitter, bHasRelativeReverb);

	vaEmitterSetOcclusionEnergyCap(Emitter, OcclusionEnergyCap);
	vaEmitterSetPermeationEnergyCap(Emitter, PermeationEnergyCap);

	vaEmitterSetAmbientOcclusionRayCount(Emitter, AmbientOcclusionRayCount);
	vaEmitterSetAmbientOcclusionBounceCount(Emitter, AmbientOcclusionBounceCount);
	vaEmitterSetAmbientOcclusionEnergyCap(Emitter, AmbientOcclusionEnergyCap);
	vaEmitterSetMinimumAmbientOcclusionEnergy(Emitter, MinimumAmbientOcclusionEnergy);
	vaEmitterSetAmbientPermeationRayCount(Emitter, AmbientPermeationRayCount);
	vaEmitterSetAmbientPermeationBounceCount(Emitter, AmbientPermeationBounceCount);
	vaEmitterSetAmbientPermeationEnergyCap(Emitter, AmbientPermeationEnergyCap);
	vaEmitterSetMinimumAmbientPermeationEnergy(Emitter, MinimumAmbientPermeationEnergy);

	vaEmitterSetClusteringEnabled(Emitter, bClusteringEnabled);
	vaEmitterSetClusteringDistance(Emitter, FMath::Max(0.01f, ClusteringDistance));

	vaEmitterSetType(Emitter, Type);
	vaEmitterSetTrailRefreshCount(Emitter, TrailRefreshCount);
	vaEmitterSetRefreshDistanceThreshold(Emitter, RefreshDistanceThreshold);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
	vaEmitterSetClampPosition(Emitter, bClampPosition);

	vaEmitterSetRandomTrailColor(Emitter, bRandomTrailColor);
	vaEmitterSetTrailColor(Emitter, FColorToVA(TrailColor));
	vaEmitterSetReverbColor(Emitter, FColorToVA(ReverbColor));
	vaEmitterSetOcclusionColor(Emitter, FColorToVA(OcclusionColor));
	vaEmitterSetPermeationColor(Emitter, FColorToVA(PermeationColor));
	vaEmitterSetAmbientPermeationColor(Emitter, FColorToVA(AmbientPermeationColor));
}

bool AVAEmitter::IsRaytraced() const
{
	return Emitter && !vaEmitterGetInitialising(Emitter);
}

bool AVAEmitter::IsReadyToPlay() const
{
	if (finalRemoval)
		return false;

	if (!GetMufflingResult())
		return false;

	// Its grouped EAX slot, and so its reverb send, isn't known until it casts its own reverb rays
	if (vaEmitterGetAffectsGroupedEAX(Emitter) && vaEmitterGetReverbEnabled(Emitter))
		return vaEmitterGetEAX(Emitter) != nullptr;

	return true;
}

int32 AVAEmitter::GetGroupedEAXIndex() const
{
	return CurrentGroupedEAXIndex;
}

bool AVAEmitter::GetWithinWorldBounds() const
{
	return Emitter && vaEmitterGetWithinWorldBounds(Emitter);
}

FVector AVAEmitter::GetVAPosition() const
{
	return Emitter ? VAVectorToFVector(vaEmitterGetPosition(Emitter)) : GetActorLocation();
}

void AVAEmitter::GetReverbResult(bool& bSuccess, FVAEAXReverbResult& Result) const
{
	VAEAXReverb* EAX = Emitter ? vaEmitterGetEAX(Emitter) : nullptr;

	// Raytracing has not completed at least once yet
	if (!EAX)
	{
		bSuccess = false;
		Result = FVAEAXReverbResult();
		return;
	}

	bSuccess = true;
	Result.OutsidePercent = EAX->outsidePercent;
	Result.ReturnedPercent = EAX->returnedPercent;
	Result.MaterialAbsorptionLF = EAX->materialAbsorptionLF;
	Result.MaterialAbsorptionHF = EAX->materialAbsorptionHF;
	Result.MaterialRoughness = EAX->materialRoughness;
	Result.ReflectionsDelay = EAX->reflectionsDelay;
	Result.Density = EAX->density;
	Result.Diffusion = EAX->diffusion;
	Result.GainLF = EAX->gainLF;
	Result.GainHF = EAX->gainHF;
	Result.Gain = EAX->gain;
	Result.DecayTime = EAX->decayTime;
	Result.DecayLFRatio = EAX->decayLFRatio;
	Result.DecayHFRatio = EAX->decayHFRatio;
	Result.ReflectionsGain = EAX->reflectionsGain;
	Result.LateReverbGain = EAX->lateReverbGain;
	Result.LateReverbDelay = EAX->lateReverbDelay;
	Result.EchoTime = EAX->echoTime;
	Result.EchoDepth = EAX->echoDepth;
	Result.ModulationTime = EAX->modulationTime;
	Result.ModulationDepth = EAX->modulationDepth;
	Result.AirAbsorptionGainHF = EAX->airAbsorptionGainHF;
	Result.HFReference = EAX->hfReference;
	Result.LFReference = EAX->lfReference;
	Result.RoomRolloffFactor = EAX->roomRolloffFactor;
	Result.bDecayHFLimit = EAX->decayHFLimit != 0;
}

void AVAEmitter::GetAmbientFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const
{
	VALowPassFilter* AmbientFilter = Emitter ? vaEmitterGetAmbientFilter(Emitter) : nullptr;

	// Raytracing has not completed at least once yet
	if (!AmbientFilter)
	{
		bSuccess = false;
		GainLF = 0.0f;
		GainHF = 0.0f;
		return;
	}

	bSuccess = true;
	GainLF = AmbientFilter->gainLF;
	GainHF = AmbientFilter->gainHF;
}

VALowPassFilter* AVAEmitter::GetMufflingResult() const
{
	if (!AudioWorld || !Emitter)
		return nullptr;

	AVAListener* Listener = AudioWorld->GetMainListener();

	if (!Listener || !Listener->GetVAEmitter())
		return nullptr;

	VALowPassFilter* filter = nullptr;
	vaEmitterGetTargetFilter(Listener->GetVAEmitter(), Emitter, &filter);
	return filter;
}

void AVAEmitter::GetMufflingFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const
{
	VALowPassFilter* MufflingFilter = GetMufflingResult();

	// Raytracing has not completed at least once yet
	if (!MufflingFilter)
	{
		bSuccess = false;
		GainLF = 0.0f;
		GainHF = 0.0f;
		return;
	}

	bSuccess = true;
	GainLF = MufflingFilter->gainLF;
	GainHF = MufflingFilter->gainHF;
}

bool AVAEmitter::ResolveReverbSend(USoundSubmix*& OutSubmix, float& OutSendLevel) const
{
	OutSubmix = nullptr;
	OutSendLevel = 0.0f;

	// Not added to a world yet, or already removed
	if (!AudioWorld || !Emitter)
		return false;

	VAWorld* vaWorld = AudioWorld->GetVAWorld();
	AVAListener* Listener = AudioWorld->GetMainListener();

	// The listener ended play
	if (!vaWorld || !Listener || !Listener->GetVAEmitter())
		return false;

	int32 GroupedEAXIndex = bAffectsGroupedEAX ? GetGroupedEAXIndex() : -1;

	if (GroupedEAXIndex >= 0)
	{
		int groupedEAXCount = vaWorldGetGroupedEAXCount(vaWorld);

		if (GroupedEAXIndex >= groupedEAXCount)
		{
			VA_WARN_NAMED(TEXT("Has an invalid grouped EAX index: %d. There are only %d grouped EAX submixes available"), GroupedEAXIndex, groupedEAXCount);
			return false;
		}

		// A grouped EAX index is only assigned once this emitter's own reverb rays have completed, so its grouped EAX is guaranteed to be available
		const VAEAXReverb* EAX = vaWorldGetGroupedEAX(vaWorld)[GroupedEAXIndex];

		// Only relative gain is supported. Can't do directional reverb in Unreal :(
		const float* relativeGain = vaEAXReverbGetRelativeGain(EAX, Listener->GetVAEmitter());

		if (!relativeGain)
			return false;

		// Null if the user assigned a null submix to World.groupedEAX[]. A warning is already logged in VAWorld.cpp
		OutSubmix = AudioWorld->GetGroupedEAXSubmix(GroupedEAXIndex);
		OutSendLevel = *relativeGain;
		return true;
	}

	if (bUseListenerReverb && Listener->ListenerReverbSubmix)
	{
		OutSubmix = Listener->ListenerReverbSubmix;
		OutSendLevel = 1.0f;
	}

	return true;
}

#if WITH_EDITOR
void AVAEmitter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Emitter only exists while PIE/game is running and TryInitializeEmitter() has completed - editing properties on a placed actor in the editor (not PIE) hits this every time
	if (!Emitter || finalRemoval)
		return;

	UpdateVAEmitter();
}
#endif
