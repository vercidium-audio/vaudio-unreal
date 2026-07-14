#include "VAudioEmitter.h"
#include "VAudioWorld.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "AudioMixerBlueprintLibrary.h"
#include "ActiveSound.h"
#include "AudioDevice.h"
#include "Sound/SoundEffectSource.h"
#include "SourceEffects/SourceEffectFilter.h"

extern "C" {
#include "vaudio.h"
}

#include "VaRawLog.h"
#include <Engine/Engine.h>
#include <Engine/EngineTypes.h>

const float MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
const float MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;
const float LOW_PASS_RESONANCE = 0.707f; // Butterworth Q constant

AVAudioEmitter::AVAudioEmitter()
{
	PrimaryActorTick.bCanEverTick = true;

	UBillboardComponent* Root = CreateDefaultSubobject<UBillboardComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AVAudioEmitter::BeginPlay()
{
	Super::BeginPlay();

	// Display a warning if the user forgot to set the AudioWorld
	if (!AudioWorld)
	{
		VALog(L"AudioWorld is not assigned - this emitter will do nothing. Assign a VAudioWorld actor in the Details panel.");
		return;
	}

	// AVAudioWorld may not have run its own BeginPlay yet (actor BeginPlay order is not guaranteed), in which case GetVAWorld() is still null here.
	//  TryInitializeEmitter() is safe to call repeatedly - it no-ops if Emitter is already set - so Tick() retries it every frame until AudioWorld's VAWorld becomes valid.
	TryInitializeEmitter();
}

bool AVAudioEmitter::TryInitializeEmitter()
{
	// Already initialised, all is good
	if (Emitter)
		return true;

	// The user forgot to set the AudioWorld
	if (!AudioWorld)
		return false;

	VAWorld* vaWorld = AudioWorld->GetVAWorld();

	// AVAudioWorld's own BeginPlay hasn't run yet (actor BeginPlay order isn't guaranteed)
	if (!vaWorld)
		return false;

	Emitter = vaEmitterCreate();

	vaEmitterSetLogCallback(Emitter, &VaSdkLogCallback);
	vaEmitterSetLogErrorCallback(Emitter, &VaSdkLogCallback);

	FVector Pos = GetActorLocation();
	vaEmitterSetPosition(Emitter, vaVectorCreate((float)Pos.X, (float)Pos.Y, (float)Pos.Z));

	vaEmitterSetReverbRayCount(Emitter, ReverbRayCount);
	vaEmitterSetReverbBounceCount(Emitter, ReverbBounceCount);
	vaEmitterSetReverbEnergyCap(Emitter, ReverbEnergyCap);
	vaEmitterSetMaxEchogramTime(Emitter, MaxEchogramTime);
	vaEmitterSetEchogramGranularity(Emitter, EchogramGranularity);

	vaEmitterSetOcclusionRayCount(Emitter, OcclusionRayCount);
	vaEmitterSetOcclusionBounceCount(Emitter, OcclusionBounceCount);
	vaEmitterSetOcclusionEnergyCap(Emitter, OcclusionEnergyCap);

	vaEmitterSetPermeationRayCount(Emitter, PermeationRayCount);
	vaEmitterSetPermeationBounceCount(Emitter, PermeationBounceCount);
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

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
	vaEmitterSetReservedEmitterCount(Emitter, ReservedEmitterCount);
	vaEmitterSetMinimumPermeationEnergy(Emitter, MinimumPermeationEnergy);
	vaEmitterSetRelativeReverbInnerThreshold(Emitter, RelativeReverbInnerThreshold);
	vaEmitterSetRelativeReverbOuterThreshold(Emitter, RelativeReverbOuterThreshold);

	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX);
	vaEmitterSetHasRelativeReverb(Emitter, bIsMainListener);

	if (bIsMainListener)
	{
		if (ListenerReverbSubmix)
		{
			ListenerReverbPreset = NewObject<USubmixEffectReverbPreset>(this);
			UAudioMixerBlueprintLibrary::AddSubmixEffect(this, ListenerReverbSubmix, ListenerReverbPreset);
		}

		if (AmbientSound)
		{
			AmbientAudioComponent = UGameplayStatics::SpawnSound2D(GetWorld(), AmbientSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);

			if (AmbientAudioComponent)
			{
				AmbientAudioComponent->SetLowPassFilterEnabled(true);

				if (bAmbientThroughReverb && ListenerReverbSubmix)
				{
					AmbientAudioComponent->SetSubmixSend(ListenerReverbSubmix, 1.0f);
				}
			}
		}
	}
	else if (SourceSound)
	{
		// Build the source effect chain (LPF only on the dry path; reverb submix taps the pre-effect signal).
		SourceLPFPreset = NewObject<USourceEffectFilterPreset>(this);

		FSourceEffectFilterSettings LPFSettings;
		LPFSettings.FilterCircuit    = ESourceEffectFilterCircuit::StateVariable;
		LPFSettings.FilterType       = ESourceEffectFilterType::LowPass;
		LPFSettings.CutoffFrequency  = MAX_LOW_PASS_CUTOFF_FREQUENCY;
		LPFSettings.FilterQ          = LOW_PASS_RESONANCE;
		SourceLPFPreset->SetSettings(LPFSettings);

		SourceEffectChain = NewObject<USoundEffectSourcePresetChain>(this);
		FSourceEffectChainEntry ChainEntry;
		ChainEntry.Preset = SourceLPFPreset;
		ChainEntry.bBypass = false;
		SourceEffectChain->Chain.Add(ChainEntry);

		// Spawn is deferred to Tick()/TrySpawnSourceSound() until the main listener has raytraced
		// this emitter at least once - otherwise the sound starts clear (LPF fully open) and pops
		// to muffled a few frames later once the first real filter result arrives.
		bSourcePendingSpawn = true;
	}
	else
	{
		VALog(L"Emitter is neither main listener nor has a sound");
	}

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

void AVAudioEmitter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (AmbientAudioComponent)
	{
		AmbientAudioComponent->Stop();
		AmbientAudioComponent = nullptr;

		VALog(L"Stopped ambient sound");
	}

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;

		VALog(L"Stopped source sound");
	}

	// No need to separately zero the submix send gain: Stop() above tears down the audio
	// components (SpawnSound* defaults to bAutoDestroy), which releases their submix sends too.
	CurrentGroupedEAXIndex = -1;

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

