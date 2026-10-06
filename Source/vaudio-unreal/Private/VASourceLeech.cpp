#include "VASourceLeech.h"
#include "VAEmitter.h"
#include "VAListener.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"

extern "C" {
#include "vaudio.h"
}

#include "VALog.h"

AVASourceLeech::AVASourceLeech()
{
	PrimaryActorTick.bCanEverTick = true;

	SourceRootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SourceRootComponent);
}

void AVASourceLeech::BeginPlay()
{
	Super::BeginPlay();

	// Disable the actor if validation fails
	if (SourceSounds.Num() == 0)
	{
		VA_WARN_NAMED(TEXT("Has no SourceSounds and will not play sound"));
		SetActorTickEnabled(false);
		return;
	}

	for (int32 i = 0; i < SourceSounds.Num(); i++)
	{
		USoundBase* sound = SourceSounds[i];

		if (!sound)
		{
			VA_WARN_NAMED(TEXT("Will not play as it has a null sound assigned to index %d"), i);
			SetActorTickEnabled(false);
			return;
		}

		if (!sound->IsPlayWhenSilent())
			VA_WARN_NAMED(TEXT("SourceSound '%s' must have Virtualization Mode set to 'Play When Silent', else it may not play correctly when fully muffled"), *sound->GetName());

		if (!sound->AttenuationSettings)
			VA_WARN_NAMED(TEXT("SourceSound '%s' has no Sound Attenuation - it will not fall off with distance"), *sound->GetName());
	}

	// The emitter is resolved in Tick, since this actor may be attached to it after spawning
	bValidConfig = true;
	bAutoPlayPending = bAutoPlay;
}

void AVASourceLeech::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	Playback.Stop();
}

AVAEmitter* AVASourceLeech::GetLeechedEmitter() const
{
	if (AVAEmitter* parent = Cast<AVAEmitter>(GetAttachParentActor()))
		return parent;

	return IsValid(Emitter) ? Emitter : nullptr;
}

bool AVASourceLeech::IsReadyToPlay() const
{
	AVAEmitter* emitter = GetLeechedEmitter();

	// A listener is never ready to play
	return emitter && emitter->IsReadyToPlay();
}

void AVASourceLeech::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Playback.RemoveFinished();

	AVAEmitter* emitter = GetLeechedEmitter();

	// Keeps the last filter and send while there's no emitter, e.g. after it was destroyed
	if (!emitter)
	{
		Playback.SetVolumeMultiplier(VolumeMultiplier);
		Playback.SetPitchMultiplier(PitchMultiplier);

		if (!bWarnedNoEmitter)
		{
			bWarnedNoEmitter = true;
			VA_WARN_NAMED(TEXT("Is not attached to an AVAEmitter and has no Emitter assigned, so it will not play"));
		}

		return;
	}

	// A listener never raytraces itself, so it has no muffling for a leech to read
	if (emitter->IsA<AVAListener>())
	{
		VA_WARN_NAMED(TEXT("Leeches the listener '%s', which has no muffling result, so it will not play. Use a VASourceRelative instead"), *emitter->GetActorNameOrLabel());
		SetActorTickEnabled(false);
		return;
	}

	bWarnedNoEmitter = false;

	UpdatePlayback(emitter);

	// Waits for the emitter's reverb results too, so the sound never starts without reverb
	if (bAutoPlayPending && emitter->IsReadyToPlay())
		Play();
}

void AVASourceLeech::UpdatePlayback(AVAEmitter* emitter)
{
	Playback.SetVolumeMultiplier(VolumeMultiplier);
	Playback.SetPitchMultiplier(PitchMultiplier);

	// Null until the listener has raytraced the emitter, and after a bRaytraceOnce emitter leaves the world. The last result is kept
	if (VALowPassFilter* lowPassFilter = emitter->GetMufflingResult())
		Playback.SetFilter(lowPassFilter->gainLF, lowPassFilter->gainHF);

	USoundSubmix* submix = nullptr;
	float sendLevel = 0.0f;

	// Set after the filter, as the send level is compensated by gainLF
	if (emitter->ResolveReverbSend(submix, sendLevel))
		Playback.SetReverbSend(submix, sendLevel);
}

bool AVASourceLeech::Play()
{
	// Wait for the emitter's muffling and reverb results, so the sound never starts unmuffled or without reverb
	if (!bValidConfig || !IsReadyToPlay())
		return false;

	bAutoPlayPending = false;

	// Play() may be called before this tick's update
	UpdatePlayback(GetLeechedEmitter());

	USoundBase* chosenSound = SourceSounds[FMath::RandHelper(SourceSounds.Num())];

	FAudioDevice::FCreateComponentParams Params(GetWorld(), this);
	Params.SetLocation(GetActorLocation());

	UAudioComponent* component = FAudioDevice::CreateComponent(chosenSound, Params);

	if (!component)
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
		return false;
	}

	component->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	component->bAllowSpatialization = true;

	Playback.Play(component);
	return true;
}

void AVASourceLeech::Stop()
{
	Playback.Stop();
}
