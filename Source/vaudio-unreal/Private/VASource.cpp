#include "VASource.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Sound/SoundAttenuation.h"

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

	if (!SourceSound->AttenuationSettings && !AttenuationSettings)
	{
		VA_WARN_NAMED(TEXT("SourceSound '%s' has no Sound Attenuation - it will not fall off with distance"), *SourceSound->GetName());
	}

	bAutoPlayPending = bAutoPlay;
}

void AVASource::Tick(float DeltaTime)
{
	// Checked before the emitter's own tick, which returns early until this source has joined a VAWorld
	if (bDestroyWithComponent)
	{
		USceneComponent* component = DestroyWithComponentTarget.Get();
		AActor* owner = component ? component->GetOwner() : nullptr;

		if (!IsValid(component) || component->IsBeingDestroyed() || (owner && owner->IsActorBeingDestroyed()))
		{
			Destroy();
			return;
		}
	}

	Super::Tick(DeltaTime);

	// After the emitter's tick, which may still release a bRaytraceOnce handle. Finished sounds were dropped in TickTypeSpecific
	if (bDestroyWhenFinished && bPlayAttempted && !IsPlaying())
		Destroy();
}

void AVASource::TickTypeSpecific(float DeltaTime)
{
	Super::TickTypeSpecific(DeltaTime);

	if (bAutoPlayPending && IsReadyToPlay())
		Play();
}

void AVASource::DestroyWithComponent(USceneComponent* component)
{
	bDestroyWithComponent = true;
	DestroyWithComponentTarget = component;
}

bool AVASource::Play()
{
	// Wait for the muffling and reverb results, so the sound never starts unmuffled or without reverb
	if (!SourceSound || !IsReadyToPlay())
		return false;

	bAutoPlayPending = false;

	if (bDestroyWhenFinished && !bPlayAttempted)
		SetLifeSpan(0.0f);

	bPlayAttempted = true;

	// Play() may be called before this tick's update
	UpdatePlayback();

	// Raytracing doesn't need the sound, so having no audio device (e.g. -nosound) isn't worth a warning
	if (!GetWorld()->GetAudioDeviceRaw())
	{
		VA_LOG_NAMED(TEXT("No audio device, so the source is raytraced but not heard"));
		return false;
	}

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

	if (AttenuationSettings)
		component->AttenuationSettings = AttenuationSettings;

	Playback.Play(component);
	return true;
}
