#include "VAudioSource.h"
#include "VAudioWorld.h"
#include "VAudioListener.h"
#include "ActiveSound.h"
#include "AudioDevice.h"

extern "C" {
#include "vaudio.h"
}

#include "VARawLog.h"
#include "VADebugMessageKeys.h"
#include "VAConstants.h"

const float LOW_PASS_RESONANCE = 0.707f; // Butterworth Q constant - maximally flat passband, no resonant peak at the cutoff

AVAudioSource::AVAudioSource()
{
}

bool AVAudioSource::InitializeTypeSpecific()
{
	Super::InitializeTypeSpecific();

	if (!SourceSound)
	{
		DisplayWarning(TEXT("[VA] Source '%s' will not play as it has no SourceSound assigned"), *GetActorNameOrLabel());
		return false;
	}

	AVAudioListener* listener = AudioWorld->GetMainListener();

	if (!listener)
	{
		DisplayWarning(TEXT("[VA] Source '%s' will not play as the AudioWorld does not have a listener"), *GetActorNameOrLabel());
		return false;
	}

	if (!SourceSound->IsPlayWhenSilent())
	{
		DisplayWarning(TEXT("[VA] Source '%s': SourceSound '%s' must have Virtualization Mode = 'Play When Silent', else it may stop playing when fully muffled"), *GetActorNameOrLabel(), *SourceSound->GetName());
	}

	if (bAffectsGroupedEAX && (ReverbRayCount == 0 || ReverbBounceCount == 0))
	{
		DisplayWarning(TEXT("[VA] Source '%s' has affectsGroupedEAX=true, but does not cast reverb rays"), *GetActorNameOrLabel());
	}

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

	return true;
}

void AVAudioSource::DeinitializeTypeSpecific()
{
	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;
	}

	// No need to separately zero the submix send gain: Stop() above tears down the audio
	// component (SpawnSound* defaults to bAutoDestroy), which releases its submix send too.
	Super::DeinitializeTypeSpecific();
}

void AVAudioSource::TickTypeSpecific(float DeltaTime)
{
	check(AudioWorld);
	check(Emitter);

	Super::TickTypeSpecific(DeltaTime);

	if (bSourcePendingSpawn)
		TrySpawnSourceSound();

	if (bAffectsGroupedEAX)
		UpdateSourceSubmix();
}

void AVAudioSource::TrySpawnSourceSound()
{
	AVAudioListener* Listener = AudioWorld->GetMainListener();
	VAEmitter* vaListener = Listener->GetVAEmitter();

	// Wait until raytracing completes
	if (!vaEmitterHasRaytracedTarget(vaListener, Emitter))
		return;

	bSourcePendingSpawn = false;

	// Create the component and attach the filter ahead of time.
	//  Else if we attach the filter after creating the sound, the filter is never applied
	FAudioDevice::FCreateComponentParams Params(GetWorld(), this);
	Params.SetLocation(GetActorLocation());

	SourceAudioComponent = FAudioDevice::CreateComponent(SourceSound, Params);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->SetWorldLocationAndRotation(GetActorLocation(), FRotator::ZeroRotator);
		SourceAudioComponent->SetVolumeMultiplier(1.0f);
		SourceAudioComponent->SetPitchMultiplier(1.0f);
		SourceAudioComponent->bAllowSpatialization = true;
		SourceAudioComponent->bAutoDestroy = true;
		SourceAudioComponent->bStopWhenOwnerDestroyed = false;
		SourceAudioComponent->SetSourceEffectChain(SourceEffectChain);

		if (!SourceAudioComponent->AttenuationSettings)
		{
			uint64 messageID = VAEmitterMessageBase + GetEmitterIndex() * VAEmitterMessageStride + VAEmitterAttenuationStatus;
			GEngine->AddOnScreenDebugMessage(messageID, 0.0f, FColor::Orange, FString::Printf(TEXT("[VA] Source '%s' has no Sound Attenuation - it will not fall off with distance"), *GetActorNameOrLabel()));
		}

		// Apply filter immediately
		VALowPassFilter* lowPassFilter = vaEmitterGetTargetFilter(vaListener, Emitter);
		ApplySourceFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);

		// Apply reverb
		UpdateSourceSubmix();

		SourceAudioComponent->Play();
	}
	else
	{
		DisplayWarning(TEXT("[VA] Source '%s' play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"), *GetActorNameOrLabel());
	}
}

