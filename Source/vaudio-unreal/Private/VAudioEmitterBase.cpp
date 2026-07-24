#include "VAudioEmitterBase.h"
#include "VAudioWorld.h"

extern "C" {
#include "vaudio.h"
}

#include "VAConstants.h"
#include "VARawLog.h"
#include "VADebugMessageKeys.h"

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

AVAudioEmitterBase::AVAudioEmitterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	UBillboardComponent* Root = CreateDefaultSubobject<UBillboardComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AVAudioEmitterBase::BeginPlay()
{
	Super::BeginPlay();

	// Display a warning if the user forgot to set the AudioWorld
	if (!AudioWorld)
	{
		DisplayWarning(TEXT("[VA] Emitter '%s' does not have an AudioWorld assigned and will not play"), *GetActorNameOrLabel());
		return;
	}

	// AVAudioWorld may not have run its own BeginPlay yet (actor BeginPlay order is not guaranteed), in which case GetVAWorld() is still null here.
	// TryInitializeEmitter() is safe to call repeatedly - it no-ops if Emitter is already set - so Tick() retries it every frame until AudioWorld's VAWorld becomes valid.
	TryInitializeEmitter();
}

bool AVAudioEmitterBase::TryInitializeEmitter()
{
	// Already initialised, all is good
	if (Emitter)
		return true;

	// The user forgot to set the AudioWorld
	if (!AudioWorld)
		return false;

	// AVAudioWorld's own BeginPlay may not have run yet (actor BeginPlay order isn't guaranteed) e.g. if the AudioWorld is a child actor of an Emitter, the Emitter actor initialises first.
	// So initialise it here anyway, rather than doing a deferred creation in Tick()
	AudioWorld->InitializeVAWorld();

	VAWorld* vaWorld = AudioWorld->GetVAWorld();

	Emitter = vaEmitterCreate();

	vaEmitterSetLogCallback(Emitter, &VASdkLogCallback);
	vaEmitterSetLogErrorCallback(Emitter, &VASdkLogCallback);

	FVector Pos = GetActorLocation();
	vaEmitterSetPositionUnreal(Emitter, Pos);

	InitializeTypeSpecific();

	VAResult result = vaWorldAddEmitter(vaWorld, Emitter);

	// VA_INVALID_VALUE = already added to this world, VA_OUT_OF_RANGE = already added to another world
	if (result != VA_SUCCESS)
	{
		VALog(L"vaWorldAddEmitter() failed with result %d - this emitter may already be registered to a VAudioWorld.", result);
	}

	AudioWorld->RegisterEmitter(this);

	VALog(L"Complete. vaWorldAddEmitter() returned %d", result);
	return true;
}

void AVAudioEmitterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	DeinitializeTypeSpecific();

	if (AudioWorld)
	{
		AudioWorld->UnregisterEmitter(this);

		VAWorld* vaWorld = AudioWorld->GetVAWorld();

		if (!vaWorld)
		{
			// AVAudioWorld::EndPlay() may run before this emitter's EndPlay (actor EndPlay order
			// isn't guaranteed), destroying the VAWorld first - nothing to remove ourselves from in that case.
			VALog(L"vaWorld is null.");
		}
		else if (!Emitter)
		{
			// TryInitializeEmitter() never got far enough to create Emitter (e.g. AudioWorld's
			// VAWorld never became valid during this actor's lifetime) - nothing to remove.
			VALog(L"Can't remove emitter from world as Emitter is null.");
		}
		else
		{
			vaWorldRemoveEmitter(vaWorld, Emitter);
			VALog(L"Emitter successfully removed from vaWorld.");
		}
	}

	if (Emitter)
	{
		vaEmitterDestroy(Emitter);
		Emitter = nullptr;
	}
	else
	{
		// Same as above: TryInitializeEmitter() never ran to completion during this actor's lifetime.
		VALog(L"Can't destroy emitter as Emitter is null.");
	}
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

	vaEmitterSetRefreshRayCount(Emitter, RefreshRayCount);
	vaEmitterSetRefreshDistanceThreshold(Emitter, RefreshDistanceThreshold);

	vaEmitterSetVisualisationRayCount(Emitter, VisualisationRayCount);
	vaEmitterSetVisualisationBounceCount(Emitter, VisualisationBounceCount);
	vaEmitterSetVisualisationUpdateFrequency(Emitter, VisualisationUpdateFrequency);

	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
}

void AVAudioEmitterBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	FVector Pos = GetActorLocation();
	vaEmitterSetPositionUnreal(Emitter, Pos);

	TickTypeSpecific(DeltaTime);
}
