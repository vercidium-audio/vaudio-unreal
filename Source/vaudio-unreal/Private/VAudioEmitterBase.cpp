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


	// Create the emitter
	Emitter = vaEmitterCreate();
	vaEmitterSetLogCallback(Emitter, &VASdkLogCallback);
	vaEmitterSetLogErrorCallback(Emitter, &VASdkLogCallback);
	vaEmitterSetPositionUnreal(Emitter, GetActorLocation());


	// Initialise the specific emitter type (Source, Continuous, etc)
	bool pass = InitializeTypeSpecific();

	if (pass)
	{
		// Add the emitter to the world
		VAResult result = vaWorldAddEmitter(vaWorld, Emitter);

		if (result == VA_ALREADY_EXISTS)
		{
			DisplayWarning(TEXT("[VA] '%s' was added to AudioWorld '%s' twice"), *GetActorNameOrLabel(), *AudioWorld->GetActorNameOrLabel());
		}
		else if (result == VA_WORLD_CONFLICT)
		{
			DisplayWarning(TEXT("[VA] '%s' cannot be added to AudioWorld '%s' as it is already added to another world"), *GetActorNameOrLabel(), *AudioWorld->GetActorNameOrLabel());
		}
		else
		{
			check(result == VA_SUCCESS);
		}

		if (result == VA_SUCCESS)
		{
			AudioWorld->RegisterEmitter(this);
			registered = true;
			return true;
		}
	}

	// Failed validation, disable this actor
	SetActorTickEnabled(false);

	// The listener calls this function when iterating its targets, so ensure we set everything here
	vaEmitterDestroy(Emitter);
	Emitter = nullptr;
	failedInitialisation = true;

	return false;

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
		// TODO - Can't just kill it here - need to wait for world pendingshutdown
		//vaEmitterDestroy(Emitter);
		//Emitter = nullptr;
	}
}

void AVAudioEmitterBase::Tick(float DeltaTime)
{
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

	vaEmitterSetRefreshRayCount(Emitter, RefreshRayCount);
	vaEmitterSetRefreshDistanceThreshold(Emitter, RefreshDistanceThreshold);

	vaEmitterSetVisualisationRayCount(Emitter, VisualisationRayCount);
	vaEmitterSetVisualisationBounceCount(Emitter, VisualisationBounceCount);
	vaEmitterSetVisualisationUpdateFrequency(Emitter, VisualisationUpdateFrequency);

	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
}
