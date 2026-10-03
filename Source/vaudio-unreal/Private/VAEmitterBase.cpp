#include "VAEmitterBase.h"
#include "VAWorld.h"
#include "VAListener.h"

extern "C" {
#include "vaudio.h"
}

#include "VAConstants.h"
#include "VALog.h"

// vaEmitterSetUserData() stashes the owning actor on the VAEmitter* itself, so these trampolines
// can resolve identity directly instead of needing a side registry.
static void VAOnRaytracingCompleteTrampoline(VAEmitter* emitter)
{
	if (AVAEmitterBase* owner = static_cast<AVAEmitterBase*>(vaEmitterGetUserData(emitter)))
		owner->QueueRaytracingComplete();
}

static void VAOnRaytracedByAnotherEmitterTrampoline(VAEmitter* source, VAEmitter* target)
{
	AVAEmitterBase* owner = static_cast<AVAEmitterBase*>(vaEmitterGetUserData(target));

	if (!owner)
		return;

	VALowPassFilter* filter = vaEmitterGetTargetFilter(source, target);

	if (filter)
		owner->QueueRaytracedByListener(filter->gainLF, filter->gainHF);
}

static void VAOnRemovedTrampoline(VAEmitter* emitter)
{
	if (AVAEmitterBase* owner = static_cast<AVAEmitterBase*>(vaEmitterGetUserData(emitter)))
	{
		owner->OnEmitterRemoved(emitter);
		return;
	}

	// The actor already released this handle while its removal was pending
	AVAWorld::OnOrphanedEmitterRemoved(emitter);
}

AVAEmitterBase::AVAEmitterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	UBillboardComponent* Root = CreateDefaultSubobject<UBillboardComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AVAEmitterBase::BeginPlay()
{
	Super::BeginPlay();

	// Disable the actor if validation fails
	if (!AudioWorld)
	{
		VA_WARN_NAMED(TEXT("Does not have an AudioWorld assigned and will not cast rays or play sound"));
		SetActorTickEnabled(false);
		return;
	}

	TryInitializeEmitter();
}

bool AVAEmitterBase::TryInitializeEmitter()
{
	// Already failed once before, don't try again
	if (!AudioWorld || failedInitialisation)
		return false;

	// Already initialised, all is good
	if (Emitter)
		return true;

	// The world may not have begun play yet, since actor BeginPlay order isn't guaranteed
	AudioWorld->InitializeVAWorld();

	if (!ValidateConfig())
	{
		// Failed validation, disable this actor
		SetActorTickEnabled(false);
		failedInitialisation = true;
		return false;
	}

	Emitter = vaEmitterCreate();
	vaEmitterSetName(Emitter, TCHAR_TO_UTF8(*GetActorNameOrLabel()));

	vaEmitterSetLogCallback(Emitter, &VASdkLogCallback);
	vaEmitterSetLogErrorCallback(Emitter, &VASdkLogErrorCallback);
	vaEmitterSetPositionUnreal(Emitter, GetActorLocation());

	// Lets the callback trampolines resolve this actor from the VAEmitter* alone
	vaEmitterSetUserData(Emitter, this);
	vaEmitterSetOnRaytracingCompleteCallback(Emitter, &VAOnRaytracingCompleteTrampoline);
	vaEmitterSetOnRaytracedByAnotherEmitterCallback(Emitter, &VAOnRaytracedByAnotherEmitterTrampoline);
	vaEmitterSetOnRemovedCallback(Emitter, &VAOnRemovedTrampoline);

	// Properties are pushed before the emitter joins the world, so the listener's ray counts are set before targets are added to it
	InitializeTypeSpecific();

	if (!AudioWorld->RegisterEmitter(this))
	{
		// Never added to the world, so it can be destroyed straight away
		vaEmitterSetUserData(Emitter, nullptr);
		vaEmitterDestroy(Emitter);
		Emitter = nullptr;

		SetActorTickEnabled(false);
		failedInitialisation = true;
		return false;
	}

	registered = true;
	return true;
}

void AVAEmitterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	DeinitializeTypeSpecific();

	if (registered)
	{
		AudioWorld->UnregisterEmitter(this);
		registered = false;
	}

	ReleaseEmitter();
}

void AVAEmitterBase::ReleaseEmitter()
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

void AVAEmitterBase::OnEmitterRemoved(VAEmitter* handle)
{
	AudioWorld->DeferEmitterDestroy(handle);

	if (Emitter == handle)
		Emitter = nullptr;
}

void AVAEmitterBase::QueueRaytracingComplete()
{
	pendingRaytracingComplete = true;
	AudioWorld->QueueEmitterEvents(this);
}

void AVAEmitterBase::QueueRaytracedByListener(float gainLF, float gainHF)
{
	pendingRaytracedByListener = true;
	pendingGainLF = gainLF;
	pendingGainHF = gainHF;
	AudioWorld->QueueEmitterEvents(this);
}

void AVAEmitterBase::FlushPendingEvents()
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

void AVAEmitterBase::Tick(float DeltaTime)
{
	// Initialisation failed after the tick function was registered
	if (!Emitter)
		return;

	Super::Tick(DeltaTime);

	vaEmitterSetPositionUnreal(Emitter, GetActorLocation());

	TickTypeSpecific(DeltaTime);
}

void AVAEmitterBase::UpdateVAEmitter()
{
	vaEmitterSetReverbRayCount(Emitter, ReverbRayCount);
	vaEmitterSetReverbBounceCount(Emitter, ReverbBounceCount);
	vaEmitterSetReverbEnergyCap(Emitter, ReverbEnergyCap);
	vaEmitterSetMinimumReverbEnergy(Emitter, MinimumReverbEnergy);
	vaEmitterSetMaxEchogramTime(Emitter, MaxEchogramTime);
	vaEmitterSetEchogramGranularity(Emitter, EchogramGranularity);

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

	vaEmitterSetTrailRefreshCount(Emitter, TrailRefreshCount);
	vaEmitterSetRefreshDistanceThreshold(Emitter, RefreshDistanceThreshold);

	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);

	vaEmitterSetRandomTrailColor(Emitter, bRandomTrailColor);
	vaEmitterSetTrailColor(Emitter, FColorToVA(TrailColor));
	vaEmitterSetReverbColor(Emitter, FColorToVA(ReverbColor));
	vaEmitterSetOcclusionColor(Emitter, FColorToVA(OcclusionColor));
	vaEmitterSetPermeationColor(Emitter, FColorToVA(PermeationColor));
	vaEmitterSetAmbientPermeationColor(Emitter, FColorToVA(AmbientPermeationColor));
}

void AVAEmitterBase::GetReverbResult(bool& bSuccess, FVAEAXReverbResult& Result) const
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

void AVAEmitterBase::GetAmbientFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const
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