void AVAudioEmitter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!Emitter)
	{
		VALog(L"Emitter is null");

		if (!TryInitializeEmitter())
		{
			VALog(L"Emitter init failed");

			static float TimeSinceLastNullLog = 0.0f;
			TimeSinceLastNullLog += DeltaTime;
			if (TimeSinceLastNullLog >= 2.0f)
			{
				TimeSinceLastNullLog = 0.0f;
				VALog(L"Emitter still NULL, waiting on AudioWorld");
			}

			return;
		}

		VALog(L"Emitter init succeeded");
	}

	if (bIsMainListener && !bTargetsRegistered)
	{
		bool bAllTargetsReady = true;
		for (AVAudioEmitter* Target : TargetEmitters)
		{
			if (RegisteredTargets.Contains(Target))
				continue;

			if (Target && Target->GetVAEmitter())
			{
				vaEmitterAddTarget(Emitter, Target->GetVAEmitter());
				RegisteredTargets.Add(Target);
			}
			else
			{
				// Target's BeginPlay/first Tick may not have run yet (actor init order
				// isn't guaranteed). Don't latch bTargetsRegistered until every target
				// has a VA emitter, otherwise stragglers are silently dropped forever.
				bAllTargetsReady = false;
				VALog(L"target '%s' has no VA emitter yet - will retry", Target ? *Target->GetName() : TEXT("null"));
			}
		}
		bTargetsRegistered = bAllTargetsReady;
	}

	if (bIsMainListener && bAutoFollowCamera)
	{
		APlayerController* playerController = GetWorld()->GetFirstPlayerController();

		if (playerController && playerController->PlayerCameraManager)
		{
			FVector CamPos = playerController->PlayerCameraManager->GetCameraLocation();
			vaEmitterSetPosition(Emitter, vaVectorCreate((float)CamPos.X, (float)CamPos.Y, (float)CamPos.Z));
			SetActorLocation(CamPos);
		}
	}
	else
	{
		FVector Pos = GetActorLocation();
		vaEmitterSetPosition(Emitter, vaVectorCreate((float)Pos.X, (float)Pos.Y, (float)Pos.Z));
	}


	if (!bIsMainListener && bSourcePendingSpawn)
	{
		TrySpawnSourceSound();
	}

	if (!bIsMainListener && bAffectsGroupedEAX)
	{
		UpdateSourceSubmix();

		if (GEngine)
		{
			int32 Idx = vaEmitterGetGroupedEAXIndex(Emitter);
			USoundSubmix* Submix = (AudioWorld && Idx >= 0) ? AudioWorld->GetGroupedEAXSubmix(Idx) : nullptr;

			GEngine->AddOnScreenDebugMessage((uint64)this + 100, 0.0f, FColor::Cyan,
				FString::Printf(TEXT("VA Source '%s': groupedEAXIndex=%d submix=%s sourceComp=%s"),
					*GetName(), Idx,
					Submix          ? TEXT("valid") : TEXT("null"),
					SourceAudioComponent ? TEXT("valid") : TEXT("null")));
		}
	}

	if (bIsMainListener)
	{
		ApplyListenerReverb();
		ApplyGroupedEAXReverb();
		ApplyAmbientFilter();

		for (AVAudioEmitter* Target : TargetEmitters)
		{
			// Target may be an unset TArray entry (Target == null), or a target whose emitter
			// hasn't been registered yet (see bTargetsRegistered above) - both resolve on a later Tick.
			if (!Target || !Target->GetVAEmitter())
				continue;

			if (!vaEmitterHasRaytracedTarget(Emitter, Target->GetVAEmitter()))
				continue;

			VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(Emitter, Target->GetVAEmitter());

			// Per vaudio.h, vaEmitterGetTargetFilter() is only safe to call once
			// vaEmitterHasRaytracedTarget() returns true, and should not return null after that.
			if (!ensureMsgf(lowPassFilter, TEXT("VAudioEmitter '%s': vaEmitterGetTargetFilter() returned null for raytraced target '%s'"), *GetName(), *Target->GetName()))
				continue;

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage((uint64)Target, 0.0f, FColor::Orange, FString::Printf(TEXT("VA Source '%s' LPF: gainLF=%.3f  gainHF=%.3f"), *Target->GetName(), lowPassFilter->gainLF, lowPassFilter->gainHF));
			}

			Target->ApplySourceFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);
		}
	}
}

