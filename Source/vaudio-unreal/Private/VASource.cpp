#include "VASource.h"
#include "VAWorld.h"
#include "VAListener.h"
#include "ActiveSound.h"
#include "AudioDevice.h"

extern "C" {
#include "vaudio.h"
}

#include "VALog.h"
#include "VAConstants.h"

const float LOW_PASS_RESONANCE = 0.707f; // Butterworth Q constant - maximally flat passband, no resonant peak at the cutoff

AVASource::AVASource()
{
}

bool AVASource::ValidateConfig()
{
	Super::ValidateConfig();

	if (!SourceSound)
	{
		VA_WARN_NAMED(TEXT("Will not play as it has no SourceSound assigned"));
		return false;
	}

	return true;
}

void AVASource::InitializeTypeSpecific()
{
	Super::InitializeTypeSpecific();

	// Already validated by ValidateConfig() above
	check(SourceSound);

	if (!SourceSound->IsPlayWhenSilent())
	{
		VA_WARN_NAMED(TEXT("SourceSound '%s' must have Virtualization Mode = 'Play When Silent', else it may stop playing when fully muffled"), *SourceSound->GetName());
	}

	if (bAffectsGroupedEAX && (ReverbRayCount == 0 || ReverbBounceCount == 0))
	{
		VA_WARN_NAMED(TEXT("Has affectsGroupedEAX=true, but does not cast reverb rays"));
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

	bSourcePendingSpawn = true;
}

void AVASource::DeinitializeTypeSpecific()
{
	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;
	}

	ReverbSubmix = nullptr;
	ReverbSendLevel = 0.0f;

	// No need to separately zero the submix send gain: Stop() above tears down the audio
	// component (SpawnSound* defaults to bAutoDestroy), which releases its submix send too.
	Super::DeinitializeTypeSpecific();
}

void AVASource::TickTypeSpecific(float DeltaTime)
{
	Super::TickTypeSpecific(DeltaTime);

	if (bSourcePendingSpawn)
		TrySpawnSourceSound();

	// Null until the main listener has raytraced this source
	if (VALowPassFilter* lowPassFilter = GetMufflingResult())
		ApplySourceFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);

	UpdateSourceSubmix();

	if (SourceAudioComponent)
	{
		FVector pos = GetActorLocation();
		SourceAudioComponent->SetWorldLocationAndRotation(pos, FRotator::ZeroRotator);
		vaEmitterSetPositionUnreal(Emitter, pos);
	}
}

void AVASource::TrySpawnSourceSound()
{
	// Wait until the main listener has raytraced this source, which may not have begun play yet
	VALowPassFilter* lowPassFilter = GetMufflingResult();

	if (!lowPassFilter)
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
			VA_WARN_NAMED(TEXT("Has no Sound Attenuation - it will not fall off with distance"));
		}

		// Apply filter immediately
		ApplySourceFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);

		// Apply reverb
		UpdateSourceSubmix();

		SourceAudioComponent->Play();
	}
	else
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
	}
}

void AVASource::ApplySourceFilter(float GainLF, float GainHF)
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

void AVASource::UpdateSourceSubmix()
{
	USoundSubmix* Submix = nullptr;
	float SendLevel = 0.0f;

	if (!ResolveReverbSend(Submix, SendLevel))
		return;

	// Moved to another submix (a different grouped EAX slot, between grouped and listener reverb, or a new current listener), so silence the old send
	if (ReverbSubmix && ReverbSubmix != Submix)
		SendToSubmix(ReverbSubmix, 0.0f);

	ReverbSubmix = Submix;
	ReverbSendLevel = SendLevel;

	if (ReverbSubmix)
		SendToSubmix(ReverbSubmix, ReverbSendLevel);
}

// Mirrors Godot's VAWorld::get_reverb_effect. Returns false to keep the current send, e.g. while the listener is switching
bool AVASource::ResolveReverbSend(USoundSubmix*& OutSubmix, float& OutSendLevel)
{
	VAWorld* vaWorld = AudioWorld->GetVAWorld();
	AVAListener* Listener = AudioWorld->GetMainListener();

	// The listener ended play
	if (!Listener || !Listener->GetVAEmitter())
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

		// A grouped EAX index is only assigned once this source's own reverb rays have completed, so its grouped EAX is available
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

void AVASource::SendToSubmix(USoundSubmix* Submix, float SendLevel)
{
	// Sound not played yet - still waiting for raytracing
	if (!SourceAudioComponent)
		return;

	FSoundSubmixSendInfo SubmixSendInfo;
	SubmixSendInfo.SoundSubmix = Submix;
	SubmixSendInfo.SendLevel = SendLevel;
	SubmixSendInfo.SendLevelControlMethod = ESendLevelControlMethod::Manual;
	SubmixSendInfo.SendStage = ESubmixSendStage::PreDistanceAttenuation;

	if (FAudioDevice* AudioDevice = SourceAudioComponent->GetAudioDevice())
	{
		uint64 AudioComponentID = SourceAudioComponent->GetAudioComponentID();
		AudioDevice->SendCommandToActiveSounds(AudioComponentID, [SubmixSendInfo](FActiveSound& ActiveSound)
		{
			ActiveSound.SetSubmixSend(SubmixSendInfo);
		});
	}
}

// Toggle whether we only hear reverb
void AVASource::SetDryOutputEnabled(bool bEnabled)
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
