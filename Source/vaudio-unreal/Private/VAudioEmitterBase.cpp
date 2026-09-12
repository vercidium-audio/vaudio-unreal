#include "VAudioEmitterBase.h"
#include "VAudioWorld.h"
#include "VAudioListener.h"

extern "C" {
#include "vaudio.h"
}

#include "VAConstants.h"
#include "VARawLog.h"
#include "VADebugMessageKeys.h"

// vaEmitterSetUserData() stashes the owning actor on the VAEmitter* itself, so these trampolines
// can resolve identity directly instead of needing a side registry.
static void VAOnRaytracingCompleteTrampoline(VAEmitter* emitter)
{
	if (AVAudioEmitterBase* Owner = static_cast<AVAudioEmitterBase*>(vaEmitterGetUserData(emitter)))
		Owner->OnRaytracingComplete.Broadcast();
}

static void VAOnRaytracedByAnotherEmitterTrampoline(VAEmitter* source, VAEmitter* target)
{
	AVAudioEmitterBase* Owner = static_cast<AVAudioEmitterBase*>(vaEmitterGetUserData(target));

	if (!Owner)
		return;

	VALowPassFilter* Filter = vaEmitterGetTargetFilter(source, target);

	if (Filter)
		Owner->OnRaytracedByListener.Broadcast(Filter->gainLF, Filter->gainHF);
}

void AVAudioEmitterBase::DisplayWarning(const TCHAR* fmt, ...) const
{
	// Format the string
	va_list args;
	va_start(args, fmt);
	TCHAR buffer[1024];
	FCString::GetVarArgs(buffer, UE_ARRAY_COUNT(buffer), fmt, args);
	va_end(args);

	DisplayDebugWarning(VAEmitterMessageBase + GetUniqueID(), TEXT("%s"), buffer);
}

void AVAudioEmitterBase::ClearWarning() const
{
	ClearDebugWarning(VAEmitterMessageBase + GetUniqueID());
}

AVAudioEmitterBase::AVAudioEmitterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	UBillboardComponent* Root = CreateDefaultSubobject<UBillboardComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AVAudioEmitterBase::BeginPlay()
{
	Super::BeginPlay();

	// Disable the actor if validation fails
	if (!AudioWorld)
	{
		DisplayWarning(TEXT("[VA] '%s' does not have an AudioWorld assigned and will not cast rays or play sound"), *GetActorNameOrLabel());
		SetActorTickEnabled(false);
		return;
	}

	TryInitializeEmitter();
}

bool AVAudioEmitterBase::TryInitializeEmitter()
{
	check(AudioWorld);

	// Already failed once before, don't try again
	if (failedInitialisation)
		return false;

	// Already initialised, all is good
	if (Emitter)
		return true;


	// If the world is a child actor of the emitter, initialise it here first:
	AudioWorld->InitializeVAWorld();
	VAWorld* vaWorld = AudioWorld->GetVAWorld();

	bool configPass = ValidateConfig();

	if (!configPass)
	{
		// Failed validation, disable this actor
		SetActorTickEnabled(false);

		failedInitialisation = true;
		return false;
	}

	// Create the emitter
	check(!Emitter);

	Emitter = vaEmitterCreate();
	vaEmitterSetName(Emitter, TCHAR_TO_UTF8(*GetActorNameOrLabel()));
	
	vaEmitterSetLogCallback(Emitter, &VASdkLogCallback);
	vaEmitterSetLogErrorCallback(Emitter, &VASdkLogCallback);
	vaEmitterSetPositionUnreal(Emitter, GetActorLocation());

	// Lets the callback trampolines below resolve this actor from the VAEmitter* alone
	vaEmitterSetUserData(Emitter, this);
	vaEmitterSetOnRaytracingCompleteCallback(Emitter, &VAOnRaytracingCompleteTrampoline);
	vaEmitterSetOnRaytracedByAnotherEmitterCallback(Emitter, &VAOnRaytracedByAnotherEmitterTrampoline);

	// Add the emitter to the world
	VAResult result = vaWorldAddEmitter(vaWorld, Emitter);

	check(result == VA_SUCCESS);

	if (result == VA_ALREADY_EXISTS)
	{
		DisplayWarning(TEXT("[VA] '%s' was added to AudioWorld '%s' twice"), *GetActorNameOrLabel(), *AudioWorld->GetActorNameOrLabel());
	}
	else if (result == VA_WORLD_CONFLICT)
	{
		DisplayWarning(TEXT("[VA] '%s' cannot be added to AudioWorld '%s' as it is already added to another world"), *GetActorNameOrLabel(), *AudioWorld->GetActorNameOrLabel());
	}

	// Initialise the specific emitter type (e.g. Listener adds targets, Source, Continuous, etc)
	InitializeTypeSpecific();

	AudioWorld->RegisterEmitter(this);
	registered = true;
	return true;
}

void AVAudioEmitterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	DeinitializeTypeSpecific();

	if (registered)
	{
		AudioWorld->UnregisterEmitter(this);
		registered = false;
	}

	if (Emitter)
	{
		// The emitter can outlive this actor (see TODO below), so clear the userData pointer now
		// to stop the callback trampolines from resolving a dangling actor.
		vaEmitterSetUserData(Emitter, nullptr);

		// TODO - Can't just kill it here - need to wait for world pendingshutdown
		//vaEmitterDestroy(Emitter);
		//Emitter = nullptr;
	}
}

void AVAudioEmitterBase::Tick(float DeltaTime)
{
	// TODO - Source still calling Tick even if it failed validation above
	if (!Emitter)
		return;

	check(Emitter);
	check(AudioWorld);

	Super::Tick(DeltaTime);

	vaEmitterSetPositionUnreal(Emitter, GetActorLocation());

	TickTypeSpecific(DeltaTime);
}

void AVAudioEmitterBase::UpdateVAEmitter()
{
	vaEmitterSetReverbRayCount(Emitter, ReverbRayCount);
	vaEmitterSetReverbBounceCount(Emitter, ReverbBounceCount);
	vaEmitterSetReverbEnergyCap(Emitter, ReverbEnergyCap);
	vaEmitterSetMaxEchogramTime(Emitter, MaxEchogramTime);
	vaEmitterSetEchogramGranularity(Emitter, EchogramGranularity);

	vaEmitterSetOcclusionEnergyCap(Emitter, OcclusionEnergyCap);
	vaEmitterSetPermeationEnergyCap(Emitter, PermeationEnergyCap);

	vaEmitterSetAmbientOcclusionRayCount(Emitter, AmbientOcclusionRayCount);
	vaEmitterSetAmbientOcclusionBounceCount(Emitter, AmbientOcclusionBounceCount);
	vaEmitterSetAmbientOcclusionEnergyCap(Emitter, AmbientOcclusionEnergyCap);
	vaEmitterSetAmbientPermeationRayCount(Emitter, AmbientPermeationRayCount);
	vaEmitterSetAmbientPermeationBounceCount(Emitter, AmbientPermeationBounceCount);
	vaEmitterSetAmbientPermeationEnergyCap(Emitter, AmbientPermeationEnergyCap);

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

void AVAudioEmitterBase::GetReverbResult(bool& bSuccess, FVAEAXReverbResult& Result) const
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

void AVAudioEmitterBase::GetAmbientFilterResult(bool& bSuccess, float& GainLF, float& GainHF) const
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