void AVAudioEmitter::ApplyListenerReverb()
{
	// ListenerReverbPreset is only created in TryInitializeEmitter() if ListenerReverbSubmix is
	// assigned (it's an optional feature - see the property comment in VAudioEmitter.h) - listener
	// reverb is simply disabled if the user hasn't assigned a submix.
	if (!ListenerReverbPreset)
		return;

	VAEAXReverb* EAX = vaEmitterGetEAX(Emitter);

	// Raytracing has not completed at least once yet
	if (!EAX)
		return;

	FSubmixEffectReverbSettings settings;

	// EAX is already clamped, but ensure its clamped again here in case UE EAX changes one day
	settings.DecayTime           = FMath::Clamp(EAX->decayTime,           0.1f,  20.0f);
	settings.DecayHFRatio        = FMath::Clamp(EAX->decayHFRatio,        0.1f,   2.0f);
	settings.Density             = FMath::Clamp(EAX->density,             0.0f,   1.0f);
	settings.Diffusion           = FMath::Clamp(EAX->diffusion,           0.0f,   1.0f);
	settings.Gain                = FMath::Clamp(EAX->gain,                0.0f,   1.0f);
	settings.GainHF              = FMath::Clamp(EAX->gainHF,              0.0f,   1.0f);
	settings.ReflectionsGain     = FMath::Clamp(EAX->reflectionsGain,     0.0f,  3.16f);
	settings.ReflectionsDelay    = FMath::Clamp(EAX->reflectionsDelay,    0.0f,   0.3f);
	settings.LateGain            = FMath::Clamp(EAX->lateReverbGain,      0.0f,  10.0f);
	settings.LateDelay           = FMath::Clamp(EAX->lateReverbDelay,     0.0f,   0.1f);
	settings.AirAbsorptionGainHF = FMath::Clamp(EAX->airAbsorptionGainHF, 0.0f,   1.0f);
	settings.WetLevel            = FMath::Clamp(EAX->returnedPercent,    0.0f,    1.0f);
	settings.DryLevel            = 0.0f;

	ListenerReverbPreset->SetSettings(settings);
}

