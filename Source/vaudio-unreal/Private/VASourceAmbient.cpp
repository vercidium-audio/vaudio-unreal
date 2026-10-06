#include "VASourceAmbient.h"
#include "VAWorld.h"
#include "VAListener.h"
#include "VALog.h"
#include "VAConstants.h"

#include "Kismet/GameplayStatics.h"

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

	// Disable the actor if validation fails
	if (!AudioWorld)
	{
		VA_WARN_NAMED(TEXT("Will not play as it does not have an AudioWorld assigned"));
		SetActorTickEnabled(false);
		return;
	}

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

	Filter.Initialize(this);
	bSourcePendingSpawn = true;
}

void AVASourceAmbient::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (SourceAudioComponent)
	{
		SourceAudioComponent->Stop();
		SourceAudioComponent = nullptr;
	}
}

void AVASourceAmbient::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AVAListener* listener = AudioWorld->GetMainListener();

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

	Filter.Apply(SourceAudioComponent, AmbientFilter->gainLF, AmbientFilter->gainHF);

	if (bSourcePendingSpawn)
		TrySpawnSourceSound();
}

void AVASourceAmbient::TrySpawnSourceSound()
{
	bSourcePendingSpawn = false;

	// CreateSound2D() builds the component without starting playback, so we can configure the low pass filter before playing any sound
	SourceAudioComponent = UGameplayStatics::CreateSound2D(GetWorld(), SourceSound, 1.0f, 1.0f, 0.0f, nullptr, false, true);

	if (SourceAudioComponent)
	{
		Filter.Attach(SourceAudioComponent);
		SourceAudioComponent->Play();
	}
	else
	{
		VA_WARN_NAMED(TEXT("Play failed. Check if this actor was correctly spawned, or if the Unreal World allows audio playback"));
	}
}
