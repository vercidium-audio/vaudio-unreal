// Human-reviewed

#include "VASource.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Sound/SoundAttenuation.h"

#include "VALog.h"
#include "VAWorld.h"

bool AVASource::ValidateConfig()
{
	Super::ValidateConfig();

	if (!SourceSound)
	{
		VA_WARN_NAMED(TEXT("Will not play as it has no SourceSound assigned%s"), VANeverDestroyedSuffix(bAutoDestroy));
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

	// Keeps a Play() made before this source joined its VAWorld
	bPlayPending = bPlayPending || bAutoPlay;
}

void AVASource::DeinitializeTypeSpecific()
{
	// A loop stopped by its VAWorld ending hasn't finished, so this source waits for the next VAWorld rather than being destroyed
	if (Playback.HasSound() && SourceSound && SourceSound->IsLooping())
		bPlayAttempted = false;

	Super::DeinitializeTypeSpecific();
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

	// Finished sounds were dropped in TickTypeSpecific
	if (!IsFinalRemovalStarted())
	{
		if (!bAutoDestroy || !bPlayAttempted || Playback.HasSound())
			return;

		BeginFinalRemoval();
	}

	// OnRemoved clears the handle during vaWorldUpdate, where this actor can't be destroyed, so it's checked here
	if (!Emitter)
		Destroy();
}

void AVASource::TickTypeSpecific(float DeltaTime)
{
	Super::TickTypeSpecific(DeltaTime);

	// Like the actor-free one-shots, a spawned source doesn't wait for a listener. A user's source does
	if (bSpawnedAsOneShot && bPlayPending && !GetAudioWorld()->GetMainListener())
	{
		bPlayPending = false;
		bPlayAttempted = true;
	}

	if (VAApplyBoundsRules(Playback, IsSilencedByBounds(), SourceSound && SourceSound->IsLooping(), bPlayPending, bPlayAttempted))
		return;

	if (bPlayPending && IsReadyToPlay())
		Play(PendingStartTime);
}

void AVASource::DestroyWithComponent(USceneComponent* component)
{
	bDestroyWithComponent = true;
	DestroyWithComponentTarget = component;
}

void AVASource::Stop()
{
	VACancelPendingPlay(bPlayPending, bPlayAttempted);
	PendingStartTime = 0.0f;
	Super::Stop();
}

bool AVASource::Play(float StartTime)
{
	// A source that bAutoDestroy is removing can't play again
	if (!SourceSound || !HasActorBegunPlay() || IsFinalRemovalStarted())
		return false;

	if (VAApplyBoundsRulesOnPlay(this, Playback, IsSilencedByBounds(), SourceSound->IsLooping(), bPlayPending, bPlayAttempted))
		return false;

	// Waits for the muffling and reverb results, so the sound never starts unmuffled or without reverb. TickTypeSpecific plays it once ready, like VASourceRelative
	if (!IsReadyToPlay())
	{
		bPlayPending = true;
		PendingStartTime = StartTime;
		return true;
	}

	bPlayPending = false;
	PendingStartTime = 0.0f;
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

	Playback.Play(component, StartTime);
	return true;
}