void AVAudioEmitter::ApplyAmbientFilter()
{
	// AmbientAudioComponent is only spawned in TryInitializeEmitter() if AmbientSound is assigned
	// (it's an optional feature - see the property comment in VAudioEmitter.h) - the ambient filter
	// is simply skipped if the user hasn't assigned an ambient sound.
	if (!AmbientAudioComponent)
		return;

	VALowPassFilter* ambientFilter = vaEmitterGetAmbientFilter(Emitter);

	// Has not raytraced yet
	if (!ambientFilter)
		return;

	AmbientAudioComponent->SetLowPassFilterFrequency(FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, ambientFilter->gainHF));
	AmbientAudioComponent->SetVolumeMultiplier(ambientFilter->gainLF);
}

void AVAudioEmitter::TrySpawnSourceSound()
{
	// This plugin currently assumes a single main listener - if that ever changes, spawn
	// readiness would need to consider raytrace state from every listener targeting this emitter.
	AVAudioEmitter* Listener = AudioWorld ? AudioWorld->GetMainListener() : nullptr;

	// Listener may not have registered yet (actor init order isn't guaranteed) - retry next Tick.
	if (!Listener || !Listener->GetVAEmitter())
		return;

	// Wait until the listener has raytraced this emitter at least once, so the LPF has a real
	// gain value to start from instead of momentarily playing at full volume/unfiltered.
	if (!vaEmitterHasRaytracedTarget(Listener->GetVAEmitter(), Emitter))
		return;

	bSourcePendingSpawn = false;

	SourceAudioComponent = UGameplayStatics::SpawnSoundAtLocation(GetWorld(), SourceSound, GetActorLocation(), FRotator::ZeroRotator, 1.0f, 1.0f, 0.0f, nullptr, nullptr, bLooping);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->SetSourceEffectChain(SourceEffectChain);

		// Prime the filter immediately so the very first played frame already reflects the
		// raytraced result rather than the MAX_LOW_PASS_CUTOFF_FREQUENCY default.
		VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(Listener->GetVAEmitter(), Emitter);

		if (ensureMsgf(lowPassFilter, TEXT("VAudioEmitter '%s': vaEmitterGetTargetFilter() returned null despite vaEmitterHasRaytracedTarget() being true"), *GetName()))
			ApplySourceFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);

		// Submix assignment is deferred to Tick once the SDK assigns a groupedEAXIndex.
		VALog(L"Low pass filter created and sound played");
	}
	else
	{
		VALog(L"Failed to play sound");
	}
}

#if WITH_EDITOR
void AVAudioEmitter::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Emitter only exists while PIE/game is running and TryInitializeEmitter() has completed -
	// editing properties on a placed actor in the editor (not PIE) hits this every time.
	if (!Emitter) return;

	vaEmitterSetReverbRayCount(Emitter, ReverbRayCount);
	vaEmitterSetReverbBounceCount(Emitter, ReverbBounceCount);
	vaEmitterSetReverbEnergyCap(Emitter, ReverbEnergyCap);
	vaEmitterSetMaxEchogramTime(Emitter, MaxEchogramTime);
	vaEmitterSetEchogramGranularity(Emitter, EchogramGranularity);

	vaEmitterSetOcclusionRayCount(Emitter, OcclusionRayCount);
	vaEmitterSetOcclusionBounceCount(Emitter, OcclusionBounceCount);
	vaEmitterSetOcclusionEnergyCap(Emitter, OcclusionEnergyCap);

	vaEmitterSetPermeationRayCount(Emitter, PermeationRayCount);
	vaEmitterSetPermeationBounceCount(Emitter, PermeationBounceCount);
	vaEmitterSetPermeationEnergyCap(Emitter, PermeationEnergyCap);

	vaEmitterSetRefreshRayCount(Emitter, RefreshRayCount);
	vaEmitterSetRefreshDistanceThreshold(Emitter, RefreshDistanceThreshold);

	vaEmitterSetAmbientOcclusionRayCount(Emitter, AmbientOcclusionRayCount);
	vaEmitterSetAmbientOcclusionBounceCount(Emitter, AmbientOcclusionBounceCount);
	vaEmitterSetAmbientOcclusionEnergyCap(Emitter, AmbientOcclusionEnergyCap);
	vaEmitterSetAmbientPermeationRayCount(Emitter, AmbientPermeationRayCount);
	vaEmitterSetAmbientPermeationBounceCount(Emitter, AmbientPermeationBounceCount);
	vaEmitterSetAmbientPermeationEnergyCap(Emitter, AmbientPermeationEnergyCap);

	vaEmitterSetVisualisationRayCount(Emitter, VisualisationRayCount);
	vaEmitterSetVisualisationBounceCount(Emitter, VisualisationBounceCount);
	vaEmitterSetVisualisationUpdateFrequency(Emitter, VisualisationUpdateFrequency);

	vaEmitterSetMaxVolume(Emitter, MaxVolume);
	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
	vaEmitterSetReservedEmitterCount(Emitter, ReservedEmitterCount);
	vaEmitterSetMinimumPermeationEnergy(Emitter, MinimumPermeationEnergy);
	vaEmitterSetRelativeReverbInnerThreshold(Emitter, RelativeReverbInnerThreshold);
	vaEmitterSetRelativeReverbOuterThreshold(Emitter, RelativeReverbOuterThreshold);

	vaEmitterSetAffectsGroupedEAX(Emitter, bAffectsGroupedEAX && !bIsMainListener);
}
#endif