void AVAudioSource::ApplySourceFilter(float GainLF, float GainHF)
{
	// Sound not played yet - still waiting for raytracing
	if (!SourceAudioComponent)
		return;

	FSourceEffectFilterSettings settings;
	settings.FilterCircuit   = ESourceEffectFilterCircuit::StateVariable;
	settings.FilterType      = ESourceEffectFilterType::LowPass;
	settings.CutoffFrequency = FMath::Lerp(MIN_LOW_PASS_CUTOFF_FREQUENCY, MAX_LOW_PASS_CUTOFF_FREQUENCY, GainHF);
	settings.FilterQ		 = LOW_PASS_RESONANCE;
	SourceLPFPreset->SetSettings(settings);

	SourceAudioComponent->SetVolumeMultiplier(GainLF);
}

void AVAudioSource::UpdateSourceSubmix()
{
	// Sound not played yet - still waiting for raytracing
	if (!SourceAudioComponent)
		return;

	int32 GroupedEAXIndex = GetGroupedEAXIndex();

	// Raytracing has not completed at least once
	if (GroupedEAXIndex < 0)
		return;

	USoundSubmix* Submix = AudioWorld->GetGroupedEAXSubmix(GroupedEAXIndex);

	// The user assigned a null submix to World.groupedEAX[]. A warning is already displayed in VAudioWorld.cpp
	if (!Submix)
		return;

	VAWorld* vaWorld = AudioWorld->GetVAWorld();
	AVAudioListener* Listener = AudioWorld->GetMainListener();

	// At this stage we have been raytraced by the listener, so groupedEAX should be available
	const VAEAXReverb** groupedEAX = vaWorldGetGroupedEAX(vaWorld);

	int groupedEAXCount = vaWorldGetGroupedEAXCount(vaWorld);

	if (GroupedEAXIndex >= groupedEAXCount)
	{
		DisplayWarning(TEXT("[VA] Source '%s' has an invalid grouped EAX index: %d. There are only %d grouped EAX submixes available"), *GetActorNameOrLabel(), GroupedEAXIndex, groupedEAXCount);
		return;
	}

	const VAEAXReverb* EAX = groupedEAX[GroupedEAXIndex];

	VAEmitter* ListenerVA = Listener->GetVAEmitter();

	// Only relative gain is supported. Can't do directional reverb in Unreal :(
	float SendLevel = *vaEAXReverbGetRelativeGain(EAX, ListenerVA);

	// TODO - can we control the overall send level of the submix to the headphones/speaker, rather than filtering how much each sound sends to the submix?
	SourceAudioComponent->SetSubmixSend(Submix, SendLevel);
}

// Toggle whether we only hear reverb
void AVAudioSource::SetDryOutputEnabled(bool bEnabled)
{
	if (bEnabled == bCurrentDryEnabled)
		return;

	// Sound not played yet - still waiting for raytracing
	if (!SourceAudioComponent)
		return;

	bCurrentDryEnabled = bEnabled;

	FAudioDevice* AudioDevice = SourceAudioComponent->GetAudioDevice();

	// No active audio device (e.g. audio disabled, or the component's sound already stopped) - nothing to update
	if (!AudioDevice)
		return;

	uint64 AudioComponentID = SourceAudioComponent->GetAudioComponentID();
	AudioDevice->SendCommandToActiveSounds(AudioComponentID, [bEnabled](FActiveSound& ActiveSound)
	{
		// Kill/restore the master submix output without touching submix send routing.
		// This lets reverb submix sends stay alive while silencing the dry signal.
		ActiveSound.bHasActiveMainSubmixOutputOverride = true;
		ActiveSound.bEnableMainSubmixOutputOverride = bEnabled;
	});
}
