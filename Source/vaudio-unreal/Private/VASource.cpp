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
#include "VASubmixSend.h"

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

	Filter.Initialize(this);

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
		Filter.Apply(SourceAudioComponent, lowPassFilter->gainLF, lowPassFilter->gainHF);

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
		SourceAudioComponent->SetPitchMultiplier(1.0f);
		SourceAudioComponent->bAllowSpatialization = true;
		SourceAudioComponent->bAutoDestroy = true;
		SourceAudioComponent->bStopWhenOwnerDestroyed = false;

		if (!SourceAudioComponent->AttenuationSettings)
		{
			VA_WARN_NAMED(TEXT("Has no Sound Attenuation - it will not fall off with distance"));
		}

		// Apply the filter immediately
		Filter.Apply(nullptr, lowPassFilter->gainLF, lowPassFilter->gainHF);
		Filter.Attach(SourceAudioComponent);

		// Apply reverb
		UpdateSourceSubmix();

		SourceAudioComponent->Play();
	}
	else
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
	}
}

void AVASource::UpdateSourceSubmix()
{
	USoundSubmix* Submix = nullptr;
	float SendLevel = 0.0f;

	if (!ResolveReverbSend(Submix, SendLevel))
		return;

	// Moved to another submix (a different grouped EAX slot, between grouped and listener reverb, or a new current listener), so silence the old send
	if (ReverbSubmix && ReverbSubmix != Submix)
		VASetReverbSend(SourceAudioComponent, ReverbSubmix, 0.0f);

	ReverbSubmix = Submix;
	ReverbSendLevel = SendLevel;

	if (ReverbSubmix)
		VASetReverbSend(SourceAudioComponent, ReverbSubmix, ReverbSendLevel);
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