static void SetAudioComponentDryOutputEnabled(UAudioComponent* Comp, bool bEnabled)
{
	// Comp is SourceAudioComponent or AmbientAudioComponent, both of which are only spawned if the
	// corresponding SourceSound/AmbientSound is assigned (see AVAudioEmitter::SetDryOutputEnabled) -
	// null here just means that optional sound isn't in use.
	if (!Comp) return;

	FAudioDevice* AudioDevice = Comp->GetAudioDevice();

	// No active audio device (e.g. audio disabled, or the component's sound already stopped) - nothing to update.
	if (!AudioDevice) return;

	uint64 AudioComponentID = Comp->GetAudioComponentID();
	AudioDevice->SendCommandToActiveSounds(AudioComponentID, [bEnabled](FActiveSound& ActiveSound)
	{
		// Kill/restore the master submix output without touching submix send routing.
		// This lets reverb submix sends stay alive while silencing the dry signal.
		ActiveSound.bHasActiveMainSubmixOutputOverride = true;
		ActiveSound.bEnableMainSubmixOutputOverride = bEnabled;
	});
}

void AVAudioEmitter::SetDryOutputEnabled(bool bEnabled)
{
	if (bEnabled == bCurrentDryEnabled) return;
	bCurrentDryEnabled = bEnabled;
	SetAudioComponentDryOutputEnabled(SourceAudioComponent, bEnabled);
	SetAudioComponentDryOutputEnabled(AmbientAudioComponent, bEnabled);
}

void AVAudioEmitter::ApplySourceFilter(float GainLF, float GainHF)
{
	// GainLF/GainHF come from VALowPassFilter, documented in vaudio.h as always in [0, 1] -
	// this is a public entry point though (also reachable from Blueprints), so guard against misuse.
	ensureMsgf(GainLF >= 0.0f && GainLF <= 1.0f, TEXT("VAudioEmitter::ApplySourceFilter: GainLF %f out of range [0,1]"), GainLF);
	ensureMsgf(GainHF >= 0.0f && GainHF <= 1.0f, TEXT("VAudioEmitter::ApplySourceFilter: GainHF %f out of range [0,1]"), GainHF);

	// SourceAudioComponent/SourceLPFPreset are only created in TryInitializeEmitter() when
	// SourceSound is assigned (this emitter isn't the main listener) - null here just means
	// this emitter has no source sound to filter.
	if (!SourceAudioComponent || !SourceLPFPreset)
		return;

	FSourceEffectFilterSettings settings;
	settings.FilterCircuit   = ESourceEffectFilterCircuit::StateVariable;
	settings.FilterType      = ESourceEffectFilterType::LowPass;
	settings.CutoffFrequency = FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, GainHF);
	settings.FilterQ         = 0.707f; // Butterworth Q - maximally flat passband, no resonant peak at the cutoff
	SourceLPFPreset->SetSettings(settings);

	SourceAudioComponent->SetVolumeMultiplier(GainLF);
}

