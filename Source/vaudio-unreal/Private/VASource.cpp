#include "VASource.h"
#include "VAWorld.h"
#include "VAListener.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"

extern "C" {
#include "vaudio.h"
}

#include "VALog.h"
#include "VAConstants.h"

AVASource::AVASource()
{
	// Godot's VASource default
	bAffectsGroupedEAX = true;
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

	if (!SourceSound->AttenuationSettings)
	{
		VA_WARN_NAMED(TEXT("SourceSound '%s' has no Sound Attenuation - it will not fall off with distance"), *SourceSound->GetName());
	}

	bAutoPlayPending = bAutoPlay;
}

void AVASource::DeinitializeTypeSpecific()
{
	Playback.Stop();

	Super::DeinitializeTypeSpecific();
}

void AVASource::TickTypeSpecific(float DeltaTime)
{
	Super::TickTypeSpecific(DeltaTime);

	Playback.RemoveFinished();
	UpdatePlayback();

	if (bAutoPlayPending && IsReadyToPlay())
		Play();

	Playback.SetLocation(GetActorLocation());
}

void AVASource::UpdatePlayback()
{
	Playback.SetVolumeMultiplier(VolumeMultiplier);
	Playback.SetPitchMultiplier(PitchMultiplier);

	// Null until the main listener has raytraced this source, and after a bRaytraceOnce source leaves the world. The last result is kept
	if (VALowPassFilter* lowPassFilter = GetMufflingResult())
		Playback.SetFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);

	USoundSubmix* submix = nullptr;
	float sendLevel = 0.0f;

	// Set after the filter, as the send level is compensated by gainLF
	if (ResolveReverbSend(submix, sendLevel))
		Playback.SetReverbSend(submix, sendLevel);
}

bool AVASource::Play()
{
	// Wait for the muffling and reverb results, so the sound never starts unmuffled or without reverb
	if (!SourceSound || !IsReadyToPlay())
		return false;

	bAutoPlayPending = false;

	// Play() may be called before this tick's update
	UpdatePlayback();

	FAudioDevice::FCreateComponentParams Params(GetWorld(), this);
	Params.SetLocation(GetActorLocation());

	UAudioComponent* component = FAudioDevice::CreateComponent(SourceSound, Params);

	if (!component)
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
		return false;
	}

	component->SetWorldLocationAndRotation(GetActorLocation(), FRotator::ZeroRotator);
	component->bAllowSpatialization = true;
	component->bStopWhenOwnerDestroyed = false;

	Playback.Play(component);
	return true;
}

void AVASource::Stop()
{
	Playback.Stop();
}
