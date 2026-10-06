#include "VASourceRelative.h"
#include "VAListener.h"
#include "VAWorld.h"

#include "Kismet/GameplayStatics.h"

#include "VALog.h"
#include "VASubmixSend.h"

AVASourceRelative::AVASourceRelative()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AVASourceRelative::BeginPlay()
{
	Super::BeginPlay();

	// Disable the actor if validation fails
	if (!AudioWorld)
	{
		VA_WARN_NAMED(TEXT("Will not play as it does not have an AudioWorld assigned"));
		SetActorTickEnabled(false);
		return;
	}

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

	bSourcePendingSpawn = true;
}

void AVASourceRelative::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;
	}
}

void AVASourceRelative::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Resolve reverb first, so the send is set before the sound plays
	UpdateSourceSubmix();

	if (bSourcePendingSpawn)
		TrySpawnSourceSound();
}

void AVASourceRelative::TrySpawnSourceSound()
{
	bSourcePendingSpawn = false;

	USoundBase* chosenSound = SourceSounds[FMath::RandHelper(SourceSounds.Num())];

	// CreateSound2D() builds the component without starting playback, so the reverb send can be set first
	SourceAudioComponent = UGameplayStatics::CreateSound2D(GetWorld(), chosenSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);

	if (SourceAudioComponent)
	{
		if (ReverbSubmix)
			VASetReverbSend(SourceAudioComponent, ReverbSubmix, ReverbSendLevel);

		SourceAudioComponent->Play();
	}
	else
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
	}
}

void AVASourceRelative::UpdateSourceSubmix()
{
	AVAListener* listener = AudioWorld->GetMainListener();

	// Keep the current send while there's no current listener, e.g. while the listener is switching
	if (!listener)
		return;

	if (!listener->ListenerReverbSubmix && WarnedListener != listener)
	{
		WarnedListener = listener;
		VA_WARN_NAMED(TEXT("Will have no reverb as the listener '%s' has no ListenerReverbSubmix"), *listener->GetActorNameOrLabel());
	}

	USoundSubmix* submix = listener->ListenerReverbSubmix;

	// The current listener changed, so silence the old send
	if (ReverbSubmix && ReverbSubmix != submix)
		VASetReverbSend(SourceAudioComponent, ReverbSubmix, 0.0f);

	// Not muffled, so there's no gainLF to compensate for
	ReverbSubmix = submix;
	ReverbSendLevel = submix ? 1.0f : 0.0f;

	if (ReverbSubmix)
		VASetReverbSend(SourceAudioComponent, ReverbSubmix, ReverbSendLevel);
}
