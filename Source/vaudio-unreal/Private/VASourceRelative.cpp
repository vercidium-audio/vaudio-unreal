#include "VASourceRelative.h"
#include "VAListener.h"
#include "VAWorld.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

#include "VALog.h"

AVASourceRelative::AVASourceRelative()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AVASourceRelative::BeginPlay()
{
	Super::BeginPlay();

	if (SourceSounds.Num() == 0)
	{
		VA_WARN_NAMED(TEXT("Has no SourceSounds and will not play sound"));
		SetActorTickEnabled(false);
		return;
	}

	for (int32 i = 0; i < SourceSounds.Num(); i++)
	{
		if (!SourceSounds[i])
		{
			VA_WARN_NAMED(TEXT("Will not play as it has a null sound assigned to index %d"), i);
			SetActorTickEnabled(false);
			return;
		}
	}

	bValidConfig = true;
	bAutoPlayPending = bAutoPlay;
}

void AVASourceRelative::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	Playback.Stop();
}

void AVASourceRelative::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Playback.RemoveFinished();
	UpdatePlayback();

	if (bAutoPlayPending)
		Play();
}

bool AVASourceRelative::Play()
{
	if (!bValidConfig)
		return false;

	bAutoPlayPending = false;

	// Play() may be called before this tick's update
	UpdatePlayback();

	USoundBase* chosenSound = SourceSounds[FMath::RandHelper(SourceSounds.Num())];

	// CreateSound2D() builds the component without starting playback
	UAudioComponent* component = UGameplayStatics::CreateSound2D(GetWorld(), chosenSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);

	if (!component)
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
		return false;
	}

	Playback.Play(component);
	return true;
}

void AVASourceRelative::Stop()
{
	Playback.Stop();
}

void AVASourceRelative::UpdatePlayback()
{
	Playback.SetVolumeMultiplier(VolumeMultiplier);
	Playback.SetPitchMultiplier(PitchMultiplier);

	// Found every tick, as the VAWorld may begin play after this actor
	AVAWorld* audioWorld = AVAWorld::Find(this);
	AVAListener* listener = audioWorld ? audioWorld->GetMainListener() : nullptr;

	// Keep the current send while there's no current listener, e.g. while the listener is switching
	if (!listener)
		return;

	if (!listener->ListenerReverbSubmix && WarnedListener != listener)
	{
		WarnedListener = listener;
		VA_WARN_NAMED(TEXT("Will have no reverb as the listener '%s' has no ListenerReverbSubmix"), *listener->GetActorNameOrLabel());
	}

	// Not muffled, so the send level isn't compensated
	USoundSubmix* submix = listener->ListenerReverbSubmix;
	Playback.SetReverbSend(submix, submix ? 1.0f : 0.0f);
}