void AVAudioEmitter::UpdateSourceSubmix()
{
	// SourceAudioComponent is only spawned when SourceSound is assigned; AudioWorld/Emitter are
	// only null before TryInitializeEmitter() has completed (see BeginPlay/Tick) - all are normal
	// transient states, not errors.
	if (!SourceAudioComponent || !AudioWorld || !Emitter)
		return;

	int32 NewIndex = vaEmitterGetGroupedEAXIndex(Emitter);

	if (NewIndex != CurrentGroupedEAXIndex)
	{
		VALog(L"groupedEAXIndex changed from %d to %d", CurrentGroupedEAXIndex, NewIndex);

		// Unlink from the old submix
		if (CurrentGroupedEAXIndex >= 0)
		{
			USoundSubmix* OldSubmix = AudioWorld->GetGroupedEAXSubmix(CurrentGroupedEAXIndex);

			if (OldSubmix)
				SourceAudioComponent->SetSubmixSend(OldSubmix, 0.0f);
		}

		CurrentGroupedEAXIndex = NewIndex;
	}

	if (CurrentGroupedEAXIndex < 0)
		return;

	USoundSubmix* Submix = AudioWorld->GetGroupedEAXSubmix(CurrentGroupedEAXIndex);

	// AVAudioWorld always sets the SDK's maximumGroupedEAXCount to at least 2 (see
	// AVAudioWorld::BeginPlay), even if GroupedEAXSubmixes has fewer than 2 entries - so the SDK
	// can hand back an index for which there's no configured submix. Warn so the user knows to add
	// more entries to GroupedEAXSubmixes on the VAudioWorld actor.
	if (!Submix)
	{
		VALog(L"no submix configured at GroupedEAX index %d - add more entries to GroupedEAXSubmixes on the VAudioWorld actor (needs at least 2).", CurrentGroupedEAXIndex);
		return;
	}

	// Default send level is overridden below if the listener has relative reverb data
	float SendLevel = 1.0f;
	bool bHasRelative = false;

	VAWorld* vaWorld = AudioWorld->GetVAWorld();
	AVAudioEmitter* Listener = AudioWorld->GetMainListener();

	if (!vaWorld)
	{
		VALog(L"Unable to access grouped EAX data as vaWorld is null");
	}
	// Wait for raytracing to run at least once
	else if (vaWorldGetReverbCalculated(vaWorld))
	{
		const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(vaWorld);

		if (!ensureMsgf(GroupedEAX, TEXT("VAudioEmitter '%s': vaWorldGetGroupedEAX() returned null after reverb was calculated - vaWorldSetMaximumGroupedEAXCount() is always called with >= 2 (see AVAudioWorld::BeginPlay), so this shouldn't happen"), *GetName()))
		{
			VALog(L"Reverb is calculated but vaWorldGetGroupedEAX() returned null");
		}
		else if (GroupedEAX[CurrentGroupedEAXIndex])
		{
			const VAEAXReverb* EAX = GroupedEAX[CurrentGroupedEAXIndex];

			if (!Listener)
			{
				VALog(L"Unable to access relative EAX gain as Listener is null");
			}
			else
			{
				VAEmitter* ListenerVA = Listener->GetVAEmitter();

				// UE-LIMITATION - only relative gain is supported. Can't do directional reverb
				float* Gain = vaEAXReverbGetRelativeGain(EAX, ListenerVA);

				if (Gain)
				{
					// Assert gain is >= 0 and <= 1
					SendLevel = *Gain;
					bHasRelative = true;
				}
			}
		}
		else
		{
			VALog(L"Reverb is calculated but GroupedEAX[CurrentGroupedEAXIndex] is null");
		}
	}
	else
	{
		VALog(L"Unable to access grouped EAX data as vaWorldGetReverbCalculated() is false");
	}

	SourceAudioComponent->SetSubmixSend(Submix, SendLevel);

	if (GEngine)
	{
		FString RelStr = FString::Printf(TEXT("Submix gain is %.3f"), SendLevel);
		GEngine->AddOnScreenDebugMessage((uint64)this + 200, 0.0f, FColor::Magenta, FString::Printf(TEXT("VAudioEmitter.cpp: UpdateSourceSubmix(): '%s': %s"), *GetName(), *RelStr));
	}
}

