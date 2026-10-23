#include "VASourceAmbient.h"
#include "VAWorld.h"
#include "VAListener.h"
#include "VALog.h"
#include "VAConstants.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"

extern "C" {
#include "vaudio.h"
}

AVASourceAmbient::AVASourceAmbient()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AVASourceAmbient::BeginPlay()
{
	Super::BeginPlay();

	if (!SourceSound)
	{
		VA_WARN_NAMED(TEXT("Will not play as it does not have a sound file assigned"));
		SetActorTickEnabled(false);
		return;
	}

	// Warnings
	if (!SourceSound->IsPlayWhenSilent())
	{
		VA_WARN_NAMED(TEXT("SourceSound '%s' must have Virtualization Mode set to 'Play When Silent', else it may stop playing when fully muffled"), *SourceSound->GetName());
	}

	bSourcePendingSpawn = true;
}

void AVASourceAmbient::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	Playback.Stop();
}

void AVASourceAmbient::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Playback.RemoveFinished();
	Playback.SetVolumeMultiplier(VolumeMultiplier);
	Playback.SetPitchMultiplier(PitchMultiplier);

	// Found every tick, as the VAWorld may begin play after this actor
	AVAWorld* audioWorld = AVAWorld::Find(this);
	AVAListener* listener = audioWorld ? audioWorld->GetMainListener() : nullptr;

	// The listener may not have begun play yet
	if (!listener || !listener->GetVAEmitter())
		return;

	VAEmitter* vaListener = listener->GetVAEmitter();

	if (!checkedListenerRays)
	{
		checkedListenerRays = true;

		if (!vaEmitterGetAmbientOcclusionEnabled(vaListener) && !vaEmitterGetAmbientPermeationEnabled(vaListener))
			VA_WARN_NAMED(TEXT("Will not be muffled as the listener does not cast ambient occlusion or ambient permeation rays"));
	}
	VALowPassFilter* AmbientFilter = vaEmitterGetAmbientFilter(vaListener);

	// Raytracing has not completed yet - don't play the sound
	if (!AmbientFilter)
		return;

	Playback.SetFilter(AmbientFilter->gainLF, AmbientFilter->gainHF);

	if (bSourcePendingSpawn)
		TrySpawnSourceSound();
}

void AVASourceAmbient::TrySpawnSourceSound()
{
	bSourcePendingSpawn = false;

	// Having no audio device (e.g. -nosound) isn't worth a warning
	if (!GetWorld()->GetAudioDeviceRaw())
	{
		VA_LOG_NAMED(TEXT("No audio device, so the source is not heard"));
		return;
	}

	// CreateSound2D() builds the component without starting playback, so we can configure the low pass filter before playing any sound
	UAudioComponent* component = UGameplayStatics::CreateSound2D(GetWorld(), SourceSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);

	if (component)
	{
		Playback.Play(component);
	}
	else
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
	}
}
