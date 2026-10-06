#include "VASource.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"

#include "VALog.h"

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

void AVASource::TickTypeSpecific(float DeltaTime)
{
	Super::TickTypeSpecific(DeltaTime);

	if (bAutoPlayPending && IsReadyToPlay())
		Play();
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