void AVAudioEmitter::ApplyGroupedEAXReverb()
{
	if (!AudioWorld)
	{
		VALog(L"AudioWorld is null");
		return;
	}

	VAWorld* vaWorld = AudioWorld->GetVAWorld();
	if (!vaWorld)
	{
		VALog(L"vaWorld is null");
		return;
	}

	const VAEAXReverb** GroupedEAX = vaWorldGetGroupedEAX(vaWorld);
	int32 Count = vaWorldGetGroupedEAXCount(vaWorld);

	// Wait for raytracing to run at least once
	if (!vaWorldGetReverbCalculated(vaWorld))
		return;

	// Same guarantee as UpdateSourceSubmix(): maximumGroupedEAXCount is always >= 2
	// (see AVAudioWorld::BeginPlay), so this shouldn't be null once reverb has been calculated.
	if (!ensureMsgf(GroupedEAX, TEXT("VAudioEmitter '%s': vaWorldGetGroupedEAX() returned null after reverb was calculated"), *GetName()))
		return;

	for (int32 i = 0; i < Count; ++i)
	{
		USubmixEffectReverbPreset* Preset = AudioWorld->GetGroupedEAXPreset(i);

		// GetGroupedEAXPreset(i) is only valid for i < GroupedEAXSubmixes.Num() on the VAudioWorld
		// actor, but Count (the SDK's grouped EAX count) is always >= 2 - if fewer than 2 submixes
		// are configured, indices in between have no preset. Same underlying cause as the
		// "no submix configured" warning in UpdateSourceSubmix().
		if (!Preset)
		{
			VALog(L"no reverb preset configured at GroupedEAX index %d - add more entries to GroupedEAXSubmixes on the VAudioWorld actor (needs at least 2).", i);
			continue;
		}

		const VAEAXReverb* EAX = GroupedEAX[i];

		// EAX itself comes straight from the SDK's GroupedEAX array (sized to Count), so it
		// should always be populated for i < Count once reverb has been calculated.
		if (!ensureMsgf(EAX, TEXT("VAudioEmitter '%s': GroupedEAX[%d] is null after reverb was calculated"), *GetName(), i))
			continue;

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(3001 + i, 0.0f, FColor::White,
				FString::Printf(TEXT("VA GroupedEAX[%d]: decayTime=%.3f wetLevel=%.3f gain=%.3f"),
					i, EAX->decayTime, EAX->returnedPercent, EAX->gain));
		}

		// EAX is already clamped, but ensure its clamped again here in case UE EAX changes one day
		FSubmixEffectReverbSettings settings;
		settings.DecayTime           = FMath::Clamp(EAX->decayTime,           0.1f,  20.0f);
		settings.DecayHFRatio        = FMath::Clamp(EAX->decayHFRatio,        0.1f,   2.0f);
		settings.Density             = FMath::Clamp(EAX->density,             0.0f,   1.0f);
		settings.Diffusion           = FMath::Clamp(EAX->diffusion,           0.0f,   1.0f);
		settings.Gain                = FMath::Clamp(EAX->gain,                0.0f,   1.0f);
		settings.GainHF              = FMath::Clamp(EAX->gainHF,              0.0f,   1.0f);
		settings.ReflectionsGain     = FMath::Clamp(EAX->reflectionsGain,     0.0f,   3.16f);
		settings.ReflectionsDelay    = FMath::Clamp(EAX->reflectionsDelay,    0.0f,   0.3f);
		settings.LateGain            = FMath::Clamp(EAX->lateReverbGain,      0.0f,  10.0f);
		settings.LateDelay           = FMath::Clamp(EAX->lateReverbDelay,     0.0f,   0.1f);
		settings.AirAbsorptionGainHF = FMath::Clamp(EAX->airAbsorptionGainHF, 0.0f,   1.0f);
		settings.WetLevel            = FMath::Clamp(EAX->returnedPercent,     0.0f,   1.0f);
		settings.DryLevel            = 0.0f;
		Preset->SetSettings(settings);
	}
}

