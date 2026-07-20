#include "VAudioListener.h"
#include "VAudioWorld.h"
#include "VAudioSource.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "AudioMixerBlueprintLibrary.h"

extern "C" {
#include "vaudio.h"
}

#include "VaRawLog.h"

const float MIN_LOW_PASS_CUTOFF_FREQUENCY = 200.0f;
const float MAX_LOW_PASS_CUTOFF_FREQUENCY = 20000.0f;

AVAudioListener::AVAudioListener()
{
}

void AVAudioListener::InitializeTypeSpecific()
{
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

	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
	vaEmitterSetMinimumPermeationEnergy(Emitter, MinimumPermeationEnergy);
	vaEmitterSetRelativeReverbInnerThreshold(Emitter, RelativeReverbInnerThreshold);
	vaEmitterSetRelativeReverbOuterThreshold(Emitter, RelativeReverbOuterThreshold);

	// This actor's existence as an AVAudioListener IS the "is main listener" flag - always true.
	vaEmitterSetHasRelativeReverb(Emitter, true);

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

void AVAudioListener::DeinitializeTypeSpecific()
{
	if (AmbientAudioComponent)
	{
		AmbientAudioComponent->Stop();
		AmbientAudioComponent = nullptr;

		VALog(L"Stopped ambient sound");
	}
}

void AVAudioListener::TickTypeSpecific(float DeltaTime)
{
	if (!bTargetsRegistered)
	{
		bool bAllTargetsReady = true;
		for (AVAudioEmitterBase* Target : TargetEmitters)
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
				VALog(L"target '%s' has no VA emitter yet - will retry", Target ? *Target->GetActorNameOrLabel() : TEXT("null"));
			}
		}
		bTargetsRegistered = bAllTargetsReady;
	}

	// Overrides the base class's plain position sync (Tick() already ran it this frame before
	// calling TickTypeSpecific()) when following the camera - this actor's position is also
	// snapped to the camera so any other code reading GetActorLocation() sees the same place.
	if (bAutoFollowCamera)
	{
		APlayerController* playerController = GetWorld()->GetFirstPlayerController();

		if (playerController && playerController->PlayerCameraManager)
		{
			FVector CamPos = playerController->PlayerCameraManager->GetCameraLocation();
			vaEmitterSetPosition(Emitter, vaVectorCreate((float)CamPos.X, (float)CamPos.Y, (float)CamPos.Z));
			SetActorLocation(CamPos);
		}
	}

	ApplyListenerReverb();
	ApplyGroupedEAXReverb();
	ApplyAmbientFilter();

	for (AVAudioEmitterBase* Target : TargetEmitters)
	{
		// Target may be an unset TArray entry (Target == null), a target whose emitter hasn't
		// been registered yet (see bTargetsRegistered above), one not yet raytraced, or one
		// with an invalid filter - all resolve on a later Tick. See AVAudioWorld::Tick() for
		// the on-screen diagnostic covering these same cases.
		if (!Target || !Target->GetVAEmitter() || !vaEmitterHasRaytracedTarget(Emitter, Target->GetVAEmitter()))
			continue;

		VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(Emitter, Target->GetVAEmitter());

		if (!lowPassFilter)
			continue;

		// ApplySourceFilter() only exists on AVAudioSource (it's the only TargetEmitters member
		// that plays a sound needing an LPF) - AVAudioContinuous targets have no filter to apply to.
		if (AVAudioSource* ConcreteTarget = Cast<AVAudioSource>(Target))
			ConcreteTarget->ApplySourceFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);
	}
}

void AVAudioListener::ApplyListenerReverb()
{
	// ListenerReverbPreset is only created in InitializeTypeSpecific() if ListenerReverbSubmix is
	// assigned (it's an optional feature - see the property comment in VAudioListener.h) - listener
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

void AVAudioListener::ApplyAmbientFilter()
{
	// AmbientAudioComponent is only spawned in InitializeTypeSpecific() if AmbientSound is assigned
	// (it's an optional feature - see the property comment in VAudioListener.h) - the ambient filter
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

void AVAudioListener::ApplyGroupedEAXReverb()
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
	if (vaWorldGetInitialising(vaWorld))
		return;

	// Same guarantee as AVAudioSource::UpdateSourceSubmix(): maximumGroupedEAXCount is always >= 2
	// (see AVAudioWorld::BeginPlay), so this shouldn't be null once reverb has been calculated.
	if (!ensureMsgf(GroupedEAX, TEXT("VAudioListener '%s': vaWorldGetGroupedEAX() returned null after reverb was calculated"), *GetActorNameOrLabel()))
		return;

	for (int32 i = 0; i < Count; ++i)
	{
		USubmixEffectReverbPreset* Preset = AudioWorld->GetGroupedEAXPreset(i);

		// GetGroupedEAXPreset(i) is only valid for i < GroupedEAXSubmixes.Num() on the VAudioWorld
		// actor, but Count (the SDK's grouped EAX count) is always >= 2 - if fewer than 2 submixes
		// are configured, indices in between have no preset.
		if (!Preset)
		{
			VALog(L"no reverb preset configured at GroupedEAX index %d - add more entries to GroupedEAXSubmixes on the VAudioWorld actor (needs at least 2).", i);
			continue;
		}

		const VAEAXReverb* EAX = GroupedEAX[i];

		// EAX itself comes straight from the SDK's GroupedEAX array (sized to Count), so it
		// should always be populated for i < Count once reverb has been calculated.
		if (!ensureMsgf(EAX, TEXT("VAudioListener '%s': GroupedEAX[%d] is null after reverb was calculated"), *GetActorNameOrLabel(), i))
			continue;

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

#if WITH_EDITOR
void AVAudioListener::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
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

	vaEmitterSetType(Emitter, EmitterType);
	vaEmitterSetClampPosition(Emitter, bClampPosition);
	vaEmitterSetScatteringSeed(Emitter, ScatteringSeed);
	vaEmitterSetMinimumPermeationEnergy(Emitter, MinimumPermeationEnergy);
	vaEmitterSetRelativeReverbInnerThreshold(Emitter, RelativeReverbInnerThreshold);
	vaEmitterSetRelativeReverbOuterThreshold(Emitter, RelativeReverbOuterThreshold);
}
#endif
